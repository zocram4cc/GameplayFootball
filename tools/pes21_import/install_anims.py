"""Installs converted pack animations into the live game's anim collection.

The engine scans data/media/animations recursively at startup, so installing
is copying the .anim in with the metadata the target class needs. Installed
files are named pes_<name>.anim (one .gitignore rule keeps generated content
out of the repo).

Classes:
  happy_normal   celebration, specialvar1=1 specialvar2=1
  happy_extreme  celebration, specialvar1=1 specialvar2=2
  sad_normal     celebration, specialvar1=2 specialvar2=1

  python3 install_anims.py <src.anim> --class happy_normal [--game-dir data]
"""

import argparse
import os

CLASSES = {
    "happy_normal": ("celebration/happy_normal", {"specialvar1": 1, "specialvar2": 1}),
    "happy_extreme": ("celebration/happy_extreme", {"specialvar1": 1, "specialvar2": 2}),
    "sad_normal": ("celebration/sad_normal", {"specialvar1": 2, "specialvar2": 1}),
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


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("src")
    parser.add_argument("--class", dest="anim_class", required=True,
                        choices=sorted(CLASSES))
    parser.add_argument("--game-dir", default="data")
    args = parser.parse_args()
    print("installed", install(args.src, args.anim_class, args.game_dir))
