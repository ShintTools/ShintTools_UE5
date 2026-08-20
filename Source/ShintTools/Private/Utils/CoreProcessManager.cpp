// Copyright 2026 ShintTools. All Rights Reserved.

#include "CoreProcessManager.h"
#include "ShintTools.h"

#include "HAL/PlatformProcess.h"
#include "Misc/Paths.h"

FCoreProcessManager::FCoreProcessManager()
	: ManagedPID(0)
{
}

FCoreProcessManager::~FCoreProcessManager()
{
}

bool FCoreProcessManager::StartCoreEngine(
	ECoreStartMode Mode,
	uint32& OutPID)
{
	OutPID = 0;

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

	FPlatformProcess::TerminateProc(ProcessHandle, true);
	FPlatformProcess::CloseProc(ProcessHandle);
	ProcessHandle = FProcHandle();
	ManagedPID = 0;
}

bool FCoreProcessManager::IsCoreRunning()
{
	return ProcessHandle.IsValid() && FPlatformProcess::IsProcRunning(ProcessHandle);
}

bool FCoreProcessManager::LaunchDocker(uint32& OutPID)
{
	const FString DockerExe = TEXT("docker");
	const FString Args = TEXT("run --rm -p 18200:18200 shinttools-core");

	ProcessHandle = FPlatformProcess::CreateProc(
		*DockerExe,
		*Args,
		 true,
		   false,
		 false,
		 &OutPID,
		 0,
		 nullptr,
		 nullptr,
		 nullptr
	);

	if (!ProcessHandle.IsValid())
	{
		OutPID = 0;
		return false;
	}

	return true;
}
