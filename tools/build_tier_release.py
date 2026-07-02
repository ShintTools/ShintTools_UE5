"""Generate the per-plan UE5 plugin source trees (Free / Indie / Studio).

Tier isolation is BRANCH-LEVEL: the launcher fetches the plugin as a raw branch
zipball with no strip step, so a lower tier must never even *contain* a higher
tier's source. This tool derives the lower-tier trees from the single source of
truth (`develop-studio`) by (a) deleting whole higher-tier module files and
(b) stripping `[STUDIO-STRIP]` / `[PAID-STRIP]` sentinel regions from the shared
files that wire those modules in.

    Studio = full source             -> branch `main`               (no strip)
    Indie  = full minus Studio-only  -> branch `release-indie`
    Free   = full minus all paid     -> branch `release-marketplace`

"Paid" = Studio-only (LOD Auditor / Predictive) + AI agent + Send-to-Dashboard.
Free additionally KEEPS the org-migration + free-core wizard (the free Core
image must reference the `ghcr.io/shinttools` ORG, never a developer name).

The most useful output without a compiler is the **leak check**: after stripping,
the staged tree is grepped for symbols that belong to deleted modules. Any hit
means a reference in a shared file still needs a `[…-STRIP]` sentinel — this is
what drives the (otherwise blind) sentinel work in the shared panel files.

Usage (from the UE5 repo root, on `develop-studio`):
    python tools/build_tier_release.py            # stage + leak-check only
    python tools/build_tier_release.py --publish  # also update the LOCAL branches
Nothing is ever pushed; `--publish` force-updates local branches only.
"""
from __future__ import annotations

import argparse
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
SOURCE_BRANCH = "develop-studio"

# ── Whole files owned by a single higher tier (deleted for lower tiers) ───────
STUDIO_ONLY_FILES = [
    "Source/ShintTools/Private/Core/ShintCoreClient_Lod.cpp",
    "Source/ShintTools/Private/UI/SShintToolsPanel_Lod.cpp",
]
PAID_FILES = STUDIO_ONLY_FILES + [
    "Source/ShintTools/Private/Core/ShintCoreClient_Agent.cpp",
    "Source/ShintTools/Private/UI/SShintToolsPanel_Explain.cpp",
    "Source/ShintTools/Private/UI/SShintToolsPanel_Fixes.cpp",
    "Source/ShintTools/Private/Core/ShintDashboardSync.cpp",
    "Source/ShintTools/Public/Core/ShintDashboardSync.h",
]

# ── Region sentinels: (BEGIN, END); both lines and everything between removed ──
S_STUDIO = ("[STUDIO-STRIP-BEGIN]", "[STUDIO-STRIP-END]")
S_PAID   = ("[PAID-STRIP-BEGIN]",   "[PAID-STRIP-END]")
S_FAB    = ("[FAB-STRIP-BEGIN]",    "[FAB-STRIP-END]")

# ── Symbols that must NOT survive in a stripped tree (leak check) ──────────────
STUDIO_SYMBOLS = [
    "ShintCoreClient_Lod", "SShintToolsPanel_Lod", "FShintLodFinding",
    "BuildLodAuditSection", "OnAuditLodsClicked", "LodAudit", "ELodTab",
    "ApplyLodFixDuplicate", "RefreshLodStats",
]
PAID_SYMBOLS = STUDIO_SYMBOLS + [
    "ShintDashboardSync", "SendCodeValidator", "SendAssetNaming",
    "ShintCoreClient_Agent", "SShintToolsPanel_Explain", "SShintToolsPanel_Fixes",
    "OnExplainIssueClicked", "AgentExplain",
]

TIERS = {
    "release-indie": {
        "delete": STUDIO_ONLY_FILES,
        "strip":  [S_STUDIO],
        "leaks":  STUDIO_SYMBOLS,
    },
    "release-marketplace": {
        "delete": PAID_FILES,
        "strip":  [S_STUDIO, S_PAID, S_FAB],
        "leaks":  PAID_SYMBOLS,
    },
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


def build_tier(branch: str, cfg: dict, stage: Path) -> list[str]:
    """Produce the stripped tree for one tier in ``stage``; return leak hits."""
    shutil.rmtree(stage, ignore_errors=True)
    stage.mkdir(parents=True)
    _export_source(stage)

    for rel in cfg["delete"]:
        p = stage / rel
        if p.exists():
            p.unlink()
        else:
            print(f"   note: '{rel}' not present (already absent?)")

    for src in stage.rglob("*"):
        if src.is_file() and src.suffix.lower() in _CODE_EXT:
            txt = src.read_text(encoding="utf-8", errors="ignore")
            if any(b in txt for b, _ in cfg["strip"]):
                src.write_text(_strip_regions(txt, cfg["strip"]), encoding="utf-8")

    # Leak check — any surviving reference to a deleted module needs a sentinel.
    hits: list[str] = []
    for src in stage.rglob("*"):
        if not (src.is_file() and src.suffix.lower() in _CODE_EXT):
            continue
        txt = src.read_text(encoding="utf-8", errors="ignore")
        for sym in cfg["leaks"]:
            if re.search(r"\b" + re.escape(sym) + r"\b", txt):
                rel = src.relative_to(stage).as_posix()
                hits.append(f"{rel}: {sym}")
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
    ap.add_argument("--publish", action="store_true",
                    help="force-update the local release branches (no push)")
    args = ap.parse_args()

    ok = True
    with tempfile.TemporaryDirectory() as tmp:
        for branch, cfg in TIERS.items():
            stage = Path(tmp) / branch
            print(f"== {branch} ==")
            hits = build_tier(branch, cfg, stage)
            if hits:
                ok = False
                print(f"   LEAKS ({len(hits)}) — add [..-STRIP] sentinels:")
                for h in hits[:40]:
                    print(f"     - {h}")
            else:
                print("   clean: no higher-tier symbols survived.")
                if args.publish:
                    publish(branch, stage)
    if not ok:
        print("\nLeaks found — resolve sentinels before --publish.")
        return 1
    print("\nAll tiers clean.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
