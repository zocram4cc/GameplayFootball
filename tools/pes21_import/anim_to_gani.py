"""Converts a GameplayFootball .anim back into a PES .gani (round trip).

This is the export half of the pipeline: convert a PES animation to the
open text .anim format with gani_to_anim.py, edit it with any text editor,
then re-encode it here against the ORIGINAL gani as a structural template
(same units, segment types, bit sizes and frame count; only the curves are
replaced).

Inverse retarget (mirrors gani_to_anim.solve_gf):
  body        -> dsk_hip local (RIG_ROOT / motion node quats identity)
  middle      -> sk_belly (sk_chest identity)
  neck        -> sk_head (sk_neck identity)
  thigh/knee  -> shortest-arc world rotations reproducing the limb
                 directions; ankle -> sk_foot orientation
  shoulder/elbow -> sk_upperarm / sk_forearm (clavicle+hand keep identity)
  player line -> RIG_ROOT XZ + motion-node Y positions

Curve encoding is the engine's: quantized quaternions (halfTheta/x/y at the
template's ComponentBitSize + 3 sign bits, L1-renormalized) and AnimHalf
(IEEE half of value/128) vectors, one key per PES frame. The auxiliary IK
vec3 channels are copied verbatim from the template.

  python3 anim_to_gani.py edited.anim --template original.gani out.gani
"""

import argparse
import math
import struct

import gani
import retarget
from anim_preview import parse_anim
from gani_to_anim import (GF_NODES, PES_FRAME_MS, GF_FRAME_MS,
                          q_mul, q_conj, q_norm, q_rot, q_nlerp,
                          v_sub, v_normalize)


# --- GF-side sampling ---------------------------------------------------------

def sample_quat(keys, frame):
    if frame <= keys[0][0]:
        return keys[0][1:5]
    for a, b in zip(keys, keys[1:]):
        if b[0] >= frame:
            span = b[0] - a[0]
            t = (frame - a[0]) / span if span else 0.0
            return q_nlerp(a[1:5], b[1:5], t)
    return keys[-1][1:5]


def sample_vec(keys, frame):
    if frame <= keys[0][0]:
        return keys[0][1:4]
    for a, b in zip(keys, keys[1:]):
        if b[0] >= frame:
            span = b[0] - a[0]
            t = (frame - a[0]) / span if span else 0.0
            return tuple(x + (y - x) * t for x, y in zip(a[1:4], b[1:4]))
    return keys[-1][1:4]


# --- GF -> Fox coordinate mapping (inverse of gani_to_anim.map_*) -------------

def unmap_quat(q):
    return (q[0], q[2], -q[1], q[3])


def unmap_vec(v):
    return (v[0], v[2], -v[1])


def shortest_arc(a, b):
    """Quaternion rotating unit vector a onto unit vector b."""
    d = a[0] * b[0] + a[1] * b[1] + a[2] * b[2]
    if d > 0.999999:
        return (0.0, 0.0, 0.0, 1.0)
    if d < -0.999999:
        axis = (1.0, 0.0, 0.0) if abs(a[0]) < 0.9 else (0.0, 1.0, 0.0)
        c = (a[1] * axis[2] - a[2] * axis[1],
             a[2] * axis[0] - a[0] * axis[2],
             a[0] * axis[1] - a[1] * axis[0])
        c = v_normalize(c)
        return (c[0], c[1], c[2], 0.0)
    c = (a[1] * b[2] - a[2] * b[1],
         a[2] * b[0] - a[0] * b[2],
         a[0] * b[1] - a[1] * b[0])
    q = (c[0], c[1], c[2], 1.0 + d)
    return q_norm(q)


def _bind_dir(child, parent):
    return v_normalize(v_sub(retarget.PES_BIND[child][0],
                             retarget.PES_BIND[parent][0]))


def solve_pes(player_pos, gf_local):
    """GF node locals at one frame -> PES bone local quats + root positions."""
    # GF world orientations
    w = {}
    for node in GF_NODES:
        parent = retarget.GF_BIND[node][1]
        q = gf_local.get(node, (0, 0, 0, 1))
        w[node] = q if parent is None else q_norm(q_mul(w[parent], q))

    down = (0.0, 0.0, -1.0)
    locals_fox = {}

    hip = unmap_quat(w["body"])
    locals_fox["dsk_hip"] = hip
    chest_w = unmap_quat(w["middle"])                    # = belly (chest id)
    locals_fox["sk_belly"] = chest_w
    locals_fox["sk_chest"] = (0, 0, 0, 1)
    neck_w = unmap_quat(w["neck"])
    locals_fox["sk_neck"] = q_norm(q_mul(q_conj(chest_w), neck_w))
    head_w = unmap_quat(w.get("head", w["neck"]))
    locals_fox["sk_head"] = q_norm(q_mul(q_conj(neck_w), head_w))

    for side, sign in (("left", "l"), ("right", "r")):
        thigh_dir = unmap_vec(q_rot(w[side + "_thigh"], down))
        shin_dir = unmap_vec(q_rot(w[side + "_knee"], down))
        thigh_w = shortest_arc(_bind_dir("sk_leg_" + sign, "sk_thigh_" + sign),
                               thigh_dir)
        leg_w = shortest_arc(_bind_dir("sk_foot_" + sign, "sk_leg_" + sign),
                             shin_dir)
        locals_fox["sk_thigh_" + sign] = q_norm(q_mul(q_conj(hip), thigh_w))
        locals_fox["sk_leg_" + sign] = q_norm(q_mul(q_conj(thigh_w), leg_w))
        foot_w = unmap_quat(w[side + "_ankle"])
        locals_fox["sk_foot_" + sign] = q_norm(q_mul(q_conj(leg_w), foot_w))

        up_dir = unmap_vec(q_rot(w[side + "_shoulder"], down))
        fore_dir = unmap_vec(q_rot(w[side + "_elbow"], down))
        up_w = shortest_arc(_bind_dir("sk_forearm_" + sign, "sk_upperarm_" + sign),
                            up_dir)
        fore_w = shortest_arc(_bind_dir("sk_hand_" + sign, "sk_forearm_" + sign),
                              fore_dir)
        locals_fox["sk_shoulder_" + sign] = (0, 0, 0, 1)
        locals_fox["sk_upperarm_" + sign] = q_norm(q_mul(q_conj(chest_w), up_w))
        locals_fox["sk_forearm_" + sign] = q_norm(q_mul(q_conj(up_w), fore_w))
        hand_w = unmap_quat(w.get(side + "_hand", w[side + "_elbow"]))
        locals_fox["sk_hand_" + sign] = q_norm(q_mul(q_conj(fore_w), hand_w))

    # root: player (x, y) horizontal + z above GF body height
    px, py, pz = player_pos
    root_pos = unmap_vec((px, py, 0.0))                       # fox XZ, mm*128
    hip_bind = retarget.PES_BIND["motion"][0]
    hip_height = pz + retarget.GF_BODY_HEIGHT
    motion_y = hip_height - hip_bind[1]
    scale = 1.0 / retarget.PES_POS_TO_M
    return locals_fox, \
        (root_pos[0] * scale, 0.0, root_pos[2] * scale), \
        (0.0, motion_y * scale, 0.0)


# --- gani curve encoding -------------------------------------------------------

class BitWriter:
    def __init__(self):
        self.data = bytearray()
        self.bitpos = 0

    def write(self, value, bits):
        for i in range(bits):
            if self.bitpos >> 3 >= len(self.data):
                self.data.append(0)
            if (value >> i) & 1:
                self.data[self.bitpos >> 3] |= 1 << (self.bitpos & 7)
            self.bitpos += 1

    def bytes(self):
        return bytes(self.data)


def encode_quat(writer, q, bits):
    x, y, z, w = q_norm(q)
    if w < 0.0:
        x, y, z, w = -x, -y, -z, -w
    mask = (1 << bits) - 1
    half_theta = math.acos(max(-1.0, min(1.0, w)))
    a = min(mask, int(round(half_theta / (math.pi * 0.5) * mask)))
    ax, ay, az = abs(x), abs(y), abs(z)
    total = ax + ay + az
    if total < 1e-9:
        b = c = 0
    else:
        b = min(mask, int(round(ax / total * mask)))
        c = min(mask, int(round(ay / total * mask)))
    signs = (1 if x < 0 else 0) | (2 if y < 0 else 0) | (4 if z < 0 else 0)
    writer.write(a, bits)
    writer.write(b, bits)
    writer.write(c, bits)
    writer.write(signs, 3)


def float_to_animhalf(value):
    """Inverse of gani._anim_half: IEEE half bits of value/128."""
    v = value / 128.0
    bits = struct.unpack("<I", struct.pack("<f", v))[0]
    sign = (bits >> 16) & 0x8000
    exp = (bits >> 23) & 0xFF
    mant = bits & 0x7FFFFF
    if exp == 0:
        return sign
    e = exp - 127 + 15
    if e >= 31:
        return sign | 0x7BFF                 # clamp to half max
    if e <= 0:
        return sign
    return sign | (e << 10) | (mant >> 13)


def encode_quat_stream(quats, bits):
    """key0 + one key per frame (delta 1), engine-decodable."""
    writer = BitWriter()
    encode_quat(writer, quats[0], bits)
    for q in quats[1:]:
        writer.write(1, 8)
        encode_quat(writer, q, bits)
    return writer.bytes()


def encode_vec_stream(vecs):
    out = bytearray()
    for comp in vecs[0]:
        out += struct.pack("<H", float_to_animhalf(comp))
    for v in vecs[1:]:
        out.append(1)
        for comp in v:
            out += struct.pack("<H", float_to_animhalf(comp))
    return bytes(out)


def encode_static_quat(q, bits):
    writer = BitWriter()
    encode_quat(writer, q, bits)
    return writer.bytes()


# --- gani writing --------------------------------------------------------------

def build(anim_path, template_path, out_path):
    template = open(template_path, "rb").read()
    tg = gani.parse(template)                # decoded: gives blob spans
    frame_count = tg.frame_count

    player, nodes = parse_anim(anim_path)

    # sample the .anim on the PES frame grid and inverse-retarget
    per_bone = {}
    root_q, root_p, mot_q, mot_p = [], [], [], []
    for f in range(frame_count + 1):
        gf_frame = f * PES_FRAME_MS / GF_FRAME_MS
        gf_local = {node: sample_quat(nodes[node], gf_frame)
                    for node in GF_NODES if node in nodes}
        ppos = sample_vec(player, gf_frame)
        locals_fox, rig_pos, motion_pos = solve_pes(ppos, gf_local)
        for bone, q in locals_fox.items():
            per_bone.setdefault(bone, []).append(q)
        root_q.append((0.0, 0.0, 0.0, 1.0))
        root_p.append(rig_pos)
        mot_q.append((0.0, 0.0, 0.0, 1.0))
        mot_p.append(motion_pos)

    # encode every segment following the template's structure
    blobs = []                               # (unit_idx, seg_idx, bytes)
    for u, unit in enumerate(tg.units):
        static = bool(unit.flags & gani.STATIC)
        for s, seg in enumerate(unit.segments):
            bone = retarget.PES_TRACK_MAP.get((u, s))
            if seg.type in (gani.QUAT, gani.QUAT_DIFF):
                if u == retarget.PES_ROOT_UNIT:
                    quats = root_q
                elif u == retarget.PES_MOTION_UNIT:
                    quats = mot_q
                elif bone and bone in per_bone:
                    quats = per_bone[bone]
                else:
                    quats = [seg.quats[0]] * (frame_count + 1)
                if static:
                    blob = encode_static_quat(quats[0], seg.component_bits)
                else:
                    blob = encode_quat_stream(quats, seg.component_bits)
            elif seg.type in (gani.VECTOR3, gani.VECTOR_DIFF):
                if u == retarget.PES_ROOT_UNIT:
                    blob = encode_vec_stream(root_p)
                elif u == retarget.PES_MOTION_UNIT:
                    blob = encode_vec_stream(mot_p)
                else:
                    # auxiliary IK channel: keep the template's bytes
                    blob = template[seg.data_offset:seg.data_end]
            else:
                blob = template[seg.data_offset:seg.data_end]
            blobs.append((u, s, blob))

    # lay out: template header + track table stay put; blobs follow
    header_at = template.find(b"MOTION") + 0x40
    table_end = max(seg.data_offset for u in tg.units for seg in u.segments
                    if True)
    first_blob = min(seg.data_offset for u in tg.units for seg in u.segments)
    out = bytearray(template[:first_blob])

    # write blobs 16-byte aligned, patch each TrackData.DataOffset
    unit_offsets = struct.unpack_from(
        "<%dI" % len(tg.units), template, header_at + 20)
    for (u, s, blob) in blobs:
        while len(out) % 4:
            out.append(0)
        at = len(out)
        # TrackData entry position inside the (unchanged) track table:
        entry_at = header_at + unit_offsets[u] + 8 + s * 8
        struct.pack_into("<i", out, entry_at, at - entry_at)
        out += blob
    while len(out) % 16:
        out.append(0)

    # patch sizes: total at 0x08, MOTION payload at 0x70 (total - 0x90)
    struct.pack_into("<I", out, 0x08, len(out))
    struct.pack_into("<I", out, 0x70, len(out) - 0x90)

    open(out_path, "wb").write(bytes(out))
    return len(out)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("anim")
    parser.add_argument("out")
    parser.add_argument("--template", required=True,
                        help="the original .gani (structure donor)")
    args = parser.parse_args()
    size = build(args.anim, args.template, args.out)
    print("wrote %s (%d bytes)" % (args.out, size))

    # self-check: the writer's output must decode cleanly
    g = gani.parse(open(args.out, "rb").read())
    problems = gani.validate(g)
    problems = [p for p in problems if "implausible vector" not in p]
    print("re-decode: %d units / %d tracks, %d frames, validation %s"
          % (len(g.units), len(g.tracks), g.frame_count,
             "OK" if not problems else problems[:3]))
