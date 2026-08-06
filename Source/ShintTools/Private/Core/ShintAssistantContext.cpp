// Copyright 2026 ShintTools. All Rights Reserved.

#include "Core/ShintAssistantContext.h"
#include "ShintTools.h"

namespace
{
	FString               GAnalysisId;
	EShintAssistantModule GModule = EShintAssistantModule::None;
	FString               GSummary;
	// [LOD-STRIP-BEGIN]
	FString               GReportId;
	// [LOD-STRIP-END]

	// Queued "Explain this finding" from a results row. Held until the panel
	// takes it, so a click that also opens the panel still gets answered.
	bool    GHasPendingExplain = false;
	FString GPendingRuleId;
	FString GPendingAssetPath;
	FString GPendingQuestion;
}

FShintAssistantContext::FOnShintAssistantContextChanged
	FShintAssistantContext::OnChanged;

void FShintAssistantContext::Publish(
	const FString& AnalysisId, EShintAssistantModule Module, const FString& Summary)
{
	GAnalysisId = AnalysisId;
	GModule     = AnalysisId.IsEmpty() ? EShintAssistantModule::None : Module;
	GSummary    = AnalysisId.IsEmpty() ? FString() : Summary;

	UE_LOG(LogShintTools, Verbose,
		TEXT("AssistantContext: analysis=%s module=%s"),
		GAnalysisId.IsEmpty() ? TEXT("(none)") : *GAnalysisId,
		*GetModuleContextString());

	OnChanged.Broadcast();
}

void FShintAssistantContext::Clear()
{
	Publish(FString(), EShintAssistantModule::None, FString());
}

FString               FShintAssistantContext::GetAnalysisId() { return GAnalysisId; }
EShintAssistantModule FShintAssistantContext::GetModule()     { return GModule; }
FString               FShintAssistantContext::GetSummary()    { return GSummary; }
bool                  FShintAssistantContext::HasContext()    { return !GAnalysisId.IsEmpty(); }

void FShintAssistantContext::RequestExplain(
	const FString& RuleId, const FString& AssetPath, const FString& Question)
{
	GHasPendingExplain = true;
	GPendingRuleId     = RuleId;
	GPendingAssetPath  = AssetPath;
	GPendingQuestion   = Question;

	UE_LOG(LogShintTools, Verbose,
		TEXT("AssistantContext: explain queued rule=%s asset=%s"),
		*RuleId, *AssetPath);

	OnChanged.Broadcast();
}

bool FShintAssistantContext::ConsumePendingExplain(
	FString& OutRuleId, FString& OutAssetPath, FString& OutQuestion)
{
	if (!GHasPendingExplain) return false;

	OutRuleId    = GPendingRuleId;
	OutAssetPath = GPendingAssetPath;
	OutQuestion  = GPendingQuestion;

	GHasPendingExplain = false;
	GPendingRuleId.Reset();
	GPendingAssetPath.Reset();
	GPendingQuestion.Reset();
	return true;
}

FString FShintAssistantContext::GetModuleContextString()
{
	switch (GModule)
	{
	case EShintAssistantModule::CodeValidator: return TEXT("code_validator");
	case EShintAssistantModule::AssetNaming:   return TEXT("asset_naming");
	// [LOD-STRIP-BEGIN]
	case EShintAssistantModule::LodAudit:      return TEXT("lod_audit");
	case EShintAssistantModule::Predictive:    return TEXT("predictive");
	// [LOD-STRIP-END]
	default:                                   return FString();
	}
}

// [LOD-STRIP-BEGIN]
void FShintAssistantContext::PublishReport(const FString& ReportId)
{
	GReportId = ReportId;
	OnChanged.Broadcast();
}

FString FShintAssistantContext::GetReportId() { return GReportId; }
// [LOD-STRIP-END]
