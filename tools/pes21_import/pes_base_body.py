"""Assembles PES 2021's own in-game player body as GameplayFootball's
default fullbody model.

PES has no single player mesh: the match player is composed from the base
character package plus per-slot parts. The pieces used here, and where they
come from (proprietary sources stay under the PES install):

  dt00_x64.cpk(.bak!)  Asset/model/character/#Win/common_package.fpk
      common/pants_out_sub.fmdl   kit shorts       (diffuse = runtime kit)
      common/socks_middle.fmdl    kit socks        (diffuse = runtime kit)
      common/arm.fmdl             bare arms        ("arm_mat" meshes)
      common/thigh_short.fmdl     bare thighs      ("thigh_mat")
      common/hand_l/r.fmdl        hands            ("arm_mat")
      common/neck.fmdl            neck skin        ("skin_head")
  dt32_g4.cpk  Asset/model/character/parts/undershirt/scenes/#Win/
      undershirt.fmdl             kit shirt        ("torso_mat" meshes -
                                  PES fills their empty diffuse slot with
                                  the team kit texture at runtime)
  boots slot (4cc stockkit k0001 or any real boots fpk)
      boots.fmdl                  shoes
  dt36_g4.cpk  face/real/<id>/#Win/face.fpk
      face_high.fmdl              head (own painted texture; also the
                                  source of faceweights.txt for FaceRig)

NOTE the .bak: the live dt00_x64.cpk was edited by 4CC for kit-body
transparency; extract the pristine dt00_x64.cpk.bak.

Texturing on the GF side:
  * kit pieces are RE-UV'd onto GF's kit template layout so every existing
    team kit PNG keeps working: the stock (migrated) fullbody.ase is the
    Rosetta stone - its kit-material vertices pair positions with template
    UVs, and each PES vertex takes an inverse-distance-weighted UV from its
    panel's nearest stock vertices (panels: shirt front/back, shorts
    front/back, sock left/right, classified by facing and position).
  * skin pieces reference skin.jpg, which the engine swaps for the
    player's flat skin-tone texture (UVs are irrelevant on a flat tone).
  * the face keeps its own painted texture; boots theirs.

Skin weights ride vertex colours exactly as fmdl_to_fullbody: PES bones
resolve through retarget.resolve_bone (1:1 for animated bones, lossless
collapse for helpers), top-3 influences renormalized.

  python3 pes_base_body.py --common <dir> --undershirt <fmdl> --boots <fmdl> \
      --face <fmdl> --face-texture <png> --boots-texture <png> \
      --stock <migrated fullbody.ase> --fmdl-lib <pes-fmdl> <out_dir>

Writes <out>/fullbody_pes.ase + <out>/fullbody_pes.object, plus
faceweights.txt for the engine FaceRig (expressions ship separately).
"""

import argparse
import math
import os
import re
import sys

import ase_util
import retarget
import face_weights
from fmdl_to_fullbody import vertex_joints, encode_color, build_bone_map


# --- stock kit UV harvest -----------------------------------------------------

_V_RE = re.compile(r"\*MESH_VERTEX\s+(\d+)\t([-\d.e]+)\t([-\d.e]+)\t([-\d.e]+)")
_T_RE = re.compile(r"\*MESH_TVERT\s+(\d+)\t([-\d.e]+)\t([-\d.e]+)")
_F_RE = re.compile(r"\*MESH_FACE\s+(\d+):\s+A:\s+(\d+)\s+B:\s+(\d+)\s+C:\s+(\d+)")
_TF_RE = re.compile(r"\*MESH_TFACE\s+(\d+)\t(\d+)\t(\d+)\t(\d+)")

# GF kit template layout (ASE v runs bottom-up): one continuous body suit
# per column - shirt with the shorts painted directly below it - front
# column u < 0.5, back column u > 0.5, with the sleeves on T-flaps at the
# top corners; the two mid-left rectangles are the SOCKS (the stock socks
# map there). Measured off kit_UVWnormal.png.
SLEEVE_FLAPS = {
    # (front/back, left/right of the TEXTURE column): (u0, u1)
    ("front", "outer_l"): (0.012, 0.105),
    ("front", "outer_r"): (0.313, 0.410),
    ("back", "outer_l"): (0.586, 0.684),
    ("back", "outer_r"): (0.889, 0.986),
}
SLEEVE_V_TOP = 0.97      # shoulder end of the flap (ASE v)
SLEEVE_V_BOTTOM = 0.80   # sleeve hem
SLEEVE_LENGTH_M = 0.30   # arm length the flap covers; longer sleeves clamp
# the arm frame (GF coords, bind pose)
_SHOULDER = {1: (0.195, -0.0335, 1.4671), -1: (-0.195, -0.0335, 1.4671)}
_ELBOW = {1: (0.3991, -0.0138, 1.262), -1: (-0.3991, -0.0138, 1.262)}


def harvest_stock_kit(stock_ase):
    """-> {panel: [(pos, uv)]} from the stock body's kit-material geoms."""
    text = open(stock_ase).read()
    head = text.split("*GEOMOBJECT")[0]
    kit_materials = set()
    for block in re.split(r"(?=\*MATERIAL \d+ \{)", head):
        m = re.match(r"\*MATERIAL (\d+) \{", block)
        if m and "kit_template" in block:
            kit_materials.add(m.group(1))
    panels = {}
    for g in text.split("*GEOMOBJECT")[1:]:
        name = re.search(r'\*NODE_NAME "([^"]+)"', g).group(1)
        ref = re.search(r"\*MATERIAL_REF (\d+)", g)
        if not ref or ref.group(1) not in kit_materials:
            continue
        verts = {int(m.group(1)): tuple(float(m.group(i)) for i in (2, 3, 4))
                 for m in _V_RE.finditer(g)}
        tverts = {int(m.group(1)): (float(m.group(2)), float(m.group(3)))
                  for m in _T_RE.finditer(g)}
        faces = [tuple(int(m.group(i)) for i in (2, 3, 4)) for m in _F_RE.finditer(g)]
        tfaces = [tuple(int(m.group(i)) for i in (2, 3, 4)) for m in _TF_RE.finditer(g)]
        for f, tf in zip(faces, tfaces):
            for vi, ti in zip(f, tf):
                pos, (u, v) = verts[vi], tverts[ti]
                if name.startswith("sock"):
                    panel = "sock_left" if name.endswith("left") else "sock_right"
                else:
                    panel = "body_front" if u < 0.5 else "body_back"
                panels.setdefault(panel, []).append((pos, (u, v)))
    return panels


def idw_uv(pos, samples, k=4, power=2.0):
    """Inverse-distance-weighted UV from the panel's stock samples."""
    scored = sorted(samples, key=lambda s: math.dist(pos, s[0]))[:k]
    num_u = num_v = den = 0.0
    for spos, (u, v) in scored:
        d = math.dist(pos, spos)
        if d < 1e-6:
            return (u, v)
        w = 1.0 / d ** power
        num_u += u * w
        num_v += v * w
        den += w
    return (num_u / den, num_v / den)


def sleeve_uv(pos, normal_y):
    """Analytic flap mapping for sleeve vertices (the stock body has no
    sleeves to learn from). Along-arm distance -> flap v; the around-arm
    angle -> flap u, front and back hemispheres on their own flaps."""
    sign = 1 if pos[0] > 0 else -1
    sh, el = _SHOULDER[sign], _ELBOW[sign]
    axis = tuple(e - s for e, s in zip(el, sh))
    alen = math.sqrt(sum(c * c for c in axis))
    axis = tuple(c / alen for c in axis)
    rel = tuple(p - s for p, s in zip(pos, sh))
    t = max(0.0, sum(r * a for r, a in zip(rel, axis)))
    v = SLEEVE_V_TOP - min(t / SLEEVE_LENGTH_M, 1.0) * (SLEEVE_V_TOP - SLEEVE_V_BOTTOM)
    # around-arm: project out the axis, measure the up-ness across the flap
    perp = tuple(r - t * a for r, a in zip(rel, axis))
    plen = math.sqrt(sum(c * c for c in perp)) or 1.0
    upness = perp[2] / plen                      # -1 under arm .. +1 on top
    side = "front" if normal_y < 0.0 else "back"
    u0, u1 = SLEEVE_FLAPS[(side, "outer_l" if sign > 0 else "outer_r")]
    frac = 0.5 + 0.5 * upness
    if sign < 0:
        frac = 1.0 - frac
    return (u0 + frac * (u1 - u0), v)


def kit_uv(pos, normal_y, kind, panels):
    """UV on the GF kit template for a PES vertex (GF coords).

    kind: body (shirt+shorts, one continuous suit panel per column, sleeves
    on their flaps) | sock. Front/back by the normal's GF Y (the character
    faces -Y); socks pick their side by X.
    """
    if kind == "sock":
        panel = "sock_left" if pos[0] > 0 else "sock_right"
        return idw_uv(pos, panels[panel])
    if abs(pos[0]) > 0.24 and pos[2] > 1.0:      # sleeve, past the torso
        return sleeve_uv(pos, normal_y)
    panel = "body_front" if normal_y < 0.0 else "body_back"
    return idw_uv(pos, panels[panel])


# --- assembly -------------------------------------------------------------------

def load_fmdl(path, fmdl_lib):
    sys.path.insert(0, fmdl_lib)
    import FmdlFile
    f = FmdlFile.FmdlFile()
    f.readFile(path)
    return f


def piece_meshes(fmdl, keep_materials=None, biggest_only=False):
    meshes = fmdl.meshes
    if keep_materials is not None:
        meshes = [m for m in meshes
                  if m.materialInstance and m.materialInstance.name in keep_materials]
    if biggest_only and meshes:
        meshes = [max(meshes, key=lambda m: len(m.vertices))]
    return meshes


def gather_piece(fmdl, meshes, kit_kind, panels, inset=0.0):
    """-> (vertices [(pos, uv, color, normal)], faces) in GF coords.

    inset pushes vertices along their inverse normal (metres): skin pieces
    that live under garments (arms in sleeves, thighs in shorts) get a few
    millimetres of clearance so they cannot poke through in animation."""
    bone_to_joint = build_bone_map(fmdl)
    joint_positions = retarget.gf_world_bind()
    vertices = []
    faces = []
    index = {}
    for mesh in meshes:
        for face in mesh.faces:
            tri = []
            for vertex in face.vertices:
                key = id(vertex)
                if key not in index:
                    index[key] = len(vertices)
                    p = vertex.position
                    pos = (p.x, -p.z, p.y)
                    n = vertex.normal
                    normal = (n.x, -n.z, n.y) if n is not None else None
                    if inset and normal is not None:
                        pos = tuple(c - inset * nc for c, nc in zip(pos, normal))
                    if kit_kind:
                        ny = -vertex.normal.z if vertex.normal else -1.0
                        uv = kit_uv(pos, ny, kit_kind, panels)
                    else:
                        uv0 = vertex.uv[0] if vertex.uv else None
                        uv = (uv0.u, 1.0 - uv0.v) if uv0 else (0.0, 0.0)
                    skin = vertex_joints(vertex, bone_to_joint, joint_positions)
                    vertices.append((pos, uv, encode_color(skin), normal))
                tri.append(index[key])
            # Fox winds clockwise-front (D3D); GF culls GL-style, so reverse
            faces.append((tri[0], tri[2], tri[1]))
    return vertices, faces


def write_geomobject(out, name, vertices, faces, material_ref):
    out.write("*GEOMOBJECT {\n")
    out.write('\t*NODE_NAME "%s"\n' % name)
    out.write('\t*NODE_TM {\n\t\t*NODE_NAME "%s"\n' % name)
    out.write("\t\t*INHERIT_POS 0 0 0\n\t\t*INHERIT_ROT 0 0 0\n\t\t*INHERIT_SCL 0 0 0\n")
    out.write("\t\t*TM_ROW0 1.0\t0.0\t0.0\n\t\t*TM_ROW1 0.0\t1.0\t0.0\n")
    out.write("\t\t*TM_ROW2 0.0\t0.0\t1.0\n\t\t*TM_ROW3 0.0\t0.0\t0.0\n\t}\n")
    out.write("\t*MESH {\n")
    out.write("\t\t*MESH_NUMVERTEX %d\n" % len(vertices))
    out.write("\t\t*MESH_NUMFACES %d\n" % len(faces))
    out.write("\t\t*MESH_VERTEX_LIST {\n")
    for i, (pos, _, _, _) in enumerate(vertices):
        out.write("\t\t\t*MESH_VERTEX %d\t%.6f\t%.6f\t%.6f\n" % (i, *pos))
    out.write("\t\t}\n\t\t*MESH_FACE_LIST {\n")
    for i, (a, b, c) in enumerate(faces):
        out.write("\t\t\t*MESH_FACE %d: A: %d B: %d C: %d AB: 1 BC: 1 CA: 1 "
                  "*MESH_SMOOTHING 1 *MESH_MTLID 0\n" % (i, a, b, c))
    out.write("\t\t}\n")
    out.write("\t\t*MESH_NUMTVERTEX %d\n" % len(vertices))
    out.write("\t\t*MESH_TVERTLIST {\n")
    for i, (_, uv, _, _) in enumerate(vertices):
        out.write("\t\t\t*MESH_TVERT %d\t%.6f\t%.6f\t0.0\n" % (i, uv[0], uv[1]))
    out.write("\t\t}\n")
    out.write("\t\t*MESH_NUMTVFACES %d\n" % len(faces))
    out.write("\t\t*MESH_TFACELIST {\n")
    for i, (a, b, c) in enumerate(faces):
        out.write("\t\t\t*MESH_TFACE %d\t%d\t%d\t%d\n" % (i, a, b, c))
    out.write("\t\t}\n")
    out.write("\t\t*MESH_NUMCVERTEX %d\n" % len(vertices))
    out.write("\t\t*MESH_CVERTLIST {\n")
    for i, (_, _, color, _) in enumerate(vertices):
        out.write("\t\t\t*MESH_VERTCOL %d\t%.3f\t%.3f\t%.3f\n" % (i, *color))
    out.write("\t\t}\n")
    out.write("\t\t*MESH_NUMCVFACES %d\n" % len(faces))
    out.write("\t\t*MESH_CFACELIST {\n")
    for i, (a, b, c) in enumerate(faces):
        out.write("\t\t\t*MESH_CFACE %d\t%d\t%d\t%d\n" % (i, a, b, c))
    out.write("\t\t}\n")
    if all(v[3] is not None for v in vertices):
        _write_authored_normals(out, vertices, faces)
    else:
        ase_util.write_mesh_normals(out, [v[0] for v in vertices], faces, smooth=True)
    out.write("\t}\n")
    out.write("\t*PROP_MOTIONBLUR 0\n\t*PROP_CASTSHADOW 1\n")
    out.write("\t*PROP_RECVSHADOW 1\n\t*MATERIAL_REF %d\n}\n" % material_ref)


def _write_authored_normals(out, vertices, faces):
    out.write("\t\t*MESH_NORMALS {\n")
    for i, (a, b, c) in enumerate(faces):
        pa, pb, pc = vertices[a][0], vertices[b][0], vertices[c][0]
        ux, uy, uz = (pb[0] - pa[0], pb[1] - pa[1], pb[2] - pa[2])
        vx, vy, vz = (pc[0] - pa[0], pc[1] - pa[1], pc[2] - pa[2])
        n = (uy * vz - uz * vy, uz * vx - ux * vz, ux * vy - uy * vx)
        l = math.sqrt(sum(x * x for x in n)) or 1.0
        out.write("\t\t\t*MESH_FACENORMAL %d\t%.4f\t%.4f\t%.4f\n"
                  % (i, n[0] / l, n[1] / l, n[2] / l))
        for idx in (a, b, c):
            nx, ny, nz = vertices[idx][3]
            out.write("\t\t\t\t*MESH_VERTEXNORMAL %d\t%.4f\t%.4f\t%.4f\n"
                      % (idx, nx, ny, nz))
    out.write("\t\t}\n")


MATERIAL_TMPL = (
    '\t*MATERIAL %(idx)d {\n'
    '\t\t*MATERIAL_NAME "%(name)s"\n\t\t*MATERIAL_CLASS "Standard"\n'
    "\t\t*MATERIAL_AMBIENT 0.588\t0.588\t0.588\n"
    "\t\t*MATERIAL_DIFFUSE 0.588\t0.588\t0.588\n"
    "\t\t*MATERIAL_SPECULAR 0.900\t0.900\t0.900\n"
    "\t\t*MATERIAL_SHINE 0.100\n\t\t*MATERIAL_SHADING Blinn\n"
    "\t\t*MATERIAL_SHINESTRENGTH 0.0\n"
    "\t\t*MATERIAL_SELFILLUM 0.0\n"
    '\t\t*MAP_DIFFUSE {\n\t\t\t*MAP_NAME "%(name)s"\n'
    '\t\t\t*MAP_CLASS "Bitmap"\n'
    '\t\t\t*BITMAP "%(texture)s"\n'
    "\t\t\t*MAP_TYPE Screen\n\t\t}\n\t}\n")


def assemble(args):
    panels = harvest_stock_kit(args.stock)

    def C(name):
        return os.path.join(args.common, name)

    # (geom name, fmdl path, keep_materials, biggest, kit kind, material, inset)
    pieces = []
    pieces.append(("shirt", args.undershirt, {"torso_mat"}, False, "body", "kit", 0.0))
    pieces.append(("shorts", C("pants_out_sub.fmdl"), None, False, "body", "kit", 0.0))
    pieces.append(("socks", C("socks_middle.fmdl"), None, False, "sock", "kit", 0.0))
    # skin under garments gets clearance so it cannot poke through in motion
    pieces.append(("arms", C("arm.fmdl"), {"arm_mat"}, False, None, "skin", 0.004))
    pieces.append(("thighs", C("thigh_short.fmdl"), {"thigh_mat"}, False, None, "skin", 0.004))
    pieces.append(("hand_l", C("hand_l.fmdl"), {"arm_mat"}, False, None, "skin", 0.0))
    pieces.append(("hand_r", C("hand_r.fmdl"), {"arm_mat"}, False, None, "skin", 0.0))
    pieces.append(("neck", C("neck.fmdl"), {"skin_head"}, False, None, "skin", 0.0))
    pieces.append(("face", args.face, {"fox_skin_mat"}, True, None, "face", 0.0))
    if args.hair:
        # the scalp/cranium ride the hair fmdl (fox_head_shell_mat) - a bare
        # face fmdl has no top of head
        pieces.append(("scalp", args.hair,
                       {"fox_skin_mat", "fox_head_shell_mat"}, False, None, "face", 0.0))
        pieces.append(("hair", args.hair, {"fox_hair_mat"}, False, None, "hair", 0.0))
    pieces.append(("boots", args.boots, None, False, None, "boots", 0.0))

    materials = [
        ("kit", "media/objects/players/textures/kit_template.png"),
        ("skin", "media/objects/players/textures/skin.jpg"),
        ("face", args.face_texture_ref),
        ("hair", args.hair_texture_ref),
        ("boots", args.boots_texture_ref),
    ]
    mat_index = {name: i for i, (name, _) in enumerate(materials)}

    os.makedirs(args.out_dir, exist_ok=True)
    ase_path = os.path.join(args.out_dir, "fullbody_pes.ase")
    with open(ase_path, "w") as out:
        out.write("*3DSMAX_ASCIIEXPORT\t200\n")
        out.write('*COMMENT "PES base player -> GF fullbody by tools/pes21_import/pes_base_body.py"\n')
        out.write("*SCENE {\n\t*SCENE_FILENAME \"fullbody\"\n")
        out.write("\t*SCENE_FIRSTFRAME 0\n\t*SCENE_LASTFRAME 100\n")
        out.write("\t*SCENE_FRAMESPEED 30\n\t*SCENE_TICKSPERFRAME 160\n")
        out.write("\t*SCENE_BACKGROUND_STATIC 0.000\t0.000\t0.000\n")
        out.write("\t*SCENE_AMBIENT_STATIC 0.000\t0.000\t0.000\n}\n")
        out.write("*MATERIAL_LIST {\n\t*MATERIAL_COUNT %d\n" % len(materials))
        for i, (name, tex) in enumerate(materials):
            out.write(MATERIAL_TMPL % {"idx": i, "name": name, "texture": tex})
        out.write("}\n")

        total_v = total_f = 0
        for name, path, keep, biggest, kit_kind, material, inset in pieces:
            fmdl = load_fmdl(path, args.fmdl_lib)
            meshes = piece_meshes(fmdl, keep, biggest)
            vertices, faces = gather_piece(fmdl, meshes, kit_kind, panels, inset)
            write_geomobject(out, name, vertices, faces, mat_index[material])
            total_v += len(vertices)
            total_f += len(faces)
            print("  %-8s %5d verts %5d faces (%s)" %
                  (name, len(vertices), len(faces), material))

    open(os.path.join(args.out_dir, "fullbody_pes.object"), "w").write(
        "<object>\n\n\t<geometry>\n"
        "\t\t<filename>models/fullbody_pes.ase</filename>\n"
        "\t\t<name>fullbody</name>\n"
        "\t\t<position>0, 0, 0</position>\n"
        "\t\t<rotation>0, 0, 0, 0</rotation>\n"
        "\t</geometry>\n\n</object>\n")

    # FaceRig weight map for the default head
    weights_path = os.path.join(args.out_dir, "faceweights.txt")
    face_weights.export(args.face, weights_path, args.fmdl_lib)
    print("total: %d verts, %d faces -> %s" % (total_v, total_f, ase_path))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("out_dir")
    parser.add_argument("--common", required=True,
                        help="extracted common_package .../character/common dir")
    parser.add_argument("--undershirt", required=True, help="undershirt.fmdl")
    parser.add_argument("--boots", required=True, help="a boots.fmdl")
    parser.add_argument("--face", required=True, help="a face_high.fmdl")
    parser.add_argument("--hair", default=None,
                        help="the matching hair_high.fmdl (scalp + hair)")
    parser.add_argument("--stock", required=True,
                        help="the migrated stock fullbody.ase (kit UV source)")
    parser.add_argument("--fmdl-lib", required=True)
    parser.add_argument("--face-texture-ref",
                        default="media/objects/players/textures/pes_base_face.png")
    parser.add_argument("--hair-texture-ref",
                        default="media/objects/players/textures/pes_base_hair.png")
    parser.add_argument("--boots-texture-ref",
                        default="media/objects/players/textures/pes_base_boots.png")
    args = parser.parse_args()
    assemble(args)


if __name__ == "__main__":
    main()
