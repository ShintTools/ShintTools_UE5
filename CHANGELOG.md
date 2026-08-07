# ShintTools UE5 Plugin — Changelog

---

## [1.5.0] - 2026-08-07 — AI Assistant panel (M5)

### Changed
- **The Code Validator's per-row buttons now match the toolbar's.** Preview,
  Explain, Apply and Ignore were the last controls still on the older,
  tighter style — 6×2 or 12×5 padding, a smaller font, an icon brush on
  Apply and ASCII markers (`▶`/`▼`, `✎`, `✗`) baked into the labels — which
  read as a different generation of UI sitting inside the same table. All
  four now use the toolbar's language: flat Surface, 12×6 padding, plain
  label, no glyph. Preview's open/closed state moves into the label itself
  ("Preview" / "Hide Preview"), so nothing is lost with the triangle, and
  Apply keeps a green label because it is the one action in the row that
  writes to the file — with the glyphs gone, colour is what tells it apart
  from Ignore.
- **The plugin now lives under Tools, not Window, in a section that says
  ShintTools.** Window is where Unreal keeps its own panels, and a plugin's
  entry point among them reads as part of the editor rather than as
  something the team installed; Tools is where the editor already groups
  what acts on the project, which is what every ShintTools surface does.
  The section header was also missing entirely — it was created without a
  label, so it rendered as a bare separator and the plugin's name never
  appeared in the menu at all. Predictive Profiler and the AI Assistant move
  into a **Modules** flyout inside it, while the control panel stays one
  click away directly under the header.

### Fixed
- **Every question was ungrounded right after a scan.** The panel's merge
  path — the one that keeps C++ and Blueprint findings visible together —
  rebuilt issues, counters and the quality score but never carried the new
  scan's `analysis_id` across. On the first merged scan of a session the
  field was still empty, and publishing an empty id CLEARS the assistant's
  context by contract, so the panel reported "No analysis in view"
  immediately after a scan that had just produced hundreds of findings. On a
  later scan the stale id was no better: answers were grounded in a
  superseded analysis while the table showed the new one. The merged view
  spans two server-side analyses and only one can be the referent — it now
  takes the freshest, which is the scan the user just ran.
- **The retired AI Assistant tab kept coming back.** The dock replaced it,
  but an editor layout saved while the tab was docked still names it, and
  Unreal restores it on every startup — leaving the spawner registered but
  hidden was not enough. The spawner is now a migration shim: its tab closes
  itself and opens the dock instead, so the stale layout entry is consumed
  once and is gone from the next layout save. Unregistering the spawner
  outright would have left that entry in the layout indefinitely.
- **The per-row "Explain" button did nothing when the assistant was
  closed.** It queued the question and broadcast, which was enough while the
  assistant was a tab the user had already docked; the dock can be collapsed
  or never opened, and then the click queued a question nobody would see.
  It now opens the dock.

### Added
- **AI Assistant dock** — a 56px launcher pinned to the bottom-right
  corner of the editor that expands into a 380×500 card in place and
  collapses back to the launcher, which doubles as the card's close
  button. Opened from Window ▸ ShintTools ▸ AI Assistant, or from the
  launcher once it is on screen; right-click the launcher to dismiss it
  entirely. Three destinations inside: **Chat**, **Memory** and
  **Studio Rules**.
  - **Why not a dock tab.** It started as one, and a tab competes for
    layout space with the thing the user is working on — opening the
    assistant meant rearranging the editor, which is the opposite of
    what an assistant is for. The dock displaces nothing. It floats in
    its own borderless, per-pixel-transparent windows parented to the
    editor's root window, so it stays above every tab and follows the
    editor as it is moved, resized, maximised or dragged to another
    monitor. The old tab spawner stays registered but hidden, so an
    editor layout saved while it was docked still restores cleanly.
  - **Available on every plan.** Free gets a working two-intent
    assistant with no memory; the panel is never hidden behind a paid
    check. Which destinations and quick-prompts appear comes from
    `GET /assistant/capabilities` — there is no tier table in the client,
    so the Core can widen a plan without a plugin release.
  - **Ambient context.** Every module publishes its `analysis_id` when a
    scan completes, and the panel sends it as `context_ref`, so "why is
    this flagged?" works with nothing copied into the conversation. The
    context strip names what the answer will be grounded in.
  - **Streaming replies**, token-by-token for explanations only —
    everything else answers from a table instantly and arrives whole.
  - **Follow-ups are labelled.** A response that inherited its intent and
    grounding server-side is marked as such, so a two-word question
    producing a detailed answer does not look like a coincidence.
  - **Confirmation cards.** A remembered fact or a drafted studio rule
    does nothing at all until accepted in the panel; a drafted rule shows
    the compiler's own reading of it, because what is accepted is exactly
    what will run.
- **`analysis_id` parsed from every scan response** (assistant contract
  §7) on the Code Validator, Asset Naming Bot and LOD Auditor.

### Fixed
- **LOD audits grounded the assistant in only their final batch.** Audits
  are POSTed in chained batches of 150 assets, and each batch minted its
  own analysis server-side — so "summarize this scan" would have answered
  for the last ~150 assets while appearing to speak for the whole
  project. The chain now echoes the first batch's `analysis_id` back on
  every later request and the Core appends to that same analysis.
  (Requires Core 2.16.0.)

### Changed
- **The plugin's Window-menu entries are now one "ShintTools" submenu**
  instead of three flat rows under a section header. As the plugin grew
  it was spreading unrelated-looking items across the Window menu; the
  footprint is a single row now, whatever windows get added later.
- **"Explain" on a finding opens the AI Assistant** instead of a modal.
  It also **works on Free now** — the old dialog drove `/agent/explain`
  (Indie and up), so it was hidden on Free and absent from the
  marketplace build entirely; the assistant answers `explain_finding` on
  every plan.

### Removed
- **The single-shot Explain dialog.** It opened a throwaway window,
  answered one finding, and was destroyed on the next click, so a
  follow-up question had nowhere to go. Superseded by the assistant
  panel, where the same question lands in a thread that keeps its
  context.

---

## [1.4.1] — 2026-08-04 — Predictive Profiler + LOD Auditor client audit

### Added
- **Send to Dashboard on the Predictive Profiler** — previously the only
  module without one. Same toolbar position as the other three (right
  before the primary action), Studio-tier gated.

### Changed
- **Send to Dashboard's toolbar position on Code Validator and Asset
  Naming Bot** now matches the LOD Auditor's — inline with Select All /
  Deselect All / Fix all, instead of a standalone row below the results
  list.
- **Declared engine compatibility widened to 5.2-5.8** (was pinned to
  5.7.0). Static audit of the plugin's UE5 API surface found nothing
  version-gated that needed guarding (GetNumTriangles/GetNumVertices,
  Nanite settings fields, StaticMeshEditorSubsystem::AddSimpleCollisions,
  FSavePackageArgs, UMaterialEditingLibrary::GetStatistics all confirmed
  stable across the range) — removed the one real risk found, an unused
  dependency on the soft-deprecated `EditorStyle` module (the UI already
  uses `FAppStyle` throughout). Real per-version compilation is still only
  verified at 5.7 locally; 5.2-5.6/5.8 need either those engines installed
  or Fab's own build farm to confirm.

### Fixed
- **Predictive Profiler — BUILD RISK always read 100.** The gauge painted the
  Core's `build_health` score (100 = healthy) directly under a RISK label
  without inverting it, so a perfectly healthy build showed as maximum risk
  (and painted red). Also fixed in the Impact Simulator's before→after
  animation for the same gauge.
- **Predictive Profiler — Top Issues' BUILD filter (and dimension tags)
  almost never matched anything.** The client picked a finding's "dominant"
  impact dimension by comparing raw magnitudes across different units (MB vs
  ms) — `build_mb` is by construction 85% of `vram_mb`, so it could
  structurally never win. Now parses and prefers the Core's own
  budget-normalized `primary_cost.dimension`.
- **Predictive Profiler — "+ add to selection" was a silent no-op** for any
  recommendation outside the top 10 issues (the simulator's recommendations
  are computed over the full cost_items set). `FindIssue`/`SelectedItemIds`
  now also search `CostItems`.
- **Predictive Profiler — CPU/GPU risk showed 0 (green) instead of "no
  data"** when the Core genuinely had nothing to score (no code/scene data
  collected) — the gauges now call `SetNoData()` for that case instead of
  painting a false "no risk".
- **Predictive Profiler — frame-budget bar's uncertainty tail was applied to
  the wrong segment** (CPU's headroom landed on a GPU segment) and the bar's
  stacked CPU+GPU total silently overstated true frame time (CPU/GPU work is
  pipelined, not additive). The caption now also surfaces the Core's own
  bottleneck-based frame prediction.
- **Predictive Profiler — dead "medium" severity filter** removed; the Core
  only ever emits critical/warning/info.
- **LOD Auditor — a fixed finding could survive Auto-Fix and keep showing up
  as unresolved.** Root cause: the in-place fixer mapped the Core's
  recommended `compression`/`lod_group` values (`"BC7"`, `"NormalMap"`, …)
  through UE's raw enum-reflection names, which never match (`"BC7"` ≠
  `"TC_BC7"`) — every such fix silently changed nothing while still being
  counted as a success, and `IsAutoApplicable` hid the Fix button for the
  same reason on rows it could have handled. Added a proper Core-vocabulary
  → UE-enum mapping (also fixing two wrong targets in the older duplicate-
  path helper: BC4 is `TC_Alpha` not `TC_Grayscale`, BC6H is
  `TC_HDR_Compressed` not `TC_HDR`, which is uncompressed RGBA16F).
- **LOD Auditor — a genuinely successful fix still left the finding in the
  list, KPI counts, and treemap** until the next full scan. Fixed findings
  now drop out of the table, the ISSUES/EST. SAVING KPIs, and the VRAM
  treemap the instant the fix applies (`RemoveFixedLodFinding`), and a
  session-scoped key set (`asset_path|rule_id`) prevents a stray re-flag if
  a re-scan races ahead of the in-memory asset change.
- **LOD Auditor — "Fix Selected" silently miscounted no-op fixes as
  successes** (an unmapped/already-matching value and a real property change
  were both reported as "fixed"); now separately reports "already matched".
- **LOD Auditor — "Fix (N)" and Fix Selected only saw the current tab.**
  Checking rows on one tab, then switching tabs, silently dropped them from
  both the counter and from what Fix Selected would act on. Both are now
  scoped to all findings, not just the visible tab.
- **LOD Auditor — findings outside Texture/Mesh/Material (e.g. Mobile-
  profile findings) counted toward the ISSUES KPI but had no tab that could
  ever show them.** Added an **Other** tab as a catch-all so no category is
  silently invisible; the ISSUES subtitle now reconciles with the header
  total.
- **LOD Auditor — Budgets view showed the wrong VRAM pool budget** (hardcoded
  4096/1024 MB) versus what LT015 actually audited against
  (`LT015_POOL_BUDGET_MB`: 2000 default / 500 mobile in the Core's
  thresholds) — a studio could see "plenty of headroom" in Budgets while the
  findings above already flagged the pool as over budget.

### Removed
- **LOD Auditor — the Rules view** (findings grouped by rule id). The Unity
  client has no equivalent, and the plugin ships to both engines from one
  contract now. `ELodView::Rules`, its widgets, and its stale symbol entries
  in `build_tier_release.py`'s leak-detection list are gone; no other code
  referenced this view by name or index.

### Known issues
- **LOD Auditor's Send to Dashboard returns HTTP 500.** Confirmed server-side
  (shint.tools, the external dashboard — not this repo, no route for it
  exists in the Core either): the client's request body is well-formed and
  the same pattern as the working Code Validator/Asset Naming endpoints.
  Needs dashboard-side access to actually fix.

---

## [1.4.0] — 2026-07-28 — Predictive Profiler (Studio)

### Added
- **Predictive Profiler — new Studio-only module.** An independent dockable
  window (its own nomad tab, opened from *Window ▸ Predictive Profiler*) that
  predicts CPU / GPU / memory / build cost **before** you play or cook, by
  static analysis against the local Core. Three zones over the ShintTools dark
  shell:
  - **Scores** — radial risk gauges (CPU / GPU / Memory / Build + Overall) and
    a stacked frame-budget bar with an uncertainty tail. Requires Core ≥ v2.10.0.
  - **Top Issues** — every priced asset/pattern as a *name + cost* row (cost is
    a band with a confidence pill), filterable by severity and dimension. It
    prices, it does not diagnose — remediation/severity show only when there's
    a known fix.
  - **Impact Simulator** — check recoverable issues to see live per-dimension
    deltas, before→after score animation, and a one-click *add to selection*
    for the next-best fix. Supports "what if I port to another platform?".
- The client scans the project itself (assets + scene digest + raw source),
  batched through the Core's session API (**150 assets per ingest**, source
  deferred per chunk) so large projects don't stall the editor.

### Notes
- Numbers carry an **uncalibrated** `calibration_version` for now (the honesty
  footer states it); calibration against reference hardware lands next.

---

## [1.3.7] — 2026-07-21 — AI Assistant window title

### Changed
- The explanation window is now titled **"ShintTools AI Assistant"** (was
  "Issue Explanation — ShintTools"). The panel answers questions well beyond
  explaining a single issue, so the old title undersold it.

---

## [1.3.6] — 2026-07-18 — LOD Auditor: Material usage-flag auto-fix + Send-to-Dashboard

### Added
- **Material auto-fix (LM012) — clear unused usage flags.** Requires Core
  ≥ v2.3.0. The client now performs a **conservative** referencer analysis of
  each material and sends `usage_flags_unused`; the Core (LM012) returns the
  concrete `bUsedWith*` flags to clear in `recommended.clear_usage_flags`, and
  the in-editor fixer clears them via reflection (`SetMaterialUsage`-style
  property write), wrapped in the undoable transaction + Journal (Revert
  restores them). Safety first: clearing a *needed* usage flag would make the
  material render as default at runtime, so the client **abstains entirely**
  whenever a Blueprint or Level references the material (they can spawn
  components whose usage isn't visible here), and only reasons about flags
  determinable from asset references (skeletal mesh, geometry collection, hair).
  This is the only Material rule that is safely auto-applicable; the rest
  (sampler/instruction/node reduction) remain advisory.
- **Send LOD audit to Dashboard.** New "Send to Dashboard" button on the LOD
  Auditor toolbar posts the audit **results (metrics only)** to the external
  dashboard — per-finding metadata + aggregate KPIs, never asset bytes. Mirrors
  the Code Validator / Asset Naming dashboard sync (Bearer per-project key).
  Endpoint: `POST {DashboardUrl}/api/public/lod-auditor/analyze`.

### Notes
- Verified with UAT BuildPlugin (UE_5.7, BUILD SUCCESSFUL); Core LOD suite
  347 tests green. The dashboard endpoint (`/api/public/lod-auditor/analyze`)
  must be implemented on the shint.tools web side to accept the payload.

---

## [1.3.5] — 2026-07-18 — LOD Auditor: real in-editor auto-fixes for Mesh findings

### Added
- **The client now applies real Mesh fixes in-editor** instead of skipping
  them. 1.3.4 correctly stopped showing a Fix button on findings nothing
  could apply; this release implements the actual editor operations behind
  the Core's Mesh recommendations, so those findings are fixable rather than
  advisory:
  - **Property fixes (undoable + Revert-able via the fix Journal):**
    - `nanite_enabled` → enable Nanite on the mesh (LD012).
    - `fallback_percent` → set the Nanite fallback triangle percentage (LD013).
    - `complex_as_simple` → switch collision off "complex as simple" to
      Simple-and-Complex (LD011).
  - **Structural fixes (undoable via Ctrl+Z within the session; recorded in
    History, not Journal-revertible since they rewrite geometry):**
    - `lod_count` (`>= N` / `<= N` / `N`) → generate or reduce the LOD chain
      to the target level count with a clean halving reduction ladder
      (LD001 / LD004 / LD005).
    - `lods` / `triangle_ratio_band` → regenerate the existing chain with a
      proper monotonic reduction (LD002 / LD006).
    - `screen_sizes` → apply the recommended LOD screen-size ladder (LD007).
    - `has_simple_collision` → generate a simple collision hull (18-DOP) when
      a placed mesh has none (LD011).
  - Routed through the `UStaticMeshEditorSubsystem`; structural ops share the
    same Fix / Fix Selected / Fix All buttons as the property fixes.
- Recommendation **lists** from the Core (e.g. the LD007 screen-size ladder)
  now reach the fixer — the client serialises array recommendations to JSON
  instead of dropping them.

### Notes
- **Materials remain advisory by design.** The Core's auto-fixable Material
  rules recommend graph-level changes — reduce sampler/instruction/node
  counts, dedupe texture samplers, drop unused usage flags — that cannot be
  applied safely as an automated property write (they require editing the
  material graph, or reference analysis the client does not perform). Those
  findings continue to show guidance without a Fix button. This is a real
  limitation of what can be automated, not a skipped case.
- Verified with UAT BuildPlugin (UE_5.7, BUILD SUCCESSFUL). Added the
  `StaticMeshEditor` module dependency.

---

## [1.3.4] — 2026-07-18 — LOD Auditor: only offer "Fix" where a fix can actually be applied

### Fixed
- **"Auto-fix failed / This finding has no auto-applicable texture
  size/compression change" on Mesh and Material findings.** 1.3.3 routed the
  Fix button to the in-place registry, but the button was still *shown* on
  every finding the server marked `auto_fixable` — and the Core marks many
  mesh/material rules auto-fixable with **advisory** recommendations
  (`sampler_count: <= 8`, `lod_count: >= 2`, `nanite_enabled: true`, a prose
  compression hint) that map to **no** editable asset property. Clicking Fix
  on those still fell through to the texture path and failed. In particular
  **no material rule emits an applicable property** (`two_sided` / `blend_mode`),
  so every material Fix button was guaranteed to fail.
  - The Fix button is now gated on a new value-aware check,
    `FShintLodFixerRegistry::IsAutoApplicable`, which returns true only when a
    recommended key maps to a real property **and** its value is
    machine-applicable (a resolvable enum name, a positive number, or a bool
    key). Advisory findings no longer show a Fix button — their guidance is
    still shown; they simply require a manual/structural change (regenerate
    LODs, reduce material complexity) the editor can't automate.
  - Findings that *are* applicable (recompute normals/tangents, lightmap-UV
    generation on meshes; compression, size, sRGB, LOD group, never-stream on
    textures) fix in place as before, wrapped in the undoable Transaction +
    Journal.
  - The single-row, "Fix Selected", and "Fix All" paths all share the same
    check; a clicked-but-already-optimal asset now reports "Already optimal"
    instead of doing nothing, and a genuinely non-automatable finding reports
    "Manual fix required" instead of the misleading texture error.
  - Verified with UAT BuildPlugin (UE_5.7, BUILD SUCCESSFUL).

---

## [1.3.3] — 2026-07-18 — LOD Auditor: fix "Fix" button on Mesh/Material findings

### Fixed
- **Applying a fix on a Mesh or Material finding always reported "Auto-fix
  failed".** Every row's Fix button (in all three tabs — Textures, Meshes,
  Materials — plus the batch "Fix Selected") was wired to
  `ApplyLodFixDuplicate`, a texture-only flow that duplicates the asset with
  the recommended size/compression applied and leaves the original
  untouched. Mesh and material recommendations (recompute normals, two-sided,
  blend mode, …) have no such duplicate concept, so that path rejected them
  unconditionally with "This finding has no auto-applicable texture
  size/compression change" — shown to the user as a flat failure. The
  in-place fixer (`FShintLodFixerRegistry`, full Transaction + Journal
  revert support) already handled meshes and materials correctly, but was
  only reachable from the separate "Fix All" button — never from the
  per-row Fix button most users actually click. `OnLodFixRow` and
  `OnLodFixSelected` now check `FShintLodFixerRegistry::CanApply` first and
  dispatch there for mesh/material (and any non-size/compression texture)
  findings, falling back to the non-destructive duplicate flow only for the
  texture size/compression case it was built for. Verified with UAT
  BuildPlugin (UE_5.7, BUILD SUCCESSFUL).

---

## [1.3.2] — 2026-07-15 — LOD Auditor: batched scan (fixes crash on large projects)

### Fixed
- **LOD Auditor no longer crashes on large projects.** `AuditLods` used to load
  every mesh/texture/material under `/Game` at once and POST them in a **single**
  request — on a project with hundreds/thousands of assets the editor ran out of
  memory building and serialising the giant payload, and the one body routinely
  exceeded the 90 s request timeout. The scan now runs in **batches of 150
  assets**, chained through the async request completions: because the editor's
  GC reclaims each batch's loaded assets before the next batch loads, memory
  stays flat regardless of project size, and no single request is large enough
  to time out. Findings and KPIs are aggregated across batches and delivered in
  the same single `OnLodAuditComplete` the panel already waits on — the batching
  is invisible to the UI. A hard HTTP failure on any batch aborts the audit with
  that error instead of returning partial results that look complete. Mirrors the
  Unity client's `SCAN_BATCH` design.

## [1.3.1] — 2026-07-15 — Unity parity: file counting + asset type icons

### Fixed
- **Code Validator FILES now counts distinct files with issues**, matching the
  Unity client (whose `DynamicToolPanel` keys a dict by each issue's path and
  shows its Count). It previously showed `FilesScanned` from the Core — every
  file the scanner *looked at* — so UE5 headlined a much larger number than
  Unity for the same project. The Asset Namer already counted distinct assets;
  both panels now share one `CountUniqueFiles` helper.

### Added
- **Asset-type icon in each Asset Namer row**, left of the type name — visual
  parity with the Unity client. Resolved from the editor's registered
  `ClassIcon.<Class>` brushes using the asset type the row already carries, so
  it costs a style lookup: no AssetRegistry query, no UClass resolution, no
  asset load even when a scan flags tens of thousands of rows. Unknown types
  fall back to the generic class icon.

## [1.3.0] — 2026-07-15 — LOD Auditor client complete (auto-fix + 5-view UX + deep-scan collectors)

### Added
- **In-place auto-fix engine** (`FShintLodFixerRegistry` + `UShintLodAutoFixLibrary`):
  applies a finding's `recommended` settings directly to the asset inside a
  `FScopedTransaction` (LOD count/reduction, build settings flags, Nanite,
  texture LOD bias/group, material flags). `ApplyAllFixes` takes a confidence
  floor (`high|medium|low`); every apply is journaled to
  `Saved/ShintTools/lod_fix_journal.jsonl` (`FShintLodFixJournal`) and is
  revertible per-entry from the UI.
- **Five-view LOD Auditor UX** (`SWidgetSwitcher`): Summary (KPIs + VRAM
  treemap), Assets, Rules (grouped by rule id, severity-ranked), Fixes
  (journal + apply/revert), Budgets — with a custom squarified-treemap Slate
  widget (`SShintTreemap`).
- **Deep-scan collectors (Contract v2 complete):** per-material `shader_stats`
  (instruction count + texture fetch count), per-UV-channel `overlap_ratio`
  (128² grid rasterisation) and `texel_density_avg`/`texel_density_cv`
  (channel 0, from dominant texture resolution), and top-level
  `internal_face_ratio` (parity ray-cast, Möller–Trumbore, `ParallelFor`).

### Notes
- All new files/symbols registered in the tier strip (`lod` module) — Studio-only;
  indie/marketplace trees stay clean.
- Build-verified with UAT BuildPlugin (UE 5.7, SharedPCH).

## [1.2.6] — 2026-07-13 — LOD audit commandlet (CI/CD)

### Added
- **Headless LOD audit commandlet** for build pipelines (`UShintLodAuditCommandlet`):
  ```
  UnrealEditor-Cmd <Project>.uproject -run=ShintLodAudit \
      -profile=<default|mobile> [-deep] -failon=<error|warning> \
      [-json=<path>] [-csv=<path>]
  ```
  Reuses `FShintCoreClient::AuditLods` against the same local Core the editor
  panel talks to (CI images start it via the shipped docker compose), pumps
  HTTP to completion, writes the JSON artifact (client-metadata block + summary
  + flattened findings) and/or CSV, and returns a CI-gateable exit code:
  **0** clean · **1** findings at/above `-failon` · **2** infrastructure error
  (Core unreachable / timeout) — the last is distinct from findings so CI can
  retry instead of failing the build. Studio-only (stripped from
  indie/marketplace with the rest of the LOD module).

### Notes
- Scope is `/Game` this round (`-scope=` path filtering, `-applyfixes`, and the
  Perforce/BuildGraph wrappers from §20.7 remain for a later phase).

## [1.2.5] — 2026-07-13 — Deep Scan cache

### Added
- **Deep Scan result cache.** Computed per-mesh Deep Scan fields are persisted
  in `Saved/ShintTools/lod_scan_cache.json`, keyed by the mesh render data's
  `DerivedDataKey` (the DDC content hash — changes only when the source mesh or
  its build settings change). A repeat Deep Scan now skips the
  `FMeshDescription` load + geometry/UV recompute for every unchanged mesh, so
  only assets edited since the last scan pay the cost. Best-effort: any cache
  I/O or parse failure degrades cleanly to a full recompute.

## [1.2.4] — 2026-07-13 — Deep Scan: per-UV-channel stats

### Added
- **Deep Scan now emits per-UV-channel stats**, activating more UV rules:
  - `outside_unit_ratio` (LW009) — fraction of UV corners outside the 0–1 box;
  - `packing_efficiency` (LW006) — used UV triangle area ÷ UV bounding-box area;
  - `island_count` (LW005) — UV shells via union-find over UV-welded corners.
  Emitted per channel in `uv_channels[]`. Deliberately left at defaults (so
  their rules keep abstaining until a later phase): `overlap_ratio` (needs UV
  rasterisation) and `texel_density_avg`/`texel_density_cv` (need the dominant
  texture's resolution).

## [1.2.3] — 2026-07-13 — Deep Scan (mesh geometry integrity) + toolbar cleanup

### Added
- **Deep Scan (opt-in).** A "Deep Scan" toggle on the Asset Optimizer scan
  bar. When on, each mesh's source `FMeshDescription` is loaded to compute the
  geometry-integrity and normal fields the fast scan can't see, activating the
  rule families that previously always abstained:
  - `degenerate_triangle_count` (LG004), `duplicate_vertex_count` (LG005),
    `overlapping_vertex_count` (LG006) — the last two via a two-resolution
    spatial-hash grid (fine grid = weldable exact duplicates; coarse grid =
    near-coincident);
  - `non_manifold_edge_count` (LG007), `open_edge_count` (LG008) from the
    edge→triangle adjacency;
  - a `normal_stats` sub-object (LN001–LN006): has_normals, zero/NaN normal
    counts, has_tangents, mirrored_tangent_ratio, hard_edge_ratio.
  Meshes with no source description (cooked-only) are skipped and their rules
  abstain, exactly as before — Deep Scan never changes fast-scan behaviour.
  `MeshDescription` + `StaticMeshDescription` added to the build.

### Removed
- **"Fix All" button** on the Asset Optimizer toolbar (per-row Fix and the
  checked-rows bulk "Fix (N)" remain).

### Notes
- Deep Scan runs synchronously on the game thread this round. Deferred to a
  later phase: the worker-thread pool + scan cache (§20.2),
  `internal_face_ratio` (LG009 hull ray-cast), and per-UV-channel
  overlap/stretch/texel-density stats (LW islands).

## [1.2.2] — 2026-07-13 — Per-family tables + selection controls (Asset Optimizer)

### Added
- **Dedicated table layouts per asset type.** Each tab now renders its own
  columns instead of reusing the texture layout with dashes:
  - *Textures* — GROUP · RESOLUTION · FORMAT · CURRENT SIZE · POTENTIAL SIZE ·
    SAVINGS (unchanged);
  - *Meshes* — TYPE (Static/Skeletal) · TRIANGLES · RENDER PATH (Nanite /
    LOD ×N) · EST. SAVINGS;
  - *Materials* — TYPE (Master/Instance) · BLEND MODE · INSTRUCTIONS ·
    EST. SAVINGS (shader instructions or MB).
  The header rebuilds on tab switch; rows render only cells meaningful for
  the family.
- **Select All / Deselect All** buttons on the Asset Optimizer toolbar —
  scoped to the current tab's visible rows, driving the checked-rows
  "Fix (N)" bulk action.
- **Scan-complete toast** with the per-family issue breakdown
  (Tex · Mesh · Mat).
- **Per-tab empty states** — after a scan, an empty tab says "No material
  findings — materials look clean" instead of the pre-scan prompt.
- CSV export gains `Detail` (triangles / instruction count) and
  `Saving_Instr` columns.

### Fixed
- The Scan button is disabled while a scan is in flight (double-click fired
  two overlapping audits).

## [1.2.1] — 2026-07-11 — Materials scanning fixed + Asset Optimizer polish

### Fixed
- **Materials tab no longer comes back empty after a scan.** Every material
  rule gated on fields the collector never sent — `used_by_primitives`
  (LR002/LR006/LR008 + LM003 need a consumer count; absent → 0 → abstain),
  `instruction_count` (LM001/LR005), `texture_samples` (LM002) — so the Core
  legitimately returned zero material findings. The collector now fills:
  `used_by_primitives` (Asset Registry hard-referencer count), compiled
  pixel/vertex instruction counts via `UMaterialEditingLibrary::GetStatistics`
  (the same numbers the Material Editor stats panel shows;
  `FMaterialStatsUtils::GetRepresentativeInstructionCounts` is not exported
  and fails to link), `texture_samples` from the material graph's texture
  nodes, `graph_node_count`, `static_switch_count` (+ permutation estimate)
  and `layer_count` (master materials only, so instances don't duplicate
  their parent's findings).

### Added
- **Per-family table columns.** GROUP / FORMAT / RESOLUTION are no longer
  texture-only: meshes show Static/Skeletal · Nanite-or-LOD-count · LOD0
  triangle count; materials show Master/Instance · blend mode · compiled
  instruction count.
- **Materials in the KPI breakdowns.** FILES and ISSUES subtitles now split
  Tex / Mesh / Mat.
- **"Fix All (N)" button** on the Asset Optimizer toolbar — applies every
  auto-applicable fix in the current tab without ticking rows. N counts only
  findings the duplicate-fix flow can actually apply, so it never spams
  errors for server-fixable-but-client-inapplicable rules.

### Changed
- **Button style unified on the LOD Auditor language across Deep Code and
  Asset Naming Bot** — flat Surface secondary buttons (Select All, Deselect
  All, Fixable Only), default+white primary buttons (Fix all, Send to
  Dashboard, Scan) — consistent padding (12,6), fonts and no per-button icon
  colors.
- Removed the decorative "Studio" badge chip above the Asset Optimizer KPI
  tiles (tier gating is enforced functionally, not decoratively).
- Replaced deprecated 5.7 API uses flagged by the build (`NaniteSettings`
  direct access → `GetNaniteSettings()`, `GetUsedTextures`/
  `GetMaterialResource` feature-level overloads → 5.7 signatures).

## [1.2.0] — 2026-07-11 — LOD Auditor Contract v2 fast-scan collection (Studio)

### Added
- **Contract v2 fast-scan fields in the LOD audit payload** (first client slice
  of TDD Part 2 §20; Core 2.1.0 ships the 111-rule engine). The collectors in
  `ShintCoreClient_Lod.cpp` now send, per asset:
  - *Meshes* — LOD0 `triangle_count`/`vertex_count`, per-LOD `section_count` +
    `uv_channel_count` (LD008/LD009), `lightmap_uv_index` (LW rules),
    `nanite_enabled` + `nanite_fallback_triangle_percent` +
    `used_material_blend_modes` (LD012/LD013), `import_uniform_scale` +
    `import_scale_nonuniform` (LG014), and a `collision` sub-object built from
    `UBodySetup::AggGeom` (LD011).
  - *Textures* — `mip_count` (LT009), inferred semantic `usage`
    (LT009/LT010/LT013/LT016), `size_kb` for the streaming-pool total (LT015).
  - *Materials* — `two_sided` (LR006), `is_decal` (LR005), `shading_model`;
    `blend_mode` now uses the core taxonomy strings (Opaque/Masked/…) instead
    of raw `BLEND_*` enum names.
  Every field is additive and optional — Core rules abstain when a field is
  absent, so v1 behaviour is unchanged where data isn't collected yet.
- `RenderCore` + `PhysicsCore` module dependencies (LOD render resources +
  body-setup reads).

### Notes
- Deferred to later Part-2 phases: material graph stats, `shader_stats`
  collection, Deep Scan (mesh-description) pipeline, fixer registry + Python
  auto-fix library, audit commandlet/CI, and the §21 panel sub-views.

## [1.1.7] — 2026-07-07 — Welcome dialog no longer shows on paid launcher installs

### Fixed
- **Paid launcher installs ran the whole marketplace path (Fab "get the
  launcher" promo + Core install wizard).** `SHINT_MARKETPLACE_BUILD` regressed
  from its canonical `0` to `1` in `9ae64c5` while chasing a green UE 5.7 build.
  Paid installs pull `@main` **source** and compile it directly, so a committed
  `1` made every paid build take the `#if SHINT_MARKETPLACE_BUILD` branch —
  showing the "download the launcher" dialog to users who already have it and
  re-opening the Core wizard. Restored the canonical `0`; the Fab source pack
  and the launcher's marketplace binary build still flip it to `1` themselves.
- **Generic tier welcome could also pop with the wrong tier.** It was fired from
  `SpawnShintToolsTab` on every panel open using the cached tier — but the
  license probe is async, so at first tab-spawn the tier is still the `"free"`
  default. It's now driven exclusively by the license-resolved callback and
  gated to the free tier only (paid users onboard through the launcher), which
  also prevents a double-welcome on Fab builds.

## [1.1.6] — 2026-07-07 — Streaming Issue Explain + timeout fix

### Fixed
- **"Could not reach the LLM" on paid tiers.** The synchronous `/agent/explain`
  call held the HTTP connection silent for the whole 30-45s CPU generation, and
  UE's HTTP backend aborts on ~30s of no activity (a timeout separate from the
  request total). Short (<30s) explanations slipped under it; real 30-45s ones
  tripped the abort and surfaced as "Could not reach the LLM" even though the
  core returned a valid 200. Both request paths now set `SetActivityTimeout` to
  match the total (180s for the LLM endpoints).

### Changed
- **Issue Explain now streams** via `/agent/explain/stream`. Tokens appear in the
  modal as they generate (first token in ~3-5s) instead of a 30-45s blank
  spinner, and the continuous token flow keeps the connection active so it can
  never hit the silent-generation timeout. New SSE parser + streaming transport
  (`FShintSseParser`, `SendRequestStream`); paid-only, stripped from the free
  build with the rest of the agent module.

---

## [1.1.5] — 2026-07-07 — Fab launcher welcome

### Added
- **Fab (marketplace) launcher welcome.** On the standalone Fab build
  (`SHINT_MARKETPLACE_BUILD`), a one-time welcome now funnels users to the
  platform: *"To get full access… visit shint.tools/login and start today."*
  with a **Get the Launcher** button that opens `https://shint.tools/login`.
  Shown once per project (persisted to `Saved/ShintTools/launcher_welcome.txt`),
  dismissible, and it replaces the generic tier welcome on the Fab build only.
  The in-editor Core install wizard is unchanged, so the free tier still works
  standalone. Paid and launcher-installed builds never show it.

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
