// Copyright 2026 ShintTools. All Rights Reserved.

#include "ShintCoreInstaller.h"
#include "ShintTools.h"

#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HttpModule.h"
#include "HttpManager.h"
#include "Interfaces/IHttpResponse.h"
#include "Interfaces/IHttpRequest.h"

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
		TEXT("Waiting for Core Engine to respond... "
		     "(first run can take ~1 min while FastAPI loads)"));
	const double Start    = FPlatformTime::Seconds();
	const double Deadline = Start
		+ static_cast<double>(HealthTimeoutSeconds);
	int32 Attempt = 0;
	while (FPlatformTime::Seconds() < Deadline)
	{
		if (IsCoreHealthy(HostPort))
		{
			return true;
		}
		++Attempt;

		if (Attempt % 5 == 0)
		{
			const double Elapsed = FPlatformTime::Seconds() - Start;
			Emit(EShintInstallStep::WaitingForHealth, 85,
				FString::Printf(TEXT(
					"Still waiting for /health on port %d "
					"(%.0fs / %.0fs)..."),
					HostPort, Elapsed, HealthTimeoutSeconds));
		}
		FPlatformProcess::Sleep(2.0f);
	}
	Emit(EShintInstallStep::Failed, 0,
		FString::Printf(TEXT(
			"Core Engine didn't respond on port %d within %.0f s. "
			"Running auto-diagnostics..."),
			HostPort, HealthTimeoutSeconds));
	EmitDiagnostics();
	return false;
}

void FShintCoreInstaller::EmitDiagnostics()
{

	const FString TempDir = FPlatformProcess::UserTempDir();
	const FString BatPath = FPaths::Combine(TempDir,
		TEXT("ShintTools_Core_Diagnostics.bat"));

	const FString BatBody = FString::Printf(TEXT(
		"@echo off\r\n"
		"chcp 65001 > nul\r\n"
		"title ShintTools Core Engine - Diagnostics\r\n"
		"echo ===========================================\r\n"
		"echo   ShintTools Core Engine - Diagnostics\r\n"
		"echo ===========================================\r\n"
		"echo.\r\n"
		"echo --- docker ps -a --filter name=%s ---\r\n"
		"docker ps -a --filter name=^^%s$ --format \"table {{.Names}}\\t{{.Status}}\\t{{.Ports}}\"\r\n"
		"echo.\r\n"
		"echo --- docker logs --tail 80 %s ---\r\n"
		"docker logs --tail 80 %s\r\n"
		"echo.\r\n"
		"echo --- curl -v http://localhost:%d/health ---\r\n"
		"curl -v --max-time 5 http://localhost:%d/health\r\n"
		"echo.\r\n"
		"echo ===========================================\r\n"
		"echo  Window stays open. Try:\r\n"
		"echo    docker logs -f %s\r\n"
		"echo    docker restart %s\r\n"
		"echo    docker exec -it %s bash\r\n"
		"echo ===========================================\r\n"
		"echo.\r\n"),
		*ContainerName, *ContainerName,
		*ContainerName, *ContainerName,
		HostPort, HostPort,
		*ContainerName, *ContainerName, *ContainerName);

	if (!FFileHelper::SaveStringToFile(BatBody, *BatPath,
		FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		Emit(EShintInstallStep::Failed, 0,
			FString::Printf(TEXT(
				"[diag] Could not write diagnostics script to %s. "
				"Manual commands:\n"
				"  docker ps -a --filter name=%s\n"
				"  docker logs --tail 80 %s\n"
				"  curl http://localhost:%d/health"),
				*BatPath, *ContainerName, *ContainerName, HostPort));
		return;
	}

	const FString CmdArgs = FString::Printf(
		TEXT("/k \"\"%s\"\""), *BatPath);

	uint32 OutPID = 0;
	FProcHandle Handle = FPlatformProcess::CreateProc(
		TEXT("cmd.exe"), *CmdArgs,
		 true,
		   false,
		 false,
		&OutPID,
		 0,
		 nullptr,
		 nullptr,
		  nullptr);

	if (Handle.IsValid())
	{
		FPlatformProcess::CloseProc(Handle);
		Emit(EShintInstallStep::Failed, 0,
			FString::Printf(TEXT(
				"[diag] Diagnostics window opened (PID %u). "
				"Check the new cmd terminal for docker ps / "
				"docker logs / curl output."), OutPID));
	}
	else
	{
		Emit(EShintInstallStep::Failed, 0,
			FString::Printf(TEXT(
				"[diag] Failed to launch cmd.exe. Run this script "
				"manually:\n  %s"), *BatPath));
	}
}

void FShintCoreInstaller::Emit(
	EShintInstallStep Step, int32 Percent, const FString& Message)
{
	UE_LOG(LogShintTools, Verbose,
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
		 false,
		   true,
		 true,
		&OutPID,
		 0,
		 nullptr,
		WritePipe,
		nullptr);

	if (!Handle.IsValid())
	{
		FPlatformProcess::ClosePipe(ReadPipe, WritePipe);
		OutStdout = TEXT("Failed to spawn docker (is it on PATH?)");
		return -1;
	}

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
