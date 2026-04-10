# ShintTools UE5 Plugin — Changelog

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
