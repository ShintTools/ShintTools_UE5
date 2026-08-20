// Copyright 2026 ShintTools. All Rights Reserved.

#include "ShintTools.h"
#include "SShintToolsPanel.h"
#include "SShintWelcomeDialog.h"
#include "ShintIconStyle.h"
#include "Assistant/SShintAssistantDock.h"
#include "Containers/Ticker.h"   // one-shot deferral in the assistant tab shim

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


	// AI Assistant — the assistant's own surface is now SShintAssistantDock.
	// What stays registered here is a MIGRATION SHIM, not the panel: an editor
	// layout saved while the old tab was docked still names this tab, and
	// Unreal restores it on every startup, which is why the retired tab kept
	// reappearing. The shim's tab closes itself and opens the dock instead, so
	// the stale layout entry is consumed once and the reference is gone from
	// the next layout save. Simply unregistering the spawner would have left
	// that entry in the layout indefinitely.
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
		// Tools, not Window. Window is where Unreal keeps its own panels, and a
		// plugin's entry point sitting among them reads as part of the editor
		// rather than as something the team installed. Tools is where the
		// editor already groups everything that ACTS on the project, which is
		// what every ShintTools surface does.
		UToolMenu* ToolsMenu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Tools");
		if (!ToolsMenu)
		{
			UE_LOG(LogShintTools, Warning, TEXT("Could not find LevelEditor.MainMenu.Tools to extend."));
			return;
		}

		// A LABELLED section. The previous one had no label, so it rendered as
		// a bare separator and the plugin's name never appeared in the menu at
		// all — the same reason Unreal's own "GET CONTENT" and "LAYOUT" headers
		// exist. The label is the second argument; omitting it is what made the
		// section invisible.
		FToolMenuSection& Section = ToolsMenu->FindOrAddSection(
			"ShintToolsSection", LOCTEXT("ShintToolsSectionLabel", "ShintTools"));

		// Two levels, not three: the control panel is the entry point and sits
		// directly under the header, while the standalone surfaces go in one
		// flyout. Nesting the panel too would put the thing people open most
		// behind an extra hop.
		Section.AddMenuEntry(
			"OpenShintToolsPanel",
			LOCTEXT("OpenShintToolsPanelLabel", "ShintTools"),
			LOCTEXT("OpenShintToolsPanelTooltip", "Open the ShintTools automation control panel"),
			FSlateIcon(FShintIconStyle::GetStyleSetName(), "ShintTools.Icons.UI"),
			FUIAction(FExecuteAction::CreateRaw(this, &FShintToolsModule::OpenShintToolsPanel))
		);

		// "Modules" and not "Tools" — a submenu named after the menu that
		// contains it reads as a mistake.
		Section.AddSubMenu(
			"ShintToolsModulesSubMenu",
			LOCTEXT("ShintToolsModulesLabel", "Modules"),
			LOCTEXT("ShintToolsModulesTooltip", "Predictive Profiler and the AI Assistant"),
			FNewToolMenuChoice(FNewToolMenuDelegate::CreateLambda(
				[this](UToolMenu* SubMenu)
			{
				FToolMenuSection& Windows =
					SubMenu->FindOrAddSection("ShintToolsModules");


				Windows.AddMenuEntry(
					"OpenShintAssistantPanel",
					LOCTEXT("OpenShintAssistantLabel", "AI Assistant"),
					LOCTEXT("OpenShintAssistantTooltip",
						"Ask about your scans — runs entirely on this machine"),
					FSlateIcon(FShintIconStyle::GetStyleSetName(), "ShintTools.Icons.Bot"),
					FUIAction(FExecuteAction::CreateRaw(this, &FShintToolsModule::OpenShintAssistantPanel))
				);
			})),
			/*bInOpenSubMenuOnClick=*/false,
			FSlateIcon(FShintIconStyle::GetStyleSetName(), "ShintTools.Icons.Grid")
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


void FShintToolsModule::OpenShintAssistantPanel()
{
	// Toggle, not open: the menu entry is the same affordance as the launcher
	// itself, and a second click on either should put the assistant away.
	SShintAssistantDock::Toggle();
}

TSharedRef<SDockTab> FShintToolsModule::SpawnShintAssistantTab(const FSpawnTabArgs& SpawnTabArgs)
{
	// Migration shim (see RegisterTabSpawner). This tab exists only to absorb a
	// restore from a layout saved before the dock, and it must not build the
	// panel: two live assistant panels would each hold their own conversation.
	TSharedRef<SDockTab> Tab = SNew(SDockTab).TabRole(ETabRole::NomadTab);

	// Deferred by one tick on purpose. Closing a tab from inside its own spawn
	// callback tears it down while the tab manager is still wiring it into the
	// layout it was restored from.
	TWeakPtr<SDockTab> WeakTab = Tab;
	FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateLambda([WeakTab](float) -> bool
		{
			if (const TSharedPtr<SDockTab> Pinned = WeakTab.Pin())
				Pinned->RequestCloseTab();
			SShintAssistantDock::Open();
			return false;
		}), 0.f);

	return Tab;
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FShintToolsModule, ShintTools)
