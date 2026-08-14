"""Fox Engine animation (.gani) reader — full curve decoder.

PES 2021 ganis are GZ-generation (v1): every file carries its own TrackHeader.
Layout (all verified against dt13 body archives):

  0x00  u32  magic 0x0BFCA2D2, u32 header size, u32 total size
  0x20  FoxData node, Name = StrCode32("MOTION") = 0x08908348
  0x50  char[16] "MOTION"
  0x60  u32  StrCode32 of the animation's own name
  0x90  TrackHeader:
          u32 UnitCount, u32 SegmentCount, u16 TrackId, u8, u8,
          u32 FrameCount, u32 FrameRate (low byte = per-delta time scale)
        u32 unitOffsets[UnitCount]   self-relative to the TrackHeader
  per unit:
        TrackUnit { u32 Name (StrCode32), u8 SegmentCount, u8 Flags, u16 pad }
        TrackData[SegmentCount] { i32 DataOffset (self-relative), i16 MsId,
                                  u8 type(low4)|nextEntry(high4), u8 ComponentBitSize }

Curve streams (decode math is a port of the Fox_Parser/FoxBrowser AnimBitReader,
itself a 1:1 port of the decode paths in the engine binary):

  quat   bitstream of 16-bit LE words: [key0][u8 delta, key]... until the summed
         deltas reach FrameCount. A key = theta, x, y (ComponentBitSize bits
         each) + 3 sign bits. Dequant: x=b/mask, y=c/mask, z=(1-x)-y, normalize
         (x,y,z), scale by sin(theta/mask*pi/2); w=cos(...); signs flip x/y/z.
  vector byte stream: [key0 comps][u8 delta, comps]...; each comp an AnimHalf
         (custom 16-bit float) or float32 by ComponentBitSize. Hermite units
         (flag 2) carry an extra tangent per key after the first.
  static units (flag 4) store only key0.
"""

import math
import struct

GANI_MAGIC = 0x0BFCA2D2
MOTION_NODE = 0x08908348  # StrCode32("MOTION")

# segment types
QUAT = 0
FLOAT = 1
VECTOR2 = 2
VECTOR3 = 3
VECTOR4 = 4
QUAT_DIFF = 5
VECTOR_DIFF = 6

_COMP_COUNT = {FLOAT: 1, VECTOR2: 2, VECTOR3: 3, VECTOR_DIFF: 3, VECTOR4: 4}

# unit flags
LOOP = 1
HERMITE = 2
STATIC = 4


class Segment:
    def __init__(self):
        self.type = QUAT
        self.component_bits = 0
        self.data_offset = 0          # absolute
        self.data_end = 0             # absolute, one past the blob (set on decode)
        self.ms_id = 0
        self.deltas = []              # deltas[0] == 0; frame k = sum(deltas[:k+1])
        self.quats = []               # (x, y, z, w) for QUAT / QUAT_DIFF
        self.vecs = []                # component tuples otherwise
        self.tans = []                # hermite tangents (tans[0] unset)

    @property
    def frames(self):
        """Absolute frame index of each key."""
        acc, out = 0, []
        for d in self.deltas:
            acc += d
            out.append(acc)
        return out


class Unit:
    def __init__(self):
        self.name_hash = 0
        self.flags = 0
        self.segments = []


class Gani:
    def __init__(self):
        self.total_size = 0
        self.name_hash = 0            # StrCode32 of the animation's name
        self.frame_count = 0
        self.frame_scale = 1
        self.units = []

    @property
    def tracks(self):
        """All segments flattened in unit order (the frig's track order)."""
        return [seg for unit in self.units for seg in unit.segments]


def _read_bits(blob, bit_pos, bit_size):
    if bit_size == 0:
        return 0, bit_pos
    byte_pos = bit_pos >> 3
    bit_offset = bit_pos & 7
    total = (bit_offset + bit_size + 7) >> 3
    raw = int.from_bytes(blob[byte_pos:byte_pos + total], "little")
    return (raw >> bit_offset) & ((1 << bit_size) - 1), bit_pos + bit_size


def _dequant_quat(a, b, c, signs, bit_size):
    mask = float((1 << bit_size) - 1)
    x = b / mask
    y = c / mask
    half_theta = (a / mask) * math.pi * 0.5
    z = (1.0 - x) - y
    lensq = (z * z + y * y) + x * x
    inv_len = 1.0 / math.sqrt(lensq) if lensq > 0.0 else 0.0
    sx = -1.0 if signs & 1 else 1.0
    sy = -1.0 if signs & 2 else 1.0
    sz = -1.0 if signs & 4 else 1.0
    f = math.sin(half_theta) * inv_len
    return (x * sx * f, y * sy * f, z * sz * f, math.cos(half_theta))


def _read_quat(blob, bit_pos, bit_size):
    a, bit_pos = _read_bits(blob, bit_pos, bit_size)
    b, bit_pos = _read_bits(blob, bit_pos, bit_size)
    c, bit_pos = _read_bits(blob, bit_pos, bit_size)
    signs, bit_pos = _read_bits(blob, bit_pos, 3)
    return _dequant_quat(a, b, c, signs, bit_size), bit_pos


def _anim_half(blob, offset):
    """The engine's custom 16-bit float: sign<<16, (exp+0x1DC00)<<13, mant<<13."""
    value = blob[offset] | (blob[offset + 1] << 8)
    num = value & 0x7C00
    if num > 0:
        num = ((num + 0x1DC00) << 13) & 0xFFFFFFFF
    num |= ((value & 0x8000) << 16) | ((value & 0x3FF) << 13)
    return struct.unpack("<f", struct.pack("<I", num))[0]


def _read_comps(blob, offset, count, component_bits):
    out = []
    for _ in range(count):
        if component_bits == 16:
            out.append(_anim_half(blob, offset))
            offset += 2
        elif component_bits == 0:
            out.append(0.0)
        else:
            out.append(struct.unpack_from("<f", blob, offset)[0])
            offset += 4
    return tuple(out), offset


def _decode_segment(blob, seg, static, hermite, frame_count):
    seg.deltas = [0]
    if seg.type in (QUAT, QUAT_DIFF):
        bit_pos = seg.data_offset * 8
        quat, bit_pos = _read_quat(blob, bit_pos, seg.component_bits)
        seg.quats = [quat]
        if not static:
            acc = 0
            while acc < frame_count:
                delta, bit_pos = _read_bits(blob, bit_pos, 8)
                acc += delta
                quat, bit_pos = _read_quat(blob, bit_pos, seg.component_bits)
                seg.quats.append(quat)
                seg.deltas.append(delta)
                if delta == 0 and acc < frame_count:
                    raise ValueError("stuck quat stream")
        seg.data_end = (bit_pos + 7) >> 3
        return

    comps = _COMP_COUNT.get(seg.type, 3)
    offset = seg.data_offset
    vec, offset = _read_comps(blob, offset, comps, seg.component_bits)
    seg.vecs = [vec]
    if hermite:
        seg.tans = [tuple(0.0 for _ in range(comps))]
    if not static:
        acc = 0
        while acc < frame_count:
            delta = blob[offset]
            offset += 1
            acc += delta
            vec, offset = _read_comps(blob, offset, comps, seg.component_bits)
            seg.vecs.append(vec)
            if hermite:
                tan, offset = _read_comps(blob, offset, comps, seg.component_bits)
                seg.tans.append(tan)
            seg.deltas.append(delta)
            if delta == 0 and acc < frame_count:
                raise ValueError("stuck vector stream")
    seg.data_end = offset


def parse(blob: bytes, decode: bool = True) -> Gani:
    (magic,) = struct.unpack_from("<I", blob, 0)
    if magic != GANI_MAGIC:
        raise ValueError("not a gani: %08x" % magic)

    gani = Gani()
    (gani.total_size,) = struct.unpack_from("<I", blob, 8)

    motion_at = blob.find(b"MOTION")
    if motion_at < 0:
        raise ValueError("no MOTION section")
    (gani.name_hash,) = struct.unpack_from("<I", blob, motion_at + 16)

    header_at = motion_at + 0x40
    unit_count, segment_count, _track_id = struct.unpack_from("<IIH", blob, header_at)
    gani.frame_count, frame_rate = struct.unpack_from("<II", blob, header_at + 12)
    gani.frame_scale = struct.unpack_from("<b", blob, header_at + 16)[0]
    if not (1 <= unit_count <= 256 and 1 <= segment_count <= 1024):
        raise ValueError("implausible track header at 0x%x" % header_at)

    unit_offsets = struct.unpack_from("<%dI" % unit_count, blob, header_at + 20)

    total_segments = 0
    for unit_offset in unit_offsets:
        if unit_offset == 0:
            continue
        at = header_at + unit_offset
        unit = Unit()
        unit.name_hash, seg_count, unit.flags = struct.unpack_from("<IBB", blob, at)
        at += 8
        for _ in range(seg_count):
            seg = Segment()
            data_offset, seg.ms_id, packed, seg.component_bits = \
                struct.unpack_from("<ihBB", blob, at)
            seg.type = packed & 0x0F
            seg.data_offset = at + data_offset
            at += 8
            unit.segments.append(seg)
        total_segments += seg_count
        gani.units.append(unit)

    if total_segments != segment_count:
        raise ValueError("segment count mismatch: %d != %d"
                         % (total_segments, segment_count))

    if decode:
        for unit in gani.units:
            static = bool(unit.flags & STATIC)
            hermite = bool(unit.flags & HERMITE)
            for seg in unit.segments:
                _decode_segment(blob, seg, static, hermite, gani.frame_count)
    return gani


def validate(gani: Gani):
    """Sanity checks on a decoded gani; returns a list of problem strings."""
    problems = []
    for u, unit in enumerate(gani.units):
        for s, seg in enumerate(unit.segments):
            tag = "unit%d/seg%d" % (u, s)
            if not (unit.flags & STATIC):
                if seg.frames[-1] < gani.frame_count:
                    problems.append("%s: keys end at %d < %d frames"
                                    % (tag, seg.frames[-1], gani.frame_count))
            for q in seg.quats:
                norm = math.sqrt(sum(c * c for c in q))
                if abs(norm - 1.0) > 1e-3:
                    problems.append("%s: quat norm %.4f" % (tag, norm))
                    break
            for a, b in zip(seg.quats, seg.quats[1:]):
                dot = abs(sum(x * y for x, y in zip(a, b)))
                if dot < 0.5:
                    problems.append("%s: quat jump (|dot| %.3f)" % (tag, dot))
                    break
            for v in seg.vecs:
                if any(not math.isfinite(c) or abs(c) > 1e5 for c in v):
                    problems.append("%s: implausible vector %r" % (tag, v))
                    break
    return problems


if __name__ == "__main__":
    import sys

    blob = open(sys.argv[1], "rb").read()
    gani = parse(blob)
    print("size %d, name hash %08x, %d frames (scale %d), %d units / %d tracks" %
          (gani.total_size, gani.name_hash, gani.frame_count, gani.frame_scale,
           len(gani.units), len(gani.tracks)))
    for u, unit in enumerate(gani.units):
        for s, seg in enumerate(unit.segments):
            kind = {QUAT: "quat", FLOAT: "float", VECTOR2: "vec2", VECTOR3: "vec3",
                    VECTOR4: "vec4", QUAT_DIFF: "quatd", VECTOR_DIFF: "vecd"}[seg.type]
            keys = len(seg.quats) or len(seg.vecs)
            sample = seg.quats[0] if seg.quats else seg.vecs[0]
            print("  unit %2d seg %d %-5s bits=%2d flags=%d keys=%3d  first=%s" %
                  (u, s, kind, seg.component_bits, unit.flags, keys,
                   "(" + ", ".join("%.3f" % c for c in sample) + ")"))
    problems = validate(gani)
    print("validation:", "OK" if not problems else "%d problems" % len(problems))
    for p in problems[:10]:
        print("  !", p)
