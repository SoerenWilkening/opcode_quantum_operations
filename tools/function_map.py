#!/usr/bin/env python3
"""Generate and verify the repository's function-location map.

The map is intentionally derived from the source tree.  Keeping the parser
small makes it usable in a fresh checkout without third-party Python packages;
the output is a navigation aid, not a substitute for the compiler.
"""

from __future__ import annotations

import argparse
import os
import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "FUNCTION_MAP.md"
SOURCE_ROOTS = ("src", "include", "shim", "tests", "tools", "cmake")
SUFFIXES = {".c", ".h", ".inc", ".py", ".sh", ".cmake"}
SPECIAL_NAMES = {"CMakeLists.txt"}
SKIP_DIR_NAMES = {"__pycache__", "qtg", "build", "build-debug", "build-release", "build-asan", "build-qec"}
CONTROL_NAMES = {
    "catch",
    "for",
    "foreach",
    "if",
    "switch",
    "while",
}
C_TOKEN_RE = re.compile(
    r"//[^\n]*|/\*[\s\S]*?\*/|\"(?:\\.|[^\"\\])*\"|'(?:\\.|[^'\\])*'"
)


def source_files() -> list[Path]:
    files: list[Path] = []
    for root_name in SOURCE_ROOTS:
        root = ROOT / root_name
        if not root.is_dir():
            continue
        for directory, dirnames, filenames in os.walk(root):
            dirnames[:] = sorted(name for name in dirnames if name not in SKIP_DIR_NAMES)
            for filename in sorted(filenames):
                path = Path(directory) / filename
                if path.suffix in SUFFIXES or path.name in SPECIAL_NAMES:
                    files.append(path)
    return sorted(files)


def strip_c_comments_and_literals(text: str) -> str:
    """Blank comments and literals while preserving offsets and line breaks."""

    def blank(match: re.Match[str]) -> str:
        return re.sub(r"[^\n]", " ", match.group(0))

    return C_TOKEN_RE.sub(blank, text)


def line_number(text: str, offset: int) -> int:
    return text.count("\n", 0, offset) + 1


def macro_info(text: str) -> tuple[set[str], dict[str, tuple[str, int]]]:
    """Return function-like macro names and macros that define a function."""

    names: set[str] = set()
    function_macros: dict[str, tuple[str, int]] = {}
    lines = text.splitlines(keepends=True)
    offset = 0
    for index, line in enumerate(lines):
        match = re.match(r"\s*#\s*define\s+([A-Za-z_]\w*)\s*\(([^)]*)\)", line)
        if not match:
            offset += len(line)
            continue
        macro = match.group(1)
        names.add(macro)
        body = line[match.end() :]
        end = index
        while body.rstrip().endswith("\\") and end + 1 < len(lines):
            end += 1
            body += lines[end]
        params = [p.strip() for p in match.group(2).split(",")]
        for param in params:
            if param and re.search(rf"\b{re.escape(param)}\s*\(", body):
                function_macros[macro] = (param, offset + match.start(1))
                break
        offset += sum(len(lines[i]) for i in range(index + 1, end + 1)) + len(line)
    return names, function_macros


def c_functions(
    text: str, macro_names: set[str], function_macros: dict[str, str]
) -> list[tuple[str, int]]:
    clean = strip_c_comments_and_literals(text)
    found: list[tuple[str, int]] = []
    identifier = re.compile(r"[A-Za-z_]\w*")
    delimiters = ";{}"
    parens: list[int] = []
    matching_open: dict[int, int] = {}
    for index, char in enumerate(clean):
        if char == "(":
            parens.append(index)
        elif char == ")" and parens:
            matching_open[index] = parens.pop()
            continue
        elif char != "{":
            continue

        brace = index
        close = brace - 1
        while close >= 0 and clean[close].isspace():
            close -= 1
        if close < 0 or clean[close] not in ")":
            continue
        opening = matching_open.get(close)
        if opening is None:
            continue

        name_end = opening - 1
        while name_end >= 0 and clean[name_end].isspace():
            name_end -= 1
        window_start = max(0, name_end - 128)
        matches = list(identifier.finditer(clean, window_start, name_end + 1))
        match = matches[-1] if matches else None
        if not match or match.end() != name_end + 1:
            continue
        name = match.group(0)
        if name in CONTROL_NAMES:
            continue

        if name in function_macros:
            argument = clean[opening + 1 : close].strip()
            if re.fullmatch(r"[A-Za-z_]\w*", argument):
                found.append(
                    (
                        f"{argument} (via {name})",
                        line_number(text, clean.find(argument, opening + 1, close)),
                    )
                )
            continue
        if name in macro_names:
            continue

        start = max(
            [clean.rfind(char, 0, match.start()) for char in delimiters]
            + [clean.rfind("\n", 0, match.start())]
        ) + 1
        signature = clean[start:match.start()]
        if "#define" in signature or "=" in signature:
            continue
        after_close = clean[close + 1 : brace].strip()
        if after_close and not after_close.startswith("__attribute__"):
            continue
        found.append((name, line_number(text, match.start())))
    return found


def python_functions(text: str) -> list[tuple[str, int]]:
    pattern = re.compile(r"^\s*(?:async\s+)?def\s+([A-Za-z_]\w*)\s*\(", re.MULTILINE)
    return [(m.group(1), line_number(text, m.start(1))) for m in pattern.finditer(text)]


def shell_functions(text: str) -> list[tuple[str, int]]:
    pattern = re.compile(
        r"^\s*(?:function\s+)?([A-Za-z_]\w*)\s*\(\s*\)\s*\{", re.MULTILINE
    )
    return [(m.group(1), line_number(text, m.start(1))) for m in pattern.finditer(text)]


def cmake_functions(text: str) -> list[tuple[str, int]]:
    pattern = re.compile(
        r"^\s*(function|macro)\s*\(\s*([A-Za-z_]\w*)", re.MULTILINE | re.IGNORECASE
    )
    return [
        (f"{m.group(1).lower()} {m.group(2)}", line_number(text, m.start(2)))
        for m in pattern.finditer(text)
    ]


def functions_for(
    path: Path,
    text: str,
    macro_names: set[str],
    function_macros: dict[str, str],
) -> list[tuple[str, int]]:
    if path.suffix == ".py":
        return python_functions(text)
    if path.suffix == ".sh":
        return shell_functions(text)
    if path.suffix == ".cmake" or path.name == "CMakeLists.txt":
        return cmake_functions(text)
    return c_functions(text, macro_names, function_macros)


def relative(path: Path) -> str:
    return path.relative_to(ROOT).as_posix()


def render() -> str:
    texts = [
        (path, path.read_text(encoding="utf-8", errors="replace"))
        for path in source_files()
    ]
    macro_names: set[str] = set()
    function_macros: dict[str, str] = {}
    for _, text in texts:
        names, functions = macro_info(text)
        macro_names.update(names)
        function_macros.update({name: param for name, (param, _) in functions.items()})

    rows: list[tuple[str, list[tuple[str, int]]]] = []
    total = 0
    for path, text in texts:
        functions = functions_for(path, text, macro_names, function_macros)
        if functions:
            rows.append((relative(path), functions))
            total += len(functions)

    lines = [
        "# Function map",
        "",
        "<!-- Generated by tools/function_map.py. Do not edit by hand. -->",
        "",
        "This is the source-navigation index for the owned code under `src/`,",
        "`include/`, `shim/`, `tests/`, `tools/`, and `cmake/`. Vendored code under",
        "`third_party/` and build products are intentionally outside the map.",
        "Generated shim files are included because they are part of the checked-in",
        "ABI surface; their definitions remain generated and must be changed through",
        "their generator inputs.",
        "",
        "Agents must consult this map before searching for a function. After adding,",
        "removing, or moving a function, regenerate it with:",
        "",
        "```sh",
        "python3 tools/function_map.py",
        "```",
        "",
        "`make lint` checks that the checked-in map is current. Line numbers point to",
        "the definition (or the build-language declaration) in the listed file.",
        "",
        f"**Indexed definitions:** {total}",
        "",
    ]
    for path, functions in rows:
        lines.extend([f"## `{path}`", "", "| Function | Line |", "|---|---:|"])
        for name, line in functions:
            lines.append(f"| `{name}` | [{line}]({path}:{line}) |")
        lines.append("")
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--check",
        action="store_true",
        help="fail if FUNCTION_MAP.md differs from the generated output",
    )
    args = parser.parse_args()
    generated = render()
    if args.check:
        actual = OUTPUT.read_text(encoding="utf-8") if OUTPUT.exists() else ""
        if actual != generated:
            print(f"function_map: FAIL {OUTPUT} is stale", file=sys.stderr)
            print("function_map: run `python3 tools/function_map.py`", file=sys.stderr)
            return 1
        print("function_map: OK — FUNCTION_MAP.md is current")
        return 0
    OUTPUT.write_text(generated, encoding="utf-8")
    print(f"function_map: wrote {OUTPUT.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
