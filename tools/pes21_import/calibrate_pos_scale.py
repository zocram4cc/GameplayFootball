"""Measures the metres-per-raw-unit of PES .gani position tracks.

The import originally read the position curves as millimetres with the
half-float exponent rebased x128, i.e. 1/128000 m per raw unit. Playing the
converted clips back exposes that as wrong by a large factor: at 1/128000 a
sprinting player's feet never come within 15 cm of the pitch, and a sliding
tackle keeps its pelvis at standing height and swims through the air. Root
motion is not decoration for GameplayFootball -- the engine reads incoming
and outgoing velocity straight off the root track to decide which situations
a clip may be selected for, and every ball contact is placed by forward
kinematics off the same track.

So the scale is measured instead of assumed, against three things that are
true of any human animation regardless of engine:

  stance    the foot a runner is standing on does not slide along the ground
  floor     the lowest the ankle ever gets is the bind pose's flat stance
            (0.107 m in the PES skeleton) -- feet reach the grass, and no
            further
  lying     a player lying on the pitch has his pelvis a hand's width up,
            about 0.14 m, not at standing height and not underground

Each is scored as a squared error in units of its own uncertainty, summed,
and swept over candidate scales.

  python3 calibrate_pos_scale.py <dir-of-ganis>...
"""

import argparse
import glob
import math
import os
import statistics
import sys

import gani
import gani_to_anim as g2a
import retarget

# clips the sweep leans on, by name; missing ones are skipped
LOCOMOTION = ["run_3_3_000_holdmiss", "run_2_4_000", "run_3_3_045",
              "run_3_3_090", "walk_2_4_000", "run_4_4_000",
              "run_3_3_090_relax", "run_2_2_000", "walk_2_2_000"]
LYING = ["dm_injury_faceup_endure_Rknee", "riseup_faceup_0_0_060",
         "dm_injury_tumbleLoop_Rknee", "dm_tu_idle_0_0_idle_fallBackWard",
         "dm_injury_endureLoop_Rknee"]

BIND_ANKLE_HEIGHT = retarget.PES_BIND["sk_foot_l"][0][1]   # 0.107 m
LYING_PELVIS_HEIGHT = 0.14

# tolerances: how much error in each measure counts as "one unit of wrong"
STANCE_TOLERANCE = 0.6      # m/s of residual slip is normal in mocap
FLOOR_TOLERANCE = 0.04      # m
PELVIS_TOLERANCE = 0.06     # m


def trace(path, scale):
    """(stance slip m/s, lowest ankle m, lowest pelvis m) at a given scale."""
    g = gani.parse(open(path, "rb").read())
    bones, root_q, root_p, mot_q, mot_p = g2a.build_samplers(g)
    feet = {"l": [], "r": []}
    pelvis = []
    for f in range(g.frame_count):
        _, pos = g2a.fk_pose(bones, root_q, root_p, mot_q, mot_p, float(f),
                             scale=scale)
        for side in "lr":
            feet[side].append(pos["sk_foot_" + side])
        pelvis.append(pos["motion"][1])
    slips = [min(math.hypot(feet[s][i][0] - feet[s][i - 1][0],
                            feet[s][i][2] - feet[s][i - 1][2]) * 59.94
                 for s in "lr")
             for i in range(1, g.frame_count)]
    slips.sort()
    stance = statistics.mean(slips[:max(1, len(slips) // 3)]) if slips else 0.0
    floor = min(min(p[1] for p in feet["l"]), min(p[1] for p in feet["r"]))
    return stance, floor, min(pelvis)


def find(dirs, names):
    out = []
    for name in names:
        for d in dirs:
            hit = glob.glob(os.path.join(d, name + ".gani"))
            if hit:
                out.append(hit[0])
                break
    return out


def sweep(dirs, low=12000, high=30000, step=1000):
    loco = find(dirs, LOCOMOTION)
    lying = find(dirs, LYING)
    if not loco:
        raise SystemExit("no locomotion reference clips found in %s" % (dirs,))
    print("references: %d locomotion, %d lying" % (len(loco), len(lying)))
    print("%-10s %8s %10s %10s %8s" %
          ("scale", "slip", "footfloor", "lyingpelvis", "score"))
    rows = []
    for inv in range(low, high + 1, step):
        scale = 1.0 / inv
        stance, floors = [], []
        for path in loco:
            s, f, _ = trace(path, scale)
            stance.append(s)
            floors.append(f)
        pelvises = [trace(path, scale)[2] for path in lying]
        slip = statistics.mean(stance)
        floor = statistics.median(floors)
        pelvis = statistics.median(pelvises) if pelvises else LYING_PELVIS_HEIGHT
        score = ((slip / STANCE_TOLERANCE) ** 2 +
                 ((floor - BIND_ANKLE_HEIGHT) / FLOOR_TOLERANCE) ** 2 +
                 ((pelvis - LYING_PELVIS_HEIGHT) / PELVIS_TOLERANCE) ** 2)
        rows.append((score, inv, slip, floor, pelvis))
        print("1/%-8d %8.3f %10.3f %10.3f %8.2f" %
              (inv, slip, floor, pelvis, score))
    rows.sort()
    print("\nbest 1/%d (score %.2f); shipping constant is 1/%d"
          % (rows[0][1], rows[0][0], round(1.0 / retarget.PES_POS_TO_M_GAMEPLAY)))
    return rows


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("dirs", nargs="+", help="directories of extracted .gani")
    parser.add_argument("--low", type=int, default=12000)
    parser.add_argument("--high", type=int, default=30000)
    parser.add_argument("--step", type=int, default=1000)
    args = parser.parse_args()
    sweep(args.dirs, args.low, args.high, args.step)


if __name__ == "__main__":
    main()
