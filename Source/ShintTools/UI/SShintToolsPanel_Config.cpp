// Copyright ShintTools. All Rights Reserved.
//
// Settings destination — the 5-field config card (Core Engine port, API
// Key, Dashboard API Key, Excluded Paths, Export Path) plus
// SaveConfigOverrides() that flushes every edited value back to
// shinttools.config.json.
//
// Layout mirrors the Unity Settings tab so users moving between engines
// see the same affordances in the same order. The Core Engine
// connection indicator lives in the TopBar (top-right corner — bridged
// from SetStatus via CurrentConnStateIndex); the Settings tab does NOT
// duplicate it.

#include "SShintToolsPanel.h"
#include "SShintToolsPanel_Private.h"
#include "ShintCoreClient.h"
#include "ShintStyle.h"

#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"

#define LOCTEXT_NAMESPACE "SShintToolsPanel"

TSharedRef<SWidget> SShintToolsPanel::BuildConfigSection()
{
	const FShintCoreConfig& Cfg = CoreClient->GetConfig();

	// One-row helper. Label column is fixed width so every field column
	// lines up vertically regardless of label length.
	auto ConfigRow = [this](const FText& Label, TSharedPtr<SEditableTextBox>& OutField,
		const FString& InitialValue, const FText& Hint) -> TSharedRef<SWidget>
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 12.f, 0.f)
			[
				SNew(SBox).WidthOverride(160.f)
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

	// Multi-line variant for Excluded Paths so users can paste one path per
	// line instead of struggling with comma/semicolon delimiters.
	auto MultiLineRow = [this](const FText& Label,
		TSharedPtr<SMultiLineEditableTextBox>& OutField,
		const FString& InitialValue, const FText& Hint) -> TSharedRef<SWidget>
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top).Padding(0.f, 4.f, 12.f, 0.f)
			[
				SNew(SBox).WidthOverride(160.f)
				[
					SNew(STextBlock).Text(Label).Font(F_Label())
					.ColorAndOpacity(FSlateColor(C_DimGray()))
				]
			]
			+ SHorizontalBox::Slot().FillWidth(1.f)
			[
				SNew(SBox).MinDesiredHeight(64.f)
				[
					SAssignNew(OutField, SMultiLineEditableTextBox)
					.Text(FText::FromString(InitialValue))
					.HintText(Hint)
					.Font(F_Mono())
					.AutoWrapText(true)
					.OnTextCommitted_Lambda([this](const FText&, ETextCommit::Type) { SaveConfigOverrides(); })
				]
			];
	};

	const FString CorePortStr = FString::Printf(TEXT("%d"), Cfg.CorePort);
	const FString ExcludedJoined = FString::Join(Cfg.ExcludedPaths, TEXT("\n"));

	return SNew(SBorder)
		.BorderImage(ST4::Outline(C_Surface(), C_Border()))
		.Padding(FMargin(20.f, 16.f))
		[
			SNew(SVerticalBox)

			// ── Header ───────────────────────────────────────────────────────
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 10.f)
			[
				SNew(STextBlock).Text(LOCTEXT("CfgTitle", "SETTINGS"))
				.Font(F_Label()).ColorAndOpacity(FSlateColor(C_DimGray()))
			]

			// ── Fields ───────────────────────────────────────────────────────
			//
			// Order mirrors the Unity Settings tab + the marketplace docs
			// table: Core Engine port → API Key → Dashboard API Key →
			// Excluded Paths → Export Path. The Launcher still owns initial
			// population of api_key_mongo (license sync on sign-in); the
			// field is exposed here so users can override or paste a key
			// manually when offline.
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)
			[
				ConfigRow(LOCTEXT("CfgCorePort", "Core Engine port"),
					CorePortField,
					CorePortStr,
					LOCTEXT("CfgCorePortHint", "Local port the Core Engine listens on. Default 18200."))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)
			[
				ConfigRow(LOCTEXT("CfgApiKeyMongo", "API Key"),
					ApiKeyMongoField,
					Cfg.ApiKeyMongo,
					LOCTEXT("CfgApiKeyMongoHint", "License key (api_key_mongo). Unlocks Indie features."))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)
			[
				ConfigRow(LOCTEXT("CfgApiKey", "Dashboard API Key"),
					ApiKeyDashboardField,
					Cfg.ApiKeyDashboard,
					LOCTEXT("CfgApiKeyHint",
						"st_… per-project Bearer for shint.tools uploads."))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)
			[
				MultiLineRow(LOCTEXT("CfgExcluded", "Excluded Paths"),
					ExcludedPathsField,
					ExcludedJoined,
					LOCTEXT("CfgExcludedHint", "One folder per line. Skipped during code scans."))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)
			[
				ConfigRow(LOCTEXT("CfgExportPath", "Export Path"),
					ExportPathField,
					Cfg.ExportPath,
					LOCTEXT("CfgExportPathHint", "Default folder for JSON exports."))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 6.f, 0.f, 0.f)
			[
				ConfigRow(LOCTEXT("CfgDashUrl", "Dashboard URL"), DashboardUrlField,
					Cfg.DashboardUrl,
					LOCTEXT("CfgDashUrlHint", "https://shint.tools"))
			]
		];
}

void SShintToolsPanel::SaveConfigOverrides()
{
	if (!CoreClient.IsValid()) return;

	FShintCoreConfig& Cfg = CoreClient->GetConfigMutable();

	if (CorePortField.IsValid())
	{
		const FString PortStr = CorePortField->GetText().ToString();
		const int32 Parsed = FCString::Atoi(*PortStr);
		// Atoi returns 0 on parse failure; treat 0 or negative as "ignore"
		// rather than corrupting the config with an unbindable port.
		if (Parsed > 0 && Parsed < 65536) Cfg.CorePort = Parsed;
	}
	if (ApiKeyMongoField.IsValid())
		Cfg.ApiKeyMongo = ApiKeyMongoField->GetText().ToString();
	if (ApiKeyDashboardField.IsValid())
		Cfg.ApiKeyDashboard = ApiKeyDashboardField->GetText().ToString();
	if (DashboardUrlField.IsValid())
		Cfg.DashboardUrl = DashboardUrlField->GetText().ToString();
	if (ExportPathField.IsValid())
		Cfg.ExportPath = ExportPathField->GetText().ToString();
	if (ExcludedPathsField.IsValid())
	{
		const FString Raw = ExcludedPathsField->GetText().ToString();
		Cfg.ExcludedPaths.Reset();
		Raw.ParseIntoArray(Cfg.ExcludedPaths, TEXT("\n"), /*CullEmpty=*/true);
		// Trim each entry — users often paste with trailing spaces.
		for (FString& Entry : Cfg.ExcludedPaths)
			Entry.TrimStartAndEndInline();
	}

	CoreClient->SaveConfig();
}

#undef LOCTEXT_NAMESPACE
