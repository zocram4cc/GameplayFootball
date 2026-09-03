# From a clone to a game

You have this repository, a PES 2021 `Data/` directory, and a few 4cc / VGL
aesthetics exports. This is the order to do things in, what each step buys you,
and what it looks like when a step has not been done.

Two other files carry the detail this one deliberately does not repeat:
[ASSETS.md](ASSETS.md) is the running order for the PES import with the reasons
behind each conversion, and [PES21_IMPORT.md](PES21_IMPORT.md) is the format
reference. [HARNESSES.md](HARNESSES.md) is how to record a match without a
screen.

---

## 0. Build and run, with no PES data at all

```bash
./build.sh          # installs your distro's dependencies, configures, builds
./run.sh            # launches it
```

`./build.sh --help` lists `--release`, `--debug`, `--clean`, `--no-deps`,
`--jobs N`. On NixOS use `nix develop` then `./build.sh --no-deps`. macOS is in
the [README](../README.md); Windows is [its own section](#doing-all-of-this-on-windows)
below, because several of the steps differ there.

**A fresh clone plays.** Verified by running a match on a tree built from
tracked files only (`git archive HEAD data`): the engine reports

```
[Warning] in [Match::Match]: player_body 'fullbody_pes' is incomplete
          (see docs/ASSETS.md), using 'fullbody'
[Notice]  in [HumanoidBase]: hand rig: no pose library, fingers stay in bind pose
```

and goes on to play both halves. What you get is the engine's own low-poly
body, its own stadium, its own scoreboard and no presentation. Everything
below replaces one of those with PES's.

**Any team plays; six of them look right.** The shipped database has fourteen
teams, and eight are 4cc/VGL rows (`/lcg/`, `/ink/`, `/hdg/`, `/vn/`,
`/smbg/`, `/dbg/`, `2HUG`, `/a/`) whose models, kits and badges are *not* in
the repository - nothing 4cc-derived is. Pick one of those before importing it
and the match still runs, saying what it substituted:

```
[Warning] in [Team::ActivateWithModel]: player 633 is assigned
          media/players/custom/lcg_2701, which is not imported; using the shared body
setting new kit: almost_white.png
```

twenty-two players on the shared body in flat white. Ids 1, 3, 4, 5, 7 and 8
are the six whose art *is* tracked and which therefore look as intended out of
the box. `sqlite3 data/databases/default/database.sqlite 'select id, name from teams'`
lists them all.

---

## 1. What you need before importing anything

* Your PES 2021 `Data/` directory. Prefer `dt00_x64.cpk.bak` over the live
  `dt00` if the 4cc mod has edited it.
* The `pes-fmdl` Blender addon, for anything reading `.fmdl`. Every converter
  takes `--fmdl-lib <the directory holding FmdlFile.py>`; `import_team.py` is
  the one that will find it for you, from `PES_FMDL_LIB` or by searching the
  tree for the 4cc Blender Starter Pack, so set that variable once.
* Python 3 with Pillow, and `ffmpeg` for the chants.
* Your 4cc/VGL exports: an aesthetics pack per team (`Boots/`, `Gloves/`,
  `Faces/`, `Kit Textures/`, `Logo/`) and, if the league published one, that
  team's `.ted`.

Nothing here writes into `src/`, and every output is covered by `.gitignore`:
after a full import `git status` is still clean. That is the point of the
no-assets rule - see [ASSETS.md](ASSETS.md).

---

## 2. The bulk pack: animations, portraits, adboards, chants

```bash
python3 tools/pes21_import/build_pes21_pack.py "/path/to/PES21/Data" \
    --pack data/imports/pes21
```

This is the one to run first, because the animation pool it writes
(`data/imports/pes21/animations/`) is what everything else animates with. It
also brings in player portraits, advertising boards and `CHANT.awb` as ogg.

Then install the match families out of that pool:

```bash
for cls in movement trap trick sliding interfere keeper; do
  python3 tools/pes21_import/install_anims.py data/imports/pes21/animations \
      --class $cls --batch
done
```

Each clip is measured on the way in - its own root track decides which incoming
velocity directory it lands in, and a clip with no ball contact is refused for
the families that need one. `--report` prints why each one went where.

If you later change anything in the conversion, `reconvert_installed.py` is the
reproducible way back to every installed clip rather than a re-download:

```bash
python3 tools/pes21_import/reconvert_installed.py --ganis <extracted dt13 ganis> \
    --fixdemo <extracted dt12 fixdemo> --only pool,installed,cutscenes
```

## 3. The player body, the hands, and the celebrations

The engine's fallback body is a low-poly relic. PES's own body, its 38 rigged
finger joints and the pose library that drives them are three commands in
[ASSETS.md §2, §2b](ASSETS.md) - `pes_base_body.py`, then `hand_poses.py`. With
the hand library in place the log changes to `hand rig: 38 finger joint(s)
bound, 7 pose(s), resting on normal`.

Celebrations, the walkout choreography and the broadcast camerawork come from
the cutscene export ([ASSETS.md §3](ASSETS.md)):

```bash
python3 tools/pes21_import/cpk.py "/path/to/PES21/Data/dt12_g4.cpk" /tmp/fixdemo \
    --filter=common/demo/fixdemo
python3 tools/pes21_import/export_cutscenes.py \
    /tmp/fixdemo/common/demo/fixdemo data/media/cutscenes
python3 tools/pes21_import/goal_cutscenes.py data/media/cutscenes/goal
```

`export_cutscenes.py` takes the *fixdemo directory*, not the whole `Data/`:
`cpk.py` unpacks it first. The second command pairs each celebration with the
camerawork PES shot it with (`celebrations.txt`), which is what stops a
long-lens shot being used on a knee-slide. Entrances have their own exporter,
`export_entrances.py`, because the engine picks a walkout by competition.

Without it a goal is a goal with the match camera on it; with it the goal is a
cast performance and the pre-match is a walkout. `entrance_id none` skips
straight to kickoff whatever you have imported.

## 4. A stadium, and the things every stadium borrows

[ASSETS.md §4](ASSETS.md) is the whole story - the scene, the staff on the
touchline, the crowd in the stands, the props round the pitch, the ground's own
lighting, and the shared assets (hoardings, net patterns, colour grading) that
PES keeps in one place. The short version for one ground:

```bash
python3 tools/pes21_import/stadium_to_gf.py \
    "<pack>/#Win/st017_fpk_extracted/center1.fmdl" \
    data/media/objects/stadiums/pes_st017 \
    --fmdl-lib "$PES_FMDL_LIB" \
    --textures "<pack>/sourceimages/tga/#windx11" \
    --name pes_st017 --max-extent 1300
python3 tools/pes21_import/import_common_stadium_assets.py \
    /path/to/4cc_15_billboard.cpk --out data --net x_netPat04
```

Then set `stadium_object` in `data/football.config` to the `.object` it wrote.
Delete any `.geomcache` beside a re-converted `.ase` or the engine reads the old
geometry back.

## 5. Import a team

One command per team, and it is the whole procedure:

```bash
python3 tools/pes21_import/import_team.py path/to/AET/ path/to/team.ted
```

It reads the `.ted` for the name, abbreviation, squad, stats, name colours,
formation and tactical sliders; writes the team, its players and its tactics
into `data/databases/default/database.sqlite`; converts every character in the
pack - body from `Boots/`, hands and fingers from `Gloves/`, head and hair from
`Faces/` - into the engine's format under
`data/media/players/custom/<prefix>_<id>/`; installs the kits, badge and
portraits; and binds each model to the database row its shirt number landed on
in `playermodels.cfg`.

Useful flags: `--force` re-imports models that already exist, `--prefix` names
the output when you have no `.ted` (models only, database untouched),
`--base data/media/objects/players/models/fullbody_pes.ase` composites a
face-slot or partial pack over a whole body, and `--dry-run` rolls the database
back so you can see what it would do.

**Look at what came out.** A pack is not always a body, and a body is not
always rigged where it looks rigged:

```bash
python3 tools/pes21_import/body_coverage.py data/media/players/custom/*/fullbody_*.ase
python3 tools/pes21_import/joint_binding.py --all
```

The first says whether the geometry clothes the rig or is a prop PES draws over
its own body; the second says whether each joint actually owns vertices, which
is the difference between a limb that bends and one that swings rigidly. Then
render one and look at it - `gfviewer` loads a single model through the engine's
own loader:

```bash
cd data && ../build/gfviewer media/players/custom/<model>/fullbody_<model>.ase \
    --shots 1 --out /tmp/m.raw
ffmpeg -f rawvideo -pixel_format rgba -video_size 1280x720 -i /tmp/m.raw /tmp/m_%02d.png
```

The first four frames are pipeline lead-in; the last `--shots` frames are the
shots. A statistic never caught a model torn in two - a picture did.

## 6. Play it, or record it

`./run.sh` and pick Quick Match, or drive a whole match headless and get an mp4:

```bash
./tools/showcase.sh --team1 16 --team2 13 --minutes 10 --out /tmp/match.mp4
```

The team numbers are database ids. The harness owns the recording path, refuses
to start unless it is writing into a fifo, keeps the engine's log beside the
video, and re-encodes to fit `--limit-mb`. Why each of those exists is in
[HARNESSES.md](HARNESSES.md), and every one of them is a mistake somebody has
already made.

---

## Config keys worth knowing

All in `data/football.config`, all optional.

| key | what it does |
|---|---|
| `player_body` | `fullbody_pes` (default) or `fullbody`; falls back on its own if the files are absent |
| `stadium_object`, `skydome_object`, `crowd_object`, `staff_object`, `adboards_object`, `entrance_props_object` | the imported scene and its furniture |
| `entrance_id`, `entrance_dir`, `entrance_choreography` | which walkout to stage; `none` goes straight to kickoff |
| `goal_cutscene_dir`, `result_cutscene_id`, `replay_wipe_dir` | the presentation's own assets |
| `scoreboard_theme` | `pes` for the imported theme, otherwise the engine's own |
| `anim_prefer_imported` | families where a mocapped clip gets first refusal; defaults to `sliding,interfere,deflect` |
| `graphics_lut_strength` | how much of PES's colour grading to apply; `0` turns it off |
| `rigdio_enabled`, `rigdio_dir` | the crowd's own music, per team (see [RIGDIO.md](RIGDIO.md)) |
| `match_duration_minutes`, `match_time_of_day`, `match_weather`, `match_difficulty` | the match itself |

## When something looks wrong

| what you see | what it means |
|---|---|
| `player_body ... is incomplete, using 'fullbody'` | step 3 not done; the low-poly body is standing in |
| `hand rig: no pose library` | `hand_poses.py` not run; hands keep PES's splayed modelling pose |
| `... is assigned media/players/custom/..., which is not imported` | that team's models are not built yet; those players are on the shared body |
| everyone in flat white (`setting new kit: almost_white.png`) | the team's kit PNGs are not on disk; `Team::FetchKit` substitutes |
| `Loaded stoppage cutscene pools for 0 categories` | no cutscene export; the match camera covers stoppages itself |
| a stadium standing on nothing | `--max-extent` cut the ground away; raise it and re-convert |
| a limb that never bends | `joint_binding.py --all`; the pack's bone mapping put those vertices elsewhere |
| a flat, milky picture | no colour grading table; run `import_common_stadium_assets.py` |

An upgrade to a converter does not mean re-downloading anything: the pool, the
installed clips and the cutscenes are all derived data, and
`reconvert_installed.py` rebuilds them from the ganis you already extracted.
Two one-way migrations exist for installs that predate a fix, both stamped so
they cannot run twice: `migrate_anims_tpose.py` for the stock clips' rig and
`migrate_cutscene_mover.py` for the cutscene clips' vertical.

---

## Doing all of this on Windows

Everything above applies; five things differ, and one does not work at all.
**None of this section has been run on a Windows machine** - it is read off the
scripts and the code, and the CMake and Python behaviour is stated where it is
explicit in them. Corrections welcome from anyone who tries it.

### Build and run

Prerequisites, once: Visual Studio 2022 with *Desktop development with C++*,
Git, and CMake on `PATH`.

```powershell
Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass   # first run only

.\build.ps1          # bootstraps vcpkg, installs vcpkg.json's deps, builds Release
.\run.ps1            # launches it
```

`build.ps1` takes `-DebugBuild`, `-Clean`, `-NoDeps`, `-Jobs N`, `-VcpkgRoot`,
`-Triplet`; `.\build.ps1 -Help` lists them. PowerShell reserves `-Debug`, which
is why the debug flag is `-DebugBuild`. `scripts\setup_windows_deps.ps1` is the
dependency step on its own, and `scripts\package_windows.ps1` assembles a
distributable folder (exe + DLLs + assets).

### The media tree is copied, not linked

`cmake/link_media.cmake` symlinks `data/media` next to the binary on Linux and
**copies it on Windows** (`FALLBACK_COPY` is `WIN32`), because a symlink there
needs Developer Mode or elevation and a build that fails on a permission is
worse than a build that is fat. The copy is per build configuration, so after a
full PES import every configuration holds its own copy of everything the import
produced - which on this machine's Linux tree is 6.2 GB apiece. Import into one
configuration, or expect the disk use.

### The importers run; two helpers do not

`tools/pes21_import` is plain Python 3 - `os`, `struct`, `argparse`, `glob`,
`tempfile`, `subprocess` - with no POSIX-only module, so it runs under Windows
Python. What it needs from you:

* **Pillow** (`pip install pillow`) and **ffmpeg on `PATH`**: the chants
  (`build_pes21_pack.py`), the replay wipe (`import_wipe.py`) and the audio
  pack (`package_assets.py`) all shell out to `ffmpeg`.
* `set PES_FMDL_LIB=<dir holding FmdlFile.py>`, or pass `--fmdl-lib` to each
  converter. Only `import_team.py` searches for it.
* **Pass `--ganis` explicitly** to `reconvert_installed.py`: its default is
  `/tmp/ganis`, which does not exist on Windows.
* Keep the extraction root short - `C:\pes\` rather than a deep folder under
  your profile. The archive paths inside a PES cpk are long
  (`Asset/model/bg/st002/#Win/...`), and Windows' 260-character limit still
  bites unless long paths are enabled.
* `extract_stadium_packs.sh` and `convert_stadiums.sh` are bash wrappers around
  `fpk.py` and `stadium_to_gf.py`. Run them under Git Bash or WSL, or call the
  Python they wrap directly - the wrappers add no conversion logic of their own.
* `sqlite3` on the command line may not be installed. Any team query in this
  guide works from Python instead:
  `python -c "import sqlite3;print(sqlite3.connect(r'data\databases\default\database.sqlite').execute('select id,name from teams').fetchall())"`

### Recording a match does not work there

`tools/showcase.sh` needs `mkfifo`, `timeout` and (as a fallback) `xvfb-run`.
None exists on Windows, and the fifo is the whole point of the harness - it is
what stops the engine writing raw frames into a file until the disk is full.

Two ways round it:

* **WSL2**, building and running the Linux side there. The harness works
  unchanged; you need a GPU-capable WSL for the hardware path, or
  `SHOWCASE_SOFTWARE=1` for the software one.
* **On Windows, by hand**: set `"frame_recording_path" "<a file>"` in
  `data/football.config`, play, then encode what it wrote:
  `ffmpeg -f rawvideo -pixel_format rgba -video_size 1280x720 -framerate 60 -i frames.raw out.mp4`.
  The stream is raw RGBA at a paced 60 fps with no timestamps of its own, and
  nothing consumes it as it is written, so the file grows fast - one accidental
  run on Linux left 32 GB. Keep the capture short and delete the raw file
  afterwards.
