// Copyright ShintTools. All Rights Reserved.

#include "CoreProcessManager.h"
#include "ShintTools/ShintTools.h"

#include "HAL/PlatformProcess.h"
#include "Misc/Paths.h"


// ─────────────────────────────────────────────────────────────────────────────
// Construction / Destruction
// ─────────────────────────────────────────────────────────────────────────────

FCoreProcessManager::FCoreProcessManager()
	: ManagedPID(0)
{
	UE_LOG(LogShintTools, Log, TEXT("CoreProcessManager: Initialized."));
}

FCoreProcessManager::~FCoreProcessManager()
{
	// Do NOT auto-kill the server - it may be intentionally kept alive
	UE_LOG(LogShintTools, Log, TEXT("CoreProcessManager: Destroyed. Core Engine process (if any) continues running."));
}

// ─────────────────────────────────────────────────────────────────────────────
// Lifecycle
// ─────────────────────────────────────────────────────────────────────────────

bool FCoreProcessManager::StartCoreEngine(
	ECoreStartMode Mode,
	uint32& OutPID)
{
	OutPID = 0;

	// Guard: don't launch twice
	if (IsCoreRunning())
	{
		UE_LOG(LogShintTools, Warning,
			TEXT("CoreProcessManager: Core Engine already running (PID=%u). Skipping launch."),
			ManagedPID);
		OutPID = ManagedPID;
		return true;
	}

	if (Mode != ECoreStartMode::Docker)
	{
		UE_LOG(LogShintTools, Warning,
			TEXT("CoreProcessManager: Docker-only mode is enabled. Ignoring requested mode and launching Docker."));
	}

	const bool bLaunched = LaunchDocker(OutPID);

	if (bLaunched)
	{
		ManagedPID = OutPID;
		UE_LOG(LogShintTools, Log,
			TEXT("CoreProcessManager: Core Engine launched successfully. PID=%u"), ManagedPID);
	}
	else
	{
		UE_LOG(LogShintTools, Error, TEXT("CoreProcessManager: Failed to launch Core Engine."));
	}

	return bLaunched;
}

void FCoreProcessManager::StopCoreEngine()
{
	if (!IsCoreRunning())
	{
		UE_LOG(LogShintTools, Log, TEXT("CoreProcessManager: StopCoreEngine called but no managed process is running."));
		return;
	}

	UE_LOG(LogShintTools, Log,
		TEXT("CoreProcessManager: Terminating Core Engine process (PID=%u)."), ManagedPID);

	FPlatformProcess::TerminateProc(ProcessHandle, /*bKillTree=*/true);
	FPlatformProcess::CloseProc(ProcessHandle);

	ProcessHandle = FProcHandle();
	ManagedPID = 0;

	UE_LOG(LogShintTools, Log, TEXT("CoreProcessManager: Core Engine terminated."));
}

// ─────────────────────────────────────────────────────────────────────────────
// Status
// ─────────────────────────────────────────────────────────────────────────────

bool FCoreProcessManager::IsCoreRunning()
{
	if (!ProcessHandle.IsValid())
	{
		return false;
	}

	return FPlatformProcess::IsProcRunning(ProcessHandle);
}

// ─────────────────────────────────────────────────────────────────────────────
// Private Launchers
// ─────────────────────────────────────────────────────────────────────────────

bool FCoreProcessManager::LaunchDocker(uint32& OutPID)
{
	const FString DockerExe = TEXT("docker");
	const FString Args = TEXT("run --rm -p 18200:18200 shinttools-core");

	UE_LOG(LogShintTools, Log,
		TEXT("CoreProcessManager: Launching Docker. Exe=%s Args=%s"),
		*DockerExe, *Args);

	ProcessHandle = FPlatformProcess::CreateProc(
		*DockerExe,
		*Args,
		/*bLaunchDetached=*/ true,
		/*bLaunchHidden=*/   false,
		/*bLaunchReallyHidden=*/ false,
		/*OutProcessID=*/ &OutPID,
		/*PriorityModifier=*/ 0,
		/*OptionalWorkingDirectory=*/ nullptr,
		/*PipeWriteChild=*/ nullptr,
		/*PipeReadChild=*/ nullptr
	);

	if (!ProcessHandle.IsValid())
	{
		UE_LOG(LogShintTools, Error,
			TEXT("CoreProcessManager: FPlatformProcess::CreateProc failed for Docker."));
		OutPID = 0;
		return false;
	}

	return true;
}
