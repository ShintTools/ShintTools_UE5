// Copyright 2026 ShintTools. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * EShintInstallStep
 * State machine emitted to the progress callback as the wizard runs.
 */
enum class EShintInstallStep : uint8
{
	CheckingDocker,
	DockerMissing,        // terminal — caller must show install link
	PullingImage,
	CreatingContainer,
	WaitingForHealth,
	Done,
	Failed,
	PaidUseLauncher,      // terminal — paid Core installs via the launcher, not here
};

/**
 * FShintInstallProgress
 * One unit of progress reported to the UI. Percent is best-effort: 0 when
 * unknown (e.g. `docker pull` doesn't emit byte counts to stdout in JSON
 * mode by default), populated during PullingImage when we parse `docker
 * pull` progress lines.
 */
struct FShintInstallProgress
{
	EShintInstallStep Step;
	int32 Percent = 0;          // 0-100; 0 if unknown
	FString Message;            // human-readable log line
};

/**
 * FShintCoreInstaller
 *
 * First-time Core Engine setup wizard. Triggered when the plugin loads
 * and finds no Core listening on the local port.
 *
 * Detects Docker, pulls the Core image, creates a named container, and
 * waits for /health to respond. Each step emits to the OnProgress
 * callback so the UI can render a log.
 *
 * Thread safety: Run() blocks; call from a background thread. The
 * callback is invoked from that thread -- marshal to Game Thread in
 * the UI layer if needed.
 */
class SHINTTOOLS_API FShintCoreInstaller
{
public:

	/** Public image pulled for the Core engine. */
	FString ImageTag = TEXT("ghcr.io/noctxas97dev/shinttools-core:latest");

	/** Container name (so we can `docker start <name>` on subsequent boots). */
	FString ContainerName = TEXT("shinttools-core");

	/** Host port we publish 18200 on. Matches the launcher's CORE_PORT default. */
	int32 HostPort = 18200;

	/** Max wait for /health to come up after start, in seconds.
	 *  Generous: on first run the FastAPI app + transformers import
	 *  can take 30-40 s, and Docker Desktop itself can take another
	 *  20-30 s to actually start the container on a cold daemon. */
	float HealthTimeoutSeconds = 180.f;

	/** Where progress events are delivered. */
	TFunction<void(const FShintInstallProgress&)> OnProgress;

	/**
	 * Run the full wizard end-to-end. Returns true on Done, false on
	 * Failed or DockerMissing.
	 */
	bool Run();

	/** Quick HTTP GET to http://localhost:HostPort/health; true on 200 OK. */
	static bool IsCoreHealthy(int32 Port);

private:

	void Emit(EShintInstallStep Step, int32 Percent, const FString& Message);

	bool CheckDocker();
	bool PullImage();
	bool StartContainer();
	bool WaitForHealth();

	/**
	 * Auto-diagnose post-failure: shell out to `docker ps`,
	 * `docker logs <container>`, and re-probe /health with the raw
	 * HTTP response code visible. Emits each output block to the
	 * wizard log so the user (and support) sees what went wrong
	 * without having to copy / paste commands into a terminal.
	 *
	 * Called from WaitForHealth on timeout; safe to invoke whenever
	 * the install is in a half-broken state.
	 */
	void EmitDiagnostics();

	/** Shell out to `docker <args>`; captures stdout. Returns exit code. */
	int32 RunDocker(const FString& Args, FString& OutStdout);
};
