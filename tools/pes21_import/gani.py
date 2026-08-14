"""Fox Engine animation (.gani) structural reader.

What is decoded so far (PES 2021, dt13):

  0x00  u32  magic 0x0BFCA2D2
  0x04  u32  header size (0x20)
  0x08  u32  total size
  0x14  u32  chunk count
  0x20  chunk header: u32 id, u32 header size (0x30), ... chunk sizes
  0x50  char[16] "MOTION"
  0x60  u32  motion name hash (StrCode32)
  0x68  u32  ?, u32 data offset (0x30-relative)
  0x90  u32  ?, u32 track count, u16 flags, ...
  0xA0  u32[trackCount] track offsets (stride observed 0x18 or 0x20)

Each track carries a bone-name hash and a compressed curve. The curve encoding
(quantized quaternion splines) is NOT decoded yet - that is the remaining work
before body animations can be converted; see docs/PES21_IMPORT.md.
"""

import struct

GANI_MAGIC = 0x0BFCA2D2


class GaniInfo:
    def __init__(self):
        self.total_size = 0
        self.motion_hash = 0
        self.track_count = 0
        self.track_offsets = []


def survey(blob: bytes) -> GaniInfo:
    info = GaniInfo()
    (magic,) = struct.unpack_from("<I", blob, 0)
    if magic != GANI_MAGIC:
        raise ValueError("not a gani: %08x" % magic)
    (info.total_size,) = struct.unpack_from("<I", blob, 8)

    motion_at = blob.find(b"MOTION")
    if motion_at < 0:
        return info
    (info.motion_hash,) = struct.unpack_from("<I", blob, motion_at + 16)

    # the track table header sits 0x40 past the MOTION tag
    header_at = motion_at + 0x40
    if header_at + 8 <= len(blob):
        _, info.track_count = struct.unpack_from("<II", blob, header_at)
        table_at = header_at + 0x10
        for i in range(min(info.track_count, 512)):
            at = table_at + i * 4
            if at + 4 > len(blob):
                break
            (offset,) = struct.unpack_from("<I", blob, at)
            info.track_offsets.append(offset)
    return info


if __name__ == "__main__":
    import sys

    blob = open(sys.argv[1], "rb").read()
    info = survey(blob)
    print("size %d, motion hash %08x, tracks %d" %
          (info.total_size, info.motion_hash, info.track_count))
    print("first offsets:", ["0x%x" % o for o in info.track_offsets[:8]])
