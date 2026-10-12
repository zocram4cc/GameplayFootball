#!/usr/bin/env python3
"""Dev oracle: the C++ rig (pesfmt_dump rig) against GameplayFootball's
retarget.py.

1. Algorithm parity (must pass, 1e-5): GF's own _build_align and
   _extend_bind_with_fingers are re-run on the full-precision values read from
   the user's install, and must reproduce every C++ anim position and W.
   GF reuses the left toe/hand tip for the right side; PES stores each side, so
   the right-side bones are checked in a second run fed the right-side values.
2. Transcription report (informational): how far GF's hand-typed PES_BIND /
   PES_ALIGN tables sit from the file values.

usage: parity_rig.py --gf-tools DIR --pes21-root DIR [--dump build/apps/pesfmt_dump]
"""
import argparse
import importlib
import json
import math
import subprocess
import sys

TOLERANCE = 1e-5
ROOT = "sk_root_hip"   # GF calls PES's mover "motion"


def run(dump, root, *args):
    out = subprocess.run([dump, *args, "--pes21-root", root], check=True, capture_output=True)
    return json.loads(out.stdout)


def qdist(a, b):
    dot = sum(x * y for x, y in zip(a, b))
    s = 1.0 if dot >= 0 else -1.0                 # q and -q are the same rotation
    return max(abs(x - s * y) for x, y in zip(a, b))


def vdist(a, b):
    return max(abs(x - y) for x, y in zip(a, b))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--gf-tools", required=True)
    ap.add_argument("--pes21-root", required=True)
    ap.add_argument("--dump", default="build/apps/pesfmt_dump")
    args = ap.parse_args()

    rig = run(args.dump, args.pes21_root, "rig")
    high = {b["name"]: b["local"] for b in run(
        args.dump, args.pes21_root, "ask", "common/anime/FoxAnim/Body/CharacterAssets/body_high_skel.ask")}
    body_skl = {b["name"]: b["position"] for b in run(
        args.dump, args.pes21_root, "skl", "Asset/model/character/#Win/common_package.fpk", "/body.skl")}

    sys.path.insert(0, args.gf_tools)
    rt = importlib.import_module("retarget")
    gf_bind = {k: v[0] for k, v in rt.PES_BIND.items()}
    gf_align = dict(rt.PES_ALIGN)

    def gf_key(pes):
        return "motion" if pes == ROOT else pes

    by_pes = {b["pes"]: b for b in rig}
    parent_of = {k: v[1] for k, v in rt.PES_BIND.items()}

    def oracle(side):
        """Re-run GF's algorithm on file values, tips taken from `side`."""
        rt.PES_BIND = {gf_key(b["pes"]): (tuple(b["animGlobal"]), parent_of[gf_key(b["pes"])])
                       for b in rig if not b["pes"].startswith("skh_")}
        rt.PES_RENDER_BIND = {gf_key(b["pes"]): (tuple(b["renderGlobal"]), parent_of[gf_key(b["pes"])])
                              for b in rig}
        rt._A_TOE_L = tuple(body_skl["dsk_toe_l"])
        rt._A_TOE_R = tuple(body_skl["dsk_toe_r"])
        rt._T_TOE_OFF = tuple(high["sk_toe" + side])
        tip = high["tip_sk_hand" + side]
        # GF mirrors the left tip for the right hand: (-x, y, z)
        rt._T_HAND_TIP_OFF = tuple(tip) if side == "_l" else (-tip[0], tip[1], tip[2])
        rt._T_HEAD_TIP_OFF = tuple(high["tip_sk_head"])
        rt.PES_ALIGN = rt._build_align()
        rt._extend_bind_with_fingers()
        return {k: v[0] for k, v in rt.PES_BIND.items()}, dict(rt.PES_ALIGN)

    fails = 0
    worst_pos = worst_w = 0.0
    for side in ("_l", "_r"):
        pos, align = oracle(side)
        for b in rig:
            pes = b["pes"]
            if pes.endswith("_l" if side == "_r" else "_r"):
                continue                      # checked in the other side's run
            if side == "_r" and not pes.endswith("_r"):
                continue                      # unsided bones: checked in the _l run
            key = gf_key(pes)
            w = align["sk_hand" + pes[-2:]] if pes.startswith("skh_") else align[key]
            dp, dw = vdist(pos[key], b["animGlobal"]), qdist(w, b["align"])
            worst_pos, worst_w = max(worst_pos, dp), max(worst_w, dw)
            if dp > TOLERANCE or dw > TOLERANCE:
                fails += 1
                print("PARITY FAIL %-20s pos %.2e  W %.2e" % (b["gf"], dp, dw))
    print("algorithm parity: %d bones, worst position %.2e m, worst W %.2e, %d failures"
          % (len(rig), worst_pos, worst_w, fails))

    print("\nGF transcribed tables vs file values (informational):")
    tp = max(vdist(gf_bind[gf_key(b["pes"])], b["animGlobal"]) for b in rig)
    tw = []
    for b in rig:
        key = gf_key(b["pes"])
        w = gf_align["sk_hand" + key[-2:]] if key.startswith("skh_") else gf_align[key]
        tw.append((qdist(w, b["align"]), b["gf"]))
    worst = max(tw)
    print("  PES_BIND anim positions: worst %.2e m" % tp)
    print("  PES_ALIGN W:             worst %.2e (%s)" % worst)
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
