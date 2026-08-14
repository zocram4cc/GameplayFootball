"""CRI AWB (AFS2) audio container splitter (PES chant/crowd banks).

  AFS2 header: 4s magic, u8 version, u8 offsetSize, u8 idSize, u8 pad,
               u32 entryCount, u32 alignment,
               entryCount x id (idSize), (entryCount+1) x offset (offsetSize)
  Entry data runs from align(offset[i]) to offset[i+1].
"""

import os
import struct
import sys


def entries(blob: bytes):
    if blob[:4] != b"AFS2":
        raise ValueError("not an AFS2/AWB file")
    offset_size = blob[5]
    id_size = blob[6]
    count, align = struct.unpack_from("<II", blob, 8)
    at = 16
    ids = []
    for _ in range(count):
        ids.append(int.from_bytes(blob[at:at + id_size], "little"))
        at += id_size
    offsets = []
    for _ in range(count + 1):
        offsets.append(int.from_bytes(blob[at:at + offset_size], "little"))
        at += offset_size
    out = []
    for i in range(count):
        start = (offsets[i] + align - 1) // align * align
        out.append((ids[i], start, offsets[i + 1]))
    return out


def extract(awb_path, dest):
    blob = open(awb_path, "rb").read()
    os.makedirs(dest, exist_ok=True)
    written = []
    for eid, start, end in entries(blob):
        data = blob[start:end]
        ext = {b"HCA\0": "hca", b"\x80\x00": "adx"}.get(data[:4], None)
        if ext is None:
            ext = "hca" if data[:3] == b"HCA" else ("adx" if data[:1] == b"\x80" else "bin")
        path = os.path.join(dest, "%05d.%s" % (eid, ext))
        with open(path, "wb") as o:
            o.write(data)
        written.append(path)
    return written


if __name__ == "__main__":
    files = extract(sys.argv[1], sys.argv[2] if len(sys.argv) > 2 else "awb_out")
    print("%d streams -> %s" % (len(files), os.path.dirname(files[0]) if files else "-"))
