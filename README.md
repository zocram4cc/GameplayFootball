# GodotFootball

An AI-vs-AI football match engine built on Godot 4.7 and a C++ GDExtension.

- The match simulation and tactics come from GameplayFootball, built here as a
  deterministic headless library (`src/sim`).
- Models, textures, skeletons and animations are read directly from your own
  Pro Evolution Soccer 2021 installation (`src/pes`). There is no conversion
  step, and no PES file is shipped with this repository.

## Build

Requirements: CMake ≥ 3.24, Ninja, a C++20 compiler, zlib, SQLite 3 and Boost
headers. Godot 4.7.2 is downloaded and checksum-verified on first use by
`tools/fetch_godot.sh`.

```bash
git submodule update --init
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
```

## Point it at your PES 2021 install

Every tool takes the install folder at runtime, either from the `PES21_ROOT`
environment variable or from `--pes21-root DIR`. Nothing records the path.

```bash
export PES21_ROOT=/path/to/PES2021
build/apps/pesfmt_dump mounts --pes21-root "$PES21_ROOT"
tools/capture_viewer.sh --face <face id>
build/apps/gfsim_run --db-sql assets/fixtures/teams.sql --home 1 --away 2 --pes21-root "$PES21_ROOT"
```

The file system mounts PES's own archives in the order the game uses: `Data/`,
then `download/` in `DpFileList.bin` order, then sider live-cpk roots from
`sider.ini`. Later sources override earlier ones.

Players wear the appearance your own game shows them in (sleeves, socks,
shirt tail, skin, boots, gloves) when you also give it your EDIT save,
`EDIT00000000` in PES's `save` folder. It is read, never written:

```bash
tools/capture_viewer.sh --face <player id> -- --team <team id> --kit 0 --edit-save /path/to/EDIT00000000
```

Without the save, players fall back to `PlayerAppearance.bin`, as in the game.
Tests read it from `PES21_EDIT_SAVE` and skip those checks when it is unset.

## How PES does it: `docs/re/`

The reverse-engineering reports behind every format reader and assembly rule
here. They cover kit and body assembly, the EDIT save, shaders and materials,
textures, animation selection and the hand rig, physique, markings, collision,
stadium and camera, lighting, cutscenes, match logic, faces, and officials and
the ball. Each claim is tagged OBS (observed, and how) or INF (inferred), with
killed hypotheses recorded. Start at `docs/re/README.md`.

## Licence

Apache-2.0. See `NOTICE` for attributions.
