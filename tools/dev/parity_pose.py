#!/usr/bin/env python3
"""Dev oracle: C++ pose sampling (pesfmt_dump pose) against GameplayFootball's
gani_to_anim.py build_samplers / sample_root, at several fractional frames of
every Nth body clip. GF's bind height is replaced by the file value so only the
sampling is compared.

usage: parity_pose.py --gf-tools DIR --pes21-root DIR [--every 200] [--dump build/apps/pesfmt_dump]
"""
import argparse
import importlib
import json
import struct
import subprocess
import sys

TOLERANCE = 1e-5
SAMPLE_FRACTIONS = (0.0, 0.37, 0.5, 0.91, 1.0)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--gf-tools", required=True)
    ap.add_argument("--pes21-root", required=True)
    ap.add_argument("--every", type=int, default=200)
    ap.add_argument("--dump", default="build/apps/pesfmt_dump")
    args = ap.parse_args()
    sys.path.insert(0, args.gf_tools)
    gani = importlib.import_module("gani")
    g2a = importlib.import_module("gani_to_anim")
    retarget = importlib.import_module("retarget")

    def dump(*a):
        return subprocess.run([args.dump, *a, "--pes21-root", args.pes21_root], check=True,
                              capture_output=True).stdout

    rig = json.loads(dump("rig"))
    gf_of_pes = {b["pes"]: b for b in rig}
    retarget.PES_BIND["motion"] = (tuple(gf_of_pes["sk_root_hip"]["animGlobal"]), None)

    clips = []
    for i in range(10):
        data = dump("cat", "common/anime/FoxAnim/Body/body_anime_file%d.mtar" % i)
        for k in range(struct.unpack_from("<I", data, 4)[0]):
            h, _, off, size = struct.unpack_from("<IIII", data, 0x20 + 16 * k)
            if struct.unpack_from("<I", data, off)[0] == gani.GANI_MAGIC:
                clips.append((h, data[off:off + size]))

    worst, compared = 0.0, 0
    for h, blob in clips[::args.every]:
        ref = gani.parse(blob)
        bones, rq, rp, mq, mp = g2a.build_samplers(ref)
        for frac in SAMPLE_FRACTIONS:
            t = frac * ref.frame_count
            body_q, body_p = g2a.sample_root(bones, rq, rp, mq, mp, t,
                                             scale=retarget.PES_POS_TO_M_GAMEPLAY)
            ours = json.loads(dump("pose", "0x%08x" % h, repr(t)))
            sign = 1.0 if sum(a * b for a, b in zip(body_q, ours["bodyRotation"])) >= 0 else -1.0
            worst = max(worst, max(abs(a - sign * b) for a, b in zip(body_q, ours["bodyRotation"])))
            worst = max(worst, max(abs(a - b) for a, b in zip(body_p, ours["bodyPosition"])))
            for joint in ours["joints"]:
                ref_q = bones[joint["pes"]].quat(t) if joint["pes"] in bones else (0.0, 0.0, 0.0, 1.0)
                s = 1.0 if sum(a * b for a, b in zip(ref_q, joint["local"])) >= 0 else -1.0
                worst = max(worst, max(abs(a - s * b) for a, b in zip(ref_q, joint["local"])))
        compared += 1
    print("compared %d clips x %d times, worst difference %.2e" % (compared, len(SAMPLE_FRACTIONS), worst))
    return 0 if worst <= TOLERANCE else 1


if __name__ == "__main__":
    sys.exit(main())
