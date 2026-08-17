#!/usr/bin/env python3
"""Drops stretched triangles from an already-imported .ase player model.

An fmdl converted without ``fmdl_to_fullbody --max-edge`` keeps every triangle
the source had, including the handful that span the whole model. On a rig they
read as enormous white shards radiating out of a character - the 2hu squad had
six of them each, edges up to 1.25 m on a 1.8 m body, against a median edge of
1.9 cm.

They cannot be repaired, only removed: a triangle joining a hand to a hairtip
has no correct shape. This rewrites the mesh without them.

The three per-face lists in an .ase - MESH_FACE, MESH_TFACE and MESH_CFACE -
are parallel and indexed in step, so a face has to be dropped from all three
and everything after it renumbered, with the *NUMFACES counts brought down to
match. That is all this does; vertices, materials and vertex colours (which
carry the skin weights, see docs) are left exactly as they were.

  python3 strip_stretched_tris.py <model.ase|model-dir> [...] [--max-edge 0.25]
"""

import argparse
import math
import os
import re
import sys

FACE_RE = re.compile(r"^(\s*\*MESH_FACE\s+)(\d+)(:.*)$")
TFACE_RE = re.compile(r"^(\s*\*MESH_TFACE\s+)(\d+)(\s+.*)$")
CFACE_RE = re.compile(r"^(\s*\*MESH_CFACE\s+)(\d+)(\s+.*)$")
FACE_ABC_RE = re.compile(r"A:\s*(\d+)\s*B:\s*(\d+)\s*C:\s*(\d+)")
VERTEX_RE = re.compile(r"^\s*\*MESH_VERTEX\s+(\d+)\s+([-\d.e+]+)\s+([-\d.e+]+)\s+([-\d.e+]+)")
NUMFACES_RE = re.compile(r"^(\s*\*MESH_NUMFACES\s+)(\d+)\s*$")
NUMTV_RE = re.compile(r"^(\s*\*MESH_NUMTVFACES\s+)(\d+)\s*$")
NUMCV_RE = re.compile(r"^(\s*\*MESH_NUMCVFACES\s+)(\d+)\s*$")


def find_stretched(lines, max_edge):
    """Ordinals of the faces to drop, per GEOMOBJECT."""
    drop = {}           # mesh index -> set of face ordinals
    verts = {}
    mesh = -1
    face_ordinal = 0
    for line in lines:
        if line.lstrip().startswith("*GEOMOBJECT"):
            mesh += 1
            verts = {}
            face_ordinal = 0
            continue
        m = VERTEX_RE.match(line)
        if m:
            verts[int(m.group(1))] = (float(m.group(2)), float(m.group(3)), float(m.group(4)))
            continue
        m = FACE_RE.match(line)
        if m:
            abc = FACE_ABC_RE.search(line)
            if abc:
                a, b, c = (int(abc.group(1)), int(abc.group(2)), int(abc.group(3)))
                if a in verts and b in verts and c in verts:
                    va, vb, vc = verts[a], verts[b], verts[c]
                    longest = max(math.dist(va, vb), math.dist(vb, vc), math.dist(vc, va))
                    if longest > max_edge:
                        drop.setdefault(mesh, set()).add(face_ordinal)
            face_ordinal += 1
    return drop


def rewrite(lines, drop):
    """Removes the marked faces and renumbers what is left."""
    out = []
    mesh = -1
    counters = {"face": 0, "tface": 0, "cface": 0}
    kept = {"face": 0, "tface": 0, "cface": 0}
    dropped = 0

    for line in lines:
        if line.lstrip().startswith("*GEOMOBJECT"):
            mesh += 1
            counters = {"face": 0, "tface": 0, "cface": 0}
            kept = {"face": 0, "tface": 0, "cface": 0}
            out.append(line)
            continue

        marked = drop.get(mesh, ())

        for kind, pattern in (("face", FACE_RE), ("tface", TFACE_RE), ("cface", CFACE_RE)):
            m = pattern.match(line)
            if not m:
                continue
            ordinal = counters[kind]
            counters[kind] += 1
            if ordinal in marked:
                if kind == "face":
                    dropped += 1
                break  # drop the line entirely
            out.append("%s%d%s\n" % (m.group(1), kept[kind], m.group(3).rstrip("\n")))
            kept[kind] += 1
            break
        else:
            # not a per-face line: fix up the counts, otherwise pass through
            for pattern in (NUMFACES_RE, NUMTV_RE, NUMCV_RE):
                m = pattern.match(line)
                if m:
                    out.append("%s%d\n" % (m.group(1), int(m.group(2)) - len(marked)))
                    break
            else:
                out.append(line)
    return out, dropped


def rebuild_normals(lines):
    """Regenerates each mesh's MESH_NORMALS block from its geometry.

    The block carries one *MESH_FACENORMAL plus three *MESH_VERTEXNORMAL per
    face, and the vertex-normal index is a running counter across the mesh
    rather than a vertex id - so it cannot survive a face being removed, and
    the loader rejects a file whose counts no longer line up. Rebuilding it is
    both simpler and safer than trying to patch it: face normals come straight
    from the winding, vertex normals from the area-weighted average of the
    faces meeting at each vertex, which is what a smooth-shaded export holds
    anyway.
    """
    # First pass: per-mesh vertices and surviving faces.
    meshes = []
    verts = None
    faces = None
    for line in lines:
        if line.lstrip().startswith("*GEOMOBJECT"):
            verts, faces = {}, []
            meshes.append((verts, faces))
            continue
        if verts is None:
            continue
        m = VERTEX_RE.match(line)
        if m:
            verts[int(m.group(1))] = (float(m.group(2)), float(m.group(3)), float(m.group(4)))
            continue
        if FACE_RE.match(line):
            abc = FACE_ABC_RE.search(line)
            if abc:
                faces.append((int(abc.group(1)), int(abc.group(2)), int(abc.group(3))))

    normals = []
    for verts, faces in meshes:
        face_normals = []
        accum = {}
        for (a, b, c) in faces:
            va, vb, vc = verts.get(a), verts.get(b), verts.get(c)
            if not (va and vb and vc):
                face_normals.append((0.0, 0.0, 1.0))
                continue
            ux, uy, uz = (vb[0] - va[0], vb[1] - va[1], vb[2] - va[2])
            wx, wy, wz = (vc[0] - va[0], vc[1] - va[1], vc[2] - va[2])
            nx, ny, nz = (uy * wz - uz * wy, uz * wx - ux * wz, ux * wy - uy * wx)
            length = math.sqrt(nx * nx + ny * ny + nz * nz)
            if length > 1e-12:
                face_normals.append((nx / length, ny / length, nz / length))
            else:
                face_normals.append((0.0, 0.0, 1.0))
            # area-weighted: the un-normalised cross product is twice the area
            for idx in (a, b, c):
                prev = accum.get(idx, (0.0, 0.0, 0.0))
                accum[idx] = (prev[0] + nx, prev[1] + ny, prev[2] + nz)
        smooth = {}
        for idx, (nx, ny, nz) in accum.items():
            length = math.sqrt(nx * nx + ny * ny + nz * nz)
            smooth[idx] = (nx / length, ny / length, nz / length) if length > 1e-12 else (0.0, 0.0, 1.0)
        normals.append((faces, face_normals, smooth))

    # Second pass: swap each MESH_NORMALS block for the rebuilt one.
    out = []
    mesh = -1
    in_block = False
    depth = 0
    for line in lines:
        if line.lstrip().startswith("*GEOMOBJECT"):
            mesh += 1
        if not in_block and line.lstrip().startswith("*MESH_NORMALS"):
            in_block = True
            depth = line.count("{") - line.count("}")
            indent = line[: len(line) - len(line.lstrip())]
            out.append(line)
            faces, face_normals, smooth = normals[mesh]
            counter = 0
            for i, (a, b, c) in enumerate(faces):
                fn = face_normals[i]
                out.append("%s\t*MESH_FACENORMAL %d\t%.4f\t%.4f\t%.4f\n"
                           % (indent, i, fn[0], fn[1], fn[2]))
                for idx in (a, b, c):
                    vn = smooth.get(idx, fn)
                    out.append("%s\t\t*MESH_VERTEXNORMAL %d\t%.4f\t%.4f\t%.4f\n"
                               % (indent, counter, vn[0], vn[1], vn[2]))
                    counter += 1
            continue
        if in_block:
            depth += line.count("{") - line.count("}")
            if depth <= 0:
                in_block = False
                out.append(line)  # the closing brace
            continue
        out.append(line)
    return out


def process(path, max_edge, dry_run):
    with open(path, "r", errors="ignore") as handle:
        lines = handle.readlines()

    drop = find_stretched(lines, max_edge)
    total = sum(len(v) for v in drop.values())
    if total == 0:
        # A file whose normals block no longer matches its faces still has to
        # be repaired - that is what a half-finished strip leaves behind.
        faces = sum(1 for line in lines if FACE_RE.match(line))
        facenormals = sum(1 for line in lines if "*MESH_FACENORMAL" in line)
        if faces and facenormals != faces:
            if not dry_run:
                with open(path, "w") as handle:
                    handle.writelines(rebuild_normals(lines))
            print("%-46s normals rebuilt (%d -> %d)"
                  % (os.path.basename(path), facenormals, faces))
            return 0
        print("%-46s clean" % os.path.basename(path))
        return 0

    out, dropped = rewrite(lines, drop)
    out = rebuild_normals(out)
    if not dry_run:
        with open(path, "w") as handle:
            handle.writelines(out)
    print("%-46s dropped %d stretched triangle(s)" % (os.path.basename(path), dropped))
    return dropped


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("targets", nargs="+", help="model dirs or .ase files")
    parser.add_argument("--max-edge", type=float, default=0.15,
                        help="longest triangle edge to keep, in metres")
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()

    files = []
    for target in args.targets:
        if os.path.isdir(target):
            for name in sorted(os.listdir(target)):
                if name.endswith(".ase"):
                    files.append(os.path.join(target, name))
        elif target.endswith(".ase"):
            files.append(target)

    total = 0
    for path in files:
        total += process(path, args.max_edge, args.dry_run)
    print("%d triangle(s) dropped across %d file(s)" % (total, len(files)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
