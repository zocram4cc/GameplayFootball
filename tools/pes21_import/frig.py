"""Fox Engine .frig rig parser (PES 2021 body_skel / face_skel).

Layout (little-endian, decoded from dt13):
  0x00 u32 magic          StrCode32 of the rig name ("HumanBody", "Face")
  0x04 u32 nameOffset     offset of the plain-text rig name
  0x08 u32 ?              0x66
  0x0C u32 unitCount      matches every gani's TrackHeader.UnitCount
  0x10 u32 trackCount     matches TrackHeader.SegmentCount
  0x14 u32 fileSize
  0x18 u32 boneTableOffset
  0x20 u32 unitOffsets[unitCount]

Each unit record starts with u16 fields {kind, 0, segCount, ...} and ends
with a row of u16 indices pairing the unit's bones with its global track
numbers. The bone table at boneTableOffset is (count, then {u32 index,
u32 StrCode32 boneHash} pairs).

parse() returns units with their (bone index, track index) pairs plus the
hash-ordered bone list; resolve bone names by hashing candidates with
strcode.strcode32 (fmdl bone tables are the natural dictionary).
"""

import struct


class Unit:
    def __init__(self):
        self.offset = 0
        self.seg_count = 0
        self.pairs = []          # (bone_index, track_index) best-effort


class Frig:
    def __init__(self):
        self.magic = 0
        self.name = ""
        self.unit_count = 0
        self.track_count = 0
        self.bone_hashes = []    # in table order; index = bone index


def bone_table(blob: bytes):
    """-> (rig name, [bone hash]) from the header alone.

    The unit records are the fiddly part of a .frig and a caller that only
    wants to know which bones a rig drives does not need them - the hand-pose
    import (hand_poses.py) reads the bone order and nothing else.
    """
    (_magic, name_off, _x, _unit_count, _track_count,
     _file_size, bone_table_off) = struct.unpack_from("<7I", blob, 0)
    end = blob.find(b"\0", name_off)
    name = blob[name_off:end].decode("ascii", "replace")
    (bone_count,) = struct.unpack_from("<I", blob, bone_table_off)
    hashes = []
    at = bone_table_off + 8
    while at + 4 <= len(blob) and len(hashes) < bone_count:
        (h,) = struct.unpack_from("<I", blob, at)
        hashes.append(h)
        at += 8
    return name, hashes


def parse(blob: bytes) -> Frig:
    rig = Frig()
    (rig.magic, name_off, _x, rig.unit_count, rig.track_count,
     file_size, bone_table_off) = struct.unpack_from("<7I", blob, 0)

    end = blob.find(b"\0", name_off)
    rig.name = blob[name_off:end].decode("ascii", "replace")

    unit_offsets = struct.unpack_from("<%dI" % rig.unit_count, blob, 0x20)

    # bone table: u32 count, then {u32 something, u32 hash} pairs -- the
    # hashes land on 8-byte stride starting count+4 further in
    (bone_count,) = struct.unpack_from("<I", blob, bone_table_off)
    rig.bone_hashes = []
    at = bone_table_off + 8
    while at + 4 <= len(blob) and len(rig.bone_hashes) < bone_count:
        (h,) = struct.unpack_from("<I", blob, at)
        rig.bone_hashes.append(h)
        at += 8

    rig.units = []
    bounds = list(unit_offsets) + [bone_table_off]
    for i in range(rig.unit_count):
        unit = Unit()
        unit.offset = unit_offsets[i]
        unit.seg_count = struct.unpack_from("<H", blob, unit_offsets[i] + 4)[0]
        # final row: 2*segCount u16 indices (bones ++ tracks, interleaved
        # with the last pair separated -- see body_skel analysis)
        row_at = bounds[i + 1] - 16
        vals = struct.unpack_from("<8H", blob, row_at)
        n = unit.seg_count
        idx = [v for v in vals[:2 * n]]
        if n == 1:
            unit.pairs = [(idx[0], idx[1])]
        elif n == 2:
            # one bone, two channels (rotation + translation)
            unit.pairs = [(idx[0], idx[1]), (idx[0], idx[2])]
        else:
            # bones[0..n-2], tracks[0..n-2], bone[n-1], track[n-1]
            bones = idx[:n - 1] + [idx[2 * (n - 1)]]
            tracks = idx[n - 1:2 * (n - 1)] + [idx[2 * n - 1]]
            unit.pairs = list(zip(bones, tracks))
        rig.units.append(unit)
    return rig


if __name__ == "__main__":
    import sys
    import strcode

    blob = open(sys.argv[1], "rb").read()
    rig = parse(blob)
    print("rig %s: %d units / %d tracks, %d bones" %
          (rig.name, rig.unit_count, rig.track_count, len(rig.bone_hashes)))

    names = {}
    if len(sys.argv) > 2:  # optional newline-separated candidate name list
        for line in open(sys.argv[2]):
            name = line.strip()
            if name:
                names[strcode.strcode32(name)] = name
    for i, h in enumerate(rig.bone_hashes):
        print("  bone %2d %08x %s" % (i, h, names.get(h, "")))
    total = sum(u.seg_count for u in rig.units)
    print("segment sum:", total, "(header says %d)" % rig.track_count)
