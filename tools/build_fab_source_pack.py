# Copyright 2026 ShintTools. All Rights Reserved.
"""Build a Fab-ready SOURCE submission pack for the ShintTools UE5 plugin.

Fab reviews ShintTools as a Code Plugin and compiles the source itself, so
the submission must ship human-readable source WITHOUT any locally generated
Binaries / Build / Intermediate / Saved folders, with the standard
Public/Private module layout, a Documentation/ folder declared in
Config/FilterPlugin.ini, and the .uplugin marked ``Installed: false`` (Fab
compiles, so the plugin is not pre-installed).

This is intentionally a NO-COMPILE packer: it just stages the cleaned source
tree from the current working copy and zips it with the plugin folder at the
zip root, e.g.::

    ShintTools/
        ShintTools.uplugin
        Config/FilterPlugin.ini
        Documentation/ShintTools_UE5_Documentation.docx
        Resources/Icon128.png ...
        Source/ShintTools/ShintTools.Build.cs
        Source/ShintTools/Public/ShintTools.h
        Source/ShintTools/Private/...

Fab requires a SEPARATE upload per supported engine version slot (5.2-5.8),
each with its own .uplugin EngineVersion matching that slot — uploading the
same file to every slot gets the submission rejected ("all download links
contain the files for version 5.2 instead of their corresponding engine
versions"). The source is verified compatible across the whole range (see
CHANGELOG "Known issues" / the 5.2-5.8 compat audit), so each zip below is
identical content with only EngineVersion rewritten per slot.

Usage (from the plugin repo root)::

    python tools/build_fab_source_pack.py

Output: dist_fab/ShintTools-UE5-Fab-Source-UE_<X.Y>.zip, one per supported
engine version (5.2 through 5.8).
"""

from __future__ import annotations

import json
import shutil
import tempfile
import zipfile
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
PLUGIN_NAME = "ShintTools"

# The Fab submission is built FROM the generated free tree (paid modules
# stripped, comments scrubbed, dev files dropped) — never from the dev
# working tree. Regenerate it with tools/build_tier_release.py.
SOURCE = REPO / "dist_tier" / "release-marketplace"

# Only these top-level entries are shipped to Fab. Everything else in the
# repo (tools/, .github/, CHANGELOG.md, README.md, .vscode/, .codegraph/,
# mempalace.yaml, .editorconfig, .gitignore, ...) is dev-only and excluded.
_KEEP = [
    "ShintTools.uplugin",
    "Source",
    "Resources",
    "Config",
    "Documentation",
]

# Generated / local folders that must never appear in the submission, even
# if a stray copy slipped into one of the kept trees (e.g. Source/.vs).
_STRIP_DIRS = {
    "Binaries", "Build", "Intermediate", "Saved", "DerivedDataCache",
    ".git", ".github", ".vs", ".vscode", ".codegraph", "__pycache__",
}

# Every engine version Fab has a separate upload slot for. Keep in sync with
# the .uplugin's minimum EngineVersion (currently 5.2.0) and the audited
# compatibility range documented in the UE5 CHANGELOG.
ENGINE_VERSIONS = ["5.2", "5.3", "5.4", "5.5", "5.6", "5.7", "5.8"]


def build() -> list[Path]:
    if not (SOURCE / "ShintTools.uplugin").is_file():
        raise SystemExit(
            "release-marketplace tree not found at "
            f"{SOURCE} — run `python tools/build_tier_release.py` first.")

    stage = Path(tempfile.mkdtemp(prefix="shint_fab_"))
    plugin = stage / PLUGIN_NAME
    plugin.mkdir(parents=True)

    try:
        for name in _KEEP:
            src = SOURCE / name
            if not src.exists():
                print(f"   WARN: '{name}' not found — skipped.")
                continue
            dst = plugin / name
            if src.is_dir():
                shutil.copytree(src, dst)
            else:
                shutil.copy2(src, dst)

        # Strip generated/local dirs anywhere under the staged plugin.
        for path in sorted(plugin.rglob("*"),
                           key=lambda p: len(p.parts), reverse=True):
            if path.is_dir() and path.name in _STRIP_DIRS:
                shutil.rmtree(path, ignore_errors=True)

        # Dev docs (README.md, refactor/roadmap notes, ...) must never ship to
        # Fab — they can leak internal architecture. Strip every *.md anywhere
        # under the staged plugin. The shipped Documentation/ uses .docx.
        for md in plugin.rglob("*.md"):
            md.unlink(missing_ok=True)

        # Strip "FAB-STRIP" regions from the staged source. A region is the
        # span between a `[FAB-STRIP-BEGIN]` and `[FAB-STRIP-END]` sentinel
        # comment (both lines removed with everything in between). This lets
        # the shared source keep launcher-managed UI (e.g. the License /
        # Dashboard API-Key Settings fields) for the launcher-distributed
        # build while the Fab submission omits it — without diverging via
        # #if. The fields' backing members/config stay declared, so the
        # remaining code compiles unchanged (the rows simply never build).
        _STRIP_BEGIN = "[FAB-STRIP-BEGIN]"
        _STRIP_END = "[FAB-STRIP-END]"
        for src in plugin.rglob("*"):
            if not src.is_file() or src.suffix.lower() not in (".cpp", ".h", ".cs"):
                continue
            text = src.read_text(encoding="utf-8")
            if _STRIP_BEGIN not in text:
                continue
            kept, stripping = [], False
            for line in text.splitlines(keepends=True):
                if _STRIP_BEGIN in line:
                    stripping = True
                    continue
                if _STRIP_END in line:
                    stripping = False
                    continue
                if not stripping:
                    kept.append(line)
            src.write_text("".join(kept), encoding="utf-8")

        # Fab compiles the source -> the plugin is not pre-installed.
        uplugin = plugin / "ShintTools.uplugin"
        data = json.loads(uplugin.read_text(encoding="utf-8"))
        data["Installed"] = False

        out_dir = REPO / "dist_fab"
        out_dir.mkdir(exist_ok=True)
        outputs: list[Path] = []

        # One zip per Fab engine-version slot: identical staged source, only
        # the .uplugin EngineVersion field differs per zip.
        for short in ENGINE_VERSIONS:
            data["EngineVersion"] = f"{short}.0"
            uplugin.write_text(json.dumps(data, indent=4), encoding="utf-8")

            out = out_dir / f"ShintTools-UE5-Fab-Source-UE_{short}.zip"
            if out.exists():
                out.unlink()

            with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as zf:
                for path in sorted(plugin.rglob("*")):
                    if not path.is_file():
                        continue
                    arc = Path(PLUGIN_NAME) / path.relative_to(plugin)
                    zf.write(path, arcname=arc.as_posix())

            print(f"OK: {out}  ({out.stat().st_size // 1024} KB)")
            outputs.append(out)

        return outputs
    finally:
        shutil.rmtree(stage, ignore_errors=True)


if __name__ == "__main__":
    build()
