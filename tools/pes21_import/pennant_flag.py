#!/usr/bin/env python3
"""Gives an imported circle-flag prop the flag its bearers are holding.

    pennant_flag.py data/media/objects/stadiums/*/entrance/pennant_*.ase

PES's circle flag is not a prop, it is a PERFORMANCE: the ceremony ships a
skeleton (`circleflag_uefa_sc_01_anim_skel.ask`) and a clip
(`dml_prop_circleflag_uefa_cl_01_anm.gani`) that walk a ring of bearers out and
lift the cloth between them. What the mesh holds is the cloth's REST state, and
that is what a static import draws: measured on st002's pennant_4cc.ase, all
six cloth meshes lie between z 0.00 and 0.42 while the bearers stand 1.88 m
tall, so the flag is a crumpled sheet on the grass with a ring of men around it
(owner, 05-09: "the mesh for it still looks torn and broken ingame").

Driving the ceremony from its gani is the real answer and needs the prop's
geometry paired with its skeleton, which is not in the packs this import reads.
Until then the cloth is rebuilt as the shape the bearers are in fact holding: a
disc at their hands, sagging towards the middle the way a carried flag does.
Everything else in the file - both bearer rings, the materials, the object
wrapper - passes through untouched.

Reproducible: run it again after any stadium conversion; it is idempotent
(a rebuilt flag is recognised by its node name and replaced, not stacked).
"""

import argparse
import glob
import math
import os
import re
import sys

# A mesh whose whole extent is below this is the cloth at rest, not a bearer:
# the cloth tops out at 0.42 m and the shorter bearer ring reaches 1.43 m.
CLOTH_MAX_Z = 0.6

# Where the cloth is held and how far it dips. The bearers' own geometry says
# where their hands are: the outer ring stands at radius 7.87-8.87 m and the
# authored cloth reaches 8.38 m, so the rim is at the ring. Height and sag are
# the one thing the rest state cannot say - a flag carried flat by twenty-four
# people is held about chest high on a 1.88 m figure and hangs a third of a
# metre in the middle.
HOLD_HEIGHT = 1.05
SAG = 0.32

SEGMENTS = 72
RINGS = 10

NODE = "pennant_flag_disc"
UNDERSIDE_NODE = "pennant_flag_disc_under"


def _floats(pattern, text):
    return [tuple(float(v) for v in m) for m in re.findall(pattern, text)]


def mesh_blocks(text):
    """-> (header, [block]) split on GEOMOBJECT, each block a whole mesh."""
    parts = re.split(r"(?=\*GEOMOBJECT)", text)
    return parts[0], parts[1:]


def block_vertices(block):
    return _floats(r"\*MESH_VERTEX\s+\d+\s+(\S+)\s+(\S+)\s+(\S+)", block)


def block_name(block):
    found = re.search(r'\*NODE_NAME "([^"]+)"', block)
    return found.group(1) if found else ""


def block_material(block):
    found = re.search(r"\*MATERIAL_REF (\d+)", block)
    return int(found.group(1)) if found else 0


def is_cloth(block):
    """Whether this mesh is the flag at rest rather than a bearer."""
    if block_name(block) in (NODE, UNDERSIDE_NODE):
        return True
    vertices = block_vertices(block)
    if not vertices:
        return False
    if max(v[2] for v in vertices) >= CLOTH_MAX_Z:
        return False
    radii = [math.hypot(v[0], v[1]) for v in vertices]
    # A doormat-sized flat mesh is a prop's base, not the flag.
    return max(radii) > 2.0


def cloth_radius(blocks):
    """-> the radius the flag reaches, from the cloth meshes themselves."""
    # The authored cloth only. A disc this tool built before is also "cloth",
    # and measuring it re-measured the answer instead of the question.
    radii = [math.hypot(v[0], v[1])
             for block in blocks
             if is_cloth(block) and block_name(block) not in (NODE, UNDERSIDE_NODE)
             for v in block_vertices(block)]
    return max(radii) if radii else 0.0


def bearer_radius(blocks):
    """-> the radius of the ring of bearers, for a file with no cloth left.

    The flag is the size of the ring that carries it, so the bearers say how
    big it is. This is the path for a file this tool has already rebuilt: the
    cloth it replaced is gone, and it must still be able to rebuild the flag
    (measured on st002 the two agree to 6%: cloth 8.38 m, bearers 8.87 m at
    their outer edge, and the flag is held inside that).
    """
    ring = None
    for block in blocks:
        vertices = block_vertices(block)
        if not vertices or block_name(block) in (NODE, UNDERSIDE_NODE):
            continue
        top = max(v[2] for v in vertices)
        if top < CLOTH_MAX_Z:
            continue          # not a bearer: a flat mesh
        # The OUTER ring - the tallest bearers, standing at the flag's rim.
        # Averaging every bearer mesh in pulled the rim to 5.4 m, inside a
        # second group that stands nearer the middle.
        if ring is None or top > ring[0]:
            ring = (top, [math.hypot(v[0], v[1]) for v in vertices])
    if not ring:
        return 0.0
    radii = sorted(ring[1])
    # Their hands, not their backs: the inner edge of that ring, so the sheet
    # stops at the arms instead of cutting through the bodies.
    return radii[int(len(radii) * 0.02)]


def emblem_materials(text):
    """-> (material for the emblem side, material for the backing).

    Read off the material list rather than off a cloth mesh, so a file whose
    cloth has already been replaced can still be rebuilt.
    """
    top = under = 0
    for index, bitmap in enumerate(re.findall(r"\*BITMAP \"([^\"]+)\"", text)):
        name = os.path.basename(bitmap).lower()
        if "prop000" in name or "emblem" in name:
            top = index
        elif "prop001" in name:
            under = index
    return top, under


def disc(radius, height=HOLD_HEIGHT, sag=SAG, segments=SEGMENTS, rings=RINGS):
    """-> (vertices, faces, uvs) for a sagging disc of `radius`.

    Held at the rim and lowest in the middle: z = height - sag*(1 - (r/R)^2).
    UVs are planar over the disc, which is how the emblem is authored - a
    circular badge centred in a square map - and v runs the way every other
    writer here emits it (1 - v, see stadium_staff._write_figure).
    """
    vertices = [(0.0, 0.0, height - sag)]
    for ring in range(1, rings + 1):
        r = radius * ring / rings
        for step in range(segments):
            angle = 2.0 * math.pi * step / segments
            vertices.append((r * math.cos(angle), r * math.sin(angle),
                             height - sag * (1.0 - (r / radius) ** 2)))
    faces = []
    for step in range(segments):
        nxt = (step + 1) % segments
        faces.append((0, 1 + nxt, 1 + step))
    for ring in range(1, rings):
        inner = 1 + (ring - 1) * segments
        outer = 1 + ring * segments
        for step in range(segments):
            nxt = (step + 1) % segments
            faces.append((inner + step, outer + nxt, outer + step))
            faces.append((inner + step, inner + nxt, outer + nxt))
    # Turned to the broadcast side, not mirrored: the sheet that faces the sky
    # is the reversed winding, so mapping it straight put the wordmark on
    # backwards, and mapping it mirrored in one axis only put it the right way
    # round for the tunnel camera and upside down for the one that films the
    # ceremony (measured on a recorded entrance).
    uvs = [(0.5 + 0.5 * v[0] / radius, 0.5 + 0.5 * v[1] / radius) for v in vertices]
    return vertices, faces, uvs


def write_mesh(name, material, vertices, faces, uvs, flip=False):
    """One GEOMOBJECT, in the shape stadium_to_gf writes them."""
    if flip:
        faces = [(a, c, b) for a, b, c in faces]
    out = ["*GEOMOBJECT {", '\t*NODE_NAME "%s"' % name,
           "\t*NODE_TM {", '\t\t*NODE_NAME "%s"' % name,
           "\t\t*INHERIT_POS 0 0 0", "\t\t*INHERIT_ROT 0 0 0", "\t\t*INHERIT_SCL 0 0 0",
           "\t\t*TM_ROW0 1.0 0.0 0.0", "\t\t*TM_ROW1 0.0 1.0 0.0",
           "\t\t*TM_ROW2 0.0 0.0 1.0", "\t\t*TM_ROW3 0.0 0.0 0.0",
           "\t\t*TM_POS 0.0 0.0 0.0", "\t}",
           "\t*MESH {", "\t\t*TIMEVALUE 0",
           "\t\t*MESH_NUMVERTEX %d" % len(vertices),
           "\t\t*MESH_NUMFACES %d" % len(faces),
           "\t\t*MESH_VERTEX_LIST {"]
    for i, v in enumerate(vertices):
        out.append("\t\t\t*MESH_VERTEX %d\t%.6f\t%.6f\t%.6f" % (i, v[0], v[1], v[2]))
    out.append("\t\t}")
    out.append("\t\t*MESH_FACE_LIST {")
    for i, (a, b, c) in enumerate(faces):
        out.append("\t\t\t*MESH_FACE %d:    A:    %d B:    %d C:    %d AB:    1 BC:    1 CA:    1"
                   "\t *MESH_SMOOTHING 1 \t*MESH_MTLID 0" % (i, a, b, c))
    out.append("\t\t}")
    out.append("\t\t*MESH_NUMTVERTEX %d" % len(uvs))
    out.append("\t\t*MESH_TVERTLIST {")
    for i, (u, v) in enumerate(uvs):
        out.append("\t\t\t*MESH_TVERT %d\t%.5f\t%.5f\t0.0" % (i, u, v))
    out.append("\t\t}")
    out.append("\t\t*MESH_NUMTVFACES %d" % len(faces))
    out.append("\t\t*MESH_TFACELIST {")
    for i, (a, b, c) in enumerate(faces):
        out.append("\t\t\t*MESH_TFACE %d\t%d\t%d\t%d" % (i, a, b, c))
    out.append("\t\t}")
    out.append("\t}")
    out.append("\t*PROP_MOTIONBLUR 0\n\t*PROP_CASTSHADOW 1\n\t*PROP_RECVSHADOW 1")
    out.append("\t*MATERIAL_REF %d" % material)
    out.append("}")
    return "\n".join(out) + "\n"


def rebuild(text):
    """-> (new text, how many cloth meshes were replaced, the radius used)."""
    header, blocks = mesh_blocks(text)
    # The authored cloth, and a disc this tool wrote on an earlier run. Both go;
    # only the first says how big the flag is, and when it is already gone the
    # bearers do.
    authored = [b for b in blocks
                if is_cloth(b) and block_name(b) not in (NODE, UNDERSIDE_NODE)]
    ours = [b for b in blocks if block_name(b) in (NODE, UNDERSIDE_NODE)]
    radius = cloth_radius(blocks) if authored else bearer_radius(blocks)
    if radius <= 0.0:
        return text, 0, 0.0
    # The emblem material is the one the top sheets already use; the underside
    # keeps whichever material the flat navy backing had, so the flag is opaque
    # from below without a new texture.
    top_material, under_material = emblem_materials(text)

    vertices, faces, uvs = disc(radius)
    kept = [b for b in blocks if not is_cloth(b)]
    # Under-side UVs sit on the backing's own patch of the bearers' map: one
    # flat navy, which is what PES's backing sheet samples.
    under_uvs = [(0.87, 0.42)] * len(vertices)
    # Fox winds clockwise-front and this engine culls GL-style, so the sheet
    # that faces the sky is the reversed one - written the other way round the
    # disc showed its navy backing from above and the emblem to the grass.
    made = (write_mesh(NODE, top_material, vertices, faces, uvs, flip=True) +
            write_mesh(UNDERSIDE_NODE, under_material, vertices, faces, under_uvs))
    return header + "".join(kept) + made, len(authored) + len(ours), radius


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("paths", nargs="+", help="pennant ASE(s), globs allowed")
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()

    files = [p for pattern in args.paths for p in sorted(glob.glob(pattern))]
    if not files:
        print("no pennant ASE matched")
        return 1
    for path in files:
        text = open(path, errors="replace").read()
        new, replaced, radius = rebuild(text)
        if not replaced:
            print("%-70s no cloth mesh found" % path)
            continue
        print("%-70s %d cloth mesh(es) -> one disc of r=%.2f m" % (path, replaced, radius))
        if args.dry_run:
            continue
        open(path, "w").write(new)
        # The cache holds the geometry it was built from, so it has to go.
        cache = path + ".geomcache"
        if os.path.exists(cache):
            os.remove(cache)
    return 0


if __name__ == "__main__":
    sys.exit(main())
