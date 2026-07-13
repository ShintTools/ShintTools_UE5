// Copyright 2026 ShintTools. All Rights Reserved.
//
// [LOD-STRIP-BEGIN]
#include "ShintLodAuditCommandlet.h"

#include "ShintCoreClient.h"
#include "ShintTools.h"                  // LogShintTools

#include "HttpModule.h"
#include "HttpManager.h"
#include "Containers/Ticker.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/DateTime.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"

namespace
{
	// Contract schema version this client speaks (matches Core SCHEMA_VERSION).
	constexpr int32 kSchemaVersion = 2;

	// Wall-clock budget for the whole audit round-trip (asset load + Core
	// compute + response). A Deep Scan of a large /Game can be slow; a generous
	// cap that still kills a wedged run (e.g. Core never came up).
	constexpr double kAuditTimeoutSeconds = 900.0;

	FString PluginVersion()
	{
		if (const TSharedPtr<IPlugin> P = IPluginManager::Get().FindPlugin(TEXT("ShintTools")))
			return P->GetDescriptor().VersionName;
		return TEXT("unknown");
	}

	// CSV cell escaping — quote when the value contains a comma or quote.
	FString CsvEscape(const FString& In)
	{
		FString S = In;
		S.ReplaceInline(TEXT("\""), TEXT("\"\""));
		return (S.Contains(TEXT(",")) || S.Contains(TEXT("\"")) || S.Contains(TEXT("\n")))
			? FString::Printf(TEXT("\"%s\""), *S) : S;
	}

	void WriteCsv(const FShintLodAuditResult& R, const FString& Path)
	{
		FString Csv = TEXT("asset_path,rule_id,category,severity,message,"
			"vram_mb,shader_instructions,auto_fixable\n");
		for (const FShintLodFinding& F : R.Findings)
		{
			Csv += FString::Printf(TEXT("%s,%s,%s,%s,%s,%.2f,%d,%s\n"),
				*CsvEscape(F.AssetPath), *CsvEscape(F.RuleId),
				*CsvEscape(F.Category), *CsvEscape(F.Severity),
				*CsvEscape(F.Message), F.VramMb, F.ShaderInstructions,
				F.bAutoFixable ? TEXT("true") : TEXT("false"));
		}
		if (FFileHelper::SaveStringToFile(Csv, *Path))
		{
			UE_LOG(LogShintTools, Display, TEXT("ShintLodAudit: CSV -> %s"), *Path);
		}
		else
		{
			UE_LOG(LogShintTools, Warning, TEXT("ShintLodAudit: could not write CSV %s"), *Path);
		}
	}

	// Structured JSON artifact (the CI contract): a client metadata block plus
	// the flattened findings. Built from the parsed result — the plugin does not
	// retain the raw response body.
	void WriteJson(const FShintLodAuditResult& R, const FString& Profile,
		bool bDeep, const FString& Path)
	{
		TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();

		TSharedRef<FJsonObject> Meta = MakeShared<FJsonObject>();
		Meta->SetStringField(TEXT("plugin_version"), PluginVersion());
		Meta->SetNumberField(TEXT("schema_version"), kSchemaVersion);
		Meta->SetStringField(TEXT("profile"), Profile);
		Meta->SetBoolField(TEXT("deep_scan"), bDeep);
		Meta->SetStringField(TEXT("timestamp"), FDateTime::UtcNow().ToIso8601());
		Meta->SetStringField(TEXT("scope"), TEXT("/Game"));
		Root->SetObjectField(TEXT("metadata"), Meta);

		TSharedRef<FJsonObject> Summary = MakeShared<FJsonObject>();
		Summary->SetNumberField(TEXT("assets_audited"), R.AssetsAudited);
		Summary->SetNumberField(TEXT("issues_found"), R.IssuesFound);
		Summary->SetNumberField(TEXT("auto_fixable"), R.AutoFixable);
		Summary->SetNumberField(TEXT("estimated_vram_saved_mb"), R.EstimatedVramSavedMb);
		Root->SetObjectField(TEXT("summary"), Summary);

		TArray<TSharedPtr<FJsonValue>> Findings;
		for (const FShintLodFinding& F : R.Findings)
		{
			TSharedRef<FJsonObject> J = MakeShared<FJsonObject>();
			J->SetStringField(TEXT("asset_path"), F.AssetPath);
			J->SetStringField(TEXT("rule_id"), F.RuleId);
			J->SetStringField(TEXT("category"), F.Category);
			J->SetStringField(TEXT("severity"), F.Severity);
			J->SetStringField(TEXT("message"), F.Message);
			J->SetNumberField(TEXT("vram_mb"), F.VramMb);
			J->SetNumberField(TEXT("shader_instructions"), F.ShaderInstructions);
			J->SetBoolField(TEXT("auto_fixable"), F.bAutoFixable);
			Findings.Add(MakeShared<FJsonValueObject>(J));
		}
		Root->SetArrayField(TEXT("findings"), Findings);

		FString Out;
		const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
		if (FJsonSerializer::Serialize(Root, Writer)
			&& FFileHelper::SaveStringToFile(Out, *Path))
		{
			UE_LOG(LogShintTools, Display, TEXT("ShintLodAudit: JSON -> %s"), *Path);
		}
		else
		{
			UE_LOG(LogShintTools, Warning, TEXT("ShintLodAudit: could not write JSON %s"), *Path);
		}
	}
}

UShintLodAuditCommandlet::UShintLodAuditCommandlet()
{
	IsClient      = false;
	IsServer      = false;
	IsEditor      = true;
	LogToConsole  = true;
}

int32 UShintLodAuditCommandlet::Main(const FString& Params)
{
	FString Profile;
	if (!FParse::Value(*Params, TEXT("profile="), Profile) || Profile.IsEmpty())
		Profile = TEXT("default");
	const bool bDeep = FParse::Param(*Params, TEXT("deep"));
	FString FailOn;
	FParse::Value(*Params, TEXT("failon="), FailOn);
	FailOn = FailOn.ToLower();
	FString JsonOut, CsvOut;
	FParse::Value(*Params, TEXT("json="), JsonOut);
	FParse::Value(*Params, TEXT("csv="), CsvOut);

	UE_LOG(LogShintTools, Display,
		TEXT("ShintLodAudit: profile=%s deep=%s failon=%s"),
		*Profile, bDeep ? TEXT("true") : TEXT("false"),
		FailOn.IsEmpty() ? TEXT("(none)") : *FailOn);

	TSharedRef<FShintCoreClient> Client = MakeShared<FShintCoreClient>();
	Client->LoadConfig();

	bool bDone = false;
	FShintLodAuditResult Result;
	Client->AuditLods(Profile, /*bExplainTop*/ false,
		FOnShintLodAuditComplete::CreateLambda(
			[&bDone, &Result](const FShintLodAuditResult& R)
			{ Result = R; bDone = true; }),
		bDeep);

	// Pump HTTP + the core ticker on this (main) thread until the response
	// callback fires. The completion delegate runs inside HttpManager.Tick, so
	// bDone flips here on the same thread — no synchronisation needed.
	const double Deadline = FPlatformTime::Seconds() + kAuditTimeoutSeconds;
	while (!bDone && FPlatformTime::Seconds() < Deadline)
	{
		FHttpModule::Get().GetHttpManager().Tick(0.1f);
		FTSTicker::GetCoreTicker().Tick(0.1f);
		FPlatformProcess::Sleep(0.03f);
	}

	if (!bDone)
	{
		UE_LOG(LogShintTools, Error,
			TEXT("ShintLodAudit: timed out after %.0fs waiting for the Core."),
			kAuditTimeoutSeconds);
		return 2;
	}
	if (!Result.bSuccess)
	{
		UE_LOG(LogShintTools, Error,
			TEXT("ShintLodAudit: Core unreachable or errored: %s"),
			*Result.ErrorMessage);
		return 2;   // infra error — distinct from findings so CI can retry
	}

	if (!JsonOut.IsEmpty()) WriteJson(Result, Profile, bDeep, JsonOut);
	if (!CsvOut.IsEmpty())  WriteCsv(Result, CsvOut);

	int32 Errors = 0, Warnings = 0, Infos = 0;
	for (const FShintLodFinding& F : Result.Findings)
	{
		const FString S = F.Severity.ToLower();
		if (S == TEXT("error"))        ++Errors;
		else if (S == TEXT("warning")) ++Warnings;
		else                           ++Infos;
	}
	UE_LOG(LogShintTools, Display,
		TEXT("ShintLodAudit: %d assets audited — %d findings "
		     "(%d error, %d warning, %d info)."),
		Result.AssetsAudited, Result.IssuesFound, Errors, Warnings, Infos);

	if (FailOn == TEXT("error")   && Errors > 0)              return 1;
	if (FailOn == TEXT("warning") && (Errors + Warnings) > 0) return 1;
	return 0;
}
// [LOD-STRIP-END]
