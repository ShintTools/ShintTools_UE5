// Copyright 2026 ShintTools. All Rights Reserved.
//
// [LOD-STRIP-BEGIN]
#include "Predictive/SShintPredictiveDashboard.h"
#include "Predictive/SShintScoreGauge.h"
#include "Predictive/SShintFrameBudgetBar.h"

#include "ShintStyle.h"
#include "ShintTools.h"
#include "SShintToolsPanel_Private.h"   // ShintShowErrorToast

#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Shared/SShintSeverityBadge.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Styling/AppStyle.h"
#include "Styling/SlateColor.h"

#define LOCTEXT_NAMESPACE "SShintPredictiveDashboard"

namespace
{
	const TCHAR* kProfiles[] = {
		TEXT("desktop_60"), TEXT("desktop_144"), TEXT("console_30"),
		TEXT("console_60"), TEXT("mobile_30"), TEXT("vr_90"), TEXT("steamdeck_60"),
	};

	// Short dimension tag shown before the cost chip.
	FString DimTag(const FString& Dimension)
	{
		if (Dimension.StartsWith(TEXT("cpu")))   return TEXT("CPU");
		if (Dimension.StartsWith(TEXT("gpu")))   return TEXT("GPU");
		if (Dimension.StartsWith(TEXT("vram")))  return TEXT("MEM");
		if (Dimension.StartsWith(TEXT("ram")))   return TEXT("MEM");
		if (Dimension.StartsWith(TEXT("gc")))    return TEXT("GC");
		if (Dimension.StartsWith(TEXT("build"))) return TEXT("BUILD");
		return TEXT("");
	}

	// Categorical ramp for the budget-bar segments (dimension ≠ severity).
	FLinearColor SegmentColor(int32 Index)
	{
		static const FLinearColor Ramp[] = {
			FLinearColor(FColor(0xef, 0x44, 0x44)),  // red
			FLinearColor(FColor(0xf9, 0x73, 0x16)),  // orange
			FLinearColor(FColor(0x22, 0xc5, 0x5e)),  // green
			FLinearColor(FColor(0x44, 0xcf, 0xef)),  // cyan
			FLinearColor(FColor(0x3b, 0x82, 0xf6)),  // blue
			FLinearColor(FColor(0x85, 0x7c, 0xb4)),  // purple
		};
		return Ramp[Index % 6];
	}

	// The core emits severity as critical|warning|info; the dashboard's chips
	// and badges speak critical/high/medium/low. Map once, here, so the badge
	// (FromSeverity keys off the same strings for colour) and the filter agree.
	FString NormalizeSeverity(const FString& Raw)
	{
		const FString S = Raw.ToLower();
		if (S == TEXT("critical") || S == TEXT("error")) return TEXT("critical");
		if (S == TEXT("warning"))                        return TEXT("high");
		if (S == TEXT("medium"))                         return TEXT("medium");
		return TEXT("low");   // info / anything else
	}
}

void SShintPredictiveDashboard::Construct(const FArguments& InArgs)
{
	CoreClient = MakeShared<FShintCoreClient>();
	CoreClient->LoadConfig();

	StatusText = LOCTEXT("Idle", "Run a scan to predict cost before you play.").ToString();

	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(FAppStyle::Get().GetBrush("WhiteBrush"))
		.BorderBackgroundColor(FShintStyle::Colors::Bg())
		.Padding(0.f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight() [ BuildTopBar() ]
			+ SVerticalBox::Slot().FillHeight(1.f)
			[
				SNew(SScrollBox)
				+ SScrollBox::Slot().Padding(FShintStyle::Space::S4) [ BuildScoresZone() ]
				+ SScrollBox::Slot().Padding(FShintStyle::Space::S4, 0.f, FShintStyle::Space::S4, FShintStyle::Space::S4) [ BuildIssuesZone() ]
				+ SScrollBox::Slot().Padding(FShintStyle::Space::S4, 0.f, FShintStyle::Space::S4, FShintStyle::Space::S4) [ BuildSimulatorZone() ]
				+ SScrollBox::Slot().Padding(FShintStyle::Space::S4, 0.f, FShintStyle::Space::S4, FShintStyle::Space::S4) [ BuildFooter() ]
			]
		]
	];
}

// ─────────────────────────────────────────────────────────────────────────────
// TopBar
// ─────────────────────────────────────────────────────────────────────────────

TSharedRef<SWidget> SShintPredictiveDashboard::BuildProfileMenu()
{
	FMenuBuilder Menu(true, nullptr);
	for (const TCHAR* P : kProfiles)
	{
		const FString Name = P;
		Menu.AddMenuEntry(
			FText::FromString(Name), FText::GetEmpty(), FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda([this, Name]()
			{
				Profile = Name;   // the menu auto-dismisses on click
			})));
	}
	return Menu.MakeWidget();
}

TSharedRef<SWidget> SShintPredictiveDashboard::BuildTopBar()
{
	return SNew(SBorder)
		.BorderImage(FAppStyle::Get().GetBrush("WhiteBrush"))
		.BorderBackgroundColor(FShintStyle::Colors::BgTopbar())
		.Padding(FMargin(FShintStyle::Space::S4, FShintStyle::Space::S3))
		[
			SNew(SHorizontalBox)
			// Title + subtitle.
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock)
					.Text(LOCTEXT("Title", "Predictive Profiler"))
					.Font(FShintStyle::Fonts::H2())
					.ColorAndOpacity(FSlateColor(FShintStyle::Colors::TextPrimary()))
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock)
					.Text(LOCTEXT("Subtitle", "independent window · nomad tab"))
					.Font(FShintStyle::Fonts::Caption())
					.ColorAndOpacity(FSlateColor(FShintStyle::Colors::TextMuted()))
				]
			]
			+ SHorizontalBox::Slot().FillWidth(1.f) [ SNew(SSpacer) ]
			// Profile dropdown.
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, FShintStyle::Space::S2, 0.f)
			[
				SAssignNew(ProfileCombo, SComboButton)
				.OnGetMenuContent(this, &SShintPredictiveDashboard::BuildProfileMenu)
				.ButtonContent()
				[
					SNew(STextBlock)
					.Text_Lambda([this]{ return FText::FromString(Profile); })
					.Font(FShintStyle::Fonts::Small())
					.ColorAndOpacity(FSlateColor(FShintStyle::Colors::TextMuted()))
				]
			]
			// Scan button.
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SButton)
				.OnClicked(this, &SShintPredictiveDashboard::OnScanClicked)
				.IsEnabled_Lambda([this]{ return !bScanning; })
				[
					SNew(STextBlock)
					.Text_Lambda([this]
					{
						return bScanning ? LOCTEXT("Scanning", "Scanning…")
						                 : LOCTEXT("Scan", "Scan");
					})
					.Font(FShintStyle::Fonts::Small())
				]
			]
		];
}

// ─────────────────────────────────────────────────────────────────────────────
// Zone 1 — scores
// ─────────────────────────────────────────────────────────────────────────────

TSharedRef<SWidget> SShintPredictiveDashboard::BuildScoresZone()
{
	auto Gauge = [](TSharedPtr<SShintScoreGauge>& Handle, const FText& Caption,
		float Diameter, bool bOverall) -> TSharedRef<SWidget>
	{
		return SAssignNew(Handle, SShintScoreGauge)
			.Caption(Caption).Diameter(Diameter).bIsOverall(bOverall);
	};

	return SNew(SBorder)
		.BorderImage(FAppStyle::Get().GetBrush("WhiteBrush"))
		.BorderBackgroundColor(FShintStyle::Colors::BgCard())
		.Padding(FShintStyle::Space::S4)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, FShintStyle::Space::S4, 0.f)
					[ Gauge(CpuGauge,   LOCTEXT("Cpu", "CPU RISK"),    96.f, false) ]
				+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, FShintStyle::Space::S4, 0.f)
					[ Gauge(GpuGauge,   LOCTEXT("Gpu", "GPU RISK"),    96.f, false) ]
				+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, FShintStyle::Space::S4, 0.f)
					[ Gauge(MemGauge,   LOCTEXT("Mem", "MEMORY RISK"), 96.f, false) ]
				+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, FShintStyle::Space::S4, 0.f)
					[ Gauge(BuildGauge, LOCTEXT("Build", "BUILD RISK"), 96.f, false) ]
				+ SHorizontalBox::Slot().FillWidth(1.f) [ SNew(SSpacer) ]
				+ SHorizontalBox::Slot().AutoWidth()
					[ Gauge(OverallGauge, LOCTEXT("Overall", "OVERALL"), 128.f, true) ]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, FShintStyle::Space::S3, 0.f, 0.f)
			[
				SAssignNew(BudgetBar, SShintFrameBudgetBar)
			]
		];
}

// ─────────────────────────────────────────────────────────────────────────────
// Zone 2 — Top Issues
// ─────────────────────────────────────────────────────────────────────────────

TSharedRef<SWidget> SShintPredictiveDashboard::BuildIssuesZone()
{
	return SNew(SBorder)
		.BorderImage(FAppStyle::Get().GetBrush("WhiteBrush"))
		.BorderBackgroundColor(FShintStyle::Colors::BgCard())
		.Padding(FShintStyle::Space::S4)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, FShintStyle::Space::S3)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("TopIssues", "Top Issues"))
					.Font(FShintStyle::Fonts::H2())
					.ColorAndOpacity(FSlateColor(FShintStyle::Colors::TextPrimary()))
				]
				+ SHorizontalBox::Slot().FillWidth(1.f) [ SNew(SSpacer) ]
				// Severity chips.
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[ BuildFilterChip(LOCTEXT("SevAll", "All sev"), TEXT("all"),      false) ]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[ BuildFilterChip(LOCTEXT("SevCrit", "critical"), TEXT("critical"), false) ]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[ BuildFilterChip(LOCTEXT("SevHigh", "high"),   TEXT("high"),     false) ]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[ BuildFilterChip(LOCTEXT("SevMed", "medium"),  TEXT("medium"),   false) ]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[ BuildFilterChip(LOCTEXT("SevLow", "low"),     TEXT("low"),      false) ]
				// Divider gap, then dimension chips.
				+ SHorizontalBox::Slot().AutoWidth().Padding(FShintStyle::Space::S3, 0.f, 0.f, 0.f).VAlign(VAlign_Center)
				[ BuildFilterChip(LOCTEXT("DimAll", "All dims"), TEXT("all"),   true) ]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[ BuildFilterChip(LOCTEXT("DimCpu", "CPU"),   TEXT("cpu"),    true) ]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[ BuildFilterChip(LOCTEXT("DimGpu", "GPU"),   TEXT("gpu"),    true) ]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[ BuildFilterChip(LOCTEXT("DimMem", "MEM"),   TEXT("mem"),    true) ]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[ BuildFilterChip(LOCTEXT("DimBuild", "BUILD"), TEXT("build"), true) ]
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SAssignNew(IssueListBox, SVerticalBox)
			]
		];
}

// A filter chip: highlighted (accent) when it's the active filter, muted
// outline otherwise. Clicking sets the filter and re-renders the list.
TSharedRef<SWidget> SShintPredictiveDashboard::BuildFilterChip(
	const FText& Label, const FString& Value, bool bDimension)
{
	auto IsActive = [this, Value, bDimension]
	{
		return (bDimension ? DimensionFilter : SeverityFilter) == Value;
	};
	return SNew(SButton)
		.ButtonColorAndOpacity_Lambda([IsActive]
		{
			return IsActive() ? FShintStyle::Colors::AccentBlue()
			                  : FShintStyle::Colors::BgCardHover();
		})
		.ContentPadding(FMargin(FShintStyle::Space::S2, FShintStyle::Space::S1))
		.OnClicked_Lambda([this, Value, bDimension]
		{
			(bDimension ? DimensionFilter : SeverityFilter) = Value;
			RefreshIssueList();
			return FReply::Handled();
		})
		[
			SNew(STextBlock)
			.Text(Label)
			.Font(FShintStyle::Fonts::Caption())
			.ColorAndOpacity(FSlateColor(FShintStyle::Colors::TextPrimary()))
		];
}

bool SShintPredictiveDashboard::IssuePassesFilter(const FShintPredictIssue& Issue) const
{
	if (SeverityFilter != TEXT("all") &&
		NormalizeSeverity(Issue.Severity) != SeverityFilter)
	{
		return false;
	}
	if (DimensionFilter != TEXT("all"))
	{
		// DimTag returns CPU/GPU/MEM/BUILD/GC; compare case-insensitively.
		if (!DimTag(Issue.DominantDimension()).Equals(DimensionFilter, ESearchCase::IgnoreCase))
		{
			return false;
		}
	}
	return true;
}

// One issue row: checkbox (only if remediable) · title + path · dim tag · cost
// chip · confidence · recoverable chip.
namespace
{
	TSharedRef<SWidget> BuildIssueRow(
		FShintPredictIssue& Issue, TFunction<void()> OnToggle)
	{
		const FString Dim = Issue.DominantDimension();
		const FShintPrediction* Cost = Issue.Impact.Find(Dim);
		// The cost's basis is the row's subtitle — its origin, not a diagnosis.
		const FString Subtitle = Cost ? Cost->Basis : Issue.SourcePath;

		TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);

		// Checkbox — only meaningful when there's a fix to simulate.
		Row->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, FShintStyle::Space::S3, 0.f)
		[
			SNew(SCheckBox)
			.IsEnabled(Issue.bHasRemediation)
			.IsChecked_Lambda([&Issue]
			{
				return Issue.bChecked ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
			})
			.OnCheckStateChanged_Lambda([&Issue, OnToggle](ECheckBoxState S)
			{
				Issue.bChecked = (S == ECheckBoxState::Checked);
				OnToggle();
			})
		];

		// Severity badge — only when the item carries a remediation (severity
		// is meaningful only when there's something to fix). Predictive prices;
		// a plain cost row shows no badge.
		if (Issue.bHasRemediation && !Issue.Severity.IsEmpty())
		{
			Row->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, FShintStyle::Space::S3, 0.f)
			[
				SNew(SShintSeverityBadge).Severity(NormalizeSeverity(Issue.Severity))
			];
		}

		// Title + subtitle (path, then the cost basis).
		Row->AddSlot().FillWidth(1.f).VAlign(VAlign_Center)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth()
				[
					SNew(STextBlock)
					.Text(FText::FromString(Issue.Title))
					.Font(FShintStyle::Fonts::Body())
					.ColorAndOpacity(FSlateColor(FShintStyle::Colors::TextPrimary()))
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(FShintStyle::Space::S2, 0.f, 0.f, 0.f)
				[
					SNew(STextBlock)
					.Text(FText::FromString(Issue.SourcePath))
					.Font(FShintStyle::Fonts::Caption())
					.ColorAndOpacity(FSlateColor(FShintStyle::Colors::TextFaint()))
				]
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(STextBlock)
				.Text(FText::FromString(Subtitle))
				.Font(FShintStyle::Fonts::Caption())
				.ColorAndOpacity(FSlateColor(FShintStyle::Colors::TextMuted()))
			]
		];

		// Dimension tag.
		if (!DimTag(Dim).IsEmpty())
		{
			Row->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(FShintStyle::Space::S2, 0.f)
			[
				SNew(STextBlock)
				.Text(FText::FromString(DimTag(Dim)))
				.Font(FShintStyle::Fonts::Caption())
				.ColorAndOpacity(FSlateColor(FShintStyle::Colors::TextMuted()))
			];
		}

		// Cost chip.
		if (Cost)
		{
			Row->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(FShintStyle::Space::S2, 0.f)
			[
				SNew(STextBlock)
				.Text(FText::FromString(Cost->ToDisplay()))
				.Font(FShintStyle::Fonts::Small())
				.ColorAndOpacity(FSlateColor(FShintStyle::Colors::TextPrimary()))
			];

			// Confidence pill.
			if (!Cost->Confidence.IsEmpty())
			{
				Row->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(FShintStyle::Space::S2, 0.f)
				[
					SNew(STextBlock)
					.Text(FText::FromString(Cost->Confidence))
					.Font(FShintStyle::Fonts::Caption())
					.ColorAndOpacity(FSlateColor(FShintStyle::Colors::TextMuted()))
				];
			}
		}

		// Recoverable chip (green) when there's a fix.
		if (Issue.bHasRemediation)
		{
			Row->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(FShintStyle::Space::S2, 0.f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("Recoverable", "↩ recoverable"))
				.Font(FShintStyle::Fonts::Caption())
				.ColorAndOpacity(FSlateColor(FShintStyle::Colors::SevLow()))
			];
		}

		return SNew(SBorder)
			.BorderImage(FAppStyle::Get().GetBrush("WhiteBrush"))
			.BorderBackgroundColor(FShintStyle::Colors::BgCard())
			.Padding(FMargin(0.f, FShintStyle::Space::S2))
			[ Row ];
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Zone 3 — Impact Simulator
// ─────────────────────────────────────────────────────────────────────────────

TSharedRef<SWidget> SShintPredictiveDashboard::BuildSimulatorZone()
{
	return SNew(SBorder)
		.BorderImage(FAppStyle::Get().GetBrush("WhiteBrush"))
		.BorderBackgroundColor(FShintStyle::Colors::BgCard())
		.Padding(FShintStyle::Space::S4)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, FShintStyle::Space::S3)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("Simulator", "Impact Simulator"))
				.Font(FShintStyle::Fonts::H2())
				.ColorAndOpacity(FSlateColor(FShintStyle::Colors::TextPrimary()))
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SAssignNew(SimulatorBody, SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock)
					.Text(LOCTEXT("SimEmpty", "Select a recoverable issue to see what fixing it buys back."))
					.Font(FShintStyle::Fonts::Small())
					.ColorAndOpacity(FSlateColor(FShintStyle::Colors::TextMuted()))
				]
			]
		];
}

// ─────────────────────────────────────────────────────────────────────────────
// Footer — the honesty line
// ─────────────────────────────────────────────────────────────────────────────

TSharedRef<SWidget> SShintPredictiveDashboard::BuildFooter()
{
	return SNew(STextBlock)
		.Text_Lambda([this]
		{
			if (!bHasReport) return FText::FromString(StatusText);
			return FText::FromString(FString::Printf(
				TEXT("calibration %s · %d issues without cost model (not summed) · reference HW: %s"),
				*Report.CalibrationVersion, Report.CodeIssuesUncosted, *Report.ReferenceHw));
		})
		.Font(FShintStyle::Fonts::Caption())
		.ColorAndOpacity(FSlateColor(FShintStyle::Colors::TextFaint()));
}

// ─────────────────────────────────────────────────────────────────────────────
// Actions
// ─────────────────────────────────────────────────────────────────────────────

bool SShintPredictiveDashboard::IsStudioTier() const
{
	const FString Tier = FShintToolsModule::GetCachedTier().ToLower();
	return Tier == TEXT("studio") || Tier == TEXT("enterprise");
}

FReply SShintPredictiveDashboard::OnScanClicked()
{
	if (!IsStudioTier())
	{
		ShintShowErrorToast(
			TEXT("Predictive Profiler requires Studio"),
			TEXT("Paste a Studio license key in Settings to unlock the Predictive Profiler."));
		return FReply::Handled();
	}
	if (bScanning) return FReply::Handled();

	bScanning = true;
	StatusText = LOCTEXT("Working", "Analyzing project…").ToString();

	CoreClient->AnalyzePrediction(Profile,
		FOnShintPredictComplete::CreateSP(this, &SShintPredictiveDashboard::OnAnalyzeComplete));
	return FReply::Handled();
}

void SShintPredictiveDashboard::OnAnalyzeComplete(const FShintPredictReport& InReport)
{
	bScanning = false;
	if (!InReport.bSuccess)
	{
		ShintShowErrorToast(TEXT("Predictive scan failed"),
			InReport.ErrorMessage.IsEmpty() ? TEXT("Unknown error.") : InReport.ErrorMessage);
		return;
	}
	Report     = InReport;
	bHasReport = true;
	RefreshGauges();
	RefreshIssueList();
	ClearSimulator();
}

void SShintPredictiveDashboard::RefreshGauges()
{
	if (CpuGauge.IsValid())    CpuGauge->SetScore(Report.CpuRisk.Value);
	if (GpuGauge.IsValid())    GpuGauge->SetScore(Report.GpuRisk.Value);
	if (MemGauge.IsValid())    MemGauge->SetScore(Report.MemoryRisk.Value);
	if (BuildGauge.IsValid())  BuildGauge->SetScore(Report.BuildHealth.Value);
	if (OverallGauge.IsValid()) OverallGauge->SetScore(Report.OverallHealth);

	// Budget bar — stack CPU then GPU breakdown segments.
	if (BudgetBar.IsValid())
	{
		TArray<FShintBudgetBarSegment> Segs;
		int32 Idx = 0;
		auto AddLine = [&](const FShintBudgetLine& Line)
		{
			for (const FShintBudgetSegment& B : Line.Breakdown)
			{
				FShintBudgetBarSegment S;
				S.Label      = B.Label;
				S.ExpectedMs = B.ExpectedMs;
				S.MaxMs      = B.ExpectedMs;   // per-segment max unknown; tail on total below
				S.Color      = SegmentColor(Idx++);
				Segs.Add(S);
			}
		};
		AddLine(Report.Cpu);
		AddLine(Report.Gpu);
		// Widen the last segment's tail to the predicted CPU max for the
		// uncertainty signature (approximation — the total band's headroom).
		if (Segs.Num() > 0 && Report.Cpu.Predicted.IsSet())
		{
			const double Headroom = Report.Cpu.Predicted.Max - Report.Cpu.Predicted.Expected;
			Segs.Last().MaxMs = Segs.Last().ExpectedMs + FMath::Max(0.0, Headroom);
		}
		BudgetBar->SetData(Segs, Report.FrameBudgetMs,
			FString::Printf(TEXT("Frame budget · %s ms · %s"),
				*FString::SanitizeFloat(Report.FrameBudgetMs, 2), *Report.ProfileName));
	}
}

void SShintPredictiveDashboard::RefreshIssueList()
{
	if (!IssueListBox.IsValid()) return;
	IssueListBox->ClearChildren();

	int32 Shown = 0;
	for (FShintPredictIssue& Issue : Report.TopIssues)
	{
		if (!IssuePassesFilter(Issue)) continue;
		IssueListBox->AddSlot().AutoHeight()
		[
			BuildIssueRow(Issue, [this]{ RunSimulation(); })
		];
		++Shown;
	}

	if (Shown == 0)
	{
		IssueListBox->AddSlot().AutoHeight().Padding(0.f, FShintStyle::Space::S2)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("NoIssuesForFilter", "No issues match the current filter."))
			.Font(FShintStyle::Fonts::Small())
			.ColorAndOpacity(FSlateColor(FShintStyle::Colors::TextMuted()))
		];
	}
}

TArray<FString> SShintPredictiveDashboard::SelectedItemIds() const
{
	TArray<FString> Ids;
	for (const FShintPredictIssue& Issue : Report.TopIssues)
	{
		if (Issue.bChecked) Ids.Add(Issue.ItemId);
	}
	return Ids;
}

void SShintPredictiveDashboard::RunSimulation()
{
	const TArray<FString> Ids = SelectedItemIds();
	if (Ids.Num() == 0) { ClearSimulator(); return; }

	CoreClient->SimulatePrediction(Report.ReportId, Ids, FString(),
		Report.CostItems,   // inline fallback if the cached report expired
		FOnShintSimulateComplete::CreateSP(this, &SShintPredictiveDashboard::OnSimulateComplete));
}

void SShintPredictiveDashboard::OnSimulateComplete(const FShintSimulateResult& Result)
{
	if (!Result.bSuccess)
	{
		ShintShowErrorToast(TEXT("Simulation failed"),
			Result.ErrorMessage.IsEmpty() ? TEXT("Unknown error.") : Result.ErrorMessage);
		return;
	}
	RefreshSimulator(Result);
}

void SShintPredictiveDashboard::ClearSimulator()
{
	if (!SimulatorBody.IsValid()) return;
	SimulatorBody->ClearChildren();
	SimulatorBody->AddSlot().AutoHeight()
	[
		SNew(STextBlock)
		.Text(LOCTEXT("SimEmpty2", "Select a recoverable issue to see what fixing it buys back."))
		.Font(FShintStyle::Fonts::Small())
		.ColorAndOpacity(FSlateColor(FShintStyle::Colors::TextMuted()))
	];
}

void SShintPredictiveDashboard::RefreshSimulator(const FShintSimulateResult& Result)
{
	if (!SimulatorBody.IsValid()) return;
	SimulatorBody->ClearChildren();

	// Header line: "N selected".
	SimulatorBody->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, FShintStyle::Space::S2)
	[
		SNew(STextBlock)
		.Text(FText::FromString(FString::Printf(TEXT("%d selected"), Result.SelectedCount)))
		.Font(FShintStyle::Fonts::Small())
		.ColorAndOpacity(FSlateColor(FShintStyle::Colors::TextMuted()))
	];

	// Delta KPI row.
	TSharedRef<SHorizontalBox> Deltas = SNew(SHorizontalBox);
	for (const TPair<FString, FShintPrediction>& D : Result.Deltas)
	{
		Deltas->AddSlot().AutoWidth().Padding(0.f, 0.f, FShintStyle::Space::S5, 0.f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(STextBlock)
				.Text(FText::FromString(DimTag(D.Key)))
				.Font(FShintStyle::Fonts::Caption())
				.ColorAndOpacity(FSlateColor(FShintStyle::Colors::TextMuted()))
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(STextBlock)
				.Text(FText::FromString(D.Value.ToHeadline()))
				.Font(FShintStyle::Fonts::H2())
				.ColorAndOpacity(FSlateColor(FShintStyle::Colors::SevLow()))   // green = recovered
			]
		];
	}
	SimulatorBody->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, FShintStyle::Space::S3) [ Deltas ];

	// Before → after gauges animate.
	if (CpuGauge.IsValid())     CpuGauge->AnimateScore(Result.Before.CpuRisk.Value, Result.After.CpuRisk.Value);
	if (GpuGauge.IsValid())     GpuGauge->AnimateScore(Result.Before.GpuRisk.Value, Result.After.GpuRisk.Value);
	if (MemGauge.IsValid())     MemGauge->AnimateScore(Result.Before.MemoryRisk.Value, Result.After.MemoryRisk.Value);
	if (BuildGauge.IsValid())   BuildGauge->AnimateScore(Result.Before.BuildHealth.Value, Result.After.BuildHealth.Value);
	if (OverallGauge.IsValid()) OverallGauge->AnimateScore(Result.Before.OverallHealth, Result.After.OverallHealth);

	// Next-fix recommendation + one-click "add to selection".
	if (Result.Recommendations.Num() > 0)
	{
		const FString NextId = Result.Recommendations[0].ItemId;
		SimulatorBody->AddSlot().AutoHeight().Padding(0.f, FShintStyle::Space::S2, 0.f, 0.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(FText::FromString(FString::Printf(
					TEXT("Fix this next: %s"), *Result.Recommendations[0].Reason)))
				.Font(FShintStyle::Fonts::Small())
				.ColorAndOpacity(FSlateColor(FShintStyle::Colors::TextMuted()))
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SButton)
				.ButtonColorAndOpacity(FShintStyle::Colors::BgCardHover())
				.ContentPadding(FMargin(FShintStyle::Space::S3, FShintStyle::Space::S1))
				.OnClicked_Lambda([this, NextId]
				{
					if (FShintPredictIssue* Issue = FindIssue(NextId))
					{
						Issue->bChecked = true;
						RefreshIssueList();   // reflect the new checkbox state
						RunSimulation();      // re-simulate with it included
					}
					return FReply::Handled();
				})
				[
					SNew(STextBlock)
					.Text(LOCTEXT("AddToSelection", "+ add to selection"))
					.Font(FShintStyle::Fonts::Caption())
					.ColorAndOpacity(FSlateColor(FShintStyle::Colors::TextPrimary()))
				]
			]
		];
	}
}

FShintPredictIssue* SShintPredictiveDashboard::FindIssue(const FString& ItemId)
{
	for (FShintPredictIssue& Issue : Report.TopIssues)
	{
		if (Issue.ItemId == ItemId) return &Issue;
	}
	return nullptr;
}

#undef LOCTEXT_NAMESPACE
// [LOD-STRIP-END]
