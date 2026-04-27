#!/usr/bin/env python3
"""cpp_to_simc.py - Convert SimC C++ APL definitions to .simc format.

Parses the C++ add_action() format used in SimC source code
(e.g., engine/class_modules/apl/mage.cpp) and outputs standard
.simc APL files.

The parser handles:
  - //spec_apl_start / //spec_apl_end block markers
  - Fallback: void spec(player_t* p) function signatures
  - action_priority_list_t* variable -> list name mapping
  - var->add_action("text") and var->add_action("text", "comment")

Output naming: <spec>_<class>.simc  (e.g., fire_mage.simc)
Class name is auto-detected from the C++ namespace or filename.

Usage:
    python scripts/cpp_to_simc.py <input.cpp> [options]

Examples:
    python scripts/cpp_to_simc.py mage.cpp
    python scripts/cpp_to_simc.py mage.cpp --spec fire
    python scripts/cpp_to_simc.py mage.cpp --class mage -o seeds/APL
    cat mage.cpp | python scripts/cpp_to_simc.py - --class mage
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path


def detect_class_name(text: str, filename: str | None = None) -> str | None:
    """Detect class name from C++ namespace, member function, or filename.

    Priority:
    1. 'namespace <class>_apl {' (apl/ directory files)
    2. '<class>_t::init_action_list_<spec>()' member functions (sc_*.cpp files)
    3. Input filename stem, stripping 'sc_' and 'apl_' prefixes
    """
    m = re.search(r"namespace\s+(\w+)_apl\s*\{", text)
    if m:
        return m.group(1)
    # Member function pattern: class_t::init_action_list_spec()
    m = re.search(r"void\s+(\w+)_t::init_action_list_\w+\s*\(", text)
    if m:
        return m.group(1)
    if filename and filename != "-":
        stem = Path(filename).stem
        # Strip common prefixes (sc_shaman.cpp -> shaman, apl_hunter.cpp -> hunter)
        stem = re.sub(r"^(sc_|apl_)", "", stem)
        if stem:
            return stem
    return None


def extract_spec_blocks(text: str) -> dict[str, str]:
    """Extract per-spec function bodies from C++ source.

    Extraction methods (tried in order, first match wins):
    1. //spec_apl_start / //spec_apl_end block markers
    2. Member function: void class_t::init_action_list_spec() (sc_*.cpp files)
    3. Free function: void spec(player_t* p) (apl/*.cpp files)
    """
    blocks: dict[str, str] = {}

    # Method 1: Marker-based extraction
    for m in re.finditer(
        r"//(\w+)_apl_start\s*\n(.*?)//\1_apl_end", text, re.DOTALL
    ):
        blocks[m.group(1)] = m.group(2)

    if blocks:
        return blocks

    # Method 2: Member function pattern: void class_t::init_action_list_spec()
    # Used by class module files (sc_shaman.cpp, sc_hunter.cpp, etc.)
    # Only includes functions that contain get_action_priority_list calls.
    for m in re.finditer(
        r"void\s+\w+_t::init_action_list_(\w+)\s*\(\s*\)", text
    ):
        name = m.group(1)
        brace = text.find("{", m.end())
        if brace == -1:
            continue
        depth, pos = 1, brace + 1
        while pos < len(text) and depth > 0:
            if text[pos] == "{":
                depth += 1
            elif text[pos] == "}":
                depth -= 1
            pos += 1
        body = text[brace:pos]
        if "get_action_priority_list" in body:
            blocks[name] = body

    if blocks:
        return blocks

    # Method 3: Free function signature with balanced braces
    for m in re.finditer(
        r"void\s+(\w+)\s*\(\s*player_t\s*\*\s*\w*\s*\)", text
    ):
        name = m.group(1)
        brace = text.find("{", m.end())
        if brace == -1:
            continue
        depth, pos = 1, brace + 1
        while pos < len(text) and depth > 0:
            if text[pos] == "{":
                depth += 1
            elif text[pos] == "}":
                depth -= 1
            pos += 1
        blocks[name] = text[brace:pos]

    return blocks


def parse_spec_block(block: str) -> dict[str, list[tuple[str, str | None]]]:
    """Parse a single spec function body into action lists.

    Extracts variable-to-list-name mappings from get_action_priority_list
    calls, then collects all add_action calls in order.

    Returns: {list_name: [(action_text, comment_or_None), ...]}
    """
    # Map C++ variable names to SimC list names
    # Supports both p->get_action_priority_list("name") and bare
    # get_action_priority_list("name", "description") member function calls
    var_to_list: dict[str, str] = {}
    for m in re.finditer(
        r"action_priority_list_t\*\s+(\w+)\s*=\s*"
        r'(?:p->)?get_action_priority_list\(\s*"(\w+)"'
        r'(?:\s*,\s*"[^"]*")?\s*\)',
        block,
    ):
        var_to_list[m.group(1)] = m.group(2)

    # Collect add_action calls (skip C++ commented lines)
    action_lists: dict[str, list[tuple[str, str | None]]] = {}
    for m in re.finditer(
        r'(\w+)->add_action\(\s*"((?:[^"\\]|\\.)*)"\s*'
        r'(?:,\s*"((?:[^"\\]|\\.)*)")?\s*\)',
        block,
    ):
        # Skip matches inside C++ line comments
        line_start = block.rfind("\n", 0, m.start()) + 1
        line_prefix = block[line_start : m.start()].lstrip()
        if line_prefix.startswith("//"):
            continue

        var_name = m.group(1)
        action_text = m.group(2)
        comment = m.group(3)

        list_name = var_to_list.get(var_name)
        if list_name is None:
            print(
                f"  Warning: unknown variable '{var_name}', skipping",
                file=sys.stderr,
            )
            continue

        action_lists.setdefault(list_name, []).append((action_text, comment))

    return action_lists


def format_simc(
    spec_name: str,
    action_lists: dict[str, list[tuple[str, str | None]]],
    class_name: str | None = None,
    source_label: str = "",
    include_comments: bool = True,
) -> str:
    """Render parsed action lists as .simc text.

    Outputs in standard SimC format:
      actions.precombat=first_action
      actions.precombat+=/second_action
      ...
      actions=first_default_action
      actions+=/second_default_action

    Lists are ordered: precombat, default, then alphabetical.
    """
    lines: list[str] = []

    # Header
    title = spec_name.replace("_", " ").title()
    if class_name:
        title += f" {class_name.replace('_', ' ').title()}"
    lines.append(f"# {title} APL")
    if source_label:
        lines.append(f"# Extracted from SimC source: {source_label}")
    lines.append("")

    # Order: precombat first, default second, rest alphabetical
    priority = {"precombat": 0, "default": 1}
    list_names = sorted(
        action_lists.keys(), key=lambda n: (priority.get(n, 2), n)
    )

    for list_name in list_names:
        actions = action_lists[list_name]
        if not actions:
            continue

        prefix = (
            f"actions.{list_name}" if list_name != "default" else "actions"
        )

        for i, (action_text, comment) in enumerate(actions):
            if include_comments and comment:
                lines.append(f"# {comment}")
            op = "=" if i == 0 else "+=/"
            lines.append(f"{prefix}{op}{action_text}")

        lines.append("")

    return "\n".join(lines)


def main() -> None:
    p = argparse.ArgumentParser(
        description="Convert SimC C++ APL definitions to .simc format.",
        epilog=(
            "Examples:\n"
            "  python scripts/cpp_to_simc.py mage.cpp\n"
            "  python scripts/cpp_to_simc.py mage.cpp --spec fire\n"
            "  python scripts/cpp_to_simc.py warlock.cpp --class warlock\n"
            "  cat mage.cpp | python scripts/cpp_to_simc.py - --class mage"
        ),
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    p.add_argument("input", help="C++ source file (use '-' for stdin)")
    p.add_argument(
        "-o",
        "--output-dir",
        default="seeds/APL",
        help="Output directory (default: seeds/APL)",
    )
    p.add_argument("--spec", help="Extract only this spec (default: all)")
    p.add_argument(
        "--class",
        dest="class_name",
        help="Class name for filenames (auto-detected from namespace/filename)",
    )
    p.add_argument(
        "--no-comments",
        action="store_true",
        help="Strip inline APL comments from output",
    )
    p.add_argument(
        "--source-label",
        help="Source file path for header comment (default: derived from input)",
    )

    args = p.parse_args()

    # Read input
    if args.input == "-":
        text = sys.stdin.read()
        source_label = args.source_label or "stdin"
    else:
        path = Path(args.input)
        if not path.exists():
            print(f"Error: {path} not found", file=sys.stderr)
            sys.exit(1)
        text = path.read_text(encoding="utf-8")
        source_label = (
            args.source_label
            or f"engine/class_modules/apl/{path.name}"
        )

    # Detect class name
    class_name = args.class_name or detect_class_name(text, args.input)

    # Extract spec blocks
    blocks = extract_spec_blocks(text)
    if not blocks:
        print("Error: no APL blocks found in input", file=sys.stderr)
        sys.exit(1)

    if args.spec:
        if args.spec not in blocks:
            avail = ", ".join(sorted(blocks.keys()))
            print(
                f"Error: spec '{args.spec}' not found. Available: {avail}",
                file=sys.stderr,
            )
            sys.exit(1)
        blocks = {args.spec: blocks[args.spec]}

    # Write output files
    out_dir = Path(args.output_dir)
    out_dir.mkdir(parents=True, exist_ok=True)

    for spec_name, block in sorted(blocks.items()):
        action_lists = parse_spec_block(block)
        simc_text = format_simc(
            spec_name,
            action_lists,
            class_name,
            source_label,
            include_comments=not args.no_comments,
        )
        stem = f"{spec_name}_{class_name}" if class_name else spec_name
        out_file = out_dir / f"{stem}.simc"
        out_file.write_text(simc_text, encoding="utf-8")

        n_lists = len(action_lists)
        n_actions = sum(len(v) for v in action_lists.values())
        print(f"  {out_file} ({n_lists} lists, {n_actions} actions)")

    print(f"Extracted {len(blocks)} spec(s) to {out_dir}/")


if __name__ == "__main__":
    main()
