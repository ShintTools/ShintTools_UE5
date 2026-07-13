// Copyright 2026 ShintTools. All Rights Reserved.
//
// [LOD-STRIP-BEGIN]
// Headless LOD Auditor entry point for CI/CD (TDD Part 2 §20.7).
//
//   UnrealEditor-Cmd.exe <Project>.uproject -run=ShintLodAudit
//       -profile=<default|mobile> [-deep]
//       -failon=<error|warning> [-json=<path>] [-csv=<path>]
//
// Reuses FShintCoreClient::AuditLods against the same local Core the editor
// panel talks to (CI images start it via the shipped docker compose file),
// pumps HTTP synchronously to completion, writes the JSON/CSV artifacts, and
// returns an exit code CI can gate on:
//   0  clean (no findings at/above -failon)
//   1  findings at/above -failon
//   2  infrastructure error (Core unreachable / timeout) — distinct from
//      findings so CI can retry rather than fail the build.
//
// Studio-only; stripped from the indie/marketplace trees with the rest of the
// LOD module (registered in tools/build_tier_release.py's "lod" file list).
#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "ShintLodAuditCommandlet.generated.h"

UCLASS()
class UShintLodAuditCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UShintLodAuditCommandlet();

	//~ UCommandlet
	virtual int32 Main(const FString& Params) override;
};
// [LOD-STRIP-END]
