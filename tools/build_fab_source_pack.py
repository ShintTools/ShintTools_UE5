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

Usage (from the plugin repo root)::

    python tools/build_fab_source_pack.py

Output: dist_fab/ShintTools-UE5-Fab-Source-UE_<X.Y>.zip
"""

from __future__ import annotations

import json
import shutil
import tempfile
import zipfile
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
PLUGIN_NAME = "ShintTools"

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


def build() -> Path:
    stage = Path(tempfile.mkdtemp(prefix="shint_fab_"))
    plugin = stage / PLUGIN_NAME
    plugin.mkdir(parents=True)

    try:
        for name in _KEEP:
            src = REPO / name
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

        # Fab compiles the source -> the plugin is not pre-installed.
        uplugin = plugin / "ShintTools.uplugin"
        data = json.loads(uplugin.read_text(encoding="utf-8"))
        data["Installed"] = False
        uplugin.write_text(json.dumps(data, indent=4), encoding="utf-8")

        engine = str(data.get("EngineVersion", "5.7.0"))
        short = ".".join(engine.split(".")[:2]) or "5.7"

        out_dir = REPO / "dist_fab"
        out_dir.mkdir(exist_ok=True)
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
        return out
    finally:
        shutil.rmtree(stage, ignore_errors=True)


if __name__ == "__main__":
    build()
