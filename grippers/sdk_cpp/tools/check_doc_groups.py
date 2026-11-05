#!/usr/bin/env python3
"""Fail if a public symbol is documented but not organized into any \\ingroup.

Doxygen's WARN_IF_UNDOCUMENTED (see ../Doxyfile) already fails the build for a
symbol with *no* documentation at all -- but a symbol can carry a perfectly
good \\brief/\\param/\\return and still have no \\ingroup tag. Doxygen stays
silent about that: the symbol still gets documented, it just never appears
under Core API (or any other group) in groups.dox, so it's effectively
invisible in the generated site's navigation.

This script closes that gap by cross-referencing Doxygen's own XML output
(see GENERATE_XML in ../Doxyfile; run `doxygen Doxyfile` first) rather than
re-parsing C++ itself:

  - A member (free function, variable, enum, typedef) declared directly in a
    namespace shows up in that namespace's XML as a lightweight
    `<member refid="group__...">` stub when it has an \\ingroup -- Doxygen
    moves its full definition into the group compound. One with no \\ingroup
    keeps its full `<memberdef>` inline in the namespace file instead. That
    distinction is exactly "grouped or not."

  - A struct/class/union declared directly in a namespace always appears as
    an `<innerclass>` there regardless of grouping (that's structural, not
    documentation-driven) -- so those are instead checked by cross-referencing
    every group__*.xml's own `<innerclass>` listing.

A symbol that is deliberately not part of the documented surface is not a
failure here -- but note that Doxyfile's EXCLUDE_SYMBOLS is *not* the way to
get that: it only drops a symbol out of group listings, it still shows up as
a fully documented namespace member (with its \\brief and all), which is
exactly the "ungrouped, no page" problem this script (and the docs site's own
build) flags. Wrap the symbol in `//! \\cond DOXYGEN_EXCLUDE` / `//! \\endcond`
instead -- that removes it from Doxygen's output entirely, so there is
nothing left for this script (or the site) to trip over. See
units::kAmperesPerRegisterStep, or the free operator==/operator!= for
GripperCommand/GripperStatus, for the pattern.

Usage: python3 check_doc_groups.py [--xml-dir doxygen-xml]
(run from sdk_cpp/, after `doxygen Doxyfile`)
"""
import argparse
import sys
import xml.etree.ElementTree as ET
from pathlib import Path


def collect_grouped_class_refids(xml_dir):
    refids = set()
    for group_file in xml_dir.glob("group__*.xml"):
        root = ET.parse(group_file).getroot()
        for innerclass in root.iter("innerclass"):
            refids.add(innerclass.get("refid"))
    return refids


def check_namespace_file(xml_path, grouped_class_refids):
    root = ET.parse(xml_path).getroot()
    compound = root.find("compounddef")
    if compound is None or compound.get("kind") != "namespace":
        return []

    problems = []

    for innerclass in compound.findall("innerclass"):
        if innerclass.get("refid") not in grouped_class_refids:
            qualified = (innerclass.text or "").strip()
            problems.append(f"{qualified} (struct/class/union)")

    for sectiondef in compound.findall("sectiondef"):
        for memberdef in sectiondef.findall("memberdef"):
            qualified = memberdef.findtext("qualifiedname") or memberdef.findtext("name")
            problems.append(f"{qualified} ({memberdef.get('kind')})")

    return problems


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--xml-dir", type=Path, default=Path("doxygen-xml"))
    args = parser.parse_args()

    if not args.xml_dir.is_dir():
        sys.exit(f"{args.xml_dir}: no such directory -- run `doxygen Doxyfile` first")

    grouped_class_refids = collect_grouped_class_refids(args.xml_dir)

    ns_files = sorted(args.xml_dir.glob("namespaceRobotiq*.xml"))
    if not ns_files:
        sys.exit(f"{args.xml_dir}: no namespace XML found -- run `doxygen Doxyfile` first")

    problems = []
    for ns_file in ns_files:
        problems.extend(check_namespace_file(ns_file, grouped_class_refids))

    if problems:
        print(f"{len(problems)} symbol(s) are documented but not organized into any \\ingroup:")
        for problem in sorted(set(problems)):
            print(f"  - {problem}")
        print()
        print("Add \\ingroup <group> to its doc comment (see groups.dox) so it shows up")
        print("in the generated API reference's navigation. If it's deliberately not")
        print("part of the documented surface, wrap it in //! \\cond DOXYGEN_EXCLUDE ...")
        print("//! \\endcond instead -- EXCLUDE_SYMBOLS in the Doxyfile does NOT remove")
        print("a symbol from the docs, it only drops it from group listings.")
        sys.exit(1)

    print("Every namespace-scope symbol is grouped.")


if __name__ == "__main__":
    main()
