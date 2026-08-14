"""Installs converted pack animations into the live game's anim collection.

The engine scans data/media/animations recursively at startup, so installing
is copying the .anim in with the metadata the target class needs. Installed
files are named pes_<name>.anim (one .gitignore rule keeps generated content
out of the repo).

Classes:
  happy_normal    celebration, specialvar1=1 specialvar2=1
  happy_extreme   celebration, specialvar1=1 specialvar2=2
  sad_normal      celebration, specialvar1=2 specialvar2=1
  entrance_lineup match entrance, specialvar1=3 specialvar2=1
                  (played while the teams stand in line before kickoff)

  python3 install_anims.py <src.anim> --class happy_normal [--game-dir data]
  python3 install_anims.py <dir> --class entrance_lineup --batch
"""

import argparse
import os

CLASSES = {
    "happy_normal": ("celebration/happy_normal", {"specialvar1": 1, "specialvar2": 1}),
    "happy_extreme": ("celebration/happy_extreme", {"specialvar1": 1, "specialvar2": 2}),
    "sad_normal": ("celebration/sad_normal", {"specialvar1": 2, "specialvar2": 1}),
    "entrance_lineup": ("entrance/lineup", {"specialvar1": 3, "specialvar2": 1}),
}


def install(src, anim_class, game_data_dir="data"):
    subdir, variables = CLASSES[anim_class]

    # keep the curve lines, replace the metadata tail
    lines = []
    for line in open(src):
        if line.startswith("<"):
            break
        lines.append(line.rstrip("\n"))
    for key, value in variables.items():
        lines.append("<%s>" % key)
        lines.append("\t%s" % value)
        lines.append("</%s>" % key)
    lines.append("<type>")
    lines.append("\tspecial")
    lines.append("</type>")

    name = "pes_" + os.path.splitext(os.path.basename(src))[0] + ".anim"
    dest = os.path.join(game_data_dir, "media", "animations", subdir, name)
    os.makedirs(os.path.dirname(dest), exist_ok=True)
    open(dest, "w").write("\n".join(lines) + "\n")
    return dest


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("src")
    parser.add_argument("--class", dest="anim_class", required=True,
                        choices=sorted(CLASSES))
    parser.add_argument("--game-dir", default="data")
    parser.add_argument("--batch", action="store_true",
                        help="src is a directory of .anim files")
    args = parser.parse_args()
    if not args.batch:
        print("installed", install(args.src, args.anim_class, args.game_dir))
        return
    count = 0
    for name in sorted(os.listdir(args.src)):
        if not name.endswith(".anim"):
            continue
        install(os.path.join(args.src, name), args.anim_class, args.game_dir)
        count += 1
    print("installed %d animations as %s" % (count, args.anim_class))


if __name__ == "__main__":
    main()
