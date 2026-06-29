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
#include "Engine/SkeletalMesh.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstance.h"

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

	// Per-asset display metadata retained from the collection pass and joined
	// onto findings client-side (the server findings don't echo width/height/
	// group/format back). Drives the Asset Optimizer table columns.
	struct FLodAssetMeta
	{
		int32   Width  = 0;
		int32   Height = 0;
		FString Group;
		FString Format;
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
		const bool bHasRenderData = Mesh->GetRenderData() != nullptr;

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
		return true;
	}

	bool ExtractMaterial(UMaterialInterface* Mat, const TSharedRef<FJsonObject>& Obj)
	{
		if (!Mat) return false;
		const bool bIsInstance = Mat->IsA<UMaterialInstance>();
		Obj->SetStringField(TEXT("asset_type"),
			bIsInstance ? TEXT("MaterialInstance") : TEXT("Material"));
		Obj->SetBoolField(TEXT("is_material_instance"), bIsInstance);
		// Texture sampler count — the unique textures the material references.
		TArray<UTexture*> Textures;
		Mat->GetUsedTextures(Textures, EMaterialQualityLevel::High,
			/*bAllQualityLevels*/ false, ERHIFeatureLevel::SM5,
			/*bAllFeatureLevels*/ false);
		Obj->SetNumberField(TEXT("sampler_count"), Textures.Num());
		const EBlendMode Blend = Mat->GetBlendMode();
		Obj->SetStringField(TEXT("blend_mode"),
			StaticEnum<EBlendMode>()
				? StaticEnum<EBlendMode>()->GetNameStringByValue(Blend)
				: TEXT("Opaque"));
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
			bUnderstood = ExtractMaterial(M, Obj);
		}
		// SkeletalMesh: send the bare path + type so LA002 (no-LOD) can fire;
		// detailed skeletal metadata extraction is a follow-up.
		else if (Loaded && Loaded->IsA<USkeletalMesh>())
		{
			Obj->SetStringField(TEXT("asset_type"), TEXT("SkeletalMesh"));
			bUnderstood = true;
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
			}

			Out.Findings.Add(MoveTemp(Finding));
		}
	}

	return Out;
}
