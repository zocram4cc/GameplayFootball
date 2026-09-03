"""Reconverts every installed PES-derived animation from its source gani.

Run after any change to the gani->anim conversion (retarget tables,
gani_to_anim math): the installed content is derived data, and this is the
one reproducible way back to it. Three passes, each idempotent:

  cutscenes   data/media/cutscenes/**/*.anim, in place. Source ganis come
              from the dt12 fixdemo extraction; each clip keeps its own
              <type> and its root-stripped-ness (a .chor supplies placement
              for stripped clips, a celebration carries its own travel).
              Stale *.anim.orig* experiment leftovers are deleted.
  pool        data/imports/pes21/animations/body_anime_fileN/*.anim from the
              dt13 mtar ganis (hash-named anim_<StrCode32(stem)>.gani).
  installed   data/media/animations/**/pes_*.anim re-installed through
              install_anims from the freshly rebuilt pool (match classes) or
              from a fresh dt12 conversion (entrance lineup). The class is
              the directory the file sits in; a clip whose velocity bucket
              moved lands in its new directory and the stale copy is removed.

  python3 reconvert_installed.py --fixdemo <dir> [--ganis /tmp/ganis]
      [--data ../../data] [--only cutscenes,pool,installed] [--jobs N]
"""

import argparse
import multiprocessing
import os
import re
import shutil
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import gani_to_anim
import install_anims
import retarget
import strcode

TYPE_RE = re.compile(r"<type>\s*(\w+)\s*</type>", re.S)

# installed pes_* top-level directory -> install_anims class
DIR_TO_CLASS = {
    "sliding": "sliding", "interfere": "interfere", "trap": "trap",
    "movement": "movement", "ballcontrol": "trick", "deflect": "keeper",
}
CELEBRATION_DIRS = ("happy_normal", "happy_extreme", "sad_normal")


def index_fixdemo(fixdemo_dirs):
    """{stem: gani path}; sub-actor and eyes-target variants keep their names."""
    out = {}
    for root_dir in fixdemo_dirs:
        for root, _, files in os.walk(root_dir):
            for name in files:
                if name.endswith(".gani"):
                    out.setdefault(name[:-5], os.path.join(root, name))
    return out


def pool_gani(ganis_dir, subdir, stem):
    path = os.path.join(ganis_dir, subdir,
                        "anim_%08x.gani" % strcode.strcode32(stem))
    return path if os.path.exists(path) else None


def reconvert_one(job):
    """(gani path, dest path, anim type, strip, scale) -> error string or None."""
    gani_path, dest, anim_type, strip = job[:4]
    scale = job[4] if len(job) > 4 else None
    try:
        text, _ = gani_to_anim.convert(open(gani_path, "rb").read(),
                                       anim_type=anim_type, strip_root=strip,
                                       pos_scale=scale)
    except Exception as exc:
        return "%s: %s" % (os.path.basename(dest), exc)
    open(dest, "w").write(text)
    return None


def run_jobs(jobs, workers):
    failures = []
    if workers > 1 and len(jobs) > 1:
        with multiprocessing.Pool(workers) as pool:
            for error in pool.imap_unordered(reconvert_one, jobs, chunksize=8):
                if error:
                    failures.append(error)
    else:
        for job in jobs:
            error = reconvert_one(job)
            if error:
                failures.append(error)
    return failures


def pass_cutscenes(data_dir, fixdemo, workers):
    base = os.path.join(data_dir, "media", "cutscenes")
    jobs, missing, cruft = [], [], 0
    for root, _, files in os.walk(base):
        for name in sorted(files):
            path = os.path.join(root, name)
            if ".anim.orig" in name:
                os.remove(path)
                cruft += 1
                continue
            if not name.endswith(".anim"):
                continue
            stem = name[:-5]
            source = fixdemo.get(stem)
            if source is None:
                missing.append(stem)
                continue
            text = open(path).read()
            match = TYPE_RE.search(text)
            anim_type = match.group(1) if match else "cutscene"
            strip = install_anims.root_is_stripped(text.split("\n"))
            jobs.append((source, path, anim_type, strip))
    failures = run_jobs(jobs, workers)
    print("cutscenes: %d reconverted, %d missing sources, %d failures, "
          "%d .orig leftovers removed"
          % (len(jobs) - len(failures), len(missing), len(failures), cruft))
    for name in missing[:10]:
        print("  no source: %s" % name)
    for error in failures[:10]:
        print("  FAIL %s" % error)
    return not failures


def pass_pool(imports_dir, ganis_dir, workers):
    jobs, missing = [], []
    for subdir in sorted(os.listdir(imports_dir)):
        full = os.path.join(imports_dir, subdir)
        if not os.path.isdir(full):
            continue
        for name in sorted(os.listdir(full)):
            if not name.endswith(".anim"):
                continue
            source = pool_gani(ganis_dir, subdir, name[:-5])
            if source is None:
                missing.append(name)
                continue
            # Match animation, so the calibrated scale rather than the
            # entrance/cutscene one. At 1/128000 every root track comes out
            # 6.25x too short: a traprun travels 0.13 m instead of 1.27 m, so
            # the clip reads as stationary and lands in `idle/`, and a diving
            # keeper keeps his pelvis at standing height and swims through the
            # air. See retarget.PES_POS_TO_M_GAMEPLAY and calibrate_pos_scale.
            jobs.append((source, os.path.join(full, name), "movement", False,
                         retarget.PES_POS_TO_M_GAMEPLAY))
    failures = run_jobs(jobs, workers)
    print("pool: %d reconverted, %d missing sources, %d failures"
          % (len(jobs) - len(failures), len(missing), len(failures)))
    for error in failures[:10]:
        print("  FAIL %s" % error)
    return not failures


def pass_installed(data_dir, imports_dir, ganis_dir, fixdemo, workers):
    anim_root = os.path.join(data_dir, "media", "animations")
    old = []
    for root, _, files in os.walk(anim_root):
        for name in sorted(files):
            if name.startswith("pes_") and name.endswith(".anim"):
                old.append(os.path.join(root, name))

    # stem -> pool path, over every pool subdir
    pool_index = {}
    for subdir in sorted(os.listdir(imports_dir)):
        full = os.path.join(imports_dir, subdir)
        if not os.path.isdir(full):
            continue
        for name in os.listdir(full):
            if name.endswith(".anim"):
                pool_index.setdefault(name[:-5], os.path.join(full, name))

    installed, skipped, failures = set(), [], []
    scratch = os.path.join(data_dir, "media", "animations", ".reconvert_tmp")
    os.makedirs(scratch, exist_ok=True)
    for path in old:
        rel = os.path.relpath(path, anim_root)
        top = rel.split(os.sep)[0]
        stem = os.path.basename(path)[len("pes_"):-len(".anim")]
        if top == "celebration":
            klass = rel.split(os.sep)[1]
            if stem.endswith("_loop"):
                klass += "_loop"
        elif top == "entrance":
            klass = "entrance_lineup"
        else:
            klass = DIR_TO_CLASS.get(top)
        if klass is None:
            skipped.append(rel)
            continue

        source = pool_index.get(stem)
        if source is None and stem in fixdemo:
            source = os.path.join(scratch, stem + ".anim")
            error = reconvert_one((fixdemo[stem], source, "cutscene", False))
            if error:
                failures.append(error)
                continue
        if source is None:
            skipped.append(rel + " (no source)")
            continue

        dest, reason = install_anims.install(source, klass,
                                             game_data_dir=data_dir)
        if dest is None:
            failures.append("%s: %s" % (rel, reason))
            continue
        installed.add(os.path.abspath(dest))

    # Anything not re-written goes: a clip whose (corrected) curves moved it
    # to another velocity bucket, or that no longer qualifies for its class,
    # must not linger with the old rig's rotations in it.
    removed = 0
    for path in old:
        if os.path.abspath(path) not in installed and os.path.exists(path):
            os.remove(path)
            removed += 1
    shutil.rmtree(scratch, ignore_errors=True)
    print("installed: %d re-installed, %d old files removed "
          "(moved bucket or no longer qualify), %d skipped, %d failures"
          % (len(installed), removed, len(skipped), len(failures)))
    for line in skipped[:10]:
        print("  skipped: %s" % line)
    for error in failures[:10]:
        print("  FAIL %s" % error)
    return True


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--fixdemo", action="append", default=[])
    parser.add_argument("--ganis", default="/tmp/ganis")
    parser.add_argument("--data", default=os.path.join(here, "..", "..", "data"))
    parser.add_argument("--only", default="cutscenes,pool,installed")
    parser.add_argument("--jobs", type=int, default=os.cpu_count() or 4)
    args = parser.parse_args()

    fixdemo = index_fixdemo(args.fixdemo)
    imports_dir = os.path.join(args.data, "imports", "pes21", "animations")
    only = set(args.only.split(","))
    ok = True
    if "cutscenes" in only:
        ok &= pass_cutscenes(args.data, fixdemo, args.jobs)
    if "pool" in only:
        ok &= pass_pool(imports_dir, args.ganis, args.jobs)
    if "installed" in only:
        ok &= pass_installed(args.data, imports_dir, args.ganis, fixdemo,
                             args.jobs)
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
