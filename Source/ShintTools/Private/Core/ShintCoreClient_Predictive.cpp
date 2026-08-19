// Copyright 2026 ShintTools. All Rights Reserved.
//
// [LOD-STRIP-BEGIN]
// Predictive Profiler endpoints (Studio tier) — split out of ShintCoreClient.cpp.
//
// Collects a scene digest (actors/ticking Blueprints/skeletal meshes/lights),
// raw source, and a lean asset list via the editor APIs, POSTs them to
// /predict/analyze, and parses the risk report back. A second entry point,
// SimulatePrediction, drives the Impact Simulator (/predict/simulate).
//
// NOTE: the "config" ingest kind (render/build settings — Lumen/Nanite/
// raytracing/target platforms/compression) is part of the contract
// (ProjectConfig in schema.py) but is not sent here — the core does not
// currently read AnalyzeRequest.config anywhere in the scoring path
// (verified: no reference in predictive_orchestrator.py or layer4_scores.py
// past the pass-through), so collecting it client-side would be dead weight
// until the core wires it up. Revisit once Layer 4 actually consumes it.
//
// Mirrors the frozen v1.0 contract in docs/predictive/API.md. Predictive
// PRICES cost — titles are entity names/locations, never rule sentences.

#include "ShintCoreClient.h"
#include "ShintTools.h"

#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"

#include "Engine/StaticMesh.h"
#include "StaticMeshResources.h"        // FStaticMeshRenderData (GetRenderData)
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "Editor.h"
#include "EngineUtils.h"                 // TActorIterator
#include "GameFramework/Actor.h"
#include "Components/LightComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Blueprint.h"

#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"

// ─────────────────────────────────────────────────────────────────────────────
// FShintPrediction display helpers
// ─────────────────────────────────────────────────────────────────────────────

namespace
{
	// "ms_frame" -> "ms", "mb" -> "MB", "mb_min" -> "MB/min", "s" -> "s".
	FString UnitLabel(const FString& Unit)
	{
		if (Unit == TEXT("ms_frame")) return TEXT("ms");
		if (Unit == TEXT("mb"))       return TEXT("MB");
		if (Unit == TEXT("mb_min"))   return TEXT("MB/min");
		if (Unit == TEXT("s"))        return TEXT("s");
		if (Unit == TEXT("min"))      return TEXT("min");
		return Unit;
	}

	// Trim trailing zeros: 1.40 -> "1.4", 23.00 -> "23".
	FString Num(double V)
	{
		FString S = FString::SanitizeFloat(V, 2);
		if (S.Contains(TEXT(".")))
		{
			S.RemoveFromEnd(TEXT("0"));
			S.RemoveFromEnd(TEXT("0"));
			S.RemoveFromEnd(TEXT("."));
		}
		return S;
	}
}

FString FShintPrediction::ToHeadline() const
{
	if (!IsSet()) return FString();
	const TCHAR* Sign = Expected > 0.0 ? TEXT("+") : TEXT("");
	return FString::Printf(TEXT("est. %s%s %s"),
		Sign, *Num(Expected), *UnitLabel(Unit));
}

FString FShintPrediction::ToDisplay() const
{
	if (!IsSet()) return FString();
	const TCHAR* Sign = Expected > 0.0 ? TEXT("+") : TEXT("");
	// "+0.9–1.8 ms · est. +1.4 ms"
	return FString::Printf(TEXT("%s%s–%s%s %s · %s"),
		Sign, *Num(Min), Sign, *Num(Max), *UnitLabel(Unit), *ToHeadline());
}

FString FShintPredictIssue::DominantDimension() const
{
	// Prefer the Core's budget-normalized pick (primary_cost.dimension): a
	// raw-magnitude comparison mixes units (MB vs ms) and is structurally
	// biased toward whichever dimension happens to have the bigger number
	// (e.g. build_mb is always 0.85x vram_mb, so it could never "win" here).
	if (!PrimaryDimension.IsEmpty() && Impact.Contains(PrimaryDimension))
	{
		return PrimaryDimension;
	}

	// Fallback for older Core payloads that predate primary_cost.
	FString Best;
	double  BestAbs = -1.0;
	for (const TPair<FString, FShintPrediction>& P : Impact)
	{
		const double A = FMath::Abs(P.Value.Expected);
		if (A > BestAbs) { BestAbs = A; Best = P.Key; }
	}
	return Best;
}

// ─────────────────────────────────────────────────────────────────────────────
// Payload collectors (file-local) — run on the game thread inside
// AnalyzePrediction. Absent data abstains: the core scores only axes it has
// data for, so a partial payload yields an honest, degraded report.
// ─────────────────────────────────────────────────────────────────────────────

namespace
{
	constexpr int32 kMaxCodeFiles      = 400;
	constexpr int32 kMaxCodeFileBytes  = 200 * 1024;   // skip generated blobs

	FString LightTypeString(const ULightComponent* L)
	{
		if (L->IsA<UDirectionalLightComponent>()) return TEXT("Directional");
		if (L->IsA<USpotLightComponent>())        return TEXT("Spot");
		if (L->IsA<UPointLightComponent>())       return TEXT("Point");
		return TEXT("Point");
	}

	FString MobilityString(EComponentMobility::Type M)
	{
		switch (M)
		{
			case EComponentMobility::Static:     return TEXT("Static");
			case EComponentMobility::Stationary: return TEXT("Stationary");
			case EComponentMobility::Movable:    return TEXT("Movable");
			default:                             return TEXT("Movable");
		}
	}

	// One scene digest from the active editor world. Fields the UE5 side fills;
	// Unity-only fields are simply omitted (the core defaults them).
	//
	// ticking_actors vs ticking_blueprints matters: Layer 2 (layer2_scene.py)
	// prices Blueprint tick dispatch at ~10x the native rate. Actors were
	// previously all lumped into ticking_actors regardless of origin, so a
	// scene heavy on ticking Blueprint actors (the common case) priced its
	// dominant CPU cost at the native rate — an order-of-magnitude
	// under-estimate of cpu_risk, silently. heavy_blueprints lets each
	// ticking Blueprint class earn its own CostItem (grouped by class, not
	// per-instance) instead of only contributing to the aggregate.
	TSharedPtr<FJsonObject> CollectSceneDigest()
	{
		if (!GEditor) return nullptr;
		UWorld* World = GEditor->GetEditorWorldContext().World();
		if (!World) return nullptr;

		int32 ActorCount           = 0;
		int32 TickingActors        = 0;   // native (non-Blueprint) only
		int32 TickingBlueprints    = 0;
		int32 SkeletalMeshCount    = 0;
		TArray<TSharedPtr<FJsonValue>> Lights;

		// Ticking Blueprint actors, grouped by generating Blueprint asset path
		// (one CostItem per class, not per instance — matches how a fix is
		// actually applied: disable Tick on the Blueprint, not per placement).
		TMap<FString, int32> BpInstancesByPath;

		for (TActorIterator<AActor> It(World); It; ++It)
		{
			AActor* Actor = *It;
			if (!IsValid(Actor)) continue;
			++ActorCount;

			TArray<USkeletalMeshComponent*> SkelComps;
			Actor->GetComponents(SkelComps);
			SkeletalMeshCount += SkelComps.Num();

			if (Actor->PrimaryActorTick.bCanEverTick &&
			    Actor->PrimaryActorTick.bStartWithTickEnabled)
			{
				UClass* Class = Actor->GetClass();
				if (UBlueprint* Bp = Class ? Cast<UBlueprint>(Class->ClassGeneratedBy) : nullptr)
				{
					++TickingBlueprints;
					BpInstancesByPath.FindOrAdd(Bp->GetPathName())++;
				}
				else
				{
					++TickingActors;
				}
			}

			TArray<ULightComponent*> LightComps;
			Actor->GetComponents(LightComps);
			for (const ULightComponent* L : LightComps)
			{
				if (!L) continue;
				// Static/baked lights are free at runtime — the core skips
				// them, but sending mobility lets it decide.
				TSharedRef<FJsonObject> LJson = MakeShared<FJsonObject>();
				LJson->SetStringField(TEXT("type"), LightTypeString(L));
				LJson->SetStringField(TEXT("mobility"), MobilityString(L->Mobility));
				LJson->SetBoolField(TEXT("casts_shadows"), L->CastShadows != 0);
				Lights.Add(MakeShared<FJsonValueObject>(LJson));
			}
		}

		TArray<TSharedPtr<FJsonValue>> HeavyBlueprints;
		for (const TPair<FString, int32>& Pair : BpInstancesByPath)
		{
			TSharedRef<FJsonObject> BpJson = MakeShared<FJsonObject>();
			BpJson->SetStringField(TEXT("path"), Pair.Key);
			BpJson->SetBoolField(TEXT("tick_enabled"), true);
			BpJson->SetNumberField(TEXT("instances"), Pair.Value);
			HeavyBlueprints.Add(MakeShared<FJsonValueObject>(BpJson));
		}

		TSharedRef<FJsonObject> Scene = MakeShared<FJsonObject>();
		Scene->SetStringField(TEXT("scene_name"), World->GetMapName());
		Scene->SetNumberField(TEXT("actor_count"), ActorCount);
		Scene->SetNumberField(TEXT("ticking_actors"), TickingActors);
		Scene->SetNumberField(TEXT("ticking_blueprints"), TickingBlueprints);
		Scene->SetNumberField(TEXT("skeletal_meshes"), SkeletalMeshCount);
		Scene->SetArrayField(TEXT("heavy_blueprints"), HeavyBlueprints);
		Scene->SetArrayField(TEXT("lights"), Lights);
		return Scene;
	}

	// Cheap: the texture/mesh handles under /Game, WITHOUT loading them.
	// GetAsset() is deferred to BuildPredictAssetJson so the session chain only
	// ever force-loads one 150-batch at a time (the LOD-audit OOM lesson —
	// loading every asset up front OOMs/times out on AAA projects).
	void GatherAssetData(TArray<FAssetData>& Out)
	{
		IAssetRegistry& AR = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(
			TEXT("AssetRegistry")).Get();

		FARFilter Filter;
		Filter.PackagePaths.Add(TEXT("/Game"));
		Filter.bRecursivePaths = true;
		Filter.ClassPaths.Add(UTexture2D::StaticClass()->GetClassPathName());
		Filter.ClassPaths.Add(UStaticMesh::StaticClass()->GetClassPathName());
		Filter.bRecursiveClasses = true;
		AR.GetAssets(Filter, Out);
	}

	// Load ONE asset and append its lean JSON — textures (width/height/
	// compression) and static meshes (vertex/triangle counts). Enough for the
	// core's high-confidence VRAM/build pricing; the LOD Auditor's full
	// extractor is reused core-side for findings. Called per-batch.
	void BuildPredictAssetJson(const FAssetData& Data,
	                           TArray<TSharedPtr<FJsonValue>>& Out)
	{
		UObject* Obj = Data.GetAsset();
		if (!Obj) return;

		if (UTexture2D* Tex = Cast<UTexture2D>(Obj))
		{
			TSharedRef<FJsonObject> J = MakeShared<FJsonObject>();
			J->SetStringField(TEXT("asset_path"), Data.PackageName.ToString());
			J->SetStringField(TEXT("asset_type"), TEXT("Texture2D"));
			J->SetNumberField(TEXT("width"),  Tex->GetSizeX());
			J->SetNumberField(TEXT("height"), Tex->GetSizeY());
			J->SetBoolField(TEXT("mips_enabled"), Tex->GetNumMips() > 1);
			// Same curated TC_* mapping the LOD Auditor sends — NOT raw
			// UEnum::GetNameStringByValue() reflection. Reflection risks a
			// name the core's alias table doesn't recognize (silently priced
			// as RGBA8), the exact class of bug the Unity DXT1 VRAM fix
			// (2.4.0-2.4.2) already burned this product on once.
			J->SetStringField(TEXT("compression"),
				FShintCoreClient::TextureCompressionToString(Tex->CompressionSettings));
			Out.Add(MakeShared<FJsonValueObject>(J));
		}
		else if (UStaticMesh* Mesh = Cast<UStaticMesh>(Obj))
		{
			TSharedRef<FJsonObject> J = MakeShared<FJsonObject>();
			J->SetStringField(TEXT("asset_path"), Data.PackageName.ToString());
			J->SetStringField(TEXT("asset_type"), TEXT("StaticMesh"));
			if (Mesh->GetNumSourceModels() > 0)
			{
				J->SetNumberField(TEXT("lod_count"), Mesh->GetNumSourceModels());
			}
			// GetNumTriangles/Vertices(0) are the public UStaticMesh APIs the
			// LOD extractor uses — valid once render data is built.
			if (Mesh->GetRenderData())
			{
				J->SetNumberField(TEXT("vertex_count"),   Mesh->GetNumVertices(0));
				J->SetNumberField(TEXT("triangle_count"), Mesh->GetNumTriangles(0));
			}
			Out.Add(MakeShared<FJsonValueObject>(J));
		}
	}

	// Raw {path, content} source under the project's Source/ dir — the core
	// scans it in-process (the preferred code_files path). Capped to keep the
	// payload sane; generated headers and huge blobs are skipped.
	void CollectCodeFiles(TArray<TSharedPtr<FJsonValue>>& Out)
	{
		const FString SourceDir = FPaths::Combine(
			FPaths::ProjectDir(), TEXT("Source"));
		if (!IFileManager::Get().DirectoryExists(*SourceDir)) return;

		TArray<FString> Files;
		IFileManager::Get().FindFilesRecursive(Files, *SourceDir,
			TEXT("*.cpp"), true, false);
		TArray<FString> Headers;
		IFileManager::Get().FindFilesRecursive(Headers, *SourceDir,
			TEXT("*.h"), true, false);
		Files.Append(Headers);

		int32 Sent = 0;
		for (const FString& Path : Files)
		{
			if (Sent >= kMaxCodeFiles) break;
			if (Path.Contains(TEXT(".generated."))) continue;
			if (IFileManager::Get().FileSize(*Path) > kMaxCodeFileBytes) continue;

			FString Content;
			if (!FFileHelper::LoadFileToString(Content, *Path)) continue;

			TSharedRef<FJsonObject> J = MakeShared<FJsonObject>();
			J->SetStringField(TEXT("path"), Path);
			J->SetStringField(TEXT("content"), Content);
			Out.Add(MakeShared<FJsonValueObject>(J));
			++Sent;
		}
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// AnalyzePrediction — batched session flow (start → ingest → analyze)
//
// The report needs the whole project (assets + scene + code) scored together,
// so it can't be a stateless per-chunk aggregate like the LOD audit. Instead
// it uses the core's session API: open a session, ingest assets 150 at a time
// (GetAsset() deferred per chunk so only one batch is ever resident — the LOD
// OOM lesson), then scene, then code, then analyze(session_id). The async gap
// between batches lets GC keep the working set flat.
// ─────────────────────────────────────────────────────────────────────────────

namespace
{
	constexpr int32 kPredictAssetBatch = 150;   // mirrors LOD kLodAuditBatchSize
	constexpr int32 kPredictCodeBatch  = 40;    // code carries file content — smaller

	// Kept alive across the async chain by a TSharedRef captured in each
	// completion lambda — the same idiom as FLodAuditBatch.
	struct FPredictSession
	{
		TSharedRef<FShintCoreClient>   Client;
		FString                        BaseUrl, ApiKey, ProjectName, Profile;
		FString                        SessionId;

		TArray<FAssetData>             Assets;     // cheap handles; GetAsset per chunk
		int32                          AssetCursor = 0;
		TArray<TSharedPtr<FJsonValue>> CodeFiles;  // collected once (already capped)
		int32                          CodeCursor  = 0;
		TSharedPtr<FJsonObject>        Scene;       // collected once (cheap)

		FOnShintPredictComplete        OnComplete;

		explicit FPredictSession(TSharedRef<FShintCoreClient> C) : Client(MoveTemp(C)) {}
	};

	void FailPredict(TSharedRef<FPredictSession> S, int32 Status, const FString& Msg)
	{
		FShintPredictReport R;
		R.bSuccess     = false;
		R.StatusCode   = Status;
		R.ErrorMessage = Msg.IsEmpty() ? TEXT("Predictive analyze failed.") : Msg;
		S->OnComplete.ExecuteIfBound(R);
	}

	// Forward declares — the chain recurses/jumps between these.
	void IngestAssetBatch(TSharedRef<FPredictSession> S);
	void IngestScene(TSharedRef<FPredictSession> S);
	void IngestCodeBatch(TSharedRef<FPredictSession> S);
	void RunAnalyze(TSharedRef<FPredictSession> S);

	// POST one ingest batch of a given kind; on 200 run Next, else fail the chain.
	void PostIngest(TSharedRef<FPredictSession> S, const FString& Kind,
	                TSharedRef<FJsonObject> Payload,
	                TFunction<void(TSharedRef<FPredictSession>)> Next)
	{
		TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
		Body->SetStringField(TEXT("api_key"),        S->ApiKey);
		Body->SetStringField(TEXT("session_id"),     S->SessionId);
		Body->SetStringField(TEXT("kind"),           Kind);
		Body->SetObjectField(TEXT("payload"),        Payload);
		Body->SetStringField(TEXT("schema_version"), TEXT("1.0"));

		S->Client->SendRequest(S->BaseUrl + TEXT("/predict/session/ingest"),
			EShintHttpMethod::POST, FShintCoreClient::SerializeJson(Body),
			FOnShintRequestComplete::CreateLambda(
				[S, Next](const FShintRequestResult& Raw)
		{
			if (!Raw.bSuccess || Raw.StatusCode != 200)
			{
				FailPredict(S, Raw.StatusCode,
					Raw.ErrorMessage.IsEmpty()
						? FString::Printf(TEXT("Ingest failed (HTTP %d)."), Raw.StatusCode)
						: Raw.ErrorMessage);
				return;
			}
			Next(S);
		}));
	}

	// 1) assets — 150 at a time; GetAsset() deferred per chunk.
	void IngestAssetBatch(TSharedRef<FPredictSession> S)
	{
		if (S->AssetCursor >= S->Assets.Num()) { IngestScene(S); return; }

		const int32 End = FMath::Min(S->AssetCursor + kPredictAssetBatch, S->Assets.Num());
		TArray<TSharedPtr<FJsonValue>> Arr;
		for (int32 i = S->AssetCursor; i < End; ++i)
			BuildPredictAssetJson(S->Assets[i], Arr);
		S->AssetCursor = End;

		// An all-skipped batch still advances the chain, no request.
		if (Arr.Num() == 0) { IngestAssetBatch(S); return; }

		TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetArrayField(TEXT("assets"), Arr);
		PostIngest(S, TEXT("assets"), Payload, &IngestAssetBatch);
	}

	// 2) scene — one cheap digest of the editor world.
	void IngestScene(TSharedRef<FPredictSession> S)
	{
		if (!S->Scene.IsValid()) { IngestCodeBatch(S); return; }
		TArray<TSharedPtr<FJsonValue>> Scenes;
		Scenes.Add(MakeShared<FJsonValueObject>(S->Scene.ToSharedRef()));
		TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetArrayField(TEXT("scenes"), Scenes);
		PostIngest(S, TEXT("scene"), Payload, &IngestCodeBatch);
	}

	// 3) code_files — chained smaller (each carries file content).
	void IngestCodeBatch(TSharedRef<FPredictSession> S)
	{
		if (S->CodeCursor >= S->CodeFiles.Num()) { RunAnalyze(S); return; }

		const int32 End = FMath::Min(S->CodeCursor + kPredictCodeBatch, S->CodeFiles.Num());
		TArray<TSharedPtr<FJsonValue>> Files;
		for (int32 i = S->CodeCursor; i < End; ++i) Files.Add(S->CodeFiles[i]);
		S->CodeCursor = End;

		TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetArrayField(TEXT("files"), Files);
		PostIngest(S, TEXT("code_files"), Payload, &IngestCodeBatch);
	}

	// 4) analyze the assembled session → the report.
	void RunAnalyze(TSharedRef<FPredictSession> S)
	{
		TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
		Body->SetStringField(TEXT("api_key"),        S->ApiKey);
		Body->SetStringField(TEXT("session_id"),     S->SessionId);
		Body->SetStringField(TEXT("schema_version"), TEXT("1.0"));

		S->Client->SendRequest(S->BaseUrl + TEXT("/predict/analyze"),
			EShintHttpMethod::POST, FShintCoreClient::SerializeJson(Body),
			FOnShintRequestComplete::CreateLambda(
				[S](const FShintRequestResult& Raw)
		{
			S->OnComplete.ExecuteIfBound(FShintCoreClient::ParsePredictResponse(Raw));
		}));
	}
}

void FShintCoreClient::AnalyzePrediction(
	const FString& Profile, FOnShintPredictComplete OnComplete)
{
	TSharedRef<FPredictSession> S = MakeShared<FPredictSession>(AsShared());
	S->BaseUrl     = Config.GetBaseUrl();
	S->ApiKey      = Config.ApiKeyMongo;
	S->ProjectName = Config.ProjectName;
	S->Profile     = Profile.IsEmpty() ? TEXT("desktop_60") : Profile;
	S->OnComplete  = OnComplete;

	// Cheap up front: asset handles (no load), scene digest, capped source.
	GatherAssetData(S->Assets);
	S->Scene = CollectSceneDigest();
	CollectCodeFiles(S->CodeFiles);

	UE_LOG(LogShintTools, Verbose,
		TEXT("ShintCoreClient: /predict session — %d assets (×%d), scene=%d, %d code files (profile=%s)"),
		S->Assets.Num(), kPredictAssetBatch, S->Scene.IsValid() ? 1 : 0,
		S->CodeFiles.Num(), *S->Profile);

	// Open the session, then chain: assets → scene → code → analyze.
	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("api_key"),          S->ApiKey);
	Body->SetStringField(TEXT("engine"),           TEXT("UE5"));
	Body->SetStringField(TEXT("project_name"),     S->ProjectName);
	Body->SetStringField(TEXT("platform_profile"), S->Profile);
	Body->SetStringField(TEXT("schema_version"),   TEXT("1.0"));

	S->Client->SendRequest(S->BaseUrl + TEXT("/predict/session/start"),
		EShintHttpMethod::POST, FShintCoreClient::SerializeJson(Body),
		FOnShintRequestComplete::CreateLambda(
			[S](const FShintRequestResult& Raw)
	{
		// Non-200 (403 non-Studio, transport error) → surface the detail report.
		if (!Raw.bSuccess || Raw.StatusCode != 200)
		{
			S->OnComplete.ExecuteIfBound(FShintCoreClient::ParsePredictResponse(Raw));
			return;
		}
		TSharedPtr<FJsonObject> Root;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Raw.ResponseBody);
		if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid()
			|| !Root->TryGetStringField(TEXT("session_id"), S->SessionId)
			|| S->SessionId.IsEmpty())
		{
			FailPredict(S, Raw.StatusCode, TEXT("Session start returned no session_id."));
			return;
		}
		IngestAssetBatch(S);
	}));
}

// ─────────────────────────────────────────────────────────────────────────────
// SimulatePrediction — POST /predict/simulate
// ─────────────────────────────────────────────────────────────────────────────

namespace
{
	// Serialize one FShintPredictIssue back to the CostItem JSON shape, for the
	// stateless simulate fallback (cached report expired).
	TSharedRef<FJsonValue> IssueToJson(const FShintPredictIssue& Issue)
	{
		TSharedRef<FJsonObject> J = MakeShared<FJsonObject>();
		J->SetStringField(TEXT("item_id"), Issue.ItemId);
		J->SetNumberField(TEXT("layer"), Issue.Layer);
		J->SetStringField(TEXT("severity"), Issue.Severity);
		J->SetStringField(TEXT("title"), Issue.Title);
		J->SetStringField(TEXT("rule_id"), Issue.RuleId);

		auto PredToJson = [](const FShintPrediction& P) -> TSharedRef<FJsonObject>
		{
			TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetNumberField(TEXT("expected"), P.Expected);
			O->SetNumberField(TEXT("min"), P.Min);
			O->SetNumberField(TEXT("max"), P.Max);
			O->SetStringField(TEXT("unit"), P.Unit);
			O->SetStringField(TEXT("confidence"), P.Confidence);
			O->SetStringField(TEXT("basis"), P.Basis);
			return O;
		};

		TSharedRef<FJsonObject> ImpactObj = MakeShared<FJsonObject>();
		for (const TPair<FString, FShintPrediction>& P : Issue.Impact)
		{
			ImpactObj->SetObjectField(P.Key, PredToJson(P.Value));
		}
		J->SetObjectField(TEXT("impact"), ImpactObj);

		if (Issue.bHasRemediation)
		{
			TSharedRef<FJsonObject> Rem = MakeShared<FJsonObject>();
			Rem->SetStringField(TEXT("action"), Issue.RemediationAction);
			Rem->SetBoolField(TEXT("auto_fixable"), Issue.bAutoFixable);
			TSharedRef<FJsonObject> RecObj = MakeShared<FJsonObject>();
			for (const TPair<FString, FShintPrediction>& P : Issue.Recovery)
			{
				RecObj->SetObjectField(P.Key, PredToJson(P.Value));
			}
			Rem->SetObjectField(TEXT("recovery"), RecObj);
			J->SetObjectField(TEXT("remediation"), Rem);
		}
		return MakeShared<FJsonValueObject>(J);
	}
}

void FShintCoreClient::SimulatePrediction(
	const FString& ReportId, const TArray<FString>& SelectedItemIds,
	const FString& NewProfile, const TArray<FShintPredictIssue>& InlineCostItems,
	FOnShintSimulateComplete OnComplete)
{
	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("api_key"),   Config.ApiKeyMongo);
	Body->SetStringField(TEXT("report_id"), ReportId);
	Body->SetStringField(TEXT("platform_profile"), NewProfile);
	Body->SetStringField(TEXT("schema_version"), TEXT("1.0"));

	TArray<TSharedPtr<FJsonValue>> Ids;
	for (const FString& Id : SelectedItemIds)
	{
		Ids.Add(MakeShared<FJsonValueString>(Id));
	}
	Body->SetArrayField(TEXT("selected_item_ids"), Ids);

	TArray<TSharedPtr<FJsonValue>> Inline;
	for (const FShintPredictIssue& Issue : InlineCostItems)
	{
		Inline.Add(IssueToJson(Issue));
	}
	Body->SetArrayField(TEXT("cost_items"), Inline);

	SendRequest(Config.GetBaseUrl() + TEXT("/predict/simulate"),
		EShintHttpMethod::POST, FShintCoreClient::SerializeJson(Body),
		FOnShintRequestComplete::CreateLambda(
			[OnComplete](const FShintRequestResult& Raw)
		{
			OnComplete.ExecuteIfBound(
				FShintCoreClient::ParseSimulateResponse(Raw));
		}));
}

// ─────────────────────────────────────────────────────────────────────────────
// Parse helpers
// ─────────────────────────────────────────────────────────────────────────────

namespace
{
	// One banded figure from a JSON object matching the Prediction shape.
	FShintPrediction ParsePrediction(const TSharedPtr<FJsonObject>& Obj)
	{
		FShintPrediction P;
		if (!Obj.IsValid()) return P;
		Obj->TryGetNumberField(TEXT("expected"), P.Expected);
		Obj->TryGetNumberField(TEXT("min"), P.Min);
		Obj->TryGetNumberField(TEXT("max"), P.Max);
		Obj->TryGetStringField(TEXT("unit"), P.Unit);
		Obj->TryGetStringField(TEXT("confidence"), P.Confidence);
		Obj->TryGetStringField(TEXT("basis"), P.Basis);
		return P;
	}

	// A {dimension: Prediction} map (impact / recovery / deltas).
	void ParsePredictionMap(const TSharedPtr<FJsonObject>& Obj,
		TMap<FString, FShintPrediction>& Out)
	{
		if (!Obj.IsValid()) return;
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Obj->Values)
		{
			const TSharedPtr<FJsonObject> P = Pair.Value->AsObject();
			if (P.IsValid()) Out.Add(Pair.Key, ParsePrediction(P));
		}
	}

	FShintPredictScore ParseScore(const TSharedPtr<FJsonObject>& Obj)
	{
		FShintPredictScore S;
		if (!Obj.IsValid()) return S;
		Obj->TryGetNumberField(TEXT("value"), S.Value);
		const TArray<TSharedPtr<FJsonValue>>* Drivers;
		if (Obj->TryGetArrayField(TEXT("drivers"), Drivers))
		{
			for (const TSharedPtr<FJsonValue>& V : *Drivers)
			{
				S.Drivers.Add(V->AsString());
			}
		}
		return S;
	}

	FShintPredictScore ScoreField(const TSharedPtr<FJsonObject>& Parent,
		const TCHAR* Field)
	{
		const TSharedPtr<FJsonObject>* Obj;
		if (Parent->TryGetObjectField(Field, Obj)) return ParseScore(*Obj);
		return FShintPredictScore();
	}

	FShintPredictIssue ParseIssue(const TSharedPtr<FJsonObject>& Obj)
	{
		FShintPredictIssue Issue;
		Obj->TryGetStringField(TEXT("item_id"),  Issue.ItemId);
		Obj->TryGetNumberField(TEXT("layer"),    Issue.Layer);
		Obj->TryGetNumberField(TEXT("rank"),     Issue.Rank);
		Obj->TryGetStringField(TEXT("severity"), Issue.Severity);
		Obj->TryGetStringField(TEXT("title"),    Issue.Title);
		Obj->TryGetStringField(TEXT("rule_id"),  Issue.RuleId);

		const TSharedPtr<FJsonObject>* Source;
		if (Obj->TryGetObjectField(TEXT("source"), Source))
		{
			(*Source)->TryGetStringField(TEXT("kind"), Issue.SourceKind);
			(*Source)->TryGetStringField(TEXT("path"), Issue.SourcePath);
			(*Source)->TryGetNumberField(TEXT("line"), Issue.SourceLine);
		}

		const TSharedPtr<FJsonObject>* Impact;
		if (Obj->TryGetObjectField(TEXT("impact"), Impact))
		{
			ParsePredictionMap(*Impact, Issue.Impact);
		}

		const TSharedPtr<FJsonObject>* Rem;
		if (Obj->TryGetObjectField(TEXT("remediation"), Rem) && Rem->IsValid())
		{
			Issue.bHasRemediation = true;
			(*Rem)->TryGetStringField(TEXT("action"), Issue.RemediationAction);
			(*Rem)->TryGetBoolField(TEXT("auto_fixable"), Issue.bAutoFixable);
			const TSharedPtr<FJsonObject>* Rec;
			if ((*Rem)->TryGetObjectField(TEXT("recovery"), Rec))
			{
				ParsePredictionMap(*Rec, Issue.Recovery);
			}
		}

		const TSharedPtr<FJsonObject>* PrimaryCost;
		if (Obj->TryGetObjectField(TEXT("primary_cost"), PrimaryCost) && PrimaryCost->IsValid())
		{
			(*PrimaryCost)->TryGetStringField(TEXT("dimension"), Issue.PrimaryDimension);
		}
		return Issue;
	}

	FShintSimScores ParseSimScores(const TSharedPtr<FJsonObject>& Obj)
	{
		FShintSimScores S;
		if (!Obj.IsValid()) return S;
		S.CpuRisk     = ScoreField(Obj, TEXT("cpu_risk"));
		S.GpuRisk     = ScoreField(Obj, TEXT("gpu_risk"));
		S.MemoryRisk  = ScoreField(Obj, TEXT("memory_risk"));
		S.BuildHealth = ScoreField(Obj, TEXT("build_health"));
		Obj->TryGetNumberField(TEXT("overall_project_health"), S.OverallHealth);
		return S;
	}
}

FShintPredictReport FShintCoreClient::ParsePredictResponse(
	const FShintRequestResult& Raw)
{
	FShintPredictReport Out;
	Out.StatusCode = Raw.StatusCode;

	if (!Raw.bSuccess)
	{
		Out.ErrorMessage = Raw.ErrorMessage;
		return Out;
	}

	TSharedPtr<FJsonObject> Root;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Raw.ResponseBody);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		Out.ErrorMessage = TEXT("Malformed predictive response.");
		return Out;
	}

	// 403/404/etc. carry a FastAPI detail object, not a report.
	if (Raw.StatusCode != 200)
	{
		FString Detail;
		const TSharedPtr<FJsonObject>* DetailObj;
		if (Root->TryGetObjectField(TEXT("detail"), DetailObj))
		{
			(*DetailObj)->TryGetStringField(TEXT("error"), Detail);
		}
		Out.ErrorMessage = Detail.IsEmpty()
			? FString::Printf(TEXT("Predictive request failed (HTTP %d)."), Raw.StatusCode)
			: Detail;
		return Out;
	}

	Out.bSuccess = true;
	Root->TryGetStringField(TEXT("report_id"),    Out.ReportId);
	Root->TryGetStringField(TEXT("engine"),       Out.Engine);
	Root->TryGetStringField(TEXT("project_name"), Out.ProjectName);
	Root->TryGetStringField(TEXT("calibration_version"), Out.CalibrationVersion);
	Root->TryGetStringField(TEXT("disclaimer"),   Out.Disclaimer);

	const TSharedPtr<FJsonObject>* Profile;
	if (Root->TryGetObjectField(TEXT("platform_profile"), Profile))
	{
		(*Profile)->TryGetStringField(TEXT("profile"), Out.ProfileName);
		(*Profile)->TryGetStringField(TEXT("display_name"), Out.ProfileDisplayName);
		(*Profile)->TryGetNumberField(TEXT("frame_budget_ms"), Out.FrameBudgetMs);
		(*Profile)->TryGetStringField(TEXT("reference_hw"), Out.ReferenceHw);
	}

	const TSharedPtr<FJsonObject>* Scores;
	if (Root->TryGetObjectField(TEXT("scores"), Scores))
	{
		Out.CpuRisk     = ScoreField(*Scores, TEXT("cpu_risk"));
		Out.GpuRisk     = ScoreField(*Scores, TEXT("gpu_risk"));
		Out.MemoryRisk  = ScoreField(*Scores, TEXT("memory_risk"));
		Out.BuildHealth = ScoreField(*Scores, TEXT("build_health"));
		(*Scores)->TryGetNumberField(TEXT("overall_project_health"), Out.OverallHealth);
	}

	// Frame budget (cpu/gpu lines with predicted + breakdown).
	auto ParseBudgetLine = [](const TSharedPtr<FJsonObject>& Obj) -> FShintBudgetLine
	{
		FShintBudgetLine Line;
		if (!Obj.IsValid()) return Line;
		Obj->TryGetNumberField(TEXT("budget_ms"), Line.BudgetMs);
		const TSharedPtr<FJsonObject>* Predicted;
		if (Obj->TryGetObjectField(TEXT("predicted"), Predicted) && Predicted->IsValid())
		{
			Line.Predicted = ParsePrediction(*Predicted);
		}
		const TArray<TSharedPtr<FJsonValue>>* Breakdown;
		if (Obj->TryGetArrayField(TEXT("breakdown"), Breakdown))
		{
			for (const TSharedPtr<FJsonValue>& V : *Breakdown)
			{
				const TSharedPtr<FJsonObject> B = V->AsObject();
				if (!B.IsValid()) continue;
				FShintBudgetSegment Seg;
				B->TryGetStringField(TEXT("label"), Seg.Label);
				B->TryGetNumberField(TEXT("expected_ms"), Seg.ExpectedMs);
				Line.Breakdown.Add(Seg);
			}
		}
		return Line;
	};

	const TSharedPtr<FJsonObject>* FrameBudget;
	if (Root->TryGetObjectField(TEXT("frame_budget"), FrameBudget))
	{
		const TSharedPtr<FJsonObject>* Cpu;
		if ((*FrameBudget)->TryGetObjectField(TEXT("cpu"), Cpu)) Out.Cpu = ParseBudgetLine(*Cpu);
		const TSharedPtr<FJsonObject>* Gpu;
		if ((*FrameBudget)->TryGetObjectField(TEXT("gpu"), Gpu)) Out.Gpu = ParseBudgetLine(*Gpu);

		const TSharedPtr<FJsonObject>* Frame;
		if ((*FrameBudget)->TryGetObjectField(TEXT("frame"), Frame) && Frame->IsValid())
		{
			(*Frame)->TryGetNumberField(TEXT("budget_ms"), Out.Frame.BudgetMs);
			(*Frame)->TryGetNumberField(TEXT("predicted_ms"), Out.Frame.PredictedMs);
			(*Frame)->TryGetStringField(TEXT("bottleneck"), Out.Frame.Bottleneck);
			Out.Frame.bIsSet = Out.Frame.PredictedMs > 0.0 || !Out.Frame.Bottleneck.IsEmpty();
		}
	}

	const TSharedPtr<FJsonObject>* Memory;
	if (Root->TryGetObjectField(TEXT("memory"), Memory))
	{
		const TSharedPtr<FJsonObject>* Vram;
		if ((*Memory)->TryGetObjectField(TEXT("vram"), Vram) && Vram->IsValid())
		{
			(*Vram)->TryGetNumberField(TEXT("budget_mb"), Out.VramBudgetMb);
			const TSharedPtr<FJsonObject>* Pred;
			if ((*Vram)->TryGetObjectField(TEXT("predicted"), Pred) && Pred->IsValid())
			{
				Out.Vram = ParsePrediction(*Pred);
			}
		}
		const TSharedPtr<FJsonObject>* Ram;
		if ((*Memory)->TryGetObjectField(TEXT("ram"), Ram) && Ram->IsValid())
		{
			(*Ram)->TryGetNumberField(TEXT("budget_mb"), Out.RamBudgetMb);
			const TSharedPtr<FJsonObject>* Pred;
			if ((*Ram)->TryGetObjectField(TEXT("predicted"), Pred) && Pred->IsValid())
			{
				Out.Ram = ParsePrediction(*Pred);
			}
		}
	}

	const TSharedPtr<FJsonObject>* Build;
	if (Root->TryGetObjectField(TEXT("build"), Build))
	{
		const TSharedPtr<FJsonObject>* Size;
		if ((*Build)->TryGetObjectField(TEXT("size_mb"), Size) && Size->IsValid())
		{
			Out.BuildSizeMb = ParsePrediction(*Size);
		}
	}

	auto ParseIssueArray = [](const TArray<TSharedPtr<FJsonValue>>* Arr,
		TArray<FShintPredictIssue>& Dst)
	{
		if (!Arr) return;
		for (const TSharedPtr<FJsonValue>& V : *Arr)
		{
			const TSharedPtr<FJsonObject> O = V->AsObject();
			if (O.IsValid()) Dst.Add(ParseIssue(O));
		}
	};

	const TArray<TSharedPtr<FJsonValue>>* Top;
	if (Root->TryGetArrayField(TEXT("top_issues"), Top)) ParseIssueArray(Top, Out.TopIssues);
	const TArray<TSharedPtr<FJsonValue>>* Cost;
	if (Root->TryGetArrayField(TEXT("cost_items"), Cost)) ParseIssueArray(Cost, Out.CostItems);

	const TSharedPtr<FJsonObject>* Stats;
	if (Root->TryGetObjectField(TEXT("stats"), Stats))
	{
		(*Stats)->TryGetNumberField(TEXT("code_issues_uncosted"), Out.CodeIssuesUncosted);
	}

	return Out;
}

FShintSimulateResult FShintCoreClient::ParseSimulateResponse(
	const FShintRequestResult& Raw)
{
	FShintSimulateResult Out;
	Out.StatusCode = Raw.StatusCode;

	if (!Raw.bSuccess)
	{
		Out.ErrorMessage = Raw.ErrorMessage;
		return Out;
	}

	TSharedPtr<FJsonObject> Root;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Raw.ResponseBody);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		Out.ErrorMessage = TEXT("Malformed simulate response.");
		return Out;
	}

	if (Raw.StatusCode != 200)
	{
		Out.ErrorMessage = FString::Printf(
			TEXT("Simulate request failed (HTTP %d)."), Raw.StatusCode);
		return Out;
	}

	Out.bSuccess = true;
	Root->TryGetNumberField(TEXT("selected_count"), Out.SelectedCount);

	const TSharedPtr<FJsonObject>* Deltas;
	if (Root->TryGetObjectField(TEXT("deltas"), Deltas))
	{
		ParsePredictionMap(*Deltas, Out.Deltas);
	}

	const TSharedPtr<FJsonObject>* Before;
	if (Root->TryGetObjectField(TEXT("scores_before"), Before))
	{
		Out.Before = ParseSimScores(*Before);
	}
	const TSharedPtr<FJsonObject>* After;
	if (Root->TryGetObjectField(TEXT("scores_after"), After))
	{
		Out.After = ParseSimScores(*After);
	}

	const TArray<TSharedPtr<FJsonValue>>* Recs;
	if (Root->TryGetArrayField(TEXT("recommendations"), Recs))
	{
		for (const TSharedPtr<FJsonValue>& V : *Recs)
		{
			const TSharedPtr<FJsonObject> O = V->AsObject();
			if (!O.IsValid()) continue;
			FShintPredictRecommendation Rec;
			O->TryGetStringField(TEXT("item_id"), Rec.ItemId);
			O->TryGetStringField(TEXT("reason"),  Rec.Reason);
			O->TryGetBoolField(TEXT("auto_fixable"), Rec.bAutoFixable);
			Out.Recommendations.Add(Rec);
		}
	}

	return Out;
}
// [LOD-STRIP-END]
