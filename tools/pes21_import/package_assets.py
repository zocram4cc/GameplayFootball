"""Packages PES / aesthetic-export assets into GameplayFootball's formats.

Everything lands under data/imports/<pack>/ so imported content stays separate
from stock assets and a pack can be removed by deleting one directory:

  data/imports/<pack>/portraits/<player>.png     player portraits
  data/imports/<pack>/adboards/<name>.png        billboard textures
                                                 (copy into data/media/textures/adboards
                                                  or point stadium config at them)
  data/imports/<pack>/chants/<name>.wav          crowd audio, 44.1kHz WAV
  data/imports/<pack>/models/<player>.ase        converted full-body models

Portraits and any .dds textures convert through Pillow; audio converts through
ffmpeg (PES ships .adx/.wav inside its sound cpks; ffmpeg decodes ADX natively).
"""

import argparse
import os
import shutil
import subprocess


def convert_dds(src, dest_png):
    from PIL import Image

    image = Image.open(src)
    os.makedirs(os.path.dirname(dest_png), exist_ok=True)
    image.save(dest_png)


def convert_audio(src, dest_wav):
    os.makedirs(os.path.dirname(dest_wav), exist_ok=True)
    subprocess.run(
        ["ffmpeg", "-y", "-loglevel", "error", "-i", src, "-ar", "44100", dest_wav],
        check=True)


def package_hdg_export(export_dir, pack_dir):
    """Packages a 4cc aesthetic export (Faces/, Portraits/, Kit Textures/...)."""
    converted = {"portraits": 0, "adboards": 0}

    portraits = os.path.join(export_dir, "Portraits")
    if os.path.isdir(portraits):
        for name in sorted(os.listdir(portraits)):
            if not name.lower().endswith(".dds"):
                continue
            convert_dds(os.path.join(portraits, name),
                        os.path.join(pack_dir, "portraits",
                                     os.path.splitext(name)[0] + ".png"))
            converted["portraits"] += 1

    # per-player portrait.dds inside the Faces folders
    faces = os.path.join(export_dir, "Faces")
    if os.path.isdir(faces):
        for player_dir in sorted(os.listdir(faces)):
            src = os.path.join(faces, player_dir, "portrait.dds")
            if os.path.isfile(src):
                convert_dds(src, os.path.join(pack_dir, "portraits",
                                              player_dir + ".png"))
                converted["portraits"] += 1

    logos = os.path.join(export_dir, "Logo")
    if os.path.isdir(logos):
        for name in sorted(os.listdir(logos)):
            if name.lower().endswith(".dds"):
                convert_dds(os.path.join(logos, name),
                            os.path.join(pack_dir, "adboards",
                                         os.path.splitext(name)[0] + ".png"))
                converted["adboards"] += 1

    return converted


def package_chants(audio_dir, pack_dir):
    converted = 0
    for root, _, files in os.walk(audio_dir):
        for name in sorted(files):
            if os.path.splitext(name)[1].lower() not in (".adx", ".wav", ".ogg", ".hca"):
                continue
            dest = os.path.join(pack_dir, "chants",
                                os.path.splitext(name)[0] + ".wav")
            try:
                convert_audio(os.path.join(root, name), dest)
                converted += 1
            except subprocess.CalledProcessError:
                pass  # unsupported codec variant; skip, do not abort the pack
    return converted


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--export", help="4cc aesthetic export directory")
    parser.add_argument("--chants", help="directory of PES audio files")
    parser.add_argument("--pack", required=True,
                        help="destination: data/imports/<pack>")
    args = parser.parse_args()

    if args.export:
        print(package_hdg_export(args.export, args.pack))
    if args.chants:
        print("chants:", package_chants(args.chants, args.pack))
