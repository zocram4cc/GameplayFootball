"""Remaps fullbody.ase vertex-color joint IDs after a skeleton change.

The engine packs skin weights into vertex colors as jointID*10 + weight*9
(ASE stores /255), with joint IDs being the player.object DFS order. Adding
nodes (head, hands) shifts the DFS indices, so existing fullbody meshes
must have their color channels remapped.

  python3 migrate_fullbody_colors.py <fullbody.ase> --map 3:4,4:5,...
  (old:new pairs; unlisted IDs stay put)
"""

import argparse
import re

# player.object DFS: old 13-joint order -> new 16-joint order after
# inserting head (under neck), left_hand (under left_elbow) and right_hand
# (under right_elbow)
DEFAULT_MAP = {0: 0, 1: 1, 2: 2, 3: 4, 4: 5, 5: 7, 6: 8,
               7: 10, 8: 11, 9: 12, 10: 13, 11: 14, 12: 15}

LINE = re.compile(
    r"^(\s*\*MESH_VERTCOL\s+\d+\t)([0-9.]+)\t([0-9.]+)\t([0-9.]+)\s*$")


def remap_channel(text_value, mapping):
    value = float(text_value) * 255.0
    joint = int(value // 10)
    weight_code = value - joint * 10
    new_joint = mapping.get(joint, joint)
    return "%.3f" % ((new_joint * 10 + weight_code) / 255.0)


def migrate(path, mapping):
    out_lines = []
    changed = 0
    for line in open(path):
        m = LINE.match(line.rstrip("\n"))
        if m:
            channels = [remap_channel(v, mapping) for v in m.groups()[1:]]
            out_lines.append(m.group(1) + "\t".join(channels) + "\n")
            changed += 1
        else:
            out_lines.append(line)
    open(path, "w").write("".join(out_lines))
    return changed


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("ase")
    parser.add_argument("--map", default=None,
                        help="comma-separated old:new pairs (default: the "
                             "13->16 joint migration)")
    args = parser.parse_args()
    mapping = DEFAULT_MAP
    if args.map:
        mapping = {int(a): int(b) for a, b in
                   (pair.split(":") for pair in args.map.split(","))}
    print("remapped %d color lines in %s" % (migrate(args.ase, mapping), args.ase))
