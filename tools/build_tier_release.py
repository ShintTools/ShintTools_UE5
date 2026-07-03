"""Generate the per-plan UE5 plugin source trees (Free / Indie / Studio).

Tier isolation is BRANCH-LEVEL: the launcher fetches the plugin as a raw branch
zipball with no strip step, so a lower tier must never even *contain* a higher
tier's source. Lower-tier trees are derived from the single source of truth
(`develop-studio`) by (a) deleting whole higher-tier module files and (b)
stripping that module's `[<MOD>-STRIP-BEGIN..END]` sentinel regions from the
shared files that wire it in.

Each paid MODULE has its own sentinel so it can be excised — and BUILD-verified —
independently (the point of the small-batch cadence):

    lod   = LOD Auditor / Asset Optimizer / Predictive   (Studio-only)
    agent = local AI agent: Explain + Fix plan            (paid: Indie+Studio)
    dash  = Send-to-Dashboard                             (paid: Indie+Studio)

Tiers are just combinations:

    Studio = full source                     -> branch `main`               (none)
    Indie  = full minus {lod}                -> branch `release-indie`
    Free   = full minus {lod, agent, dash}   -> branch `release-marketplace`

(FAB-STRIP is NOT handled here — build_fab_source_pack.py applies it when it
produces the Fab submission from release-marketplace.)

The leak-check greps each stripped tree for surviving references to a removed
module: any hit means a shared-file reference still needs a sentinel. It proves
no higher-tier *symbol* leaks, NOT that the tree compiles — a BuildPlugin pass is
still required (Slate widget trees can break in ways grep can't see).

Usage (from the UE5 repo root, on `develop-studio`):
    python tools/build_tier_release.py --only dash   # stage+leak-check ONE module
    python tools/build_tier_release.py               # stage+leak-check both tiers
    python tools/build_tier_release.py --publish      # also update LOCAL branches
Nothing is ever pushed; --publish force-updates local branches only.
"""
from __future__ import annotations

import argparse
import re
import shutil
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
SOURCE_BRANCH = "develop-studio"

# ── Paid modules: files deleted wholesale + sentinel + leak symbols ───────────
# `symbols` are MANUAL EXTRAS (class/struct/delegate/member names). The bulk of
# the leak list is AUTO-DERIVED from the module's own files at build time
# (every `Type FShintX::Method(` definition they contain) — a hand-kept list
# silently under-checks: it once missed 7 of the 12 methods a module defined.
# `endpoints` are paid REST paths that must not survive in a stripped tree
# (catches wiring the symbol pass can't see).
#
# NOTE agent ≠ Fixes: SShintToolsPanel_Fixes.cpp (fix preview / safety check /
# Apply) drives the FREE /validate/fix route and ships in every tier. The paid
# agent surface is only /agent/explain + /agent/plan (Explain modal + plan).
MODULES = {
    "lod": {
        "sentinel": ("[LOD-STRIP-BEGIN]", "[LOD-STRIP-END]"),
        "files": [
            "Source/ShintTools/Private/Core/ShintCoreClient_Lod.cpp",
            "Source/ShintTools/Private/UI/SShintToolsPanel_Lod.cpp",
        ],
        "symbols": [
            "ShintCoreClient_Lod", "SShintToolsPanel_Lod", "FShintLodFinding",
            "BuildLodAuditSection", "OnAuditLodsClicked", "LodAudit", "ELodTab",
            "ApplyLodFixDuplicate", "RefreshLodStats",
            # Types / delegates / client API stripped from the headers.
            "FShintLodFindingItem", "FShintLodFindingPtr", "FShintLodAuditResult",
            "FOnShintLodAuditComplete", "AuditLods", "ParseLodAuditResponse",
            # Panel members — auto-derivation only sees method definitions, so a
            # kept file touching one of these would slip through without them.
            "LastLodResult", "LodFindingItems", "LodFilteredItems",
            "LodThumbnailPool", "LodFindingListView", "LodState",
            "bLodExplainTop", "LodProfile", "LodActiveTab", "LodSearchText",
            "LodGroupFilter", "LodFormatFilter", "LodSeverityFilter",
            "LodFiles_Label", "LodFilesSub_Label", "LodMemImpact_Label",
            "LodMemSavings_Label", "LodSavingsPct_Label", "LodFrameTime_Label",
            "LodIssues_Label", "LodIssuesSub_Label", "LodFixSelected_Label",
            "LodAudited_Label", "LodVramSaved_Label", "AuditLodBtn",
            "AuditLodBtnLabel", "LodEmptyState",
        ],
        "endpoints": ["assets/lod"],
    },
    "agent": {
        "sentinel": ("[AGENT-STRIP-BEGIN]", "[AGENT-STRIP-END]"),
        "files": [
            "Source/ShintTools/Private/Core/ShintCoreClient_Agent.cpp",
            "Source/ShintTools/Private/UI/SShintToolsPanel_Explain.cpp",
        ],
        "symbols": [
            "ShintCoreClient_Agent", "SShintToolsPanel_Explain",
            "FShintAgentPlanStep", "FShintAgentPlanResult",
            "FShintAgentExplainResponse", "FOnShintAgentPlanComplete",
            "FOnShintAgentExplainComplete", "ParseAgentPlanResponse",
            "ExplainWindow", "ExplainResultBox", "ExplainSpinner",
            "ExplainStatusLine", "ExplainStatusIndex", "ExplainRequestId",
            "ExplainTickerHandle",
        ],
        "endpoints": ["agent/explain", "agent/plan"],
    },
    "dash": {
        "sentinel": ("[DASH-STRIP-BEGIN]", "[DASH-STRIP-END]"),
        "files": [
            "Source/ShintTools/Private/Core/ShintDashboardSync.cpp",
            "Source/ShintTools/Public/Core/ShintDashboardSync.h",
        ],
        "symbols": [
            "ShintDashboardSync", "DashboardSync", "SendCodeValidator",
            "SendAssetNaming", "OnSendCodeToDashboard", "OnSendAssetToDashboard",
            "OnCodeDashboardComplete", "OnAssetDashboardComplete",
        ],
        "endpoints": ["api/public/code-validator/analyze",
                      "api/public/naming-bot/analyze"],
    },
}

# Tier = which modules are STRIPPED for that release branch, plus how hard the
# tree is sanitised. `scrub_comments` removes ALL comments from shipped source
# (free/Fab customers must not receive developer commentary); the copyright
# header line survives (Fab requires it).
TIERS = {
    "release-indie":       {"mods": ["lod"],                  "scrub_comments": False},
    "release-marketplace": {"mods": ["lod", "agent", "dash"], "scrub_comments": True},
}

# Dev-only / confidential content that never ships in ANY generated release
# tree: CI, editor config, internal docs, security audits, and the tier/Fab
# tooling itself (it documents the strip mechanism and module boundaries).
_DEV_ONLY = ["tools", "docs", ".github", ".vscode", ".editorconfig",
             "CHANGELOG.md", "SECURITY_AUDIT.md"]

_CODE_EXT = {".cpp", ".h", ".hpp", ".cs", ".inl"}


def _export_source(dest: Path) -> None:
    """Export a clean tree of SOURCE_BRANCH into ``dest`` (tracked files only)."""
    archive = dest / "_src.tar"
    with archive.open("wb") as fh:
        subprocess.run(["git", "archive", "--format=tar", SOURCE_BRANCH],
                       cwd=REPO, stdout=fh, check=True)
    subprocess.run(["tar", "-xf", str(archive), "-C", str(dest)], check=True)
    archive.unlink()


def _strip_comments(text: str) -> str:
    """Remove /* */ and // comments — for the leak check only. A paid symbol
    mentioned solely in a comment (a file-listing, a "moved to …" note) is not a
    compilable leak, so the grep should ignore it. Heuristic: ignores the rare
    case of these tokens inside string literals, which is fine for this use."""
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.DOTALL)
    text = re.sub(r"//[^\n]*", "", text)
    return text


def _scrub_comments_shipped(text: str) -> str:
    """Remove every // and /* */ comment from SHIPPED source (free tier): no
    developer commentary or internal notes reach a marketplace customer.

    Unlike the leak-check heuristic above, this must be string-literal-aware —
    a naive regex truncates every TEXT("https://…") URL at the "//". Small
    state machine over ", ' and escapes. The leading copyright comment lines
    are preserved (Fab requires a copyright header). Raw strings R"()" are not
    handled — the codebase has none."""
    lines = text.splitlines(keepends=True)
    head, i = [], 0
    while i < len(lines) and lines[i].lstrip().startswith("//"):
        if "copyright" in lines[i].lower():
            head.append(lines[i])
        i += 1
    body = "".join(lines[i:])

    out: list[str] = []
    j, n = 0, len(body)
    while j < n:
        two = body[j:j + 2]
        if two == "//":                       # line comment → drop to newline
            k = body.find("\n", j)
            k = n if k == -1 else k
            # FAB-STRIP markers are CONSUMED downstream by
            # build_fab_source_pack.py (they delimit launcher-managed UI the
            # Fab submission omits) — they must survive the scrub.
            if "[FAB-STRIP" in body[j:k]:
                out.append(body[j:k])
            j = k
        elif two == "/*":                     # block comment → drop to */
            k = body.find("*/", j + 2)
            j = n if k == -1 else k + 2
        elif body[j] in ('"', "'"):           # literal → copy verbatim
            quote = body[j]
            out.append(body[j])
            j += 1
            while j < n:
                out.append(body[j])
                if body[j] == "\\" and j + 1 < n:
                    out.append(body[j + 1])
                    j += 2
                    continue
                if body[j] == quote:
                    j += 1
                    break
                j += 1
        else:
            out.append(body[j])
            j += 1

    scrubbed = re.sub(r"[ \t]+\n", "\n", "".join(out))   # trailing whitespace
    scrubbed = re.sub(r"\n{3,}", "\n\n", scrubbed)        # collapse blank runs
    return "".join(head) + scrubbed


# Function DEFINITIONS a module file contains: `Ret FShintX::Method(` anchored
# at column 0 so shared inline helpers merely *used* by the module (e.g.
# `SShintToolsPanel::C_BG()` inside an expression) are not claimed.
_DEF_RE = re.compile(
    r"^[A-Za-z_][\w<>,*&:\s]*?\b[FSU]Shint\w*::(~?\w+)\s*\(", re.MULTILINE)


def _auto_symbols(stage: Path, mods: list[str]) -> set[str]:
    """Derive leak symbols from the module's own files (pre-deletion): every
    method they define. A symbol a module defines can then never be missing
    from its own leak-check — the manual lists are extras, not the source of
    truth."""
    syms: set[str] = set()
    for m in mods:
        for rel in MODULES[m]["files"]:
            p = stage / rel
            if not p.exists():
                continue
            txt = _strip_comments(p.read_text(encoding="utf-8", errors="ignore"))
            syms.update(_DEF_RE.findall(txt))
    return syms


def _strip_regions(text: str, sentinels: list[tuple[str, str]]) -> str:
    """Drop every [BEGIN..END] region (inclusive) for each sentinel pair."""
    begins = {b for b, _ in sentinels}
    ends = {e for _, e in sentinels}
    out, depth = [], 0
    for line in text.splitlines(keepends=True):
        if any(b in line for b in begins):
            depth += 1
            continue
        if any(e in line for e in ends):
            depth = max(0, depth - 1)
            continue
        if depth == 0:
            out.append(line)
    return "".join(out)


def build(mods: list[str], stage: Path, *,
          scrub_comments: bool = False,
          drop_dev: bool = False) -> list[str]:
    """Stage a tree with ``mods`` stripped; return leak hits for those mods."""
    shutil.rmtree(stage, ignore_errors=True)
    stage.mkdir(parents=True)
    _export_source(stage)

    # Derive the leak-symbol set from the module files BEFORE deleting them.
    symbols = _auto_symbols(stage, mods)
    symbols.update(s for m in mods for s in MODULES[m]["symbols"])

    if drop_dev:
        for rel in _DEV_ONLY:
            p = stage / rel
            if p.is_dir():
                shutil.rmtree(p)
            elif p.exists():
                p.unlink()

    sentinels = [MODULES[m]["sentinel"] for m in mods]
    for m in mods:
        for rel in MODULES[m]["files"]:
            p = stage / rel
            if p.exists():
                p.unlink()
            else:
                print(f"   note: '{rel}' not present.")

    # Marker lines of ALL modules (kept ones included) are removed from every
    # shipped tree — a customer must not see the strip machinery in the source.
    all_markers = [t for m in MODULES.values() for t in m["sentinel"]]
    for src in stage.rglob("*"):
        if src.is_file() and src.suffix.lower() in _CODE_EXT:
            txt = src.read_text(encoding="utf-8", errors="ignore")
            changed = False
            if any(b in txt for b, _ in sentinels):
                txt, changed = _strip_regions(txt, sentinels), True
            if any(mk in txt for mk in all_markers):
                txt = "".join(ln for ln in txt.splitlines(keepends=True)
                              if not any(mk in ln for mk in all_markers))
                changed = True
            if scrub_comments:
                txt, changed = _scrub_comments_shipped(txt), True
            if changed:
                src.write_text(txt, encoding="utf-8")

    endpoints = [e for m in mods for e in MODULES[m].get("endpoints", [])]
    hits: list[str] = []
    for src in stage.rglob("*"):
        if not (src.is_file() and src.suffix.lower() in _CODE_EXT):
            continue
        txt = _strip_comments(src.read_text(encoding="utf-8", errors="ignore"))
        for sym in sorted(symbols):
            if re.search(r"\b" + re.escape(sym) + r"\b", txt):
                hits.append(f"{src.relative_to(stage).as_posix()}: {sym}")
        # Paid REST paths must not survive either — catches string-level wiring
        # (URL construction, config defaults) the symbol pass can't see.
        for ep in endpoints:
            if ep in txt:
                hits.append(f"{src.relative_to(stage).as_posix()}: endpoint '{ep}'")
    return hits


def publish(branch: str, stage: Path) -> None:
    """Force the LOCAL ``branch`` to match the staged tree (no push)."""
    wt = REPO / ".git" / "tier-wt"
    subprocess.run(["git", "worktree", "remove", "--force", str(wt)],
                   cwd=REPO, capture_output=True)
    subprocess.run(["git", "worktree", "add", "--force", "-B", branch, str(wt),
                    SOURCE_BRANCH], cwd=REPO, check=True)
    for item in wt.iterdir():
        if item.name == ".git":
            continue
        shutil.rmtree(item) if item.is_dir() else item.unlink()
    for item in stage.iterdir():
        dst = wt / item.name
        shutil.copytree(item, dst) if item.is_dir() else shutil.copy2(item, dst)
    subprocess.run(["git", "add", "-A"], cwd=wt, check=True)
    subprocess.run(["git", "commit", "-m",
                    f"build(tier): regenerate {branch} from {SOURCE_BRANCH}"],
                   cwd=wt, check=True)
    subprocess.run(["git", "worktree", "remove", "--force", str(wt)], cwd=REPO)
    print(f"   updated local branch {branch} (not pushed)")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--only", choices=list(MODULES),
                    help="strip a single module for isolated build verification")
    ap.add_argument("--publish", action="store_true",
                    help="force-update the local release branches (no push)")
    args = ap.parse_args()

    if args.only:
        # Verify trees stay minimal-delta (no scrub / no dev-drop) so a
        # BuildPlugin failure maps 1:1 onto the sentinel edits being tested.
        targets = {f"_verify-{args.only}": {"mods": [args.only],
                                            "scrub_comments": False}}
        drop_dev = False
    else:
        targets = TIERS
        drop_dev = True

    out_root = REPO / "dist_tier"
    out_root.mkdir(exist_ok=True)
    ok = True
    for name, cfg in targets.items():
        stage = out_root / name
        mods = cfg["mods"]
        print(f"== {name}  (strip: {', '.join(mods)}"
              f"{', scrubbed' if cfg['scrub_comments'] else ''}) ==")
        hits = build(mods, stage, scrub_comments=cfg["scrub_comments"],
                     drop_dev=drop_dev)
        if hits:
            ok = False
            print(f"   LEAKS ({len(hits)}) — add sentinels:")
            for h in hits[:60]:
                print(f"     - {h}")
        else:
            print(f"   clean: no stripped-module symbols survived.")
            print(f"   tree staged for BuildPlugin -> {stage}")
            if args.publish and not args.only:
                publish(name, stage)
    print("\nAll clean." if ok else "\nLeaks remain — add sentinels.")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
