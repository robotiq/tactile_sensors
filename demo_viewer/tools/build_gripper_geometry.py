"""
Generate web/gripper_geometry.js from the Robotiq 2F-85 ROS description.

The 2F-85 linkage is planar: every joint origin in robotiq_2f_85_macro.urdf.xacro
has y = 0 and every joint axis is (0, -1, 0). Projecting the link meshes onto the
x-z plane therefore gives an exact side view rather than an approximation.

This reads the joint origins from the xacro and the link shapes from the *visual*
COLLADA meshes — not the collision hulls, which are coarse simplifications. The
visual meshes also split each link into material groups (grey aluminium, black
rubber), which is what lets the drawing show the rubber pad on the fingertip
instead of one undifferentiated blob.

Each (link, material) group is placed at the fully-open pose, projected, and
traced into an outline: the meshes run to tens of thousands of triangles, far too
many to dump into a web page as raw path data, and a silhouette only needs its
boundary. The projection is rasterised and the resulting mask contoured, then
simplified, giving a few hundred points per link.

Requires numpy, matplotlib and Pillow (development-only; the generated file is
committed, so the viewer needs none of them). Regenerate with:

    python3 tools/build_gripper_geometry.py \
        ~/robotiq/ros2/sandbox/src/robotiq_description
"""

import argparse
import json
import re
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
from PIL import Image, ImageDraw

MM_PER_M = 1000.0
COLLADA_NS = {"c": "http://www.collada.org/2005/11/COLLADASchema"}

# Raster pitch for outline tracing, and how far a simplified outline may stray
# from the traced one. The whole gripper is ~150 mm wide and is drawn a couple
# of hundred pixels across, so a quarter of a millimetre is already invisible.
RASTER_MM_PER_PX = 0.15
SIMPLIFY_TOLERANCE_MM = 0.25
# Drop traced loops smaller than this. The meshes carry engraved lettering (the
# ROBOTIQ logo on the base) which traces into a scatter of ~1 mm^2 specks and
# reads as noise at the size this is drawn; real features — screw holes, pivot
# bosses, recesses — are all an order of magnitude larger.
MIN_FEATURE_MM2 = 3.0

# Links that make up the 2D view, in draw order. The fingertips are separate
# because they are the one moving part.
STATIC_LINKS = [
    ("robotiq_85_base_link", "robotiq_base"),
    ("robotiq_85_left_inner_knuckle_link", "left_inner_knuckle"),
    ("robotiq_85_right_inner_knuckle_link", "right_inner_knuckle"),
    ("robotiq_85_left_knuckle_link", "left_knuckle"),
    ("robotiq_85_right_knuckle_link", "right_knuckle"),
    ("robotiq_85_left_finger_link", "left_finger"),
    ("robotiq_85_right_finger_link", "right_finger"),
]

TIP_LINKS = [
    ("robotiq_85_left_finger_tip_link", "left_finger_tip"),
    ("robotiq_85_right_finger_tip_link", "right_finger_tip"),
]

# The pad — the face that actually touches the object, and where the tactile
# array sits — is not a mesh anywhere. It is a box primitive in the older
# ros-industrial description (robotiq_2f_85_gripper_visualization,
# robotiq_arg2f_85_model_macro.xacro: "the default are the big pads with
# rubber"), reached through this joint chain from the base. The first joint
# carries rpy="0 0 pi", so every offset below it has its y negated.
PAD_CHAIN = ["finger_joint", "outer_finger_joint",
             "inner_finger_joint", "inner_finger_pad_joint"]

# The linkage is a closed five-bar, which the ROS description does not model: it
# slaves everything to the drive with mimic tags, i.e. parallel grip. The Isaac
# Sim asset carries both variants, and its "physics_compliant" configuration is
# the encompassing one — the mimic joints are replaced by real revolute joints,
# with a weak spring on inner_finger_joint (stiffness 0.0002 against the drive's
# 0.17) and inner_finger_knuckle_joint closing the loop at zero stiffness.
#
# Joints per finger, and the role each plays in the loop
#   base -P1- outer_knuckle -P2- outer_finger -P3- inner_finger -P4- inner_knuckle -P5- base
LINKAGE_JOINTS = {
    "left": {
        "knuckle": "finger_joint",                       # P1, driven
        "outerFinger": "left_outer_finger_joint",        # P2
        "distal": "left_inner_finger_joint",             # P3, the compliant one
        "coupler": "left_inner_finger_knuckle_joint",    # P4, closes the loop
        "innerKnuckle": "left_inner_knuckle_joint",      # P5
    },
    "right": {
        "knuckle": "right_outer_knuckle_joint",
        "outerFinger": "right_outer_finger_joint",
        "distal": "right_inner_finger_joint",
        "coupler": "right_inner_finger_knuckle_joint",
        "innerKnuckle": "right_inner_knuckle_joint",
    },
}

# Which mesh plays which part of the loop, so the viewer knows what to move.
LINK_ROLES = {
    "robotiq_base": ("base", None),
    "left_knuckle": ("outerKnuckle", "left"),
    "left_finger": ("outerFinger", "left"),
    "left_finger_tip": ("distal", "left"),
    "left_inner_knuckle": ("innerKnuckle", "left"),
    "right_knuckle": ("outerKnuckle", "right"),
    "right_finger": ("outerFinger", "right"),
    "right_finger_tip": ("distal", "right"),
    "right_inner_knuckle": ("innerKnuckle", "right"),
}

# COLLADA material name -> the class the viewer styles it with.
MATERIAL_CLASS = {"grey-material": "metal", "black-material": "rubber"}
# Draw order within a link: the pad is the face worth looking at, so it goes on
# top; sorting by material name would bury it instead.
CLASS_ORDER = ["metal", "rubber", "pad"]


# --- URDF ---------------------------------------------------------------------

def parse_joints(xacro_path):
    """Return {child_link: (parent_link, x, z)} for every joint in the macro.

    A regex rather than an XML parse: the file is a xacro macro whose element
    names carry a ${prefix} substitution, and only the plain numeric origins are
    needed here. Any joint whose origin has a non-zero y would break the planar
    assumption, so that is checked rather than assumed.
    """
    text = xacro_path.read_text()
    joints = {}
    pattern = re.compile(
        r'<joint name="\$\{prefix\}(?P<name>[^"]+)"[^>]*>(?P<body>.*?)</joint>',
        re.DOTALL)
    for match in pattern.finditer(text):
        body = match.group("body")
        parent = re.search(r'<parent link="\$\{prefix\}([^"]+)"', body)
        child = re.search(r'<child link="\$\{prefix\}([^"]+)"', body)
        origin = re.search(r'<origin xyz="([^"]+)"', body)
        if not (parent and child and origin):
            continue
        x, y, z = (float(v) for v in origin.group(1).split())
        if abs(y) > 1e-9:
            raise SystemExit(
                f"joint {match.group('name')} has y={y}: the linkage is not "
                "planar in the x-z plane, and this 2D projection is invalid")
        joints[child.group(1)] = (parent.group(1), x, z)
    return joints


def link_origin(joints, link, root="robotiq_85_base_link"):
    """Position of a link's frame in the base frame, at the fully-open pose.

    With every joint angle at zero the chain is a pure translation, so the
    origins simply add up.
    """
    x = z = 0.0
    while link != root:
        if link not in joints:
            raise SystemExit(f"no joint produces link {link}")
        parent, dx, dz = joints[link]
        x += dx
        z += dz
        link = parent
    return x, z


def parse_linkage(usda_path):
    """Return {side: {role: [x, y]}} — the five-bar pivots, in drawing mm.

    Isaac states each joint anchor in a frame shared by both bodies, so the
    numbers can be read straight out. Its lateral axis is y where the drawing
    uses x, and with the opposite sign; z is up in both, and the drawing flips
    it because SVG's y points down.
    """
    text = usda_path.read_text()
    anchors = {}
    for match in re.finditer(
            r'def PhysicsRevoluteJoint "(\w+)"[^{]*\{(.*?)\n\s{12}\}', text, re.S):
        name, body = match.group(1), match.group(2)
        position = re.search(r'physics:localPos0 = \(([^)]+)\)', body)
        if position:
            _x, y, z = (float(v) for v in position.group(1).split(","))
            anchors[name] = [-y * MM_PER_M, -z * MM_PER_M]

    linkage = {}
    for side, joints in LINKAGE_JOINTS.items():
        missing = [j for j in joints.values() if j not in anchors]
        if missing:
            raise SystemExit(f"{usda_path}: no anchor for {missing}")
        linkage[side] = {role: anchors[joint] for role, joint in joints.items()}
    return linkage


def parse_pad(xacro_path):
    """Return (half_thickness, half_length, centre_offset, centre_z) in mm.

    `centre_offset` is the pad centre's distance from the gripper's axis of
    symmetry; the caller mirrors it per finger.
    """
    text = xacro_path.read_text()

    def origin(macro):
        body = re.search(r'<xacro:macro name="%s".*?</xacro:macro>' % macro,
                         text, re.S).group(0)
        xyz = re.search(r'<origin xyz="([^"]+)"', body).group(1)
        return [float(v) for v in xyz.split()]

    box = [float(v) for v in re.search(r'<box size="([^"]+)"', text).group(1).split()]
    offset = z = 0.0
    for index, macro in enumerate(PAD_CHAIN):
        _, dy, dz = origin(macro)
        offset += dy if index == 0 else -dy
        z += dz
    return (box[1] * MM_PER_M / 2, box[2] * MM_PER_M / 2,
            abs(offset) * MM_PER_M, z * MM_PER_M)


def pad_path(pad, face_x, side):
    """Rectangle for one fingertip pad, in drawing coordinates.

    The pad's inner face is snapped to the face traced from the visual mesh:
    the two descriptions disagree by about 0.6 mm (they bracket the 85 mm
    stroke from either side), and a pad floating proud of the fingertip it
    belongs to would just look like a rendering bug.
    """
    half_thickness, half_length, _centre, centre_z = pad
    inner = abs(face_x)
    outer = inner + 2 * half_thickness
    top, bottom = -(centre_z + half_length), -(centre_z - half_length)
    xs = (inner, outer) if side > 0 else (-inner, -outer)
    corners = [(xs[0], top), (xs[1], top), (xs[1], bottom), (xs[0], bottom)]
    return "M" + "L".join(f"{fmt(round(x, 2))} {fmt(round(y, 2))}"
                          for x, y in corners) + "Z"


# --- COLLADA ------------------------------------------------------------------

def load_dae(path):
    """Return {material: Nx3x3 array of triangle vertices} from a COLLADA file."""
    root = ET.parse(path).getroot()

    unit = root.find("c:asset/c:unit", COLLADA_NS)
    scale = float(unit.get("meter", 1.0)) if unit is not None else 1.0

    groups = {}
    for mesh in root.iterfind(".//c:library_geometries/c:geometry/c:mesh", COLLADA_NS):
        # VERTEX inputs point at a <vertices> element, which forwards to POSITION.
        sources = {}
        for source in mesh.iterfind("c:source", COLLADA_NS):
            array = source.find("c:float_array", COLLADA_NS)
            sources[source.get("id")] = np.fromstring(array.text, sep=" ")
        vertices = {}
        for vert in mesh.iterfind("c:vertices", COLLADA_NS):
            position = vert.find('c:input[@semantic="POSITION"]', COLLADA_NS)
            vertices[vert.get("id")] = position.get("source").lstrip("#")

        for tris in mesh.iterfind("c:triangles", COLLADA_NS):
            inputs = tris.findall("c:input", COLLADA_NS)
            stride = max(int(i.get("offset", 0)) for i in inputs) + 1
            vertex_input = next(i for i in inputs if i.get("semantic") == "VERTEX")
            offset = int(vertex_input.get("offset", 0))
            source_id = vertex_input.get("source").lstrip("#")
            points = sources[vertices.get(source_id, source_id)].reshape(-1, 3)

            indices = np.fromstring(tris.find("c:p", COLLADA_NS).text,
                                    sep=" ", dtype=np.int64)
            indices = indices.reshape(-1, stride)[:, offset]
            triangles = points[indices].reshape(-1, 3, 3) * scale
            material = tris.get("material", "grey-material")
            groups.setdefault(material, []).append(triangles)

    return {mat: np.concatenate(chunks) for mat, chunks in groups.items()}


def place_3d(triangles, origin_x, origin_z):
    """Move a link's triangles to their fully-open pose, in mm.

    Kept in the gripper's own frame — x across the fingers, y along the joint
    axes, z up — so the viewer applies the same rotations the 2D view does.
    """
    out = np.array(triangles, dtype=np.float32)
    out[:, :, 0] += origin_x
    out[:, :, 2] += origin_z
    return out * MM_PER_M


def project(triangles, origin_x, origin_z):
    """Project link-frame triangles onto the drawing plane, in mm.

    SVG's y axis points down and the gripper's z axis points up, so z is
    negated: the drawing then has the gripper standing upright, fingers up.
    """
    out = np.empty((len(triangles), 3, 2))
    out[:, :, 0] = (triangles[:, :, 0] + origin_x) * MM_PER_M
    out[:, :, 1] = -(triangles[:, :, 2] + origin_z) * MM_PER_M
    return out


# --- Outline tracing ----------------------------------------------------------

def trace_outline(triangles_2d, bbox):
    """Rasterise projected triangles and trace the boundary of their union.

    Returns a list of polygons, in mm. Filling every triangle into one mask and
    contouring it sidesteps polygon-union code, and collapses tens of thousands
    of overlapping triangles into a handful of loops.
    """
    x0, y0, x1, y1 = bbox
    width = int((x1 - x0) / RASTER_MM_PER_PX) + 4
    height = int((y1 - y0) / RASTER_MM_PER_PX) + 4

    image = Image.new("L", (width, height), 0)
    draw = ImageDraw.Draw(image)
    pixels = (triangles_2d - (x0, y0)) / RASTER_MM_PER_PX + 2.0
    for tri in pixels:
        draw.polygon([tuple(p) for p in tri], fill=255)

    mask = np.asarray(image, dtype=float)
    if not mask.any():
        return []

    # Contour the half-way level of the filled mask.
    figure = plt.figure()
    try:
        contours = figure.gca().contour(mask, levels=[128.0])
        loops = [seg for seg in contours.allsegs[0] if len(seg) >= 3]
    finally:
        plt.close(figure)

    polygons = []
    for loop in loops:
        points = (loop - 2.0) * RASTER_MM_PER_PX + (x0, y0)
        simplified = simplify(points, SIMPLIFY_TOLERANCE_MM)
        if len(simplified) >= 3 and polygon_area(simplified) >= MIN_FEATURE_MM2:
            polygons.append(simplified)
    return polygons


def polygon_area(points):
    """Unsigned area of a closed polygon, by the shoelace formula."""
    x, y = points[:, 0], points[:, 1]
    return 0.5 * abs(np.dot(x, np.roll(y, -1)) - np.dot(y, np.roll(x, -1)))


def simplify(points, tolerance):
    """Ramer-Douglas-Peucker, iterative so long outlines cannot blow the stack."""
    points = np.asarray(points)
    keep = np.zeros(len(points), dtype=bool)
    keep[0] = keep[-1] = True
    stack = [(0, len(points) - 1)]
    while stack:
        first, last = stack.pop()
        if last <= first + 1:
            continue
        start, end = points[first], points[last]
        segment = end - start
        length = np.hypot(*segment)
        span = points[first + 1:last]
        if length == 0:
            distances = np.hypot(*(span - start).T)
        else:
            distances = np.abs(np.cross(segment, span - start)) / length
        index = int(np.argmax(distances))
        if distances[index] > tolerance:
            index += first + 1
            keep[index] = True
            stack.append((first, index))
            stack.append((index, last))
    return points[keep]


def to_path(polygons, decimals=2):
    parts = []
    for polygon in polygons:
        coords = [f"{fmt(round(x, decimals))} {fmt(round(y, decimals))}"
                  for x, y in polygon]
        parts.append("M" + "L".join(coords) + "Z")
    return "".join(parts)


def fmt(value):
    text = f"{value:.2f}".rstrip("0").rstrip(".")
    return "0" if text in ("", "-0") else text


def write_meshes(placed, path):
    """Write every link's triangles to one binary blob; return the index.

    Triangles are stored un-indexed, three vertices each, so the viewer can
    compute per-face normals: these are machined parts, and shared-vertex
    smoothing would round off edges that are genuinely sharp. Positions only —
    normals are cheaper to derive in the browser than to ship.
    """
    index, blob, offset = [], [], 0
    for item in placed:
        for material, triangles in item["solids"].items():
            data = np.ascontiguousarray(triangles.reshape(-1, 3), dtype="<f4")
            blob.append(data.tobytes())
            index.append({
                "name": item["name"],
                "cls": MATERIAL_CLASS.get(material, "metal"),
                "byteOffset": offset,
                "vertexCount": len(data),
            })
            offset += data.nbytes
    path.write_bytes(b"".join(blob))
    index.sort(key=lambda part: CLASS_ORDER.index(part["cls"]))
    return index


# --- Build --------------------------------------------------------------------

def build(description_dir, pad_xacro=None, linkage_usda=None, mesh_path=None):
    urdf = description_dir / "urdf" / "robotiq_2f_85_macro.urdf.xacro"
    meshes = description_dir / "meshes" / "visual" / "2f_85"
    if not urdf.is_file():
        raise SystemExit(f"not found: {urdf}")
    joints = parse_joints(urdf)
    pad = parse_pad(pad_xacro) if pad_xacro else None
    linkage = parse_linkage(linkage_usda) if linkage_usda else None

    # Project everything first so every link can be rasterised on one grid.
    placed = []
    for link_name, mesh_name in STATIC_LINKS + TIP_LINKS:
        x, z = link_origin(joints, link_name) if link_name in joints else (0.0, 0.0)
        groups = load_dae(meshes / f"{mesh_name}.dae")
        placed.append({
            "name": mesh_name,
            "is_tip": (link_name, mesh_name) in TIP_LINKS,
            "pivot": [round(x * MM_PER_M, 3), round(-z * MM_PER_M, 3)],
            "origin": (x, z),
            "groups": {mat: project(tris, x, z) for mat, tris in groups.items()},
            "solids": {mat: place_3d(tris, x, z) for mat, tris in groups.items()},
        })

    everything = np.concatenate([tris.reshape(-1, 2)
                                 for item in placed
                                 for tris in item["groups"].values()])
    bbox = [everything[:, 0].min(), everything[:, 1].min(),
            everything[:, 0].max(), everything[:, 1].max()]

    mesh_index = write_meshes(placed, mesh_path) if mesh_path else []

    links, tips = [], []
    for item in placed:
        parts = []
        for material, triangles in item["groups"].items():
            polygons = trace_outline(triangles, bbox)
            if polygons:
                parts.append({"class": MATERIAL_CLASS.get(material, "metal"),
                              "d": to_path(polygons)})
        parts.sort(key=lambda part: CLASS_ORDER.index(part["class"]))
        if not parts:
            continue
        role, side = LINK_ROLES.get(item["name"], (None, None))
        entry = {"name": item["name"], "parts": parts}
        if role:
            entry["role"] = role
        if side:
            entry["side"] = side
        if item["is_tip"]:
            entry["pivot"] = item["pivot"]
            if pad:
                # Inner face of the traced rubber, i.e. the side facing the
                # other finger, is what the pad rectangle is aligned to.
                rubber = next((part for part in parts if part["class"] == "rubber"), None)
                xs = [float(v) for v in
                      re.findall(r'-?\d+(?:\.\d+)?', rubber["d"])][0::2]
                side = 1.0 if item["pivot"][0] > 0 else -1.0
                face = min(xs) if side > 0 else max(xs)
                parts.append({"class": "pad", "d": pad_path(pad, face, side)})
                parts.sort(key=lambda part: CLASS_ORDER.index(part["class"]))
            tips.append(entry)
        else:
            links.append(entry)

    return {
        "units": "mm",
        "source": "robotiq_description visual meshes, 2F-85 fully open",
        "meshFile": "gripper_meshes.bin",
        "meshes": mesh_index,
        # A positive joint angle turns about the URDF's (0, -1, 0) axis, which
        # is counter-clockwise in the x-z plane. SVG's y axis points down, so
        # the same turn is clockwise on screen and rotate() takes -angle.
        "svgRotationSign": -1,
        "bbox": [round(float(v), 2) for v in bbox],
        "linkage": linkage,
        "links": links,
        "tips": tips,
    }


def render_js(geometry):
    lines = [
        "// GENERATED FILE - do not edit.",
        "//",
        "// The Robotiq 2F-85 at its fully-open pose: link outlines and meshes traced",
        "// and placed from published descriptions of the gripper, plus the pivots of",
        "// its five-bar linkage. Each link is split into material groups so the pads",
        "// can be drawn as pads.",
        "//",
        "// Derived from, with thanks:",
        "//   robotiq_description, ros2_robotiq_gripper (BSD-3-Clause, PickNik Robotics)",
        "//     link meshes and joint origins",
        "//   robotiq_2f_85_gripper_visualization, ros-industrial/robotiq (BSD, 2013)",
        "//     the finger pad, given as a box primitive rather than a mesh",
        "//   Robotiq 2F-85 Isaac Sim asset (CC BY 4.0)",
        "//     the five-bar pivots, modelled as real joints rather than mimics",
        "//",
        "// Regenerate with tools/build_gripper_geometry.py; see the demo README.",
        "",
        '"use strict";',
        "",
        "const GRIPPER_GEOMETRY = {",
        f'    units: {json.dumps(geometry["units"])},',
        f'    source: {json.dumps(geometry["source"])},',
        f'    svgRotationSign: {geometry["svgRotationSign"]},',
        f'    bbox: {json.dumps(geometry["bbox"])},',
    ]
    if geometry.get("meshes"):
        lines.append("    // Triangle soup for the 3D view, in the gripper's own frame at the")
        lines.append("    // fully-open pose: x across the fingers, y along the joint axes, z up.")
        lines.append(f'    meshFile: {json.dumps(geometry["meshFile"])},')
        lines.append("    meshes: [")
        for part in geometry["meshes"]:
            lines.append(f'        {{ name: {json.dumps(part["name"])}, '
                         f'cls: {json.dumps(part["cls"])}, '
                         f'byteOffset: {part["byteOffset"]}, '
                         f'vertexCount: {part["vertexCount"]} }},')
        lines.append("    ],")
    if geometry.get("linkage"):
        lines.append("    // Five-bar pivots, in drawing mm. With the drive held at")
        lines.append("    // fully open the loop reduces to a four-bar whose coupler is the")
        lines.append("    // distal phalanx — which is the link the fingertip IMU measures.")
        lines.append("    linkage: {")
        for side, pivots in geometry["linkage"].items():
            lines.append(f"        {side}: {{")
            for role, point in pivots.items():
                lines.append(f'            {role}: [{point[0]:.2f}, {point[1]:.2f}],')
            lines.append("        },")
        lines.append("    },")
    for key in ("links", "tips"):
        lines.append(f"    {key}: [")
        for entry in geometry[key]:
            lines.append("        {")
            lines.append(f'            name: {json.dumps(entry["name"])},')
            if "role" in entry:
                lines.append(f'            role: {json.dumps(entry["role"])},')
            if "side" in entry:
                lines.append(f'            side: {json.dumps(entry["side"])},')
            if "pivot" in entry:
                lines.append(f'            pivot: {json.dumps(entry["pivot"])},')
            lines.append("            parts: [")
            for part in entry["parts"]:
                lines.append(f'                {{ cls: {json.dumps(part["class"])}, '
                             f'd: {json.dumps(part["d"])} }},')
            lines.append("            ],")
            lines.append("        },")
        lines.append("    ],")
    lines.append("};")
    lines.append("")
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("description_dir", type=Path,
                        help="path to the robotiq_description package")
    parser.add_argument("--pad-description", type=Path,
                        help="path to robotiq_arg2f_85_model_macro.xacro from the "
                             "ros-industrial robotiq_2f_85_gripper_visualization "
                             "package, which defines the finger pad as a box "
                             "primitive; omit to leave the pads unmarked")
    parser.add_argument("--linkage", type=Path,
                        help="path to Robotiq_2F_85_physics_compliant.usda from the "
                             "Isaac Sim asset, which models the five-bar as real "
                             "joints instead of the ROS description's mimics")
    parser.add_argument("-o", "--output", type=Path,
                        default=Path(__file__).resolve().parent.parent / "web" / "gripper_geometry.js",
                        help="output file (default: web/gripper_geometry.js)")
    args = parser.parse_args()

    geometry = build(args.description_dir, args.pad_description, args.linkage,
                     args.output.parent / "gripper_meshes.bin")
    args.output.write_text(render_js(geometry))

    x0, y0, x1, y1 = geometry["bbox"]
    print(f"wrote {args.output} ({args.output.stat().st_size // 1024} KB)", file=sys.stderr)
    print(f"  {len(geometry['links'])} static links, {len(geometry['tips'])} fingertips",
          file=sys.stderr)
    print(f"  extent {x1 - x0:.1f} x {y1 - y0:.1f} mm", file=sys.stderr)
    if geometry.get("meshes"):
        blob = args.output.parent / geometry["meshFile"]
        total = sum(part["vertexCount"] for part in geometry["meshes"])
        print(f"  {blob.name}: {total // 3} triangles, "
              f"{blob.stat().st_size / 1024 / 1024:.2f} MB", file=sys.stderr)
    for tip in geometry["tips"]:
        materials = ", ".join(p["class"] for p in tip["parts"])
        print(f"  {tip['name']} pivot {tip['pivot']} mm, parts: {materials}",
              file=sys.stderr)


if __name__ == "__main__":
    main()
