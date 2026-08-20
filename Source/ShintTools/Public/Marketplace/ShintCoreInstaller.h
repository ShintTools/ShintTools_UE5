// Copyright 2026 ShintTools. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

enum class EShintInstallStep : uint8
{
	CheckingDocker,
	DockerMissing,
	PullingImage,
	CreatingContainer,
	WaitingForHealth,
	Done,
	Failed,
	PaidUseLauncher,
};

struct FShintInstallProgress
{
	EShintInstallStep Step;
	int32 Percent = 0;
	FString Message;
};

class SHINTTOOLS_API FShintCoreInstaller
{
public:

	FString ImageTag = TEXT("ghcr.io/shinttools/shinttools-core:latest");

	FString ContainerName = TEXT("shinttools-core");

	int32 HostPort = 18200;

	float HealthTimeoutSeconds = 180.f;

	TFunction<void(const FShintInstallProgress&)> OnProgress;

	bool Run();

	static bool IsCoreHealthy(int32 Port);

private:

	void Emit(EShintInstallStep Step, int32 Percent, const FString& Message);

	bool CheckDocker();
	bool PullImage();
	bool StartContainer();
	bool WaitForHealth();

	void EmitDiagnostics();

	int32 RunDocker(const FString& Args, FString& OutStdout);
};
