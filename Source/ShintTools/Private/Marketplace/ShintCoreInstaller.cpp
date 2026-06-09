// Copyright 2026 ShintTools. All Rights Reserved.

#include "ShintCoreInstaller.h"
#include "ShintTools/ShintTools.h"

#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HttpModule.h"
#include "HttpManager.h"
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
		// Surface progress every 5 attempts so the user can see the
		// wizard is alive instead of staring at a frozen "Waiting...".
		// Without this the only signal during the 1-3 min cold-start
		// window is the spinning progress bar, which has historically
		// pushed users to kill the editor.
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

// ─────────────────────────────────────────────────────────────────────────────
// Diagnostics
// ─────────────────────────────────────────────────────────────────────────────

void FShintCoreInstaller::EmitDiagnostics()
{
	// Spawn an interactive cmd.exe window that runs `docker ps`,
	// `docker logs`, and `curl /health` against our container, then
	// stays open with `cmd /k` so the user can keep typing extra
	// commands (`docker logs -f`, `docker exec -it ... bash`, etc.)
	// without copy-pasting anything from the wizard log.
	//
	// We materialise the script as a .bat in the OS temp dir rather
	// than passing the whole sequence as a single /k argument because
	// (a) it survives the quoting horror of nested `"..."` in cmd /k
	// and (b) the user can re-run the file later from Explorer.

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

	// `cmd /k` keeps the window open after the .bat finishes. Quote
	// the path because TempDir contains spaces on most systems
	// (`C:\Users\<name>\AppData\Local\Temp\`).
	const FString CmdArgs = FString::Printf(
		TEXT("/k \"\"%s\"\""), *BatPath);

	uint32 OutPID = 0;
	FProcHandle Handle = FPlatformProcess::CreateProc(
		TEXT("cmd.exe"), *CmdArgs,
		/*bLaunchDetached=*/ true,
		/*bLaunchHidden=*/   false,
		/*bLaunchReallyHidden=*/ false,
		&OutPID,
		/*PriorityModifier=*/ 0,
		/*OptionalWorkingDirectory=*/ nullptr,
		/*PipeWriteChild=*/ nullptr,
		/*PipeReadChild=*/  nullptr);

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
