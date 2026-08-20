// Copyright 2026 ShintTools. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "Framework/Docking/TabManager.h"

class FToolBarBuilder;
class FMenuBuilder;
class SDockTab;
class FSpawnTabArgs;

DECLARE_LOG_CATEGORY_EXTERN(LogShintTools, Log, All);

class FShintToolsModule : public IModuleInterface
{
public:

	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	static FShintToolsModule& Get()
	{
		return FModuleManager::LoadModuleChecked<FShintToolsModule>("ShintTools");
	}

	static bool IsAvailable()
	{
		return FModuleManager::Get().IsModuleLoaded("ShintTools");
	}

	static const FName ShintToolsTabName;

	static const FName ShintAssistantTabName;

	static FString GetCachedTier();

	static void RefreshTierAsync();

	DECLARE_MULTICAST_DELEGATE(FOnShintLicenseResolved);
	static FOnShintLicenseResolved OnLicenseResolved;

private:

	void RegisterTabSpawner();

	void UnregisterTabSpawner();

	void ExtendLevelEditorMenu();

	void RemoveLevelEditorMenuExtension();

	void OpenShintToolsPanel();

	TSharedRef<SDockTab> SpawnShintToolsTab(const FSpawnTabArgs& SpawnTabArgs);

	void OpenShintAssistantPanel();

	TSharedRef<SDockTab> SpawnShintAssistantTab(const FSpawnTabArgs& SpawnTabArgs);

	void BuildShintToolsMenu(FMenuBuilder& MenuBuilder);

	TSharedPtr<FExtender> MenuExtender;
};
