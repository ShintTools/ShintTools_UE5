// Copyright 2026 ShintTools. All Rights Reserved.
//
// [LOD-STRIP-BEGIN]
// SShintPredictiveDashboard — the Predictive Profiler's independent window
// (second nomad tab). Three zones over the ShintTools dark shell:
//
//   TopBar   "Predictive Profiler" · profile dropdown · Scan · connection LED
//   Zone 1   score gauges (CPU/GPU/MEM/BUILD + Overall) + frame-budget bar
//   Zone 2   Top Issues — name + cost rows, severity/remediation only when set
//   Zone 3   Impact Simulator — delta KPIs, before→after scores, next-fix hint
//   Footer   calibration · uncosted count · reference HW (the honesty line)
//
// Owns its own FShintCoreClient (like SShintToolsPanel). Studio-gated: the
// nomad tab is registered unconditionally, but the dashboard re-checks the
// cached tier before firing /predict/* and shows an upgrade prompt otherwise.
#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Core/ShintCoreClient.h"

class SShintScoreGauge;
class SShintFrameBudgetBar;
class SVerticalBox;
class SComboButton;

class SShintPredictiveDashboard : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SShintPredictiveDashboard) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	// ── Sections ──────────────────────────────────────────────────────────────
	TSharedRef<SWidget> BuildTopBar();
	TSharedRef<SWidget> BuildScoresZone();
	TSharedRef<SWidget> BuildIssuesZone();
	TSharedRef<SWidget> BuildSimulatorZone();
	TSharedRef<SWidget> BuildFooter();

	// ── Actions ───────────────────────────────────────────────────────────────
	FReply OnScanClicked();
	void   OnAnalyzeComplete(const FShintPredictReport& Report);
	void   RunSimulation();
	void   OnSimulateComplete(const FShintSimulateResult& Result);

	// ── Rebuild helpers ───────────────────────────────────────────────────────
	void RefreshGauges();
	void RefreshIssueList();
	void RefreshSimulator(const FShintSimulateResult& Result);
	void ClearSimulator();

	// Selection helpers.
	TArray<FString>            SelectedItemIds() const;
	FShintPredictIssue*        FindIssue(const FString& ItemId);

	// Top-issues filtering (chips in the zone header).
	bool                IssuePassesFilter(const FShintPredictIssue& Issue) const;
	TSharedRef<SWidget> BuildFilterChip(const FText& Label, const FString& Value, bool bDimension);

	// Tier gate — mirrors the LOD Auditor posture (bound-lambda visibility +
	// a defensive re-check before the request).
	bool IsStudioTier() const;

	// ── State ─────────────────────────────────────────────────────────────────
	TSharedPtr<FShintCoreClient> CoreClient;

	FString              Profile = TEXT("desktop_60");
	FShintPredictReport  Report;
	bool                 bHasReport = false;
	bool                 bScanning  = false;
	FString              StatusText;

	// Filters (Top Issues).
	FString SeverityFilter = TEXT("all");   // all|critical|high|medium|low
	FString DimensionFilter = TEXT("all");  // all|cpu|gpu|mem|build

	// ── Widget handles ────────────────────────────────────────────────────────
	TSharedPtr<SShintScoreGauge>     CpuGauge, GpuGauge, MemGauge, BuildGauge, OverallGauge;
	TSharedPtr<SShintFrameBudgetBar> BudgetBar;
	TSharedPtr<SVerticalBox>         IssueListBox;
	TSharedPtr<SVerticalBox>         SimulatorBody;

	// Profile dropdown.
	TSharedPtr<SComboButton>         ProfileCombo;
	TSharedRef<SWidget>              BuildProfileMenu();
};
// [LOD-STRIP-END]
