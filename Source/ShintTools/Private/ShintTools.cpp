// Copyright 2026 ShintTools. All Rights Reserved.

#include "ShintTools.h"
#include "SShintToolsPanel.h"
#include "SShintWelcomeDialog.h"
#include "ShintIconStyle.h"

#include "Framework/Docking/TabManager.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "LevelEditor.h"
#include "ToolMenus.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"
#include "Widgets/Docking/SDockTab.h"
#include "Styling/AppStyle.h"

#include "Api/LicenseApi.h"
#include "Transport/FShintHttpClient.h"
#include "Core/ShintCoreClient.h"  // for LoadConfig() — reuses the existing
                                   // shinttools.config.json parser to source
                                   // base_url + api_key without duplicating
                                   // the JSON-parsing logic.

#if SHINT_MARKETPLACE_BUILD
#include "Marketplace/SShintCoreInstallerWindow.h"
#endif

// Define the log category for the entire plugin
DEFINE_LOG_CATEGORY(LogShintTools);

#define LOCTEXT_NAMESPACE "FShintToolsModule"

// Static tab name identifier
const FName FShintToolsModule::ShintToolsTabName = FName("ShintTools");

static FString GCachedTier = TEXT("free");
FShintToolsModule::FOnShintLicenseResolved
    FShintToolsModule::OnLicenseResolved;

FString FShintToolsModule::GetCachedTier()
{
	return GCachedTier;
}

void FShintToolsModule::RefreshTierAsync()
{
	FShintCoreClient Tmp;  // reads shinttools.config.json to get base_url
	const FShintCoreConfig& Cfg = Tmp.GetConfig();
	const FString BaseUrl = Cfg.GetBaseUrl();
	const FString ApiKey  = Cfg.ApiKeyMongo;

	const TSharedRef<FShintHttpClient> Transport =
		MakeShared<FShintHttpClient>(TEXT("license-probe"));
	TSharedRef<FShintLicenseApi> Api =
		MakeShared<FShintLicenseApi>(Transport, BaseUrl);
	Api->RequestStatus(ApiKey, FOnShintLicenseStatusComplete::CreateLambda(
		[Api](const FShintLicenseStatus& Status)
		{
			GCachedTier = Status.bSuccess && !Status.Tier.IsEmpty()
				? Status.Tier
				: TEXT("free");
			UE_LOG(LogShintTools, Verbose,
			       TEXT("ShintTools: license probe -> tier=%s (took %.3fs)"),
			       *GCachedTier, Status.ElapsedSeconds);
			OnLicenseResolved.Broadcast();
		}));
}

// ─────────────────────────────────────────────────────────────────────────────
// IModuleInterface
// ─────────────────────────────────────────────────────────────────────────────

void FShintToolsModule::StartupModule()
{
	// Register our SVG icon library first so the tab spawner + panel render
	// ShintTools' own glyphs instead of the native editor (Starship) icons.
	FShintIconStyle::Initialize();

	RegisterTabSpawner();
	ExtendLevelEditorMenu();
	
	RefreshTierAsync();

#if SHINT_MARKETPLACE_BUILD
	// Marketplace builds own Core install. Probe the local Core on a
	// worker thread; if it isn't healthy, open the install wizard.
	SShintCoreInstallerWindow::OpenIfNeededAsync(18200);
#endif

	UE_LOG(LogShintTools, Verbose, TEXT("ShintTools: Module started."));
}

void FShintToolsModule::ShutdownModule()
{
	RemoveLevelEditorMenuExtension();
	UnregisterTabSpawner();
	FShintIconStyle::Shutdown();
	UE_LOG(LogShintTools, Verbose, TEXT("ShintTools: Module shut down."));
}

// ─────────────────────────────────────────────────────────────────────────────
// Tab Spawner Registration
// ─────────────────────────────────────────────────────────────────────────────

void FShintToolsModule::RegisterTabSpawner()
{
	// Register our tab inside the "Developer Tools" workspace group so it
	// appears correctly in the Window menu hierarchy.
	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
		ShintToolsTabName,
		FOnSpawnTab::CreateRaw(this, &FShintToolsModule::SpawnShintToolsTab))
		.SetDisplayName(LOCTEXT("ShintToolsTabTitle", "ShintTools"))
		.SetTooltipText(LOCTEXT("ShintToolsTabTooltip", "Open the ShintTools Control Panel"))
		.SetGroup(WorkspaceMenu::GetMenuStructure().GetDeveloperToolsMiscCategory())
		.SetIcon(FSlateIcon(FShintIconStyle::GetStyleSetName(), "ShintTools.Icons.UI"));
}

void FShintToolsModule::UnregisterTabSpawner()
{
	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(ShintToolsTabName);
}

// ─────────────────────────────────────────────────────────────────────────────
// Menu Extension
// ─────────────────────────────────────────────────────────────────────────────

void FShintToolsModule::ExtendLevelEditorMenu()
{
	// Use ToolMenus API (UE5 preferred way)
	UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateLambda([this]()
	{
		// Extend the "Window" top-level menu
		UToolMenu* WindowMenu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Window");
		if (!WindowMenu)
		{
			UE_LOG(LogShintTools, Warning, TEXT("Could not find LevelEditor.MainMenu.Window to extend."));
			return;
		}

		// Add a new section "ShintTools" inside the Window menu
		FToolMenuSection& Section = WindowMenu->FindOrAddSection("ShintToolsSection");
		Section.Label = LOCTEXT("ShintToolsSectionLabel", "ShintTools");

		Section.AddMenuEntry(
			"OpenShintToolsPanel",
			LOCTEXT("OpenShintToolsPanelLabel", "ShintTools"),
			LOCTEXT("OpenShintToolsPanelTooltip", "Open the ShintTools automation control panel"),
			FSlateIcon(FShintIconStyle::GetStyleSetName(), "ShintTools.Icons.UI"),
			FUIAction(FExecuteAction::CreateRaw(this, &FShintToolsModule::OpenShintToolsPanel))
		);
	}));
}

void FShintToolsModule::RemoveLevelEditorMenuExtension()
{
	UToolMenus::UnRegisterStartupCallback(this);
	UToolMenus::UnregisterOwner(this);
}

// ─────────────────────────────────────────────────────────────────────────────
// Panel Management
// ─────────────────────────────────────────────────────────────────────────────

void FShintToolsModule::OpenShintToolsPanel()
{
	FGlobalTabmanager::Get()->TryInvokeTab(ShintToolsTabName);
}

TSharedRef<SDockTab> FShintToolsModule::SpawnShintToolsTab(const FSpawnTabArgs& SpawnTabArgs)
{
	SShintWelcomeDialog::MaybeShowForTier(GetCachedTier());

	return SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		[
			// The entire panel widget lives here
			SNew(SShintToolsPanel)
		];
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FShintToolsModule, ShintTools)
