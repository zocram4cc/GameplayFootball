# PES 2021 bulk import

The goal: bring PES 2021 content — player models, animations (match play,
celebrations, entrances), portraits, billboards, chants, and eventually
stadiums — into GameplayFootball's own formats, so packs of imported content
work on this engine. PES 2021 is abandonware, no longer for sale; the content
sources here are a local install and 4cc aesthetic exports.

Everything lives in `tools/pes21_import/`; converted content is written to
`data/imports/<pack>/` (git-ignored — packs are generated, not source).

## The two sides

### GameplayFootball (target)

| Thing | Format | Where |
|---|---|---|
| Player model | 3ds Max ASE, one `GEOMOBJECT` per body part (`pelvis`, `trunk`, `head`, `upperarm_left`…) referenced by an `.object` XML | `data/media/objects/players/` |
| Skeleton | 15 implicit nodes: `player` (root motion) + body, middle, neck, L/R shoulder/elbow/thigh/knee/ankle | encoded in the animations |
| Animation | plain text: one CSV line per node — `name,frame,qx,qy,qz,qw,frame,…`; the `player` line carries `frame,x,y,z` root positions; optional `extension,football,…` ball keys; XML-ish metadata tail (`<type>`, touch variables) | `data/media/animations/**/*.anim`, loader `src/utils/animation.cpp` (`Animation::Load`) |
| Billboards | PNG textures | `data/media/textures/adboards/` |
| Audio | 44.1 kHz WAV | `data/media/sounds/` |
| Portraits | none in-engine yet; packs store PNG, menu wiring is future work | `data/imports/<pack>/portraits/` |

### PES 2021 (source)

| Thing | Format | Notes |
|---|---|---|
| Containers | CriPak `.cpk` (`@UTF` tables + CRILAYLA) | `cpk.py` extracts, pure Python — verified on `dt13_all.cpk` (416 files) |
| Body animations | `.mtar` motion archives, each entry a `.gani` blob keyed by a 32-bit name hash | `mtar.py` — verified: 10 archives, **4,389 animations** |
| Animation | Fox Engine `.gani`: MOTION chunk, track table (body anims: consistently 27 tracks = the animated body bones), compressed curves | `gani.py` surveys structure; **curve decoding is the open problem** (see below) |
| Rig | `.frig` (`body_skel.frig`, `face_skel.frig`, hand rigs) — group metadata; authoritative bone list+bind pose comes from any `.fmdl`'s bone table | |
| Player model | Fox Engine `.fmdl` + `.dds` texture set | parsed by the 4cc `pes-fmdl` Blender addon's `FmdlFile.py`, which runs fine outside Blender |
| Face expressions | `face_skel.frig` + `FHSequence` tables (`Facebase.bin`, `Faceadd.bin`) + face `.gani`s (59 tracks) | required for full player import — flagged, not started |
| Audio | `.adx`/`.hca` in sound cpks | ffmpeg decodes ADX directly |

## What works today

```sh
# 1. Unpack a cpk (no wine, no .NET)
python3 tools/pes21_import/cpk.py dt13_all.cpk out/

# 2. List / extract a motion archive
python3 tools/pes21_import/mtar.py out/.../body_anime_file0.mtar ganis/

# 3. Survey a .gani (size, motion hash, track count/offsets)
python3 tools/pes21_import/gani.py ganis/anim_xxxxxxxx.gani

# 4. Convert a full-body .fmdl into a GameplayFootball ASE
python3 tools/pes21_import/fmdl_to_ase.py model.fmdl out.ase \
    --fmdl-lib "<4cc Blender pack>/scripts/addons/pes-fmdl"
# verified: an HDG Helldiver (12.5k verts) → 11,778 faces across 10 body parts

# 5. Package portraits / adboards / chants into a pack
python3 tools/pes21_import/package_assets.py --export "HDG VGL26 AET" --pack data/imports/hdg
# verified: 23 portraits → data/imports/hdg/portraits/*.png
```

The skeleton bridge is `retarget.py`: a PES-bone → GF-node map that drives mesh
segmentation now and animation retargeting later. It is data — refine it there,
not in code.

## Open problems, in priority order

1. **`.gani` curve decoding.** Structure is mapped (header, MOTION chunk, track
   table); the per-track curve encoding (quantized quaternion keys) is not.
   The MGSV modding community (FoxKit) has partial GAni research to draw on.
   Until then no PES animation can be converted. Everything downstream of the
   curves — the `.anim` writer, root-motion extraction from `sk_hip`, metadata
   classification (which anims are celebrations vs entrances vs play, from
   `AnimeTable/bin/*.bin`) — is straightforward once keys are readable.
2. **Name hash dictionary.** mtar entries and gani tracks are keyed by a Fox
   32-bit name hash. `strcode.py` has a placeholder polynomial + dictionary
   matcher; the real algorithm should be lifted from community tooling and
   validated against bone names harvested from `.fmdl` files (they are plain
   text there). With it, animations get real names instead of `anim_<hash>`.
3. **Engine-side model hookup.** `PlayerData` needs a `model_url` (planned in
   TECHNICAL_ROADMAP "Arbitrary Model Loading") so a converted `.ase`/`.object`
   replaces the shared fullbody per player. `Team::FetchKit` /
   `ObjectLoader::LoadObject` are the seams.
4. **Face skeleton + expressions** (user requirement): bring `face_skel` and
   the FHSequence expression system across once body animation conversion
   works.
5. **Stadiums**: PES stadium models are large multi-`.fmdl` scenes in other
   cpks; the fmdl→ase path applies, but scene assembly, lighting and collision
   are their own project. Billboards/chants already have a home
   (`textures/adboards`, `sounds/`).

## Pack layout

```
data/imports/<pack>/
  models/<player>.ase        converted full-body models
  portraits/<player>.png
  adboards/<name>.png        copy or reference from stadium config
  chants/<name>.wav
```
