"""Strip comments + minify the UE5 plugin source for free-tier distribution.

Reads everything under Source/, writes a slimmed copy under a target
directory. The output is still valid C++ — the user's UE5 build pipeline
compiles it like any plugin — but comments, docstrings, and consecutive
blank lines are gone. Symbols are NOT renamed because reflection (UCLASS,
UFUNCTION, UPROPERTY) depends on the original identifiers, and renaming
would break Blueprint access. The intent is "raise the bar for casual
fork", not full obfuscation.

Universal across UE5 versions: the cliente compila on their own engine,
so we don't take a version dependency the way a precompiled .dll does.

Usage:
    python tools/minify_plugin.py --out u:/ShintTools_Launcher/payload/ShintTools_UE5_Stripped
"""

from __future__ import annotations

import argparse
import re
import shutil
import sys
from pathlib import Path

# Files that must not be stripped — uplugin/build descriptors carry
# version metadata that UE5 parses verbatim.
_NEVER_STRIP_SUFFIXES = {".uplugin", ".Build.cs", ".Target.cs"}
_NEVER_STRIP_NAMES    = {"ShintTools.Build.cs"}

# Source extensions we DO process.
_STRIP_SUFFIXES = {".h", ".cpp", ".hpp", ".inl"}

# Directories that should never ship in the stripped variant: build artifacts,
# editor caches, repo metadata, dev tooling.
_EXCLUDE_DIRS = {
    ".git", ".github", ".vs", ".vscode", ".claude",
    "Binaries", "Intermediate", "DerivedDataCache", "Saved",
    "tools", "node_modules", "__pycache__",
}
# Top-level files that are repo-only, not plugin payload.
_EXCLUDE_TOP_FILES = {
    "mempalace.yaml", "CHANGELOG.md", "README.md", ".gitignore",
    ".gitattributes",
}

# Block-comment + line-comment patterns (handles strings safely enough for
# normal UE5 code — no exotic raw-string literals seen in this plugin).
_BLOCK_COMMENT = re.compile(r"/\*[\s\S]*?\*/", re.MULTILINE)
_LINE_COMMENT  = re.compile(r"//.*?$", re.MULTILINE)

# Collapse 2+ blank lines into a single blank line.
_MULTI_BLANK = re.compile(r"\n[ \t]*\n[ \t]*\n+")


def strip_source(text: str) -> str:
    out = _BLOCK_COMMENT.sub("", text)
    out = _LINE_COMMENT.sub("", out)
    out = _MULTI_BLANK.sub("\n\n", out)
    return out.rstrip() + "\n"


def should_strip(p: Path) -> bool:
    if p.name in _NEVER_STRIP_NAMES:
        return False
    for suf in _NEVER_STRIP_SUFFIXES:
        if p.name.endswith(suf):
            return False
    return p.suffix.lower() in _STRIP_SUFFIXES


def process(src_root: Path, out_root: Path) -> tuple[int, int]:
    if out_root.exists():
        shutil.rmtree(out_root)
    out_root.mkdir(parents=True, exist_ok=True)

    stripped = copied = 0
    for src in src_root.rglob("*"):
        if src.is_dir():
            continue
        rel = src.relative_to(src_root)
        # Skip anything inside an excluded directory or a repo-only top file.
        if any(part in _EXCLUDE_DIRS for part in rel.parts):
            continue
        if len(rel.parts) == 1 and rel.parts[0] in _EXCLUDE_TOP_FILES:
            continue
        dst = out_root / rel
        dst.parent.mkdir(parents=True, exist_ok=True)
        if should_strip(src):
            text = src.read_text(encoding="utf-8", errors="replace")
            dst.write_text(strip_source(text), encoding="utf-8")
            stripped += 1
        else:
            shutil.copy2(src, dst)
            copied += 1
    return stripped, copied


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--src", default=str(Path(__file__).resolve().parent.parent),
                    help="Plugin root (contains ShintTools.uplugin + Source/)")
    ap.add_argument("--out", required=True,
                    help="Output directory for the stripped plugin variant")
    args = ap.parse_args()

    src_root = Path(args.src)
    out_root = Path(args.out)
    if not (src_root / "ShintTools.uplugin").is_file():
        print(f"[minify_plugin] ShintTools.uplugin not found under {src_root}",
              file=sys.stderr)
        return 1

    stripped, copied = process(src_root, out_root)
    print(f"[minify_plugin] Stripped {stripped} source(s), copied {copied} verbatim.")
    print(f"[minify_plugin] Output: {out_root}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
