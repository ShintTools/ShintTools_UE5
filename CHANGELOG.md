# ShintTools UE5 Plugin — Changelog

---

## [1.1.4] — 2026-07-06 — Rename safety + tile cards + Free welcome

### Fixed
- **Asset rename no longer breaks references (all tiers).** The rename flow
  kept deleting the redirector stub (`DeleteFixedUpRedirectors`) and leaned on
  `[CoreRedirects]` as the fallback — but CoreRedirects are only read from the
  `.ini` at editor startup, so anything `FixupReferencers` couldn't re-save in
  the live session (the open level, read-only or unloaded packages) was left
  dangling until the next launch. The redirector is now KEPT
  (`LeaveFixedUpRedirectors`): referencers are still re-pointed at the new
  asset, but every reference form resolves through the stub immediately. Users
  can still sweep stubs via Content Browser → "Fix Up Redirectors".

### Changed
- **KPI tile cards unified across every module.** Code Validator, Asset Naming
  Bot and LOD Auditor tiles now paint the same rounded card (BgCard fill +
  subtle border) as the Overview hero, instead of flat squared surfaces.
- **Per-category Quality breakdown strip removed** from the Code Validator
  (Perf / Sec / BP / Maint / Naming). The overall QUALITY tile is the single
  quality readout.
- **"Fix all" moved onto the filter toolbar** (Unity layout) on both the Code
  Validator and Asset Naming Bot, renamed from "Apply Selected" / "Apply
  Corrections". The paid Send-to-Dashboard button stays in the footer.
- **Welcome dialog now shows for the Free tier too** (once per project), with
  Free-specific copy and an upgrade CTA instead of the paid "full edition"
  unlock text.

---

## [1.1.3] — 2026-07-06 — Panel design unification + fix precision

### Changed
- **Code Validator + Asset Naming Bot restyled to the Asset Optimizer
  design language.** KPI tile rows (caption / big value / coloured subtitle),
  fill-width scan bars, an All/C++/Blueprints tab strip on the Code Validator
  (replaces the "All Types" dropdown), and toolbar text search on both
  modules (message/file/rule for code; names/path for assets).
- **Per-module sidebar icons** (Nieo pack v1.0.3): Code, Assets and the LOD
  Auditor each get a distinct glyph — the rail previously reused the same
  grid icon for Assets and LOD.
- **Paid welcome dialog now fires when the async license probe resolves** —
  it previously checked the tier at tab-spawn, when the probe hadn't
  answered yet, so paid users never saw it.

### Bug Fixes
- **"N fixes applied" toast labels by what was actually applied**, not by
  the last scan mode — a C++ fix applied after a Blueprint scan announced
  itself as Blueprint fixes. Mixed batches show a C++/Blueprint breakdown.
- **Asset rename precision.** Renames are verified individually before any
  [CoreRedirects] mapping / server report is emitted (partial RenameAssets
  failures no longer write poisoned mappings); redirector fixup touches only
  the redirectors this batch created and deletes them after fixup; the
  post-rename auto-save is scoped to the batch's packages instead of saving
  every dirty package in the project.

---

## [Unreleased] — Tier-isolated branch model (Free / Indie / Studio)

### Changed
- **Per-plan release branches with true source isolation.** A customer's
  install never even *contains* a higher tier's source: Free →
  `release-marketplace` (LOD Auditor + AI agent + Send-to-Dashboard stripped),
  Indie → `release-indie` (LOD Auditor stripped), Studio → `main` (full).
  Branches are GENERATED from the single source of truth `develop-studio` by
  `tools/build_tier_release.py` — never hand-committed. Module wiring in
  shared files is delimited by `[LOD/AGENT/DASH-STRIP]` sentinel regions;
  each generated tree was leak-checked (auto-derived symbols + paid endpoint
  strings) and BuildPlugin-verified on UE_5.7.
- **Free tree is fully sanitised.** All developer comments scrubbed from
  shipped source (string-literal-safe; copyright headers kept), dev-only /
  confidential files dropped (tools/, docs/, CI, security audits), sentinel
  markers removed, and paid-feature marketing copy stripped from the welcome
  dialog. Apply-fixes stays free — it drives the free `/validate/fix` route.
- **Fab pack now builds only from the generated `release-marketplace` tree**
  (`tools/build_fab_source_pack.py`); submission zip verified to contain no
  paid symbols, endpoints, or strip markers.

### Added
- **CI tier leak gate** (`.github/workflows/tier-leak-gate.yml`): every
  push/PR to `main`/`develop-studio` regenerates both stripped trees and
  fails on any surviving paid symbol, endpoint string, or module file.

---

## [Unreleased] — Dashboard send: v2 metrics-only contract

### Changed
- **"Send to Dashboard" migrated to the v2 metrics-only server contract.** Code
  Validator now posts `{ project_name, engine, files[], issues[], stats }` —
  each `files[]` entry is a `.strict()` FileItem of only
  `{name, path, type, lines_count}`, and per-issue findings move to a flat
  top-level `issues[]` keyed by project-relative `file_path`
  (`title`/`description`, `suggestion` intentionally empty — never fix text).
  Naming Bot now posts `asset_paths[]` (with real asset `type`) instead of the
  deprecated `items[]`. Still strictly metrics-only: no source, snippets or fix
  suggestions ever leave the machine, matching the server's source-rejecting
  pre-check. (`Source/ShintTools/Private/Core/ShintDashboardSync.cpp`)

---

## [Unreleased — develop-paid] — Studio LOD Auditor: Asset Optimizer completion

### Added (Studio / paid-only)
- **Real asset thumbnails in the Asset Optimizer table (Stage 2b).** Each finding
  row now renders the actual `FAssetThumbnail` (shared `FAssetThumbnailPool`)
  instead of a neutral swatch; falls back to the swatch when the asset can't be
  resolved. (`SShintToolsPanel_Lod.cpp`, `SShintToolsPanel.h`)
- **Per-row + bulk auto-fix as an optimized *duplicate* (Stage 3).** "Fix" writes
  `<Name>_Optimized` next to the original (original never modified), applying the
  server's recommended `max_texture_size` / compression, then rebuilds + saves the
  new asset. Bulk "Fix (N)" applies to all checked rows and reports a success/fail
  summary. New plumbing parses `recommended.max_texture_size` + `recommended.compression`
  from the audit response into `FShintLodFinding`. (`ShintCoreClient.h`,
  `ShintCoreClient_Lod.cpp`, `SShintToolsPanel_Lod.cpp`)
- **Findings export.** "Export" writes a CSV of all findings to
  `Saved/ShintTools/lod_audit_<timestamp>.csv`.
- **Frame-time savings estimate.** The KPI tile now shows a transparent `~X.XX ms`
  estimate derived from the resident-VRAM reduction (texture-bandwidth-weighted
  fraction of a 60fps budget), prefixed `~` and clearly an estimate until real
  profiling telemetry exists.

Verified: clean `BuildPlugin` under forced full-unity (`bForceUnityBuild` +
`bUseAdaptiveUnityBuild=false`), the condition that surfaces jumbo-TU symbol
collisions.

### Bug Reports
- None this cycle (planned Studio feature work, not user/bug-hunt sourced).

---

## [1.1.1] — 2026-06-25 — Fab source-pack sanitization

### Security / Privacy
- **Internal infra leaking through `Source/` comments and user-facing error
  strings.** `Source/` ships verbatim to Fab reviewers/buyers, so several
  comments and error messages disclosed backend internals: dashboard auth key
  format + a past credential leak, `MongoDB`/license-document schema details,
  the internal `core/scripts/seed_license.py` path, "free SKU" build jargon,
  and roadmap/version-history notes. All neutralized to user-meaningful text
  with no code or behaviour change. Hardened `tools/build_fab_source_pack.py`
  to also strip any `*.md` under the staged plugin (dev docs must never ship).

### Bug Reports
- None this cycle (proactive Fab-compliance pass, not user/bug-hunt sourced).

---

## [1.1.0] — 2026-06-23 — privacy, apply-fix & recompile hardening

### Privacy / Security
- **#314 — Send to Dashboard is metrics-only.** The code-validator upload now
  carries per-file findings + counts + project totals; raw source `content`/
  snippets are never transmitted (and not read from disk), and paths are
  project-relative, not absolute (no OS-username leak). The dashboard endpoint
  must ingest findings instead of re-analysing source. (`ShintDashboardSync.cpp`)
- **#311 — Security audit** of the marketplace plugin (`SECURITY_AUDIT.md`):
  source-bearing requests stay on the loopback Core, no hardcoded secrets, no
  device/user fingerprint or telemetry, and `SessionToken` is never transmitted.

### Bug Fixes
- **#312 — Blueprint fixes dropped when applied alongside C++.** `ApplyCodeFixes`
  classified Blueprints by file path only, so a BP finding carrying `FileContent`
  was misrouted into the C++ tree-sitter fixer and silently skipped. Now routed
  by RuleId prefix (matching the UI). (`ShintCoreClient_Validator.cpp`)
- **#313 — Recompile fails after recompile.** The Apply button stayed live during
  the hidden `Build.bat`, so a second Apply spawned a contending build that failed
  on the shared UBT/linker locks. A game-thread re-entrancy guard now skips the
  overlapping build (fixes still apply). (`ShintCoreClient_Validator.cpp`)
- **#310 / #306 — Marketplace logs.** The remaining `Log`/`Display` lines are now
  `Verbose`, so the end-user Output Log shows only warnings/errors; verified the
  plugin writes no log files to disk.
- **Apply-fix recompile gave no feedback (looked like nothing happened).** After
  a C++ auto-fix the plugin spawns `Build.bat` via `ExecProcess`, which runs
  **hidden** — so in the marketplace build (no IDE/console) the user saw no
  window and no compilation, and assumed the fix never applied. Both apply paths
  (local apply + tree-sitter `/validate/fix`) now show a non-blocking editor
  notification: *"recompiling project after fix…"* → *"recompiled successfully"*
  or *"finished with N error(s)"*. (`ShintCoreClient_Validator.cpp`)
- **Null-check auto-fix broke compilation when the line declared a variable**
  (Core-side, fixed in ShintTools Core `7d9cd33`). `int32 N = GI->Count();`
  became `if (IsValid(GI)) { int32 N = GI->Count(); }`, scoping `N` inside the
  block. The Core now hoists the declaration above the guard
  (`int32 N{}; if (IsValid(GI)) { N = GI->Count(); }`) and bails to
  mark-for-review for `auto`/`const`. Reaches marketplace via the republished
  `ghcr.io/shinttools/shinttools-core:latest` image.

### Changed
- **Core image owner.** The install wizard now pulls
  `ghcr.io/shinttools/shinttools-core:latest` (was `genesishg1509`).

---

## [1.0.0] — 2026-06-09 — Fab Marketplace submission (compliance)

Addresses the Fab Technical Review rejection of *ShintTools AI for Unreal Engine*.
Each item below maps to a failed checklist row. These changes live on
`develop-marketplace` and ship via `tools/build_fab_source_pack.py`.

### Marketplace / Fab compliance
- **Standard UE module layout (Public/Private).** All sources moved under
  `Source/ShintTools/Public/` (the exposed module header `ShintTools.h`) and
  `Source/ShintTools/Private/` (every internal implementation file, subfolders
  preserved). `Build.cs` `PrivateIncludePaths` re-rooted under `Private/` so the
  existing folder-qualified sibling includes keep resolving. *(Fail: "asset types
  inside respective folders" / Public-Private recommendation.)*
- **Copyright + year on every source file.** Header changed from
  `// Copyright ShintTools. All Rights Reserved.` to
  `// Copyright 2026 ShintTools. All Rights Reserved.` across all 50 `.h/.cpp/.cs`
  files. *(Fail: "All source and header files contain a commented copyright
  notice with Publisher name and year of publishing.")*
- **`.uplugin` metadata.** Added `"EngineVersion": "5.7.0"` (latest UE — Fab
  requires the latest engine as a Supported Version, 4.2.2.b), added
  `"PlatformAllowList": [ "Win64" ]` to the `ShintTools` module, and pinned
  `"VersionName": "1.0.0"`. *(Fails: latest-engine + per-module PlatformAllowList.)*
- **Documentation folder.** Customer documentation placed in
  `Documentation/ShintTools_UE5_Documentation.docx` and declared in
  `Config/FilterPlugin.ini` (`/Documentation/...`). *(Fails: docs must live in a
  `Docs`/`Documentation` folder declared in FilterPlugin.ini.)*
- **Source-only submission pack.** New `tools/build_fab_source_pack.py` stages a
  clean source tree (no `Binaries/Build/Intermediate/Saved`, no dev folders),
  marks the `.uplugin` `"Installed": false`, and zips the plugin folder at the zip
  root → `dist_fab/ShintTools-UE5-Fab-Source-UE_5.7.zip`. *(Fail: "no unused or
  local folders such as Binaries, Build, Intermediate, or Saved".)*

> **Action required before resubmission:** build/compile the plugin on **UE 5.7**
> (UAT BuildPlugin) to confirm the Public/Private move resolves all includes, and
> list UE 5.7 as the Supported Engine Version on the Fab product page.

---

## [Unreleased] — 2026-06-08

### Bug Fixes
- **#28 — Quality score frozen after "Scan All BP".** `OnBlueprintValidateComplete` rebuilds a stripped `QualityResult` (to route `BPB001` naming issues out of the code list) but copied only `bSuccess` + `FilesScanned`, leaving `QualityScoreOverall` at its `-1` default. `HandleValidateResult`'s merge path updates the score only when `QualityScoreOverall >= 0`, so the Overview score badge + per-category breakdown stayed frozen on the previous C++ scan (or "—" if none ran). Now copies `QualityScoreOverall`, `bHasCategoryBreakdown`, the five per-category scores and `Tier` before the merge. (`SShintToolsPanel_Http.cpp`)
- **#27 — TreeSitter-only auto-fixes silently skipped.** Both apply entry points (`OnApplySelectedCodeFixesClicked`, `OnApplySingleFix`) rejected any non-Blueprint issue with an empty `FixSuggestion`, even when it carried `FileContent` — the prerequisite for the `/validate/fix` AST path that `ApplyCodeFixes` already routes. Clicking **Apply** on such an issue did nothing (no toast when it was the only selection). Guards now reject only when `FixSuggestion` **and** `FileContent` are both empty. (`SShintToolsPanel_Fixes.cpp`)

### Tooling
- **Daily bug-hunt routine** (`tools/bug_hunt/`, `.github/workflows/bug-hunt.yml`). A headless `claude -p` pass scans `Source/ShintTools/**` once a day, de-dupes against open `bug-hunt` issues, and files one issue in the standard template (Bug / File / Lines / Faulty code / Root cause / Trigger path / Suggested fix). Reviews all three shipped build variants — Marketplace (`SHINT_MARKETPLACE_BUILD=1`), Free (`SHINT_FREE_TIER=1`) and Paid — plus the runtime tier gating, so defects inside `#if` branches the default config compiles out are still caught; each issue tags the affected variant. Schedulable via Windows Task Scheduler (local) or GitHub Actions cron (always-on).

### Bug Reports (addressed)
- `bug-hunt #28` — score badge frozen after Scan All BP → fixed above.
- `bug-hunt #27` — "Apply Selected" no-ops for TreeSitter-only C++ issues → fixed above.

---

## [Unreleased] — 2026-04-09

### Bug Fixes

- **CB013 / CS002 / CS003** — False positives on multi-line conditionals. If the line preceding a pointer dereference ended with `&&`, `||`, or `(`, the issue is no longer raised.
- **CB023** — `override` no longer added to functions that already have it. Fixed a regex lookahead bug that failed to detect `override` in certain patterns.
- **CB012** — C-style cast fix was consuming the first character of the target variable. Detection and substitution patterns are now separate.
- **CB008** — Float-without-suffix detection failed at end of line. Changed `[^f]` to `(?![fe])` negative lookahead.
- **CS001 / CS006** — `GetWorld`/`GetOwner` fix produced invalid C++ when the call was inside an assignment expression. Context is now checked and `is_auto_fixable` is set to `false` in that case.
- **CM001, CB003–CB008, CS001, CS006** — Rules no longer flag issues inside `//` line comments or `/* */` block comments.

### New Features

#### Asset Naming Bot — asset type filter
- Added "All Types" dropdown to the naming bot results panel.
- Categories: Materials · Textures · Meshes · Blueprints · VFX · Audio · Animations · Data.
- `AllAssetItems` backing store separated from the visible list, following the same pattern as the code validator.
- BPB001 (Blueprint naming) items now go through `ApplyAssetFilter` instead of being added directly to the visible list.

#### Before / After preview per issue
- **▶ Preview** toggle button on each code validator row.
- When expanded, shows BEFORE and AFTER code side-by-side with line numbers and the affected line highlighted.
- Server returns a ±2-line context window per issue (`context_before`, `context_after`, `context_line_start`) to build the preview without re-reading the file.

#### Per-issue Apply / Ignore buttons
- Every auto-fixable issue shows individual **✓ Apply** and **✗ Ignore** buttons, always visible without expanding the preview.
- **Apply** — applies the fix for that single issue and removes it from the list using the existing fix flow.
- **Ignore** — adds the issue to the fingerprint set so it does not reappear on incremental rescans.

### Infrastructure
- `FileContent` propagated through the full fix flow (`FShintCodeIssue` → `FShintIssueItem` → `ApplyCodeFixes`). Ready for server-side tree-sitter AST validation once that is implemented.

---

## [Unreleased] — 2026-04-10

### New Features

#### Tree-sitter AST-aware auto-fix (plugin integration)
- `ApplyCodeFixes` now triages issues into three categories:
  - **Tree-sitter path** — issues with full `FileContent` are sent to the server endpoint `POST /validate/fix` as `{issues: [{rule_id, file_path, line, content}]}`. The server uses tree-sitter C++ AST analysis to produce a safe `fixed_code` for each issue.
  - **Local fallback path** — issues without `FileContent` but with `fix_suggestion` are applied via line-replacement (previous behaviour, unchanged).
  - **Blueprint path** — issues with `/Game/` or `/Engine/` paths are handled programmatically (BPP001 tick disable via CDO).
- `HandleTreeSitterFixResponse` — new async callback that receives the `/validate/fix` response, writes each `fixed_code` back to disk, merges results with any local fixes, then launches the incremental UBT build check.
- `ParseTreeSitterFixResponse` — parses `{fixes: [{rule_id, file_path, success, fixed_code, additions, changes}], summary: {total, successful, failed}}` into `FShintFixResult`.
- `FShintFixedFile` extended with `Additions` (suggested header additions) and `Changes` (human-readable change list) from the server.
- Removed old fire-and-forget duplicate request to `/validate/fix` that was sending only issue metadata.

#### Tree-sitter fix preview on demand
- Preview toggle on tree-sitter issues (those with full `FileContent`) now fetches the actual post-fix code from `/validate/fix` on first expand, instead of showing the `fix_suggestion` text description.
- `FetchFixPreview` calls `FetchSingleFixPreview` (preview-only, no disk write), receives `fixed_code`, extracts the same ±2-line context window at `ContextLineStart`, and stores it in `FixPreviewCode` on the issue item.
- While the request is in-flight, the AFTER panel shows "Fetching preview…". On completion the list refreshes and shows the real diff.
- `bIsFixable` and `bHasContext` now also consider `FileContent` presence, so tree-sitter issues correctly show the Preview toggle and Apply/Ignore buttons even when `fix_suggestion` or `context_before` are absent.

---

## [Unreleased] — 2026-04-10 (b)

### Bug Fixes

#### Server — fixer correctness
- **`context_after` now contains real C++ code**: server runs the actual tree-sitter/pattern fixer at scan time and extracts the fixed line window. Previously it substituted `fix_suggestion` description text as code (e.g. "Add null-check for Cast result" appeared as a line of code). Multi-line fixes (+4 line headroom) are shown fully.
- **CB023 `add_override`**: replaced optional-group backreference `\1` with two explicit regex branches (const / non-const) — eliminated malformed `const override` concatenation.
- **CB008 `add_suffix`**: float suffix now applied only to the code portion of a line (before `//`); pure comment lines are skipped entirely; scientific notation (`1.0e5`) excluded via `(?![fe\d])`.

#### Plugin — AFTER preview panel
- Removed `bIsTreeSitter` restriction on `ContextAfter`: since the server now always sends real fixed code in `context_after`, it is safe to display for all issue types without waiting for a secondary HTTP call.
- `FetchFixPreview` (HTTP fallback) still fires on Preview toggle but only when `ContextAfter` is also empty, avoiding redundant requests.
