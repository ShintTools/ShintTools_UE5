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
}

FCoreProcessManager::~FCoreProcessManager()
{
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
		OutPID = ManagedPID;
		return true;
	}

	const bool bLaunched = LaunchDocker(OutPID);
	if (bLaunched)
	{
		ManagedPID = OutPID;
	}
	else
	{
		UE_LOG(LogShintTools, Error, TEXT("CoreProcessManager: Failed to launch Core Engine."));
	}
	return bLaunched;
}

void FCoreProcessManager::StopCoreEngine()
{
	if (!IsCoreRunning()) return;

	FPlatformProcess::TerminateProc(ProcessHandle, /*bKillTree=*/true);
	FPlatformProcess::CloseProc(ProcessHandle);
	ProcessHandle = FProcHandle();
	ManagedPID = 0;
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
		OutPID = 0;
		return false;
	}

	return true;
}
