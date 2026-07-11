// Copyright 2026 ShintTools. All Rights Reserved.
//
// Asset Optimizer destination (Studio tier) — the LOD Auditor's client UI.
// Replicates the Asset Optimizer mockup: a 5-tile KPI row, a Scan bar with a
// target-platform selector, Textures/Meshes/Materials tabs, a filter row
// (search + group/format/severity + Export + bulk Fix), and a per-finding data
// table (thumbnail · group · resolution · format · current/potential size ·
// savings · severity · recommendation · per-row Fix).
//
// Data: findings come from /assets/lod/audit; resolution/group/format and
// current/potential size are joined onto each finding client-side (see
// ShintCoreClient_Lod.cpp). Per-row + bulk Fix write an optimised *duplicate*
// (<Name>_Optimized) with the server's recommended max-size/compression applied,
// leaving the original untouched; Export writes a CSV of all findings.

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
#include "Styling/AppStyle.h"
#include "Misc/Paths.h"          // FPaths::GetCleanFilename (asset display name)

#define LOCTEXT_NAMESPACE "SShintToolsPanel"

namespace
{
	// Shared column proportions — the table header and every data row use these
	// identical FillWidth values so the columns line up. (The checkbox and Fix
	// columns are fixed AutoWidth and handled separately.)
	namespace LodCol
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
	TextureCompressionSettings LodMapRecCompression(const FString& In)
	{
		const FString S = In.TrimStartAndEnd().ToUpper();
		if (S == TEXT("BC7"))                                   return TC_BC7;
		if (S == TEXT("BC5")  || S == TEXT("NORMALMAP"))        return TC_Normalmap;
		if (S == TEXT("BC4")  || S == TEXT("GRAYSCALE"))        return TC_Grayscale;
		if (S == TEXT("BC6H") || S == TEXT("BC6") || S == TEXT("HDR")) return TC_HDR;
		if (S == TEXT("BC1")  || S == TEXT("BC3") || S == TEXT("DXT1") ||
		    S == TEXT("DXT5") || S == TEXT("DEFAULT"))          return TC_Default;
		return TC_MAX;
	}

	// True when the finding carries a change the duplicate-fix flow can actually
	// apply (texture max-size / compression). Server-side auto_fixable findings
	// without an applicable client action (e.g. mesh rules) are excluded so
	// "Fix All" never spams per-row errors.
	bool IsLodFixApplicable(const FShintLodFinding& F)
	{
		return F.bAutoFixable &&
			(F.RecMaxSize > 0 || LodMapRecCompression(F.RecCompression) != TC_MAX);
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

			// KPI tile row
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, FShintStyle::Space::S3)
			[ BuildLodKpiRow() ]

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

			// Tabs + filters + bulk actions
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 10.f)
			[ BuildLodToolbar() ]

			// Table header + results
			+ SVerticalBox::Slot().AutoHeight()
			[ BuildLodResultsPanel() ]
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
			+ SHorizontalBox::Slot().AutoWidth()
			[ TabBtn(LOCTEXT("AOTabMat", "Materials"),  ELodTab::Materials) ]
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
			// Bulk Fix (N) — applies the *checked* rows.
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 6.f, 0.f)
			[
				SNew(SButton).ContentPadding(FMargin(12.f, 6.f))
				.OnClicked(this, &SShintToolsPanel::OnLodFixSelected)
				[
					SAssignNew(LodFixSelected_Label, STextBlock)
					.Text(LOCTEXT("AOFixN", "Fix")).Font(F_Small())
					.ColorAndOpacity(FSlateColor(C_White()))
				]
			]
			// Fix All (N) — applies every applicable fix in the current tab,
			// no row selection needed.
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SButton).ContentPadding(FMargin(12.f, 6.f))
				.OnClicked(this, &SShintToolsPanel::OnLodFixAll)
				[
					SAssignNew(LodFixAll_Label, STextBlock)
					.Text(LOCTEXT("AOFixAll", "Fix All")).Font(F_Small())
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
	#define LOD_HDR_CELL(TextKey, TextVal, Fill)                              \
		+ SHorizontalBox::Slot().FillWidth(Fill).VAlign(VAlign_Center)        \
		[                                                                     \
			SNew(STextBlock).Text(LOCTEXT(TextKey, TextVal))                  \
			.Font(FShintStyle::Fonts::Caption())                             \
			.ColorAndOpacity(FSlateColor(FShintStyle::Colors::TextMuted()))   \
		]

	return SNew(SBorder)
		.BorderImage(ST4::Solid(C_BG()))
		.Padding(FMargin(8.f, 6.f))
		[
			SNew(SHorizontalBox)
			// checkbox spacer
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 8.f, 0.f)
			[ SNew(SBox).WidthOverride(20.f) ]
			LOD_HDR_CELL("AOColAsset", "ASSET",          LodCol::Asset)
			LOD_HDR_CELL("AOColGroup", "GROUP",          LodCol::Group)
			LOD_HDR_CELL("AOColRes",   "RESOLUTION",     LodCol::Res)
			LOD_HDR_CELL("AOColFmt",   "FORMAT",         LodCol::Fmt)
			LOD_HDR_CELL("AOColCur",   "CURRENT SIZE",   LodCol::Cur)
			LOD_HDR_CELL("AOColPot",   "POTENTIAL SIZE", LodCol::Pot)
			LOD_HDR_CELL("AOColSav",   "SAVINGS",        LodCol::Sav)
			LOD_HDR_CELL("AOColSev",   "SEVERITY",       LodCol::Sev)
			LOD_HDR_CELL("AOColRec",   "RECOMMENDATION", LodCol::Rec)
			// Fix spacer
			+ SHorizontalBox::Slot().AutoWidth().Padding(8.f, 0.f, 0.f, 0.f)
			[ SNew(SBox).WidthOverride(48.f) ]
		];

	#undef LOD_HDR_CELL
}

TSharedRef<SWidget> SShintToolsPanel::BuildLodResultsPanel()
{
	SAssignNew(LodEmptyState, SBox)
	.HAlign(HAlign_Center).VAlign(VAlign_Center).MinDesiredHeight(64.f)
	[
		SNew(STextBlock)
		.Text(LOCTEXT("AOEmpty", "Run a scan to see optimization findings."))
		.Font(F_Small()).ColorAndOpacity(FSlateColor(C_DimGray()))
	];

	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[ BuildLodTableHeader() ]

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

	return SNew(STableRow<FShintLodFindingPtr>, Owner)
		.Padding(FMargin(0.f, 1.f))
		[
			SNew(SBorder)
			.BorderImage(ST4::Solid(C_BG()))
			.Padding(FMargin(8.f, 8.f))
			[
				SNew(SHorizontalBox)

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
				]

				// ASSET: thumbnail placeholder + name + path
				+ SHorizontalBox::Slot().FillWidth(LodCol::Asset).VAlign(VAlign_Center)
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
				]

				// GROUP
				+ SHorizontalBox::Slot().FillWidth(LodCol::Group).VAlign(VAlign_Center)
				[
					SNew(STextBlock).Text(FText::FromString(F.Group))
					.Font(F_Small()).ColorAndOpacity(FSlateColor(C_Gray()))
				]
				// RESOLUTION
				+ SHorizontalBox::Slot().FillWidth(LodCol::Res).VAlign(VAlign_Center)
				[
					SNew(STextBlock).Text(FText::FromString(Resolution))
					.Font(F_Mono()).ColorAndOpacity(FSlateColor(C_Gray()))
				]
				// FORMAT
				+ SHorizontalBox::Slot().FillWidth(LodCol::Fmt).VAlign(VAlign_Center)
				[
					SNew(STextBlock).Text(FText::FromString(F.Format))
					.Font(F_Mono()).ColorAndOpacity(FSlateColor(C_Gray()))
				]
				// CURRENT SIZE
				+ SHorizontalBox::Slot().FillWidth(LodCol::Cur).VAlign(VAlign_Center)
				[
					SNew(STextBlock).Text(FText::FromString(FmtSizeMb(F.CurrentVramMb)))
					.Font(F_Small()).ColorAndOpacity(FSlateColor(FShintStyle::Colors::SevCritical()))
				]
				// POTENTIAL SIZE
				+ SHorizontalBox::Slot().FillWidth(LodCol::Pot).VAlign(VAlign_Center)
				[
					SNew(STextBlock).Text(FText::FromString(FmtSizeMb(F.PotentialVramMb)))
					.Font(F_Small()).ColorAndOpacity(FSlateColor(FShintStyle::Colors::SevLow()))
				]
				// SAVINGS (MB + %)
				+ SHorizontalBox::Slot().FillWidth(LodCol::Sav).VAlign(VAlign_Center)
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
				]
				// SEVERITY
				+ SHorizontalBox::Slot().FillWidth(LodCol::Sev).VAlign(VAlign_Center)
				[
					SNew(STextBlock).Text(FText::FromString(SeverityLabel(F.Severity)))
					.Font(F_Small()).ColorAndOpacity(FSlateColor(SeverityColor(F.Severity)))
				]
				// RECOMMENDATION
				+ SHorizontalBox::Slot().FillWidth(LodCol::Rec).VAlign(VAlign_Center)
				[
					SNew(STextBlock).Text(FText::FromString(F.Message))
					.Font(F_Label()).ColorAndOpacity(FSlateColor(C_Gray())).AutoWrapText(true)
				]
				// Fix
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8.f, 0.f, 0.f, 0.f)
				[
					SNew(SBox).WidthOverride(48.f)
					[
						SNew(SButton)
						.Visibility(F.bAutoFixable ? EVisibility::Visible : EVisibility::Collapsed)
						.ContentPadding(FMargin(8.f, 4.f))
						.ButtonColorAndOpacity(FSlateColor(C_Surface()))
						.OnClicked_Lambda([this, Item]() { return OnLodFixRow(Item); })
						[
							SNew(STextBlock).Text(LOCTEXT("AOFix", "Fix")).Font(F_Label())
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
	if (AuditLodBtnLabel.IsValid())
		AuditLodBtnLabel->SetText(LOCTEXT("AOScanning", "Scanning…"));

	LodFindingItems.Empty();
	LodFilteredItems.Empty();
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
	RefreshLodFilteredList();
}

void SShintToolsPanel::SetLodTab(ELodTab Tab)
{
	LodActiveTab = Tab;
	RefreshLodFilteredList();
}

int32 SShintToolsPanel::LodCheckedCount() const
{
	int32 N = 0;
	for (const FShintLodFindingPtr& It : LodFilteredItems)
		if (It.IsValid() && It->bChecked) ++N;
	return N;
}

void SShintToolsPanel::RefreshLodFilteredList()
{
	const FString TabCat =
		LodActiveTab == ELodTab::Textures  ? TEXT("Texture")  :
		LodActiveTab == ELodTab::Meshes    ? TEXT("Mesh")     : TEXT("Material");
	const FString Search = LodSearchText.TrimStartAndEnd();

	LodFilteredItems.Empty(LodFindingItems.Num());
	for (const FShintLodFindingPtr& It : LodFindingItems)
	{
		const FShintLodFinding& F = It->Finding;

		if (!F.Category.Contains(TabCat)) continue;
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
	if (LodFixSelected_Label.IsValid())
		LodFixSelected_Label->SetText(LOCTEXT("AOFixN", "Fix"));
	if (LodFixAll_Label.IsValid())
	{
		int32 Applicable = 0;
		for (const FShintLodFindingPtr& It : LodFilteredItems)
			if (It.IsValid() && IsLodFixApplicable(It->Finding)) ++Applicable;
		LodFixAll_Label->SetText(FText::FromString(
			FString::Printf(TEXT("Fix All (%d)"), Applicable)));
	}
}

void SShintToolsPanel::RefreshLodStats()
{
	const FShintLodAuditResult& R = LastLodResult;

	// Per-category issue counts (from findings) for the breakdown subtitles.
	int32 TexIssues = 0, MeshIssues = 0, MatIssues = 0;
	for (const FShintLodFinding& F : R.Findings)
	{
		if (F.Category.Contains(TEXT("Texture")))        ++TexIssues;
		else if (F.Category.Contains(TEXT("Mesh")))      ++MeshIssues;
		else if (F.Category.Contains(TEXT("Material")))  ++MatIssues;
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
		LodIssuesSub_Label->SetText(FText::FromString(FString::Printf(
			TEXT("Tex: %d   Mesh: %d   Mat: %d"), TexIssues, MeshIssues, MatIssues)));
}

// ─────────────────────────────────────────────────────────────────────────────
// Stage 3 — auto-fix (optimised duplicate) + export
// ─────────────────────────────────────────────────────────────────────────────
namespace
{
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
	FString NewPath, Err;
	if (ApplyLodFixDuplicate(Item->Finding, NewPath, Err))
		LodShowSuccessToast(TEXT("Optimized copy created"),
			FString::Printf(TEXT("Wrote %s — original untouched."), *NewPath));
	else
		ShintShowErrorToast(TEXT("Auto-fix failed"), Err);
	return FReply::Handled();
}

FReply SShintToolsPanel::OnLodFixSelected()
{
	int32 Ok = 0, Failed = 0;
	FString LastErr;
	for (const FShintLodFindingPtr& It : LodFilteredItems)
	{
		if (!It.IsValid() || !It->bChecked) continue;
		FString NewPath, Err;
		if (ApplyLodFixDuplicate(It->Finding, NewPath, Err)) ++Ok;
		else { ++Failed; LastErr = Err; }
	}
	if (Ok == 0 && Failed == 0)
		ShintShowErrorToast(TEXT("Nothing selected"),
			TEXT("Tick one or more rows, then press Fix."));
	else if (Failed == 0)
		LodShowSuccessToast(TEXT("Optimized copies created"),
			FString::Printf(TEXT("%d optimised %s written; originals untouched."),
				Ok, Ok == 1 ? TEXT("copy") : TEXT("copies")));
	else
		ShintShowErrorToast(
			FString::Printf(TEXT("Fixed %d, %d failed"), Ok, Failed), LastErr);
	return FReply::Handled();
}

FReply SShintToolsPanel::OnLodFixAll()
{
	int32 Ok = 0, Failed = 0;
	FString LastErr;
	for (const FShintLodFindingPtr& It : LodFilteredItems)
	{
		if (!It.IsValid() || !IsLodFixApplicable(It->Finding)) continue;
		FString NewPath, Err;
		if (ApplyLodFixDuplicate(It->Finding, NewPath, Err)) ++Ok;
		else { ++Failed; LastErr = Err; }
	}
	if (Ok == 0 && Failed == 0)
		ShintShowErrorToast(TEXT("Nothing to fix"),
			TEXT("No finding in this tab has an auto-applicable fix."));
	else if (Failed == 0)
		LodShowSuccessToast(TEXT("Optimized copies created"),
			FString::Printf(TEXT("%d optimised %s written; originals untouched."),
				Ok, Ok == 1 ? TEXT("copy") : TEXT("copies")));
	else
		ShintShowErrorToast(
			FString::Printf(TEXT("Fixed %d, %d failed"), Ok, Failed), LastErr);
	return FReply::Handled();
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
		"CurrentVRAM_MB,PotentialVRAM_MB,Saving_MB,Recommendation\n");
	for (const FShintLodFindingPtr& It : LodFindingItems)
	{
		if (!It.IsValid()) continue;
		const FShintLodFinding& F = It->Finding;
		Csv += FString::Printf(TEXT("%s,%s,%s,%s,%s,%d,%d,%s,%.2f,%.2f,%.2f,%s\n"),
			*Esc(F.AssetPath), *Esc(F.RuleId), *Esc(F.Category), *Esc(F.Severity),
			*Esc(F.Group), F.Width, F.Height, *Esc(F.Format),
			F.CurrentVramMb, F.PotentialVramMb, F.VramMb, *Esc(F.Guidance));
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

#undef LOCTEXT_NAMESPACE
