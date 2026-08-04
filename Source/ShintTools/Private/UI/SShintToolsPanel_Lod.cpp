// Copyright 2026 ShintTools. All Rights Reserved.
//
// Asset Optimizer destination (Studio tier) — the LOD Auditor's client UI.
// Replicates the Asset Optimizer mockup: a 5-tile KPI row, a Scan bar with a
// target-platform selector, Textures/Meshes/Materials/Other tabs, a filter row
// (search + group/format/severity + Export + bulk Fix), and a per-finding data
// table (thumbnail · group · resolution · format · current/potential size ·
// savings · severity · recommendation · per-row Fix).
//
// Data: findings come from /assets/lod/audit; resolution/group/format and
// current/potential size are joined onto each finding client-side (see
// ShintCoreClient_Lod.cpp). The default, primary fix path is IN-PLACE (§20.5,
// ShintLodFixerRegistry) — it writes the server's recommendation directly onto
// the original asset, journaled and revertible. ApplyLodFixDuplicate (writes a
// non-destructive <Name>_Optimized copy, original untouched) is the fallback
// for texture size/compression findings the registry can't dispatch to (see
// OnLodFixRow). A fix that actually applies removes its finding from the list
// immediately (RemoveFixedLodFinding) — it does not wait for the next scan.
// Export writes a CSV of all findings.

#include "SShintToolsPanel.h"
#include "SShintToolsPanel_Private.h"
#include "ShintCoreClient.h"
#include "ShintTools.h"      // FShintToolsModule::GetCachedTier (tier guard)

#include "ShintStyle.h"
#include "ShintIconStyle.h"

// Stage 2b (thumbnails) + Stage 3 (optimised-duplicate auto-fix + export).
#include "AssetThumbnail.h"                            // FAssetThumbnail / pool
#include "Engine/Texture2D.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "UObject/SavePackage.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/PackageName.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"

#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Styling/AppStyle.h"

// §21 — Summary treemap + in-place auto-fix engine (§20.5) driving the Fixes view.
#include "SShintTreemap.h"
#include "ShintLodFixerRegistry.h"
#include "ShintLodFixJournal.h"
#include "Misc/Paths.h"          // FPaths::GetCleanFilename (asset display name)

#define LOCTEXT_NAMESPACE "SShintToolsPanel"

namespace
{
	// Shared column proportions — the table header and every data row use these
	// identical FillWidth values so the columns line up. (The checkbox and Fix
	// columns are fixed AutoWidth and handled separately.) Each tab renders its
	// own table: only columns meaningful for that asset family are shown.
	namespace LodCol            // Textures
	{
		constexpr float Asset = 2.8f;
		constexpr float Group = 0.8f;
		constexpr float Res   = 1.0f;
		constexpr float Fmt   = 0.7f;
		constexpr float Cur   = 1.0f;
		constexpr float Pot   = 1.0f;
		constexpr float Sav   = 1.0f;
		constexpr float Sev   = 0.8f;
		constexpr float Rec   = 2.2f;
	}
	namespace LodColMesh        // Meshes
	{
		constexpr float Asset = 2.8f;
		constexpr float Type  = 0.8f;   // Static / Skeletal
		constexpr float Tris  = 1.0f;   // LOD0 triangle count
		constexpr float Lods  = 0.9f;   // Nanite / LOD xN
		constexpr float Sav   = 1.0f;   // est. VRAM saving
		constexpr float Sev   = 0.8f;
		constexpr float Rec   = 2.8f;
	}
	namespace LodColMat         // Materials
	{
		constexpr float Asset = 2.8f;
		constexpr float Type  = 0.8f;   // Master / Instance
		constexpr float Blend = 0.9f;   // blend mode
		constexpr float Instr = 1.0f;   // compiled instruction count
		constexpr float Sav   = 1.0f;   // est. instruction saving
		constexpr float Sev   = 0.8f;
		constexpr float Rec   = 2.8f;
	}

	// Icon+label button content (same look as the other sections). File-local;
	// uniquely named so it never collides with the shared ShintBtnContent under
	// unity builds.
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

	// Server severity → accent colour + the High/Medium/Low label the mockup
	// shows. error→Critical(red), warning→High(orange), info→Low(muted).
	FLinearColor SeverityColor(const FString& Severity)
	{
		const FString S = Severity.ToLower();
		if (S == TEXT("error"))   return FShintStyle::Colors::SevCritical();
		if (S == TEXT("warning")) return FLinearColor(0.95f, 0.65f, 0.20f);
		return FShintStyle::Colors::TextMuted(); // info
	}

	FString SeverityLabel(const FString& Severity)
	{
		const FString S = Severity.ToLower();
		if (S == TEXT("error"))   return TEXT("Critical");
		if (S == TEXT("warning")) return TEXT("High");
		return TEXT("Low");
	}

	// "42.67 MB". Returns an em dash when the rule carries no size estimate.
	FString FmtSizeMb(double Mb)
	{
		return Mb > 0.0 ? FString::Printf(TEXT("%.2f MB"), Mb) : TEXT("—");
	}

	// Total memory impact — GB above 1024 MB, MB below.
	FString FmtImpact(double Mb)
	{
		return Mb >= 1024.0
			? FString::Printf(TEXT("%.2f GB"), Mb / 1024.0)
			: FString::Printf(TEXT("%.0f MB"), Mb);
	}

	// Map the server's recommended compression string to a UE setting.
	// Returns TC_MAX for unknown values (caller then skips the compression change).
	// Kept in sync with ShintLodFixerRegistry.cpp's MapCoreCompression — same
	// Core vocabulary, same target enum values (BC4 is TC_Alpha not
	// TC_Grayscale; BC6H is TC_HDR_Compressed not TC_HDR, which is
	// uncompressed RGBA16F).
	TextureCompressionSettings LodMapRecCompression(const FString& In)
	{
		const FString S = In.TrimStartAndEnd().ToUpper();
		if (S == TEXT("BC7"))                                   return TC_BC7;
		if (S == TEXT("BC5")  || S == TEXT("NORMALMAP"))        return TC_Normalmap;
		if (S == TEXT("BC4")  || S == TEXT("ALPHA"))            return TC_Alpha;
		if (S == TEXT("BC6H") || S == TEXT("BC6"))              return TC_HDR_Compressed;
		if (S == TEXT("RGBA8"))                                 return TC_EditorIcon;
		if (S == TEXT("BC1")  || S == TEXT("BC3") || S == TEXT("DXT1") ||
		    S == TEXT("DXT5") || S == TEXT("DEFAULT"))          return TC_Default;
		return TC_MAX;
	}

	// Identity key for a finding, used to hide a row the instant its fix
	// applies (see AppliedLodFixKeys) — findings have no server-issued id,
	// and asset_path + rule_id is the natural, stable substitute.
	FString LodFixKey(const FString& AssetPath, const FString& RuleId)
	{
		return AssetPath + TEXT("|") + RuleId;
	}

	void LodShowSuccessToast(const FString& Title, const FString& Detail)
	{
		FNotificationInfo Info(FText::FromString(Title));
		Info.SubText              = FText::FromString(Detail);
		Info.ExpireDuration       = 6.0f;
		Info.bUseSuccessFailIcons = true;
		TSharedPtr<SNotificationItem> N = FSlateNotificationManager::Get().AddNotification(Info);
		if (N.IsValid()) N->SetCompletionState(SNotificationItem::CS_Success);
		UE_LOG(LogShintTools, Log, TEXT("%s — %s"), *Title, *Detail);
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

			// Title row — title + subtitle. (The "Studio" badge chip that used to
			// sit above the KPI tiles was removed — cleaner, more compact cards;
			// tier gating is enforced functionally, not decoratively.)
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, FShintStyle::Space::S3)
			[
				BuildSectionTitle(
					LOCTEXT("AOTitle", "Asset Optimizer"),
					LOCTEXT("AOSub", "Audit textures · meshes · materials for memory + frame-time savings · apply fixes"))
			]

			// Scan bar — Scan button (fill) + target-platform selector.
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, FShintStyle::Space::S3)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.f).Padding(0.f, 0.f, 8.f, 0.f)
				[
					SAssignNew(AuditLodBtn, SButton).ContentPadding(FMargin(14.f, 9.f))
					.HAlign(HAlign_Center)
					.OnClicked(this, &SShintToolsPanel::OnAuditLodsClicked)
					[
						SAssignNew(AuditLodBtnLabel, STextBlock)
						.Text(LOCTEXT("AOScan", "Scan")).Font(F_Small())
						.ColorAndOpacity(FSlateColor(C_White()))
					]
				]
				// Deep Scan toggle — loads each mesh's source description to compute
				// geometry-integrity + normal stats (degenerate/duplicate verts,
				// non-manifold/open edges, tangent mirroring). Slower per mesh, so
				// it's opt-in; off = the fast property/render-data scan only.
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				  .Padding(0.f, 0.f, 8.f, 0.f)
				[
					SNew(SCheckBox)
					.IsChecked_Lambda([this]() {
						return bLodDeepScan ? ECheckBoxState::Checked
											: ECheckBoxState::Unchecked;
					})
					.OnCheckStateChanged_Lambda([this](ECheckBoxState S) {
						bLodDeepScan = (S == ECheckBoxState::Checked);
					})
					.ToolTipText(LOCTEXT("AODeepTip",
						"Load each mesh's source geometry to detect degenerate / "
						"duplicate / overlapping verts, non-manifold & open edges, "
						"and tangent issues. Slower — off uses the fast scan only."))
					[
						SNew(STextBlock).Text(LOCTEXT("AODeep", "Deep Scan"))
						.Font(F_Small()).ColorAndOpacity(FSlateColor(C_Gray()))
						.Margin(FMargin(6.f, 0.f, 0.f, 0.f))
					]
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(SComboButton)
					.ContentPadding(FMargin(12.f, 8.f))
					.ButtonColorAndOpacity(FSlateColor(C_Surface()))
					.OnGetMenuContent_Lambda([this]() -> TSharedRef<SWidget>
					{
						struct FPlat { FText Label; FString Profile; };
						const TArray<FPlat> Plats = {
							{ LOCTEXT("AOPlatDesktop", "DESKTOP (Windows)"), TEXT("default") },
							{ LOCTEXT("AOPlatMobile",  "MOBILE"),            TEXT("mobile")  },
						};
						TSharedRef<SVerticalBox> Menu = SNew(SVerticalBox);
						for (const FPlat& P : Plats)
						{
							Menu->AddSlot().AutoHeight()
							[
								SNew(SButton)
								.ButtonColorAndOpacity(FSlateColor(C_Surface()))
								.ContentPadding(FMargin(12.f, 6.f))
								.OnClicked_Lambda([this, Profile = P.Profile]() -> FReply
								{
									LodProfile = Profile;
									return FReply::Handled();
								})
								[
									SNew(STextBlock).Text(P.Label).Font(F_Label())
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
								? LOCTEXT("AOPlatMobileShort", "MOBILE")
								: LOCTEXT("AOPlatDesktopShort", "DESKTOP (Windows)");
						})
						.Font(FShintStyle::Fonts::Caption())
						.ColorAndOpacity(FSlateColor(FShintStyle::Colors::TextPrimary()))
					]
				]
			]

			// Top-level view nav — Summary / Assets / Fixes / Budgets (§21).
			// Rules was removed: the Unity client has no equivalent view, and
			// the plugin now ships to both engines from one contract.
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 10.f)
			[ BuildLodViewNav() ]

			// Active view (all four built once; the switcher keeps them alive so
			// KPI/treemap/list handles stay valid regardless of which is showing).
			+ SVerticalBox::Slot().AutoHeight()
			[
				SAssignNew(LodViewSwitcher, SWidgetSwitcher)

				// 0 — Summary
				+ SWidgetSwitcher::Slot()[ BuildLodSummaryView() ]

				// 1 — Assets (the existing tabs + filters + table)
				+ SWidgetSwitcher::Slot()
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 10.f)
					[ BuildLodToolbar() ]
					+ SVerticalBox::Slot().AutoHeight()
					[ BuildLodResultsPanel() ]
				]

				// 2 — Fixes   3 — Budgets
				+ SWidgetSwitcher::Slot()[ BuildLodFixesView() ]
				+ SWidgetSwitcher::Slot()[ BuildLodBudgetsView() ]
			]
		];
}

// ─────────────────────────────────────────────────────────────────────────────
// KPI tile row — FILES · MEMORY IMPACT · MEMORY SAVINGS · FRAME TIME · ISSUES
// ─────────────────────────────────────────────────────────────────────────────
TSharedRef<SWidget> SShintToolsPanel::BuildLodKpiRow()
{
	// Local tile builder — caption (muted) over a big value (white) over a
	// coloured breakdown subtitle. Captures the two labels by ref for refresh.
	auto Tile = [this](const FText& Caption,
		TSharedPtr<STextBlock>& OutValue, TSharedPtr<STextBlock>& OutSub,
		const FLinearColor& SubColor, const FText& InitialSub) -> TSharedRef<SWidget>
	{
		return SNew(SBorder)
			.BorderImage(ST4::Outline(FShintStyle::Colors::BgCard(),
				FShintStyle::Colors::BorderSubtle(), FShintStyle::Radius::Card))
			.Padding(FMargin(16.f, 14.f))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)
				[
					SNew(STextBlock).Text(Caption).Font(F_Label())
					.ColorAndOpacity(FSlateColor(FShintStyle::Colors::TextMuted()))
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					SAssignNew(OutValue, STextBlock).Text(FText::FromString(TEXT("—")))
					.Font(FShintStyle::Fonts::H1())
					.ColorAndOpacity(FSlateColor(C_White()))
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 4.f, 0.f, 0.f)
				[
					SAssignNew(OutSub, STextBlock).Text(InitialSub)
					.Font(F_Label()).ColorAndOpacity(FSlateColor(SubColor))
				]
			];
	};

	// MEMORY IMPACT / FRAME TIME use static subtitles — throwaway sub handles.
	TSharedPtr<STextBlock> ImpactSub, FrameSub;
	const float Gap = FShintStyle::Space::S2 * 0.5f;
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.f).Padding(FMargin(0.f, 0.f, Gap, 0.f))
		[ Tile(LOCTEXT("AOKpiFiles", "FILES"), LodFiles_Label, LodFilesSub_Label,
			FShintStyle::Colors::TextMuted(), FText::GetEmpty()) ]
		+ SHorizontalBox::Slot().FillWidth(1.f).Padding(FMargin(Gap, 0.f, Gap, 0.f))
		[ Tile(LOCTEXT("AOKpiMem", "MEMORY IMPACT"), LodMemImpact_Label, ImpactSub,
			FShintStyle::Colors::TextMuted(), LOCTEXT("AOKpiMemSub", "Total Estimated")) ]
		+ SHorizontalBox::Slot().FillWidth(1.f).Padding(FMargin(Gap, 0.f, Gap, 0.f))
		[ Tile(LOCTEXT("AOKpiSave", "MEMORY SAVINGS"), LodMemSavings_Label, LodSavingsPct_Label,
			FShintStyle::Colors::SevLow(), FText::GetEmpty()) ]
		+ SHorizontalBox::Slot().FillWidth(1.f).Padding(FMargin(Gap, 0.f, Gap, 0.f))
		[ Tile(LOCTEXT("AOKpiFrame", "FRAME TIME SAVINGS"), LodFrameTime_Label, FrameSub,
			FShintStyle::Colors::SevLow(), LOCTEXT("AOKpiFrameSub", "Est. Improvement")) ]
		+ SHorizontalBox::Slot().FillWidth(1.f).Padding(FMargin(Gap, 0.f, 0.f, 0.f))
		[ Tile(LOCTEXT("AOKpiIssues", "ISSUES"), LodIssues_Label, LodIssuesSub_Label,
			FShintStyle::Colors::TextMuted(), FText::GetEmpty()) ];
}

// ─────────────────────────────────────────────────────────────────────────────
// Toolbar — tabs (Textures/Meshes/Materials) + filter row.
// ─────────────────────────────────────────────────────────────────────────────
TSharedRef<SWidget> SShintToolsPanel::BuildLodToolbar()
{
	// One tab button. Highlights when active; switches tab + re-filters on click.
	auto TabBtn = [this](const FText& Label, ELodTab Tab) -> TSharedRef<SWidget>
	{
		return SNew(SButton)
			.ContentPadding(FMargin(14.f, 7.f))
			.ButtonColorAndOpacity_Lambda([this, Tab]() {
				return FSlateColor(LodActiveTab == Tab ? C_Surface() : C_BG());
			})
			.OnClicked_Lambda([this, Tab]() { SetLodTab(Tab); return FReply::Handled(); })
			[
				SNew(STextBlock).Text(Label).Font(F_Small())
				.ColorAndOpacity_Lambda([this, Tab]() {
					return FSlateColor(LodActiveTab == Tab ? C_White() : C_Gray());
				})
			];
	};

	// A filter combo: shows LabelGetter, lists OptionsGetter() (with an "All"
	// reset already included by the caller), calls OnPick on selection.
	auto FilterCombo = [this](TFunction<FText()> LabelGetter,
		TFunction<TArray<FString>()> OptionsGetter,
		TFunction<void(const FString&)> OnPick) -> TSharedRef<SWidget>
	{
		return SNew(SComboButton)
			.ContentPadding(FMargin(10.f, 6.f))
			.ButtonColorAndOpacity(FSlateColor(C_Surface()))
			.OnGetMenuContent_Lambda([this, OptionsGetter, OnPick]() -> TSharedRef<SWidget>
			{
				TSharedRef<SVerticalBox> Menu = SNew(SVerticalBox);
				for (const FString& Opt : OptionsGetter())
				{
					Menu->AddSlot().AutoHeight()
					[
						SNew(SButton)
						.ButtonColorAndOpacity(FSlateColor(C_Surface()))
						.ContentPadding(FMargin(12.f, 6.f))
						.OnClicked_Lambda([this, Opt, OnPick]() -> FReply {
							OnPick(Opt);
							RefreshLodFilteredList();
							return FReply::Handled();
						})
						[
							SNew(STextBlock).Text(FText::FromString(Opt)).Font(F_Label())
							.ColorAndOpacity(FSlateColor(C_White()))
						]
					];
				}
				return SNew(SBorder).BorderImage(ST4::Solid(C_Surface())).Padding(2.f)[ Menu ];
			})
			.ButtonContent()
			[
				SNew(STextBlock).Text_Lambda(MoveTemp(LabelGetter))
				.Font(FShintStyle::Fonts::Caption())
				.ColorAndOpacity(FSlateColor(FShintStyle::Colors::TextPrimary()))
			];
	};

	// Distinct values present in the current findings, for the group/format menus.
	auto DistinctValues = [this](TFunction<FString(const FShintLodFinding&)> Field,
		const FString& AllLabel) -> TArray<FString>
	{
		TArray<FString> Out;
		Out.Add(AllLabel);
		for (const FShintLodFindingPtr& It : LodFindingItems)
		{
			const FString V = Field(It->Finding);
			if (!V.IsEmpty() && !Out.Contains(V)) Out.Add(V);
		}
		return Out;
	};

	return SNew(SVerticalBox)
		// Tabs
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 4.f, 0.f)
			[ TabBtn(LOCTEXT("AOTabTex", "Textures"),   ELodTab::Textures) ]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 4.f, 0.f)
			[ TabBtn(LOCTEXT("AOTabMesh", "Meshes"),    ELodTab::Meshes) ]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 4.f, 0.f)
			[ TabBtn(LOCTEXT("AOTabMat", "Materials"),  ELodTab::Materials) ]
			+ SHorizontalBox::Slot().AutoWidth()
			[ TabBtn(LOCTEXT("AOTabOther", "Other"),    ELodTab::Other) ]
		]
		// Filter row
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SHorizontalBox)
			// Search
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center).Padding(0.f, 0.f, 8.f, 0.f)
			[
				SNew(SEditableTextBox)
				.HintText(LOCTEXT("AOSearch", "Search"))
				.OnTextChanged_Lambda([this](const FText& T) {
					LodSearchText = T.ToString();
					RefreshLodFilteredList();
				})
			]
			// Group filter
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 6.f, 0.f)
			[
				FilterCombo(
					[this]() { return FText::FromString(LodGroupFilter); },
					[this, DistinctValues]() {
						return DistinctValues([](const FShintLodFinding& F){ return F.Group; }, TEXT("All Groups"));
					},
					[this](const FString& V) { LodGroupFilter = V; })
			]
			// Format filter
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 6.f, 0.f)
			[
				FilterCombo(
					[this]() { return FText::FromString(LodFormatFilter); },
					[this, DistinctValues]() {
						return DistinctValues([](const FShintLodFinding& F){ return F.Format; }, TEXT("All Formats"));
					},
					[this](const FString& V) { LodFormatFilter = V; })
			]
			// Severity filter
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 12.f, 0.f)
			[
				FilterCombo(
					[this]() { return FText::FromString(LodSeverityFilter); },
					[]() { return TArray<FString>{ TEXT("All Severities"), TEXT("Critical"), TEXT("High"), TEXT("Low") }; },
					[this](const FString& V) { LodSeverityFilter = V; })
			]
			// Select All / Deselect All — operate on the current tab's rows and
			// drive the checked-rows "Fix (N)" bulk action.
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 4.f, 0.f)
			[
				SNew(SButton).ContentPadding(FMargin(12.f, 6.f))
				.ButtonColorAndOpacity(FSlateColor(C_Surface()))
				.OnClicked_Lambda([this]() { SetLodAllChecked(true);  return FReply::Handled(); })
				[
					SNew(STextBlock).Text(LOCTEXT("AOSelAll", "Select All")).Font(F_Small())
					.ColorAndOpacity(FSlateColor(C_Gray()))
				]
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 10.f, 0.f)
			[
				SNew(SButton).ContentPadding(FMargin(12.f, 6.f))
				.ButtonColorAndOpacity(FSlateColor(C_Surface()))
				.OnClicked_Lambda([this]() { SetLodAllChecked(false); return FReply::Handled(); })
				[
					SNew(STextBlock).Text(LOCTEXT("AODesAll", "Deselect All")).Font(F_Small())
					.ColorAndOpacity(FSlateColor(C_Gray()))
				]
			]
			// Export
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 6.f, 0.f)
			[
				SNew(SButton).ContentPadding(FMargin(12.f, 6.f))
				.ButtonColorAndOpacity(FSlateColor(C_Surface()))
				.OnClicked(this, &SShintToolsPanel::OnLodExport)
				[
					SNew(STextBlock).Text(LOCTEXT("AOExport", "Export")).Font(F_Small())
					.ColorAndOpacity(FSlateColor(C_Gray()))
				]
			]
			// Send to Dashboard (metrics only)
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 6.f, 0.f)
			[
				SNew(SButton).ContentPadding(FMargin(12.f, 6.f))
				.ButtonColorAndOpacity(FSlateColor(C_Surface()))
				.OnClicked(this, &SShintToolsPanel::OnSendLodToDashboardClicked)
				[
					SAssignNew(SendLodBtnLabel, STextBlock)
					.Text(LOCTEXT("AODash", "Send to Dashboard")).Font(F_Small())
					.ColorAndOpacity(FSlateColor(C_Gray()))
				]
			]
			// Bulk Fix (N) — applies the *checked* rows.
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SButton).ContentPadding(FMargin(12.f, 6.f))
				.OnClicked(this, &SShintToolsPanel::OnLodFixSelected)
				[
					SAssignNew(LodFixSelected_Label, STextBlock)
					.Text(LOCTEXT("AOFixN", "Fix")).Font(F_Small())
					.ColorAndOpacity(FSlateColor(C_White()))
				]
			]
		];
}

// ─────────────────────────────────────────────────────────────────────────────
// Table header + results list.
// ─────────────────────────────────────────────────────────────────────────────
TSharedRef<SWidget> SShintToolsPanel::BuildLodTableHeader()
{
	// Shared shell: checkbox spacer + per-tab cells + Fix spacer. Each tab gets
	// only the columns that mean something for its asset family (rebuilt on tab
	// switch via LodTableHeaderBox).
	TSharedRef<SHorizontalBox> Cells = SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 8.f, 0.f)
		[ SNew(SBox).WidthOverride(20.f) ];

	auto HdrCell = [&Cells](const FText& Label, float Fill)
	{
		Cells->AddSlot().FillWidth(Fill).VAlign(VAlign_Center)
		[
			SNew(STextBlock).Text(Label)
			.Font(FShintStyle::Fonts::Caption())
			.ColorAndOpacity(FSlateColor(FShintStyle::Colors::TextMuted()))
		];
	};

	switch (LodActiveTab)
	{
	case ELodTab::Meshes:
		HdrCell(LOCTEXT("AOColAsset", "ASSET"),          LodColMesh::Asset);
		HdrCell(LOCTEXT("AOColMType", "TYPE"),           LodColMesh::Type);
		HdrCell(LOCTEXT("AOColTris",  "TRIANGLES"),      LodColMesh::Tris);
		HdrCell(LOCTEXT("AOColLods",  "RENDER PATH"),    LodColMesh::Lods);
		HdrCell(LOCTEXT("AOColMSav",  "EST. SAVINGS"),   LodColMesh::Sav);
		HdrCell(LOCTEXT("AOColSev",   "SEVERITY"),       LodColMesh::Sev);
		HdrCell(LOCTEXT("AOColRec",   "RECOMMENDATION"), LodColMesh::Rec);
		break;
	case ELodTab::Materials:
		HdrCell(LOCTEXT("AOColAsset", "ASSET"),          LodColMat::Asset);
		HdrCell(LOCTEXT("AOColXType", "TYPE"),           LodColMat::Type);
		HdrCell(LOCTEXT("AOColBlend", "BLEND MODE"),     LodColMat::Blend);
		HdrCell(LOCTEXT("AOColInstr", "INSTRUCTIONS"),   LodColMat::Instr);
		HdrCell(LOCTEXT("AOColXSav",  "EST. SAVINGS"),   LodColMat::Sav);
		HdrCell(LOCTEXT("AOColSev",   "SEVERITY"),       LodColMat::Sev);
		HdrCell(LOCTEXT("AOColRec",   "RECOMMENDATION"), LodColMat::Rec);
		break;
	case ELodTab::Other:
		// No family-specific numeric columns exist for this bucket (VFX/
		// Mobile/Animation/Lighting/Audio findings have heterogeneous
		// shapes) — CATEGORY replaces the family-specific cells so the row
		// still says what kind of finding it is.
		HdrCell(LOCTEXT("AOColAsset", "ASSET"),          LodCol::Asset);
		HdrCell(LOCTEXT("AOColOCat",  "CATEGORY"),       LodCol::Group + LodCol::Res + LodCol::Fmt);
		HdrCell(LOCTEXT("AOColOSav",  "EST. SAVINGS"),   LodCol::Cur + LodCol::Pot);
		HdrCell(LOCTEXT("AOColSav",   "SAVINGS"),        LodCol::Sav);
		HdrCell(LOCTEXT("AOColSev",   "SEVERITY"),       LodCol::Sev);
		HdrCell(LOCTEXT("AOColRec",   "RECOMMENDATION"), LodCol::Rec);
		break;
	default: // Textures
		HdrCell(LOCTEXT("AOColAsset", "ASSET"),          LodCol::Asset);
		HdrCell(LOCTEXT("AOColGroup", "GROUP"),          LodCol::Group);
		HdrCell(LOCTEXT("AOColRes",   "RESOLUTION"),     LodCol::Res);
		HdrCell(LOCTEXT("AOColFmt",   "FORMAT"),         LodCol::Fmt);
		HdrCell(LOCTEXT("AOColCur",   "CURRENT SIZE"),   LodCol::Cur);
		HdrCell(LOCTEXT("AOColPot",   "POTENTIAL SIZE"), LodCol::Pot);
		HdrCell(LOCTEXT("AOColSav",   "SAVINGS"),        LodCol::Sav);
		HdrCell(LOCTEXT("AOColSev",   "SEVERITY"),       LodCol::Sev);
		HdrCell(LOCTEXT("AOColRec",   "RECOMMENDATION"), LodCol::Rec);
		break;
	}
	Cells->AddSlot().AutoWidth().Padding(8.f, 0.f, 0.f, 0.f)
	[ SNew(SBox).WidthOverride(48.f) ];

	return SNew(SBorder)
		.BorderImage(ST4::Solid(C_BG()))
		.Padding(FMargin(8.f, 6.f))
		[ Cells ];
}

TSharedRef<SWidget> SShintToolsPanel::BuildLodResultsPanel()
{
	SAssignNew(LodEmptyState, SBox)
	.HAlign(HAlign_Center).VAlign(VAlign_Center).MinDesiredHeight(64.f)
	[
		SNew(STextBlock)
		// Before any scan: generic prompt. After a scan: name the tab that has
		// no findings, so an empty Materials tab reads as a clean result, not
		// as a scan that didn't run.
		.Text_Lambda([this]() -> FText {
			if (LodFindingItems.IsEmpty())
				return LOCTEXT("AOEmpty", "Run a scan to see optimization findings.");
			switch (LodActiveTab)
			{
			case ELodTab::Meshes:    return LOCTEXT("AOEmptyMesh", "No mesh findings — meshes look clean.");
			case ELodTab::Materials: return LOCTEXT("AOEmptyMat",  "No material findings — materials look clean.");
			case ELodTab::Other:     return LOCTEXT("AOEmptyOther","No other findings.");
			default:                 return LOCTEXT("AOEmptyTex",  "No texture findings — textures look clean.");
			}
		})
		.Font(F_Small()).ColorAndOpacity(FSlateColor(C_DimGray()))
	];

	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			// Rebuilt on tab switch — each tab renders its own column set.
			SAssignNew(LodTableHeaderBox, SBox)
			[ BuildLodTableHeader() ]
		]

		+ SVerticalBox::Slot().AutoHeight()
		[ LodEmptyState.ToSharedRef() ]

		+ SVerticalBox::Slot().AutoHeight().MaxHeight(520.f)
		[
			SAssignNew(LodFindingListView, SListView<FShintLodFindingPtr>)
			.ListItemsSource(&LodFilteredItems)
			.SelectionMode(ESelectionMode::None)
			.OnGenerateRow(this, &SShintToolsPanel::GenerateLodFindingRow)
		];
}

// ─────────────────────────────────────────────────────────────────────────────
// Row factory — one finding per row, column-aligned with the header.
// ─────────────────────────────────────────────────────────────────────────────
TSharedRef<ITableRow> SShintToolsPanel::GenerateLodFindingRow(
	FShintLodFindingPtr Item, const TSharedRef<STableViewBase>& Owner)
{
	const FShintLodFinding& F = Item->Finding;

	const FString AssetName = FPaths::GetCleanFilename(F.AssetPath);
	// RESOLUTION is per-family: textures render WxH; meshes/materials carry a
	// pre-formatted ResText ("12,345 tris" / "140 instr") from the collector.
	const FString Resolution =
		!F.ResText.IsEmpty() ? F.ResText :
		(F.Width > 0 && F.Height > 0)
			? FString::Printf(TEXT("%dx%d"), F.Width, F.Height) : TEXT("—");
	const FString SavingsPct = (F.CurrentVramMb > 0.0 && F.VramMb > 0.0)
		? FString::Printf(TEXT("%.0f%%"), (F.VramMb / F.CurrentVramMb) * 100.0)
		: FString();

	// Stage 2b: render the real asset thumbnail (falls back to a neutral swatch
	// when the asset can't be resolved). The FAssetThumbnail is stored on the
	// item so it outlives the widget; one shared pool backs every row.
	if (!LodThumbnailPool.IsValid())
		LodThumbnailPool = MakeShared<FAssetThumbnailPool>(48);
	TSharedRef<SWidget> ThumbWidget =
		SNew(SBorder).BorderImage(ST4::Solid(C_Surface()))[ SNew(SBox) ];
	{
		IAssetRegistry& AR = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(
			TEXT("AssetRegistry")).Get();
		TArray<FAssetData> Found;
		AR.GetAssetsByPackageName(FName(*F.AssetPath), Found);
		if (Found.Num() > 0)
		{
			Item->Thumbnail = MakeShared<FAssetThumbnail>(Found[0], 34, 34, LodThumbnailPool);
			ThumbWidget = Item->Thumbnail->MakeThumbnailWidget(FAssetThumbnailConfig());
		}
	}

	// Row skeleton: checkbox + ASSET cell, then the active tab's data cells,
	// then SEVERITY / RECOMMENDATION / Fix. Cell widths mirror the matching
	// per-tab header namespaces so the columns line up.
	TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox)

		// Checkbox
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 8.f, 0.f)
		[
			SNew(SBox).WidthOverride(20.f)
			[
				SNew(SCheckBox)
				.IsChecked_Lambda([Item]() {
					return Item->bChecked ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
				})
				.OnCheckStateChanged_Lambda([this, Item](ECheckBoxState S) {
					Item->bChecked = (S == ECheckBoxState::Checked);
					if (LodFixSelected_Label.IsValid())
						LodFixSelected_Label->SetText(FText::FromString(
							FString::Printf(TEXT("Fix (%d)"), LodCheckedCount())));
				})
			]
		];

	// ASSET: thumbnail + name + path (same width across all three tabs).
	Row->AddSlot().FillWidth(LodCol::Asset).VAlign(VAlign_Center)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 8.f, 0.f)
		[
			SNew(SBox).WidthOverride(34.f).HeightOverride(34.f)
			[ ThumbWidget ]
		]
		+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(STextBlock).Text(FText::FromString(AssetName))
				.Font(F_Small()).ColorAndOpacity(FSlateColor(C_White()))
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(STextBlock).Text(FText::FromString(F.AssetPath))
				.Font(F_Label()).ColorAndOpacity(FSlateColor(C_DimGray()))
			]
		]
	];

	// Small helpers for the per-tab data cells.
	auto TextCell = [&Row](const FString& Value, float Fill, bool bMono,
		const FLinearColor& Color)
	{
		Row->AddSlot().FillWidth(Fill).VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Text(FText::FromString(Value.IsEmpty() ? TEXT("—") : Value))
			.Font(bMono ? F_Mono() : F_Small())
			.ColorAndOpacity(FSlateColor(Color))
		];
	};

	switch (LodActiveTab)
	{
	case ELodTab::Meshes:
		// TYPE · TRIANGLES · RENDER PATH · EST. SAVINGS
		TextCell(F.Group,   LodColMesh::Type, false, C_Gray());
		TextCell(F.ResText, LodColMesh::Tris, true,  C_Gray());
		TextCell(F.Format,  LodColMesh::Lods, true,  C_Gray());
		TextCell(F.VramMb > 0.0
			? FString::Printf(TEXT("%.1f MB"), F.VramMb) : FString(),
			LodColMesh::Sav, false, FShintStyle::Colors::SevLow());
		break;
	case ELodTab::Materials:
		// TYPE · BLEND MODE · INSTRUCTIONS · EST. SAVINGS
		TextCell(F.Group,   LodColMat::Type,  false, C_Gray());
		TextCell(F.Format,  LodColMat::Blend, true,  C_Gray());
		TextCell(F.ResText, LodColMat::Instr, true,  C_Gray());
		TextCell(F.ShaderInstructions > 0
			? FString::Printf(TEXT("%d instr"), F.ShaderInstructions)
			: (F.VramMb > 0.0
				? FString::Printf(TEXT("%.1f MB"), F.VramMb) : FString()),
			LodColMat::Sav, false, FShintStyle::Colors::SevLow());
		break;
	case ELodTab::Other:
		// CATEGORY (as reported by the server) · EST. SAVINGS, no per-family
		// numeric fields apply here.
		TextCell(F.Category, LodCol::Group + LodCol::Res + LodCol::Fmt, false, C_Gray());
		TextCell(F.VramMb > 0.0 ? FString::Printf(TEXT("%.1f MB"), F.VramMb) : FString(),
			LodCol::Cur + LodCol::Pot, false, FShintStyle::Colors::SevLow());
		TextCell(FString(), LodCol::Sav, false, C_Gray());   // no per-item % — no baseline to compare against
		break;
	default: // Textures — GROUP · RESOLUTION · FORMAT · CURRENT · POTENTIAL · SAVINGS
		TextCell(F.Group,     LodCol::Group, false, C_Gray());
		TextCell(Resolution,  LodCol::Res,   true,  C_Gray());
		TextCell(F.Format,    LodCol::Fmt,   true,  C_Gray());
		TextCell(FmtSizeMb(F.CurrentVramMb), LodCol::Cur, false,
			FShintStyle::Colors::SevCritical());
		TextCell(FmtSizeMb(F.PotentialVramMb), LodCol::Pot, false,
			FShintStyle::Colors::SevLow());
		Row->AddSlot().FillWidth(LodCol::Sav).VAlign(VAlign_Center)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(STextBlock)
				.Text(FText::FromString(F.VramMb > 0.0
					? FString::Printf(TEXT("%.1f MB"), F.VramMb) : TEXT("—")))
				.Font(F_Small()).ColorAndOpacity(FSlateColor(FShintStyle::Colors::SevLow()))
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(STextBlock).Text(FText::FromString(SavingsPct))
				.Font(F_Label()).ColorAndOpacity(FSlateColor(FShintStyle::Colors::SevLow()))
			]
		];
		break;
	}

	const float SevFill = LodActiveTab == ELodTab::Meshes    ? LodColMesh::Sev :
	                      LodActiveTab == ELodTab::Materials ? LodColMat::Sev  : LodCol::Sev;
	const float RecFill = LodActiveTab == ELodTab::Meshes    ? LodColMesh::Rec :
	                      LodActiveTab == ELodTab::Materials ? LodColMat::Rec  : LodCol::Rec;

	// SEVERITY
	TextCell(SeverityLabel(F.Severity), SevFill, false, SeverityColor(F.Severity));
	// RECOMMENDATION
	Row->AddSlot().FillWidth(RecFill).VAlign(VAlign_Center)
	[
		SNew(STextBlock).Text(FText::FromString(F.Message))
		.Font(F_Label()).ColorAndOpacity(FSlateColor(C_Gray())).AutoWrapText(true)
	];
	// Fix
	Row->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(8.f, 0.f, 0.f, 0.f)
	[
		SNew(SBox).WidthOverride(48.f)
		[
			SNew(SButton)
			// Show Fix only when a fix can actually be applied in-editor — not
			// merely when the server flagged the rule auto_fixable. Many mesh/
			// material rules are auto_fixable with advisory recommendations
			// (reduce samplers, add LODs) that no property write satisfies;
			// showing Fix on those produced the "Auto-fix failed" toast.
			.Visibility(FShintLodFixerRegistry::IsAutoApplicable(F.Recommended)
				? EVisibility::Visible : EVisibility::Collapsed)
			.ContentPadding(FMargin(8.f, 4.f))
			.ButtonColorAndOpacity(FSlateColor(C_Surface()))
			.OnClicked_Lambda([this, Item]() { return OnLodFixRow(Item); })
			[
				SNew(STextBlock).Text(LOCTEXT("AOFix", "Fix")).Font(F_Label())
				.ColorAndOpacity(FSlateColor(C_White()))
			]
		]
	];

	return SNew(STableRow<FShintLodFindingPtr>, Owner)
		.Padding(FMargin(0.f, 1.f))
		[
			SNew(SBorder)
			.BorderImage(ST4::Solid(C_BG()))
			.Padding(FMargin(8.f, 8.f))
			[ Row ]
		];
}

// ─────────────────────────────────────────────────────────────────────────────
// Handlers
// ─────────────────────────────────────────────────────────────────────────────
FReply SShintToolsPanel::OnAuditLodsClicked()
{
	// Defensive tier guard — the rail entry is hidden for non-Studio users, but
	// a stale cached tier shouldn't fire a request the server will 403.
	const FString Tier = FShintToolsModule::GetCachedTier().ToLower();
	if (!Tier.IsEmpty() && Tier != TEXT("studio") && Tier != TEXT("enterprise"))
	{
		ShintShowErrorToast(
			TEXT("Asset Optimizer requires Studio"),
			TEXT("Paste a Studio license key in Settings to unlock the Asset Optimizer."));
		return FReply::Handled();
	}

	LodState = EModuleState::Running;
	if (AuditLodBtn.IsValid()) AuditLodBtn->SetEnabled(false);
	if (AuditLodBtnLabel.IsValid())
		AuditLodBtnLabel->SetText(LOCTEXT("AOScanning", "Scanning…"));

	LodFindingItems.Empty();
	LodFilteredItems.Empty();
	// A fresh scan's result is authoritative the moment it lands — the
	// session-scoped "hide this immediately" set from the previous scan no
	// longer needs to override it.
	AppliedLodFixKeys.Empty();
	if (LodFindingListView.IsValid()) LodFindingListView->RebuildList();
	if (LodEmptyState.IsValid()) LodEmptyState->SetVisibility(EVisibility::Visible);

	CoreClient->AuditLods(LodProfile, bLodExplainTop,
		FOnShintLodAuditComplete::CreateSP(this, &SShintToolsPanel::OnLodAuditComplete),
		bLodDeepScan);
	return FReply::Handled();
}

void SShintToolsPanel::OnLodAuditComplete(const FShintLodAuditResult& Result)
{
	LodState = Result.bSuccess ? EModuleState::Done : EModuleState::Error;
	if (AuditLodBtn.IsValid()) AuditLodBtn->SetEnabled(true);
	if (AuditLodBtnLabel.IsValid())
		AuditLodBtnLabel->SetText(LOCTEXT("AOScan", "Scan"));

	if (!Result.bSuccess)
	{
		ShintShowErrorToast(TEXT("Scan failed"),
			Result.ErrorMessage.IsEmpty()
				? TEXT("The Core engine did not return a valid response.")
				: Result.ErrorMessage);
		return;
	}

	LastLodResult = Result;
	PopulateLodFindingList(Result);
	RefreshLodStats();
	RefreshLodTreemap();     // Summary view — VRAM-by-asset picture

	// Per-family completion summary, mirroring the KPI subtitle breakdown.
	int32 TexIssues = 0, MeshIssues = 0, MatIssues = 0, OtherIssues = 0;
	for (const FShintLodFinding& F : Result.Findings)
	{
		if (F.Category.Contains(TEXT("Texture")))        ++TexIssues;
		else if (F.Category.Contains(TEXT("Mesh")))      ++MeshIssues;
		else if (F.Category.Contains(TEXT("Material")))  ++MatIssues;
		else                                              ++OtherIssues;
	}
	LodShowSuccessToast(TEXT("Scan complete"),
		OtherIssues > 0
			? FString::Printf(TEXT("%d assets audited — %d issues (Tex %d · Mesh %d · Mat %d · Other %d)."),
				Result.AssetsAudited, Result.IssuesFound,
				TexIssues, MeshIssues, MatIssues, OtherIssues)
			: FString::Printf(TEXT("%d assets audited — %d issues (Tex %d · Mesh %d · Mat %d)."),
				Result.AssetsAudited, Result.IssuesFound,
				TexIssues, MeshIssues, MatIssues));
}

void SShintToolsPanel::PopulateLodFindingList(const FShintLodAuditResult& Result)
{
	LodFindingItems.Empty(Result.Findings.Num());
	for (const FShintLodFinding& F : Result.Findings)
	{
		// Belt-and-suspenders against a finding the user already fixed this
		// session reappearing — normally a re-scan simply won't re-flag it
		// (the in-memory asset already carries the fixed value), but this
		// covers the case where the fix hasn't propagated to whatever the
		// scan actually reads from yet.
		if (AppliedLodFixKeys.Contains(LodFixKey(F.AssetPath, F.RuleId)))
			continue;
		TSharedPtr<FShintLodFindingItem> Item = MakeShared<FShintLodFindingItem>();
		Item->Finding = F;
		LodFindingItems.Add(Item);
	}
	RefreshLodFilteredList();
}

void SShintToolsPanel::RemoveFixedLodFinding(const FString& AssetPath, const FString& RuleId)
{
	RemoveFixedLodFindings({ TPair<FString, FString>(AssetPath, RuleId) });
}

void SShintToolsPanel::RemoveFixedLodFindings(const TArray<TPair<FString, FString>>& Keys)
{
	if (Keys.Num() == 0) return;
	for (const TPair<FString, FString>& K : Keys)
		AppliedLodFixKeys.Add(LodFixKey(K.Key, K.Value));

	for (int32 i = LastLodResult.Findings.Num() - 1; i >= 0; --i)
	{
		const FShintLodFinding& F = LastLodResult.Findings[i];
		const bool bFixed = Keys.ContainsByPredicate([&](const TPair<FString, FString>& K)
		{
			return K.Key == F.AssetPath && K.Value == F.RuleId;
		});
		if (!bFixed) continue;
		LastLodResult.IssuesFound = FMath::Max(0, LastLodResult.IssuesFound - 1);
		LastLodResult.EstimatedVramSavedMb =
			FMath::Max(0.0, LastLodResult.EstimatedVramSavedMb - F.VramMb);
		LastLodResult.Findings.RemoveAt(i);
	}

	// Single rebuild path — the same one OnLodAuditComplete uses — so the
	// table, KPIs and treemap can never disagree about which findings exist.
	PopulateLodFindingList(LastLodResult);
	RefreshLodStats();
	RefreshLodTreemap();
}

void SShintToolsPanel::SetLodTab(ELodTab Tab)
{
	LodActiveTab = Tab;
	if (LodTableHeaderBox.IsValid())
		LodTableHeaderBox->SetContent(BuildLodTableHeader());
	RefreshLodFilteredList();
}

int32 SShintToolsPanel::LodCheckedCount() const
{
	// Global, not scoped to the current tab: OnLodFixSelected applies every
	// checked row regardless of which tab is active when Fix is pressed, so
	// the label must count the same set it will actually act on — otherwise
	// switching tabs after checking rows silently drops the "N" from "Fix
	// (N)" while those rows are still queued.
	int32 N = 0;
	for (const FShintLodFindingPtr& It : LodFindingItems)
		if (It.IsValid() && It->bChecked) ++N;
	return N;
}

void SShintToolsPanel::SetLodAllChecked(bool bChecked)
{
	// Scoped to the current tab's visible rows — Select All on Textures must
	// not silently queue hidden mesh/material rows into the bulk Fix.
	for (const FShintLodFindingPtr& It : LodFilteredItems)
		if (It.IsValid()) It->bChecked = bChecked;
	if (LodFixSelected_Label.IsValid())
		LodFixSelected_Label->SetText(FText::FromString(
			FString::Printf(TEXT("Fix (%d)"), LodCheckedCount())));
}

namespace
{
	// True when `Category` belongs on the given tab. Other is a negative
	// match (anything not Texture/Mesh/Material) so a finding always has a
	// tab it can show up on — no category is silently uncovered.
	bool LodCategoryMatchesTab(const FString& Category, ELodTab Tab)
	{
		const bool bTex = Category.Contains(TEXT("Texture"));
		const bool bMesh = Category.Contains(TEXT("Mesh"));
		const bool bMat = Category.Contains(TEXT("Material"));
		switch (Tab)
		{
		case ELodTab::Textures:  return bTex;
		case ELodTab::Meshes:    return bMesh;
		case ELodTab::Materials: return bMat;
		default:                 return !bTex && !bMesh && !bMat;   // Other
		}
	}
}

void SShintToolsPanel::RefreshLodFilteredList()
{
	const FString Search = LodSearchText.TrimStartAndEnd();

	LodFilteredItems.Empty(LodFindingItems.Num());
	for (const FShintLodFindingPtr& It : LodFindingItems)
	{
		const FShintLodFinding& F = It->Finding;

		if (!LodCategoryMatchesTab(F.Category, LodActiveTab)) continue;
		if (!Search.IsEmpty() &&
			!F.AssetPath.Contains(Search, ESearchCase::IgnoreCase)) continue;
		if (LodGroupFilter != TEXT("All Groups") && F.Group != LodGroupFilter) continue;
		if (LodFormatFilter != TEXT("All Formats") && F.Format != LodFormatFilter) continue;
		if (LodSeverityFilter != TEXT("All Severities") &&
			SeverityLabel(F.Severity) != LodSeverityFilter) continue;

		LodFilteredItems.Add(It);
	}

	if (LodFindingListView.IsValid()) LodFindingListView->RebuildList();
	if (LodEmptyState.IsValid())
		LodEmptyState->SetVisibility(
			LodFilteredItems.IsEmpty() ? EVisibility::Visible : EVisibility::Collapsed);
	// Reflect rows checked in OTHER tabs too — Fix Selected acts on all of
	// them, not just what's currently visible (see LodCheckedCount).
	if (LodFixSelected_Label.IsValid())
	{
		const int32 N = LodCheckedCount();
		LodFixSelected_Label->SetText(N > 0
			? FText::FromString(FString::Printf(TEXT("Fix (%d)"), N))
			: LOCTEXT("AOFixN", "Fix"));
	}
}

void SShintToolsPanel::RefreshLodStats()
{
	const FShintLodAuditResult& R = LastLodResult;

	// Per-category issue counts (from findings) for the breakdown subtitles.
	// Every finding lands in exactly one bucket (Other is the catch-all) so
	// Tex+Mesh+Mat+Other always reconciles with IssuesFound — previously
	// anything outside the three named families (e.g. Mobile-profile
	// findings) counted toward IssuesFound but into none of these buckets,
	// so the subtitle's own numbers never summed to the header above it.
	int32 TexIssues = 0, MeshIssues = 0, MatIssues = 0, OtherIssues = 0;
	for (const FShintLodFinding& F : R.Findings)
	{
		if (F.Category.Contains(TEXT("Texture")))        ++TexIssues;
		else if (F.Category.Contains(TEXT("Mesh")))      ++MeshIssues;
		else if (F.Category.Contains(TEXT("Material")))  ++MatIssues;
		else                                              ++OtherIssues;
	}

	if (LodFiles_Label.IsValid())
		LodFiles_Label->SetText(FText::FromString(FmtN(R.AssetsAudited)));
	if (LodFilesSub_Label.IsValid())
		LodFilesSub_Label->SetText(FText::FromString(FString::Printf(
			TEXT("Tex: %d   Mesh: %d   Mat: %d"),
			R.TexturesAudited, R.MeshesAudited, R.MaterialsAudited)));

	if (LodMemImpact_Label.IsValid())
		LodMemImpact_Label->SetText(FText::FromString(FmtImpact(R.TotalVramMb)));

	if (LodMemSavings_Label.IsValid())
		LodMemSavings_Label->SetText(FText::FromString(
			FString::Printf(TEXT("%.1f MB"), R.EstimatedVramSavedMb)));
	if (LodSavingsPct_Label.IsValid())
	{
		const double Pct = R.TotalVramMb > 0.0
			? (R.EstimatedVramSavedMb / R.TotalVramMb) * 100.0 : 0.0;
		LodSavingsPct_Label->SetText(FText::FromString(
			FString::Printf(TEXT("%.1f%% Reduction"), Pct)));
	}

	if (LodFrameTime_Label.IsValid())
	{
		// Heuristic estimate (no GPU telemetry is available client-side): the
		// resident texture VRAM we free relieves per-frame sampler bandwidth.
		// Approximate the relief as a fraction of a 60fps (16.67ms) budget,
		// weighted by how much of a typical frame is texture-bandwidth-bound
		// (~25%). Conservative and clearly prefixed "~" as an estimate — swap in
		// real profiling telemetry when we have it.
		const double FrameMs            = 1000.0 / 60.0;
		const double TexBandwidthWeight = 0.25;
		const double ReductionFrac      = R.TotalVramMb > 0.0
			? FMath::Clamp(R.EstimatedVramSavedMb / R.TotalVramMb, 0.0, 1.0) : 0.0;
		const double SavedMs = FrameMs * ReductionFrac * TexBandwidthWeight;
		LodFrameTime_Label->SetText(FText::FromString(
			SavedMs > 0.0 ? FString::Printf(TEXT("~%.2f ms"), SavedMs) : TEXT("—")));
	}

	if (LodIssues_Label.IsValid())
		LodIssues_Label->SetText(FText::FromString(FmtN(R.IssuesFound)));
	if (LodIssuesSub_Label.IsValid())
		LodIssuesSub_Label->SetText(FText::FromString(OtherIssues > 0
			? FString::Printf(TEXT("Tex: %d   Mesh: %d   Mat: %d   Other: %d"),
				TexIssues, MeshIssues, MatIssues, OtherIssues)
			: FString::Printf(TEXT("Tex: %d   Mesh: %d   Mat: %d"),
				TexIssues, MeshIssues, MatIssues)));
}

// ─────────────────────────────────────────────────────────────────────────────
// Stage 3 — auto-fix (optimised duplicate) + export
// ─────────────────────────────────────────────────────────────────────────────
// Writes an optimised *duplicate* (<Name>_Optimized) of the finding's texture
// with the server's recommended max-size / compression applied; the original is
// never modified. Returns false + OutError on failure.
bool SShintToolsPanel::ApplyLodFixDuplicate(
	const FShintLodFinding& F, FString& OutNewPath, FString& OutError)
{
	const TextureCompressionSettings RecTC =
		F.RecCompression.IsEmpty() ? TC_MAX : LodMapRecCompression(F.RecCompression);
	if (F.RecMaxSize <= 0 && RecTC == TC_MAX)
	{
		OutError = TEXT("This finding has no auto-applicable texture size/compression change.");
		return false;
	}

	IAssetRegistry& AR = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(
		TEXT("AssetRegistry")).Get();
	TArray<FAssetData> Found;
	AR.GetAssetsByPackageName(FName(*F.AssetPath), Found);
	UTexture2D* Src = nullptr;
	for (const FAssetData& AD : Found)
	{
		if ((Src = Cast<UTexture2D>(AD.GetAsset())) != nullptr) break;
	}
	if (!Src)
	{
		OutError = FString::Printf(TEXT("Could not load a Texture2D at '%s'."), *F.AssetPath);
		return false;
	}

	const FString PackagePath = FPackageName::GetLongPackagePath(F.AssetPath);
	const FString NewName     = FPaths::GetBaseFilename(F.AssetPath) + TEXT("_Optimized");
	FAssetToolsModule& ATM = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
	UObject* Dup = ATM.Get().DuplicateAsset(NewName, PackagePath, Src);
	UTexture2D* NewTex = Cast<UTexture2D>(Dup);
	if (!NewTex)
	{
		OutError = FString::Printf(TEXT("Could not create '%s/%s' (it may already exist)."),
			*PackagePath, *NewName);
		return false;
	}

	NewTex->Modify();
	if (F.RecMaxSize > 0) NewTex->MaxTextureSize      = F.RecMaxSize;
	if (RecTC != TC_MAX)  NewTex->CompressionSettings = RecTC;
	NewTex->PostEditChange();   // rebuild the platform data with the new settings

	UPackage* Pkg = NewTex->GetOutermost();
	Pkg->MarkPackageDirty();
	const FString FileName = FPackageName::LongPackageNameToFilename(
		Pkg->GetName(), FPackageName::GetAssetPackageExtension());
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	SaveArgs.SaveFlags     = SAVE_NoError;
	UPackage::SavePackage(Pkg, NewTex, *FileName, SaveArgs);

	OutNewPath = NewTex->GetPathName();
	return true;
}

FReply SShintToolsPanel::OnLodFixRow(FShintLodFindingPtr Item)
{
	if (!Item.IsValid()) return FReply::Handled();

	const FShintLodFinding& F = Item->Finding;

	// Meshes and materials (and any texture finding that isn't a size/
	// compression change) have no notion of a "duplicate" — the recommended
	// property lives on the mesh's build settings or the material itself, so
	// ApplyLodFixDuplicate always rejected them. Route those through the
	// in-place registry (Transaction + Journal, so it's still undoable/
	// revertible); keep the non-destructive duplicate path for the texture
	// size/compression case it was built for.
	if (FShintLodFixerRegistry::CanApply(F.AssetPath, F.Recommended))
		return OnLodFixInPlace(Item);

	if (F.RecMaxSize > 0 || !F.RecCompression.IsEmpty())
	{
		FString NewPath, Err;
		if (ApplyLodFixDuplicate(F, NewPath, Err))
			LodShowSuccessToast(TEXT("Optimized copy created"),
				FString::Printf(TEXT("Wrote %s — original untouched."), *NewPath));
		else
			ShintShowErrorToast(TEXT("Auto-fix failed"), Err);
		return FReply::Handled();
	}

	// Neither path applies: the recommendation is advisory (regenerate LODs,
	// reduce material complexity, …) — an honest message, not a false failure.
	// With the Fix button now gated on IsAutoApplicable this should be
	// unreachable from the button, but a stale/edge finding still lands here.
	ShintShowErrorToast(TEXT("Manual fix required"),
		TEXT("This recommendation can't be applied automatically in-editor — "
		     "it needs a structural change (e.g. regenerate LODs, reduce "
		     "material complexity). See the recommendation for details."));
	return FReply::Handled();
}

FReply SShintToolsPanel::OnLodFixSelected()
{
	int32 Ok = 0, Failed = 0, Skipped = 0, AlreadyOk = 0;
	FString LastErr;
	// Collected, not applied in-place: RemoveFixedLodFindings rebuilds
	// LodFindingItems, and this loop is iterating that same array.
	TArray<TPair<FString, FString>> FixedKeys;
	// ALL findings, not just the current tab's visible rows — a row checked
	// on Meshes and left checked while the user switched to Textures must
	// still be included, matching what the "Fix (N)" label (LodCheckedCount)
	// now promises.
	for (const FShintLodFindingPtr& It : LodFindingItems)
	{
		if (!It.IsValid() || !It->bChecked) continue;
		const FShintLodFinding& F = It->Finding;

		// A checked row whose recommendation isn't auto-applicable (advisory
		// mesh/material finding) is skipped, not counted as a failure — its
		// Fix button is hidden anyway; this just keeps a manual multi-select
		// honest instead of erroring on it.
		if (!FShintLodFixerRegistry::IsAutoApplicable(F.Recommended)
			&& F.RecMaxSize <= 0 && F.RecCompression.IsEmpty())
		{
			++Skipped;
			continue;
		}

		// Same dispatch as OnLodFixRow.
		if (FShintLodFixerRegistry::CanApply(F.AssetPath, F.Recommended))
		{
			const FShintLodFixResult R = FShintLodFixerRegistry::ApplyFromFinding(F);
			if (!R.Error.IsEmpty())  { ++Failed; LastErr = R.Error; }
			else if (R.bApplied)     { ++Ok; FixedKeys.Emplace(F.AssetPath, F.RuleId); }
			else                     ++AlreadyOk;   // idempotent no-op — already matched, not a fix
			continue;
		}

		// The duplicate path never touches the original asset — its finding
		// stays valid and stays in the list, so it's NOT added to FixedKeys.
		FString NewPath, Err;
		if (ApplyLodFixDuplicate(F, NewPath, Err)) ++Ok;
		else { ++Failed; LastErr = Err; }
	}

	RemoveFixedLodFindings(FixedKeys);   // one rebuild, after the loop is done reading LodFindingItems

	if (Ok == 0 && Failed == 0 && Skipped == 0 && AlreadyOk == 0)
		ShintShowErrorToast(TEXT("Nothing selected"),
			TEXT("Tick one or more rows, then press Fix."));
	else if (Failed == 0)
		LodShowSuccessToast(TEXT("Fix Selected complete"),
			FString::Printf(TEXT("%d finding(s) fixed%s%s."), Ok,
				AlreadyOk > 0
					? *FString::Printf(TEXT(", %d already matched"), AlreadyOk)
					: TEXT(""),
				Skipped > 0
					? *FString::Printf(TEXT(", %d skipped (manual)"), Skipped)
					: TEXT("")));
	else
		ShintShowErrorToast(
			FString::Printf(TEXT("Fixed %d, %d failed"), Ok, Failed), LastErr);
	RefreshLodFixesList();
	return FReply::Handled();
}

FReply SShintToolsPanel::OnSendLodToDashboardClicked()
{
	if (LastLodResult.Findings.Num() == 0)
	{
		ShintShowErrorToast(TEXT("Nothing to send"),
			TEXT("Run a LOD audit first, then Send to Dashboard."));
		return FReply::Handled();
	}
	if (SendLodBtnLabel.IsValid())
	{
		SendLodBtnLabel->SetText(LOCTEXT("AODashSending", "Sending…"));
		SendLodBtnLabel->SetColorAndOpacity(FSlateColor(C_Gray()));
	}
	DashboardSync->SendLodAudit(LastLodResult,
		FOnShintWebDashboardComplete::CreateSP(this, &SShintToolsPanel::OnLodDashboardComplete));
	return FReply::Handled();
}

void SShintToolsPanel::OnLodDashboardComplete(const FShintWebDashboardResult& Result)
{
	if (!SendLodBtnLabel.IsValid()) return;
	if (Result.bSuccess)
	{
		SendLodBtnLabel->SetText(LOCTEXT("AODashOk", "Sent!"));
		SendLodBtnLabel->SetColorAndOpacity(FSlateColor(FShintStyle::Colors::SevLow()));
		LodShowSuccessToast(TEXT("Sent to Dashboard"),
			FString::Printf(TEXT("%d finding(s) pushed."), LastLodResult.Findings.Num()));
	}
	else
	{
		FString Short = Result.ErrorMessage.IsEmpty()
			? FString(TEXT("Network or auth error")) : Result.ErrorMessage;
		if (Short.Len() > 60) Short = Short.Left(57) + TEXT("…");
		SendLodBtnLabel->SetText(LOCTEXT("AODash", "Send to Dashboard"));
		SendLodBtnLabel->SetColorAndOpacity(FSlateColor(C_Gray()));
		ShintShowErrorToast(TEXT("Dashboard send failed"), Short);
		UE_LOG(LogShintTools, Error,
			TEXT("LOD dashboard send failed: %s | response: %s"),
			*Result.ErrorMessage, *Result.ResponseBody);
	}
}

FReply SShintToolsPanel::OnLodExport()
{
	if (LodFindingItems.Num() == 0)
	{
		ShintShowErrorToast(TEXT("Nothing to export"),
			TEXT("Run a scan first, then Export."));
		return FReply::Handled();
	}

	auto Esc = [](const FString& In) -> FString {
		FString S = In; S.ReplaceInline(TEXT("\""), TEXT("\"\""));
		return (S.Contains(TEXT(",")) || S.Contains(TEXT("\"")))
			? FString::Printf(TEXT("\"%s\""), *S) : S;
	};

	FString Csv = TEXT("Asset,Rule,Category,Severity,Group,Width,Height,Format,"
		"Detail,CurrentVRAM_MB,PotentialVRAM_MB,Saving_MB,Saving_Instr,"
		"Recommendation\n");
	for (const FShintLodFindingPtr& It : LodFindingItems)
	{
		if (!It.IsValid()) continue;
		const FShintLodFinding& F = It->Finding;
		Csv += FString::Printf(TEXT("%s,%s,%s,%s,%s,%d,%d,%s,%s,%.2f,%.2f,%.2f,%d,%s\n"),
			*Esc(F.AssetPath), *Esc(F.RuleId), *Esc(F.Category), *Esc(F.Severity),
			*Esc(F.Group), F.Width, F.Height, *Esc(F.Format), *Esc(F.ResText),
			F.CurrentVramMb, F.PotentialVramMb, F.VramMb, F.ShaderInstructions,
			*Esc(F.Guidance));
	}

	const FString OutPath = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("ShintTools"),
		FString::Printf(TEXT("lod_audit_%s.csv"),
			*FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"))));
	if (FFileHelper::SaveStringToFile(Csv, *OutPath))
		LodShowSuccessToast(TEXT("Export complete"), OutPath);
	else
		ShintShowErrorToast(TEXT("Export failed"),
			FString::Printf(TEXT("Could not write %s"), *OutPath));
	return FReply::Handled();
}

// ─────────────────────────────────────────────────────────────────────────────
// §21 — top-level views (Summary / Assets / Fixes / Budgets)
// ─────────────────────────────────────────────────────────────────────────────

// Row model for the Fixes list view (defined here; the header only
// forward-declares it).
struct FShintLodJournalRow
{
	FString Id;
	FString AssetName;
	FString RuleId;
	FString Timestamp;
	int32   Props = 0;
};

namespace
{
	// Asset family accent for the treemap / badges.
	FLinearColor LodCategoryColor(const FString& Category)
	{
		if (Category.Contains(TEXT("Texture")))  return FLinearColor(0.16f, 0.52f, 0.55f);
		if (Category.Contains(TEXT("Mesh")))     return FLinearColor(0.82f, 0.53f, 0.22f);
		if (Category.Contains(TEXT("Material"))) return FLinearColor(0.46f, 0.41f, 0.74f);
		return FLinearColor(0.40f, 0.42f, 0.46f);
	}

	// "/Game/Foo/Bar.Bar" → "Bar".
	FString LodAssetName(const FString& Path)
	{
		FString Name = Path;
		int32 Slash;
		if (Name.FindLastChar(TEXT('/'), Slash)) Name = Name.RightChop(Slash + 1);
		int32 Dot;
		if (Name.FindChar(TEXT('.'), Dot))       Name = Name.Left(Dot);
		return Name;
	}

}

// ── View nav ─────────────────────────────────────────────────────────────────
TSharedRef<SWidget> SShintToolsPanel::BuildLodViewNav()
{
	auto NavBtn = [this](const FText& Label, ELodView View) -> TSharedRef<SWidget>
	{
		return SNew(SButton)
			.ContentPadding(FMargin(16.f, 8.f))
			.ButtonColorAndOpacity_Lambda([this, View]() {
				return FSlateColor(LodActiveView == View ? C_Surface() : C_BG());
			})
			.OnClicked_Lambda([this, View]() { SetLodView(View); return FReply::Handled(); })
			[
				SNew(STextBlock).Text(Label).Font(F_Small())
				.ColorAndOpacity_Lambda([this, View]() {
					return FSlateColor(LodActiveView == View ? C_White() : C_Gray());
				})
			];
	};

	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 6.f, 0.f)
		[ NavBtn(LOCTEXT("LodNavSummary", "Summary"), ELodView::Summary) ]
		+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 6.f, 0.f)
		[ NavBtn(LOCTEXT("LodNavAssets",  "Assets"),  ELodView::Assets) ]
		+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 6.f, 0.f)
		[ NavBtn(LOCTEXT("LodNavFixes",   "Fixes"),   ELodView::Fixes) ]
		+ SHorizontalBox::Slot().AutoWidth()
		[ NavBtn(LOCTEXT("LodNavBudgets", "Budgets"), ELodView::Budgets) ];
}

void SShintToolsPanel::SetLodView(ELodView View)
{
	LodActiveView = View;
	if (LodViewSwitcher.IsValid())
		LodViewSwitcher->SetActiveWidgetIndex(static_cast<int32>(View));
	// The Fixes view mirrors an on-disk journal that other flows (per-row fix,
	// commandlet) also append to — reload it whenever the user opens the view.
	if (View == ELodView::Fixes)
		RefreshLodFixesList();
}

// ── Summary view — KPI tiles + VRAM treemap ─────────────────────────────────
TSharedRef<SWidget> SShintToolsPanel::BuildLodSummaryView()
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, FShintStyle::Space::S3)
		[ BuildLodKpiRow() ]
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SBorder)
			.BorderImage(ST4::Outline(FShintStyle::Colors::BgCard(),
				FShintStyle::Colors::BorderSubtle(), FShintStyle::Radius::Card))
			.Padding(FMargin(14.f))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("LodTreemapTitle", "VRAM BY ASSET"))
					.Font(F_Label())
					.ColorAndOpacity(FSlateColor(FShintStyle::Colors::TextMuted()))
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(SBox).HeightOverride(260.f)
					[ SAssignNew(LodTreemap, SShintTreemap).MinDesiredHeight(260.f) ]
				]
			]
		];
}

void SShintToolsPanel::RefreshLodTreemap()
{
	if (!LodTreemap.IsValid()) return;

	// One cell per asset, weighted by its heaviest resident-VRAM finding.
	TMap<FString, FShintTreemapItem> ByAsset;
	for (const FShintLodFinding& F : LastLodResult.Findings)
	{
		if (F.CurrentVramMb <= 0.0) continue;
		FShintTreemapItem& It = ByAsset.FindOrAdd(F.AssetPath);
		if (F.CurrentVramMb > It.Value)
		{
			It.Label  = LodAssetName(F.AssetPath);
			It.Detail = FString::Printf(TEXT("%.1f MB"), F.CurrentVramMb);
			It.Value  = F.CurrentVramMb;
			It.Color  = LodCategoryColor(F.Category);
		}
	}
	TArray<FShintTreemapItem> Items;
	ByAsset.GenerateValueArray(Items);
	LodTreemap->SetItems(Items);
}

// ── Fixes view — in-place auto-fix engine (§20.5): batch apply + journal + revert
TSharedRef<SWidget> SShintToolsPanel::BuildLodFixesView()
{
	return SNew(SVerticalBox)
		// Action bar: Fix All (in place) + confidence floor selector.
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 8.f, 0.f)
			[
				SNew(SButton).ContentPadding(FMargin(14.f, 8.f))
				.OnClicked(this, &SShintToolsPanel::OnLodFixAllInPlace)
				.ToolTipText(LOCTEXT("LodFixAllTip",
					"Apply every auto-fixable finding at or above the selected "
					"confidence directly to the source assets. Each change is a "
					"single Undo step and is journalled for Revert."))
				[
					SNew(STextBlock).Text(LOCTEXT("LodFixAll", "Fix All (in place)"))
					.Font(F_Small()).ColorAndOpacity(FSlateColor(C_White()))
				]
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SComboButton).ContentPadding(FMargin(12.f, 7.f))
				.ButtonColorAndOpacity(FSlateColor(C_Surface()))
				.OnGetMenuContent_Lambda([this]() -> TSharedRef<SWidget>
				{
					const TArray<TPair<FText, FString>> Levels = {
						{ LOCTEXT("LodConfHigh",   "High confidence only"), TEXT("high") },
						{ LOCTEXT("LodConfMedium", "Medium and up"),        TEXT("medium") },
						{ LOCTEXT("LodConfLow",    "All (incl. low)"),      TEXT("low") },
					};
					TSharedRef<SVerticalBox> Menu = SNew(SVerticalBox);
					for (const TPair<FText, FString>& L : Levels)
					{
						Menu->AddSlot().AutoHeight()
						[
							SNew(SButton).ButtonColorAndOpacity(FSlateColor(C_Surface()))
							.ContentPadding(FMargin(12.f, 6.f))
							.OnClicked_Lambda([this, Conf = L.Value]() {
								LodFixConfidence = Conf; return FReply::Handled();
							})
							[
								SNew(STextBlock).Text(L.Key).Font(F_Label())
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
						return FText::FromString(FString::Printf(
							TEXT("Confidence: %s"), *LodFixConfidence));
					})
					.Font(FShintStyle::Fonts::Caption())
					.ColorAndOpacity(FSlateColor(FShintStyle::Colors::TextPrimary()))
				]
			]
		]
		// Journal column header
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)
		[
			SNew(SBorder).BorderImage(ST4::Solid(C_BG())).Padding(FMargin(10.f, 6.f))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(2.6f)
				[ SNew(STextBlock).Text(LOCTEXT("LodFixHdrAsset", "ASSET")).Font(F_Label()).ColorAndOpacity(FSlateColor(C_Gray())) ]
				+ SHorizontalBox::Slot().FillWidth(1.0f)
				[ SNew(STextBlock).Text(LOCTEXT("LodFixHdrRule", "RULE")).Font(F_Label()).ColorAndOpacity(FSlateColor(C_Gray())) ]
				+ SHorizontalBox::Slot().FillWidth(0.8f).HAlign(HAlign_Right)
				[ SNew(STextBlock).Text(LOCTEXT("LodFixHdrProps", "PROPS")).Font(F_Label()).ColorAndOpacity(FSlateColor(C_Gray())) ]
				+ SHorizontalBox::Slot().FillWidth(1.8f)
				[ SNew(STextBlock).Text(LOCTEXT("LodFixHdrWhen", "WHEN")).Font(F_Label()).ColorAndOpacity(FSlateColor(C_Gray())) ]
				+ SHorizontalBox::Slot().AutoWidth().Padding(8.f, 0.f, 0.f, 0.f)
				[ SNew(SBox).WidthOverride(72.f) ]
			]
		]
		// Empty hint
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SBox).HAlign(HAlign_Center).VAlign(VAlign_Center).MinDesiredHeight(48.f)
			.Visibility_Lambda([this]() {
				return LodJournalRows.Num() == 0 ? EVisibility::Visible : EVisibility::Collapsed;
			})
			[
				SNew(STextBlock)
				.Text(LOCTEXT("LodFixEmpty", "No fixes applied yet. Fix All, or use Fix on a row, to populate the journal."))
				.Font(F_Small()).ColorAndOpacity(FSlateColor(C_DimGray()))
			]
		]
		// Journal list
		+ SVerticalBox::Slot().AutoHeight().MaxHeight(520.f)
		[
			SAssignNew(LodFixesListView, SListView<TSharedPtr<FShintLodJournalRow>>)
			.ListItemsSource(&LodJournalRows)
			.SelectionMode(ESelectionMode::None)
			.OnGenerateRow(this, &SShintToolsPanel::GenerateLodJournalRow)
		];
}

TSharedRef<ITableRow> SShintToolsPanel::GenerateLodJournalRow(
	TSharedPtr<FShintLodJournalRow> Item, const TSharedRef<STableViewBase>& Owner)
{
	return SNew(STableRow<TSharedPtr<FShintLodJournalRow>>, Owner)
		.Padding(FMargin(0.f, 2.f))
		[
			SNew(SBorder).BorderImage(ST4::Solid(C_Surface())).Padding(FMargin(10.f, 6.f))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(2.6f).VAlign(VAlign_Center)
				[ SNew(STextBlock).Text(FText::FromString(Item->AssetName))
					.Font(F_Small()).ColorAndOpacity(FSlateColor(C_White()))
					.OverflowPolicy(ETextOverflowPolicy::Ellipsis) ]
				+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
				[ SNew(STextBlock).Text(FText::FromString(Item->RuleId))
					.Font(FShintStyle::Fonts::Caption()).ColorAndOpacity(FSlateColor(C_Gray())) ]
				+ SHorizontalBox::Slot().FillWidth(0.8f).HAlign(HAlign_Right).VAlign(VAlign_Center)
				[ SNew(STextBlock).Text(FText::AsNumber(Item->Props))
					.Font(F_Small()).ColorAndOpacity(FSlateColor(C_White())) ]
				+ SHorizontalBox::Slot().FillWidth(1.8f).VAlign(VAlign_Center)
				[ SNew(STextBlock).Text(FText::FromString(Item->Timestamp))
					.Font(F_Label()).ColorAndOpacity(FSlateColor(C_DimGray())) ]
				+ SHorizontalBox::Slot().AutoWidth().Padding(8.f, 0.f, 0.f, 0.f).VAlign(VAlign_Center)
				[
					SNew(SBox).WidthOverride(72.f)
					[
						SNew(SButton).ContentPadding(FMargin(10.f, 5.f)).HAlign(HAlign_Center)
						.OnClicked(this, &SShintToolsPanel::OnLodRevertFix, Item)
						[
							SNew(STextBlock).Text(LOCTEXT("LodRevert", "Revert"))
							.Font(F_Label()).ColorAndOpacity(FSlateColor(C_White()))
						]
					]
				]
			]
		];
}

void SShintToolsPanel::RefreshLodFixesList()
{
	LodJournalRows.Reset();
	const TArray<FShintLodJournalEntry> Entries = FShintLodFixJournal::LoadAll();
	// Newest first (LoadAll is chronological).
	for (int32 i = Entries.Num() - 1; i >= 0; --i)
	{
		const FShintLodJournalEntry& E = Entries[i];
		TSharedPtr<FShintLodJournalRow> Row = MakeShared<FShintLodJournalRow>();
		Row->Id        = E.Id;
		Row->AssetName = LodAssetName(E.AssetPath);
		Row->RuleId    = E.RuleId;
		Row->Timestamp = E.Timestamp;
		Row->Props     = E.After.Num();
		LodJournalRows.Add(Row);
	}
	if (LodFixesListView.IsValid()) LodFixesListView->RequestListRefresh();
}

FReply SShintToolsPanel::OnLodFixInPlace(FShintLodFindingPtr Item)
{
	if (!Item.IsValid()) return FReply::Handled();
	const FShintLodFixResult R = FShintLodFixerRegistry::ApplyFromFinding(Item->Finding);
	if (!R.Error.IsEmpty())
		ShintShowErrorToast(TEXT("Fix failed"), R.Error);
	else if (R.bApplied)
	{
		LodShowSuccessToast(TEXT("Fix applied"),
			FString::Printf(TEXT("%s — %d property(ies) changed%s."),
				*LodAssetName(Item->Finding.AssetPath), R.PropertiesChanged,
				R.bRebuilt ? TEXT(", rebuilt") : TEXT("")));
		RemoveFixedLodFinding(Item->Finding.AssetPath, Item->Finding.RuleId);
	}
	else
		// Applicable key, but the asset already matches the recommended value
		// (stale finding) — give feedback instead of a silent click.
		LodShowSuccessToast(TEXT("Already optimal"),
			FString::Printf(TEXT("%s already matches the recommended settings."),
				*LodAssetName(Item->Finding.AssetPath)));
	RefreshLodFixesList();
	return FReply::Handled();
}

FReply SShintToolsPanel::OnLodFixAllInPlace()
{
	auto ConfRank = [](const FString& C) -> int32 {
		if (C.Equals(TEXT("low"),    ESearchCase::IgnoreCase)) return 1;
		if (C.Equals(TEXT("medium"), ESearchCase::IgnoreCase)) return 2;
		return 3;   // high / unspecified
	};
	const int32 FloorRank = ConfRank(LodFixConfidence);

	int32 Applied = 0, Skipped = 0, Failed = 0, Props = 0;
	// Collected, not applied in-place: RemoveFixedLodFindings rebuilds
	// LodFindingItems, and this loop is iterating that same array.
	TArray<TPair<FString, FString>> FixedKeys;
	for (const FShintLodFindingPtr& Item : LodFindingItems)
	{
		if (!Item.IsValid()) continue;
		const FShintLodFinding& F = Item->Finding;
		if (!F.bAutoFixable || F.Recommended.Num() == 0) { ++Skipped; continue; }
		if (ConfRank(F.Confidence) < FloorRank)          { ++Skipped; continue; }
		// Cheap asset-free pre-filter first: skips advisory findings without
		// loading their asset (CanApply below does load it to confirm class).
		if (!FShintLodFixerRegistry::IsAutoApplicable(F.Recommended))      { ++Skipped; continue; }
		if (!FShintLodFixerRegistry::CanApply(F.AssetPath, F.Recommended)) { ++Skipped; continue; }

		const FShintLodFixResult R = FShintLodFixerRegistry::ApplyFromFinding(F);
		if (!R.Error.IsEmpty())      ++Failed;
		else if (R.bApplied)         { ++Applied; Props += R.PropertiesChanged; FixedKeys.Emplace(F.AssetPath, F.RuleId); }
		else                         ++Skipped;
	}

	RemoveFixedLodFindings(FixedKeys);   // one rebuild, after the loop is done reading LodFindingItems
	RefreshLodFixesList();
	if (Failed > 0)
		ShintShowErrorToast(TEXT("Fix All finished with errors"),
			FString::Printf(TEXT("%d applied, %d failed, %d skipped."),
				Applied, Failed, Skipped));
	else
		LodShowSuccessToast(TEXT("Fix All complete"),
			FString::Printf(TEXT("%d asset(s) fixed (%d properties), %d skipped."),
				Applied, Props, Skipped));
	return FReply::Handled();
}

FReply SShintToolsPanel::OnLodRevertFix(TSharedPtr<FShintLodJournalRow> Row)
{
	if (!Row.IsValid()) return FReply::Handled();
	const FShintLodFixResult R = FShintLodFixerRegistry::RevertFix(Row->Id);
	if (!R.Error.IsEmpty())
		ShintShowErrorToast(TEXT("Revert failed"), R.Error);
	else
		LodShowSuccessToast(TEXT("Reverted"),
			FString::Printf(TEXT("%s restored (%d property(ies))."),
				*Row->AssetName, R.PropertiesChanged));
	RefreshLodFixesList();
	return FReply::Handled();
}

// ── Budgets view — per-platform memory budget vs current usage ───────────────
TSharedRef<SWidget> SShintToolsPanel::BuildLodBudgetsView()
{
	// Profile-based texture-VRAM budget (MB) — must track the Core's own
	// LT015_POOL_BUDGET_MB (modules/lod_auditor/config/thresholds_*.yaml:
	// 2000 default, 500 mobile). This bar used to show 4096/1024, a
	// different number from what LT015 actually audited against — a studio
	// could see "plenty of headroom" here while the findings above already
	// flagged the pool as over budget. The bar reads live from LastLodResult
	// so it updates without a rebuild.
	auto BudgetMb = [this]() -> double {
		return LodProfile == TEXT("mobile") ? 500.0 : 2000.0;
	};

	auto UsageBar = [this, BudgetMb](const FText& Label,
		TFunction<double()> CurrentFn) -> TSharedRef<SWidget>
	{
		return SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
				[ SNew(STextBlock).Text(Label).Font(F_Small())
					.ColorAndOpacity(FSlateColor(C_White())) ]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text_Lambda([BudgetMb, CurrentFn]() {
						return FText::FromString(FString::Printf(TEXT("%.0f / %.0f MB"),
							CurrentFn(), BudgetMb()));
					})
					.Font(FShintStyle::Fonts::Caption())
					.ColorAndOpacity_Lambda([BudgetMb, CurrentFn]() {
						const double B = BudgetMb();
						const bool bOver = B > 0.0 && CurrentFn() > B;
						return FSlateColor(bOver ? FShintStyle::Colors::SevCritical()
												 : FShintStyle::Colors::TextMuted());
					})
				]
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SBox).HeightOverride(10.f)
				[
					SNew(SProgressBar)
					.Percent_Lambda([BudgetMb, CurrentFn]() -> TOptional<float> {
						const double B = BudgetMb();
						return B > 0.0 ? (float)FMath::Clamp(CurrentFn() / B, 0.0, 1.0) : 0.f;
					})
					.FillColorAndOpacity_Lambda([BudgetMb, CurrentFn]() {
						const double B = BudgetMb();
						const double Frac = B > 0.0 ? CurrentFn() / B : 0.0;
						if (Frac >= 1.0)  return FSlateColor(FShintStyle::Colors::SevCritical());
						if (Frac >= 0.85) return FSlateColor(FLinearColor(0.95f, 0.65f, 0.20f));
						return FSlateColor(FShintStyle::Colors::SevLow());
					})
				]
			];
	};

	return SNew(SBorder)
		.BorderImage(ST4::Outline(FShintStyle::Colors::BgCard(),
			FShintStyle::Colors::BorderSubtle(), FShintStyle::Radius::Card))
		.Padding(FMargin(16.f, 14.f))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 12.f)
			[
				SNew(STextBlock)
				.Text_Lambda([this]() {
					return FText::FromString(FString::Printf(
						TEXT("MEMORY BUDGET — %s"),
						LodProfile == TEXT("mobile") ? TEXT("MOBILE") : TEXT("DESKTOP")));
				})
				.Font(F_Label()).ColorAndOpacity(FSlateColor(FShintStyle::Colors::TextMuted()))
			]
			// Current resident texture VRAM vs budget.
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 14.f)
			[ UsageBar(LOCTEXT("LodBudgetCurrent", "Texture VRAM (current)"),
				[this]() { return LastLodResult.TotalVramMb; }) ]
			// Projected VRAM after applying all recommended savings.
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 14.f)
			[ UsageBar(LOCTEXT("LodBudgetProjected", "Texture VRAM (after fixes)"),
				[this]() {
					return FMath::Max(0.0,
						LastLodResult.TotalVramMb - LastLodResult.EstimatedVramSavedMb);
				}) ]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(STextBlock)
				.Text_Lambda([this]() {
					return FText::FromString(FString::Printf(
						TEXT("Estimated saving available: %.1f MB across %d issue(s). "
						     "Switch platform in the Scan bar to compare budgets."),
						LastLodResult.EstimatedVramSavedMb, LastLodResult.IssuesFound));
				})
				.Font(F_Label())
				.ColorAndOpacity(FSlateColor(C_DimGray()))
				.AutoWrapText(true)
			]
		];
}

#undef LOCTEXT_NAMESPACE
