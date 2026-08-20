// Copyright 2026 ShintTools. All Rights Reserved.

#include "ShintTools.h"
#include "SShintToolsPanel.h"
#include "SShintWelcomeDialog.h"
#include "ShintIconStyle.h"
#include "Assistant/SShintAssistantDock.h"
#include "Containers/Ticker.h"

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
#include "Core/ShintCoreClient.h"

#if SHINT_MARKETPLACE_BUILD
#include "Marketplace/SShintCoreInstallerWindow.h"
#include "Marketplace/SShintLauncherWelcomeDialog.h"
#endif

DEFINE_LOG_CATEGORY(LogShintTools);

#define LOCTEXT_NAMESPACE "FShintToolsModule"

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
	FShintCoreClient Tmp;
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

void FShintToolsModule::StartupModule()
{

	FShintIconStyle::Initialize();

	RegisterTabSpawner();
	ExtendLevelEditorMenu();

#if SHINT_MARKETPLACE_BUILD

	SShintLauncherWelcomeDialog::MaybeShow();

	SShintCoreInstallerWindow::OpenIfNeededAsync(18200);
#else

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

	SShintAssistantDock::Shutdown();
	FShintIconStyle::Shutdown();
	UE_LOG(LogShintTools, Verbose, TEXT("ShintTools: Module shut down."));
}

void FShintToolsModule::RegisterTabSpawner()
{

	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
		ShintToolsTabName,
		FOnSpawnTab::CreateRaw(this, &FShintToolsModule::SpawnShintToolsTab))
		.SetDisplayName(LOCTEXT("ShintToolsTabTitle", "ShintTools"))
		.SetTooltipText(LOCTEXT("ShintToolsTabTooltip", "Open the ShintTools Control Panel"))
		.SetGroup(WorkspaceMenu::GetMenuStructure().GetDeveloperToolsMiscCategory())
		.SetIcon(FSlateIcon(FShintIconStyle::GetStyleSetName(), "ShintTools.Icons.UI"));

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

void FShintToolsModule::ExtendLevelEditorMenu()
{

	UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateLambda([this]()
	{

		UToolMenu* ToolsMenu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Tools");
		if (!ToolsMenu)
		{
			UE_LOG(LogShintTools, Warning, TEXT("Could not find LevelEditor.MainMenu.Tools to extend."));
			return;
		}

		FToolMenuSection& Section = ToolsMenu->FindOrAddSection(
			"ShintToolsSection", LOCTEXT("ShintToolsSectionLabel", "ShintTools"));

		Section.AddMenuEntry(
			"OpenShintToolsPanel",
			LOCTEXT("OpenShintToolsPanelLabel", "ShintTools"),
			LOCTEXT("OpenShintToolsPanelTooltip", "Open the ShintTools automation control panel"),
			FSlateIcon(FShintIconStyle::GetStyleSetName(), "ShintTools.Icons.UI"),
			FUIAction(FExecuteAction::CreateRaw(this, &FShintToolsModule::OpenShintToolsPanel))
		);

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
			false,
			FSlateIcon(FShintIconStyle::GetStyleSetName(), "ShintTools.Icons.Grid")
		);
	}));
}

void FShintToolsModule::RemoveLevelEditorMenuExtension()
{
	UToolMenus::UnRegisterStartupCallback(this);
	UToolMenus::UnregisterOwner(this);
}

void FShintToolsModule::OpenShintToolsPanel()
{
	FGlobalTabmanager::Get()->TryInvokeTab(ShintToolsTabName);
}

TSharedRef<SDockTab> FShintToolsModule::SpawnShintToolsTab(const FSpawnTabArgs& SpawnTabArgs)
{

	return SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		[

			SNew(SShintToolsPanel)
		];
}

void FShintToolsModule::OpenShintAssistantPanel()
{

	SShintAssistantDock::Toggle();
}

TSharedRef<SDockTab> FShintToolsModule::SpawnShintAssistantTab(const FSpawnTabArgs& SpawnTabArgs)
{

	TSharedRef<SDockTab> Tab = SNew(SDockTab).TabRole(ETabRole::NomadTab);

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
