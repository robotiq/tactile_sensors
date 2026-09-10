#!/usr/bin/env python3
"""Fail if a markdown code block has drifted from the real source it claims to be.

Doxygen's \\snippet already keeps header doc-comments in sync with real,
compiled code (see EXAMPLE_PATH in ../Doxyfile) -- this script gives plain
markdown docs (outside Doxygen's reach) the same guarantee. A markdown code
fence opts in by adding an HTML-comment marker directly above it:

    <!-- snippet: quick_start.cpp qs-config -->
    ```cpp
    ...
    ```

<file> is resolved relative to --examples-dir (default sdk_cpp/examples).
<tag> must bracket a region there with a Doxygen-style `//! [tag]` marker
pair, exactly as \\snippet requires:

    //! [qs-config]
    Robotiq::ConnectionConfig config;
    ...
    //! [qs-config]

The fenced block's content must then match that region verbatim (each
side's common leading indentation is stripped first, and trailing
whitespace is ignored per line).

Every ```cpp fence in the file must have one of these markers directly
above it -- a fence with no marker at all is a failure, not something
silently skipped, so a new example can't slip in without the guarantee.
For the rare case of a genuinely illustrative, non-compilable block (an
ASCII diagram, pseudo-code), opt out explicitly instead of leaving it
unmarked:

    <!-- snippet: exempt -->
    ```cpp
    ...
    ```
"""
import argparse
import difflib
import re
import sys
from pathlib import Path

MARKER_RE = re.compile(r"^<!--\s*snippet:\s*(\S+)\s+(\S+)\s*-->\s*$")
EXEMPT_RE = re.compile(r"^<!--\s*snippet:\s*exempt\s*-->\s*$")
FENCE_OPEN_RE = re.compile(r"^```cpp\s*$")
FENCE_CLOSE_RE = re.compile(r"^```\s*$")


def dedent(lines):
    indents = [len(line) - len(line.lstrip(" ")) for line in lines if line.strip()]
    if not indents:
        return [line.rstrip() for line in lines]
    common = min(indents)
    return [line[common:].rstrip() if line.strip() else "" for line in lines]


def read_fence_body(lines, md_path, marker_line, start):
    """Read fence body lines starting right after the opening ```cpp at index `start`."""
    body = []
    j = start
    while j < len(lines) and not FENCE_CLOSE_RE.match(lines[j]):
        body.append(lines[j])
        j += 1
    if j >= len(lines):
        sys.exit(f"{md_path}:{marker_line}: fence is never closed")
    return body, j


def scan_markdown(md_path):
    """Yield ('checked', line_no, file, tag, [content lines]) for each marked fence,
    ('exempt', line_no) for each explicitly exempted fence, and
    ('missing', line_no) for each ```cpp fence with no marker at all."""
    lines = md_path.read_text(encoding="utf-8").splitlines()
    i = 0
    while i < len(lines):
        if FENCE_OPEN_RE.match(lines[i]):
            # An unmarked ```cpp fence: no marker line consumed it on the way here.
            fence_line = i + 1
            _, j = read_fence_body(lines, md_path, fence_line, i + 1)
            yield ("missing", fence_line, None, None, None)
            i = j + 1
            continue

        marker = MARKER_RE.match(lines[i])
        exempt = EXEMPT_RE.match(lines[i])
        if not marker and not exempt:
            i += 1
            continue

        marker_line = i + 1
        if i + 1 >= len(lines) or not FENCE_OPEN_RE.match(lines[i + 1]):
            sys.exit(f"{md_path}:{marker_line}: snippet marker not immediately followed by a ```cpp fence")

        if exempt:
            _, j = read_fence_body(lines, md_path, marker_line, i + 2)
            yield ("exempt", marker_line, None, None, None)
            i = j + 1
            continue

        file_, tag = marker.group(1), marker.group(2)
        body, j = read_fence_body(lines, md_path, marker_line, i + 2)
        yield ("checked", marker_line, file_, tag, body)
        i = j + 1


def extract_tagged_region(source_path, tag):
    lines = source_path.read_text(encoding="utf-8").splitlines()
    marker = f"[{tag}]"
    hits = [n for n, line in enumerate(lines) if marker in line and line.strip().startswith("//!")]
    if len(hits) != 2:
        sys.exit(f"{source_path}: expected exactly 2 '//! {marker}' markers, found {len(hits)}")
    start, end = hits
    return lines[start + 1 : end]


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("markdown_files", nargs="+", type=Path)
    parser.add_argument("--examples-dir", type=Path, default=Path("sdk_cpp/examples"))
    args = parser.parse_args()

    checked = 0
    exempt = 0
    failed = 0
    for md_path in args.markdown_files:
        for kind, marker_line, file_, tag, doc_lines in scan_markdown(md_path):
            if kind == "missing":
                failed += 1
                print(f"MISSING SNIPPET {md_path}:{marker_line}: ```cpp fence has no <!-- snippet: --> marker "
                      f"(and isn't marked <!-- snippet: exempt -->)")
                continue
            if kind == "exempt":
                exempt += 1
                continue

            source_path = args.examples_dir / file_
            if not source_path.is_file():
                sys.exit(f"{md_path}:{marker_line}: no such file {source_path}")
            real_lines = extract_tagged_region(source_path, tag)
            checked += 1

            doc_norm = dedent(doc_lines)
            real_norm = dedent(real_lines)
            if doc_norm != real_norm:
                failed += 1
                print(f"MISMATCH {md_path}:{marker_line} [{file_} {tag}] has drifted from {source_path}:")
                diff = difflib.unified_diff(real_norm, doc_norm, fromfile=str(source_path), tofile=str(md_path), lineterm="")
                print("\n".join(diff))
                print()

    print(f"checked {checked} snippet(s), {exempt} exempt, {failed} failure(s)")
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
