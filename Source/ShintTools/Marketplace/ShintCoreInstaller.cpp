// Copyright ShintTools. All Rights Reserved.

#include "ShintCoreInstaller.h"
#include "ShintTools/ShintTools.h"

#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Misc/Paths.h"
#include "HttpModule.h"
#include "Interfaces/IHttpResponse.h"
#include "Interfaces/IHttpRequest.h"


// ─────────────────────────────────────────────────────────────────────────────
// Public entrypoint
// ─────────────────────────────────────────────────────────────────────────────

bool FShintCoreInstaller::Run()
{
	if (!CheckDocker())
	{
		return false;
	}
	if (!PullImage())
	{
		return false;
	}
	if (!StartContainer())
	{
		return false;
	}
	if (!WaitForHealth())
	{
		return false;
	}
	Emit(EShintInstallStep::Done, 100,
		TEXT("Core Engine is running."));
	return true;
}

bool FShintCoreInstaller::IsCoreHealthy(int32 Port)
{
	// Best-effort sync HTTP probe using the engine's HTTP module.
	// We block briefly in a polling loop -- this is only ever called
	// from the wizard's worker thread, never the Game Thread.
	const FString Url = FString::Printf(
		TEXT("http://127.0.0.1:%d/health"), Port);
	auto Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(Url);
	Request->SetVerb(TEXT("GET"));
	Request->SetTimeout(2.f);

	bool bDone = false;
	bool bOk = false;
	Request->OnProcessRequestComplete().BindLambda(
		[&bDone, &bOk](
			FHttpRequestPtr, FHttpResponsePtr Resp, bool bSuccess)
		{
			bOk = bSuccess && Resp.IsValid()
				&& Resp->GetResponseCode() == 200;
			bDone = true;
		});
	Request->ProcessRequest();

	const double Deadline = FPlatformTime::Seconds() + 3.0;
	while (!bDone && FPlatformTime::Seconds() < Deadline)
	{
		FHttpModule::Get().GetHttpManager().Tick(0.05f);
		FPlatformProcess::Sleep(0.05f);
	}
	return bOk;
}

// ─────────────────────────────────────────────────────────────────────────────
// Steps
// ─────────────────────────────────────────────────────────────────────────────

bool FShintCoreInstaller::CheckDocker()
{
	Emit(EShintInstallStep::CheckingDocker, 0,
		TEXT("Looking for Docker..."));
	FString Out;
	const int32 Code = RunDocker(TEXT("--version"), Out);
	if (Code != 0)
	{
		Emit(EShintInstallStep::DockerMissing, 0, TEXT(
			"Docker not found. Install Docker Desktop from "
			"https://www.docker.com/products/docker-desktop, then "
			"retry."));
		return false;
	}
	Emit(EShintInstallStep::CheckingDocker, 10,
		FString::Printf(TEXT("Found: %s"), *Out.TrimStartAndEnd()));
	return true;
}

bool FShintCoreInstaller::PullImage()
{
	Emit(EShintInstallStep::PullingImage, 15,
		FString::Printf(TEXT("Pulling %s ..."), *ImageTag));
	FString Out;
	const int32 Code = RunDocker(
		FString::Printf(TEXT("pull %s"), *ImageTag), Out);
	if (Code != 0)
	{
		Emit(EShintInstallStep::Failed, 0,
			FString::Printf(TEXT("docker pull failed (exit %d). %s"),
				Code, *Out.Left(400)));
		return false;
	}
	Emit(EShintInstallStep::PullingImage, 60,
		TEXT("Image pulled."));
	return true;
}

bool FShintCoreInstaller::StartContainer()
{
	Emit(EShintInstallStep::CreatingContainer, 65,
		TEXT("Creating container..."));

	// If a container by the same name already exists (previous run),
	// just start it. Otherwise create a new one.
	FString Out;
	int32 Code = RunDocker(FString::Printf(
		TEXT("ps -a --filter name=^%s$ --format {{.Names}}"),
		*ContainerName), Out);
	const bool bExists = (Code == 0
		&& Out.TrimStartAndEnd().Equals(ContainerName));

	if (bExists)
	{
		Code = RunDocker(
			FString::Printf(TEXT("start %s"), *ContainerName), Out);
	}
	else
	{
		// Detached, restart on failure, publish health port. No volume
		// mount for the LLM model on first install -- the image bakes
		// in a baseline model; users who want the upgraded model can
		// mount a directory later.
		const FString CreateArgs = FString::Printf(
			TEXT("run -d --name %s -p %d:18200 --restart unless-stopped %s"),
			*ContainerName, HostPort, *ImageTag);
		Code = RunDocker(CreateArgs, Out);
	}

	if (Code != 0)
	{
		Emit(EShintInstallStep::Failed, 0,
			FString::Printf(TEXT("docker run/start failed (exit %d). %s"),
				Code, *Out.Left(400)));
		return false;
	}
	Emit(EShintInstallStep::CreatingContainer, 80,
		TEXT("Container started."));
	return true;
}

bool FShintCoreInstaller::WaitForHealth()
{
	Emit(EShintInstallStep::WaitingForHealth, 85,
		TEXT("Waiting for Core Engine to respond..."));
	const double Deadline = FPlatformTime::Seconds()
		+ static_cast<double>(HealthTimeoutSeconds);
	while (FPlatformTime::Seconds() < Deadline)
	{
		if (IsCoreHealthy(HostPort))
		{
			return true;
		}
		FPlatformProcess::Sleep(1.0f);
	}
	Emit(EShintInstallStep::Failed, 0,
		FString::Printf(TEXT(
			"Core Engine didn't respond on port %d within %.0f s. "
			"Check Docker Desktop is running and try again."),
			HostPort, HealthTimeoutSeconds));
	return false;
}

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

void FShintCoreInstaller::Emit(
	EShintInstallStep Step, int32 Percent, const FString& Message)
{
	UE_LOG(LogShintTools, Log,
		TEXT("[CoreInstaller] step=%d pct=%d %s"),
		static_cast<int32>(Step), Percent, *Message);
	if (OnProgress)
	{
		FShintInstallProgress Prog;
		Prog.Step    = Step;
		Prog.Percent = Percent;
		Prog.Message = Message;
		OnProgress(Prog);
	}
}

int32 FShintCoreInstaller::RunDocker(
	const FString& Args, FString& OutStdout)
{
	OutStdout.Reset();

	void* ReadPipe  = nullptr;
	void* WritePipe = nullptr;
	FPlatformProcess::CreatePipe(ReadPipe, WritePipe);

	int32 ReturnCode = -1;
	uint32 OutPID = 0;
	FProcHandle Handle = FPlatformProcess::CreateProc(
		TEXT("docker"), *Args,
		/*bLaunchDetached=*/ false,
		/*bLaunchHidden=*/   true,
		/*bLaunchReallyHidden=*/ true,
		&OutPID,
		/*PriorityModifier=*/ 0,
		/*OptionalWorkingDirectory=*/ nullptr,
		WritePipe,
		nullptr);

	if (!Handle.IsValid())
	{
		FPlatformProcess::ClosePipe(ReadPipe, WritePipe);
		OutStdout = TEXT("Failed to spawn docker (is it on PATH?)");
		return -1;
	}

	// Drain pipe while process runs.
	while (FPlatformProcess::IsProcRunning(Handle))
	{
		OutStdout += FPlatformProcess::ReadPipe(ReadPipe);
		FPlatformProcess::Sleep(0.05f);
	}
	OutStdout += FPlatformProcess::ReadPipe(ReadPipe);

	FPlatformProcess::GetProcReturnCode(Handle, &ReturnCode);
	FPlatformProcess::CloseProc(Handle);
	FPlatformProcess::ClosePipe(ReadPipe, WritePipe);
	return ReturnCode;
}
