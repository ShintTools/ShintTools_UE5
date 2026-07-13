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

// Deep Scan (§20.2) — mesh-description geometry integrity + normal stats.
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"
#include "Serialization/JsonWriter.h"   // scan-cache persistence
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

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

	// Copy every field of Src onto Dst (shallow — the JSON values are shared
	// pointers). Used to merge cached/computed Deep Scan fields onto the payload.
	void MergeJsonFields(const TSharedRef<FJsonObject>& Dst,
		const TSharedRef<FJsonObject>& Src)
	{
		for (const TPair<FString, TSharedPtr<FJsonValue>>& It : Src->Values)
			Dst->SetField(It.Key, It.Value);
	}

	// ── Deep Scan cache (§20.2) ──────────────────────────────────────────────
	// Persists computed Deep Scan fields per asset in
	// Saved/ShintTools/lod_scan_cache.json, keyed by the render data's
	// DerivedDataKey (the DDC content hash — changes whenever the source mesh or
	// its build settings change). An unchanged mesh skips the FMeshDescription
	// load + recompute entirely, which is what makes a repeat Deep Scan cheap.
	// Best-effort: any I/O or parse failure degrades to "always recompute".
	class FLodScanCache
	{
	public:
		explicit FLodScanCache(FString InPath) : Path(MoveTemp(InPath)) {}

		void Load()
		{
			FString Text;
			if (!FFileHelper::LoadFileToString(Text, *Path)) return;
			const TSharedRef<TJsonReader<>> Reader =
				TJsonReaderFactory<>::Create(Text);
			FJsonSerializer::Deserialize(Reader, Root);
			if (!Root.IsValid()) Root = MakeShared<FJsonObject>();
		}

		// Cached deep-scan fields when the stored DDC key matches; else null.
		TSharedPtr<FJsonObject> Get(
			const FString& AssetPath, const FString& DdcKey) const
		{
			if (!Root.IsValid() || DdcKey.IsEmpty()) return nullptr;
			const TSharedPtr<FJsonObject>* Entry = nullptr;
			if (!Root->TryGetObjectField(AssetPath, Entry) || !Entry) return nullptr;
			FString StoredKey;
			if (!(*Entry)->TryGetStringField(TEXT("ddc_key"), StoredKey)
				|| StoredKey != DdcKey)
				return nullptr;
			const TSharedPtr<FJsonObject>* Fields = nullptr;
			if (!(*Entry)->TryGetObjectField(TEXT("fields"), Fields) || !Fields)
				return nullptr;
			return *Fields;
		}

		void Put(const FString& AssetPath, const FString& DdcKey,
			const TSharedRef<FJsonObject>& Fields)
		{
			if (DdcKey.IsEmpty()) return;
			if (!Root.IsValid()) Root = MakeShared<FJsonObject>();
			TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("ddc_key"), DdcKey);
			Entry->SetObjectField(TEXT("fields"), Fields);
			Root->SetObjectField(AssetPath, Entry);
			bDirty = true;
		}

		void Save()
		{
			if (!bDirty || !Root.IsValid()) return;
			FString Out;
			const TSharedRef<TJsonWriter<>> Writer =
				TJsonWriterFactory<>::Create(&Out);
			if (FJsonSerializer::Serialize(Root.ToSharedRef(), Writer))
				FFileHelper::SaveStringToFile(Out, *Path);
		}

	private:
		FString Path;
		TSharedPtr<FJsonObject> Root;
		bool bDirty = false;
	};

	// ── Deep Scan (§20.2) ────────────────────────────────────────────────────
	// Loads the source FMeshDescription (editor-only) and computes the
	// geometry-integrity + normal-attribute fields the fast scan can't see:
	// degenerate/duplicate/overlapping verts, non-manifold/open edges, and the
	// normal_stats sub-object. Fills the fields the LG004/005/006/007/008 and
	// LN001–LN006 rules read. Absent when there is no source mesh description
	// (cooked-only asset) — the rules then abstain, exactly as before.
	//
	// Deliberately NOT computed this round (expensive; the rules abstain until a
	// later phase supplies them): internal_face_ratio (LG009 hull ray-cast) and
	// the per-UV-channel overlap/stretch/texel-density stats (LW islands).
	//
	// Quantise-grid duplicate/overlap detection: a fine grid (1e-3 cm cell)
	// counts weldable exact duplicates; a coarse grid (0.05 cm) counts
	// near-coincident verts, and overlapping = coarse-near minus fine-duplicate.
	// Cell hashing is O(n); the min-count thresholds on LG005/LG006 absorb the
	// rare cross-cell-boundary miscount.
	void DeepScanStaticMesh(UStaticMesh* Mesh, const TSharedRef<FJsonObject>& Obj)
	{
		if (!Mesh) return;
		const FMeshDescription* MD = Mesh->GetMeshDescription(0);
		if (!MD) return;   // no source data (cooked-only) — rules abstain

		const FStaticMeshConstAttributes Attr(*MD);
		TVertexAttributesConstRef<FVector3f> Positions = Attr.GetVertexPositions();
		TVertexInstanceAttributesConstRef<FVector3f> Normals =
			Attr.GetVertexInstanceNormals();
		TVertexInstanceAttributesConstRef<FVector3f> Tangents =
			Attr.GetVertexInstanceTangents();
		TVertexInstanceAttributesConstRef<float> BinormalSigns =
			Attr.GetVertexInstanceBinormalSigns();
		TEdgeAttributesConstRef<bool> HardEdges = Attr.GetEdgeHardnesses();
		TVertexInstanceAttributesConstRef<FVector2f> UVs =
			Attr.GetVertexInstanceUVs();

		// ── Geometry integrity: triangles ──
		int32 Degenerate = 0;
		for (const FTriangleID Tri : MD->Triangles().GetElementIDs())
		{
			TArrayView<const FVertexID> V = MD->GetTriangleVertices(Tri);
			if (V.Num() < 3) continue;
			const FVector3f P0 = Positions[V[0]];
			const FVector3f P1 = Positions[V[1]];
			const FVector3f P2 = Positions[V[2]];
			// Twice the triangle area = |(P1-P0) x (P2-P0)|. Zero => degenerate.
			if (FVector3f::CrossProduct(P1 - P0, P2 - P0).SizeSquared()
					< UE_KINDA_SMALL_NUMBER)
				++Degenerate;
		}

		// ── Geometry integrity: duplicate / overlapping vertices ──
		auto CellKey = [](const FVector3f& P, float Cell) -> FIntVector
		{
			return FIntVector(
				FMath::FloorToInt(P.X / Cell),
				FMath::FloorToInt(P.Y / Cell),
				FMath::FloorToInt(P.Z / Cell));
		};
		constexpr float FineCell   = 0.001f;   // weldable exact duplicates
		constexpr float CoarseCell = 0.05f;    // near-coincident
		TMap<FIntVector, int32> FineGrid, CoarseGrid;
		for (const FVertexID Vtx : MD->Vertices().GetElementIDs())
		{
			const FVector3f P = Positions[Vtx];
			++FineGrid.FindOrAdd(CellKey(P, FineCell));
			++CoarseGrid.FindOrAdd(CellKey(P, CoarseCell));
		}
		int32 DupVerts = 0, NearVerts = 0;
		for (const TPair<FIntVector, int32>& Cell : FineGrid)
			if (Cell.Value > 1) DupVerts += Cell.Value - 1;
		for (const TPair<FIntVector, int32>& Cell : CoarseGrid)
			if (Cell.Value > 1) NearVerts += Cell.Value - 1;
		const int32 OverlapVerts = FMath::Max(0, NearVerts - DupVerts);

		// ── Geometry integrity: edges ──
		int32 NonManifold = 0, OpenEdges = 0, HardCount = 0, EdgeTotal = 0;
		const bool bHasHard = HardEdges.IsValid();
		for (const FEdgeID Edge : MD->Edges().GetElementIDs())
		{
			++EdgeTotal;
			const int32 NumTris = MD->GetNumEdgeConnectedTriangles(Edge);
			if (NumTris > 2)      ++NonManifold;
			else if (NumTris == 1) ++OpenEdges;
			if (bHasHard && HardEdges[Edge]) ++HardCount;
		}

		// ── Normal / tangent attribute scan ──
		const bool bHasNormals  = Normals.IsValid();
		const bool bHasTangents = Tangents.IsValid();
		const bool bHasBinormal = BinormalSigns.IsValid();
		int32 ZeroNormals = 0, NanNormals = 0, Mirrored = 0, NumVI = 0;
		for (const FVertexInstanceID VI : MD->VertexInstances().GetElementIDs())
		{
			++NumVI;
			if (bHasNormals)
			{
				const FVector3f N = Normals[VI];
				if (N.ContainsNaN())            ++NanNormals;
				else if (N.IsNearlyZero(1e-4f)) ++ZeroNormals;
			}
			if (bHasBinormal && BinormalSigns[VI] < 0.f) ++Mirrored;
		}

		// ── Emit top-level geometry fields ──
		Obj->SetNumberField(TEXT("degenerate_triangle_count"), Degenerate);   // LG004
		Obj->SetNumberField(TEXT("duplicate_vertex_count"), DupVerts);        // LG005
		Obj->SetNumberField(TEXT("overlapping_vertex_count"), OverlapVerts);  // LG006
		Obj->SetNumberField(TEXT("non_manifold_edge_count"), NonManifold);    // LG007
		Obj->SetNumberField(TEXT("open_edge_count"), OpenEdges);              // LG008
		if (UVs.IsValid())
			Obj->SetNumberField(TEXT("uv_channel_count"), UVs.GetNumChannels());

		// ── Emit normal_stats sub-object (contract §6.3) ──
		TSharedRef<FJsonObject> NS = MakeShared<FJsonObject>();
		NS->SetBoolField(TEXT("has_normals"), bHasNormals);
		NS->SetNumberField(TEXT("zero_normal_count"), ZeroNormals);
		NS->SetNumberField(TEXT("nan_normal_count"), NanNormals);
		NS->SetBoolField(TEXT("has_tangents"), bHasTangents);
		NS->SetNumberField(TEXT("mirrored_tangent_ratio"),
			NumVI > 0 ? (double)Mirrored / NumVI : 0.0);
		NS->SetNumberField(TEXT("hard_edge_ratio"),
			EdgeTotal > 0 ? (double)HardCount / EdgeTotal : 0.0);
		Obj->SetObjectField(TEXT("normal_stats"), NS);

		// ── Per-UV-channel stats (contract §6.2): the unambiguous subset ──
		// outside_unit_ratio (LW009), packing_efficiency (LW006), island_count
		// (LW005). Left at their safe defaults so their rules abstain:
		// overlap_ratio (needs UV rasterisation) and texel_density_* (needs the
		// dominant texture resolution) — both a later phase.
		const int32 NumUV = UVs.IsValid() ? UVs.GetNumChannels() : 0;
		if (NumUV > 0)
		{
			TArray<TSharedPtr<FJsonValue>> UvChannels;
			for (int32 Ch = 0; Ch < NumUV; ++Ch)
			{
				int32 OutsideCorners = 0, TotalCorners = 0;
				double UsedUvArea = 0.0;
				FVector2f UvMin(FLT_MAX, FLT_MAX), UvMax(-FLT_MAX, -FLT_MAX);

				// Union-find over UV-welded corners → connected shells (islands).
				TMap<FIntPoint, int32> WeldId;
				TArray<int32> Parent;
				auto Find = [&Parent](int32 X) -> int32
				{
					while (Parent[X] != X)
						{ Parent[X] = Parent[Parent[X]]; X = Parent[X]; }
					return X;
				};
				auto Weld = [&](const FVector2f& UV) -> int32
				{
					const FIntPoint Key(FMath::RoundToInt(UV.X / 1e-4f),
					                    FMath::RoundToInt(UV.Y / 1e-4f));
					if (const int32* Found = WeldId.Find(Key)) return *Found;
					const int32 Id = Parent.Num();
					Parent.Add(Id); WeldId.Add(Key, Id); return Id;
				};
				auto Union = [&](int32 A, int32 B) { Parent[Find(A)] = Find(B); };

				for (const FTriangleID Tri : MD->Triangles().GetElementIDs())
				{
					TArrayView<const FVertexInstanceID> VIs =
						MD->GetTriangleVertexInstances(Tri);
					if (VIs.Num() < 3) continue;
					const FVector2f C0 = UVs.Get(VIs[0], Ch);
					const FVector2f C1 = UVs.Get(VIs[1], Ch);
					const FVector2f C2 = UVs.Get(VIs[2], Ch);
					for (const FVector2f& UV : { C0, C1, C2 })
					{
						++TotalCorners;
						if (UV.X < 0.f || UV.X > 1.f || UV.Y < 0.f || UV.Y > 1.f)
							++OutsideCorners;
						UvMin.X = FMath::Min(UvMin.X, UV.X);
						UvMin.Y = FMath::Min(UvMin.Y, UV.Y);
						UvMax.X = FMath::Max(UvMax.X, UV.X);
						UvMax.Y = FMath::Max(UvMax.Y, UV.Y);
					}
					UsedUvArea += 0.5 * FMath::Abs(
						(C1.X - C0.X) * (C2.Y - C0.Y)
						- (C2.X - C0.X) * (C1.Y - C0.Y));
					Union(Weld(C0), Weld(C1));
					Union(Weld(C1), Weld(C2));
				}

				double BboxArea = 0.0;
				if (UvMax.X > UvMin.X && UvMax.Y > UvMin.Y)
					BboxArea = (double)(UvMax.X - UvMin.X) * (UvMax.Y - UvMin.Y);
				const double Packing = BboxArea > 0.0
					? FMath::Clamp(UsedUvArea / BboxArea, 0.0, 1.0) : 1.0;

				TSet<int32> Roots;
				for (int32 I = 0; I < Parent.Num(); ++I) Roots.Add(Find(I));

				TSharedRef<FJsonObject> Uc = MakeShared<FJsonObject>();
				Uc->SetNumberField(TEXT("channel"), Ch);
				Uc->SetNumberField(TEXT("outside_unit_ratio"),
					TotalCorners > 0 ? (double)OutsideCorners / TotalCorners : 0.0);
				Uc->SetNumberField(TEXT("packing_efficiency"), Packing);
				Uc->SetNumberField(TEXT("island_count"), Roots.Num());
				UvChannels.Add(MakeShared<FJsonValueObject>(Uc));
			}
			Obj->SetArrayField(TEXT("uv_channels"), UvChannels);
		}
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// AuditLods — gather + POST
// ─────────────────────────────────────────────────────────────────────────────

void FShintCoreClient::AuditLods(
	const FString& Profile, bool bExplainTop, FOnShintLodAuditComplete OnComplete,
	bool bDeepScan)
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

	// Deep Scan reuses cached per-mesh fields when the mesh's DDC key is
	// unchanged, so a repeat scan only pays the FMeshDescription cost for
	// assets edited since last time.
	FLodScanCache ScanCache(FPaths::Combine(
		FPaths::ProjectSavedDir(), TEXT("ShintTools"), TEXT("lod_scan_cache.json")));
	if (bDeepScan) ScanCache.Load();

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
			// Deep Scan (opt-in): mesh-description geometry-integrity + normal
			// stats that the fast scan can't see. Fills the fields the LG/LN/LW
			// rule families read; absent otherwise, so those rules abstain.
			// Cache-first: skip the recompute when the mesh's DDC key is
			// unchanged since the last Deep Scan.
			if (bUnderstood && bDeepScan)
			{
				const FString DdcKey = SM->GetRenderData()
					? SM->GetRenderData()->DerivedDataKey : FString();
				TSharedPtr<FJsonObject> DeepFields = ScanCache.Get(AssetPath, DdcKey);
				if (!DeepFields.IsValid())
				{
					TSharedRef<FJsonObject> Fresh = MakeShared<FJsonObject>();
					DeepScanStaticMesh(SM, Fresh);
					ScanCache.Put(AssetPath, DdcKey, Fresh);
					DeepFields = Fresh;
				}
				MergeJsonFields(Obj, DeepFields.ToSharedRef());
			}
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

	if (bDeepScan) ScanCache.Save();

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
