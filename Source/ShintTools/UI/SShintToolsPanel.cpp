// Copyright ShintTools. All Rights Reserved.

#include "SShintToolsPanel.h"
#include "ShintTools/ShintTools.h"
#include "ShintCoreClient.h"
#include "CoreProcessManager.h"

// Shared design-system widgets (UI redesign foundation)
#include "ShintStyle.h"
#include "SShintCard.h"
#include "SShintSeverityBadge.h"
#include "SShintKpiTile.h"
#include "SShintEmptyState.h"
#include "SShintSidebar.h"
#include "SShintTopBar.h"
#include "Widgets/Layout/SWidgetSwitcher.h"

// Slate windows / dialogs
#include "Widgets/SWindow.h"
#include "Framework/Application/SlateApplication.h"

// Slate layout
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SWrapBox.h"

// Slate widgets
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Views/SListView.h"
// Style
#include "Styling/AppStyle.h"
// Asset tools (for IAssetTools::RenameAssets + FixupReferencers)
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "UObject/ObjectRedirector.h"
#include "Engine/Blueprint.h"          // T2 — detect BP for ClassRedirects entries
#include "Kismet2/KismetEditorUtilities.h" // ASSET-FIX-2 — recompile descendants after parent rename
#include "Kismet2/BlueprintEditorUtils.h"  // ASSET-FIX-2 — FBlueprintTags::ParentClassPath
#include "FileHelpers.h"               // ASSET-FIX-2 — auto-save renamed packages
#include "Misc/ConfigCacheIni.h"       // T2 — write CoreRedirects to DefaultEngine.ini
#include "Misc/FileHelper.h"
#include "Misc/MessageDialog.h"        // T4 — Yes/No confirm before safety check
#include "Misc/Paths.h"
#include "Algo/Count.h"
#include "Containers/Ticker.h"

#define LOCTEXT_NAMESPACE "SShintToolsPanel"

// ─────────────────────────────────────────────────────────────────────────────
// T2 / ASSET-FIX-2 — CoreRedirects writer.
//
// IAssetTools::RenameAssets + FixupReferencers fixes references that exist as
// soft/hard pointers in other LOADED assets. It does NOT survive when the
// .uasset redirector is later deleted, and it does not cover unloaded packages
// or **native parent class references** — child Blueprints whose parent is a
// renamed Blueprint reload with "Class not found" until you provide a
// CoreRedirects mapping.
//
// ASSET-FIX-2 widens coverage to three redirect kinds per rename:
//   +ClassRedirects   — Blueprint generated-class lookup ("BP_Old_C")
//   +PackageRedirects — soft package paths ("/Game/.../BP_Old")
//   +ObjectRedirects  — UObject paths ("/Game/.../BP_Old.BP_Old")
//
// Previously only one of the three was emitted per asset; soft references and
// FObjectPath-style references therefore broke until the editor restarted with
// hand-written redirects. Now every BP rename emits all three; non-BP renames
// emit Package + Object. Existing entries are deduplicated so re-running the
// bot is idempotent.
// ─────────────────────────────────────────────────────────────────────────────
struct FShintRedirectEntry
{
	FString Key;     // "+ClassRedirects" / "+PackageRedirects" / "+ObjectRedirects"
	FString OldName; // /Game/Path/Asset (caller adds .Asset / _C suffixes per kind)
	FString NewName;
};

static FString FormatCoreRedirectValue(const FString& OldName, const FString& NewName)
{
	return FString::Printf(TEXT("(OldName=\"%s\",NewName=\"%s\")"), *OldName, *NewName);
}

static int32 WriteShintCoreRedirects(const TArray<FShintRedirectEntry>& Entries)
{
	if (Entries.IsEmpty()) return 0;

	const FString IniPath = FPaths::ProjectConfigDir() / TEXT("DefaultEngine.ini");
	const TCHAR* Section  = TEXT("CoreRedirects");

	// Read existing entries so we can dedupe. GConfig stores +Foo=... lines as
	// an array under the Foo key, so we split read by entry kind.
	TArray<FString> ExistingClass;
	TArray<FString> ExistingPackage;
	TArray<FString> ExistingObject;
	GConfig->GetArray(Section, TEXT("+ClassRedirects"),   ExistingClass,   IniPath);
	GConfig->GetArray(Section, TEXT("+PackageRedirects"), ExistingPackage, IniPath);
	GConfig->GetArray(Section, TEXT("+ObjectRedirects"),  ExistingObject,  IniPath);

	int32 Added = 0;
	for (const FShintRedirectEntry& E : Entries)
	{
		const FString Value = FormatCoreRedirectValue(E.OldName, E.NewName);

		TArray<FString>* Bucket = nullptr;
		if      (E.Key.Equals(TEXT("+ClassRedirects")))   Bucket = &ExistingClass;
		else if (E.Key.Equals(TEXT("+PackageRedirects"))) Bucket = &ExistingPackage;
		else if (E.Key.Equals(TEXT("+ObjectRedirects")))  Bucket = &ExistingObject;
		else continue; // unknown redirect kind — skip rather than corrupt the .ini

		// Dedupe by exact textual match — UE normalises whitespace away.
		const bool bAlreadyPresent = Bucket->ContainsByPredicate(
			[&Value](const FString& S) { return S.Equals(Value, ESearchCase::IgnoreCase); });
		if (!bAlreadyPresent)
		{
			Bucket->Add(Value);
			++Added;
		}
	}

	if (Added > 0)
	{
		GConfig->SetArray(Section, TEXT("+ClassRedirects"),   ExistingClass,   IniPath);
		GConfig->SetArray(Section, TEXT("+PackageRedirects"), ExistingPackage, IniPath);
		GConfig->SetArray(Section, TEXT("+ObjectRedirects"),  ExistingObject,  IniPath);
		GConfig->Flush(/*Read=*/false, IniPath);
	}
	return Added;
}

// ─────────────────────────────────────────────────────────────────────────────
// Toast helper — surfaces backend / connectivity failures to the user instead
// of silently leaving the UI in an empty state. Without this, "no response
// from server" looked like the buttons were dead.
// ─────────────────────────────────────────────────────────────────────────────
static void ShintShowErrorToast(const FString& Title, const FString& Detail)
{
	FNotificationInfo Info(FText::FromString(Title));
	Info.SubText      = FText::FromString(Detail.IsEmpty()
		? FString(TEXT("Check that the Core Engine container is running and that "
		               "core_host / core_port in shinttools.config.json point to it."))
		: Detail);
	Info.ExpireDuration = 8.0f;
	Info.bUseLargeFont  = false;
	Info.bUseSuccessFailIcons = true;
	TSharedPtr<SNotificationItem> N = FSlateNotificationManager::Get().AddNotification(Info);
	if (N.IsValid()) N->SetCompletionState(SNotificationItem::CS_Fail);
	UE_LOG(LogShintTools, Error, TEXT("%s — %s"), *Title, *Detail);
}

// ─────────────────────────────────────────────────────────────────────────────
// Brush cache
// ─────────────────────────────────────────────────────────────────────────────

namespace ST4
{
	// TUniquePtr keeps FSlateBrush at a stable heap address — TMap reallocation
	// does NOT invalidate the brush pointer stored in Slate widget attributes.
	static TMap<FString, TUniquePtr<FSlateBrush>> BC;

	static const FSlateBrush* Solid(const FLinearColor& C, float R = 0.f)
	{
		const FString K = FString::Printf(TEXT("S%.3f%.3f%.3f%.1f"), C.R, C.G, C.B, R);
		if (!BC.Contains(K))
		{
			TUniquePtr<FSlateBrush> B = MakeUnique<FSlateBrush>();
			B->TintColor = FSlateColor(C);
			B->DrawAs    = R > 0.f ? ESlateBrushDrawType::RoundedBox : ESlateBrushDrawType::Box;
			if (R > 0.f) {
				B->OutlineSettings.CornerRadii  = FVector4(R,R,R,R);
				B->OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
			}
			BC.Add(K, MoveTemp(B));
		}
		return BC[K].Get();
	}

	static const FSlateBrush* Outline(const FLinearColor& Fill, const FLinearColor& Brd, float R = 4.f)
	{
		const FString K = FString::Printf(TEXT("O%.3f%.3f%.1f"), Fill.R, Brd.R, R);
		if (!BC.Contains(K))
		{
			TUniquePtr<FSlateBrush> B = MakeUnique<FSlateBrush>();
			B->TintColor = FSlateColor(Fill);
			B->DrawAs    = ESlateBrushDrawType::RoundedBox;
			B->OutlineSettings.CornerRadii  = FVector4(R,R,R,R);
			B->OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
			B->OutlineSettings.Color        = FSlateColor(Brd);
			B->OutlineSettings.Width        = 1.f;
			BC.Add(K, MoveTemp(B));
		}
		return BC[K].Get();
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Construct
// ─────────────────────────────────────────────────────────────────────────────

void SShintToolsPanel::Construct(const FArguments& InArgs)
{
	CoreClient     = MakeShared<FShintCoreClient>();
	ProcessManager = MakeShared<FCoreProcessManager>();

	// UI-REDESIGN — dashboard shell: VBox(TopBar) over HBox(Sidebar, SwitcherContent).
	// The sidebar pushes destination changes to SetDestinationIndex(); the switcher
	// reads CurrentDestinationIndex via a lambda so the routing stays declarative
	// without manual SetActiveWidgetIndex() calls.
	//
	// Each destination wraps a single existing Build*Section() inside a SScrollBox
	// so per-section vertical scrolling works independently of the sidebar — the
	// rail itself never scrolls.

	auto CurrentTitle = [this]() -> FText
	{
		switch (static_cast<EShintDestination>(CurrentDestinationIndex))
		{
		case EShintDestination::Code:     return NSLOCTEXT("ShintPanel","TitleCode",     "Code Validator");
		case EShintDestination::Assets:   return NSLOCTEXT("ShintPanel","TitleAssets",   "Asset Naming Bot");
		case EShintDestination::Settings: return NSLOCTEXT("ShintPanel","TitleSettings", "Settings");
		case EShintDestination::Overview:
		default:                          return NSLOCTEXT("ShintPanel","TitleOverview", "Overview");
		}
	};

	auto StatusText = [this]() -> FText
	{
		switch (static_cast<EShintConnState>(CurrentConnStateIndex))
		{
		case EShintConnState::Connected:    return NSLOCTEXT("ShintPanel","Conn",  "Connected");
		case EShintConnState::Connecting:   return NSLOCTEXT("ShintPanel","Probe", "Connecting…");
		case EShintConnState::Disconnected: return NSLOCTEXT("ShintPanel","Down",  "Core offline");
		case EShintConnState::Unknown:
		default:                            return NSLOCTEXT("ShintPanel","Idle",  "Idle");
		}
	};

	auto ActiveDest = [this]() -> EShintDestination
	{
		return static_cast<EShintDestination>(CurrentDestinationIndex);
	};

	auto ConnState = [this]() -> EShintConnState
	{
		return static_cast<EShintConnState>(CurrentConnStateIndex);
	};

	// Wrap a section in a vertical scrollbox so long content doesn't push the
	// sidebar/topbar off-screen. Padding around the section uses S5 (24px) to
	// give the dashboard feel some breathing room from the edges.
	auto WrapSection = [](TSharedRef<SWidget> Content) -> TSharedRef<SWidget>
	{
		return SNew(SScrollBox)
			.Orientation(Orient_Vertical)
			+ SScrollBox::Slot()
			.Padding(FMargin(FShintStyle::Space::S5))
			[ Content ];
	};

	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(ST4::Solid(C_BG()))
		.Padding(0.f)
		[
			SNew(SVerticalBox)

			// ── Top bar ──────────────────────────────────────────────────
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SShintTopBar)
				.Title_Lambda(CurrentTitle)
				.StatusText_Lambda(StatusText)
				.ConnState_Lambda(ConnState)
			]

			// ── Body: sidebar | content ──────────────────────────────────
			+ SVerticalBox::Slot()
			.FillHeight(1.f)
			[
				SNew(SHorizontalBox)

				// Left rail
				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SShintSidebar)
					.Active_Lambda(ActiveDest)
					.OnSelected_Lambda([this](EShintDestination Dest)
					{
						SetDestinationIndex(static_cast<int32>(Dest));
					})
				]

				// Destination switcher
				+ SHorizontalBox::Slot()
				.FillWidth(1.f)
				[
					SAssignNew(DestinationSwitcher, SWidgetSwitcher)
					.WidgetIndex_Lambda([this]() { return CurrentDestinationIndex; })

					// 0 — Overview: header + status bar (legacy; KPI tile hero
					// will replace this in a future step)
					// Overview hero (step 8 of UI-REDESIGN) — 4 KPI tiles bound
					// to LastQualityScore via Value_Lambda; legacy BuildHeader +
					// BuildStatusBar retired.
					+ SWidgetSwitcher::Slot()
					[ WrapSection(BuildOverviewHero()) ]

					// 1 — Code Validator
					+ SWidgetSwitcher::Slot()
					[ WrapSection(BuildCodeValidatorSection()) ]

					// 2 — Asset Naming Bot
					+ SWidgetSwitcher::Slot()
					[ WrapSection(BuildAssetNamingSection()) ]

					// 3 — Settings (was Config Section, now its own destination)
					+ SWidgetSwitcher::Slot()
					[ WrapSection(BuildConfigSection()) ]
				]
			]
		]
	];
}

void SShintToolsPanel::SetDestinationIndex(int32 Index)
{
	// Clamp defensively so an out-of-range value can't crash the switcher.
	if (Index < 0) Index = 0;
	if (Index > 3) Index = 3;
	CurrentDestinationIndex = Index;
	// The switcher's WidgetIndex_Lambda will read the new value on the next
	// tick — no explicit refresh needed.
}

SShintToolsPanel::~SShintToolsPanel()
{
}

// ─────────────────────────────────────────────────────────────────────────────
// Static helpers
// ─────────────────────────────────────────────────────────────────────────────

TSharedRef<SWidget> SShintToolsPanel::Divider()
{
	return SNew(SBox).HeightOverride(1.f)
		[ SNew(SBorder).BorderImage(ST4::Solid(C_Border())).Padding(0.f) ];
}

TSharedRef<SWidget> SShintToolsPanel::BuildSectionTitle(const FText& Title, const FText& Subtitle)
{
	// UI-REDESIGN — uses ShintStyle tokens so all section headings render with
	// the same Bahnschrift typography + spacing as the rest of the dashboard.
	// Title sits on the H2 scale (16px), subtitle on Caption (10px) with the
	// muted text color.
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, FShintStyle::Space::S1)
		[
			SNew(STextBlock).Text(Title)
			.Font(FShintStyle::Fonts::H2())
			.ColorAndOpacity(FSlateColor(FShintStyle::Colors::TextPrimary()))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, FShintStyle::Space::S4)
		[
			SNew(STextBlock).Text(Subtitle)
			.Font(FShintStyle::Fonts::Caption())
			.ColorAndOpacity(FSlateColor(FShintStyle::Colors::TextMuted()))
		];
}

TSharedRef<SWidget> SShintToolsPanel::BuildDiffLine(
	const FString& Icon, const FString& Text,
	const FLinearColor& IconColor, const FLinearColor& TextColor)
{
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 6.f, 0.f)
		[
			SNew(STextBlock).Text(FText::FromString(Icon)).Font(F_Mono())
			.ColorAndOpacity(FSlateColor(IconColor))
		]
		+ SHorizontalBox::Slot().FillWidth(1.f)
		[
			SNew(STextBlock).Text(FText::FromString(Text)).Font(F_Mono())
			.ColorAndOpacity(FSlateColor(TextColor)).AutoWrapText(true)
		];
}

TSharedRef<SWidget> SShintToolsPanel::BuildContextPanel(
	const FString& Label, const FString& ContextText,
	int32 ContextLineStart, int32 IssueLineNo,
	const FLinearColor& HighlightColor)
{
	// Build a titled code block that highlights IssueLineNo
	TSharedRef<SVerticalBox> Lines = SNew(SVerticalBox);

	// Header label (BEFORE / AFTER)
	Lines->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)
	[
		SNew(STextBlock).Text(FText::FromString(Label)).Font(F_Label())
		.ColorAndOpacity(FSlateColor(C_DimGray()))
	];

	TArray<FString> SrcLines;
	ContextText.ParseIntoArray(SrcLines, TEXT("\n"), false);

	for (int32 Idx = 0; Idx < SrcLines.Num(); ++Idx)
	{
		const int32 LineNo = ContextLineStart + Idx;
		const bool  bIsIssueLine = (LineNo == IssueLineNo);
		const FLinearColor TextCol = bIsIssueLine ? HighlightColor : C_Gray();
		const FString Prefix = FString::Printf(TEXT("%4d  "), LineNo);

		Lines->AddSlot().AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(STextBlock).Text(FText::FromString(Prefix))
				.Font(F_Mono()).ColorAndOpacity(FSlateColor(C_DimGray()))
			]
			+ SHorizontalBox::Slot().FillWidth(1.f)
			[
				SNew(STextBlock).Text(FText::FromString(SrcLines[Idx]))
				.Font(F_Mono()).ColorAndOpacity(FSlateColor(TextCol))
				.AutoWrapText(false)
			]
		];
	}

	return SNew(SBorder)
		.BorderImage(ST4::Solid(C_CodeBG()))
		.Padding(FMargin(8.f, 6.f))
		[ Lines ];
}

TSharedRef<SWidget> SShintToolsPanel::BuildModuleProgressBar(
	TSharedPtr<SProgressBar>& OutBar,
	TAttribute<TOptional<float>> PercentAttr)
{
	return SNew(SBox).HeightOverride(2.f)
		[
			SAssignNew(OutBar, SProgressBar)
			.Percent(PercentAttr)
			.FillColorAndOpacity(FSlateColor(C_Blue()))
			.BackgroundImage(FAppStyle::GetBrush("ProgressBar.Background"))
		];
}

FString SShintToolsPanel::FmtN(int32 N)
{
	return N < 0 ? TEXT("\u2014") : FString::Printf(TEXT("%d"), N);
}

// ─────────────────────────────────────────────────────────────────────────────
// Header
// ─────────────────────────────────────────────────────────────────────────────

TSharedRef<SWidget> SShintToolsPanel::BuildHeader()
{
	return SNew(SBorder)
		.BorderImage(ST4::Solid(C_BG()))
		.Padding(FMargin(20.f, 18.f, 20.f, 14.f))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(STextBlock).Text(LOCTEXT("Brand","ShintTools"))
				.Font(F_Title()).ColorAndOpacity(FSlateColor(C_White()))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f,4.f,0.f,0.f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("Sub","Automation and optimization tools for Unreal Engine and Unity"))
				.Font(F_Label()).ColorAndOpacity(FSlateColor(C_Gray()))
			]
		];
}

// ─────────────────────────────────────────────────────────────────────────────
// Config section
// ─────────────────────────────────────────────────────────────────────────────

TSharedRef<SWidget> SShintToolsPanel::BuildConfigSection()
{
	const FShintCoreConfig& Cfg = CoreClient->GetConfig();

	auto ConfigRow = [this](const FText& Label, TSharedPtr<SEditableTextBox>& OutField,
		const FString& InitialValue, const FText& Hint) -> TSharedRef<SWidget>
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f,0.f,10.f,0.f)
			[
				SNew(SBox).WidthOverride(110.f)
				[
					SNew(STextBlock).Text(Label).Font(F_Label())
					.ColorAndOpacity(FSlateColor(C_DimGray()))
				]
			]
			+ SHorizontalBox::Slot().FillWidth(1.f)
			[
				SAssignNew(OutField, SEditableTextBox)
				.Text(FText::FromString(InitialValue))
				.HintText(Hint)
				.Font(F_Mono())
				.OnTextCommitted_Lambda([this](const FText&, ETextCommit::Type) { SaveConfigOverrides(); })
			];
	};

	return SNew(SBorder)
		.BorderImage(ST4::Outline(C_Surface(), C_Border()))
		.Padding(FMargin(20.f, 12.f))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f,0.f,0.f,8.f)
			[
				SNew(STextBlock).Text(LOCTEXT("CfgTitle","PROJECT CONFIG"))
				.Font(F_Label()).ColorAndOpacity(FSlateColor(C_DimGray()))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f,0.f,0.f,6.f)
			[
				ConfigRow(LOCTEXT("CfgProjId","Project ID"), ProjectIdField,
					Cfg.ProjectId, LOCTEXT("CfgProjIdHint","proj_..."))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f,0.f,0.f,6.f)
			[
				ConfigRow(LOCTEXT("CfgApiKey","API Key"), ApiKeyField,
					Cfg.ApiKey, LOCTEXT("CfgApiKeyHint","shint_..."))
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				ConfigRow(LOCTEXT("CfgDashUrl","Dashboard URL"), DashboardUrlField,
					Cfg.DashboardUrl, LOCTEXT("CfgDashUrlHint","https://shint.tools"))
			]
		];
}

void SShintToolsPanel::SaveConfigOverrides()
{
	if (!CoreClient.IsValid()) return;

	FShintCoreConfig& Cfg = CoreClient->GetConfigMutable();

	if (ProjectIdField.IsValid())    Cfg.ProjectId    = ProjectIdField->GetText().ToString();
	if (ApiKeyField.IsValid())       Cfg.ApiKey       = ApiKeyField->GetText().ToString();
	if (DashboardUrlField.IsValid()) Cfg.DashboardUrl = DashboardUrlField->GetText().ToString();

	CoreClient->SaveConfig();
}

// ─────────────────────────────────────────────────────────────────────────────
// Status bar
// ─────────────────────────────────────────────────────────────────────────────

TSharedRef<SWidget> SShintToolsPanel::BuildStatusBar()
{
	return SNew(SBorder)
		.BorderImage(ST4::Outline(C_Surface(), C_Border()))
		.Padding(FMargin(18.f, 10.f))
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f,0.f,8.f,0.f)
			[
				SNew(STextBlock).Text(FText::FromString(TEXT("●"))).Font(F_Body())
				.ColorAndOpacity(TAttribute<FSlateColor>::Create(
					TAttribute<FSlateColor>::FGetter::CreateSP(this, &SShintToolsPanel::GetStatusColor)))
			]

			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f,0.f,6.f,0.f)
			[
				SNew(STextBlock).Text(LOCTEXT("CoreLbl","CORE ENGINE")).Font(F_Label())
				.ColorAndOpacity(FSlateColor(C_DimGray()))
			]

			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(TAttribute<FText>::Create(
					TAttribute<FText>::FGetter::CreateSP(this, &SShintToolsPanel::GetStatusText)))
				.Font(F_Small()).ColorAndOpacity(FSlateColor(C_White()))
			]

			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SButton)
				.ButtonColorAndOpacity(C_BG())
				.ContentPadding(FMargin(14.f,5.f))
				.OnClicked(this, &SShintToolsPanel::OnCheckConnectionClicked)
				[
					SNew(STextBlock).Text(LOCTEXT("CheckBtn","Check Connection"))
					.Font(F_Small()).ColorAndOpacity(FSlateColor(C_Blue()))
				]
			]
		];
}

// ─────────────────────────────────────────────────────────────────────────────
// Stat badge
// ─────────────────────────────────────────────────────────────────────────────

// UI-REDESIGN — KPI badge for the metrics row at the top of the Code Validator
// section. Wrapped in SShintCard so the metrics read as discrete dashboard
// tiles instead of free-floating numbers, with caption above and the big value
// below (matches launcher KPI layout).
//
// `OutLabel` is captured into a STextBlock that callers continue to update via
// `OutLabel->SetText("123")` — the surrounding visuals change without breaking
// the existing controller code that mutates the badge content on scan results.
static TSharedRef<SWidget> StatBadge(
	TSharedPtr<STextBlock>& OutLabel, const FText& Caption, const FLinearColor& Clr)
{
	SAssignNew(OutLabel, STextBlock)
		.Text(FText::FromString(TEXT("—")))
		.Font(FShintStyle::Fonts::H1())
		.ColorAndOpacity(FSlateColor(Clr));

	return SNew(SShintCard)
		.bShowHeader(false)
		.ContentPadding(FShintStyle::Space::S3)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(FMargin(0.f, 0.f, 0.f, FShintStyle::Space::S1))
			[
				SNew(STextBlock)
				.Text(Caption)
				.Font(FShintStyle::Fonts::Caption())
				.ColorAndOpacity(FSlateColor(FShintStyle::Colors::TextMuted()))
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				OutLabel.ToSharedRef()
			]
		];
}

// ─────────────────────────────────────────────────────────────────────────────
// Code Validator section
// ─────────────────────────────────────────────────────────────────────────────

TSharedRef<SWidget> SShintToolsPanel::BuildCodeValidatorSection()
{
	return SNew(SBorder)
		.BorderImage(ST4::Solid(C_BG()))
		.Padding(FMargin(20.f, 18.f))
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot().AutoHeight()
			[
				BuildSectionTitle(
					LOCTEXT("CVTitle","CODE VALIDATOR"),
					LOCTEXT("CVSub","Analyse C++ source and Blueprints · review issues · apply fixes · send to dashboard"))
			]

			// Stats — 4-up KPI grid. Each tile is a SShintCard so metrics read
			// as discrete dashboard surfaces. Slot HAlign is Fill + horizontal
			// padding for inter-tile gutters; FillWidth(1.f) gives equal width.
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f,0.f,0.f,FShintStyle::Space::S2)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.f).Padding(FMargin(0.f, 0.f, FShintStyle::Space::S2 * 0.5f, 0.f))
				[ StatBadge(CodeFiles_Label,    LOCTEXT("CVF","FILES"),    FShintStyle::Colors::TextPrimary())  ]
				+ SHorizontalBox::Slot().FillWidth(1.f).Padding(FMargin(FShintStyle::Space::S2 * 0.5f, 0.f, FShintStyle::Space::S2 * 0.5f, 0.f))
				[ StatBadge(CodeErrors_Label,   LOCTEXT("CVE","ERRORS"),   FShintStyle::Colors::SevCritical())  ]
				+ SHorizontalBox::Slot().FillWidth(1.f).Padding(FMargin(FShintStyle::Space::S2 * 0.5f, 0.f, FShintStyle::Space::S2 * 0.5f, 0.f))
				[ StatBadge(CodeWarnings_Label, LOCTEXT("CVW","WARNINGS"), FShintStyle::Colors::SevHigh())      ]
				// Slice B — Quality Score overall tile
				+ SHorizontalBox::Slot().FillWidth(1.f).Padding(FMargin(FShintStyle::Space::S2 * 0.5f, 0.f, 0.f, 0.f))
				[ StatBadge(CodeScore_Label,    LOCTEXT("CVQ","QUALITY"),  FShintStyle::Colors::SevLow())       ]
			]
			// Slice B — sub-score breakdown line (perf / sec / bp / maint / naming)
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f,0.f,0.f,16.f).HAlign(HAlign_Center)
			[
				SAssignNew(CodeScoreBreakdown_Label, STextBlock)
					.Text(LOCTEXT("CVQBreakdownEmpty",
						"Quality Score: run a scan to compute"))
					.Font(F_Small())
					.ColorAndOpacity(FSlateColor(C_Gray()))
			]

			+ SVerticalBox::Slot().AutoHeight().Padding(0.f,0.f,0.f,14.f)
			[
				BuildModuleProgressBar(CodeProgressBar,
					TAttribute<TOptional<float>>::Create(
						TAttribute<TOptional<float>>::FGetter::CreateSP(this, &SShintToolsPanel::GetCodeProgress)))
			]

			// Scan buttons
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f,0.f,0.f,18.f)
			[
				SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(8.f,6.f))
				+ SWrapBox::Slot()
				[
					SNew(SButton).ContentPadding(FMargin(14.f,7.f))
					.OnClicked(this, &SShintToolsPanel::OnScanProjectClicked)
					[
						SNew(STextBlock).Text(LOCTEXT("ScanSrc",">  Scan All C++ Source")).Font(F_Small())
						.ColorAndOpacity(FSlateColor(C_White()))
					]
				]
				+ SWrapBox::Slot()
				[
					SNew(SButton).ContentPadding(FMargin(14.f,7.f))
					.OnClicked(this, &SShintToolsPanel::OnScanBlueprintsClicked)
					[
						SNew(STextBlock).Text(LOCTEXT("ScanBP",">  Scan All BP")).Font(F_Small())
						.ColorAndOpacity(FSlateColor(C_White()))
					]
				]
			]

			// Results panel
			+ SVerticalBox::Slot().AutoHeight()
			[ BuildCodeResultsPanel() ]
		];
}

// ─────────────────────────────────────────────────────────────────────────────
// Code filter bar
// ─────────────────────────────────────────────────────────────────────────────

TSharedRef<SWidget> SShintToolsPanel::BuildCategoryMenuContent()
{
	struct FCatEntry { FText Label; EIssueCategoryFilter Value; };
	const TArray<FCatEntry> Entries = {
		{ LOCTEXT("CatAll",  "All Categories"),   EIssueCategoryFilter::All            },
		{ LOCTEXT("CatPerf", "Performance"),      EIssueCategoryFilter::Performance    },
		{ LOCTEXT("CatBest", "Best Practices"),   EIssueCategoryFilter::BestPractices  },
		{ LOCTEXT("CatSec",  "Security"),         EIssueCategoryFilter::Security       },
		{ LOCTEXT("CatMain", "Maintainability"),  EIssueCategoryFilter::Maintainability},
	};

	TSharedRef<SVerticalBox> Menu = SNew(SVerticalBox);
	for (const FCatEntry& E : Entries)
	{
		Menu->AddSlot().AutoHeight()
		[
			SNew(SButton)
			.ButtonColorAndOpacity(FSlateColor(C_Surface()))
			.ContentPadding(FMargin(12.f, 6.f))
			.OnClicked_Lambda([this, Value = E.Value, Label = E.Label]() -> FReply
			{
				CurrentCategoryFilter = Value;
				if (CategoryFilterLabel.IsValid())
					CategoryFilterLabel->SetText(Label);
				ApplyCodeFilter();
				return FReply::Handled();
			})
			[
				SNew(STextBlock).Text(E.Label).Font(F_Label())
				.ColorAndOpacity(FSlateColor(C_White()))
			]
		];
	}
	return SNew(SBorder)
		.BorderImage(ST4::Solid(C_Surface()))
		.Padding(2.f)
		[ Menu ];
}

TSharedRef<SWidget> SShintToolsPanel::BuildSeverityMenuContent()
{
	struct FSevEntry { FText Label; EIssueSeverityFilter Value; };
	const TArray<FSevEntry> Entries = {
		{ LOCTEXT("SevAll",  "All Severities"), EIssueSeverityFilter::All      },
		{ LOCTEXT("SevCrit", "Critical"),       EIssueSeverityFilter::Critical  },
		{ LOCTEXT("SevErr",  "Error"),          EIssueSeverityFilter::Error     },
		{ LOCTEXT("SevWarn", "Warning"),        EIssueSeverityFilter::Warning   },
		{ LOCTEXT("SevInfo", "Info"),           EIssueSeverityFilter::Info      },
	};

	TSharedRef<SVerticalBox> Menu = SNew(SVerticalBox);
	for (const FSevEntry& E : Entries)
	{
		Menu->AddSlot().AutoHeight()
		[
			SNew(SButton)
			.ButtonColorAndOpacity(FSlateColor(C_Surface()))
			.ContentPadding(FMargin(12.f, 6.f))
			.OnClicked_Lambda([this, Value = E.Value, Label = E.Label]() -> FReply
			{
				CurrentSeverityFilter = Value;
				if (SeverityFilterLabel.IsValid())
					SeverityFilterLabel->SetText(Label);
				ApplyCodeFilter();
				return FReply::Handled();
			})
			[
				SNew(STextBlock).Text(E.Label).Font(F_Label())
				.ColorAndOpacity(FSlateColor(C_White()))
			]
		];
	}
	return SNew(SBorder)
		.BorderImage(ST4::Solid(C_Surface()))
		.Padding(2.f)
		[ Menu ];
}

TSharedRef<SWidget> SShintToolsPanel::BuildAssetTypeMenuContent()
{
	struct FTypeEntry { FText Label; EAssetTypeFilter Value; };
	const TArray<FTypeEntry> Entries = {
		{ LOCTEXT("ATAll",   "All Types"),   EAssetTypeFilter::All        },
		{ LOCTEXT("ATMat",   "Materials"),   EAssetTypeFilter::Materials  },
		{ LOCTEXT("ATTex",   "Textures"),    EAssetTypeFilter::Textures   },
		{ LOCTEXT("ATMesh",  "Meshes"),      EAssetTypeFilter::Meshes     },
		{ LOCTEXT("ATBP",    "Blueprints"),  EAssetTypeFilter::Blueprints },
		{ LOCTEXT("ATVFX",   "VFX"),         EAssetTypeFilter::VFX        },
		{ LOCTEXT("ATAudio", "Audio"),       EAssetTypeFilter::Audio      },
		{ LOCTEXT("ATAnim",  "Animations"),  EAssetTypeFilter::Animations },
		{ LOCTEXT("ATData",  "Data"),        EAssetTypeFilter::Data       },
	};

	TSharedRef<SVerticalBox> Menu = SNew(SVerticalBox);
	for (const FTypeEntry& E : Entries)
	{
		Menu->AddSlot().AutoHeight()
		[
			SNew(SButton)
			.ButtonColorAndOpacity(FSlateColor(C_Surface()))
			.ContentPadding(FMargin(12.f, 6.f))
			.OnClicked_Lambda([this, Value = E.Value, Label = E.Label]() -> FReply
			{
				CurrentAssetTypeFilter = Value;
				if (AssetTypeFilterLabel.IsValid())
					AssetTypeFilterLabel->SetText(Label);
				ApplyAssetFilter();
				return FReply::Handled();
			})
			[
				SNew(STextBlock).Text(E.Label).Font(F_Label())
				.ColorAndOpacity(FSlateColor(C_White()))
			]
		];
	}
	return SNew(SBorder)
		.BorderImage(ST4::Solid(C_Surface()))
		.Padding(2.f)
		[ Menu ];
}

TSharedRef<SWidget> SShintToolsPanel::BuildCodeTypeMenuContent()
{
	struct FEntry { FText Label; ECodeTypeFilter Value; };
	const TArray<FEntry> Entries = {
		{ LOCTEXT("CodeTypeAll", "All Types"),       ECodeTypeFilter::All           },
		{ LOCTEXT("CodeTypeCpp", "C++ Only"),         ECodeTypeFilter::CppOnly       },
		{ LOCTEXT("CodeTypeBP",  "Blueprints Only"),  ECodeTypeFilter::BlueprintsOnly},
	};

	TSharedRef<SVerticalBox> Menu = SNew(SVerticalBox);
	for (const FEntry& E : Entries)
	{
		const FText  Label = E.Label;
		const ECodeTypeFilter Value = E.Value;
		Menu->AddSlot().AutoHeight()
		[
			SNew(SButton).ContentPadding(FMargin(10.f, 5.f))
			.ButtonColorAndOpacity(FSlateColor(C_Surface()))
			.OnClicked_Lambda([this, Label, Value]() -> FReply
			{
				CurrentCodeTypeFilter = Value;
				if (CodeTypeFilterLabel.IsValid())
					CodeTypeFilterLabel->SetText(Label);
				ApplyCodeFilter();
				return FReply::Handled();
			})
			[
				SNew(STextBlock).Text(Label).Font(F_Label())
				.ColorAndOpacity(FSlateColor(C_Gray()))
			]
		];
	}
	return SNew(SBorder)
		.BorderImage(ST4::Solid(C_Surface()))
		.Padding(2.f)
		[ Menu ];
}

TSharedRef<SWidget> SShintToolsPanel::BuildCodeFilterBar()
{
	// Category dropdown
	TSharedRef<SWidget> CategoryCombo =
		SNew(SComboButton)
		.ContentPadding(FMargin(8.f, 4.f))
		.ButtonColorAndOpacity(FSlateColor(C_Surface()))
		.OnGetMenuContent(this, &SShintToolsPanel::BuildCategoryMenuContent)
		.ButtonContent()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SAssignNew(CategoryFilterLabel, STextBlock)
				.Text(LOCTEXT("CatAll","All Categories"))
				.Font(FShintStyle::Fonts::Caption())
				.ColorAndOpacity(FSlateColor(FShintStyle::Colors::TextPrimary()))
			]
			// SComboButton already renders Unreal's native dropdown arrow icon;
			// the manual unicode ▾ glyph rendered as a missing-glyph box on
			// Bahnschrift's variable axis. Removed in favor of the engine icon.
		];

	// Severity dropdown
	TSharedRef<SWidget> SeverityCombo =
		SNew(SComboButton)
		.ContentPadding(FMargin(8.f, 4.f))
		.ButtonColorAndOpacity(FSlateColor(C_Surface()))
		.OnGetMenuContent(this, &SShintToolsPanel::BuildSeverityMenuContent)
		.ButtonContent()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SAssignNew(SeverityFilterLabel, STextBlock)
				.Text(LOCTEXT("SevAll","All Severities"))
				.Font(FShintStyle::Fonts::Caption())
				.ColorAndOpacity(FSlateColor(FShintStyle::Colors::TextPrimary()))
			]
			// SComboButton already renders Unreal's native dropdown arrow icon;
			// the manual unicode ▾ glyph rendered as a missing-glyph box on
			// Bahnschrift's variable axis. Removed in favor of the engine icon.
		];

	// Code type dropdown (C++ / Blueprints / All)
	TSharedRef<SWidget> CodeTypeCombo =
		SNew(SComboButton)
		.ContentPadding(FMargin(8.f, 4.f))
		.ButtonColorAndOpacity(FSlateColor(C_Surface()))
		.OnGetMenuContent(this, &SShintToolsPanel::BuildCodeTypeMenuContent)
		.ButtonContent()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SAssignNew(CodeTypeFilterLabel, STextBlock)
				.Text(LOCTEXT("CodeTypeAll","All Types"))
				.Font(FShintStyle::Fonts::Caption())
				.ColorAndOpacity(FSlateColor(FShintStyle::Colors::TextPrimary()))
			]
			// SComboButton already renders Unreal's native dropdown arrow icon;
			// the manual unicode ▾ glyph rendered as a missing-glyph box on
			// Bahnschrift's variable axis. Removed in favor of the engine icon.
		];

	// Fixable toggle
	TSharedRef<SWidget> FixableBtn =
		SNew(SButton).ContentPadding(FMargin(8.f, 4.f))
		.ButtonColorAndOpacity(FSlateColor(C_Surface()))
		.OnClicked_Lambda([this]() -> FReply
		{
			CurrentFilter = (CurrentFilter == EIssueFilter::FixableOnly)
				? EIssueFilter::All : EIssueFilter::FixableOnly;
			ApplyCodeFilter();
			return FReply::Handled();
		})
		[
			SNew(STextBlock).Text(LOCTEXT("FFix","Fixable Only")).Font(F_Label())
			.ColorAndOpacity(FSlateColor(C_Green()))
		];

	return SNew(SVerticalBox)

		// Row 1: "RESULTS" label + dropdowns + fixable toggle
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f,0.f,0.f,6.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
			[
				SNew(STextBlock).Text(LOCTEXT("ResLbl","RESULTS")).Font(F_Label())
				.ColorAndOpacity(FSlateColor(C_DimGray()))
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.f,0.f,6.f,0.f) [ CodeTypeCombo ]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.f,0.f,6.f,0.f) [ CategoryCombo ]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.f,0.f,6.f,0.f) [ SeverityCombo ]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.f,0.f,10.f,0.f) [ FixableBtn    ]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.f,0.f,4.f,0.f)
			[
				SNew(SButton).ContentPadding(FMargin(8.f, 4.f))
				.OnClicked(this, &SShintToolsPanel::OnSelectAllCodeClicked)
				[ SNew(STextBlock).Text(LOCTEXT("SelAll","Select All")).Font(F_Label())
				  .ColorAndOpacity(FSlateColor(C_Blue())) ]
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SButton).ContentPadding(FMargin(8.f, 4.f))
				.OnClicked(this, &SShintToolsPanel::OnDeselectAllCodeClicked)
				[ SNew(STextBlock).Text(LOCTEXT("DeselAll","Deselect All")).Font(F_Label())
				  .ColorAndOpacity(FSlateColor(C_Gray())) ]
			]
		];
}

// ─────────────────────────────────────────────────────────────────────────────
// Code results panel
// ─────────────────────────────────────────────────────────────────────────────

TSharedRef<SWidget> SShintToolsPanel::BuildCodeResultsPanel()
{
	SAssignNew(CodeEmptyState, SBox)
	.HAlign(HAlign_Center).VAlign(VAlign_Center).MinDesiredHeight(64.f)
	[
		SAssignNew(CodeEmptyText, STextBlock)
		.Text(LOCTEXT("CVEmpty","Run a scan to see results here."))
		.Font(F_Small()).ColorAndOpacity(FSlateColor(C_DimGray()))
	];

	TSharedRef<SWidget> ListArea =
		SNew(SVerticalBox)

		// Filter + select toolbar
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f,0.f,0.f,8.f) [ BuildCodeFilterBar() ]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f,0.f,0.f,8.f) [ Divider() ]

		// Virtual list
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SBox).MaxDesiredHeight(900.f)
			[
				SAssignNew(CodeIssueListView, SListView<FShintIssueItemPtr>)
				.ListItemsSource(&CodeIssueItems)
				.OnGenerateRow(this, &SShintToolsPanel::GenerateCodeIssueRow)
				.SelectionMode(ESelectionMode::None)
				.AllowOverscroll(EAllowOverscroll::Yes)
			]
		]

		+ SVerticalBox::Slot().AutoHeight().Padding(0.f,8.f,0.f,0.f) [ Divider() ]

		// Action row
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f,8.f,0.f,0.f)
		[
			SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(8.f,6.f))
			+ SWrapBox::Slot()
			[
				SAssignNew(ApplyCodeBtn, SButton)
				.IsEnabled(false).ContentPadding(FMargin(14.f,7.f))
				.OnClicked(this, &SShintToolsPanel::OnApplySelectedCodeFixesClicked)
				[
					SAssignNew(ApplyCodeBtnLabel, STextBlock)
					.Text(LOCTEXT("ApplyCode","✓  Apply Selected (0)"))
					.Font(F_Small()).ColorAndOpacity(FSlateColor(C_Green()))
				]
			]
			+ SWrapBox::Slot()
			[
				SAssignNew(SendCodeBtn, SButton)
				.IsEnabled(false).ContentPadding(FMargin(14.f,7.f))
				.OnClicked(this, &SShintToolsPanel::OnSendCodeToDashboardClicked)
				[
					SAssignNew(SendCodeBtnLabel, STextBlock)
					.Text(LOCTEXT("SendCode","↑  Send to Dashboard"))
					.Font(F_Small()).ColorAndOpacity(FSlateColor(C_Blue()))
				]
			]
			+ SWrapBox::Slot()
			[
				// Auto-Fix Plan — calls /agent/plan, shows a modal with the
				// prioritized step list. Indie-only: free-tier servers
				// answer 403 and the modal surfaces an upgrade hint.
				SNew(SButton)
				.ContentPadding(FMargin(14.f,7.f))
				.OnClicked(this, &SShintToolsPanel::OnAutoFixPlanClicked)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("AutoFixPlan","✨  Auto-Fix Plan"))
					.Font(F_Small()).ColorAndOpacity(FSlateColor(FLinearColor(0.55f, 0.42f, 0.95f)))
				]
			]
		];

	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight() [ CodeEmptyState.ToSharedRef() ]
		+ SVerticalBox::Slot().AutoHeight() [ ListArea ];
}

// ─────────────────────────────────────────────────────────────────────────────
// Code issue row
// ─────────────────────────────────────────────────────────────────────────────

TSharedRef<ITableRow> SShintToolsPanel::GenerateCodeIssueRow(
	FShintIssueItemPtr Item, const TSharedRef<STableViewBase>& Owner)
{
	const bool bError = (Item->Severity == TEXT("error"));
	const FLinearColor SevColor  = bError ? C_Red() : C_Yellow();
	const FLinearColor RowBG     = (Item->OriginalIndex % 2 == 0) ? C_RowEven() : C_RowOdd();
	const FString      AutoBadge = Item->bIsAutoFixable ? TEXT("  AUTO") : TEXT("");

	// Location string: for blueprints show "ClassName > GraphName", for C++ show "File:Line"
	const bool bIsBlueprintIssue = !Item->Graph.IsEmpty();
	FString LocationStr;
	if (bIsBlueprintIssue)
	{
		LocationStr = Item->Class.IsEmpty()
			? FString::Printf(TEXT("%s > %s"), *Item->FileName, *Item->Graph)
			: FString::Printf(TEXT("%s > %s"), *Item->Class, *Item->Graph);
	}
	else if (!Item->Class.IsEmpty())
	{
		LocationStr = FString::Printf(TEXT("%s :: %s : %d"), *Item->Class, *Item->FileName, Item->Line);
	}
	else
	{
		LocationStr = FString::Printf(TEXT("%s : %d"), *Item->FileName, Item->Line);
	}

	// bHasContext: true when server sent context window OR when we can fetch it (tree-sitter)
	const bool bHasContext  = !Item->ContextBefore.IsEmpty() || !Item->FileContent.IsEmpty();
	// bIsFixable: auto-fixable issues (tree-sitter, local line-replacement, or plugin-side BP handler)
	const bool bIsFixable   = Item->bIsAutoFixable
		&& (!Item->FixSuggestion.IsEmpty() || !Item->FileContent.IsEmpty() || Item->bIsBlueprint);

	// ── Context diff panels (shown when Preview is toggled) ──────────────────
	// Priority: FixPreviewCode (fetched on-demand from /validate/fix) >
	//           ContextAfter  (pre-computed by server fixer during scan — real code).
	// ContextAfter is now always actual fixed code from the tree-sitter/pattern fixer,
	// never a human-readable description, so it is safe to display for all issue types.
	const FString& AfterText = !Item->FixPreviewCode.IsEmpty()
		? Item->FixPreviewCode
		: Item->ContextAfter;
	const bool bAfterAvailable = !AfterText.IsEmpty();
	const bool bAfterLoading   = Item->bFixPreviewLoading;

	TSharedRef<SWidget> ContextDiff = SNullWidget::NullWidget;
	if (bHasContext)
	{
		TSharedRef<SWidget> AntesPanelWidget =
			BuildContextPanel(TEXT("BEFORE"), Item->ContextBefore,
				Item->ContextLineStart, Item->Line, C_DiffRed());

		TSharedRef<SWidget> DespuesPanelWidget = SNullWidget::NullWidget;
		if (bAfterLoading)
		{
			DespuesPanelWidget = SNew(STextBlock)
				.Text(FText::FromString(TEXT("Fetching preview...")))
				.Font(F_Label()).ColorAndOpacity(FSlateColor(C_DimGray()));
		}
		else if (bAfterAvailable)
		{
			DespuesPanelWidget = BuildContextPanel(TEXT("AFTER"), AfterText,
				Item->ContextLineStart, Item->Line, C_DiffGreen());
		}

		ContextDiff =
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f).Padding(0.f, 0.f, 4.f, 0.f)
			[ AntesPanelWidget ]
			+ SHorizontalBox::Slot().FillWidth(1.f).Padding(4.f, 0.f, 0.f, 0.f).HAlign(HAlign_Fill)
			[
				SNew(SBox)
				.Visibility((bAfterAvailable || bAfterLoading) ? EVisibility::Visible : EVisibility::Collapsed)
				[ DespuesPanelWidget ]
			];
	}

	// ── Compact diff fallback (blueprints / issues without context window) ───
	TSharedRef<SWidget> CompactDiff = SNew(SBorder)
		.Visibility((Item->ContextBefore.IsEmpty() && !Item->Snippet.IsEmpty())
			? EVisibility::Visible : EVisibility::Collapsed)
		.BorderImage(ST4::Solid(C_CodeBG()))
		.Padding(FMargin(8.f, 5.f))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SBox).Visibility(Item->Snippet.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible)
				[ BuildDiffLine(TEXT(">"), Item->Snippet, C_Red(), C_DiffRed()) ]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 3.f, 0.f, 0.f)
			[
				SNew(SBox).Visibility(Item->FixSuggestion.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible)
				[ BuildDiffLine(TEXT("\u2192"), Item->FixSuggestion, C_Green(), C_DiffGreen()) ]
			]
		];

	return SNew(STableRow<FShintIssueItemPtr>, Owner)
		.Style(FAppStyle::Get(), "TableView.Row").Padding(0.f)
		[
			SNew(SBorder).BorderImage(ST4::Solid(RowBG)).Padding(FMargin(12.f, 9.f))
			[
				SNew(SHorizontalBox)

				// Checkbox
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top).Padding(0.f,2.f,10.f,0.f)
				[
					SNew(SCheckBox)
					.IsChecked(Item->bChecked ? ECheckBoxState::Checked : ECheckBoxState::Unchecked)
					.OnCheckStateChanged_Lambda([this, Item](ECheckBoxState S) {
						Item->bChecked = (S == ECheckBoxState::Checked);
						RefreshApplyCodeLabel();
					})
				]

				// Content column
				+ SHorizontalBox::Slot().FillWidth(1.f)
				[
					SNew(SVerticalBox)

					// Row 1: severity pill + rule_id + location + preview toggle + AUTO badge
					// (UI-REDESIGN — the ● dot was replaced by SShintSeverityBadge so
					//  the severity name is rendered alongside the color, mirroring the
					//  launcher/web-dashboard look. SevColor stays in scope so older
					//  call-sites still compile until they migrate.)
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f,0.f,0.f,4.f)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f,0.f,8.f,0.f)
						[
							SNew(SShintSeverityBadge).Severity(Item->Severity)
						]
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f,0.f,10.f,0.f)
						[
							SNew(STextBlock).Text(FText::FromString(Item->RuleId)).Font(FShintStyle::Fonts::Small())
							.ColorAndOpacity(FSlateColor(C_White()))
						]
						+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
						[
							SNew(STextBlock).Text(FText::FromString(LocationStr))
							.Font(F_Mono()).ColorAndOpacity(FSlateColor(C_Gray()))
						]
						// Preview toggle — only for issues with context
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(6.f,0.f,6.f,0.f)
						[
							SNew(SBox).Visibility(bHasContext ? EVisibility::Visible : EVisibility::Collapsed)
							[
								SNew(SButton).ContentPadding(FMargin(6.f, 2.f))
								.ButtonColorAndOpacity(FSlateColor(C_Surface()))
								.OnClicked_Lambda([this, Item]() -> FReply {
									Item->bPreviewExpanded = !Item->bPreviewExpanded;
									// Fetch on-demand only when ContextAfter is also empty
									// (server could not run the fixer at scan time).
									if (Item->bPreviewExpanded
										&& !Item->FileContent.IsEmpty()
										&& Item->ContextAfter.IsEmpty()
										&& Item->FixPreviewCode.IsEmpty()
										&& !Item->bFixPreviewLoading)
									{
										FetchFixPreview(Item);
									}
									if (CodeIssueListView.IsValid())
										CodeIssueListView->RequestListRefresh();
									return FReply::Handled();
								})
								[
									SNew(STextBlock)
									.Text_Lambda([Item]() {
										return FText::FromString(Item->bPreviewExpanded
											? TEXT("\u25BC Preview") : TEXT("\u25B6 Preview"));
									})
									.Font(F_Label()).ColorAndOpacity(FSlateColor(C_Blue()))
								]
							]
						]
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
						[
							SNew(STextBlock).Text(FText::FromString(AutoBadge)).Font(F_Label())
							.ColorAndOpacity(FSlateColor(C_Blue()))
						]
					]

					// Row 2: message
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f,0.f,0.f,5.f)
					[
						SNew(STextBlock).Text(FText::FromString(Item->Message)).Font(F_Small())
						.ColorAndOpacity(FSlateColor(C_White())).AutoWrapText(true)
					]

					// Row 3: compact diff (no context) OR expanded context diff
					+ SVerticalBox::Slot().AutoHeight() [ CompactDiff ]

					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, bHasContext ? 6.f : 0.f, 0.f, 0.f)
					[
						SNew(SBox)
						.Visibility_Lambda([Item]() {
							return (Item->bPreviewExpanded && !Item->ContextBefore.IsEmpty())
								? EVisibility::Visible : EVisibility::Collapsed;
						})
						[ ContextDiff ]
					]

					// Row 4: Apply / Ignore — always visible for auto-fixable issues
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 6.f, 0.f, 0.f)
					[
						SNew(SBox).Visibility(bIsFixable ? EVisibility::Visible : EVisibility::Collapsed)
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot().AutoWidth().Padding(0.f,0.f,8.f,0.f)
							[
								SNew(SButton).ContentPadding(FMargin(12.f, 5.f))
								.OnClicked(this, &SShintToolsPanel::OnApplySingleFix, Item)
								[
									SNew(STextBlock).Text(LOCTEXT("ApplySingle","✓  Apply"))
									.Font(F_Small()).ColorAndOpacity(FSlateColor(C_Green()))
								]
							]
							+ SHorizontalBox::Slot().AutoWidth()
							[
								SNew(SButton).ContentPadding(FMargin(12.f, 5.f))
								.OnClicked(this, &SShintToolsPanel::OnIgnoreSingleFix, Item)
								[
									SNew(STextBlock).Text(LOCTEXT("IgnoreSingle","✗  Ignore"))
									.Font(F_Small()).ColorAndOpacity(FSlateColor(C_DimGray()))
								]
							]
						]
					]
				]
			]
		];
}

// ─────────────────────────────────────────────────────────────────────────────
// Asset Naming Bot section
// ─────────────────────────────────────────────────────────────────────────────

TSharedRef<SWidget> SShintToolsPanel::BuildAssetNamingSection()
{
	return SNew(SBorder)
		.BorderImage(ST4::Solid(C_BG()))
		.Padding(FMargin(20.f,18.f))
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot().AutoHeight()
			[
				BuildSectionTitle(
					LOCTEXT("ANBTitle","ASSET NAMING BOT"),
					LOCTEXT("ANBSub","Scan entire project · detect invalid names · apply UE5 rename (refs preserved) · send to dashboard"))
			]

			// Stats — 3-up KPI grid (asset count / invalid / scan time).
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f,0.f,0.f,FShintStyle::Space::S4)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.f).Padding(FMargin(0.f, 0.f, FShintStyle::Space::S2 * 0.5f, 0.f))
				[ StatBadge(AssetTotal_Label,   LOCTEXT("ANBT","ASSETS"),   FShintStyle::Colors::TextPrimary()) ]
				+ SHorizontalBox::Slot().FillWidth(1.f).Padding(FMargin(FShintStyle::Space::S2 * 0.5f, 0.f, FShintStyle::Space::S2 * 0.5f, 0.f))
				[ StatBadge(AssetInvalid_Label, LOCTEXT("ANBI","INVALID"),  FShintStyle::Colors::SevCritical()) ]
				+ SHorizontalBox::Slot().FillWidth(1.f).Padding(FMargin(FShintStyle::Space::S2 * 0.5f, 0.f, 0.f, 0.f))
				[ StatBadge(AssetTime_Label,    LOCTEXT("ANBMS","TIME (s)"), FShintStyle::Colors::TextMuted()) ]
			]

			+ SVerticalBox::Slot().AutoHeight().Padding(0.f,0.f,0.f,14.f)
			[
				BuildModuleProgressBar(AssetProgressBar,
					TAttribute<TOptional<float>>::Create(
						TAttribute<TOptional<float>>::FGetter::CreateSP(this, &SShintToolsPanel::GetAssetProgress)))
			]

			+ SVerticalBox::Slot().AutoHeight().Padding(0.f,0.f,0.f,18.f)
			[
				SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(8.f,6.f))
				+ SWrapBox::Slot()
				[
					SNew(SButton).ContentPadding(FMargin(14.f,7.f))
					.OnClicked(this, &SShintToolsPanel::OnScanAssetsClicked)
					[
						SNew(STextBlock).Text(LOCTEXT("ScanAssets",">  Scan All Assets")).Font(F_Small())
						.ColorAndOpacity(FSlateColor(C_White()))
					]
				]
			]

			+ SVerticalBox::Slot().AutoHeight()
			[ BuildAssetResultsPanel() ]
		];
}

TSharedRef<SWidget> SShintToolsPanel::BuildAssetResultsPanel()
{
	SAssignNew(AssetEmptyState, SBox)
	.HAlign(HAlign_Center).VAlign(VAlign_Center).MinDesiredHeight(64.f)
	[
		SAssignNew(AssetEmptyText, STextBlock)
		.Text(LOCTEXT("ANBEmpty","Run a scan to see naming violations."))
		.Font(F_Small()).ColorAndOpacity(FSlateColor(C_DimGray()))
	];

	TSharedRef<SWidget> AssetTypeCombo =
		SNew(SComboButton)
		.ContentPadding(FMargin(8.f, 4.f))
		.ButtonColorAndOpacity(FSlateColor(C_Surface()))
		.OnGetMenuContent(this, &SShintToolsPanel::BuildAssetTypeMenuContent)
		.ButtonContent()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SAssignNew(AssetTypeFilterLabel, STextBlock)
				.Text(LOCTEXT("ATAll","All Types"))
				.Font(FShintStyle::Fonts::Caption())
				.ColorAndOpacity(FSlateColor(FShintStyle::Colors::TextPrimary()))
			]
			// SComboButton already renders Unreal's native dropdown arrow icon;
			// the manual unicode ▾ glyph rendered as a missing-glyph box on
			// Bahnschrift's variable axis. Removed in favor of the engine icon.
		];

	TSharedRef<SWidget> ListArea =
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f,0.f,0.f,8.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
			[
				SNew(STextBlock).Text(LOCTEXT("ANBRes","RESULTS")).Font(F_Label())
				.ColorAndOpacity(FSlateColor(C_DimGray()))
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.f,0.f,6.f,0.f) [ AssetTypeCombo ]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.f,0.f,4.f,0.f)
			[
				SNew(SButton).ContentPadding(FMargin(10.f,4.f))
				.OnClicked(this, &SShintToolsPanel::OnSelectAllAssetsClicked)
				[ SNew(STextBlock).Text(LOCTEXT("ANBSel","Select All")).Font(F_Label())
				  .ColorAndOpacity(FSlateColor(C_Blue())) ]
			]
			// T6 — Deselect All companion button. Lives next to Select All so
			// users have symmetric controls for the asset rename batch.
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SButton).ContentPadding(FMargin(10.f,4.f))
				.OnClicked(this, &SShintToolsPanel::OnDeselectAllAssetsClicked)
				[ SNew(STextBlock).Text(LOCTEXT("ANBDes","Deselect All")).Font(F_Label())
				  .ColorAndOpacity(FSlateColor(C_DimGray())) ]
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f,0.f,0.f,8.f) [ Divider() ]
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SBox).MaxDesiredHeight(900.f)
			[
				SAssignNew(AssetIssueListView, SListView<FShintAssetItemPtr>)
				.ListItemsSource(&AssetIssueItems)
				.OnGenerateRow(this, &SShintToolsPanel::GenerateAssetIssueRow)
				.SelectionMode(ESelectionMode::None)
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f,8.f,0.f,0.f) [ Divider() ]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f,8.f,0.f,0.f)
		[
			SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(8.f,6.f))
			+ SWrapBox::Slot()
			[
				SAssignNew(ApplyAssetBtn, SButton)
				.IsEnabled(false).ContentPadding(FMargin(14.f,7.f))
				.OnClicked(this, &SShintToolsPanel::OnApplySelectedAssetFixesClicked)
				[
					SAssignNew(ApplyAssetBtnLabel, STextBlock)
					.Text(LOCTEXT("ApplyAsset","✓  Apply Corrections (0)"))
					.Font(F_Small()).ColorAndOpacity(FSlateColor(C_Green()))
				]
			]
			+ SWrapBox::Slot()
			[
				SAssignNew(SendAssetBtn, SButton)
				.IsEnabled(false).ContentPadding(FMargin(14.f,7.f))
				.OnClicked(this, &SShintToolsPanel::OnSendAssetToDashboardClicked)
				[
					SAssignNew(SendAssetBtnLabel, STextBlock)
					.Text(LOCTEXT("SendAsset","↑  Send to Dashboard"))
					.Font(F_Small()).ColorAndOpacity(FSlateColor(C_Blue()))
				]
			]
		];

	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight() [ AssetEmptyState.ToSharedRef() ]
		+ SVerticalBox::Slot().AutoHeight() [ ListArea ];
}

TSharedRef<ITableRow> SShintToolsPanel::GenerateAssetIssueRow(
	FShintAssetItemPtr Item, const TSharedRef<STableViewBase>& Owner)
{
	const FLinearColor RowBG = (Item->OriginalIndex % 2 == 0) ? C_RowEven() : C_RowOdd();

	return SNew(STableRow<FShintAssetItemPtr>, Owner)
		.Style(FAppStyle::Get(), "TableView.Row").Padding(0.f)
		[
			SNew(SBorder).BorderImage(ST4::Solid(RowBG)).Padding(FMargin(12.f,9.f))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f,0.f,10.f,0.f)
				[
					SNew(SCheckBox)
					.IsChecked(Item->bChecked ? ECheckBoxState::Checked : ECheckBoxState::Unchecked)
					.OnCheckStateChanged_Lambda([this, Item](ECheckBoxState S) {
						Item->bChecked = (S == ECheckBoxState::Checked);
						RefreshApplyAssetLabel();
					})
				]
				+ SHorizontalBox::Slot().FillWidth(1.f)
				[
					SNew(SVerticalBox)

					// Type + path row
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f,0.f,0.f,4.f)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth().Padding(0.f,0.f,6.f,0.f)
						[
							SNew(STextBlock).Text(FText::FromString(TEXT("⚠"))).Font(F_Small())
							.ColorAndOpacity(FSlateColor(C_Yellow()))
						]
						+ SHorizontalBox::Slot().AutoWidth().Padding(0.f,0.f,10.f,0.f)
						[
							SNew(STextBlock).Text(FText::FromString(Item->AssetType))
							.Font(FCoreStyle::GetDefaultFontStyle("Bold",10))
							.ColorAndOpacity(FSlateColor(C_White()))
						]
						+ SHorizontalBox::Slot().FillWidth(1.f)
						[
							SNew(STextBlock).Text(FText::FromString(Item->AssetPath))
							.Font(F_Mono()).ColorAndOpacity(FSlateColor(C_Gray()))
						]
					]

					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(SBorder).BorderImage(ST4::Solid(C_CodeBG()))
						.Padding(FMargin(8.f,4.f))
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot().AutoWidth().Padding(0.f,0.f,6.f,0.f)
							[ SNew(STextBlock).Text(FText::FromString(TEXT(">"))).Font(F_Mono())
							  .ColorAndOpacity(FSlateColor(C_Red())) ]
							+ SHorizontalBox::Slot().AutoWidth().Padding(0.f,0.f,12.f,0.f)
							[ SNew(STextBlock).Text(FText::FromString(Item->CurrentName))
							  .Font(F_Mono()).ColorAndOpacity(FSlateColor(C_DiffRed())) ]
							+ SHorizontalBox::Slot().AutoWidth().Padding(0.f,0.f,6.f,0.f)
							[ SNew(STextBlock).Text(FText::FromString(TEXT("\u2192"))).Font(F_Mono())
							  .ColorAndOpacity(FSlateColor(C_Green())) ]
							+ SHorizontalBox::Slot().FillWidth(1.f)
							[ SNew(STextBlock).Text(FText::FromString(Item->SuggestedName))
							  .Font(F_Mono()).ColorAndOpacity(FSlateColor(C_DiffGreen())) ]
						]
					]
				]
			]
		];
}

// ─────────────────────────────────────────────────────────────────────────────
// Button handlers
// ─────────────────────────────────────────────────────────────────────────────

FReply SShintToolsPanel::OnCheckConnectionClicked()
{
	SetStatus(ECoreStatus::Checking);
	CoreClient->CheckHealth(FOnShintRequestComplete::CreateSP(
		this, &SShintToolsPanel::OnHealthCheckComplete));
	return FReply::Handled();
}

FReply SShintToolsPanel::OnScanProjectClicked()
{
	// ── All-clear guard ───────────────────────────────────────────────────────
	// If every issue found in the previous scan has already been fixed this
	// session, skip the server round-trip and show an informational message.
	// Clearing AppliedFixFingerprints means the *next* click triggers a real scan.
	if (AllCodeItems.IsEmpty() && !AppliedFixFingerprints.IsEmpty())
	{
		AppliedFixFingerprints.Empty();
		if (CodeEmptyText.IsValid())
			CodeEmptyText->SetText(LOCTEXT("CVAllFixed",
				"✓  All issues resolved — click 'Scan' again to do a full re-scan."));
		if (CodeEmptyState.IsValid())
			CodeEmptyState->SetVisibility(EVisibility::Visible);
		return FReply::Handled();
	}

	++ScanGeneration;
	bBlueprintScanActive = false;   // full source scan — no BP-only filter

	// Auto-set the code type filter to C++ Only for this scan
	// T1 — Auto-switch the visible filter to "All" so the user sees both
	// kinds at once (the previous flow forced CppOnly/BlueprintsOnly on every
	// click, which silently hid the other half of the merged list).
	CurrentCodeTypeFilter = ECodeTypeFilter::All;
	if (CodeTypeFilterLabel.IsValid())
		CodeTypeFilterLabel->SetText(LOCTEXT("CodeTypeAll","All"));

	SetCodeState(EModuleState::Running);
	if (CodeEmptyText.IsValid())
		CodeEmptyText->SetText(LOCTEXT("CVEmpty", "Run a scan to see results here."));

	// T1 — Do NOT wipe LastCodeResult or AllCodeItems here. HandleValidateResult
	// merges the new C++ findings against the existing Blueprint findings (and
	// vice-versa) by RuleId prefix; clearing them on click defeats the merge
	// and the BP results disappear the moment a C++ scan starts.
	// We only reset the visible (filtered) list so the panel doesn't paint stale
	// rows during the scan.
	CodeIssueItems.Reset();
	if (CodeIssueListView.IsValid()) CodeIssueListView->RebuildList();

	CoreClient->ValidateProject(FPaths::GameSourceDir(),
		FOnShintValidateComplete::CreateSP(this, &SShintToolsPanel::OnProjectValidateComplete));
	return FReply::Handled();
}

FReply SShintToolsPanel::OnScanBlueprintsClicked()
{
	++ScanGeneration;
	bBlueprintScanActive = true;

	// T1 — see OnScanProjectClicked. Auto-switch to "All" instead of forcing
	// BlueprintsOnly, and preserve previous-scan state so the merge survives.
	CurrentCodeTypeFilter = ECodeTypeFilter::All;
	if (CodeTypeFilterLabel.IsValid())
		CodeTypeFilterLabel->SetText(LOCTEXT("CodeTypeAll","All"));

	SetCodeState(EModuleState::Running);
	if (CodeEmptyText.IsValid())
		CodeEmptyText->SetText(LOCTEXT("CVEmpty", "Run a scan to see results here."));

	CodeIssueItems.Reset();
	if (CodeIssueListView.IsValid()) CodeIssueListView->RebuildList();

	CoreClient->ValidateBlueprints(FPaths::ProjectContentDir(),
		FOnShintValidateComplete::CreateSP(this, &SShintToolsPanel::OnBlueprintValidateComplete));

	return FReply::Handled();
}

FReply SShintToolsPanel::OnSelectAllCodeClicked()
{
	for (FShintIssueItemPtr& I : AllCodeItems)
		I->bChecked = true;
	if (CodeIssueListView.IsValid()) CodeIssueListView->RebuildList();
	RefreshApplyCodeLabel();
	return FReply::Handled();
}

FReply SShintToolsPanel::OnDeselectAllCodeClicked()
{
	for (FShintIssueItemPtr& I : AllCodeItems) I->bChecked = false;
	if (CodeIssueListView.IsValid()) CodeIssueListView->RebuildList();
	RefreshApplyCodeLabel();
	return FReply::Handled();
}

// ─────────────────────────────────────────────────────────────────────────────
// Apply code fixes
// ─────────────────────────────────────────────────────────────────────────────

FReply SShintToolsPanel::OnApplySelectedCodeFixesClicked()
{
	TArray<FShintCodeIssue> Accepted;

	for (const FShintIssueItemPtr& Item : AllCodeItems)
	{
		// Only accept issues that are checked AND actually auto-fixable
		if (!Item->bChecked || !Item->bIsAutoFixable) continue;
		// BP issues use plugin-side handlers — no FixSuggestion required
		if (!Item->bIsBlueprint && Item->FixSuggestion.IsEmpty()) continue;

		FShintCodeIssue I;
		I.RuleId         = Item->RuleId;
		I.Severity       = Item->Severity;
		I.Message        = Item->Message;
		I.FilePath       = Item->FilePath;
		I.Line           = Item->Line;
		I.Snippet        = Item->Snippet;
		I.FixSuggestion  = Item->FixSuggestion;
		I.bIsAutoFixable = Item->bIsAutoFixable;
		I.Class          = Item->Class;
		I.Category       = Item->Category;
		I.Graph          = Item->Graph;
		I.FileContent    = Item->FileContent;
		I.bChecked       = true;
		Accepted.Add(I);
	}

	// Log why Accepted might be empty — counters are independent of bIsAutoFixable
	int32 TotalChecked = 0, TotalNotFixable = 0, TotalNoFixSuggestion = 0;
	for (const FShintIssueItemPtr& Item : AllCodeItems)
	{
		if (Item->bChecked) ++TotalChecked;
		if (Item->bChecked && !Item->bIsAutoFixable)        ++TotalNotFixable;
		if (Item->bChecked && Item->FixSuggestion.IsEmpty()) ++TotalNoFixSuggestion;
	}
	UE_LOG(LogShintTools, Log,
		TEXT("ApplyFix: Checked=%d, NotAutoFixable=%d, NoFixSuggestion=%d, Accepted=%d"),
		TotalChecked, TotalNotFixable, TotalNoFixSuggestion, Accepted.Num());

	if (Accepted.IsEmpty())
	{
		UE_LOG(LogShintTools, Warning, TEXT("ApplyFix: Nothing to apply — no auto-fixable issues selected"));
		return FReply::Handled();
	}

	UE_LOG(LogShintTools, Log, TEXT("ApplyFix: Applying %d fix(es) locally."), Accepted.Num());

	PendingCodeFixes = Accepted;

	// T4 — Always ask first. The safety check is the dry-run that flags
	// fixes which would alter signatures, public API, or otherwise risk
	// breaking dependent code. The previous flow ran it implicitly, so the
	// user never knew it happened and either trusted the silent green path
	// or was confused by the warning popping up out of nowhere.
	const FText DialogTitle = FText::FromString(TEXT("ShintTools — Safety Check"));
	const FText DialogBody  = FText::FromString(TEXT(
		"Do you want to check if the fix breaks any code structure?\n\n"
		"Recommended: pick Yes. ShintTools will dry-run the fix server-side "
		"and warn you about anything that could ripple into other files.\n\n"
		"Pick No to apply immediately without the safety dry-run."));
	const EAppReturnType::Type Choice =
		FMessageDialog::Open(EAppMsgType::YesNo, DialogBody, DialogTitle);

	SetCodeState(EModuleState::Running);

	if (Choice == EAppReturnType::Yes)
	{
		CoreClient->CheckFixSafety(Accepted,
			FOnShintSafetyCheckComplete::CreateSP(this, &SShintToolsPanel::OnSafetyCheckComplete));
	}
	else
	{
		// User opted to skip the dry-run — short-circuit straight to apply.
		FShintSafetyCheckResult Skipped;
		Skipped.bSafe = true;
		OnSafetyCheckComplete(Skipped);
	}
	return FReply::Handled();
}

FReply SShintToolsPanel::OnSendCodeToDashboardClicked()
{
	CoreClient->SendCodeValidatorToDashboard(LastCodeResult,
		FOnShintWebDashboardComplete::CreateSP(this, &SShintToolsPanel::OnCodeDashboardComplete));
	return FReply::Handled();
}

FReply SShintToolsPanel::OnApplySingleFix(FShintIssueItemPtr Item)
{
	if (!Item.IsValid() || !Item->bIsAutoFixable
		|| (!Item->bIsBlueprint && Item->FixSuggestion.IsEmpty()))
		return FReply::Handled();

	TArray<FShintCodeIssue> Issues;
	FShintCodeIssue I;
	I.RuleId         = Item->RuleId;
	I.Severity       = Item->Severity;
	I.Message        = Item->Message;
	I.FilePath       = Item->FilePath;
	I.Line           = Item->Line;
	I.Snippet        = Item->Snippet;
	I.FixSuggestion  = Item->FixSuggestion;
	I.bIsAutoFixable = true;
	I.Class          = Item->Class;
	I.Category       = Item->Category;
	I.Graph          = Item->Graph;
	I.FileContent    = Item->FileContent;
	I.bChecked       = true;
	Issues.Add(I);

	PendingCodeFixes = Issues;

	// T4 — same Yes/No prompt as the batch path so the single-issue Apply
	// button gives the user the same control over the safety dry-run.
	const FText DialogTitle = FText::FromString(TEXT("ShintTools — Safety Check"));
	const FText DialogBody  = FText::FromString(TEXT(
		"Do you want to check if the fix breaks any code structure?\n\n"
		"Recommended: pick Yes. ShintTools will dry-run the fix server-side "
		"and warn you about anything that could ripple into other files.\n\n"
		"Pick No to apply immediately without the safety dry-run."));
	const EAppReturnType::Type Choice =
		FMessageDialog::Open(EAppMsgType::YesNo, DialogBody, DialogTitle);

	SetCodeState(EModuleState::Running);

	if (Choice == EAppReturnType::Yes)
	{
		CoreClient->CheckFixSafety(Issues,
			FOnShintSafetyCheckComplete::CreateSP(this, &SShintToolsPanel::OnSafetyCheckComplete));
	}
	else
	{
		FShintSafetyCheckResult Skipped;
		Skipped.bSafe = true;
		OnSafetyCheckComplete(Skipped);
	}
	return FReply::Handled();
}

// ─────────────────────────────────────────────────────────────────────────────
// Safety Check + Modal Dialog
// ─────────────────────────────────────────────────────────────────────────────

void SShintToolsPanel::OnSafetyCheckComplete(const FShintSafetyCheckResult& Result)
{
	if (Result.bSafe)
	{
		ProceedWithCodeFixes();
	}
	else
	{
		// Not safe — restore idle and show warning dialog
		SetCodeState(EModuleState::Idle);
		ShowSafetyWarningDialog(Result);
	}
}

void SShintToolsPanel::ShowSafetyWarningDialog(const FShintSafetyCheckResult& Result)
{
	TSharedRef<SWindow> Dialog = SNew(SWindow)
		.Title(NSLOCTEXT("ShintTools", "SafetyTitle", "Safety Check"))
		.ClientSize(FVector2D(560, 420))
		.SupportsMaximize(false)
		.SupportsMinimize(false)
		.IsTopmostWindow(true)
		.SizingRule(ESizingRule::FixedSize);

	// ── Header ────────────────────────────────────────────────────────────────
	TSharedRef<SVerticalBox> WarningList = SNew(SVerticalBox);
	for (int32 i = 0; i < Result.Warnings.Num(); ++i)
	{
		WarningList->AddSlot()
		.AutoHeight()
		.Padding(0.f, 2.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 8.f, 0.f)
			[
				SNew(STextBlock)
				.Text(FText::FromString(FString::Printf(TEXT("%d."), i + 1)))
				.Font(F_Small())
				.ColorAndOpacity(C_Gray())
			]
			+ SHorizontalBox::Slot().FillWidth(1.f)
			[
				SNew(STextBlock)
				.Text(FText::FromString(Result.Warnings[i]))
				.Font(F_Small())
				.ColorAndOpacity(C_White())
				.AutoWrapText(true)
			]
		];
	}

	// ── Buttons ───────────────────────────────────────────────────────────────
	TSharedPtr<SWindow> DialogPtr = TSharedPtr<SWindow>(&Dialog.Get(), [](SWindow*){});
	TWeakPtr<SWindow> WeakDialog(Dialog);

	Dialog->SetContent(
		SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("NoBorder"))
		.Padding(24.f)
		[
			SNew(SVerticalBox)
			// Header
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 12.f)
			[
				SNew(STextBlock)
				.Text(NSLOCTEXT("ShintTools", "SafetyHeader", "These fixes may affect your code"))
				.Font(FShintStyle::Fonts::H2())
				.ColorAndOpacity(C_Yellow())
			]
			// Warning list
			+ SVerticalBox::Slot().FillHeight(1.f).Padding(0.f, 0.f, 0.f, 12.f)
			[
				SNew(SScrollBox)
				+ SScrollBox::Slot()
				[
					WarningList
				]
			]
			// Preview (if provided)
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 16.f)
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("NoBorder"))
				.Visibility(Result.Preview.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible)
				[
					SNew(STextBlock)
					.Text(FText::FromString(Result.Preview))
					.Font(F_Mono())
					.ColorAndOpacity(C_Gray())
					.AutoWrapText(true)
				]
			]
			// Buttons
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.f)
				+ SHorizontalBox::Slot().AutoWidth().Padding(8.f, 0.f, 0.f, 0.f)
				[
					SNew(SButton)
					.ContentPadding(FMargin(14.f, 7.f))
					.ButtonColorAndOpacity(C_Surface())
					.OnClicked_Lambda([WeakDialog]() -> FReply
					{
						if (WeakDialog.IsValid()) WeakDialog.Pin()->RequestDestroyWindow();
						return FReply::Handled();
					})
					[
						SNew(STextBlock).Text(NSLOCTEXT("ShintTools","Cancel","Cancel"))
						.Font(F_Body()).ColorAndOpacity(C_Gray())
					]
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(8.f, 0.f, 0.f, 0.f)
				[
					SNew(SButton)
					.ContentPadding(FMargin(14.f, 7.f))
					.ButtonColorAndOpacity(C_Yellow())
					.OnClicked_Lambda([this, WeakDialog]() -> FReply
					{
						if (WeakDialog.IsValid()) WeakDialog.Pin()->RequestDestroyWindow();
						ProceedWithCodeFixes();
						return FReply::Handled();
					})
					[
						SNew(STextBlock)
						.Text(NSLOCTEXT("ShintTools","ApplyAnyway","Apply Anyway"))
						.Font(F_Body()).ColorAndOpacity(C_BG())
					]
				]
			]
		]
	);

	FSlateApplication::Get().AddModalWindow(Dialog, FSlateApplication::Get().GetActiveTopLevelWindow());
}

void SShintToolsPanel::ProceedWithCodeFixes()
{
	if (PendingCodeFixes.IsEmpty()) return;

	const uint32 FixGeneration = ScanGeneration;
	SetCodeState(EModuleState::Running);
	CoreClient->ApplyCodeFixes(PendingCodeFixes,
		FOnShintFixComplete::CreateSP(this, &SShintToolsPanel::OnCodeFixComplete, FixGeneration));
	PendingCodeFixes.Empty();
}


FReply SShintToolsPanel::OnIgnoreSingleFix(FShintIssueItemPtr Item)
{
	if (!Item.IsValid()) return FReply::Handled();

	// Fingerprint so this issue is skipped on the next incremental scan
	const FString Fingerprint = FString::Printf(
		TEXT("%s:%d:%s"), *Item->FilePath, Item->Line, *Item->RuleId);
	AppliedFixFingerprints.Add(Fingerprint);

	// Remove from backing store and rebuild visible list
	AllCodeItems.RemoveAll([&Item](const FShintIssueItemPtr& P){ return P == Item; });
	ApplyCodeFilter();
	RefreshApplyCodeLabel();
	return FReply::Handled();
}

void SShintToolsPanel::FetchFixPreview(FShintIssueItemPtr Item)
{
	if (!Item.IsValid() || Item->FileContent.IsEmpty() || Item->bFixPreviewLoading) return;

	Item->bFixPreviewLoading = true;
	if (CodeIssueListView.IsValid()) CodeIssueListView->RequestListRefresh();

	FShintCodeIssue Issue;
	Issue.RuleId      = Item->RuleId;
	Issue.FilePath    = Item->FilePath;
	Issue.Line        = Item->Line;
	Issue.FileContent = Item->FileContent;

	// Capture info needed to extract the window from fixed_code
	const int32 ContextStart = Item->ContextLineStart; // 1-based
	TArray<FString> BeforeLines;
	Item->ContextBefore.ParseIntoArray(BeforeLines, TEXT("\n"), false);
	const int32 NumContextLines = FMath::Max(1, BeforeLines.Num());

	TWeakPtr<SShintToolsPanel> WeakPtr = SharedThis(this);
	TWeakPtr<FShintIssueItem>  WeakItem = Item;

	CoreClient->FetchSingleFixPreview(Issue,
		FOnShintFixComplete::CreateLambda(
			[WeakPtr, WeakItem, ContextStart, NumContextLines](const FShintFixResult& Result) mutable
		{
			TSharedPtr<SShintToolsPanel> PinnedPanel = WeakPtr.Pin();
			TSharedPtr<FShintIssueItem>  PinnedItem  = WeakItem.Pin();
			if (!PinnedPanel.IsValid() || !PinnedItem.IsValid()) return;

			PinnedItem->bFixPreviewLoading = false;

			if (Result.bSuccess && Result.FixedFiles.Num() > 0)
			{
				const FString& FixedCode = Result.FixedFiles[0].CorrectedContent;
				TArray<FString> AllLines;
				FixedCode.ParseIntoArray(AllLines, TEXT("\n"), false);

				const int32 StartIdx = FMath::Max(0, ContextStart - 1);
				TArray<FString> Window;
				for (int32 i = StartIdx; i < StartIdx + NumContextLines && i < AllLines.Num(); ++i)
					Window.Add(AllLines[i]);

				PinnedItem->FixPreviewCode = FString::Join(Window, TEXT("\n"));
			}
			else
			{
				UE_LOG(LogShintTools, Warning, TEXT("FetchFixPreview: server error for [%s] — %s"),
					*PinnedItem->RuleId, *Result.ErrorMessage);
			}

			if (PinnedPanel->CodeIssueListView.IsValid())
				PinnedPanel->CodeIssueListView->RequestListRefresh();
		}));
}

FReply SShintToolsPanel::OnScanAssetsClicked()
{
	// ── All-clear guard ───────────────────────────────────────────────────────
	if (AllAssetItems.IsEmpty() && AssetFixesApplied > 0)
	{
		AssetFixesApplied = 0;
		if (AssetEmptyText.IsValid())
			AssetEmptyText->SetText(LOCTEXT("ANBAllFixed",
				"✓  All violations resolved — click 'Scan' again to do a full re-scan."));
		if (AssetEmptyState.IsValid())
			AssetEmptyState->SetVisibility(EVisibility::Visible);
		return FReply::Handled();
	}

	SetAssetState(EModuleState::Running);
	// Reset to default empty text before new results arrive
	if (AssetEmptyText.IsValid())
		AssetEmptyText->SetText(LOCTEXT("ANBEmpty", "Run a scan to see naming violations."));
	AllAssetItems.Empty();
	AssetIssueItems.Empty();
	if (AssetIssueListView.IsValid()) AssetIssueListView->RebuildList();
	CoreClient->ScanAssetNaming(FPaths::ProjectContentDir(),
		FOnShintAssetScanComplete::CreateSP(this, &SShintToolsPanel::OnAssetScanComplete));
	return FReply::Handled();
}

FReply SShintToolsPanel::OnSelectAllAssetsClicked()
{
	// Toggle the FULL backing store, not just the filtered view, so a partial
	// type filter doesn't leave items off-screen unchanged.
	for (FShintAssetItemPtr& I : AllAssetItems)   if (I.IsValid()) I->bChecked = true;
	for (FShintAssetItemPtr& I : AssetIssueItems) if (I.IsValid()) I->bChecked = true;
	if (AssetIssueListView.IsValid()) AssetIssueListView->RebuildList();
	RefreshApplyAssetLabel();
	return FReply::Handled();
}

// T6 — Deselect All for the asset naming bot. Symmetric companion to
// OnSelectAllAssetsClicked. Operates on AllAssetItems first so any items
// hidden by the active type filter are also unticked.
FReply SShintToolsPanel::OnDeselectAllAssetsClicked()
{
	for (FShintAssetItemPtr& I : AllAssetItems)   if (I.IsValid()) I->bChecked = false;
	for (FShintAssetItemPtr& I : AssetIssueItems) if (I.IsValid()) I->bChecked = false;
	if (AssetIssueListView.IsValid()) AssetIssueListView->RebuildList();
	RefreshApplyAssetLabel();
	return FReply::Handled();
}

// ─────────────────────────────────────────────────────────────────────────────
// Apply asset fixes
// ─────────────────────────────────────────────────────────────────────────────

FReply SShintToolsPanel::OnApplySelectedAssetFixesClicked()
{
	if (!FModuleManager::Get().IsModuleLoaded(TEXT("AssetTools"))) return FReply::Handled();

	const FAssetToolsModule& AssetToolsModule =
	FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");

	IAssetTools& AssetTools = AssetToolsModule.Get();

	IAssetRegistry& AR =
		FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();

	TArray<FAssetRenameData> RenameData;
	TArray<FShintAssetIssue> ForServer;
	// T2 — captured BEFORE RenameAssets() so we know the old object path.
	// After the rename, Asset->GetPathName() reports the new path, so any later
	// attempt to derive OldName would only emit identity mappings.
	TArray<FShintRedirectEntry> RedirectEntries;
	int32 SkippedCircular  = 0;
	int32 SkippedCollision = 0;
	int32 SkippedLoadFail  = 0;

	// Iterate the FULL backing store, not the filtered view. The previous
	// behavior renamed only the currently-visible items, so any active type
	// filter (Materials / Textures / etc.) silently skipped everything else
	// even though "Apply Corrections" advertises "all selected".
	for (const FShintAssetItemPtr& Item : AllAssetItems)
	{
		if (!Item->bChecked) continue;

		// ── E-001: skip circular / no-op renames ─────────────────────────────
		// If the suggested name equals the current on-disk name, firing a rename
		// creates a self-referencing ObjectRedirector and triggers an UE5 ensure.
		if (Item->SuggestedName.IsEmpty() ||
			Item->SuggestedName.Equals(Item->CurrentName, ESearchCase::CaseSensitive))
		{
			++SkippedCircular;
			continue;
		}

		// Load the UObject from its package path (/Game/...AssetName)
		UObject* Asset = StaticLoadObject(UObject::StaticClass(), nullptr, *Item->AssetPath);
		if (!Asset)
		{
			++SkippedLoadFail;
			UE_LOG(LogShintTools, Warning,
				TEXT("ShintPanel: skipped rename '%s' — failed to load UObject at %s"),
				*Item->CurrentName, *Item->AssetPath);
			continue;
		}

		const FString NewPackagePath = FPaths::GetPath(Item->AssetPath);

		// ── E-002: skip if the destination package already exists ────────────
		// Otherwise UE5 fails the rename with "An object named 'X' already exists".
		{
			const FString NewPackageName = NewPackagePath / Item->SuggestedName;
			TArray<FAssetData> ExistingAssets;
			AR.GetAssetsByPackageName(FName(*NewPackageName), ExistingAssets);
			if (ExistingAssets.Num() > 0)
			{
				UE_LOG(LogShintTools, Warning,
					TEXT("ShintPanel: skipped rename '%s' → '%s' (target already exists at %s)"),
					*Item->CurrentName, *Item->SuggestedName, *NewPackageName);
				++SkippedCollision;
				continue;
			}
		}

		RenameData.Add(FAssetRenameData(Asset, NewPackagePath, Item->SuggestedName));

		// ASSET-FIX-2 — record redirect mappings for DefaultEngine.ini.
		// Three redirect kinds, emitted per asset, because UE5 resolves
		// references through different paths depending on context:
		//
		//   +PackageRedirects (always)  — soft asset paths "/Game/.../OldName"
		//   +ObjectRedirects  (always)  — UObject paths    "/Game/.../OldName.OldName"
		//   +ClassRedirects   (BP only) — generated class  "/Game/.../OldName.OldName_C"
		//
		// Previously we emitted only one of these per asset, so renames broke
		// soft references in unloaded packages and child Blueprints whose parent
		// was renamed (parent class lookup fell through to "Class not found").
		{
			const FString OldPackage = Item->AssetPath;                          // /Game/.../OldName
			const FString NewPackage = NewPackagePath / Item->SuggestedName;     // /Game/.../NewName
			const FString OldBase    = FPaths::GetBaseFilename(OldPackage);
			const FString NewBase    = Item->SuggestedName;

			// 1) Package path — covers FName package references and soft asset paths.
			{
				FShintRedirectEntry RE;
				RE.Key     = TEXT("+PackageRedirects");
				RE.OldName = OldPackage;
				RE.NewName = NewPackage;
				RedirectEntries.Add(MoveTemp(RE));
			}

			// 2) Object path — covers UObject references stored as Path.Object.
			{
				FShintRedirectEntry RE;
				RE.Key     = TEXT("+ObjectRedirects");
				RE.OldName = OldPackage + TEXT(".") + OldBase;
				RE.NewName = NewPackage + TEXT(".") + NewBase;
				RedirectEntries.Add(MoveTemp(RE));
			}

			// 3) Generated class (BP only) — covers parent-class lookup on
			//    child Blueprints inheriting from this BP.
			if (Asset->IsA<UBlueprint>())
			{
				FShintRedirectEntry RE;
				RE.Key     = TEXT("+ClassRedirects");
				RE.OldName = OldPackage + TEXT(".") + OldBase + TEXT("_C");
				RE.NewName = NewPackage + TEXT(".") + NewBase + TEXT("_C");
				RedirectEntries.Add(MoveTemp(RE));
			}
		}

		FShintAssetIssue I;
		I.AssetPath    = Item->AssetPath;
		I.CurrentName  = Item->CurrentName;
		I.SuggestedName= Item->SuggestedName;
		I.AssetType    = Item->AssetType;
		ForServer.Add(I);
	}

	if (SkippedCircular + SkippedCollision + SkippedLoadFail > 0)
	{
		UE_LOG(LogShintTools, Log,
			TEXT("ShintPanel: asset rename pre-check skipped %d circular, %d collision(s), %d load-fail"),
			SkippedCircular, SkippedCollision, SkippedLoadFail);
	}

	if (RenameData.IsEmpty()) return FReply::Handled();

	// ASSET-FIX-2 — Pre-rename: gather every BP whose parent class is in this
	// rename batch. After the parents rename, RenameAssets/FixupReferencers
	// updates loaded references but leaves the **in-memory generated class
	// pointer** on each child BP stale (it still resolves to the old class name
	// via cached UClass*). Re-compiling each child after the rename forces the
	// kismet compiler to rebuild the parent pointer through the new class path.
	TSet<UBlueprint*> DescendantBPsToRecompile;
	{
		// Build map of OLD generated-class path -> NEW generated-class path,
		// extracted from the +ClassRedirects entries we just queued.
		TMap<FString, FString> OldBPClassToNew;
		for (const FShintRedirectEntry& RE : RedirectEntries)
		{
			if (RE.Key.Equals(TEXT("+ClassRedirects")))
				OldBPClassToNew.Add(RE.OldName, RE.NewName);
		}

		if (!OldBPClassToNew.IsEmpty())
		{
			FARFilter ChildFilter;
			ChildFilter.ClassPaths.Add(UBlueprint::StaticClass()->GetClassPathName());
			ChildFilter.bRecursiveClasses = true;
			ChildFilter.PackagePaths.Add(TEXT("/Game"));
			ChildFilter.bRecursivePaths = true;

			TArray<FAssetData> AllBPs;
			AR.GetAssets(ChildFilter, AllBPs);

			for (const FAssetData& BPData : AllBPs)
			{
				FString ParentClassPath;
				if (!BPData.GetTagValue(FBlueprintTags::ParentClassPath, ParentClassPath))
					continue;

				// The tag is stored either bare ("/Game/Foo.Foo_C") or wrapped
				// ("/Script/Engine.Class'/Game/Foo.Foo_C'"); strip the wrapper.
				if (ParentClassPath.Contains(TEXT("'")))
				{
					int32 First = INDEX_NONE, Last = INDEX_NONE;
					ParentClassPath.FindChar(TEXT('\''), First);
					Last = ParentClassPath.Find(TEXT("'"),
						ESearchCase::CaseSensitive, ESearchDir::FromEnd);
					if (First != INDEX_NONE && Last != INDEX_NONE && Last > First)
						ParentClassPath = ParentClassPath.Mid(First + 1, Last - First - 1);
				}

				if (OldBPClassToNew.Contains(ParentClassPath))
				{
					// GetAsset() forces a synchronous load — required so the
					// child is in memory when we recompile it after the rename.
					if (UBlueprint* Child = Cast<UBlueprint>(BPData.GetAsset()))
						DescendantBPsToRecompile.Add(Child);
				}
			}

			if (!DescendantBPsToRecompile.IsEmpty())
			{
				UE_LOG(LogShintTools, Log,
					TEXT("ShintPanel: %d descendant BP(s) queued for recompile after parent rename"),
					DescendantBPsToRecompile.Num());
			}
		}
	}

	AssetTools.RenameAssets(RenameData);

	// ── Fix redirectors left at old paths ─────────────────────────────────────
	// After RenameAssets, UE5 creates an ObjectRedirector at the original package
	// path. Collect all redirectors under /Game and fix references so no stale
	// pointers remain and DefaultEngine.ini stays clean.
	{
		FARFilter RedirFilter;
		RedirFilter.ClassPaths.Add(UObjectRedirector::StaticClass()->GetClassPathName());
		RedirFilter.PackagePaths.Add(TEXT("/Game"));
		RedirFilter.bRecursivePaths = true;

		TArray<FAssetData> RedirAssets;
		AR.GetAssets(RedirFilter, RedirAssets);

		TArray<UObjectRedirector*> Redirectors;
		Redirectors.Reserve(RedirAssets.Num());
		for (const FAssetData& RD : RedirAssets)
		{
			if (UObjectRedirector* Redir = Cast<UObjectRedirector>(RD.GetAsset()))
				Redirectors.Add(Redir);
		}
		if (!Redirectors.IsEmpty())
		{
			UE_LOG(LogShintTools, Log,
				TEXT("ShintPanel: fixing %d redirector(s) after asset rename"), Redirectors.Num());
			AssetTools.FixupReferencers(Redirectors);
		}
	}

	// T2 — Persist redirect mappings to DefaultEngine.ini. ObjectRedirector
	// .uasset files cover live references but are fragile (deleted by clean,
	// missed by native parent-class lookup on child Blueprints). The
	// [CoreRedirects] entries make the rename survive both.
	{
		const int32 Added = WriteShintCoreRedirects(RedirectEntries);
		if (Added > 0)
		{
			UE_LOG(LogShintTools, Log,
				TEXT("ShintPanel: wrote %d new entr(ies) to [CoreRedirects] in DefaultEngine.ini"),
				Added);
		}
	}

	// ASSET-FIX-2 — Recompile every descendant BP we collected before the rename.
	// Without this, child BPs still resolve their ParentClass through the old
	// in-memory pointer and report "Class not found" the next time they're
	// loaded by name (which the user perceives as "the bot broke my parents").
	if (!DescendantBPsToRecompile.IsEmpty())
	{
		int32 Recompiled = 0;
		for (UBlueprint* Child : DescendantBPsToRecompile)
		{
			if (!IsValid(Child)) continue;
			FKismetEditorUtilities::CompileBlueprint(Child);
			++Recompiled;
		}
		UE_LOG(LogShintTools, Log,
			TEXT("ShintPanel: recompiled %d descendant BP(s) after parent rename"),
			Recompiled);
	}

	// ASSET-FIX-2 — Save renamed packages + their referencers so the rename
	// (and the .uasset redirector left behind) survives an editor close. With
	// the redirectors un-saved, closing the editor without saving silently
	// reverts the rename and the user reports "broken references".
	{
		TArray<UPackage*> DirtyPackages;
		FEditorFileUtils::GetDirtyContentPackages(DirtyPackages);
		if (!DirtyPackages.IsEmpty())
		{
			const bool bSaved = FEditorFileUtils::PromptForCheckoutAndSave(
				DirtyPackages,
				/*bCheckDirty=*/true,
				/*bPromptToSave=*/false) == FEditorFileUtils::EPromptReturnCode::PR_Success;
			UE_LOG(LogShintTools, Log,
				TEXT("ShintPanel: auto-saved %d dirty package(s) post-rename (success=%d)"),
				DirtyPackages.Num(), bSaved ? 1 : 0);
		}
	}

	CoreClient->ReportAssetFixesToServer(ForServer,
		FOnShintAssetFixComplete::CreateSP(this, &SShintToolsPanel::OnAssetFixComplete));

	return FReply::Handled();
}

FReply SShintToolsPanel::OnSendAssetToDashboardClicked()
{
	CoreClient->SendAssetNamingToDashboard(LastAssetResult,
		FOnShintWebDashboardComplete::CreateSP(this, &SShintToolsPanel::OnAssetDashboardComplete));
	return FReply::Handled();
}

// ─────────────────────────────────────────────────────────────────────────────
// HTTP callbacks
// ─────────────────────────────────────────────────────────────────────────────

void SShintToolsPanel::OnHealthCheckComplete(const FShintRequestResult& Result)
{
	SetStatus(Result.bSuccess ? ECoreStatus::Online : ECoreStatus::Offline);
}

void SShintToolsPanel::OnProjectValidateComplete(const FShintValidateResult& Result)
{
	HandleValidateResult(Result, /*bMerge=*/false, /*bIsBPScan=*/false);
}

void SShintToolsPanel::OnBlueprintValidateComplete(const FShintValidateResult& Result)
{
	// Separate naming issues (BPB001) → route to Asset Naming panel
	FShintValidateResult QualityResult;
	QualityResult.bSuccess       = Result.bSuccess;
	QualityResult.FilesScanned   = Result.FilesScanned;
	QualityResult.Issues.Reserve(Result.Issues.Num());

	int32 NamingRouted = 0;

	for (const FShintCodeIssue& Issue : Result.Issues)
	{
		// BPB001 (naming) → Asset Naming panel ONLY when triggered by the legacy
		// naming-chain path (bBlueprintScanActive=false). When the user clicks
		// "Scan All BP", all issues stay in the Code Validator — no asset panel side-effect.
		if (!bBlueprintScanActive && Issue.RuleId == TEXT("BPB001"))
		{
			// Convert to asset naming item and add to backing store
			FShintAssetItemPtr Item = MakeShared<FShintAssetItem>();
			Item->AssetPath     = Issue.FilePath;
			Item->CurrentName   = FPaths::GetBaseFilename(Issue.FilePath);
			Item->SuggestedName = TEXT("BP_") + Item->CurrentName;
			Item->Reason        = Issue.Message;
			Item->AssetType     = TEXT("Blueprint");
			Item->bChecked      = true;
			Item->OriginalIndex = AllAssetItems.Num();
			AllAssetItems.Add(MoveTemp(Item));
			++NamingRouted;
		}
		else
		{
			QualityResult.Issues.Add(Issue);
			if (Issue.Severity == TEXT("error"))   ++QualityResult.TotalErrors;
			if (Issue.Severity == TEXT("warning")) ++QualityResult.TotalWarnings;
		}
	}
	QualityResult.TotalIssues = QualityResult.Issues.Num();

	// Refresh asset list view if naming items were added
	if (NamingRouted > 0)
	{
		ApplyAssetFilter();
		RefreshAssetStats();
	}

	// T4 — Always merge into the unified code-validator panel so the user
	// sees C++ AND Blueprint findings in the same list. The Code-Type filter
	// (CppOnly / BlueprintsOnly) and the Category filter let them slice the
	// view; they don't need a destructive REPLACE on every BP scan.
	// `bIsBPScan` is still passed so PopulateCodeIssueList tags rows correctly.
	HandleValidateResult(QualityResult, /*bMerge=*/true, /*bIsBPScan=*/bBlueprintScanActive);
}

void SShintToolsPanel::HandleValidateResult(const FShintValidateResult& Result, bool bMerge, bool bIsBPScan)
{
	if (!Result.bSuccess)
	{
		ShintShowErrorToast(TEXT("Code validation failed"), Result.ErrorMessage);
		SetCodeState(EModuleState::Error);
		return;
	}

	if (bMerge)
	{
		// T4 — merge-by-scan-type. A C++ scan replaces only C++ issues (RuleId
		// prefixed CP/CB/CS/CM); a BP scan replaces only BP issues (BP* prefix).
		// Re-running either scan does NOT duplicate findings, and the unified
		// list keeps both kinds visible at once.
		auto IsBP = [](const FShintCodeIssue& I) {
			return I.RuleId.StartsWith(TEXT("BP"));
		};
		LastCodeResult.Issues.RemoveAll([&](const FShintCodeIssue& I)
		{
			return bIsBPScan ? IsBP(I) : !IsBP(I);
		});
		LastCodeResult.Issues.Append(Result.Issues);

		// Recompute counters from the merged set so they always match the list.
		LastCodeResult.bSuccess      = true;
		LastCodeResult.FilesScanned  = Result.FilesScanned;  // last scan's coverage
		LastCodeResult.TotalIssues   = LastCodeResult.Issues.Num();
		LastCodeResult.TotalErrors   = 0;
		LastCodeResult.TotalWarnings = 0;
		for (const FShintCodeIssue& I : LastCodeResult.Issues)
		{
			if (I.Severity == TEXT("error"))   ++LastCodeResult.TotalErrors;
			if (I.Severity == TEXT("warning")) ++LastCodeResult.TotalWarnings;
		}
	}
	else
	{
		LastCodeResult = Result;
		// Bump generation so any in-flight async build check is discarded — its
		// BUILD001 errors belong to the previous set of files, not this fresh scan.
		++ScanGeneration;
	}

	SetCodeState(EModuleState::Done);
	PopulateCodeIssueList(LastCodeResult, bIsBPScan);
	RefreshCodeStats();

	// Slice B — show the overall score the server returned inline (no extra round-trip),
	// then fetch the full per-category breakdown via /metrics/score/latest.
	if (LastCodeResult.QualityScoreOverall >= 0.f)
	{
		LastQualityScore = FShintQualityScoreSnapshot();
		LastQualityScore.bValid       = true;
		LastQualityScore.OverallScore = LastCodeResult.QualityScoreOverall;
		LastQualityScore.Errors       = LastCodeResult.TotalErrors;
		LastQualityScore.Warnings     = LastCodeResult.TotalWarnings;
		LastQualityScore.TotalIssues  = LastCodeResult.TotalIssues;
		LastQualityScore.FilesScanned = LastCodeResult.FilesScanned;
		RefreshQualityScore();
	}

	if (CoreClient.IsValid())
	{
		const FString& ProjectId = CoreClient->GetConfig().ProjectId;
		if (!ProjectId.IsEmpty())
		{
			CoreClient->GetLatestQualityScore(
				ProjectId,
				FOnShintQualityScoreComplete::CreateSP(this, &SShintToolsPanel::OnLatestScoreFetched));
		}
	}
}

void SShintToolsPanel::OnCodeFixComplete(const FShintFixResult& Result, uint32 FixGeneration)
{
	SetCodeState(EModuleState::Done);

	if (Result.bSuccess)
	{
		UE_LOG(LogShintTools, Log,
			TEXT("ApplyFix: %d fix(es) applied, %d skipped."),
			Result.TotalFixesApplied, Result.TotalFixesSkipped);

		// T3 — Surface a success toast so the user sees what just landed.
		// Without this, the panel only updated counters and the "click was
		// silent" perception drove repeat-clicks. Mention the kind of scan
		// (Blueprint vs C++) explicitly because BP fixes were the most
		// visually-quiet flow.
		{
			const TCHAR* Kind = bBlueprintScanActive ? TEXT("Blueprint") : TEXT("C++");
			FNotificationInfo Info(FText::FromString(
				FString::Printf(TEXT("✓  %d %s fix(es) applied"),
					Result.TotalFixesApplied, Kind)));
			if (Result.TotalFixesSkipped > 0)
			{
				Info.SubText = FText::FromString(
					FString::Printf(TEXT("%d skipped — open the log for details."),
						Result.TotalFixesSkipped));
			}
			Info.ExpireDuration       = 5.0f;
			Info.bUseSuccessFailIcons = true;
			TSharedPtr<SNotificationItem> N =
				FSlateNotificationManager::Get().AddNotification(Info);
			if (N.IsValid())
			{
				N->SetCompletionState(Result.TotalFixesApplied > 0
					? SNotificationItem::CS_Success
					: SNotificationItem::CS_None);
			}
		}

		// Record fingerprints so these issues are suppressed on any future
		// incremental re-scan within this session.
		for (const FShintCodeIssue& I : PendingCodeFixes)
		{
			AppliedFixFingerprints.Add(
				FString::Printf(TEXT("%s:%d:%s"), *I.FilePath, I.Line, *I.RuleId));
		}
		PendingCodeFixes.Empty();

		// Clear visible list FIRST so Slate never touches stale pointers
		CodeIssueItems.Reset();
		if (CodeIssueListView.IsValid()) CodeIssueListView->RebuildList();

		// Now safe to remove from backing data
		AllCodeItems.RemoveAll([](const FShintIssueItemPtr& I) {
			return I->bChecked && I->bIsAutoFixable
				&& (!I->FixSuggestion.IsEmpty() || I->bIsBlueprint);
		});

		// ── Inject compile errors from the incremental build check ──────────────
		// Only inject if no new scan has been triggered since the fix was applied.
		// A changed ScanGeneration means the user already launched a fresh scan
		// that wiped AllCodeItems — stale BUILD001 items must not be re-added.
		if (Result.bHasCompileErrors && Result.CompileErrors.Num() > 0
			&& FixGeneration == ScanGeneration)
		{
			UE_LOG(LogShintTools, Warning,
				TEXT("OnCodeFixComplete: %d compile error(s) injected into panel"),
				Result.CompileErrors.Num());

			for (const FShintCompileError& CE : Result.CompileErrors)
			{
				FShintIssueItemPtr Item = MakeShared<FShintIssueItem>();
				Item->RuleId         = TEXT("BUILD001");
				// Compile errors are always critical — they block compilation.
				// "critical" maps to the Critical filter chip in the panel.
				Item->Severity       = TEXT("critical");
				Item->Message        = CE.Code.IsEmpty()
					? CE.Message
					: FString::Printf(TEXT("[%s] %s"), *CE.Code, *CE.Message);
				Item->FilePath       = CE.FilePath;
				Item->FileName       = CE.FileName;
				Item->Line           = CE.Line;
				Item->Snippet        = TEXT("");
				Item->FixSuggestion  = TEXT("");
				Item->bIsAutoFixable = false;
				Item->bChecked       = false;
				Item->Category       = TEXT("Build");
				Item->OriginalIndex  = AllCodeItems.Num();
				AllCodeItems.Add(MoveTemp(Item));
			}
		}
		else if (Result.bHasCompileErrors && FixGeneration != ScanGeneration)
		{
			UE_LOG(LogShintTools, Log,
				TEXT("OnCodeFixComplete: build errors discarded — scan generation changed (fix=%u current=%u)"),
				FixGeneration, ScanGeneration);
		}

		// Re-populate visible list with updated data
		ApplyCodeFilter();
		RefreshCodeStats();

		// If all items are now gone, show the "all resolved" state immediately
		if (AllCodeItems.IsEmpty())
		{
			if (CodeEmptyText.IsValid())
				CodeEmptyText->SetText(LOCTEXT("CVAllFixed",
					"✓  All issues resolved — click 'Scan' again to do a full re-scan."));
			if (CodeEmptyState.IsValid())
				CodeEmptyState->SetVisibility(EVisibility::Visible);
		}
	}
	else
	{
		UE_LOG(LogShintTools, Error, TEXT("ApplyFix failed: %s"), *Result.ErrorMessage);
	}

	if (CodeIssueListView.IsValid()) CodeIssueListView->RebuildList();
	RefreshApplyCodeLabel();
}

void SShintToolsPanel::OnCodeDashboardComplete(const FShintWebDashboardResult& Result)
{
	if (!SendCodeBtnLabel.IsValid()) return;

	if (Result.bSuccess)
	{
		SendCodeBtnLabel->SetText(LOCTEXT("SendCodeOk", "✓  Sent!"));
		SendCodeBtnLabel->SetColorAndOpacity(FSlateColor(C_Green()));
	}
	else
	{
		SendCodeBtnLabel->SetText(LOCTEXT("SendCodeErr", "✗  Send failed"));
		SendCodeBtnLabel->SetColorAndOpacity(FSlateColor(C_Red()));
		UE_LOG(LogShintTools, Error, TEXT("Dashboard send failed: %s"), *Result.ErrorMessage);
	}

	TWeakPtr<SShintToolsPanel> weak_this = SharedThis(this);
	FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateLambda([weak_this](float) -> bool {
			if (TSharedPtr<SShintToolsPanel> Pin = weak_this.Pin())
			{
				if (Pin->SendCodeBtnLabel.IsValid())
				{
					Pin->SendCodeBtnLabel->SetText(LOCTEXT("SendCodeRst", "↑  Send to Dashboard"));
					Pin->SendCodeBtnLabel->SetColorAndOpacity(FSlateColor(C_Blue()));
				}
			}
			return false;
		}), 3.f);
}

void SShintToolsPanel::OnAssetScanComplete(const FShintAssetScanResult& Result)
{
	if (!Result.bSuccess)
	{
		ShintShowErrorToast(TEXT("Asset scan failed"), Result.ErrorMessage);
		SetAssetState(EModuleState::Error);
		return;
	}
	LastAssetResult = Result;
	PopulateAssetIssueList(Result);
	RefreshAssetStats();

	// Chain a BP validation pass to pick up BPB001 (naming violations).
	// Results go ONLY to the asset naming panel — code validator is not touched.
	CoreClient->ValidateBlueprints(FPaths::ProjectContentDir(),
		FOnShintValidateComplete::CreateSP(this, &SShintToolsPanel::OnBlueprintNamingScanComplete));
}

void SShintToolsPanel::OnAssetScanFromBPComplete(const FShintAssetScanResult& Result)
{
	// Asset scan triggered by "Scan Blueprints":
	//   • populate the asset list from full scan results
	//   • auto-set filter to Blueprints so only BP naming violations are visible
	//   • do NOT chain another ValidateBlueprints call — BP code scan is already running
	if (!Result.bSuccess)
	{
		ShintShowErrorToast(TEXT("Blueprint asset scan failed"), Result.ErrorMessage);
		SetAssetState(EModuleState::Error);
		return;
	}
	LastAssetResult = Result;
	PopulateAssetIssueList(Result);

	CurrentAssetTypeFilter = EAssetTypeFilter::Blueprints;
	ApplyAssetFilter();
	RefreshAssetStats();
}

void SShintToolsPanel::OnBlueprintNamingScanComplete(const FShintValidateResult& Result)
{
	// Extract only BPB001 (wrong/missing BP_ prefix) and add to asset panel.
	// Every other BP issue is silently discarded — code validator stays untouched.
	int32 NamingRouted = 0;
	for (const FShintCodeIssue& Issue : Result.Issues)
	{
		if (Issue.RuleId != TEXT("BPB001")) continue;

		FShintAssetItemPtr Item = MakeShared<FShintAssetItem>();
		Item->AssetPath     = Issue.FilePath;
		Item->CurrentName   = FPaths::GetBaseFilename(Issue.FilePath);
		Item->SuggestedName = TEXT("BP_") + Item->CurrentName;
		Item->Reason        = Issue.Message;
		Item->AssetType     = TEXT("Blueprint");
		Item->bChecked      = true;
		Item->OriginalIndex = AllAssetItems.Num();
		AllAssetItems.Add(MoveTemp(Item));
		++NamingRouted;
	}

	SetAssetState(EModuleState::Done);

	if (NamingRouted > 0)
	{
		ApplyAssetFilter();
		RefreshAssetStats();
	}
}

void SShintToolsPanel::OnAssetFixComplete(const FShintAssetFixResult& Result)
{
	SetAssetState(EModuleState::Done);

	if (Result.bSuccess)
	{
		UE_LOG(LogShintTools, Log, TEXT("AssetFix: %d asset(s) renamed."), Result.AssetsRenamed);

		// Clear both backing store and visible list
		AllAssetItems.Reset();
		AssetIssueItems.Reset();
		if (AssetIssueListView.IsValid()) AssetIssueListView->RebuildList();
		RefreshAssetStats();
		RefreshApplyAssetLabel();

		// Track that fixes were applied; show "all resolved" message
		++AssetFixesApplied;
		if (AssetEmptyText.IsValid())
			AssetEmptyText->SetText(LOCTEXT("ANBAllFixed",
				"✓  All violations resolved — click 'Scan' again to do a full re-scan."));
		if (AssetEmptyState.IsValid())
			AssetEmptyState->SetVisibility(EVisibility::Visible);
	}
	else
	{
		UE_LOG(LogShintTools, Error, TEXT("AssetFix failed: %s"), *Result.ErrorMessage);
	}
}

void SShintToolsPanel::OnAssetDashboardComplete(const FShintWebDashboardResult& Result)
{
	if (!SendAssetBtnLabel.IsValid()) return;

	if (Result.bSuccess)
	{
		SendAssetBtnLabel->SetText(LOCTEXT("SendAssetOk", "✓  Sent!"));
		SendAssetBtnLabel->SetColorAndOpacity(FSlateColor(C_Green()));
	}
	else
	{
		SendAssetBtnLabel->SetText(LOCTEXT("SendAssetErr", "✗  Send failed"));
		SendAssetBtnLabel->SetColorAndOpacity(FSlateColor(C_Red()));
		UE_LOG(LogShintTools, Error, TEXT("Dashboard send failed: %s"), *Result.ErrorMessage);
	}

	TWeakPtr<SShintToolsPanel> weak_this = SharedThis(this);
	FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateLambda([weak_this](float) -> bool {
			if (const TSharedPtr<SShintToolsPanel> pin = weak_this.Pin())
			{
				if (pin->SendAssetBtnLabel.IsValid())
				{
					pin->SendAssetBtnLabel->SetText(LOCTEXT("SendAssetRst", "↑  Send to Dashboard"));
					pin->SendAssetBtnLabel->SetColorAndOpacity(FSlateColor(C_Blue()));
				}
			}
			return false;
		}), 3.f);
}

// ─────────────────────────────────────────────────────────────────────────────
// Populate + refresh
// ─────────────────────────────────────────────────────────────────────────────

void SShintToolsPanel::PopulateCodeIssueList(const FShintValidateResult& Result, bool bIsBPScan)
{
	(void)bIsBPScan;   // classification is now derived from FilePath (see BUG-001)
	const double PopStart = FPlatformTime::Seconds();
	// Clear visible list FIRST so Slate never references stale items during a paint tick
	CodeIssueItems.Reset();
	if (CodeIssueListView.IsValid()) CodeIssueListView->RequestListRefresh();

	AllCodeItems.Reset();
	AllCodeItems.Reserve(Result.Issues.Num());

	for (int32 i = 0; i < Result.Issues.Num(); ++i)
	{
		const FShintCodeIssue& Src = Result.Issues[i];

		// Skip issues that were already fixed this session
		const FString Fingerprint = FString::Printf(
			TEXT("%s:%d:%s"), *Src.FilePath, Src.Line, *Src.RuleId);
		if (AppliedFixFingerprints.Contains(Fingerprint))
			continue;

		FShintIssueItemPtr Item = MakeShared<FShintIssueItem>();
		Item->RuleId         = Src.RuleId;
		Item->Severity       = Src.Severity;
		Item->Message        = Src.Message;
		Item->FilePath       = Src.FilePath;
		Item->FileName       = FPaths::GetCleanFilename(Src.FilePath);
		Item->Line           = Src.Line;
		Item->Snippet        = Src.Snippet;
		Item->FixSuggestion  = Src.FixSuggestion;
		Item->bIsAutoFixable = Src.bIsAutoFixable;
		Item->bChecked       = Src.bIsAutoFixable;
		// Classify each issue. The previous implementation looked only at
		// FilePath.StartsWith("/Game/") which broke whenever the server
		// normalised C++ paths in odd ways (mixed slashes, trimmed roots),
		// causing the "C++ Only" filter to silently skip every C++ issue
		// while "Blueprints Only" worked. Use the rule-id prefix as the
		// primary signal — BP* rules can only be produced by Blueprint
		// scanning, C{P,B,S,M}* rules can only be produced by C++ scanning
		// — and fall back to the path heuristic only when RuleId is empty.
		const FString& Rid = Src.RuleId;
		// BUILD* (compile errors) belong to C++ — keep them out of the BP set
		// so fix-routing logic that branches on bIsBlueprint stays correct.
		const bool bRidIsBP = Rid.StartsWith(TEXT("BP"));
		const bool bRidIsCpp =
			Rid.StartsWith(TEXT("CP")) || Rid.StartsWith(TEXT("CB"))
		 || Rid.StartsWith(TEXT("CS")) || Rid.StartsWith(TEXT("CM"));
		if (bRidIsBP)        Item->bIsBlueprint = true;
		else if (bRidIsCpp)  Item->bIsBlueprint = false;
		else                 Item->bIsBlueprint = Src.FilePath.StartsWith(TEXT("/Game/"));
		Item->OriginalIndex  = i;
		Item->Class            = Src.Class;
		Item->Category         = Src.Category;
		Item->Graph            = Src.Graph;
		Item->FileContent      = Src.FileContent;
		Item->ContextBefore    = Src.ContextBefore;
		Item->ContextAfter     = Src.ContextAfter;
		Item->ContextLineStart = Src.ContextLineStart;
		AllCodeItems.Add(MoveTemp(Item));
	}

	ApplyCodeFilter();

	const bool bCodeEmpty = AllCodeItems.IsEmpty();
	if (CodeEmptyState.IsValid())
		CodeEmptyState->SetVisibility(bCodeEmpty ? EVisibility::Visible : EVisibility::Collapsed);
	if (bCodeEmpty && CodeEmptyText.IsValid())
		CodeEmptyText->SetText(LOCTEXT("CVNoIssues", "✓  No issues found in your project."));

	if (ApplyCodeBtn.IsValid()) ApplyCodeBtn->SetEnabled(!AllCodeItems.IsEmpty());
	if (SendCodeBtn.IsValid())  SendCodeBtn->SetEnabled(true);
	RefreshApplyCodeLabel();

	UE_LOG(LogShintTools, Log,
		TEXT("[BENCH] PopulateCodeIssueList: %.3f s, %d items (filtered from %d)"),
		FPlatformTime::Seconds() - PopStart, AllCodeItems.Num(), Result.Issues.Num());
}

void SShintToolsPanel::ApplyCodeFilter()
{
	CodeIssueItems.Reset();
	CodeIssueItems.Reserve(AllCodeItems.Num());

	for (const FShintIssueItemPtr& Item : AllCodeItems)
	{
		// Compile errors always show regardless of active filters — they are critical
		const bool bIsBuildError = (Item->RuleId == TEXT("BUILD001"));

		// ── Code type filter (C++ vs Blueprint) ──────────────────────────────
		if (!bIsBuildError && CurrentCodeTypeFilter != ECodeTypeFilter::All)
		{
			if (CurrentCodeTypeFilter == ECodeTypeFilter::CppOnly        &&  Item->bIsBlueprint) continue;
			if (CurrentCodeTypeFilter == ECodeTypeFilter::BlueprintsOnly && !Item->bIsBlueprint) continue;
		}

		// ── Fixable filter ────────────────────────────────────────────────────
		if (!bIsBuildError && CurrentFilter == EIssueFilter::FixableOnly && !Item->bIsAutoFixable)
			continue;

		// ── Severity filter ───────────────────────────────────────────────────
		if (!bIsBuildError && CurrentSeverityFilter != EIssueSeverityFilter::All)
		{
			const FString SevLower = Item->Severity.ToLower();
			bool bSevMatch = false;
			switch (CurrentSeverityFilter)
			{
			case EIssueSeverityFilter::Critical: bSevMatch = (SevLower == TEXT("critical")); break;
			case EIssueSeverityFilter::Error:    bSevMatch = (SevLower == TEXT("error"));    break;
			case EIssueSeverityFilter::Warning:  bSevMatch = (SevLower == TEXT("warning"));  break;
			case EIssueSeverityFilter::Info:     bSevMatch = (SevLower == TEXT("info"));     break;
			default: bSevMatch = true; break;
			}
			if (!bSevMatch) continue;
		}

		// ── Category filter ───────────────────────────────────────────────────
		// T1 — Fall back to the RuleId prefix when the server didn't populate
		// `category` (some BP and validator rules return it empty). Without
		// this the filter dropped every uncategorised issue, so selecting
		// e.g. "Performance" silently emptied the panel.
		if (!bIsBuildError && CurrentCategoryFilter != EIssueCategoryFilter::All)
		{
			const FString  CatLower = Item->Category.ToLower();
			const FString& Rid      = Item->RuleId;
			bool bCatMatch = false;
			switch (CurrentCategoryFilter)
			{
			case EIssueCategoryFilter::Performance:
				bCatMatch = CatLower.Contains(TEXT("performance"))
				         || Rid.StartsWith(TEXT("CP")) || Rid.StartsWith(TEXT("BPP"));
				break;
			case EIssueCategoryFilter::BestPractices:
				bCatMatch = CatLower.Contains(TEXT("best")) || CatLower.Contains(TEXT("practice"))
				         || Rid.StartsWith(TEXT("CB")) || Rid.StartsWith(TEXT("BPB"));
				break;
			case EIssueCategoryFilter::Security:
				bCatMatch = CatLower.Contains(TEXT("security"))
				         || Rid.StartsWith(TEXT("CS")) || Rid.StartsWith(TEXT("BPS"));
				break;
			case EIssueCategoryFilter::Maintainability:
				bCatMatch = CatLower.Contains(TEXT("maintain"))
				         || Rid.StartsWith(TEXT("CM")) || Rid.StartsWith(TEXT("BPM"));
				break;
			default: bCatMatch = true; break;
			}
			if (!bCatMatch) continue;
		}

		CodeIssueItems.Add(Item);
	}

	if (CodeIssueListView.IsValid()) CodeIssueListView->RequestListRefresh();
}

void SShintToolsPanel::ApplyAssetFilter()
{
	AssetIssueItems.Reset();
	AssetIssueItems.Reserve(AllAssetItems.Num());

	for (const FShintAssetItemPtr& Item : AllAssetItems)
	{
		if (CurrentAssetTypeFilter != EAssetTypeFilter::All)
		{
			const FString TypeLower = Item->AssetType.ToLower();
			bool bTypeMatch = false;
			switch (CurrentAssetTypeFilter)
			{
			case EAssetTypeFilter::Materials:  bTypeMatch = TypeLower.Contains(TEXT("material"));  break;
			case EAssetTypeFilter::Textures:   bTypeMatch = TypeLower.Contains(TEXT("texture"));   break;
			case EAssetTypeFilter::Meshes:     bTypeMatch = TypeLower.Contains(TEXT("mesh"));      break;
			case EAssetTypeFilter::Blueprints: bTypeMatch = TypeLower.Contains(TEXT("blueprint")); break;
			case EAssetTypeFilter::VFX:        bTypeMatch = TypeLower.Contains(TEXT("niagara")) || TypeLower.Contains(TEXT("particle")); break;
			case EAssetTypeFilter::Audio:      bTypeMatch = TypeLower.Contains(TEXT("sound")) || TypeLower.Contains(TEXT("audio")); break;
			case EAssetTypeFilter::Animations: bTypeMatch = TypeLower.Contains(TEXT("anim"));      break;
			case EAssetTypeFilter::Data:       bTypeMatch = TypeLower.Contains(TEXT("data")) || TypeLower.Contains(TEXT("table")) || TypeLower.Contains(TEXT("curve")); break;
			default: bTypeMatch = true; break;
			}
			if (!bTypeMatch) continue;
		}
		AssetIssueItems.Add(Item);
	}

	if (AssetIssueListView.IsValid()) AssetIssueListView->RequestListRefresh();

	const bool bHasItems = !AssetIssueItems.IsEmpty();
	if (AssetEmptyState.IsValid())
		AssetEmptyState->SetVisibility(bHasItems ? EVisibility::Collapsed : EVisibility::Visible);
	if (ApplyAssetBtn.IsValid()) ApplyAssetBtn->SetEnabled(bHasItems);
	if (SendAssetBtn.IsValid())  SendAssetBtn->SetEnabled(!AllAssetItems.IsEmpty());
	RefreshApplyAssetLabel();
}

void SShintToolsPanel::PopulateAssetIssueList(const FShintAssetScanResult& Result)
{
	const double PopStart = FPlatformTime::Seconds();
	AllAssetItems.Reset();
	AllAssetItems.Reserve(Result.Issues.Num());

	// Free-tier display cap. Server caps the *scan* at 500 asset records but
	// each asset can fire several rules, so issue rows can outnumber assets.
	// Cap by unique AssetPath so AssetTotal_Label honours the 500-asset
	// promise without dropping the second / third issue on the same asset.
	// Only apply when the server reports tier="free"; Indie returns the full
	// set untouched.
	constexpr int32 MaxUniqueAssets = 500;
	const bool bApplyFreeCap = (Result.Tier == TEXT("free"));
	TSet<FString> SeenAssetPaths;
	if (bApplyFreeCap) SeenAssetPaths.Reserve(MaxUniqueAssets);

	for (int32 i = 0; i < Result.Issues.Num(); ++i)
	{
		const FShintAssetIssue& Src = Result.Issues[i];
		if (bApplyFreeCap && !SeenAssetPaths.Contains(Src.AssetPath))
		{
			if (SeenAssetPaths.Num() >= MaxUniqueAssets) continue;
			SeenAssetPaths.Add(Src.AssetPath);
		}

		FShintAssetItemPtr Item = MakeShared<FShintAssetItem>();
		Item->AssetPath    = Src.AssetPath;
		Item->CurrentName  = Src.CurrentName;
		Item->SuggestedName= Src.SuggestedName;
		Item->Reason       = Src.Reason;
		Item->AssetType    = Src.AssetType;
		Item->bChecked     = true;
		Item->OriginalIndex= i;
		AllAssetItems.Add(MoveTemp(Item));
	}

	ApplyAssetFilter();
	if (SendAssetBtn.IsValid()) SendAssetBtn->SetEnabled(true);

	// If the scan returned no violations, show a clear success message
	if (AllAssetItems.IsEmpty() && AssetEmptyText.IsValid())
		AssetEmptyText->SetText(LOCTEXT("ANBNoIssues", "✓  No naming violations found."));

	UE_LOG(LogShintTools, Log,
		TEXT("[BENCH] PopulateAssetIssueList: %.3f s, %d items"),
		FPlatformTime::Seconds() - PopStart, AllAssetItems.Num());
}

void SShintToolsPanel::RefreshCodeStats()
{
	if (CodeFiles_Label.IsValid())    CodeFiles_Label->SetText(FText::FromString(FmtN(LastCodeResult.FilesScanned)));
	if (CodeErrors_Label.IsValid())   CodeErrors_Label->SetText(FText::FromString(FmtN(LastCodeResult.TotalErrors)));
	if (CodeWarnings_Label.IsValid()) CodeWarnings_Label->SetText(FText::FromString(FmtN(LastCodeResult.TotalWarnings)));
}

// ─── Slice B — Quality Score helpers ───────────────────────────────────────
//
// Color thresholds match the dashboard convention:
//   ≥ 90 → green   (healthy)
//   ≥ 70 → yellow  (needs attention)
//   < 70 → red     (poor quality)
namespace
{
	FLinearColor ScoreColor(float Score)
	{
		if (Score >= 90.f) return SShintToolsPanel::C_Green();
		if (Score >= 70.f) return SShintToolsPanel::C_Yellow();
		return SShintToolsPanel::C_Red();
	}
}

void SShintToolsPanel::RefreshQualityScore()
{
	if (CodeScore_Label.IsValid())
	{
		if (LastQualityScore.bValid)
		{
			CodeScore_Label->SetText(FText::FromString(
				FString::Printf(TEXT("%.0f"), LastQualityScore.OverallScore)));
			CodeScore_Label->SetColorAndOpacity(
				FSlateColor(ScoreColor(LastQualityScore.OverallScore)));
		}
		else
		{
			CodeScore_Label->SetText(FText::FromString(TEXT("—")));
			CodeScore_Label->SetColorAndOpacity(FSlateColor(C_Gray()));
		}
	}

	if (CodeScoreBreakdown_Label.IsValid())
	{
		if (LastQualityScore.bValid)
		{
			// Compact one-liner. Server-side category names match these labels.
			const FString Line = FString::Printf(
				TEXT("Perf %.0f  ·  Sec %.0f  ·  BP %.0f  ·  Maint %.0f  ·  Naming %.0f"),
				LastQualityScore.PerformanceScore,
				LastQualityScore.SecurityScore,
				LastQualityScore.BestPracticesScore,
				LastQualityScore.MaintainabilityScore,
				LastQualityScore.NamingScore);
			CodeScoreBreakdown_Label->SetText(FText::FromString(Line));
			CodeScoreBreakdown_Label->SetColorAndOpacity(
				FSlateColor(ScoreColor(LastQualityScore.OverallScore)));
		}
		else
		{
			CodeScoreBreakdown_Label->SetText(LOCTEXT("CVQBreakdownEmpty",
				"Quality Score: run a scan to compute"));
			CodeScoreBreakdown_Label->SetColorAndOpacity(FSlateColor(C_Gray()));
		}
	}
}

void SShintToolsPanel::OnLatestScoreFetched(const FShintQualityScoreSnapshot& Snap)
{
	if (!Snap.bValid)
	{
		// 404 / older free server / empty project — keep the inline overall we already
		// painted from the scan response, just log for diagnostics.
		UE_LOG(LogShintTools, Verbose,
			TEXT("Slice B: /metrics/score/latest returned no score (%s)"),
			Snap.ErrorMessage.IsEmpty() ? TEXT("not found") : *Snap.ErrorMessage);
		return;
	}

	LastQualityScore = Snap;
	RefreshQualityScore();
}

void SShintToolsPanel::RefreshAssetStats()
{
	// Drive counters from the backing store so stats reflect total, not the filtered view.
	// AssetTotal: count of UNIQUE assets (one per asset_path), not issue rows
	// — multiple issues on the same asset must not inflate the headline number
	// and break the free-tier 500-asset promise. AssetInvalid: total flagged
	// rows, kept as-is so the user can see "37 issues across 19 assets".
	TSet<FString> Unique;
	Unique.Reserve(AllAssetItems.Num());
	for (const FShintAssetItemPtr& It : AllAssetItems)
		if (It.IsValid()) Unique.Add(It->AssetPath);
	const int32 UniqueAssets = Unique.Num();
	const int32 IssueRows    = AllAssetItems.Num();
	if (AssetTotal_Label.IsValid())   AssetTotal_Label->SetText(FText::FromString(FmtN(UniqueAssets)));
	if (AssetInvalid_Label.IsValid()) AssetInvalid_Label->SetText(FText::FromString(FmtN(IssueRows)));
	if (AssetTime_Label.IsValid())    AssetTime_Label->SetText(FText::FromString(
		FString::Printf(TEXT("%.2f"), LastAssetResult.ScanTimeSeconds)));
}

void SShintToolsPanel::RefreshApplyCodeLabel()
{
	const int32 N = Algo::CountIf(AllCodeItems,
		[](const FShintIssueItemPtr& P){ return P->bChecked; });
	if (ApplyCodeBtnLabel.IsValid())
		ApplyCodeBtnLabel->SetText(FText::FromString(
			FString::Printf(TEXT("✓  Apply Selected (%d)"), N)));
	if (ApplyCodeBtn.IsValid()) ApplyCodeBtn->SetEnabled(N > 0);
}

void SShintToolsPanel::RefreshApplyAssetLabel()
{
	const int32 N = Algo::CountIf(AssetIssueItems,
		[](const FShintAssetItemPtr& P){ return P->bChecked; });
	if (ApplyAssetBtnLabel.IsValid())
		ApplyAssetBtnLabel->SetText(FText::FromString(
			FString::Printf(TEXT("✓  Apply Corrections (%d)"), N)));
	if (ApplyAssetBtn.IsValid()) ApplyAssetBtn->SetEnabled(N > 0);
}

// ─────────────────────────────────────────────────────────────────────────────
// State setters + attribute getters
// ─────────────────────────────────────────────────────────────────────────────

void SShintToolsPanel::SetStatus(ECoreStatus S)
{ StatusState = S; Invalidate(EInvalidateWidget::Paint); }

void SShintToolsPanel::SetCodeState(EModuleState S)
{ CodeState = S; Invalidate(EInvalidateWidget::Paint); }

void SShintToolsPanel::SetAssetState(EModuleState S)
{ AssetState = S; Invalidate(EInvalidateWidget::Paint); }

FSlateColor SShintToolsPanel::GetStatusColor() const
{
	switch (StatusState)
	{
	case ECoreStatus::Online:   return FSlateColor(C_Green());
	case ECoreStatus::Offline:  return FSlateColor(C_Red());
	case ECoreStatus::Checking: return FSlateColor(C_Yellow());
	default:                    return FSlateColor(C_DimGray());
	}
}

FText SShintToolsPanel::GetStatusText() const
{
	switch (StatusState)
	{
	case ECoreStatus::Online:   return LOCTEXT("On",  "Online");
	case ECoreStatus::Offline:  return LOCTEXT("Off", "Offline");
	case ECoreStatus::Checking: return LOCTEXT("Chk", "Checking…");
	default:                    return LOCTEXT("Unk", "Not checked");
	}
}

TOptional<float> SShintToolsPanel::GetCodeProgress() const
{
	if (CodeState == EModuleState::Running) return TOptional<float>();
	if (CodeState == EModuleState::Done)    return TOptional<float>(1.f);
	return TOptional<float>(0.f);
}

TOptional<float> SShintToolsPanel::GetAssetProgress() const
{
	if (AssetState == EModuleState::Running) return TOptional<float>();
	if (AssetState == EModuleState::Done)    return TOptional<float>(1.f);
	return TOptional<float>(0.f);
}

// ─────────────────────────────────────────────────────────────────────────────
// Agent — Auto-Fix Plan
// ─────────────────────────────────────────────────────────────────────────────

FReply SShintToolsPanel::OnAutoFixPlanClicked()
{
	if (LastCodeResult.Issues.IsEmpty())
	{
		ShintShowErrorToast(
			TEXT("Auto-Fix Plan"),
			TEXT("Run a code scan first — the planner needs issues to prioritize."));
		return FReply::Handled();
	}

	CoreClient->RequestAgentPlan(
		LastCodeResult,
		FOnShintAgentPlanComplete::CreateSP(this, &SShintToolsPanel::OnAgentPlanComplete));
	return FReply::Handled();
}

void SShintToolsPanel::OnAgentPlanComplete(const FShintAgentPlanResult& Result)
{
	if (!Result.bSuccess)
	{
		// Free-tier servers answer 403; surface as a friendly upgrade hint
		// rather than as a generic HTTP error.
		const bool bForbidden = Result.ErrorMessage.Contains(TEXT("Indie-tier"))
			|| Result.ErrorMessage.Contains(TEXT("403"));
		const FString Title = bForbidden
			? TEXT("Auto-Fix Plan — Indie only")
			: TEXT("Auto-Fix Plan failed");
		ShintShowErrorToast(*Title, *Result.ErrorMessage);
		return;
	}
	ShowAgentPlanDialog(Result);
}

void SShintToolsPanel::ShowAgentPlanDialog(const FShintAgentPlanResult& Result)
{
	TSharedRef<SVerticalBox> Body = SNew(SVerticalBox);

	Body->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)
	[
		SNew(STextBlock)
		.Text(FText::FromString(FString::Printf(
			TEXT("%d pasos · %s"), Result.Steps.Num(), *Result.Summary)))
		.Font(F_Label())
		.ColorAndOpacity(FSlateColor(C_DimGray()))
	];

	for (const FShintAgentPlanStep& S : Result.Steps)
	{
		FLinearColor PriorityColor = FLinearColor::Gray;
		if      (S.Priority == TEXT("critical")) PriorityColor = FLinearColor(0.93f, 0.27f, 0.27f);
		else if (S.Priority == TEXT("high"))     PriorityColor = FLinearColor(0.97f, 0.45f, 0.09f);
		else if (S.Priority == TEXT("medium"))   PriorityColor = FLinearColor(0.86f, 0.78f, 0.16f);
		else                                     PriorityColor = FLinearColor(0.42f, 0.65f, 0.42f);

		Body->AddSlot().AutoHeight().Padding(0.f, 4.f)
		[
			SNew(SBorder).BorderImage(ST4::Solid(C_Surface())).Padding(8.f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 8.f, 0.f)
					[
						SNew(STextBlock)
						.Text(FText::FromString(FString::Printf(
							TEXT("%d. [%s] %s"), S.Order, *S.Priority.ToUpper(), *S.RuleId)))
						.Font(F_Label())
						.ColorAndOpacity(FSlateColor(PriorityColor))
					]
					+ SHorizontalBox::Slot().FillWidth(1.f)
					[
						SNew(STextBlock)
						.Text(FText::FromString(FString::Printf(
							TEXT("%s:%d"),
							*FPaths::GetCleanFilename(S.FilePath), S.Line)))
						.Font(F_Small())
						.ColorAndOpacity(FSlateColor(C_DimGray()))
					]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 4.f, 0.f, 0.f)
				[
					SNew(STextBlock)
					.Text(FText::FromString(S.Rationale))
					.Font(F_Small())
					.ColorAndOpacity(FSlateColor(C_White()))
					.AutoWrapText(true)
				]
			]
		];
	}

	TSharedRef<SWindow> Win = SNew(SWindow)
		.Title(LOCTEXT("AgentPlanWin", "ShintTools — Auto-Fix Plan"))
		.ClientSize(FVector2D(720.f, 520.f))
		.SizingRule(ESizingRule::UserSized);

	Win->SetContent(
		SNew(SBorder).BorderImage(ST4::Solid(C_BG())).Padding(16.f)
		[
			SNew(SScrollBox)
			+ SScrollBox::Slot()
			[
				Body
			]
		]
	);

	FSlateApplication::Get().AddWindow(Win, true);
}

// ─────────────────────────────────────────────────────────────────────────────
// UI-REDESIGN step 8 — Overview hero
//
// Replaces the legacy BuildHeader + BuildStatusBar combo with a 4-up KPI grid
// reading from LastQualityScore. Values are bound via Value_Lambda so they
// refresh automatically as scans complete (RefreshQualityScore mutates
// LastQualityScore; Slate re-reads on the next paint).
//
// Action row beneath the KPIs offers a primary CTA to scan the project, so
// the empty Overview is still actionable for first-time users.
// ─────────────────────────────────────────────────────────────────────────────
TSharedRef<SWidget> SShintToolsPanel::BuildOverviewHero()
{
	auto Snap = [this]() -> const FShintQualityScoreSnapshot& { return LastQualityScore; };

	auto QualityValue = [Snap]() -> FText
	{
		const FShintQualityScoreSnapshot& S = Snap();
		return S.bValid
			? FText::FromString(FString::Printf(TEXT("%.0f"), S.OverallScore))
			: FText::FromString(TEXT("—"));
	};

	auto QualityColor = [Snap]() -> FSlateColor
	{
		const FShintQualityScoreSnapshot& S = Snap();
		if (!S.bValid)             return FSlateColor(FShintStyle::Colors::TextMuted());
		if (S.OverallScore >= 80)  return FSlateColor(FShintStyle::Colors::SevLow());      // green
		if (S.OverallScore >= 50)  return FSlateColor(FShintStyle::Colors::SevHigh());     // orange
		return                              FSlateColor(FShintStyle::Colors::SevCritical());// red
	};

	auto IntText = [](int32 N) -> FText { return FText::AsNumber(N); };

	auto IssuesValue = [Snap, IntText]() -> FText { return IntText(Snap().TotalIssues); };
	auto ErrorsValue = [Snap, IntText]() -> FText { return IntText(Snap().Errors); };
	auto FilesValue  = [Snap, IntText]() -> FText { return IntText(Snap().FilesScanned); };

	// 4-up tile grid — inlined (avoids TAttribute<FSlateColor>::Create gymnastics).
	const FMargin GapL  = FMargin(0.f, 0.f, FShintStyle::Space::S2 * 0.5f, 0.f);
	const FMargin GapM  = FMargin(FShintStyle::Space::S2 * 0.5f, 0.f, FShintStyle::Space::S2 * 0.5f, 0.f);
	const FMargin GapR  = FMargin(FShintStyle::Space::S2 * 0.5f, 0.f, 0.f, 0.f);

	TSharedRef<SHorizontalBox> Grid = SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.f).Padding(GapL)
		[
			SNew(SShintCard).bShowHeader(false).ContentPadding(FShintStyle::Space::S4)
			[
				SNew(SShintKpiTile)
				.Caption(NSLOCTEXT("OverviewHero","KQ","QUALITY SCORE"))
				.Value_Lambda(QualityValue)
				.ValueColor_Lambda(QualityColor)
			]
		]
		+ SHorizontalBox::Slot().FillWidth(1.f).Padding(GapM)
		[
			SNew(SShintCard).bShowHeader(false).ContentPadding(FShintStyle::Space::S4)
			[
				SNew(SShintKpiTile)
				.Caption(NSLOCTEXT("OverviewHero","KI","TOTAL ISSUES"))
				.Value_Lambda(IssuesValue)
			]
		]
		+ SHorizontalBox::Slot().FillWidth(1.f).Padding(GapM)
		[
			SNew(SShintCard).bShowHeader(false).ContentPadding(FShintStyle::Space::S4)
			[
				SNew(SShintKpiTile)
				.Caption(NSLOCTEXT("OverviewHero","KE","ERRORS"))
				.Value_Lambda(ErrorsValue)
				.ValueColor(FSlateColor(FShintStyle::Colors::SevCritical()))
			]
		]
		+ SHorizontalBox::Slot().FillWidth(1.f).Padding(GapR)
		[
			SNew(SShintCard).bShowHeader(false).ContentPadding(FShintStyle::Space::S4)
			[
				SNew(SShintKpiTile)
				.Caption(NSLOCTEXT("OverviewHero","KF","FILES SCANNED"))
				.Value_Lambda(FilesValue)
				.ValueColor(FSlateColor(FShintStyle::Colors::TextMuted()))
			]
		];

	return SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(FMargin(0.f, 0.f, 0.f, FShintStyle::Space::S5))
		[
			BuildSectionTitle(
				NSLOCTEXT("OverviewHero","Title", "PROJECT OVERVIEW"),
				NSLOCTEXT("OverviewHero","Sub",   "Quality Score and issue counters · last scan summary"))
		]

		// KPI grid
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(FMargin(0.f, 0.f, 0.f, FShintStyle::Space::S5))
		[ Grid ]

		// Empty-state CTA shown when no scan has run yet
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SShintCard)
			.bShowHeader(false)
			.ContentPadding(FShintStyle::Space::S5)
			[
				SNew(SShintEmptyState)
				.Headline(NSLOCTEXT("OverviewHero","Hero", "Run your first scan"))
				.Subtitle(NSLOCTEXT("OverviewHero","HeroSub",
					"Switch to the Code or Assets tab on the left to begin a scan. "
					"Scan results populate the tiles above."))
			]
		];
}

#undef LOCTEXT_NAMESPACE