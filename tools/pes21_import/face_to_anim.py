"""Exports PES face expression ganis to an open text format (.faceanim).

Face expressions live as NAMED loose ganis in dt13
(common/anime/FoxAnim/Face/Animations/{Base,Add}/*.gani, e.g.
"angr_brwnit_bite_soft"), 32 units / 59 tracks over the face_skel.frig rig
(skf_* muscle bones). Most are one-frame poses meant for blending.

Output mirrors GameplayFootball's .anim text format, with skf_* bone names:

  <bone>,<frame>,<qx>,<qy>,<qz>,<qw>[,...]      rotation tracks
  <bone>_pos,<frame>,<x>,<y>,<z>[,...]          translation tracks (metres)

The engine-side face rig (see docs/PES21_IMPORT.md) will read these.

  python3 face_to_anim.py <face_skel.frig> <in_dir_or_gani> <out_dir>
"""

import os
import sys

import frig
import gani
import retarget


def bone_names(rig, candidates):
    import strcode
    table = {strcode.strcode32(n): n for n in candidates}
    return [table.get(h, "bone_%08x" % h) for h in rig.bone_hashes]


def convert(gani_blob, rig, names):
    g = gani.parse(gani_blob)
    lines = []
    for u, unit in enumerate(g.units):
        pairs = rig.units[u].pairs if u < len(rig.units) else []
        for s, seg in enumerate(unit.segments):
            bone = names[pairs[s][0]] if s < len(pairs) else "unit%d_seg%d" % (u, s)
            if seg.quats:
                parts = [bone]
                for f, q in zip(seg.frames, seg.quats):
                    parts.append("%d,%f,%f,%f,%f" % ((f,) + q))
                lines.append(",".join(parts))
            elif seg.vecs and any(any(abs(c) < 1e6 for c in v) for v in seg.vecs):
                parts = [bone + "_pos"]
                for f, v in zip(seg.frames, seg.vecs):
                    metres = tuple(c * retarget.PES_POS_TO_M for c in v)
                    parts.append("%d,%f,%f,%f" % ((f,) + metres))
                lines.append(",".join(parts))
    lines.append("<frames>")
    lines.append("\t%d" % g.frame_count)
    lines.append("</frames>")
    return "\n".join(lines) + "\n"


if __name__ == "__main__":
    rig = frig.parse(open(sys.argv[1], "rb").read())
    src, dest = sys.argv[2], sys.argv[3]
    os.makedirs(dest, exist_ok=True)

    # skf_* names are plain text in any face fmdl; simplest built-in list:
    candidates = ["RIG_ROOT", "skf_jaw", "skf_glabella", "skf_nose",
                  "skf_doublechin", "skf_lip_volume", "skf_lip_b_c",
                  "skf_lip_t_c", "skf_tongue"]
    for base in ["brow_o", "brow_i", "nasolabialfold_t", "orbicularisoculi_b",
                 "lip_b", "lip_t", "lip_s", "eyelid_t", "eyelid_b",
                 "nosewing", "cheek", "cheek_s", "eye"]:
        candidates += ["skf_%s_l" % base, "skf_%s_r" % base]
    names = bone_names(rig, candidates)

    paths = []
    if os.path.isdir(src):
        for root, _, files in os.walk(src):
            paths += [os.path.join(root, f) for f in sorted(files)
                      if f.endswith(".gani")]
    else:
        paths = [src]

    done = 0
    for path in paths:
        name = os.path.splitext(os.path.basename(path))[0]
        try:
            text = convert(open(path, "rb").read(), rig, names)
            open(os.path.join(dest, name + ".faceanim"), "w").write(text)
            done += 1
        except Exception as exc:
            print("FAIL %s: %s" % (name, exc))
    print("exported %d face animations -> %s" % (done, dest))
