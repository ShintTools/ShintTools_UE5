# ShintTools UE5 Plugin — Changelog

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
