"""Fox Engine .skl skeleton parser (PES 2021 common_package body/face/hand).

This is the authoritative bone list Konami ships with the base character
package (dt00_x64: Asset/model/character/#Win/common_package.fpk ->
/Assets/pes16/model/character/common/body.skl): every bone the character
system knows, with its parent and its global bind transform. body.skl holds
175 bones; face.skl and hand_[lr].skl the face/finger rigs.

Layout (little-endian, decoded here):
  0x00 u32 version      12 in all shipped skl files
  0x04 u32 boneCount
  0x08 u32 recordSize   0x38 (56) in all shipped skl files
  0x0C boneCount x record:
       u32 nameOffset   absolute, into the string table after the records
       i32 parent       bone index, -1 for roots
       f32[12]          row-major 3x4 global bind matrix (rotation | position):
                        rows (r00 r01 r02 tx / r10 r11 r12 ty / r20 r21 r22 tz)
  then NUL-terminated name strings.

Note the parents: only the sk_* chains are parented (sk_belly and dsk_hip are
the two roots); the dsk_*/tip_*/pos_* helper bones sit parentless because Fox
drives them with runtime constraints, not the hierarchy. The animation rig
(body_skel.frig) adds RIG_ROOT and the "motion" mover above dsk_hip/sk_belly.
"""

import struct


class SklBone:
    def __init__(self, name, parent, matrix):
        self.name = name
        self.parent = parent            # index, -1 = root
        self.matrix = matrix            # 12 floats, row-major 3x4
        self.position = (matrix[3], matrix[7], matrix[11])  # global bind, Fox coords


def parse(blob: bytes):
    """-> list of SklBone in file order."""
    version, count, record_size = struct.unpack_from("<3I", blob, 0)
    if record_size != 56:
        raise ValueError("unexpected .skl record size %d" % record_size)
    bones = []
    for i in range(count):
        at = 12 + i * record_size
        name_off, parent = struct.unpack_from("<Ii", blob, at)
        matrix = struct.unpack_from("<12f", blob, at + 8)
        name = blob[name_off:blob.find(b"\0", name_off)].decode("ascii")
        bones.append(SklBone(name, parent, matrix))
    return bones


def parse_file(path):
    with open(path, "rb") as f:
        return parse(f.read())


if __name__ == "__main__":
    import sys
    bones = parse_file(sys.argv[1])
    print("%d bones" % len(bones))
    kids = {}
    for i, b in enumerate(bones):
        kids.setdefault(b.parent, []).append(i)

    def walk(i, depth):
        b = bones[i]
        print("  " * depth + "%-24s (%7.4f %7.4f %7.4f)" % ((b.name,) + b.position))
        for k in kids.get(i, []):
            walk(k, depth + 1)

    for root in kids.get(-1, []):
        walk(root, 0)
