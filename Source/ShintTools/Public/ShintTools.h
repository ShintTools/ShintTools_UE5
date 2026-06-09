// Copyright 2026 ShintTools. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "Framework/Docking/TabManager.h"

// Forward declarations
class FToolBarBuilder;
class FMenuBuilder;
class SDockTab;
class FSpawnTabArgs;

// Log category declaration - defined in ShintTools.cpp
DECLARE_LOG_CATEGORY_EXTERN(LogShintTools, Log, All);

/**
 * FShintToolsModule
 *
 * Main editor module for the ShintTools plugin.
 * Responsible for registering menus, tabs, and managing the plugin lifecycle.
 * Acts as a thin client - all business logic lives in the Core Engine.
 */
class FShintToolsModule : public IModuleInterface
{
public:

	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	/**
	 * Returns the singleton instance of this module.
	 * @return Reference to the module instance
	 */
	static FShintToolsModule& Get()
	{
		return FModuleManager::LoadModuleChecked<FShintToolsModule>("ShintTools");
	}

	/**
	 * Checks whether the module is loaded and available.
	 * @return True if module is loaded
	 */
	static bool IsAvailable()
	{
		return FModuleManager::Get().IsModuleLoaded("ShintTools");
	}

	/** Name of the ShintTools dockable tab */
	static const FName ShintToolsTabName;

	// Cached license status — populated at module startup by an async
	// POST /license/status. Read with GetCachedTier() so UI code does not
	// have to wait for the first scan to learn the customer's tier (the
	// previous behaviour). Defaults to "free" until the round-trip
	// resolves; widgets should refresh when ``OnLicenseResolved`` fires.
	static FString GetCachedTier();

	/** Multicast delegate fired once /license/status returns. */
	DECLARE_MULTICAST_DELEGATE(FOnShintLicenseResolved);
	static FOnShintLicenseResolved OnLicenseResolved;

private:

	/** Registers the ShintTools tab spawner with the global tab manager */
	void RegisterTabSpawner();

	/** Unregisters the ShintTools tab spawner */
	void UnregisterTabSpawner();

	/** Extends the Level Editor "Window > Developer Tools" menu */
	void ExtendLevelEditorMenu();

	/** Removes the Level Editor menu extension */
	void RemoveLevelEditorMenuExtension();

	/** Called to open/bring-to-front the ShintTools panel */
	void OpenShintToolsPanel();

	/** Spawns the ShintTools dockable tab widget */
	TSharedRef<SDockTab> SpawnShintToolsTab(const FSpawnTabArgs& SpawnTabArgs);

	/** Creates the menu entry under Window > Developer Tools */
	void BuildShintToolsMenu(FMenuBuilder& MenuBuilder);

	/** Menu extender handle - kept so we can remove on shutdown */
	TSharedPtr<FExtender> MenuExtender;
};
