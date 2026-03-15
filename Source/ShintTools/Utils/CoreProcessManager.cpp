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
	const FString& ScriptPath,
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

	bool bLaunched;

	switch (Mode)
	{
	case ECoreStartMode::PythonScript:
		bLaunched = LaunchPython(ScriptPath, OutPID);
		break;

	case ECoreStartMode::Docker:
		bLaunched = LaunchDocker(OutPID);
		break;

	default:
		UE_LOG(LogShintTools, Error, TEXT("CoreProcessManager: Unknown start mode."));
		return false;
	}

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
// Path Resolution
// ─────────────────────────────────────────────────────────────────────────────

FString FCoreProcessManager::ResolveCoreScriptPath()
{
	// 1. Try next to the plugin
	const FString PluginRelative = FPaths::Combine(
		FPaths::ProjectPluginsDir(),
		TEXT("ShintTools"),
		TEXT("CoreEngine"),
		TEXT("main.py"));

	if (FPaths::FileExists(PluginRelative))
	{
		UE_LOG(LogShintTools, Log,
			TEXT("CoreProcessManager: Found main.py at plugin path: %s"), *PluginRelative);
		return PluginRelative;
	}

	// 2. Try at project root
	const FString ProjectRelative = FPaths::Combine(
		FPaths::ProjectDir(),
		TEXT("CoreEngine"),
		TEXT("main.py"));

	if (FPaths::FileExists(ProjectRelative))
	{
		UE_LOG(LogShintTools, Log,
			TEXT("CoreProcessManager: Found main.py at project path: %s"), *ProjectRelative);
		return ProjectRelative;
	}

	UE_LOG(LogShintTools, Warning,
		TEXT("CoreProcessManager: Could not locate main.py. Checked:\n  %s\n  %s"),
		*PluginRelative, *ProjectRelative);

	return FString();
}

// ─────────────────────────────────────────────────────────────────────────────
// Private Launchers
// ─────────────────────────────────────────────────────────────────────────────

bool FCoreProcessManager::LaunchPython(const FString& ScriptPath, uint32& OutPID)
{
	if (ScriptPath.IsEmpty())
	{
		UE_LOG(LogShintTools, Error,
			TEXT("CoreProcessManager: LaunchPython called with empty script path."));
		return false;
	}

	if (!FPaths::FileExists(ScriptPath))
	{
		UE_LOG(LogShintTools, Error,
			TEXT("CoreProcessManager: Script not found: %s"), *ScriptPath);
		return false;
	}

	// Use 'python3' on Linux/Mac, 'python' on Windows as fallback
#if PLATFORM_WINDOWS
	const FString PythonExe = TEXT("python");
#else
	const FString PythonExe = TEXT("python3");
#endif

	const FString Args = FString::Printf(TEXT("\"%s\""), *ScriptPath);
	const FString WorkingDir = FPaths::GetPath(ScriptPath);

	UE_LOG(LogShintTools, Log,
		TEXT("CoreProcessManager: Launching Python. Exe=%s Args=%s WorkingDir=%s"),
		*PythonExe, *Args, *WorkingDir);

	// bLaunchDetached=true  : don't block the editor
	// bLaunchHidden=false   : show console window (useful for debugging)
	// bLaunchReallyHidden=true : truly hidden in production
	ProcessHandle = FPlatformProcess::CreateProc(
		*PythonExe,
		*Args,
		/*bLaunchDetached=*/ true,
		/*bLaunchHidden=*/   false,
		/*bLaunchReallyHidden=*/ false,
		/*OutProcessID=*/ &OutPID,
		/*PriorityModifier=*/ 0,
		*WorkingDir,
		/*PipeWriteChild=*/ nullptr,
		/*PipeReadChild=*/ nullptr
	);

	if (!ProcessHandle.IsValid())
	{
		UE_LOG(LogShintTools, Error,
			TEXT("CoreProcessManager: FPlatformProcess::CreateProc failed for Python."));
		OutPID = 0;
		return false;
	}

	return true;
}

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
