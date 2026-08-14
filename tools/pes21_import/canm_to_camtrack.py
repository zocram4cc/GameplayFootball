"""Exports PES camera cuts (.fdc/.canm, see camera_cut.py) to the engine's
open .camtrack text format.

One line per frame, already in GameplayFootball space (metres, Z up,
vertical FOV in degrees):

  <frame>,<px>,<py>,<pz>,<qx>,<qy>,<qz>,<qw>,<fov>,<near>,<far>

Frames are global across the fdc's whole cut timeline (30 fps): each cut's
clip is stamped from its start_frame, so hard cuts between shots are
preserved exactly as authored.

  python3 canm_to_camtrack.py ent_020_st002_cam_1.fdc out.camtrack
"""

import sys

import camera_cut


def export(fdc_path, out_path):
    fdc = camera_cut.load(fdc_path)
    lines = []
    total = 0
    for cut, canm in fdc.timeline():
        if canm is None:
            continue
        for f in range(canm.frame_count):
            g = camera_cut.to_gf(canm, f)
            p, q = g["position"], g["rotation"]
            near = max(0.1, cut.near if cut.near > 0 else g["near"])
            far = cut.far if cut.far > 0 else g["far"]
            lines.append("%d,%.4f,%.4f,%.4f,%.6f,%.6f,%.6f,%.6f,%.3f,%.2f,%.1f"
                         % ((cut.start_frame + f,) + tuple(p) + tuple(q)
                            + (g["fov"], near, far)))
            total += 1
    open(out_path, "w").write("\n".join(lines) + "\n")
    return len(fdc.cuts), total


if __name__ == "__main__":
    cuts, frames = export(sys.argv[1], sys.argv[2])
    print("wrote %s: %d cuts, %d frames" % (sys.argv[2], cuts, frames))
