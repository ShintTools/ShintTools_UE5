// Copyright 2026 ShintTools. All Rights Reserved.
//
// LOD Auditor endpoint (Studio tier) — split out of ShintCoreClient.cpp.
//
// Gathers per-asset metadata via the editor APIs (mesh LOD chains + triangle
// counts, texture dimensions + compression, material slot counts), POSTs them
// to /assets/lod/audit, and parses the Finding list back. Mirrors the request
// shape the core LodAssetFile model accepts (asset_path + per-category fields).

#include "ShintCoreClient.h"
#include "ShintTools.h"

#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"

#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"

#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshSourceData.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstance.h"
#include "Materials/Material.h"

// Contract v2 fast-scan sources (Studio tier):
#include "StaticMeshResources.h"        // FStaticMeshRenderData / FStaticMeshLODResources
#include "PhysicsEngine/BodySetup.h"    // UBodySetup, FKAggregateGeom, CollisionTraceFlag

// Material analysis (§20.3) — graph walks + compiled-stats instruction counts.
#include "MaterialEditingLibrary.h"                           // GetStatistics (exported)
#include "Materials/MaterialExpression.h"
#include "Materials/MaterialExpressionTextureBase.h"          // texture_samples (LM002)
#include "Materials/MaterialExpressionStaticSwitchParameter.h" // static_switch_count (LM011)

// ─────────────────────────────────────────────────────────────────────────────
// Metadata extractors — one per asset family. Each fills *Obj in place and
// returns true if the asset was understood (so the caller knows to send it).
// All run on the game thread inside AuditLods; loading is synchronous via
// FAssetData::GetAsset(), matching how the asset-naming scan already resolves
// classes. Assets that fail to load are skipped, never aborting the batch.
// ─────────────────────────────────────────────────────────────────────────────

namespace
{
	// Map UE2 TextureCompressionSettings enum to the TC_* string the core's
	// vram_model.normalize_compression() understands. Only the formats the
	// rules care about are mapped; anything else passes through as the raw
	// enum name and the server falls back to RGBA8 sizing.
	FString CompressionToString(TextureCompressionSettings TC)
	{
		switch (TC)
		{
			case TC_Default:        return TEXT("TC_Default");
			case TC_Normalmap:      return TEXT("TC_Normalmap");
			case TC_Masks:          return TEXT("TC_Masks");
			case TC_Grayscale:      return TEXT("TC_Grayscale");
			case TC_HDR:            return TEXT("TC_HDR");
			case TC_HDR_Compressed: return TEXT("TC_HDR_Compressed");
			case TC_BC7:            return TEXT("TC_BC7");
			case TC_EditorIcon:     return TEXT("RGBA8");
			case TC_VectorDisplacementmap: return TEXT("RGBA8");
			default:                return TEXT("TC_Default");
		}
	}

	// EBlendMode -> the taxonomy string the core rules read
	// (Opaque / Masked / Translucent / Additive / Modulate). Used both for the
	// material's own blend_mode and the mesh's used_material_blend_modes list
	// (LD012/LD013 Nanite gate). Unknown modes fall back to Opaque.
	FString BlendModeToString(EBlendMode Blend)
	{
		switch (Blend)
		{
			case BLEND_Opaque:      return TEXT("Opaque");
			case BLEND_Masked:      return TEXT("Masked");
			case BLEND_Translucent: return TEXT("Translucent");
			case BLEND_Additive:    return TEXT("Additive");
			case BLEND_Modulate:    return TEXT("Modulate");
			case BLEND_AlphaComposite: return TEXT("Translucent");
			case BLEND_AlphaHoldout:   return TEXT("Translucent");
			default:                return TEXT("Opaque");
		}
	}

	// Best-effort semantic usage bucket (BaseColor / Normal / Mask / HDR / UI /
	// Data) inferred from compression + sRGB + texture group. UE stores no
	// explicit semantic usage, so this is a field filler for LT009/LT010/LT013/
	// LT016; when the guess is wrong the affected rule simply abstains.
	FString InferTextureUsage(UTexture2D* Tex)
	{
		if (Tex->LODGroup == TEXTUREGROUP_UI)          return TEXT("UI");
		switch (Tex->CompressionSettings)
		{
			case TC_Normalmap:      return TEXT("Normal");
			case TC_Masks:
			case TC_Grayscale:
			case TC_Alpha:          return TEXT("Mask");
			case TC_HDR:
			case TC_HDR_Compressed: return TEXT("HDR");
			default: break;
		}
		return Tex->SRGB ? TEXT("BaseColor") : TEXT("Data");
	}

	// Per-asset display metadata retained from the collection pass and joined
	// onto findings client-side (the server findings don't echo width/height/
	// group/format back). Drives the Asset Optimizer table columns. ResText is
	// the per-family RESOLUTION cell: "2048x2048" stays derived from W/H for
	// textures; meshes carry "12,345 tris" and materials "140 instr" here.
	struct FLodAssetMeta
	{
		int32   Width  = 0;
		int32   Height = 0;
		FString Group;
		FString Format;
		FString ResText;
	};

	// Display cleanup: "TC_BC7" -> "BC7", "TEXTUREGROUP_World" -> "World".
	FString StripPrefix(const FString& In, const TCHAR* Prefix)
	{
		return In.StartsWith(Prefix) ? In.RightChop(FCString::Strlen(Prefix)) : In;
	}

	bool ExtractStaticMesh(UStaticMesh* Mesh, const TSharedRef<FJsonObject>& Obj)
	{
		if (!Mesh) return false;

		const int32 NumLods = Mesh->GetNumLODs();
		Obj->SetStringField(TEXT("asset_type"), TEXT("StaticMesh"));
		Obj->SetNumberField(TEXT("lod_count"), NumLods);

		// Render data carries the cooked triangle/vertex counts per LOD. It can
		// be null for a freshly-imported mesh whose build hasn't finished; guard
		// so a half-built asset never crashes the scan (it just reports lod_count
		// without per-LOD triangle detail, and LD001 still fires on lod_count<2).
		FStaticMeshRenderData* RD = Mesh->GetRenderData();
		const bool bHasRenderData = RD != nullptr;

		TArray<TSharedPtr<FJsonValue>> Lods;
		for (int32 i = 0; i < NumLods; ++i)
		{
			TSharedRef<FJsonObject> L = MakeShared<FJsonObject>();
			L->SetNumberField(TEXT("index"), i);
			if (bHasRenderData)
			{
				// GetNumTriangles/Vertices are public UStaticMesh APIs (no extra
				// RenderCore module dep) and read the same render data.
				L->SetNumberField(TEXT("triangles"), Mesh->GetNumTriangles(i));
				L->SetNumberField(TEXT("vertices"),  Mesh->GetNumVertices(i));

				// Contract v2 per-LOD detail: draw sections (LD008) + UV channels
				// (LD009). Read straight off the LOD render resources.
				if (RD->LODResources.IsValidIndex(i))
				{
					const FStaticMeshLODResources& LR = RD->LODResources[i];
					L->SetNumberField(TEXT("section_count"), LR.Sections.Num());
					L->SetNumberField(TEXT("uv_channel_count"),
						LR.VertexBuffers.StaticMeshVertexBuffer.GetNumTexCoords());
				}
			}
			// Screen size per LOD: source-imported value when available.
			float ScreenSize = 0.f;
			if (Mesh->IsSourceModelValid(i))
			{
				ScreenSize = Mesh->GetSourceModel(i).ScreenSize.Default;
			}
			L->SetNumberField(TEXT("screen_size"), ScreenSize);
			Lods.Add(MakeShared<FJsonValueObject>(L));
		}
		Obj->SetArrayField(TEXT("lods"), Lods);

		Obj->SetNumberField(TEXT("bounds_radius"),
			Mesh->GetBounds().SphereRadius);
		Obj->SetNumberField(TEXT("material_slot_count"),
			Mesh->GetStaticMaterials().Num());

		// ── Contract v2 fast-scan geometry / chain fields ──────────────────────
		// LOD0 totals (LG001/LG004/LG009 + LG002/LG003/LG005). Falls back to the
		// per-LOD detail already sent, so a rule reads whichever it prefers.
		if (bHasRenderData)
		{
			Obj->SetNumberField(TEXT("triangle_count"), Mesh->GetNumTriangles(0));
			Obj->SetNumberField(TEXT("vertex_count"),   Mesh->GetNumVertices(0));
			if (RD->LODResources.IsValidIndex(0))
			{
				const FStaticMeshLODResources& L0 = RD->LODResources[0];
				Obj->SetNumberField(TEXT("section_count"), L0.Sections.Num());
				Obj->SetNumberField(TEXT("uv_channel_count"),
					L0.VertexBuffers.StaticMeshVertexBuffer.GetNumTexCoords());
			}
		}

		// Lightmap UV channel (LW002/LW006/LW009/LW010, LG007/LG008 static-light
		// legs). -1 when the mesh has no dedicated lightmap UV.
		Obj->SetNumberField(TEXT("lightmap_uv_index"),
			Mesh->GetLightMapCoordinateIndex());

		// Nanite (LD012/LD013 + the LG Nanite-abstain legs). FallbackPercentTriangles
		// is a 0–1 fraction; the contract carries a percentage.
#if WITH_EDITORONLY_DATA
		Obj->SetBoolField(TEXT("nanite_enabled"), Mesh->GetNaniteSettings().bEnabled);
		Obj->SetNumberField(TEXT("nanite_fallback_triangle_percent"),
			Mesh->GetNaniteSettings().FallbackPercentTriangles * 100.0);

		// Import scale (LG014). BuildScale3D is the closest fast-scan proxy for
		// the FBX import scale; a non-uniform build scale is the LG014 warning.
		if (Mesh->IsSourceModelValid(0))
		{
			const FVector S = Mesh->GetSourceModel(0).BuildSettings.BuildScale3D;
			Obj->SetNumberField(TEXT("import_uniform_scale"), S.X);
			const bool bNonUniform =
				!FMath::IsNearlyEqual(S.X, S.Y) || !FMath::IsNearlyEqual(S.X, S.Z);
			Obj->SetBoolField(TEXT("import_scale_nonuniform"), bNonUniform);
		}
#else
		Obj->SetBoolField(TEXT("nanite_enabled"), Mesh->IsNaniteEnabled());
#endif

		// Material slots' blend modes — LD012/LD013 abstain without this list.
		TArray<TSharedPtr<FJsonValue>> Blends;
		for (const FStaticMaterial& SMat : Mesh->GetStaticMaterials())
		{
			if (SMat.MaterialInterface)
			{
				Blends.Add(MakeShared<FJsonValueString>(
					BlendModeToString(SMat.MaterialInterface->GetBlendMode())));
			}
		}
		Obj->SetArrayField(TEXT("used_material_blend_modes"), Blends);

		// Collision (LD011). AggGeom primitive count + complex-as-simple flag.
		if (UBodySetup* BS = Mesh->GetBodySetup())
		{
			const FKAggregateGeom& Agg = BS->AggGeom;
			const int32 PrimCount =
				Agg.BoxElems.Num() + Agg.SphereElems.Num() +
				Agg.SphylElems.Num() + Agg.ConvexElems.Num() +
				Agg.TaperedCapsuleElems.Num();
			TSharedRef<FJsonObject> Col = MakeShared<FJsonObject>();
			Col->SetBoolField(TEXT("has_simple_collision"), PrimCount > 0);
			Col->SetNumberField(TEXT("primitive_count"), PrimCount);
			Col->SetBoolField(TEXT("complex_as_simple"),
				BS->CollisionTraceFlag == CTF_UseComplexAsSimple);
			Col->SetNumberField(TEXT("complex_triangles"),
				bHasRenderData ? Mesh->GetNumTriangles(0) : 0);
			Obj->SetObjectField(TEXT("collision"), Col);
		}
		return true;
	}

	bool ExtractTexture(UTexture2D* Tex, const TSharedRef<FJsonObject>& Obj)
	{
		if (!Tex) return false;
		Obj->SetStringField(TEXT("asset_type"), TEXT("Texture2D"));
		Obj->SetNumberField(TEXT("width"),  Tex->GetSizeX());
		Obj->SetNumberField(TEXT("height"), Tex->GetSizeY());
		Obj->SetStringField(TEXT("compression"),
			CompressionToString(Tex->CompressionSettings));
		Obj->SetBoolField(TEXT("srgb"), Tex->SRGB);
		Obj->SetBoolField(TEXT("mips_enabled"),
			Tex->MipGenSettings != TMGS_NoMipmaps);
		Obj->SetBoolField(TEXT("streaming"), !Tex->NeverStream);
		Obj->SetStringField(TEXT("lod_group"),
			StaticEnum<TextureGroup>()
				? StaticEnum<TextureGroup>()->GetNameStringByValue(Tex->LODGroup)
				: TEXT("World"));

		// ── Contract v2 fast-scan texture fields ───────────────────────────────
		Obj->SetNumberField(TEXT("mip_count"), Tex->GetNumMips());   // LT009
		Obj->SetStringField(TEXT("usage"), InferTextureUsage(Tex));  // LT009/10/13/16
		Obj->SetNumberField(TEXT("size_kb"),                          // LT015 pool
			(double)Tex->CalcTextureMemorySizeEnum(TMC_AllMips) / 1024.0);
		return true;
	}

	bool ExtractMaterial(UMaterialInterface* Mat, const TSharedRef<FJsonObject>& Obj,
		IAssetRegistry& AR, const FString& PackageName)
	{
		if (!Mat) return false;
		const bool bIsInstance = Mat->IsA<UMaterialInstance>();
		Obj->SetStringField(TEXT("asset_type"),
			bIsInstance ? TEXT("MaterialInstance") : TEXT("Material"));
		Obj->SetBoolField(TEXT("is_material_instance"), bIsInstance);
		// Texture sampler count — the unique textures the material references
		// (all quality levels / shader platforms; the 5.7 default overload).
		TArray<UTexture*> Textures;
		Mat->GetUsedTextures(Textures);
		Obj->SetNumberField(TEXT("sampler_count"), Textures.Num());
		Obj->SetStringField(TEXT("blend_mode"), BlendModeToString(Mat->GetBlendMode()));

		// ── Contract v2 trivial material flags (no graph walk / shader compile) ──
		Obj->SetBoolField(TEXT("two_sided"), Mat->IsTwoSided());          // LR006
		if (const UMaterial* Base = Mat->GetMaterial())
		{
			Obj->SetBoolField(TEXT("is_decal"),                          // LR005
				Base->MaterialDomain == MD_DeferredDecal);
		}
		const EMaterialShadingModel SM =
			Mat->GetShadingModels().GetFirstShadingModel();
		Obj->SetStringField(TEXT("shading_model"),
			StaticEnum<EMaterialShadingModel>()
				? StaticEnum<EMaterialShadingModel>()->GetNameStringByValue(SM)
				: TEXT("MSM_DefaultLit"));

		// ── used_by_primitives (LM003, LM009, LR002/LR006/LR008) ────────────────
		// Registry-only referencer count (§20.1 "reference analysis") — hard
		// package refs pointing at this material. An approximation of primitive
		// consumers (levels + meshes + blueprints), but the rules only need an
		// order-of-magnitude gate. Without this every LR rule abstains, which is
		// why the Materials tab used to come back empty.
		{
			TArray<FName> Referencers;
			AR.GetReferencers(FName(*PackageName), Referencers,
				UE::AssetRegistry::EDependencyCategory::Package,
				UE::AssetRegistry::EDependencyQuery::Hard);
			Obj->SetNumberField(TEXT("used_by_primitives"), Referencers.Num());
		}

		// ── Graph stats (master materials only) ─────────────────────────────────
		// Instances resolve GetMaterial() to their root parent — attributing the
		// parent graph's stats to every instance would fire duplicate findings on
		// each instance, so instances skip the graph-derived fields entirely.
		if (!bIsInstance)
		{
			if (UMaterial* Base = Mat->GetMaterial())
			{
				int32 NodeCount = 0, SwitchCount = 0;
				TArray<TSharedPtr<FJsonValue>> Samples;   // texture_samples (LM002)
				for (UMaterialExpression* E : Base->GetExpressions())
				{
					if (!E) continue;
					++NodeCount;
					if (E->IsA<UMaterialExpressionStaticSwitchParameter>())
						++SwitchCount;
					if (const UMaterialExpressionTextureBase* TexNode =
							Cast<UMaterialExpressionTextureBase>(E))
					{
						if (TexNode->Texture)
						{
							TSharedRef<FJsonObject> S = MakeShared<FJsonObject>();
							S->SetStringField(TEXT("texture"),
								TexNode->Texture->GetPathName());
							Samples.Add(MakeShared<FJsonValueObject>(S));
						}
					}
				}
				Obj->SetNumberField(TEXT("graph_node_count"), NodeCount);      // LM005
				Obj->SetNumberField(TEXT("static_switch_count"), SwitchCount); // LM011
				if (SwitchCount > 0)
					Obj->SetNumberField(TEXT("static_permutation_estimate"),
						1 << FMath::Min(SwitchCount, 20));
				if (Samples.Num() > 0)
					Obj->SetArrayField(TEXT("texture_samples"), Samples);      // LM002
			}
		}

		// ── layer_count (LM007/LM008) ───────────────────────────────────────────
		{
			FMaterialLayersFunctions Layers;
			if (Mat->GetMaterialLayers(Layers) && Layers.Layers.Num() > 0)
				Obj->SetNumberField(TEXT("layer_count"), Layers.Layers.Num());
		}

		// ── Instruction counts (LM001, LM008, LM014, LR005) ─────────────────────
		// Compiled-shader statistics via the exported Material Editor library
		// (the same numbers the editor's stats panel shows).
		// FMaterialStatsUtils::GetRepresentativeInstructionCounts is NOT
		// MATERIALEDITOR_API-exported — calling it fails to link. Best-effort: an
		// uncompiled material yields zeros and the fields stay absent → abstain.
		{
			const FMaterialStatistics Stats =
				UMaterialEditingLibrary::GetStatistics(Mat);
			if (Stats.NumPixelShaderInstructions > 0)
			{
				Obj->SetNumberField(TEXT("instruction_count"),
					Stats.NumPixelShaderInstructions);                          // LM001
				Obj->SetNumberField(TEXT("base_pass_instructions"),
					Stats.NumPixelShaderInstructions);                          // LM008
			}
			if (Stats.NumVertexShaderInstructions > 0)
				Obj->SetNumberField(TEXT("vertex_shader_instructions"),
					Stats.NumVertexShaderInstructions);                         // LM014
		}
		return true;
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// AuditLods — gather + POST
// ─────────────────────────────────────────────────────────────────────────────

void FShintCoreClient::AuditLods(
	const FString& Profile, bool bExplainTop, FOnShintLodAuditComplete OnComplete)
{
	const double BenchStart = FPlatformTime::Seconds();
	IAssetRegistry& AR =
		FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();

	FARFilter Filter;
	Filter.PackagePaths.Add(TEXT("/Game"));
	Filter.bRecursivePaths = true;
	// Only the families the LOD rules audit — avoids loading every Blueprint
	// and data asset in the project just to skip it.
	Filter.ClassPaths.Add(UStaticMesh::StaticClass()->GetClassPathName());
	Filter.ClassPaths.Add(USkeletalMesh::StaticClass()->GetClassPathName());
	Filter.ClassPaths.Add(UTexture2D::StaticClass()->GetClassPathName());
	Filter.ClassPaths.Add(UMaterialInterface::StaticClass()->GetClassPathName());
	Filter.bRecursiveClasses = true;

	TArray<FAssetData> AllAssets;
	AR.GetAssets(Filter, AllAssets);

	TArray<TSharedPtr<FJsonValue>> Arr;
	TMap<FString, FLodAssetMeta>   MetaByPath;   // joined onto findings post-parse
	int64 TotalVramBytes = 0;                    // resident texture VRAM (KPI tile)

	for (const FAssetData& AD : AllAssets)
	{
		TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
		const FString AssetPath = AD.PackageName.ToString();
		Obj->SetStringField(TEXT("asset_path"), AssetPath);

		UObject* Loaded = AD.GetAsset();
		bool bUnderstood = false;
		if (UStaticMesh* SM = Cast<UStaticMesh>(Loaded))
		{
			bUnderstood = ExtractStaticMesh(SM, Obj);
			if (bUnderstood)
			{
				// Mesh columns: GROUP = mesh family, FORMAT = render path,
				// RESOLUTION = LOD0 triangle count.
				FLodAssetMeta Meta;
				Meta.Group = TEXT("Static");
				const int32 NumLods = SM->GetNumLODs();
				const bool bNanite =
#if WITH_EDITORONLY_DATA
					SM->GetNaniteSettings().bEnabled;
#else
					SM->IsNaniteEnabled();
#endif
				Meta.Format = bNanite ? TEXT("Nanite")
					: FString::Printf(TEXT("LOD x%d"), NumLods);
				if (SM->GetRenderData())
					Meta.ResText = FString::Printf(TEXT("%s tris"),
						*FText::AsNumber(SM->GetNumTriangles(0)).ToString());
				MetaByPath.Add(AssetPath, MoveTemp(Meta));
			}
		}
		else if (UTexture2D* T = Cast<UTexture2D>(Loaded))
		{
			bUnderstood = ExtractTexture(T, Obj);
			if (bUnderstood)
			{
				TotalVramBytes += (int64)T->CalcTextureMemorySizeEnum(TMC_AllMips);
				FLodAssetMeta Meta;
				Meta.Width  = T->GetSizeX();
				Meta.Height = T->GetSizeY();
				Meta.Group  = StaticEnum<TextureGroup>()
					? StripPrefix(StaticEnum<TextureGroup>()
						->GetNameStringByValue(T->LODGroup), TEXT("TEXTUREGROUP_"))
					: TEXT("World");
				Meta.Format = StripPrefix(
					CompressionToString(T->CompressionSettings), TEXT("TC_"));
				MetaByPath.Add(AssetPath, MoveTemp(Meta));
			}
		}
		else if (UMaterialInterface* M = Cast<UMaterialInterface>(Loaded))
		{
			bUnderstood = ExtractMaterial(M, Obj, AR, AssetPath);
			if (bUnderstood)
			{
				// Material columns: GROUP = Master/Instance, FORMAT = blend mode,
				// RESOLUTION = compiled instruction count when available.
				FLodAssetMeta Meta;
				Meta.Group  = M->IsA<UMaterialInstance>()
					? TEXT("Instance") : TEXT("Master");
				Meta.Format = BlendModeToString(M->GetBlendMode());
				double Instr = 0.0;
				if (Obj->TryGetNumberField(TEXT("instruction_count"), Instr) && Instr > 0.0)
					Meta.ResText = FString::Printf(TEXT("%d instr"), (int32)Instr);
				MetaByPath.Add(AssetPath, MoveTemp(Meta));
			}
		}
		// SkeletalMesh: send the bare path + type so LA002 (no-LOD) can fire;
		// detailed skeletal metadata extraction is a follow-up.
		else if (Loaded && Loaded->IsA<USkeletalMesh>())
		{
			Obj->SetStringField(TEXT("asset_type"), TEXT("SkeletalMesh"));
			bUnderstood = true;
			FLodAssetMeta Meta;
			Meta.Group = TEXT("Skeletal");
			MetaByPath.Add(AssetPath, MoveTemp(Meta));
		}

		if (bUnderstood)
		{
			Arr.Add(MakeShared<FJsonValueObject>(Obj));
		}
	}

	// Per-category file counts for the KPI breakdowns (textures vs meshes).
	int32 NumTextures = 0, NumMeshes = 0, NumMaterials = 0;
	for (const TSharedPtr<FJsonValue>& V : Arr)
	{
		const TSharedPtr<FJsonObject> O = V->AsObject();
		FString T;
		if (O.IsValid()) O->TryGetStringField(TEXT("asset_type"), T);
		if (T == TEXT("Texture2D")) ++NumTextures;
		else if (T == TEXT("StaticMesh") || T == TEXT("SkeletalMesh")) ++NumMeshes;
		else if (T == TEXT("Material") || T == TEXT("MaterialInstance")) ++NumMaterials;
	}

	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("project_name"), Config.ProjectName);
	Body->SetStringField(TEXT("engine"),       TEXT("unreal"));
	Body->SetStringField(TEXT("api_key"),      Config.ApiKeyMongo);
	Body->SetStringField(TEXT("profile"),      Profile.IsEmpty() ? TEXT("default") : Profile);
	Body->SetBoolField(TEXT("explain"),        bExplainTop);
	Body->SetNumberField(TEXT("max_explanations"), 5);
	Body->SetArrayField(TEXT("assets"),        Arr);

	const FString BodyStr = SerializeJson(Body);
	const int32 SentAssets = Arr.Num();

	UE_LOG(LogShintTools, Verbose,
		TEXT("ShintCoreClient: LOD audit of %d assets (profile=%s explain=%s)"),
		SentAssets, *Profile, bExplainTop ? TEXT("true") : TEXT("false"));

	SendRequest(Config.GetBaseUrl() + TEXT("/assets/lod/audit"),
		EShintHttpMethod::POST, BodyStr,
		FOnShintRequestComplete::CreateLambda(
			[OnComplete, BenchStart, SentAssets, NumTextures, NumMeshes,
			 NumMaterials, TotalVramBytes, MetaByPath = MoveTemp(MetaByPath)]
			(const FShintRequestResult& Raw) mutable
		{
			FShintLodAuditResult R = ParseLodAuditResponse(Raw);
			R.TexturesAudited  = NumTextures;
			R.MeshesAudited    = NumMeshes;
			R.MaterialsAudited = NumMaterials;
			R.TotalVramMb      = (double)TotalVramBytes / (1024.0 * 1024.0);

			// Join the collection-pass metadata onto each finding so the Asset
			// Optimizer table has resolution / group / format without a server
			// round-trip. Parse already filled Format from current.compression
			// when present; only fall back to the collected value if it didn't.
			for (FShintLodFinding& F : R.Findings)
			{
				if (const FLodAssetMeta* M = MetaByPath.Find(F.AssetPath))
				{
					F.Width  = M->Width;
					F.Height = M->Height;
					F.ResText = M->ResText;
					if (F.Group.IsEmpty())  F.Group  = M->Group;
					if (F.Format.IsEmpty()) F.Format = M->Format;
				}
			}

			UE_LOG(LogShintTools, Verbose,
				TEXT("[BENCH] AuditLods: %.2f s, %d assets sent, %d findings"),
				FPlatformTime::Seconds() - BenchStart, SentAssets, R.Findings.Num());
			OnComplete.ExecuteIfBound(R);
		}));
}

// ─────────────────────────────────────────────────────────────────────────────
// ParseLodAuditResponse — JSON -> FShintLodAuditResult
// ─────────────────────────────────────────────────────────────────────────────

FShintLodAuditResult FShintCoreClient::ParseLodAuditResponse(
	const FShintRequestResult& Raw)
{
	FShintLodAuditResult Out;
	Out.StatusCode = Raw.StatusCode;

	if (!Raw.bSuccess)
	{
		Out.bSuccess     = false;
		Out.ErrorMessage = Raw.ErrorMessage;
		return Out;
	}

	TSharedPtr<FJsonObject> Root;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Raw.ResponseBody);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		Out.bSuccess     = false;
		Out.ErrorMessage = TEXT("Malformed LOD audit response.");
		return Out;
	}

	// A non-empty "error" field means the server ran but the audit failed.
	FString ServerError;
	if (Root->TryGetStringField(TEXT("error"), ServerError) && !ServerError.IsEmpty())
	{
		Out.bSuccess     = false;
		Out.ErrorMessage = ServerError;
		return Out;
	}

	Out.bSuccess = true;

	const TSharedPtr<FJsonObject>* Summary;
	if (Root->TryGetObjectField(TEXT("summary"), Summary) && Summary->IsValid())
	{
		(*Summary)->TryGetNumberField(TEXT("assets_audited"), Out.AssetsAudited);
		(*Summary)->TryGetNumberField(TEXT("issues_found"),   Out.IssuesFound);
		(*Summary)->TryGetNumberField(TEXT("auto_fixable"),   Out.AutoFixable);
		(*Summary)->TryGetNumberField(
			TEXT("estimated_vram_saved_mb"), Out.EstimatedVramSavedMb);
		(*Summary)->TryGetNumberField(
			TEXT("estimated_shader_instructions_saved"),
			Out.EstimatedShaderInstructionsSaved);
	}

	const TArray<TSharedPtr<FJsonValue>>* Results;
	if (Root->TryGetArrayField(TEXT("results"), Results))
	{
		for (const TSharedPtr<FJsonValue>& V : *Results)
		{
			const TSharedPtr<FJsonObject> F = V->AsObject();
			if (!F.IsValid()) continue;

			FShintLodFinding Finding;
			F->TryGetStringField(TEXT("asset_path"),  Finding.AssetPath);
			F->TryGetStringField(TEXT("rule_id"),     Finding.RuleId);
			F->TryGetStringField(TEXT("rule_name"),   Finding.RuleName);
			F->TryGetStringField(TEXT("category"),    Finding.Category);
			F->TryGetStringField(TEXT("severity"),    Finding.Severity);
			F->TryGetStringField(TEXT("message"),     Finding.Message);
			F->TryGetStringField(TEXT("guidance"),    Finding.Guidance);
			F->TryGetStringField(TEXT("ai_guidance"), Finding.AiGuidance);
			F->TryGetBoolField(TEXT("auto_fixable"),  Finding.bAutoFixable);

			const TSharedPtr<FJsonObject>* Saving;
			if (F->TryGetObjectField(TEXT("estimated_saving"), Saving) &&
			    Saving->IsValid())
			{
				(*Saving)->TryGetNumberField(TEXT("vram_mb"), Finding.VramMb);
				(*Saving)->TryGetNumberField(
					TEXT("shader_instructions"), Finding.ShaderInstructions);
			}

			// current/recommended carry vram_mb + compression for the size-
			// changing rules — drives the table's CURRENT/POTENTIAL/FORMAT
			// columns. Absent for non-size rules (left 0 / empty → shown as "—").
			const TSharedPtr<FJsonObject>* Current;
			if (F->TryGetObjectField(TEXT("current"), Current) && Current->IsValid())
			{
				(*Current)->TryGetNumberField(TEXT("vram_mb"), Finding.CurrentVramMb);
				(*Current)->TryGetStringField(TEXT("compression"), Finding.Format);
			}
			const TSharedPtr<FJsonObject>* Recommended;
			if (F->TryGetObjectField(TEXT("recommended"), Recommended) &&
			    Recommended->IsValid())
			{
				(*Recommended)->TryGetNumberField(
					TEXT("vram_mb"), Finding.PotentialVramMb);
				(*Recommended)->TryGetNumberField(
					TEXT("max_texture_size"), Finding.RecMaxSize);
				(*Recommended)->TryGetStringField(
					TEXT("compression"), Finding.RecCompression);
			}

			Out.Findings.Add(MoveTemp(Finding));
		}
	}

	return Out;
}
