// Copyright 2026 ShintTools. All Rights Reserved.
//
// LOD Auditor destination (Studio tier) — every widget builder under the LOD
// section (KPI tile row, profile selector, "Explain top issues" toggle, scan
// button, results list, per-finding row factory) plus the handlers
// (OnAuditLodsClicked / OnLodAuditComplete / PopulateLodFindingList).
//
// Read-only module: it reports findings + estimated savings; there is no
// in-editor auto-fix flow (unlike the Asset Naming Bot), so this TU stays self
// contained and does not pull the AssetTools rename stack.

#include "SShintToolsPanel.h"
#include "SShintToolsPanel_Private.h"
#include "ShintCoreClient.h"
#include "ShintTools.h"      // FShintToolsModule::GetCachedTier (tier guard)

#include "ShintStyle.h"
#include "ShintIconStyle.h"

#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Images/SImage.h"
#include "Styling/AppStyle.h"

#define LOCTEXT_NAMESPACE "SShintToolsPanel"

namespace
{
	// Same icon+label button content helper the other sections use. Kept local
	// to this TU (the one in _Asset.cpp is also file-local) to avoid exporting a
	// shared helper just for two call sites.
	TSharedRef<SWidget> LodBtnContent(const FName& Icon,
		const TSharedRef<SWidget>& Label, const FSlateColor& Tint)
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			  .Padding(0.f, 0.f, 6.f, 0.f)
			[
				SNew(SImage)
				.Image(FShintIconStyle::GetBrush(Icon))
				.ColorAndOpacity(Tint)
				.DesiredSizeOverride(FVector2D(13.f, 13.f))
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[ Label ];
	}

	// Severity → accent colour, matching the Code/Asset severity badges.
	FLinearColor SeverityColor(const FString& Severity)
	{
		const FString S = Severity.ToLower();
		if (S == TEXT("error"))   return FShintStyle::Colors::SevCritical();
		if (S == TEXT("warning")) return FLinearColor(0.95f, 0.65f, 0.20f);
		return FShintStyle::Colors::TextMuted(); // info
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Section root
// ─────────────────────────────────────────────────────────────────────────────
TSharedRef<SWidget> SShintToolsPanel::BuildLodAuditSection()
{
	return SNew(SBorder)
		.BorderImage(ST4::Solid(C_BG()))
		.Padding(FMargin(20.f, 18.f))
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot().AutoHeight()
			[
				BuildSectionTitle(
					LOCTEXT("LODTitle", "LOD AUDITOR"),
					LOCTEXT("LODSub", "Audit meshes · textures · materials for LOD + VRAM + shader-cost issues · estimate savings"))
			]

			// Stats — 3-up KPI grid (assets audited / issues / VRAM saved).
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, FShintStyle::Space::S4)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.f).Padding(FMargin(0.f, 0.f, FShintStyle::Space::S2 * 0.5f, 0.f))
				[ StatBadge(LodAudited_Label,   LOCTEXT("LODA", "AUDITED"),     FShintStyle::Colors::TextPrimary()) ]
				+ SHorizontalBox::Slot().FillWidth(1.f).Padding(FMargin(FShintStyle::Space::S2 * 0.5f, 0.f, FShintStyle::Space::S2 * 0.5f, 0.f))
				[ StatBadge(LodIssues_Label,    LOCTEXT("LODI", "ISSUES"),      FShintStyle::Colors::SevCritical()) ]
				+ SHorizontalBox::Slot().FillWidth(1.f).Padding(FMargin(FShintStyle::Space::S2 * 0.5f, 0.f, 0.f, 0.f))
				[ StatBadge(LodVramSaved_Label, LOCTEXT("LODV", "VRAM (MB)"),   FShintStyle::Colors::TextMuted()) ]
			]

			// Action row: profile selector · explain toggle · scan button.
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 18.f)
			[
				SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(10.f, 6.f))

				// Profile combo (default | mobile)
				+ SWrapBox::Slot().VAlign(VAlign_Center)
				[
					SNew(SComboButton)
					.ContentPadding(FMargin(10.f, 5.f))
					.ButtonColorAndOpacity(FSlateColor(C_Surface()))
					.OnGetMenuContent_Lambda([this]() -> TSharedRef<SWidget>
					{
						struct FProfileEntry { FText Label; FString Value; };
						const TArray<FProfileEntry> Entries = {
							{ LOCTEXT("LODProfDefault", "Profile: Default (PC/Console)"), TEXT("default") },
							{ LOCTEXT("LODProfMobile",  "Profile: Mobile"),               TEXT("mobile")  },
						};
						TSharedRef<SVerticalBox> Menu = SNew(SVerticalBox);
						for (const FProfileEntry& E : Entries)
						{
							Menu->AddSlot().AutoHeight()
							[
								SNew(SButton)
								.ButtonColorAndOpacity(FSlateColor(C_Surface()))
								.ContentPadding(FMargin(12.f, 6.f))
								.OnClicked_Lambda([this, Value = E.Value]() -> FReply
								{
									LodProfile = Value;
									return FReply::Handled();
								})
								[
									SNew(STextBlock).Text(E.Label).Font(F_Label())
									.ColorAndOpacity(FSlateColor(C_White()))
								]
							];
						}
						return SNew(SBorder).BorderImage(ST4::Solid(C_Surface())).Padding(2.f)[ Menu ];
					})
					.ButtonContent()
					[
						SNew(STextBlock)
						.Text_Lambda([this]() {
							return LodProfile == TEXT("mobile")
								? LOCTEXT("LODProfMobileShort",  "Profile: Mobile")
								: LOCTEXT("LODProfDefaultShort", "Profile: Default");
						})
						.Font(FShintStyle::Fonts::Caption())
						.ColorAndOpacity(FSlateColor(FShintStyle::Colors::TextPrimary()))
					]
				]

				// "Explain top issues" toggle — drives explain=true (LLM, ~30s each).
				+ SWrapBox::Slot().VAlign(VAlign_Center)
				[
					SNew(SCheckBox)
					.IsChecked_Lambda([this]() {
						return bLodExplainTop ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
					})
					.OnCheckStateChanged_Lambda([this](ECheckBoxState S) {
						bLodExplainTop = (S == ECheckBoxState::Checked);
					})
					[
						SNew(STextBlock)
						.Text(LOCTEXT("LODExplain", "Explain top issues (AI · slower)"))
						.Font(F_Small())
						.ColorAndOpacity(FSlateColor(C_Gray()))
						.Margin(FMargin(6.f, 0.f, 0.f, 0.f))
					]
				]

				// Scan button
				+ SWrapBox::Slot()
				[
					SAssignNew(AuditLodBtn, SButton).ContentPadding(FMargin(14.f, 7.f))
					.OnClicked(this, &SShintToolsPanel::OnAuditLodsClicked)
					[
						LodBtnContent(TEXT("ShintTools.Icons.Search"),
						SAssignNew(AuditLodBtnLabel, STextBlock)
							.Text(LOCTEXT("LODScan", "Audit LODs")).Font(F_Small())
							.ColorAndOpacity(FSlateColor(C_White())),
						FSlateColor(C_White()))
					]
				]
			]

			+ SVerticalBox::Slot().AutoHeight()
			[ BuildLodResultsPanel() ]
		];
}

// ─────────────────────────────────────────────────────────────────────────────
// Results panel — empty state + findings list.
// ─────────────────────────────────────────────────────────────────────────────
TSharedRef<SWidget> SShintToolsPanel::BuildLodResultsPanel()
{
	SAssignNew(LodEmptyState, SBox)
	.HAlign(HAlign_Center).VAlign(VAlign_Center).MinDesiredHeight(64.f)
	[
		SNew(STextBlock)
		.Text(LOCTEXT("LODEmpty", "Run an audit to see LOD / optimisation findings."))
		.Font(F_Small()).ColorAndOpacity(FSlateColor(C_DimGray()))
	];

	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[ LodEmptyState.ToSharedRef() ]

		+ SVerticalBox::Slot().AutoHeight().MaxHeight(560.f)
		[
			SAssignNew(LodFindingListView, SListView<FShintLodFindingPtr>)
			.ListItemsSource(&LodFindingItems)
			.SelectionMode(ESelectionMode::None)
			.OnGenerateRow(this, &SShintToolsPanel::GenerateLodFindingRow)
		];
}

// ─────────────────────────────────────────────────────────────────────────────
// Row factory — one finding per row, with an expandable guidance detail.
// ─────────────────────────────────────────────────────────────────────────────
TSharedRef<ITableRow> SShintToolsPanel::GenerateLodFindingRow(
	FShintLodFindingPtr Item, const TSharedRef<STableViewBase>& Owner)
{
	const FShintLodFinding& F = Item->Finding;

	// Saving summary string — only the non-zero parts.
	FString SavingStr;
	if (F.VramMb > 0.0)
		SavingStr += FString::Printf(TEXT("~%.1f MB VRAM"), F.VramMb);
	if (F.ShaderInstructions > 0)
	{
		if (!SavingStr.IsEmpty()) SavingStr += TEXT("  ·  ");
		SavingStr += FString::Printf(TEXT("%d shader instr"), F.ShaderInstructions);
	}

	// Prefer AI guidance when present; fall back to deterministic guidance.
	const FString DetailText = !F.AiGuidance.IsEmpty() ? F.AiGuidance : F.Guidance;

	return SNew(STableRow<FShintLodFindingPtr>, Owner)
		.Padding(FMargin(0.f, 2.f))
		[
			SNew(SBorder)
			.BorderImage(ST4::Outline(C_Surface(), C_Border()))
			.Padding(FMargin(12.f, 9.f))
			[
				SNew(SVerticalBox)

				// Header line: severity dot · rule name · saving summary.
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 8.f, 0.f)
					[
						SNew(STextBlock).Text(FText::FromString(TEXT("●")))
						.ColorAndOpacity(FSlateColor(SeverityColor(F.Severity)))
						.Font(F_Small())
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(FText::FromString(F.RuleName.IsEmpty() ? F.RuleId : F.RuleName))
						.Font(FShintStyle::Fonts::Small())
						.ColorAndOpacity(FSlateColor(C_White()))
					]
					+ SHorizontalBox::Slot().FillWidth(1.f).HAlign(HAlign_Right).VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(FText::FromString(SavingStr))
						.Font(F_Label())
						.ColorAndOpacity(FSlateColor(FShintStyle::Colors::SevCritical()))
					]
				]

				// Asset path
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 3.f, 0.f, 0.f)
				[
					SNew(STextBlock)
					.Text(FText::FromString(F.AssetPath))
					.Font(F_Mono())
					.ColorAndOpacity(FSlateColor(C_Gray()))
				]

				// Message
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 4.f, 0.f, 0.f)
				[
					SNew(STextBlock)
					.AutoWrapText(true)
					.Text(FText::FromString(F.Message))
					.Font(F_Small())
					.ColorAndOpacity(FSlateColor(C_White()))
				]

				// Guidance (deterministic or AI). Empty -> collapsed.
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 6.f, 0.f, 0.f)
				[
					SNew(SBorder)
					.Visibility(DetailText.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible)
					.BorderImage(ST4::Solid(C_CodeBG()))
					.Padding(FMargin(10.f, 7.f))
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight()
						[
							SNew(STextBlock)
							.Text(!F.AiGuidance.IsEmpty()
								? LOCTEXT("LODAIGuide", "AI GUIDANCE")
								: LOCTEXT("LODGuide",   "GUIDANCE"))
							.Font(F_Label())
							.ColorAndOpacity(FSlateColor(FShintStyle::Colors::TextMuted()))
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 3.f, 0.f, 0.f)
						[
							SNew(STextBlock)
							.AutoWrapText(true)
							.Text(FText::FromString(DetailText))
							.Font(F_Small())
							.ColorAndOpacity(FSlateColor(C_White()))
						]
					]
				]
			]
		];
}

// ─────────────────────────────────────────────────────────────────────────────
// Handlers
// ─────────────────────────────────────────────────────────────────────────────
FReply SShintToolsPanel::OnAuditLodsClicked()
{
	// Defensive tier guard — the rail entry is already hidden for non-Studio
	// users, but a stale cached tier or a direct destination switch shouldn't
	// fire a request the server will 403. The server is the source of truth.
	const FString Tier = FShintToolsModule::GetCachedTier().ToLower();
	if (!Tier.IsEmpty() && Tier != TEXT("studio") && Tier != TEXT("enterprise"))
	{
		ShintShowErrorToast(
			TEXT("LOD Auditor requires Studio"),
			TEXT("Paste a Studio license key in Settings to unlock the LOD Auditor."));
		return FReply::Handled();
	}

	LodState = EModuleState::Running;
	if (AuditLodBtnLabel.IsValid())
		AuditLodBtnLabel->SetText(LOCTEXT("LODScanning", "Auditing…"));

	LodFindingItems.Empty();
	if (LodFindingListView.IsValid()) LodFindingListView->RebuildList();
	if (LodEmptyState.IsValid()) LodEmptyState->SetVisibility(EVisibility::Visible);

	CoreClient->AuditLods(LodProfile, bLodExplainTop,
		FOnShintLodAuditComplete::CreateSP(this, &SShintToolsPanel::OnLodAuditComplete));
	return FReply::Handled();
}

void SShintToolsPanel::OnLodAuditComplete(const FShintLodAuditResult& Result)
{
	LodState = Result.bSuccess ? EModuleState::Done : EModuleState::Error;
	if (AuditLodBtnLabel.IsValid())
		AuditLodBtnLabel->SetText(LOCTEXT("LODScan", "Audit LODs"));

	if (!Result.bSuccess)
	{
		ShintShowErrorToast(TEXT("LOD audit failed"),
			Result.ErrorMessage.IsEmpty()
				? TEXT("The Core engine did not return a valid response.")
				: Result.ErrorMessage);
		return;
	}

	LastLodResult = Result;
	PopulateLodFindingList(Result);
	RefreshLodStats();
}

void SShintToolsPanel::PopulateLodFindingList(const FShintLodAuditResult& Result)
{
	LodFindingItems.Empty(Result.Findings.Num());
	for (const FShintLodFinding& F : Result.Findings)
	{
		TSharedPtr<FShintLodFindingItem> Item = MakeShared<FShintLodFindingItem>();
		Item->Finding = F;
		LodFindingItems.Add(Item);
	}

	if (LodFindingListView.IsValid()) LodFindingListView->RebuildList();
	if (LodEmptyState.IsValid())
	{
		LodEmptyState->SetVisibility(
			LodFindingItems.IsEmpty() ? EVisibility::Visible : EVisibility::Collapsed);
	}
}

void SShintToolsPanel::RefreshLodStats()
{
	if (LodAudited_Label.IsValid())
		LodAudited_Label->SetText(FText::FromString(FmtN(LastLodResult.AssetsAudited)));
	if (LodIssues_Label.IsValid())
		LodIssues_Label->SetText(FText::FromString(FmtN(LastLodResult.IssuesFound)));
	if (LodVramSaved_Label.IsValid())
		LodVramSaved_Label->SetText(FText::FromString(
			FString::Printf(TEXT("%.1f"), LastLodResult.EstimatedVramSavedMb)));
}

#undef LOCTEXT_NAMESPACE
