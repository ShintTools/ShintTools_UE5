// Copyright 2026 ShintTools. All Rights Reserved.

#include "ShintTools.h"
#include "SShintToolsPanel.h"
#include "SShintWelcomeDialog.h"
#include "ShintIconStyle.h"
#include "Assistant/SShintAssistantPanel.h"
#include "Assistant/SShintAssistantDock.h"
// [LOD-STRIP-BEGIN]
#include "Predictive/SShintPredictiveDashboard.h"
// [LOD-STRIP-END]

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
#include "Marketplace/SShintLauncherWelcomeDialog.h"
#endif

// Define the log category for the entire plugin
DEFINE_LOG_CATEGORY(LogShintTools);

#define LOCTEXT_NAMESPACE "FShintToolsModule"

// Static tab name identifier
const FName FShintToolsModule::ShintToolsTabName = FName("ShintTools");
// [LOD-STRIP-BEGIN]
const FName FShintToolsModule::ShintPredictiveTabName = FName("ShintPredictive");
// [LOD-STRIP-END]
const FName FShintToolsModule::ShintAssistantTabName = FName("ShintAssistant");

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

#if SHINT_MARKETPLACE_BUILD
	// Fab build: the user has the plugin but no launcher. Show a one-time
	// welcome that funnels them to the dashboard to download the launcher
	// (full access + upgrades) INSTEAD of the generic tier welcome — the
	// launcher promo is the marketplace-appropriate welcome, and stacking two
	// would be noise. The Core install wizard still runs below so the free
	// tier keeps working standalone if they don't grab the launcher.
	SShintLauncherWelcomeDialog::MaybeShow();

	// Marketplace builds own Core install. Probe the local Core on a
	// worker thread; if it isn't healthy, open the install wizard.
	SShintCoreInstallerWindow::OpenIfNeededAsync(18200);
#else
	// Show the generic welcome ONCE, and only for the FREE tier. Paid users
	// install + onboard through the launcher, so an in-editor welcome popup is
	// redundant noise for them. Decide only AFTER the async license probe
	// resolves: at tab-spawn time GCachedTier is still the "free" default, so
	// firing on tab-open would pop the free welcome on a paid install (the
	// reported bug). MaybeShowForTier's once-per-machine guard keeps it to a
	// single appearance. Subscribe BEFORE kicking the probe so the first
	// resolve can't slip through.
	OnLicenseResolved.AddLambda([]()
	{
		const FString Tier = GetCachedTier();
		const bool bIsFree =
			Tier.IsEmpty() || Tier.Equals(TEXT("free"), ESearchCase::IgnoreCase);
		if (bIsFree)
		{
			SShintWelcomeDialog::MaybeShowForTier(Tier);
		}
	});
#endif

	RefreshTierAsync();

	UE_LOG(LogShintTools, Verbose, TEXT("ShintTools: Module started."));
}

void FShintToolsModule::ShutdownModule()
{
	RemoveLevelEditorMenuExtension();
	UnregisterTabSpawner();
	// The dock lives in its own windows, outside the tab manager — nothing
	// else tears them down, and a window that outlives the module leaves the
	// editor painting a dangling widget.
	SShintAssistantDock::Shutdown();
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

	// [LOD-STRIP-BEGIN]
	// Predictive Profiler — a second, independent nomad tab (its own window,
	// not a section of the main panel). Registered unconditionally like the
	// main tab; the dashboard re-checks the Studio tier before scanning.
	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
		ShintPredictiveTabName,
		FOnSpawnTab::CreateRaw(this, &FShintToolsModule::SpawnShintPredictiveTab))
		.SetDisplayName(LOCTEXT("ShintPredictiveTabTitle", "Predictive Profiler"))
		.SetTooltipText(LOCTEXT("ShintPredictiveTabTooltip", "Predict CPU/GPU/memory/build cost before you play"))
		.SetGroup(WorkspaceMenu::GetMenuStructure().GetDeveloperToolsMiscCategory())
		.SetIcon(FSlateIcon(FShintIconStyle::GetStyleSetName(), "ShintTools.Icons.Profiler"));
	// [LOD-STRIP-END]

	// AI Assistant — the assistant's own surface is now SShintAssistantDock,
	// anchored to the editor's bottom-right corner instead of taking a slice
	// of the layout (see that header for why). The spawner stays registered
	// and HIDDEN: an editor layout saved while the old tab was docked still
	// names this tab on restore, and an unregistered spawner turns that into a
	// missing-tab warning on every startup. Hidden keeps the restore path
	// working without offering a second, competing way in.
	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
		ShintAssistantTabName,
		FOnSpawnTab::CreateRaw(this, &FShintToolsModule::SpawnShintAssistantTab))
		.SetDisplayName(LOCTEXT("ShintAssistantTabTitle", "AI Assistant"))
		.SetTooltipText(LOCTEXT("ShintAssistantTabTooltip",
			"Ask about your scans — runs entirely on this machine"))
		.SetMenuType(ETabSpawnerMenuType::Hidden)
		.SetIcon(FSlateIcon(FShintIconStyle::GetStyleSetName(), "ShintTools.Icons.UI"));
}

void FShintToolsModule::UnregisterTabSpawner()
{
	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(ShintToolsTabName);
	// [LOD-STRIP-BEGIN]
	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(ShintPredictiveTabName);
	// [LOD-STRIP-END]
	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(ShintAssistantTabName);
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

		// One "ShintTools" flyout in the Window menu, with every window the
		// plugin owns inside it. Previously these were flat entries under a
		// section header, which spread three unrelated-looking items across
		// the Window menu as the plugin grew — a submenu keeps the plugin's
		// footprint to a single row no matter how many windows it adds.
		FToolMenuSection& Section = WindowMenu->FindOrAddSection("ShintToolsSection");

		Section.AddSubMenu(
			"ShintToolsSubMenu",
			LOCTEXT("ShintToolsSubMenuLabel", "ShintTools"),
			LOCTEXT("ShintToolsSubMenuTooltip", "ShintTools windows"),
			FNewToolMenuChoice(FNewToolMenuDelegate::CreateLambda(
				[this](UToolMenu* SubMenu)
			{
				FToolMenuSection& Windows =
					SubMenu->FindOrAddSection("ShintToolsWindows");

				Windows.AddMenuEntry(
					"OpenShintToolsPanel",
					LOCTEXT("OpenShintToolsPanelLabel", "ShintTools"),
					LOCTEXT("OpenShintToolsPanelTooltip", "Open the ShintTools automation control panel"),
					FSlateIcon(FShintIconStyle::GetStyleSetName(), "ShintTools.Icons.UI"),
					FUIAction(FExecuteAction::CreateRaw(this, &FShintToolsModule::OpenShintToolsPanel))
				);

				// [LOD-STRIP-BEGIN]
				Windows.AddMenuEntry(
					"OpenShintPredictiveDashboard",
					LOCTEXT("OpenShintPredictiveLabel", "Predictive Profiler"),
					LOCTEXT("OpenShintPredictiveTooltip", "Predict CPU/GPU/memory/build cost before you play"),
					FSlateIcon(FShintIconStyle::GetStyleSetName(), "ShintTools.Icons.Profiler"),
					FUIAction(FExecuteAction::CreateRaw(this, &FShintToolsModule::OpenShintPredictiveDashboard))
				);
				// [LOD-STRIP-END]

				Windows.AddMenuEntry(
					"OpenShintAssistantPanel",
					LOCTEXT("OpenShintAssistantLabel", "AI Assistant"),
					LOCTEXT("OpenShintAssistantTooltip",
						"Ask about your scans — runs entirely on this machine"),
					FSlateIcon(FShintIconStyle::GetStyleSetName(), "ShintTools.Icons.UI"),
					FUIAction(FExecuteAction::CreateRaw(this, &FShintToolsModule::OpenShintAssistantPanel))
				);
			})),
			/*bInOpenSubMenuOnClick=*/false,
			FSlateIcon(FShintIconStyle::GetStyleSetName(), "ShintTools.Icons.UI")
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
	// NOTE: the welcome is driven exclusively by the license-resolved callback
	// in StartupModule (free-tier only). Do NOT pop it here on tab-open — at
	// first spawn the tier is still the "free" default, which would show the
	// free welcome on a paid install. It would also double up with the Fab
	// launcher-welcome on marketplace builds.

	return SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		[
			// The entire panel widget lives here
			SNew(SShintToolsPanel)
		];
}

// [LOD-STRIP-BEGIN]
void FShintToolsModule::OpenShintPredictiveDashboard()
{
	FGlobalTabmanager::Get()->TryInvokeTab(ShintPredictiveTabName);
}

TSharedRef<SDockTab> FShintToolsModule::SpawnShintPredictiveTab(const FSpawnTabArgs& SpawnTabArgs)
{
	return SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		[
			SNew(SShintPredictiveDashboard)
		];
}
// [LOD-STRIP-END]

void FShintToolsModule::OpenShintAssistantPanel()
{
	// Toggle, not open: the menu entry is the same affordance as the launcher
	// itself, and a second click on either should put the assistant away.
	SShintAssistantDock::Toggle();
}

TSharedRef<SDockTab> FShintToolsModule::SpawnShintAssistantTab(const FSpawnTabArgs& SpawnTabArgs)
{
	return SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		[
			SNew(SShintAssistantPanel)
		];
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FShintToolsModule, ShintTools)
