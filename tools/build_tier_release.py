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
        ],
    },
    "agent": {
        "sentinel": ("[AGENT-STRIP-BEGIN]", "[AGENT-STRIP-END]"),
        "files": [
            "Source/ShintTools/Private/Core/ShintCoreClient_Agent.cpp",
            "Source/ShintTools/Private/UI/SShintToolsPanel_Explain.cpp",
            "Source/ShintTools/Private/UI/SShintToolsPanel_Fixes.cpp",
        ],
        "symbols": [
            "ShintCoreClient_Agent", "SShintToolsPanel_Explain",
            "SShintToolsPanel_Fixes", "OnExplainIssueClicked", "AgentExplain",
        ],
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
    },
}

# Tier = which modules are STRIPPED for that release branch.
TIERS = {
    "release-indie":       ["lod"],
    "release-marketplace": ["lod", "agent", "dash"],
}

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


def build(mods: list[str], stage: Path) -> list[str]:
    """Stage a tree with ``mods`` stripped; return leak hits for those mods."""
    shutil.rmtree(stage, ignore_errors=True)
    stage.mkdir(parents=True)
    _export_source(stage)

    sentinels = [MODULES[m]["sentinel"] for m in mods]
    for m in mods:
        for rel in MODULES[m]["files"]:
            p = stage / rel
            if p.exists():
                p.unlink()
            else:
                print(f"   note: '{rel}' not present.")

    for src in stage.rglob("*"):
        if src.is_file() and src.suffix.lower() in _CODE_EXT:
            txt = src.read_text(encoding="utf-8", errors="ignore")
            if any(b in txt for b, _ in sentinels):
                src.write_text(_strip_regions(txt, sentinels), encoding="utf-8")

    symbols = [s for m in mods for s in MODULES[m]["symbols"]]
    hits: list[str] = []
    for src in stage.rglob("*"):
        if not (src.is_file() and src.suffix.lower() in _CODE_EXT):
            continue
        txt = _strip_comments(src.read_text(encoding="utf-8", errors="ignore"))
        for sym in symbols:
            if re.search(r"\b" + re.escape(sym) + r"\b", txt):
                hits.append(f"{src.relative_to(stage).as_posix()}: {sym}")
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
        targets = {f"_verify-{args.only}": [args.only]}
    else:
        targets = TIERS

    out_root = REPO / "dist_tier"
    out_root.mkdir(exist_ok=True)
    ok = True
    for name, mods in targets.items():
        stage = out_root / name
        print(f"== {name}  (strip: {', '.join(mods)}) ==")
        hits = build(mods, stage)
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
