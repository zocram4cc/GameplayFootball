"""Brings a fullbody ASE from joint-local storage into base-pose world space.

The engine used to skin by rotating a vertex by its joint's ABSOLUTE
orientation:

    result = R_current * (v - p_bind) + p_current

which only reconstructs the right shape when the geometry is stored
pre-counter-rotated into each joint's own frame. That convention cannot
describe a vertex driven by several joints - each influence would need its own
copy of the same point - so imported PES models had to be flattened onto one
joint apiece, throwing away the weights PES authored.

Ordinary linear blend skinning inserts the joint's inverse bind rotation:

    result = R_current * R_bind^-1 * (v - p_bind) + p_current

which needs only the one stored position per vertex and blends influences
correctly. Geometry authored for the old convention converts once:

    v_new = p_bind + R_bind * (v_old - p_bind)

using each vertex's dominant joint, read back out of its vertex colour.

Structure matters here: an ASE holds several GEOMOBJECTs, vertex indices
restart in each of them, and the colour array is indexed separately from the
vertex array - the two are related through *MESH_FACE / *MESH_CFACE, exactly as
gamedefines.cpp GetVertexColors reads them.

  python3 migrate_fullbody_pose.py <fullbody.ase>... [--dry-run]

Writes in place, keeping the original as <name>.preLBS.
"""

import argparse
import os
import shutil
import sys

import fmdl_to_fullbody as F


def decode_joint(color):
    """Dominant joint id from an ASE vertex colour (see encode_color)."""
    best_id, best_weight = 0, -1.0
    for channel in color:
        value = channel * 255.0
        if value <= 0.01:
            continue
        joint = int(value * 0.1)
        weight = (value - joint * 10.0) / 9.0
        if 0 <= joint < len(F.GF_JOINT_ORDER) and weight > best_weight:
            best_id, best_weight = joint, weight
    return best_id


def parse_meshes(lines):
    """-> [ {verts:{i:(x,y,z)}, cols:{i:(r,g,b)}, faces:[(a,b,c)], cfaces:[...]} ]."""
    meshes = []
    current = None
    for line in lines:
        parts = line.split()
        if not parts:
            continue
        keyword = parts[0]
        if keyword == "*GEOMOBJECT":
            current = {"verts": {}, "cols": {}, "faces": [], "cfaces": [],
                       "vert_lines": {}}
            meshes.append(current)
        elif current is None:
            continue
        elif keyword == "*MESH_VERTEX":
            current["verts"][int(parts[1])] = tuple(float(x) for x in parts[2:5])
        elif keyword == "*MESH_VERTCOL":
            current["cols"][int(parts[1])] = tuple(float(x) for x in parts[2:5])
        elif keyword == "*MESH_FACE":
            # *MESH_FACE 0: A: 1 B: 2 C: 3 ...
            current["faces"].append((int(parts[3]), int(parts[5]), int(parts[7])))
        elif keyword == "*MESH_CFACE":
            current["cfaces"].append(tuple(int(x) for x in parts[2:5]))
    return meshes


def vertex_colors(mesh):
    """vertex index -> colour, related through the face/colour-face pairs."""
    mapping = {}
    for face, cface in zip(mesh["faces"], mesh["cfaces"]):
        for vertex_index, color_index in zip(face, cface):
            if vertex_index in mapping:
                continue
            color = mesh["cols"].get(color_index)
            if color is not None:
                mapping[vertex_index] = color
    return mapping


def migrate(path, dry_run=False):
    base_pos, base_rot = F.gf_base_pose(want_rotations=True)
    lines = open(path).read().splitlines()
    meshes = parse_meshes(lines)
    if not meshes:
        raise ValueError("no GEOMOBJECT in %s" % path)

    # per mesh, per vertex index -> new position
    moved = []
    total = shifted = uncoloured = 0
    for mesh in meshes:
        colors = vertex_colors(mesh)
        out = {}
        for index, position in mesh["verts"].items():
            total += 1
            color = colors.get(index)
            if color is None:
                uncoloured += 1
                out[index] = position
                continue
            joint = F.GF_JOINT_ORDER[decode_joint(color)]
            offset = tuple(p - o for p, o in zip(position, base_pos[joint]))
            rotated = F._quat_rot(base_rot[joint], offset)
            new = tuple(o + r for o, r in zip(base_pos[joint], rotated))
            out[index] = new
            if max(abs(a - b) for a, b in zip(position, new)) > 1e-4:
                shifted += 1
        moved.append(out)

    print("%s: %d meshes, %d vertices, %d moved, %d without a colour"
          % (os.path.basename(path), len(meshes), total, shifted, uncoloured))
    if dry_run:
        return shifted

    result = []
    mesh_index = -1
    for line in lines:
        parts = line.split()
        if parts and parts[0] == "*GEOMOBJECT":
            mesh_index += 1
        if parts and parts[0] == "*MESH_VERTEX" and mesh_index >= 0:
            index = int(parts[1])
            x, y, z = moved[mesh_index][index]
            indent = line[:len(line) - len(line.lstrip())]
            result.append("%s*MESH_VERTEX %d\t%.4f\t%.4f\t%.4f" % (indent, index, x, y, z))
        else:
            result.append(line)

    if not os.path.exists(path + ".preLBS"):
        shutil.copy2(path, path + ".preLBS")
    open(path, "w").write("\n".join(result) + "\n")
    return shifted


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("ase", nargs="+")
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()
    for path in args.ase:
        migrate(path, args.dry_run)
    return 0


if __name__ == "__main__":
    sys.exit(main())
