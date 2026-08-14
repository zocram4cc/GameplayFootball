"""PES Motion ARchive (.mtar) reader.

Layout, as reverse-engineered from PES 2021's dt13 body archives:

  0x00  u32  magic 0x0BFFA89C
  0x04  u32  entry count
  0x08  u16  ?, u16 ?  (seen 0x15, 0x1d)
  0x10  padding to 0x20
  0x20  entry table, 16 bytes per entry:
          u32 nameHash   (StrCode32 of the animation name)
          u32 index      (sequential id, low bits sometimes flags)
          u32 offset     (absolute file offset of the .gani blob)
          u32 size       (blob size in bytes)

Each blob is a complete .gani file (magic 0x0BFCA2D2).
"""

import os
import struct

MTAR_MAGIC = 0x0BFFA89C
GANI_MAGIC = 0x0BFCA2D2


class MtarEntry:
    def __init__(self, name_hash, index, offset, size):
        self.name_hash = name_hash
        self.index = index
        self.offset = offset
        self.size = size


def read_entries(path):
    with open(path, "rb") as f:
        magic, count = struct.unpack("<II", f.read(8))
        if magic != MTAR_MAGIC:
            raise ValueError("not an mtar: magic %08x" % magic)
        f.seek(0x20)
        entries = []
        for _ in range(count):
            name_hash, index, offset, size = struct.unpack("<IIII", f.read(16))
            entries.append(MtarEntry(name_hash, index, offset, size))
        return entries


def extract(path, dest, dictionary=None):
    """Extracts every .gani blob; names come from the dictionary or the hash."""
    entries = read_entries(path)
    os.makedirs(dest, exist_ok=True)
    written = 0
    with open(path, "rb") as f:
        for entry in entries:
            f.seek(entry.offset)
            blob = f.read(entry.size)
            if len(blob) < 4 or struct.unpack("<I", blob[:4])[0] != GANI_MAGIC:
                continue  # padding or foreign chunk
            name = None
            if dictionary is not None:
                name = dictionary.lookup(entry.name_hash)
            if not name:
                name = "anim_%08x" % entry.name_hash
            with open(os.path.join(dest, name + ".gani"), "wb") as o:
                o.write(blob)
            written += 1
    return written


if __name__ == "__main__":
    import sys

    entries = read_entries(sys.argv[1])
    print("entries:", len(entries))
    for entry in entries[:10]:
        print("  %08x idx=%-6d off=0x%08x size=%d" %
              (entry.name_hash, entry.index, entry.offset, entry.size))
    if len(sys.argv) > 2:
        print("extracted:", extract(sys.argv[1], sys.argv[2]))
