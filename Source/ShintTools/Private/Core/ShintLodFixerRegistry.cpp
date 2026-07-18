// Copyright 2026 ShintTools. All Rights Reserved.
//
// [LOD-STRIP-BEGIN]
#include "ShintLodFixerRegistry.h"

#include "ShintLodFixJournal.h"
#include "ShintCoreClient.h"             // FShintLodFinding
#include "ShintTools.h"                  // LogShintTools

#include "Engine/Texture2D.h"
#include "Engine/TextureDefines.h"
#include "Engine/StaticMesh.h"
#include "Engine/EngineTypes.h"                 // FMeshNaniteSettings
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "PhysicsEngine/BodySetup.h"            // UBodySetup / ECollisionTraceFlag (LD011)
#include "StaticMeshEditorSubsystem.h"          // structural LOD ops (LD001/004/005/007/011)
#include "StaticMeshEditorSubsystemHelpers.h"   // FStaticMeshReductionOptions, EScriptCollisionShapeType
#include "Editor.h"                             // GEditor->GetEditorSubsystem

#include "UObject/UObjectGlobals.h"
#include "UObject/Package.h"
#include "ScopedTransaction.h"

#include "Serialization/JsonReader.h"          // screen_sizes JSON array parse
#include "Serialization/JsonSerializer.h"

#define LOCTEXT_NAMESPACE "ShintLodFixer"

namespace
{
	// ── value parsing ────────────────────────────────────────────────────────
	bool ParseBool(const FString& V) { return V == TEXT("true") || V == TEXT("1"); }

	// The Core expresses several numeric recommendations as an operator + value
	// ("lod_count: >= 2", "fallback_percent: <= 30"), or as a bare number. Pull
	// the first signed number out; returns false when there is none (an advisory
	// string like "align sizes or confirm intent" has no number → not applicable).
	bool ParseNumber(const FString& V, double& Out)
	{
		FString Digits;
		for (int32 i = 0; i < V.Len(); ++i)
		{
			const TCHAR C = V[i];
			if (FChar::IsDigit(C) || C == TEXT('.') ||
			    (C == TEXT('-') && Digits.IsEmpty()))
				Digits.AppendChar(C);
			else if (!Digits.IsEmpty())
				break;   // stop at the first gap after we started collecting
		}
		if (Digits.IsEmpty()) return false;
		Out = FCString::Atod(*Digits);
		return true;
	}

	// Enum <-> name via UE reflection. Accepts short ("TC_Normalmap") or full
	// ("TextureCompressionSettings::TC_Normalmap") names; returns INDEX_NONE on
	// miss so an unknown recommended value is skipped rather than mis-applied.
	template <typename TEnum>
	int64 EnumFromName(const FString& Name)
	{
		if (const UEnum* E = StaticEnum<TEnum>())
		{
			int64 V = E->GetValueByNameString(Name);
			if (V == INDEX_NONE)
				V = E->GetValueByNameString(FString::Printf(TEXT("%s::%s"),
					*E->CppType, *Name));
			return V;
		}
		return INDEX_NONE;
	}

	template <typename TEnum>
	FString EnumToName(TEnum Value)
	{
		if (const UEnum* E = StaticEnum<TEnum>())
			return E->GetNameStringByValue(static_cast<int64>(Value));
		return FString();
	}

	// ── per-asset appliers ───────────────────────────────────────────────────
	// Each returns the number of properties it changed and records the prior
	// value into OutBefore (keyed identically to the recommended key) so Revert
	// is symmetric. bOutRebuild is set when the change is reimport-class.

	int32 ApplyToTexture(UTexture2D* Tex, const TMap<FString, FString>& Rec,
		TMap<FString, FString>& OutBefore, bool& bOutRebuild)
	{
		int32 Changed = 0;
		if (const FString* V = Rec.Find(TEXT("compression")))
		{
			const int64 New = EnumFromName<TextureCompressionSettings>(*V);
			if (New != INDEX_NONE && Tex->CompressionSettings != New)
			{
				OutBefore.Add(TEXT("compression"),
					EnumToName(Tex->CompressionSettings.GetValue()));
				Tex->CompressionSettings =
					static_cast<TextureCompressionSettings>(New);
				bOutRebuild = true; ++Changed;
			}
		}
		if (const FString* V = Rec.Find(TEXT("lod_group")))
		{
			const int64 New = EnumFromName<TextureGroup>(*V);
			if (New != INDEX_NONE && Tex->LODGroup != New)
			{
				OutBefore.Add(TEXT("lod_group"), EnumToName(Tex->LODGroup.GetValue()));
				Tex->LODGroup = static_cast<TextureGroup>(New);
				bOutRebuild = true; ++Changed;
			}
		}
		if (const FString* V = Rec.Find(TEXT("max_texture_size")))
		{
			const int32 New = FCString::Atoi(**V);
			if (New > 0 && Tex->MaxTextureSize != New)
			{
				OutBefore.Add(TEXT("max_texture_size"),
					FString::FromInt(Tex->MaxTextureSize));
				Tex->MaxTextureSize = New;
				bOutRebuild = true; ++Changed;
			}
		}
		if (const FString* V = Rec.Find(TEXT("srgb")))
		{
			const bool New = ParseBool(*V);
			if (Tex->SRGB != (New ? 1 : 0))
			{
				OutBefore.Add(TEXT("srgb"), Tex->SRGB ? TEXT("true") : TEXT("false"));
				Tex->SRGB = New; bOutRebuild = true; ++Changed;
			}
		}
		if (const FString* V = Rec.Find(TEXT("never_stream")))
		{
			const bool New = ParseBool(*V);
			if (Tex->NeverStream != (New ? 1 : 0))
			{
				OutBefore.Add(TEXT("never_stream"),
					Tex->NeverStream ? TEXT("true") : TEXT("false"));
				Tex->NeverStream = New; ++Changed;   // stream flag: no rebuild
			}
		}
		if (const FString* V = Rec.Find(TEXT("mip_gen")))
		{
			const int64 New = EnumFromName<TextureMipGenSettings>(*V);
			if (New != INDEX_NONE && Tex->MipGenSettings != New)
			{
				OutBefore.Add(TEXT("mip_gen"), EnumToName(Tex->MipGenSettings.GetValue()));
				Tex->MipGenSettings = static_cast<TextureMipGenSettings>(New);
				bOutRebuild = true; ++Changed;
			}
		}
		return Changed;
	}

#if WITH_EDITORONLY_DATA
	// Static-mesh build settings are reimport-class: every change forces one
	// UStaticMesh rebuild (§13.6.4). Operates on LOD0's source model.
	int32 ApplyToStaticMesh(UStaticMesh* Mesh, const TMap<FString, FString>& Rec,
		TMap<FString, FString>& OutBefore, bool& bOutRebuild)
	{
		if (Mesh->GetNumSourceModels() == 0) return 0;
		FMeshBuildSettings& BS = Mesh->GetSourceModel(0).BuildSettings;
		int32 Changed = 0;

		// FMeshBuildSettings flags are uint8:1 bitfields — a non-const reference
		// cannot bind to a bitfield, so read the current value by value and write
		// through a setter lambda instead of taking a uint32& to the field.
		auto ApplyFlag = [&](const TCHAR* Key, bool bCurrent, auto&& Setter)
		{
			if (const FString* V = Rec.Find(Key))
			{
				const bool New = ParseBool(*V);
				if (bCurrent != New)
				{
					OutBefore.Add(Key, bCurrent ? TEXT("true") : TEXT("false"));
					Setter(New); bOutRebuild = true; ++Changed;
				}
			}
		};
		ApplyFlag(TEXT("recompute_normals"),      BS.bRecomputeNormals,      [&](bool b){ BS.bRecomputeNormals = b; });
		ApplyFlag(TEXT("recompute_tangents"),     BS.bRecomputeTangents,     [&](bool b){ BS.bRecomputeTangents = b; });
		ApplyFlag(TEXT("remove_degenerates"),     BS.bRemoveDegenerates,     [&](bool b){ BS.bRemoveDegenerates = b; });
		ApplyFlag(TEXT("use_full_precision_uvs"), BS.bUseFullPrecisionUVs,   [&](bool b){ BS.bUseFullPrecisionUVs = b; });
		ApplyFlag(TEXT("generate_lightmap_uvs"),  BS.bGenerateLightmapUVs,   [&](bool b){ BS.bGenerateLightmapUVs = b; });

		if (const FString* V = Rec.Find(TEXT("build_scale")))
		{
			const float New = FCString::Atof(**V);
			if (New > 0.f && !FMath::IsNearlyEqual(BS.BuildScale3D.X, New))
			{
				OutBefore.Add(TEXT("build_scale"),
					FString::SanitizeFloat(BS.BuildScale3D.X));
				BS.BuildScale3D = FVector(New);
				bOutRebuild = true; ++Changed;
			}
		}

		// ── mesh-level properties (not build settings) ───────────────────────
		// Nanite enable (LD012) and fallback percent (LD013). These live on the
		// mesh's NaniteSettings, and — like build settings — are reimport-class,
		// so a change forces one rebuild. Snapshotted for symmetric Revert.
		FMeshNaniteSettings Nanite = Mesh->GetNaniteSettings();
		bool bNaniteDirty = false;
		if (const FString* V = Rec.Find(TEXT("nanite_enabled")))
		{
			const bool New = ParseBool(*V);
			if ((Nanite.bEnabled != 0) != New)
			{
				OutBefore.Add(TEXT("nanite_enabled"),
					Nanite.bEnabled ? TEXT("true") : TEXT("false"));
				Nanite.bEnabled = New; bNaniteDirty = true;
				bOutRebuild = true; ++Changed;
			}
		}
		if (const FString* V = Rec.Find(TEXT("fallback_percent")))
		{
			double Pct = 0.0;
			// Core sends a percentage (e.g. "<= 30"); FallbackPercentTriangles is 0..1.
			if (ParseNumber(*V, Pct))
			{
				const float New = FMath::Clamp(static_cast<float>(Pct) / 100.f, 0.f, 1.f);
				if (!FMath::IsNearlyEqual(Nanite.FallbackPercentTriangles, New))
				{
					OutBefore.Add(TEXT("fallback_percent"),
						FString::SanitizeFloat(Nanite.FallbackPercentTriangles * 100.f));
					Nanite.FallbackPercentTriangles = New; bNaniteDirty = true;
					bOutRebuild = true; ++Changed;
				}
			}
		}
		if (bNaniteDirty) Mesh->SetNaniteSettings(Nanite);

		// Complex-as-simple collision (LD011): switch the trace flag away from
		// cooking the render mesh into physics. Not a rebuild — physics-only.
		if (const FString* V = Rec.Find(TEXT("complex_as_simple")))
		{
			if (UBodySetup* Body = Mesh->GetBodySetup())
			{
				const bool WantCaS = ParseBool(*V);   // recommended is usually "false"
				const bool CurCaS  = (Body->CollisionTraceFlag == CTF_UseComplexAsSimple);
				if (CurCaS != WantCaS)
				{
					Body->Modify();
					OutBefore.Add(TEXT("complex_as_simple"),
						CurCaS ? TEXT("true") : TEXT("false"));
					Body->CollisionTraceFlag =
						WantCaS ? CTF_UseComplexAsSimple : CTF_UseSimpleAndComplex;
					++Changed;
				}
			}
		}
		return Changed;
	}
#endif // WITH_EDITORONLY_DATA

	int32 ApplyToMaterial(UMaterial* Mat, const TMap<FString, FString>& Rec,
		TMap<FString, FString>& OutBefore, bool& /*bOutRebuild*/)
	{
		int32 Changed = 0;
		if (const FString* V = Rec.Find(TEXT("two_sided")))
		{
			const bool New = ParseBool(*V);
			if ((Mat->TwoSided != 0) != New)
			{
				OutBefore.Add(TEXT("two_sided"),
					Mat->TwoSided ? TEXT("true") : TEXT("false"));
				Mat->TwoSided = New; ++Changed;
			}
		}
		if (const FString* V = Rec.Find(TEXT("blend_mode")))
		{
			const int64 New = EnumFromName<EBlendMode>(*V);
			if (New != INDEX_NONE && Mat->BlendMode != New)
			{
				OutBefore.Add(TEXT("blend_mode"), EnumToName(Mat->BlendMode.GetValue()));
				Mat->BlendMode = static_cast<EBlendMode>(New);
				++Changed;
			}
		}
		return Changed;
	}

	// Dispatch to the applier for the asset's class. Returns properties changed.
	int32 ApplyAll(UObject* Asset, const TMap<FString, FString>& Rec,
		TMap<FString, FString>& OutBefore, bool& bOutRebuild)
	{
		if (UTexture2D* Tex = Cast<UTexture2D>(Asset))
			return ApplyToTexture(Tex, Rec, OutBefore, bOutRebuild);
#if WITH_EDITORONLY_DATA
		if (UStaticMesh* Mesh = Cast<UStaticMesh>(Asset))
			return ApplyToStaticMesh(Mesh, Rec, OutBefore, bOutRebuild);
#endif
		if (UMaterial* Mat = Cast<UMaterial>(Asset))
			return ApplyToMaterial(Mat, Rec, OutBefore, bOutRebuild);
		return 0;
	}

	UObject* LoadAssetChecked(const FString& AssetPath)
	{
		return StaticLoadObject(UObject::StaticClass(), nullptr, *AssetPath);
	}

	// ── structural static-mesh ops (LOD chain / collision generation) ─────────
	// These rewrite geometry or add collision via the editor subsystem; unlike a
	// property write they can't be reverted by replaying a Before snapshot, so
	// they are NOT journalled with revertible state — the caller wraps them in an
	// FScopedTransaction (Ctrl+Z) and the History row records them as applied.
	// Returns the number of structural changes made. Set *bOutStructural* so the
	// caller knows the change isn't property-revertible.

	// Which keys are handled here (vs the property applier).
	bool HasStructuralKey(const TMap<FString, FString>& Rec)
	{
		static const TCHAR* Keys[] = {
			TEXT("lod_count"), TEXT("lods"), TEXT("triangle_ratio_band"),
			TEXT("screen_sizes"), TEXT("has_simple_collision") };
		for (const TCHAR* K : Keys)
			if (Rec.Contains(K)) return true;
		return false;
	}

	TArray<float> ParseFloatArray(const FString& Json)
	{
		TArray<float> Out;
		TArray<TSharedPtr<FJsonValue>> Arr;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
		if (FJsonSerializer::Deserialize(Reader, Arr))
			for (const TSharedPtr<FJsonValue>& V : Arr)
				if (V.IsValid() && (V->Type == EJson::Number))
					Out.Add(static_cast<float>(V->AsNumber()));
		return Out;
	}

#if WITH_EDITORONLY_DATA
	int32 ApplyStructuralStaticMesh(UStaticMesh* Mesh, const TMap<FString, FString>& Rec)
	{
		if (!GEditor) return 0;
		UStaticMeshEditorSubsystem* SM =
			GEditor->GetEditorSubsystem<UStaticMeshEditorSubsystem>();
		if (!SM) return 0;

		int32 Changed = 0;
		const int32 CurrentLods = SM->GetLodCount(Mesh);

		// Determine a target LOD count from lod_count (">= N" / "<= N" / "N"), or
		// from a "regenerate the chain" rule (lods / triangle_ratio_band), which
		// rebuilds the current number of levels with a clean reduction ladder.
		int32 TargetLods = 0;
		if (const FString* V = Rec.Find(TEXT("lod_count")))
		{
			double N = 0.0;
			if (ParseNumber(*V, N) && N >= 1.0)
			{
				const int32 Want = FMath::Clamp(static_cast<int32>(N), 1, 8);
				const bool bAtLeast = V->Contains(TEXT(">="));
				const bool bAtMost  = V->Contains(TEXT("<="));
				if (bAtLeast)      TargetLods = (CurrentLods < Want) ? Want : 0;
				else if (bAtMost)  TargetLods = (CurrentLods > Want) ? Want : 0;
				else               TargetLods = (CurrentLods != Want) ? Want : 0;
			}
		}
		else if (Rec.Contains(TEXT("lods")) || Rec.Contains(TEXT("triangle_ratio_band")))
		{
			// Regenerate the existing chain (LD002/LD006). Need ≥2 to matter.
			TargetLods = (CurrentLods >= 2) ? CurrentLods : 2;
		}

		if (TargetLods > 0)
		{
			// Default reduction ladder: LOD0 full, each level halves the triangles.
			// bAutoComputeLODScreenSize lets the engine place the switch points.
			FStaticMeshReductionOptions Options;
			Options.bAutoComputeLODScreenSize = true;
			float Percent = 1.0f;
			for (int32 i = 0; i < TargetLods; ++i)
			{
				FStaticMeshReductionSettings S;
				S.PercentTriangles = Percent;
				Options.ReductionSettings.Add(S);
				Percent *= 0.5f;
			}
			SM->SetLods(Mesh, Options);
			++Changed;
		}

		// Explicit screen-size ladder (LD007) — applied after any regeneration so
		// it overrides the auto-computed sizes.
		if (const FString* V = Rec.Find(TEXT("screen_sizes")))
		{
			const TArray<float> Sizes = ParseFloatArray(*V);
			if (Sizes.Num() >= 2 && SM->SetLodScreenSizes(Mesh, Sizes))
				++Changed;
		}

		// Generate a simple collision hull (LD011) when the mesh has none.
		if (const FString* V = Rec.Find(TEXT("has_simple_collision")))
		{
			if (ParseBool(*V) &&
			    SM->AddSimpleCollisions(Mesh, EScriptCollisionShapeType::NDOP18) >= 0)
				++Changed;
		}
		return Changed;
	}
#endif // WITH_EDITORONLY_DATA
}

// ─────────────────────────────────────────────────────────────────────────────
bool FShintLodFixerRegistry::IsAutoApplicable(const TMap<FString, FString>& Rec)
{
	for (const auto& Pair : Rec)
	{
		const FString& K = Pair.Key;
		const FString& V = Pair.Value;

		// Bool-valued property keys: any value ParseBool accepts is applicable
		// (recompute_normals:false is a legitimate "turn it off" fix).
		if (K == TEXT("recompute_normals")   || K == TEXT("recompute_tangents") ||
		    K == TEXT("remove_degenerates")  || K == TEXT("use_full_precision_uvs") ||
		    K == TEXT("generate_lightmap_uvs")|| K == TEXT("srgb") ||
		    K == TEXT("never_stream")        || K == TEXT("two_sided") ||
		    K == TEXT("nanite_enabled")      || K == TEXT("complex_as_simple"))
			return true;

		// has_simple_collision is only a fix when the recommendation is to ADD one.
		if (K == TEXT("has_simple_collision") && ParseBool(V)) return true;

		// Enum keys: applicable only if the recommended value resolves to a real
		// enum entry. A prose hint ("ASTC_6x6 (color) or ETC2_RGBA") does not.
		if (K == TEXT("compression") && EnumFromName<TextureCompressionSettings>(V) != INDEX_NONE) return true;
		if (K == TEXT("lod_group")   && EnumFromName<TextureGroup>(V) != INDEX_NONE) return true;
		if (K == TEXT("mip_gen")     && EnumFromName<TextureMipGenSettings>(V) != INDEX_NONE) return true;
		if (K == TEXT("blend_mode")  && EnumFromName<EBlendMode>(V) != INDEX_NONE) return true;

		// Numeric keys: applicable only for a positive value (an advisory string
		// like "align sizes or confirm intent" parses to 0 and is not a fix).
		if (K == TEXT("max_texture_size") && FCString::Atoi(*V) > 0)   return true;
		if (K == TEXT("build_scale")      && FCString::Atof(*V) > 0.f) return true;

		// Structural mesh keys: a fix when a target number can be read
		// (lod_count / fallback_percent), or when it's a "regenerate the chain"
		// marker (lods / triangle_ratio_band), or a parseable screen-size ladder.
		double NumTmp = 0.0;
		if ((K == TEXT("lod_count") || K == TEXT("fallback_percent")) && ParseNumber(V, NumTmp))
			return true;
		if (K == TEXT("lods") || K == TEXT("triangle_ratio_band"))
			return true;
		if (K == TEXT("screen_sizes") && ParseFloatArray(V).Num() >= 2)
			return true;
	}
	return false;
}

// ─────────────────────────────────────────────────────────────────────────────
bool FShintLodFixerRegistry::CanApply(
	const FString& AssetPath, const TMap<FString, FString>& Recommended)
{
	if (Recommended.IsEmpty()) return false;
	UObject* Asset = LoadAssetChecked(AssetPath);
	if (!Asset) return false;

	// Dry-run against copies: probe on the real object but never commit — we only
	// want to know whether any recognised key applies to this class. A cheap way
	// is to check class + key membership without mutating.
	static const TSet<FString> TextureKeys = {
		TEXT("compression"), TEXT("lod_group"), TEXT("max_texture_size"),
		TEXT("srgb"), TEXT("never_stream"), TEXT("mip_gen") };
	static const TSet<FString> MeshKeys = {
		TEXT("recompute_normals"), TEXT("recompute_tangents"),
		TEXT("remove_degenerates"), TEXT("use_full_precision_uvs"),
		TEXT("generate_lightmap_uvs"), TEXT("build_scale"),
		// mesh-level property + structural keys
		TEXT("nanite_enabled"), TEXT("fallback_percent"), TEXT("complex_as_simple"),
		TEXT("lod_count"), TEXT("lods"), TEXT("triangle_ratio_band"),
		TEXT("screen_sizes"), TEXT("has_simple_collision") };
	static const TSet<FString> MaterialKeys = {
		TEXT("two_sided"), TEXT("blend_mode") };

	const TSet<FString>* Keys = nullptr;
	if (Asset->IsA<UTexture2D>())        Keys = &TextureKeys;
	else if (Asset->IsA<UStaticMesh>())  Keys = &MeshKeys;
	else if (Asset->IsA<UMaterial>())    Keys = &MaterialKeys;
	if (!Keys) return false;

	for (const auto& Pair : Recommended)
		if (Keys->Contains(Pair.Key)) return true;
	return false;
}

FShintLodFixResult FShintLodFixerRegistry::ApplyFix(
	const FString& AssetPath, const FString& RuleId, const FString& RuleName,
	const TMap<FString, FString>& Recommended)
{
	FShintLodFixResult Out;

	UObject* Asset = LoadAssetChecked(AssetPath);
	if (!Asset)
	{
		Out.Error = FString::Printf(TEXT("Asset not found: %s"), *AssetPath);
		return Out;
	}

	const FText Label = FText::Format(
		LOCTEXT("ShintLodFixTx", "ShintTools: {0}"),
		FText::FromString(RuleName.IsEmpty() ? RuleId : RuleName));
	FScopedTransaction Transaction(Label);

	Asset->Modify();

	TMap<FString, FString> Before;
	bool bRebuild = false;
	int32 Changed = ApplyAll(Asset, Recommended, Before, bRebuild);

	// Structural mesh ops (LOD chain regen/reduce, screen sizes, collision) don't
	// fit the property-snapshot model, so they run only when no property write
	// applied and are recorded as non-revertible (Before stays empty → Revert is
	// a no-op; in-session undo is Ctrl+Z). Kept in the SAME transaction.
	bool bStructural = false;
#if WITH_EDITORONLY_DATA
	if (Changed == 0)
	{
		if (UStaticMesh* Mesh = Cast<UStaticMesh>(Asset))
		{
			const int32 StructChanged = ApplyStructuralStaticMesh(Mesh, Recommended);
			if (StructChanged > 0) { Changed = StructChanged; bStructural = true; }
		}
	}
#endif

	if (Changed == 0)
	{
		// Nothing applicable actually differed — cancel the (empty) transaction.
		Transaction.Cancel();
		return Out;   // bApplied=false, no error: idempotent no-op
	}

	// Property writes need PostEditChange to rebuild render/platform data; the
	// structural subsystem calls already ran their own build, so only fire it
	// for the property path (a redundant PostEditChange after SetLods is wasteful).
	if (!bStructural)
	{
		Asset->PostEditChange();
	}
	Asset->MarkPackageDirty();

	// Journal AFTER the write succeeds, capturing the exact Before/After so
	// Revert is a faithful inverse. Structural changes carry no Before snapshot.
	FShintLodJournalEntry Entry;
	Entry.AssetPath       = AssetPath;
	Entry.RuleId          = bStructural ? (RuleId + TEXT(" (structural)")) : RuleId;
	Entry.TransactionName = Label.ToString();
	Entry.bRebuild        = bRebuild;
	Entry.Before          = Before;
	for (const auto& Pair : Before)      // After = the recommended values we set
		if (const FString* V = Recommended.Find(Pair.Key))
			Entry.After.Add(Pair.Key, *V);
	Out.JournalId = FShintLodFixJournal::Append(Entry);

	Out.bApplied          = true;
	Out.bRebuilt          = bRebuild;
	Out.PropertiesChanged = Changed;

	UE_LOG(LogShintTools, Verbose,
		TEXT("ShintLodFix: %s on %s — %d change(s)%s%s [journal %s]"),
		*RuleId, *AssetPath, Changed, bRebuild ? TEXT(", rebuilt") : TEXT(""),
		bStructural ? TEXT(", structural") : TEXT(""), *Out.JournalId);
	return Out;
}

FShintLodFixResult FShintLodFixerRegistry::ApplyFromFinding(const FShintLodFinding& F)
{
	return ApplyFix(F.AssetPath, F.RuleId, F.RuleName, F.Recommended);
}

FShintLodFixResult FShintLodFixerRegistry::RevertFix(const FString& JournalId)
{
	FShintLodFixResult Out;

	FShintLodJournalEntry Entry;
	if (!FShintLodFixJournal::Find(JournalId, Entry))
	{
		Out.Error = FString::Printf(TEXT("Journal entry not found: %s"), *JournalId);
		return Out;
	}

	UObject* Asset = LoadAssetChecked(Entry.AssetPath);
	if (!Asset)
	{
		Out.Error = FString::Printf(TEXT("Asset not found: %s"), *Entry.AssetPath);
		return Out;
	}

	const FText RevertLabel = FText::Format(
		LOCTEXT("ShintLodRevertTx", "ShintTools: Revert {0}"),
		FText::FromString(Entry.RuleId));
	FScopedTransaction Transaction(RevertLabel);
	Asset->Modify();

	// Replay the Before map — restores the pre-fix property values.
	TMap<FString, FString> Discard;
	bool bRebuild = false;
	const int32 Changed = ApplyAll(Asset, Entry.Before, Discard, bRebuild);

	if (Changed == 0)
	{
		Transaction.Cancel();
		Out.Error = TEXT("Nothing to revert (state already matches the snapshot).");
		return Out;
	}

	Asset->PostEditChange();
	Asset->MarkPackageDirty();

	// Journal the revert as its own entry (Before/After swapped) so a revert is
	// itself revertible and the History view shows the full chain.
	FShintLodJournalEntry RevEntry;
	RevEntry.AssetPath       = Entry.AssetPath;
	RevEntry.RuleId          = Entry.RuleId + TEXT(" (revert)");
	RevEntry.TransactionName = RevertLabel.ToString();
	RevEntry.bRebuild        = Entry.bRebuild;
	RevEntry.Before          = Entry.After;
	RevEntry.After           = Entry.Before;
	Out.JournalId = FShintLodFixJournal::Append(RevEntry);

	Out.bApplied          = true;
	Out.bRebuilt          = Entry.bRebuild;
	Out.PropertiesChanged = Changed;
	return Out;
}

#undef LOCTEXT_NAMESPACE
// [LOD-STRIP-END]
