// Copyright ShintTools. All Rights Reserved.
//
// Settings destination — the "PROJECT CONFIG" card with the dashboard API
// key + dashboard URL fields, plus SaveConfigOverrides() that flushes the
// edited values back to shinttools.config.json.
//
// Split out because the section is independent of every other panel
// destination: it does not consume scan state, it does not produce HTTP
// requests, and it only mutates CoreClient's config struct.

#include "SShintToolsPanel.h"
#include "SShintToolsPanel_Private.h"
#include "ShintCoreClient.h"

#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SEditableTextBox.h"

#define LOCTEXT_NAMESPACE "SShintToolsPanel"

TSharedRef<SWidget> SShintToolsPanel::BuildConfigSection()
{
	const FShintCoreConfig& Cfg = CoreClient->GetConfig();

	auto ConfigRow = [this](const FText& Label, TSharedPtr<SEditableTextBox>& OutField,
		const FString& InitialValue, const FText& Hint) -> TSharedRef<SWidget>
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 10.f, 0.f)
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
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)
			[
				SNew(STextBlock).Text(LOCTEXT("CfgTitle", "PROJECT CONFIG"))
				.Font(F_Label()).ColorAndOpacity(FSlateColor(C_DimGray()))
			]
			// Project ID, API Key Mongo and API Key Dashboard fields have been
			// removed from the panel in v1.3 — they are managed by the
			// Launcher (it writes them into shinttools.config.json after the
			// user signs in). Showing them here let curious users edit values
			// they shouldn't touch, and inviting an Indie user to paste their
			// license_key into a panel is a confusing flow now that the
			// Launcher handles activation automatically.
			//
			// The struct fields (Cfg.ProjectId / Cfg.ApiKeyMongo /
			// Cfg.ApiKeyDashboard) still exist and are loaded from JSON — the
			// runtime contract is unchanged.
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)
			[
				ConfigRow(LOCTEXT("CfgApiKey", "Dashboard API Key"),
					ApiKeyDashboardField,
					Cfg.ApiKeyDashboard, LOCTEXT("CfgApiKeyHint",
						"st_… key from shint.tools/dashboard → Projects "
						"→ + New project (per-project Bearer credential)"))
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				ConfigRow(LOCTEXT("CfgDashUrl", "Dashboard URL"), DashboardUrlField,
					Cfg.DashboardUrl, LOCTEXT("CfgDashUrlHint", "https://shint.tools"))
			]
		];
}

void SShintToolsPanel::SaveConfigOverrides()
{
	if (!CoreClient.IsValid()) return;

	FShintCoreConfig& Cfg = CoreClient->GetConfigMutable();

	// Dashboard API Key + Dashboard URL are user-editable. ApiKeyMongo is
	// launcher-managed (license sync on sign-in writes it).
	if (ApiKeyDashboardField.IsValid())
		Cfg.ApiKeyDashboard = ApiKeyDashboardField->GetText().ToString();
	if (DashboardUrlField.IsValid())
		Cfg.DashboardUrl = DashboardUrlField->GetText().ToString();

	CoreClient->SaveConfig();
}

#undef LOCTEXT_NAMESPACE
