# LOD Auditor / Asset Optimizer — UE5 Client Context

> Scope: the **Unreal (UE5 plugin) client** for the LOD Auditor feature — the
> editor-side metadata collection, HTTP transport, data model, and Slate UI.
> **The Core engine (server) is out of scope** here; it is referenced only as a
> contract boundary (`POST /assets/lod/audit`). This is a **Studio-tier** feature.

The feature is branded **"Asset Optimizer"** in the UI (the LOD Auditor UI was
rebuilt to the Asset Optimizer mockup); "LOD Auditor" remains the internal /
rail name and the Core endpoint name.

---

## 1. What the client does (and does not)

**Client responsibilities (this doc):**
- Walk `/Game` via the Asset Registry and extract per-asset metadata for the
  four audited families (StaticMesh, SkeletalMesh, Texture2D, Material/MI).
- POST that metadata to the local Core at `/assets/lod/audit`.
- Parse the findings + summary back, join client-side display metadata, and
  render the Asset Optimizer dashboard (KPIs, tabs, filters, table).
- Apply per-row / bulk **auto-fixes as optimised duplicates** (never mutate the
  original asset), and **export** findings to CSV.

**Core responsibilities (NOT this doc):** the actual rule engine (LDxxx / LTxxx
/ LAxxx rules), severity assignment, VRAM/shader saving estimates, and optional
bounded LLM guidance. The client sends metadata and renders whatever findings
come back — it contains **no LOD rules of its own**.

---

## 2. File map

| File | Role |
|------|------|
| `Source/ShintTools/Public/Core/ShintCoreClient.h` | Data types (`FShintLodFinding`, `FShintLodAuditResult`), the `AuditLods` / `ParseLodAuditResponse` declarations, and `FShintCoreConfig` (base URL, api key). |
| `Source/ShintTools/Private/Core/ShintCoreClient_Lod.cpp` | **Transport + collection.** Metadata extractors per asset family, `AuditLods` (gather → POST), `ParseLodAuditResponse` (JSON → result). |
| `Source/ShintTools/Public/UI/SShintToolsPanel.h` | UI state: `FShintLodFindingItem`, `ELodTab`, all `Lod*` handler decls + member widgets/state. |
| `Source/ShintTools/Private/UI/SShintToolsPanel_Lod.cpp` | **The Asset Optimizer Slate UI:** KPI row, scan bar, tabs/filters, table + rows, handlers, auto-fix (optimised duplicate), CSV export. |
| `Source/ShintTools/Private/UI/SShintToolsPanel.cpp` | Hosts the section: `EShintDestination::LodAudit` rail entry (index 3 of Overview/Code/Assets/LodAudit/Settings) → `WrapSection(BuildLodAuditSection())`; tier badge via `FShintToolsModule::GetCachedTier()`. |

There is a separate `*_Asset.cpp` pair (Asset **Naming** Bot) — distinct feature,
not part of the LOD client.

---

## 3. End-to-end data flow

```
[Scan click]  OnAuditLodsClicked()                       SShintToolsPanel_Lod.cpp:675
   │  tier guard (Studio/Enterprise) + button → "Scanning…"
   ▼
FShintCoreClient::AuditLods(Profile, bExplainTop, OnComplete)   ShintCoreClient_Lod.cpp:168
   │  1. Asset Registry filter over /Game for StaticMesh|SkeletalMesh|Texture2D|MaterialInterface
   │  2. Load each asset (synchronous FAssetData::GetAsset), run the family extractor
   │  3. Build JSON body { project_name, engine:"unreal", api_key, profile, explain,
   │     max_explanations:5, assets:[ {asset_path, asset_type, …family fields…} ] }
   │  4. Client-side KPIs collected in the same pass: per-category counts +
   │     TotalVramBytes (sum of resident texture VRAM); MetaByPath (w/h/group/format)
   ▼
POST  {BaseUrl}/assets/lod/audit                          ShintCoreClient_Lod.cpp:268
   ▼  (Core runs the rules — out of scope)
ParseLodAuditResponse(Raw)                                ShintCoreClient_Lod.cpp:307
   │  summary{…} + results[] → FShintLodAuditResult{ Findings[] }
   │  join MetaByPath (width/height/group/format) onto each finding
   ▼
OnLodAuditComplete(Result)                                SShintToolsPanel_Lod.cpp:702
   │  PopulateLodFindingList → LodFindingItems (wrap each finding in FShintLodFindingItem)
   │  RefreshLodFilteredList (tab + filters → LodFilteredItems → list view)
   │  RefreshLodStats (KPI tiles)
   ▼
[per-row Fix] OnLodFixRow / [bulk] OnLodFixSelected → ApplyLodFixDuplicate()   :925/:937/:869
[Export]      OnLodExport → CSV to Saved/ShintTools/lod_audit_<ts>.csv         :961
```

---

## 4. Endpoint contract — `POST {BaseUrl}/assets/lod/audit`

Base URL from `FShintCoreConfig::GetBaseUrl()` — local Core, default
`http://127.0.0.1:18200` (forces IPv4; free-tier builds hardcode host/port).
`ShintCoreClient.h:397,450`.

### Request body (built in `AuditLods`, `ShintCoreClient_Lod.cpp:252`)
```jsonc
{
  "project_name": "<Config.ProjectName>",
  "engine": "unreal",
  "api_key": "<Config.ApiKeyMongo>",   // license key the LOCAL core resolves to a tier
  "profile": "default" | "mobile",      // target platform selector
  "explain": <bool>,                    // opt-in bounded LLM guidance (top-N)
  "max_explanations": 5,
  "assets": [ { "asset_path": "/Game/…", "asset_type": "…", /* family fields */ } ]
}
```

Per-family fields (extractors, `ShintCoreClient_Lod.cpp:79-161`):
- **StaticMesh** — `lod_count`, `lods[]{index,triangles,vertices,screen_size}`,
  `bounds_radius`, `material_slot_count`. (Render data guarded — half-built
  meshes still report `lod_count`.)
- **Texture2D** — `width`, `height`, `compression` (mapped to `TC_*`), `srgb`,
  `mips_enabled`, `streaming`, `lod_group`.
- **Material / MaterialInstance** — `is_material_instance`, `sampler_count`
  (unique used textures), `blend_mode`.
- **SkeletalMesh** — bare `asset_type` only (so no-LOD rule can fire; detailed
  extraction is a follow-up).

### Response (parsed in `ParseLodAuditResponse`, `ShintCoreClient_Lod.cpp:307`)
```jsonc
{
  "error": "",                          // non-empty ⇒ audit failed
  "summary": {
    "assets_audited": N, "issues_found": N, "auto_fixable": N,
    "estimated_vram_saved_mb": F, "estimated_shader_instructions_saved": N
  },
  "results": [ {
    "asset_path","rule_id","rule_name","category","severity","message",
    "guidance","ai_guidance","auto_fixable",
    "estimated_saving": { "vram_mb": F, "shader_instructions": N },
    "current":     { "vram_mb": F, "compression": "…" },
    "recommended": { "vram_mb": F, "max_texture_size": N, "compression": "…" }
  } ]
}
```
`category` values seen: `"Texture" | "Mesh" | "Material"` (drive the tabs).
`severity`: `"error" | "warning" | "info"` → UI labels `Critical | High | Low`.

---

## 5. Data model

`FShintLodFinding` (`ShintCoreClient.h:215`) — one violation. Server fields
(rule_id, category, severity, message, guidance, ai_guidance, auto_fixable,
VramMb, ShaderInstructions) **plus client-joined display fields**:
`Width/Height/Group/Format` (from the collection pass), `CurrentVramMb`,
`PotentialVramMb` (from `current`/`recommended`), and the **auto-fix targets**
`RecMaxSize` (`recommended.max_texture_size`) + `RecCompression`.

`FShintLodAuditResult` (`ShintCoreClient.h:250`) — `bSuccess`, `StatusCode`,
`ErrorMessage`, server summary counts, **client-computed KPIs**
(`TexturesAudited`, `MeshesAudited`, `MaterialsAudited`, `TotalVramMb`), and
`Findings[]`.

`FShintLodFindingItem` (`SShintToolsPanel.h:112`) — UI wrapper: the finding +
`bChecked` (row selection) + a lazily-created `FAssetThumbnail`.

`ELodTab { Textures, Meshes, Materials }` (`SShintToolsPanel.h:125`).
Module state uses the shared `EModuleState { Idle, Running, Done, Error }`.

---

## 6. UI structure (`BuildLodAuditSection`, `SShintToolsPanel_Lod.cpp:126`)

- **Title row** — "Asset Optimizer" + subtitle + a "Studio" badge.
- **KPI row** (`BuildLodKpiRow` :235) — 5 tiles: **FILES** (assets audited;
  sub "Textures: N Meshes: N"), **MEMORY IMPACT** (total resident VRAM),
  **MEMORY SAVINGS** (MB + "% Reduction"), **FRAME TIME SAVINGS** (heuristic —
  see §8), **ISSUES** (count + per-category sub).
- **Scan bar** — Scan button + target-platform `SComboButton` (DESKTOP→`default`
  / MOBILE→`mobile`, sets `LodProfile`).
- **Toolbar** (`BuildLodToolbar` :291) — Textures/Meshes/Materials tabs; filter
  row: search (asset path), Group / Format / Severity combos (distinct values
  from current findings), **Export**, **bulk Fix (N)**.
- **Table** (`BuildLodResultsPanel` :481, `GenerateLodFindingRow` :510) — columns
  `[checkbox] ASSET(thumb+name+path) · GROUP · RESOLUTION · FORMAT · CURRENT SIZE
  · POTENTIAL SIZE · SAVINGS(MB+%) · SEVERITY · RECOMMENDATION · [Fix]`.
  Shared `LodCol::*` fill-weights keep header/rows aligned. Per-row **Fix**
  button shows only when `auto_fixable`. Empty state until a scan runs.

Filtering pipeline: `RefreshLodFilteredList` (:748) applies tab category +
search + group/format/severity → `LodFilteredItems` → `SListView`.

---

## 7. Auto-fix — optimised duplicate (never mutate original)

`ApplyLodFixDuplicate` (`SShintToolsPanel_Lod.cpp:869`):
1. Resolve the recommended compression (`LodMapRecCompression`, :842) and
   `RecMaxSize`; if neither is applicable → error out (nothing auto-applicable).
2. Load the source `UTexture2D`, `DuplicateAsset` as `<Name>_Optimized` in the
   same package path (fails cleanly if it already exists).
3. Apply `MaxTextureSize` and/or `CompressionSettings`, `PostEditChange()` to
   rebuild platform data, then `SavePackage`.
4. Return the new asset path — **the original is never modified.**

`OnLodFixRow` (:925) does one; `OnLodFixSelected` (:937) iterates checked rows
and reports `Ok/Failed`. Toasts via `LodShowSuccessToast` / `ShintShowErrorToast`.
Current auto-fix is **texture-only** (size/compression); mesh/material fixes are
guidance-only.

---

## 8. Client-computed vs server-provided

- **Server:** all findings, `summary` counts, `estimated_vram_saved_mb`,
  `estimated_shader_instructions_saved`, per-finding `current`/`recommended`.
- **Client:** `TotalVramMb` (Σ `CalcTextureMemorySizeEnum(TMC_AllMips)` over
  textures), per-category file counts, resolution/group/format joined onto
  findings, and the **FRAME TIME SAVINGS** tile — a **heuristic estimate**
  (`RefreshLodStats` :811): `16.67ms × (savedVRAM/totalVRAM) × 0.25`
  (texture-bandwidth weight), clearly prefixed `~`. No GPU telemetry exists
  client-side; swap in real profiling when available.

---

## 9. Export (`OnLodExport`, `SShintToolsPanel_Lod.cpp:961`)

Writes **all** findings (not just the filtered view) to
`{ProjectSaved}/ShintTools/lod_audit_<YYYYMMDD_HHMMSS>.csv` with CSV-escaping.
Columns: Asset, Rule, Category, Severity, Group, Width, Height, Format,
CurrentVRAM_MB, PotentialVRAM_MB, Saving_MB, Recommendation(=guidance).

---

## 10. Tier gating

Studio-only. Two layers:
- The rail entry (`EShintDestination::LodAudit`) is hidden for non-Studio users.
- **Defensive guard** in `OnAuditLodsClicked` (:679): if the cached tier
  (`FShintToolsModule::GetCachedTier`) is a known non-Studio/Enterprise value,
  it shows an "Asset Optimizer requires Studio" toast and never fires the
  request (the Core would 403 anyway). Empty tier (probe not yet resolved) is
  allowed through to avoid a false lock during boot.

The tier is resolved from `ApiKeyMongo` (the license key the local Core maps to
a tier) via the startup `/license/status` probe.

---

## 11. Rule IDs (server contract, referenced in client comments)

Client comments cite these server rules (the client does not implement them):
`LD001` (lod_count<2), `LD003`, `LA002` (skeletal no-LOD), `LT001`/`LT007`
(compression), `LT003` (oversized). The `recommended.max_texture_size` /
`recommended.compression` fields are what the client's texture auto-fix consumes.

---

## 12. Known gaps / follow-ups

- **SkeletalMesh** sends only type + path (enables the no-LOD rule); detailed
  LOD/triangle extraction is pending.
- **Auto-fix is texture-only** (size + compression duplicate); mesh/material
  findings are guidance-only.
- **FRAME TIME SAVINGS** is a heuristic pending real profiling telemetry.
- Unity has a separate client with a different request contract
  (`assets[].asset_path` + `engine:"unity"`) — do not conflate with this UE5
  path (`assets[].asset_path` + `engine:"unreal"`).

---

*Generated as a client-side reference for the ShintTools UE5 plugin
(`u:/ShintTools_UE5`). Line references are against the working tree at authoring
time; treat them as anchors, not guarantees.*
