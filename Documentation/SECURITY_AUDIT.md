# ShintTools — Marketplace Plugin Security Audit

**Scope:** `ShintTools` UE5 editor plugin, Marketplace/Fab build
(`SHINT_MARKETPLACE_BUILD=1`). Free build adds `SHINT_FREE_TIER=1`.
**Date:** 2026-06-22 · **Issue:** #311 · **Engine target:** UE 5.7

This document records what the plugin does that touches the network, the
filesystem, secrets, and external processes, and confirms that every such
action is one the user is told about (Docker pull, a localhost Core engine, a
license check, and an opt-in dashboard upload). Methodology: full source review
of the egress, process-exec, secret-handling and file-write call sites.

---

## 1. Network egress

| Destination | Transport | When | Data sent |
|---|---|---|---|
| `http://127.0.0.1:18200/*` (local Core) | HTTP, loopback only | Every scan / fix / explain | Source of the files being analysed — **stays on `localhost`** |
| `https://shint.tools/api/public/*` (dashboard) | HTTPS | Only on explicit **"Send to Dashboard"** | **Metrics only** — see §3 |
| `ghcr.io/noctxas97dev/shinttools-core` | docker CLI (HTTPS) | Marketplace install / update | Nothing (image *pull*) |
| License status | via local Core (loopback) | Startup + on key change | The per-project `st_` api key |

- Analysis traffic never leaves the machine — it goes to the loopback Core
  container. Plaintext HTTP is acceptable because the endpoint is `127.0.0.1`.
- In the **Free** build, `FShintCoreConfig::GetBaseUrl()` hard-codes
  `127.0.0.1:18200` and ignores any host/port override in the config, so a
  tampered config cannot redirect the plugin at a rogue proxy
  (`ShintSecurity.h`, `SHINT_FREE_TIER`).

## 2. Process execution

| Process | Launcher | Shell? | Arguments |
|---|---|---|---|
| `docker` (pull/run/ps/start/logs) | `CreateProc("docker", …)` | **No shell** | Plugin constants only (`ImageTag`, `ContainerName`, int `HostPort`) |
| `Build.bat <Project>Editor` | `ExecProcess` | No shell | Engine path + project name/path |
| `cmd.exe /k <diagnostics.bat>` | `CreateProc("cmd.exe", …)` | cmd (shell) | User-triggered diagnostics only; `.bat` embeds the constant `ContainerName` |

- `RunDocker` invokes `docker.exe` **directly** (`FPlatformProcess::CreateProc`,
  no `cmd`/`sh`), so shell metacharacters in arguments are not interpreted.
- `ImageTag`, `ContainerName`, and `HostPort` are compile-time constants
  (`ShintCoreInstaller.h`), never sourced from user or config input ⇒ **no
  command-injection vector**.
- The only shell (`cmd.exe`) path is the *Diagnostics* button, which the user
  explicitly clicks; it embeds the constant container name, not user input.

**Recommendation:** keep `ContainerName`/`ImageTag` plugin-controlled. If they
ever become config-driven, validate against `^[A-Za-z0-9_.-]+$` before use.

## 3. Data handling / privacy

- **Finding (HIGH, remediated — #314):** the "Send to Dashboard" code-validator
  upload previously attached each scanned file's **full source** (`content`
  field) to `shint.tools`. Fixed: the payload now carries **metrics only** —
  per-file findings (`rule_id`, `rule_name`, `severity`, `category`, `line`,
  `message`), counts, and `lines_count` — plus project totals. Raw source,
  snippets, and source context are never transmitted, and the files are no
  longer even read from disk during the send. (`ShintDashboardSync.cpp`)
- The Asset-Naming upload already sent metadata only (`name`, `path`, `type`,
  `category`).
- The dashboard upload is **opt-in** (a button) and **paid-tier gated**.

## 4. Secrets handling

- The dashboard api key is the **per-project `st_` key** (limited scope), held
  in `shinttools.config.json` and sent as `Authorization: Bearer`. The
  `session_token` is deliberately kept out of the config (it leaked via git
  historically) — see `BuildAuthHeaders`.
- Keys are masked (`shint_***xxxx`) when they must appear in a log, and secure
  logging collapses to `Verbose` so keys are not shown in the default editor log
  (`ShintSecurity.h`).
- **Recommendation (LOW):** the `st_` key is stored in plaintext in the project
  config (standard for API keys, limited blast radius). A future hardening could
  move it to the OS credential store.

## 5. Filesystem writes

| File | Purpose | Notes |
|---|---|---|
| `shinttools.config.json` | Config (base url, keys, project) | Functional |
| `Saved/ShintTools/welcome.txt` | One-time onboarding marker | State, not a log |
| `Saved/ShintTools/<consent marker>` | One-time consent marker | State, not a log |
| `%TEMP%/ShintTools_Core_Diagnostics.bat` | Diagnostics helper | **User-triggered** only |
| User's own `.cpp/.h` | Applying an accepted fix | The fix feature, by design |

- **#306 / #310:** the plugin writes **no log files** to disk (verified: no
  `CreateFileWriter`/`FOutputDeviceFile`/`*.log` writers anywhere in `Source/`).
  Console logging for the marketplace build is downgraded to `Verbose`
  (default-hidden); only `Warning`/`Error` surface in the end-user Output Log.

## 6. Third-party code execution

- The Core runs as a local Docker container pulled from the project's own GHCR
  image. The image is disclosed and the install flows through a consent dialog
  (`SShintConsentDialog`). No other third-party binaries are downloaded or run.

---

## Findings summary

| # | Severity | Status |
|---|---|---|
| Source code sent to external dashboard | HIGH | ✅ Remediated (#314 — metrics only) |
| `st_` api key stored plaintext in config | LOW | Accepted (per-project, limited scope); OS keychain is a future option |
| Diagnostics uses `cmd.exe` | INFO | Safe (plugin-constant args, user-triggered) |
| No on-disk logs; console logs `Verbose` | INFO | ✅ (#306 / #310) |

**Conclusion:** after the #314 remediation, the plugin's observable actions are
limited to the disclosed set — a localhost Core, an opt-in metrics-only
dashboard upload, a license check, and a consented Docker image pull. No
undisclosed network egress, no source-code exfiltration, and no
command-injection surface were found.
