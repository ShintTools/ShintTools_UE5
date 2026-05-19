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


# Doxygen tags inside surviving doc comments — strip @brief, @param, @return,
# @note, etc., that occasionally slip through when an author used a /** */
# block that the BLOCK_COMMENT regex catches but a stray single-line @brief
# // remained.
_DOXYGEN_LINE = re.compile(
    r"^\s*//+\s*@(brief|param|return|returns|note|see|throws?|deprecated)\b.*$",
    re.MULTILINE,
)

# Static file-local function declarations in .cpp files. These are NOT
# reflected (UFUNCTION/UCLASS/UPROPERTY are mutually exclusive with `static`
# at file scope) so renaming them is safe. Capture the identifier so we can
# rename it consistently across the file, including its call sites.
_STATIC_FUNC = re.compile(
    r"^\s*static\s+[\w:<>&*\s]+?\b([A-Z_][A-Za-z0-9_]{3,})\s*\(",
    re.MULTILINE,
)

# Reflection macros that must never be touched — these symbols are looked
# up by name from UE5's reflection registry. If we rename a function that
# the engine resolves via reflection, Blueprints break silently at runtime.
_REFLECTED_TOKEN_GUARD = re.compile(
    r"\b(UCLASS|UFUNCTION|UPROPERTY|USTRUCT|UENUM|GENERATED_BODY|"
    r"GENERATED_UCLASS_BODY|UDELEGATE)\s*\("
)


def _rename_static_funcs(text: str, file_path: Path) -> str:
    """Rename `static T Foo(` declarations in .cpp files to opaque names.

    Skipped for headers (`.h`/`.hpp`) because callers across translation
    units may reach the symbol via the header forward declaration even
    when marked static — better to leave it alone than risk a link
    breakage. Skipped when the surrounding text contains any reflection
    macro within 200 chars before the match (defence in depth: the
    static qualifier already makes reflection impossible, but Epic's
    own samples occasionally combine the two in weird ways).
    """
    if file_path.suffix.lower() not in {".cpp", ".cc"}:
        return text

    renames: dict[str, str] = {}
    counter = [0]

    def _next_name() -> str:
        counter[0] += 1
        return f"_st{counter[0]}"

    def _replace_decl(match: re.Match) -> str:
        name = match.group(1)
        # Refuse rename if the 200 chars before the match contain a
        # reflection macro — be paranoid.
        window_start = max(0, match.start() - 200)
        window = text[window_start:match.start()]
        if _REFLECTED_TOKEN_GUARD.search(window):
            return match.group(0)
        if name not in renames:
            renames[name] = _next_name()
        return match.group(0).replace(name, renames[name], 1)

    new_text = _STATIC_FUNC.sub(_replace_decl, text)
    if renames:
        # Replace call sites too. Anchored on word boundaries so we never
        # mangle a substring of an unrelated identifier.
        for original, opaque in renames.items():
            new_text = re.sub(
                r"\b" + re.escape(original) + r"\b",
                opaque,
                new_text,
            )
    return new_text


def strip_source(text: str, file_path: Path) -> str:
    out = _BLOCK_COMMENT.sub("", text)
    out = _LINE_COMMENT.sub("", out)
    out = _DOXYGEN_LINE.sub("", out)
    out = _rename_static_funcs(out, file_path)
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
            dst.write_text(strip_source(text, src), encoding="utf-8")
            stripped += 1
        else:
            shutil.copy2(src, dst)
            copied += 1

    # Flip SHINT_FREE_TIER=0 -> SHINT_FREE_TIER=1 in the Build.cs of the
    # output tree. The line lives in PublicDefinitions and our minimal
    # textual transform is enough — we own that exact phrasing.
    build_cs = out_root / "Source" / "ShintTools" / "ShintTools.Build.cs"
    if build_cs.is_file():
        contents = build_cs.read_text(encoding="utf-8")
        flipped = contents.replace(
            'PublicDefinitions.Add("SHINT_FREE_TIER=0");',
            'PublicDefinitions.Add("SHINT_FREE_TIER=1");',
        )
        if flipped != contents:
            build_cs.write_text(flipped, encoding="utf-8")
            print("[minify_plugin] Flipped SHINT_FREE_TIER -> 1 in Build.cs")

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
