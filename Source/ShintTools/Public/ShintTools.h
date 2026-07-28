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

	// [LOD-STRIP-BEGIN]
	/** Name of the Predictive Profiler dockable tab (Studio-only, independent). */
	static const FName ShintPredictiveTabName;
	// [LOD-STRIP-END]

	// Cached license status — populated at module startup by an async
	static FString GetCachedTier();

	/**
	 * Re-resolve the cached tier from the current shinttools.config.json via
	 * an async POST /license/status. Called once at StartupModule and again
	 * whenever the user saves a new license key in the Config panel.
	 */
	static void RefreshTierAsync();

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

	// [LOD-STRIP-BEGIN]
	/** Opens / brings-to-front the Predictive Profiler window. */
	void OpenShintPredictiveDashboard();

	/** Spawns the Predictive Profiler dockable tab widget. */
	TSharedRef<SDockTab> SpawnShintPredictiveTab(const FSpawnTabArgs& SpawnTabArgs);
	// [LOD-STRIP-END]

	/** Creates the menu entry under Window > Developer Tools */
	void BuildShintToolsMenu(FMenuBuilder& MenuBuilder);

	/** Menu extender handle - kept so we can remove on shutdown */
	TSharedPtr<FExtender> MenuExtender;
};
