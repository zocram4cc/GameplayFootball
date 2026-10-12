#!/usr/bin/env python3
"""Dev oracle: the C++ gani decoder (pesfmt_dump gani) against GameplayFootball's
gani.py, key for key. Samples every Nth body clip in archive order.

usage: parity_gani.py --gf-tools DIR --pes21-root DIR [--every 88] [--dump build/apps/pesfmt_dump]
"""
import argparse
import importlib
import json
import struct
import subprocess
import sys

TOLERANCE = 1e-6
BODY_MTARS = ["common/anime/FoxAnim/Body/body_anime_file%d.mtar" % i for i in range(10)]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--gf-tools", required=True)
    ap.add_argument("--pes21-root", required=True)
    ap.add_argument("--every", type=int, default=88)
    ap.add_argument("--dump", default="build/apps/pesfmt_dump")
    args = ap.parse_args()
    sys.path.insert(0, args.gf_tools)
    gani = importlib.import_module("gani")

    def dump(*a):
        return subprocess.run([args.dump, *a, "--pes21-root", args.pes21_root], check=True,
                              capture_output=True).stdout

    clips = []  # (archive, hash, blob)
    for path in BODY_MTARS:
        data = dump("cat", path)
        count = struct.unpack_from("<I", data, 4)[0]
        for i in range(count):
            h, _, off, size = struct.unpack_from("<IIII", data, 0x20 + 16 * i)
            blob = data[off:off + size]
            if struct.unpack_from("<I", blob, 0)[0] == gani.GANI_MAGIC:
                clips.append((h, blob))

    worst, compared, fails = 0.0, 0, 0
    for h, blob in clips[::args.every]:
        ref = gani.parse(blob)
        ours = json.loads(dump("gani", "0x%08x" % h))
        if ours["frameCount"] != ref.frame_count or len(ours["units"]) != len(ref.units):
            print("FAIL %08x: header differs" % h)
            fails += 1
            continue
        for ru, ou in zip(ref.units, ours["units"]):
            for rs, os_ in zip(ru.segments, ou["segments"]):
                if rs.frames != os_["frames"]:
                    print("FAIL %08x: key frames differ" % h)
                    fails += 1
                    continue
                for a, b in zip(rs.quats, os_["quats"]):
                    worst = max(worst, max(abs(x - y) for x, y in zip(a, b)))
                for a, b in zip(rs.vecs, os_["vecs"]):
                    worst = max(worst, max(abs(x - y) for x, y in zip(a, b)))
        compared += 1
    bad = worst > TOLERANCE
    print("compared %d of %d clips, worst component difference %.2e, %d structural failures"
          % (compared, len(clips), worst, fails))
    return 1 if fails or bad else 0


if __name__ == "__main__":
    sys.exit(main())
