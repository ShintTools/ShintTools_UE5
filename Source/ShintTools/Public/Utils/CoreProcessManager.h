// Copyright 2026 ShintTools. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HAL/PlatformProcess.h"

enum class ECoreStartMode : uint8
{

	Docker,
};

class SHINTTOOLS_API FCoreProcessManager
{
public:

	FCoreProcessManager();
	~FCoreProcessManager();

	bool StartCoreEngine(
		ECoreStartMode Mode,
		uint32& OutPID);

	void StopCoreEngine();

	bool IsCoreRunning();

	uint32 GetCorePID() const { return ManagedPID; }

private:

	FProcHandle ProcessHandle;

	uint32 ManagedPID = 0;

	bool LaunchDocker(uint32& OutPID);
};
