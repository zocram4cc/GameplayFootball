"""Renders a lineup portrait for every player whose pack did not ship one.

A pack's own portrait art always wins: import_team reads it out of the pack and
writes it into playerportraits.cfg (`bind_portraits`), and this leaves those
entries alone. This is for a squad whose pack ships none - or whose pack is no
longer on disk, which is the case for /vn/: its 23 models are installed and its
players have no portrait art anywhere. The game plan then draws a card with no
face on it, and the owner asked for portraits in the line-up.

The portrait is the player's own model, head and shoulders, shot through the
engine's own loader (gfviewer --portrait), so what the card shows is what the
pitch shows.

  python3 render_portraits.py [--data ../../data] [--viewer ../../build/gfviewer]
      [--only PREFIX] [--force] [--size 256]
"""

import argparse
import os
import subprocess
import sys

WIDTH, HEIGHT = 1280, 720


def read_map(path):
    """-> {databaseID: value} from a `<id> <value>` config, comments skipped."""
    out = {}
    if not os.path.exists(path):
        return out
    for line in open(path, "r", errors="replace"):
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        parts = line.split(None, 1)
        if len(parts) != 2:
            continue
        try:
            out[int(parts[0])] = parts[1].strip()
        except ValueError:
            continue
    return out


def model_ase(data_dir, model_rel):
    """-> the .ase inside an installed model directory, or ''."""
    directory = os.path.join(data_dir, model_rel)
    name = os.path.basename(os.path.normpath(model_rel))
    ase = os.path.join(directory, "fullbody_%s.ase" % name)
    return ase if os.path.exists(ase) else ""


def render(viewer, data_dir, ase_rel, out_raw, timeout=180):
    """Runs the viewer headless; -> True when it wrote frames."""
    env = dict(os.environ)
    env["SDL_VIDEODRIVER"] = "offscreen"
    env.pop("WAYLAND_DISPLAY", None)
    env.pop("DISPLAY", None)
    # The scratch file is shared by every model in the run, and this function
    # answered "did it write frames?" by asking whether a big enough file
    # exists. One viewer failure after any success and the NEXT player's
    # portrait was the previous player's last frame, saved and bound as his.
    if os.path.exists(out_raw):
        os.remove(out_raw)
    try:
        done = subprocess.run([viewer, ase_rel, "--portrait", "--shots", "1", "--out", out_raw],
                              cwd=data_dir, env=env, timeout=timeout,
                              stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=False)
    except subprocess.TimeoutExpired:
        return False
    if done.returncode != 0:
        return False
    return os.path.exists(out_raw) and os.path.getsize(out_raw) >= WIDTH * HEIGHT * 4


def last_frame(path, size):
    """-> the last recorded frame as a square PIL image of `size`.

    The recorder holds a few frames in flight, so the file opens with lead-in
    frames of an empty scene; the last one is the drawn shot (gfviewer says the
    same in its own output).
    """
    from PIL import Image
    frame_bytes = WIDTH * HEIGHT * 4
    raw = open(path, "rb").read()
    frames = len(raw) // frame_bytes
    if frames < 1:
        return None
    image = Image.frombytes("RGBA", (WIDTH, HEIGHT), raw[(frames - 1) * frame_bytes:
                                                         frames * frame_bytes])
    # Square, from the middle: a card is square and the shot is 16:9.
    left = (WIDTH - HEIGHT) // 2
    return image.convert("RGB").crop((left, 0, left + HEIGHT, HEIGHT)).resize((size, size))


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--data", default=os.path.join(here, "..", "..", "data"))
    parser.add_argument("--viewer", default=os.path.join(here, "..", "..", "build", "gfviewer"))
    parser.add_argument("--only", default="", help="a model prefix, e.g. vn")
    parser.add_argument("--size", type=int, default=256)
    parser.add_argument("--force", action="store_true")
    args = parser.parse_args()

    data_dir = os.path.abspath(args.data)
    viewer = os.path.abspath(args.viewer)
    if not os.path.exists(viewer):
        print("no viewer at %s - build the gfviewer target" % viewer)
        return 1

    models_cfg = os.path.join(data_dir, "media", "players", "playermodels.cfg")
    portraits_cfg = os.path.join(data_dir, "media", "players", "playerportraits.cfg")
    models = read_map(models_cfg)
    portraits = read_map(portraits_cfg)
    if not models:
        print("no bound models in %s" % models_cfg)
        return 1

    scratch = os.path.join(data_dir, "..", "portrait_scratch.raw")
    written = 0
    skipped = 0
    for database_id in sorted(models):
        model_rel = models[database_id]
        prefix = os.path.basename(os.path.normpath(model_rel)).split("_")[0]
        if args.only and prefix != args.only:
            continue
        if database_id in portraits and not args.force:
            skipped += 1
            continue
        ase = model_ase(data_dir, model_rel)
        if not ase:
            print("  %-6d no model at %s" % (database_id, model_rel))
            continue
        ase_rel = os.path.relpath(ase, data_dir)
        if not render(viewer, data_dir, ase_rel, os.path.abspath(scratch)):
            print("  %-6d could not render %s" % (database_id, ase_rel))
            continue
        image = last_frame(os.path.abspath(scratch), args.size)
        if image is None:
            print("  %-6d empty render" % database_id)
            continue
        out_rel = os.path.join("imports", prefix, "portraits", "player_%d.png" % database_id)
        out_abs = os.path.join(data_dir, out_rel)
        os.makedirs(os.path.dirname(out_abs), exist_ok=True)
        image.save(out_abs)
        portraits[database_id] = out_rel
        written += 1
        print("  %-6d %s" % (database_id, out_rel))
    if os.path.exists(scratch):
        os.remove(scratch)

    if written:
        header = "# imported portraits: \"<databaseID> <png path>\"\n"
        lines = [header]
        for database_id in sorted(portraits):
            lines.append("%d %s\n" % (database_id, portraits[database_id]))
        open(portraits_cfg, "w").writelines(lines)
    print("%d portrait(s) rendered, %d already had one" % (written, skipped))
    return 0


if __name__ == "__main__":
    sys.exit(main())
