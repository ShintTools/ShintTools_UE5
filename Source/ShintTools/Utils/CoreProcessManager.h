// Copyright ShintTools. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HAL/PlatformProcess.h"

/**
 * ECoreStartMode
 * Controls which backend is launched when the Core Engine is not running.
 */
enum class ECoreStartMode : uint8
{
	/** Launch the Core Engine through Docker. */
	Docker,
};

/**
 * FCoreProcessManager
 *
 * Manages the lifecycle of the ShintTools Core Engine process.
 * Wraps FPlatformProcess to launch, monitor, and stop the Docker backend.
 *
 * Design contract:
 *   - One Core Engine process per plugin session.
 *   - The plugin never kills the Core Engine on shutdown by default
 *     (the server may be shared across multiple editors).
 *   - Call StopCoreEngine() explicitly if you want to terminate it.
 *
 * Thread safety: All methods must be called from the Game Thread.
 */
class SHINTTOOLS_API FCoreProcessManager
{
public:

	FCoreProcessManager();
	~FCoreProcessManager();

	// ── Lifecycle ─────────────────────────────────────────────────────────────

	/**
	 * Attempts to launch the Core Engine using the specified mode.
	 *
	 * @param Mode         - Whether to launch via Docker
	 * @param OutPID       - Receives the PID of the launched process on success
	 * @return True if the process was launched successfully
	 */
	bool StartCoreEngine(
		ECoreStartMode Mode,
		uint32& OutPID);

	/**
	 * Terminates the managed Core Engine process.
	 * No-op if no process has been launched by this instance.
	 */
	void StopCoreEngine();

	// ── Status ────────────────────────────────────────────────────────────────

	/** Returns true if a Core Engine process was launched and is still alive */
	bool IsCoreRunning();

	/** Returns the PID of the managed process, or 0 if none */
	uint32 GetCorePID() const { return ManagedPID; }

	// /**
	//  * Resolves the path to the Core Engine main.py relative to the plugin or project.
	//  * Searches:
	//  *   1. <PluginDir>/CoreEngine/main.py
	//  *   2. <ProjectDir>/CoreEngine/main.py
	//  *   3. Fallback: empty string (caller should warn the user)
	//  */
	// static FString ResolveCoreScriptPath();

private:

	/** Handle to the launched process (invalid if not running) */
	FProcHandle ProcessHandle;

	/** PID of the last process launched by this manager */
	uint32 ManagedPID = 0;

	/** Launches via: docker run shinttools-core */
	bool LaunchDocker(uint32& OutPID);
};
