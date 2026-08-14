"""One-shot PES 2021 -> data/imports/pes21 pack builder.

Given the PES 2021 install's Data directory, pulls and converts:

  portraits/    dt14  common/render/symbol/player/<playerID>.dds -> png
  animations/   dt13  body_anime_file*.mtar -> .gani -> GF .anim
  adboards/     dt30  Asset/model/bg/common/bill/**.ftex -> png
  chants/       dt44  CHANT.awb (AFS2/HCA) -> 44.1kHz ogg (needs ffmpeg)
  models/       dt36  face/real/<playerID>/face.fpk -> .ase + textures
                      (pass --faces id1,id2,... ; there are ~9k players)

  python3 build_pes21_pack.py "/path/to/PES21/Data" [--pack data/imports/pes21]
                              [--faces 100117,7511] [--skip portraits,anims,...]
"""

import argparse
import glob
import os
import shutil
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import awb
import cpk
import fpk
import ftex
import gani_to_anim


def log(msg):
    print("[pack] " + msg, flush=True)


def build_portraits(data_dir, pack, tmp):
    from PIL import Image
    src = os.path.join(tmp, "dt14")
    cpk.extract(os.path.join(data_dir, "dt14_all.cpk"), src,
                pattern="render/symbol/player")
    dest = os.path.join(pack, "portraits")
    os.makedirs(dest, exist_ok=True)
    n = 0
    for path in glob.glob(src + "/**/*.dds", recursive=True):
        pid = os.path.splitext(os.path.basename(path))[0]
        try:
            Image.open(path).save(os.path.join(dest, pid + ".png"))
            n += 1
        except Exception:
            pass
    log("portraits: %d" % n)


def build_animations(data_dir, pack, tmp):
    import mtar
    src = os.path.join(tmp, "dt13")
    cpk.extract(os.path.join(data_dir, "dt13_all.cpk"), src,
                pattern="FoxAnim/Body/body_anime")
    total = 0
    for mtar_path in sorted(glob.glob(src + "/**/body_anime_file*.mtar",
                                      recursive=True)):
        name = os.path.splitext(os.path.basename(mtar_path))[0]
        gani_dir = os.path.join(tmp, "ganis", name)
        mtar.extract(mtar_path, gani_dir)
        dest = os.path.join(pack, "animations", name)
        os.makedirs(dest, exist_ok=True)
        for gani_path in sorted(glob.glob(gani_dir + "/*.gani")):
            base = os.path.splitext(os.path.basename(gani_path))[0]
            try:
                text, _ = gani_to_anim.convert(open(gani_path, "rb").read())
                open(os.path.join(dest, base + ".anim"), "w").write(text)
                total += 1
            except Exception as exc:
                log("anim FAIL %s: %s" % (base, exc))
    log("animations: %d" % total)


def build_adboards(data_dir, pack, tmp):
    src = os.path.join(tmp, "dt30bill")
    cpk.extract(os.path.join(data_dir, "dt30_g4.cpk"), src, pattern="/bill/")
    dest = os.path.join(pack, "adboards")
    os.makedirs(dest, exist_ok=True)
    n = 0
    for path in glob.glob(src + "/**/*.ftex", recursive=True):
        parts = path.split(os.sep)
        setname = parts[parts.index("bill") + 1] if "bill" in parts else "misc"
        name = os.path.splitext(os.path.basename(path))[0]
        try:
            ftex.convert(path, os.path.join(dest, "%s__%s.png" % (setname, name)))
            n += 1
        except Exception:
            pass
    log("adboards: %d" % n)


def build_chants(data_dir, pack, tmp):
    src = os.path.join(tmp, "dt44")
    cpk.extract(os.path.join(data_dir, "dt44_all.cpk"), src,
                pattern="awb/chant/CHANT.awb")
    awb_path = glob.glob(src + "/**/CHANT.awb", recursive=True)[0]
    streams = awb.extract(awb_path, os.path.join(tmp, "chants"))
    dest = os.path.join(pack, "chants")
    os.makedirs(dest, exist_ok=True)
    n = 0
    for path in streams:
        base = os.path.splitext(os.path.basename(path))[0]
        out = os.path.join(dest, "chant_%s.ogg" % base)
        try:
            subprocess.run(["ffmpeg", "-y", "-loglevel", "error", "-i", path,
                            "-ar", "44100", "-c:a", "libvorbis", "-q:a", "5",
                            out], check=True)
            n += 1
        except subprocess.CalledProcessError:
            pass
    log("chants: %d" % n)


def build_faces(data_dir, pack, tmp, player_ids, fmdl_lib):
    import fmdl_to_ase
    for pid in player_ids:
        src = os.path.join(tmp, "face_" + pid)
        cpk.extract(os.path.join(data_dir, "dt36_g4.cpk"), src,
                    pattern="face/real/%s/" % pid)
        fpks = glob.glob(src + "/**/face.fpk", recursive=True)
        if not fpks:
            log("face %s: not found" % pid)
            continue
        unpacked = os.path.join(src, "unpacked")
        fpk.extract(fpks[0], unpacked)
        dest = os.path.join(pack, "models", "face_" + pid)
        os.makedirs(dest, exist_ok=True)
        for model in glob.glob(unpacked + "/**/*.fmdl", recursive=True):
            name = os.path.splitext(os.path.basename(model))[0]
            try:
                fmdl_to_ase.convert(model, os.path.join(dest, name + ".ase"),
                                    fmdl_lib, "face_bsm_alp.png")
            except Exception as exc:
                log("face %s %s: %s" % (pid, name, exc))
        for tex in glob.glob(src + "/**/*.ftex", recursive=True):
            name = os.path.splitext(os.path.basename(tex))[0]
            try:
                ftex.convert(tex, os.path.join(dest, name + ".png"))
            except Exception:
                pass
        log("face %s -> %s" % (pid, dest))


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("data_dir", help="PES 2021 Data directory (dt*.cpk)")
    parser.add_argument("--pack", default="data/imports/pes21")
    parser.add_argument("--faces", default="",
                        help="comma-separated player IDs for face import")
    parser.add_argument("--fmdl-lib", default="",
                        help="pes-fmdl addon dir (needed for --faces)")
    parser.add_argument("--skip", default="",
                        help="comma-separated: portraits,animations,adboards,chants")
    args = parser.parse_args()

    skip = set(filter(None, args.skip.split(",")))
    tmp = tempfile.mkdtemp(prefix="pes21_pack_")
    try:
        if "portraits" not in skip:
            build_portraits(args.data_dir, args.pack, tmp)
        if "animations" not in skip:
            build_animations(args.data_dir, args.pack, tmp)
        if "adboards" not in skip:
            build_adboards(args.data_dir, args.pack, tmp)
        if "chants" not in skip:
            build_chants(args.data_dir, args.pack, tmp)
        if args.faces:
            build_faces(args.data_dir, args.pack, tmp,
                        args.faces.split(","), args.fmdl_lib)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    log("pack ready: " + args.pack)
