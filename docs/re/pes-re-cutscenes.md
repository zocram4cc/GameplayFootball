# PES 2021 cutscenes ("fixdemo"): data formats, runtime selection, replays

Evidence tags: **OBS** means observed, and says how. **INF** means inferred, with a confidence level.
Addresses are virtual addresses in the dumped image `<scratch>/pesmem.bin`, which was loaded raw at 0x140000000. Function bounds come from the in-memory `.pdata`; see `local://pes-kit-assembly.md` §7.0 for how the dump was made.
All decompiles were produced with the headless Ghidra copy `<scratch>/CutsceneRE/ghidra` (no auto-analysis; functions are created on demand) using `<scratch>/CutsceneRE/bin/dec.py`. The decompiled C is in `<scratch>/CutsceneRE/dec/`, `<scratch>/CutsceneRE/dec/rep/` (replay), `<scratch>/CutsceneRE/dec/ev/` (event builders) and `<scratch>/CutsceneRE/cond/dec/` (condition setters).
Scripts: `tbl.py` decodes `table_*.bin`. `fdcdec.py` decodes `.fdc`. `fdcsurvey.py` and `recstats.py` produce the corpus statistics. `regparse.py` parses the reflection descriptors. `livescan.py` and `livedump.py` read the live process. All scripts are in `<scratch>/CutsceneRE/`. Nothing PES-derived goes into the repository; every table described here is read at runtime from the user's install.

Game state observed (§7): PES2021.exe pid <pid> under Wine. A match was **in play** throughout: the replay ring buffer advanced at about 57 frames/s while I watched. No cutscene was playing at that moment.

---

## 0. Summary for the reimplementation

1. **Selection tables.** All match and menu cutscenes come from nine category tables, `common/demo/fixdemo/<cat>/table_<cat>.bin`, plus 4,925 cut files, `common/demo/fixdemo/<cat>/cut_data/*.fdc`. There are no `.evf` or `.fdemo` files.
2. **Three-level hierarchy.** A table defines **Scenes**, a **Cut library** and, for goal only, extra lists.
   - A Scene is an ordered list of steps.
   - Each step references a **cut group** (`(cat<<16)|cutIndex1`).
   - A cut group has up to ~1,700 **variants**. Each variant is a set of up to 10 `.fdc` files that play *together* as layers: base actors, camera, crowd, effects, props.
3. **Variant choice.** A decision-tree matcher (`FUN_141f4b1d0`) picks the variant. It is driven by up to 10 condition keys per variant, `key = condType<<8 | value`, and a condition evaluator that reads the match context (`FUN_141f4d2f0`; the full table is in Appendix A). Type 0x43 is a weighted random roll and type 0x19 a random threshold. The chosen variant is cached per cut id for the session.
4. **Scene choice.** Each event has its own builder. Goal is the most complex (§4.3):
   - player-specific signature celebrations (a player-id → scene map stored in `table_goal.bin`);
   - the player's edit-mode celebration slots (an index list in `table_goal.bin`);
   - otherwise a uniform random pick among scenes whose flag bits match the goal "level", using a 31-bit LCG, with retries to avoid repeating the previous celebration.
5. **What an `.fdc` holds.** An `.fdc` is a small container. Its main block holds typed fixed-size records: Cut, Env, Timing, Effect, Human, Object, Camera, Function and HumanControl. The type mapping comes from the exe switch at `FUN_141f36200`. The other entries reference or embed the resources:
   - `.gani` motion: external 4-byte stub, or embedded;
   - `.seq` face/event sequence: embedded;
   - `.canm` camera stream: embedded;
   - `.ask` skeleton.
   Times are in **1/60 s ticks**. `.canm` streams run at 30 fps.
6. **Actor binding.** Actors are bound by **slot number**: Human/Object record +0x00 (u16). The data convention is:
   - 0–10: main/home side, with slot 0 the subject player in goal scenes;
   - 11–21: the other side;
   - 22–25: officials;
   - 26: used by goal scenes, probably the coach.
   A scene is placed relative to a runtime anchor that its step "kind" selects (§5).
7. **Replays are recorded state, not re-simulation.** There are 21 replay "units":
   - Each unit is a ring buffer of 0x1fb0-byte frames, 660 frames ≈ 11 s at 60 Hz.
   - A frame holds a 0x264-byte header (ball, match state) plus 25 human records of 300 bytes each.
   - A human record stores a quantised position (i16, ×256 = metres), a heading (×100), motion ids and motion times (×1000), look/IK targets and flags.
   - Playback re-samples motions from these ids and times.
   - No coin-toss cutscene exists in PES 2021's data.

---

## 1. Where cutscene data lives (Q1)

Survey method (OBS): `pesfmt_dump ls ''` over the full mounted VFS (game mount order, download cpks included) → `<scratch>/allpaths.txt` (106,100 paths).

| path | ext | count | role |
|---|---|---|---|
| `common/demo/fixdemo/<cat>/table_<cat>.bin` | .bin | 9 | scene / cut / ctrl selection tables (§2) |
| `common/demo/fixdemo/<cat>/cut_data/*.fdc` | .fdc | 4,925 | cut files (§3) |
| `common/demo/anime/FoxAnim/FixDemo/Animations/*.gani` | .gani | 5,483 (5,703 .gani under `demo/anime`) | demo motions (`dml_*`), including `*_eyes_target.gani` |
| `common/demo/anime/FoxAnim/FixDemo/CharacterAssets/` | .ask/.frig | 116 / 58 | demo skeletons (`body_anim_skel.ask`, `body_render_skel.ask`, `eyes_target_anim_skel.ask`, `mob_high_anim_skel.ask`, `prop_anim_skel.ask`, `pes_eyes_target_150120.frig`, …) |
| `common/demo/fixdemoobj/**` | .fpk/.ftex/.xml | ~5,700 | demo-only sets and props: tunnel/passage, locker room, press room, banners, cup stands, `demoobj_human` press/staff/steward models |
| `common/demo/prop/**`, `common/demo/mob/**` | | 790 / 382 | trophies and props; crowd/mob textures |

Cut files per category (OBS):

| category (id) | .fdc |
|---|---|
| ent (1) | 1,083 |
| foul (2) | 78 |
| goal (3) | 1,691 |
| pk (4) | 128 |
| change (5) | 192 |
| timeup (6) | 198 |
| end (7) | 526 |
| result (8) | 106 |
| mode (9) | 923 |

- **Category ids (OBS).** The pointer table at 0x1434a4c00 lists the cut_data directories in this order: `""`, ent, foul, goal, pk, change, timeup, end, result, mode, menu. That makes category ids 0..10. The table loader `FUN_141f33650` loads `table_*.bin` with ids 1..9. No `menu` table exists in the VFS.
- **Other candidates (OBS).** No `.evf`, `.fdemo` or `.demo` files exist anywhere in the VFS.
  - The dev-only strings `DevelopData/fixdemocut/win/…`, `.fox`, `.json` and `Tools\Match\FixDemoEdit\Excel2ImportFile.js` show the authoring pipeline: Excel → JSON/Fox entities → binary `.fdc` and `.bin`.
- **Coin toss (OBS).**
  - No path contains `toss`.
  - The only `coin` path is a MyClub menu json.
  - None of the 71 entrance cut groups or the 50 entrance scenes is a coin toss. The entrance families are tunnel/aisle walk, walkout, line-up/anthem (`ENT_ANTH*`), choreo, smoke and fireworks, plus a kick-off "kicker" pack (`ent_*_pl_*kicker*.fdc`).
  - **INF hi:** PES 2021 has no coin-toss cutscene.

Scene families per table (OBS, from the decoded tables):
- **ent:** `ENT_LOADING`, `ENT_ANTH0/1/2_*` (anthem/line-up), `ENT_CLUB_*`, `ENT_CLUBSOUTH_*`, `ENT_ACL_*`, `*_DERBY`, `*_PICKUP`, `*_DIGEST_*`.
- **timeup:** `TU_HALF_CMN` (half time) and `TU_FULL_<homeMood>_<awayMood>` (full time, mood ∈ GLAD/POSITIVE/NORMAL/NEGATIVE/SAD).
- **end:** end-of-match ceremonies (`END_CMN`, `END_PK_WIN/LOSE`, `END_CUP_*`, `END_LG_*`, `END_RETIRE_*`, …).
- **result:** `RESULT_CMN/EXT/FIN/FULL/MODE`.
- **change:** substitutions.
- **foul:** cards and injuries.
- **pk:** penalties.
- **goal:** 338 scenes (celebrations, runs, coach, hugs, own goal, offside goal, golazo, …).
- **mode:** news, press conference, awards.

---

## 2. `table_<cat>.bin`: selection tables (fully decoded)

### 2.1 Container: column-split typed streams (OBS)

- **Reader.** `FUN_1423dce10` sets up the reader. The sequential getters are:
  - `FUN_1423dca90`: f32 stream;
  - `FUN_1423dcbc0`: u32 stream;
  - `FUN_1423dcac0`: u16 stream;
  - `FUN_1423dcae0`: u8 stream;
  - `FUN_1423dcb00`: next string;
  - `FUN_1423dcdf0`: "end of section" test.
- **Decoder verification.** `<scratch>/CutsceneRE/tbl.py` re-implements the parse order of `FUN_141f40c00`. On **all nine tables** it consumes exactly 100% of every stream (A, B, W, U8, strings) and lands exactly on the section-end read count.

Header: 14 × u32.

| idx | meaning |
|---|---|
| 0 | count of f32 values (stream A) |
| 1 | count of strings |
| 2 | number of section-end entries (always 1 here) |
| 3 | count of u32 values (stream B) |
| 4 | count of u16 values |
| 5 | count of u8 values |
| 6 | total string bytes |
| 7 | offset of stream A (f32) |
| 8 | offset of the string end-offset table (u32 per string: exclusive end; the start is the previous end) |
| 9 | offset of the section-end table (u32 "total reads so far") |
| 10 | offset of stream B (u32) |
| 11 | offset of stream W (u16) |
| 12 | offset of stream U8 |
| 13 | offset of the string characters (no NULs; lengths come from the end table) |

- **End test.** Every getter increments a global read counter. `FUN_1423dcdf0` returns `sectionEnd[0] <= readCount`, which is how optional trailing blocks are detected.

### 2.2 Parse order (`FUN_141f40c00`, OBS)

```
u16 n; ver=0; if n==1 { ver=1; n=u16 }          # all 9 shipped tables are ver 1
# --- T1: CUT library (stored at DB + cat*0x20 + 0x168)
{u16 start, u16 count}[n]  ; string name[n]       # cut-group index + names
u16 m ; u8 nKeys ; u8 nStr                         # nKeys/nStr read ONCE for all rows
m x { u16 key[nKeys] (first 10 kept) ; string fdc[nStr] (first 10 kept) ; u32 b0 ; f32 f ; u32 b1 }   # 0x78-byte runtime row
# --- T2: SCENE table (stored at DB + 8 + cat*0x20)
u16 n ; {u16 start,u16 count}[n] ; string name[n]
u16 m ; m x { u8 kind ; f32 fade ; u32 cutRef ; u32 flags ; u8 c0 ; u8 c1 ; u16 w0 ; u16 w1 ;
              u8 k ; k x {u8,u8,u8} (first 10 kept) ; if ver==1: u16 w2 }     # 0x38-byte runtime row
# --- goal only (cat==3)
u16 n ; u32 goalEditScene[n]                       # DB+0x428/0x430
if !end: u16 n ; n x {u32 playerId, u32 sceneId} -> std::map (DB+0x438)
for each T2 scene: {u8 b[5]; f32 v0[4]; f32 v1[4]; f32 v2[4]; u8 flag[4]}   # 0x3c rows, DB+0x448
# --- T3: CTRL table (only if !end; only goal has one)   (DB + cat*0x20 + 0x2c8)
u16 n ; {u16,u16}[n] ; string name[n]
u16 m ; u8 nKeys ; m x { u16 key[nKeys] ; u8 nf ; u8 ni ; f32[nf] (first 4) ; u32[ni] (first kept) }   # 0x2c rows
```

Live check (OBS, §7): the `fixdemo::DB` singleton at heap 0x1bbde3a0 holds:
- at +0x28 for category 1 (ent): count 0x32 = 50, matching the decoded ent T2 scene count;
- at +0x48 for category 2 (foul): count 0x21 = 33, matching foul.

This confirms that T2 is the Scene table stored at `DB + 8 + cat*0x20`.

Decoded sizes (OBS):

| table | T1 groups / variant rows | T2 scenes / steps | extra |
|---|---|---|---|
| ent | 71 / 32,400 | 50 / 509 | |
| foul | 30 / 80 | 33 / 100 | |
| goal | 362 / 5,734 | 338 / 712 | 162 edit ids, 35 player→scene, T3: 8 groups / 482 rows |
| pk | ? / 390 | 27 / 67 | |
| change | ? / 784 | 15 / 40 | |
| timeup | ? / 1,692 | 22 / 66 | |
| end | 72 / 2,370 | 41 / 306 | |
| result | ? / 1,405 | 5 / 10 | |
| mode | ? / 2,215 | 132 / 457 | |

`?` = group count not tabulated; rows are exact.

### 2.3 Semantics

**Ids (OBS).**
- `sceneId = (cat<<16) | (sceneIndex+1)`. Evidence: `FUN_140a7cce0` builds `cat*0x10000 + 1 + i`, and `FUN_140a7c800` uses `(id & 0xffff) - 1`.
- Cut ids use the same form: T2 `cutRef = (cat<<16) | (cutGroupIndex+1)`, resolved by `FUN_141f4fda0` (§4.2).
- A T2 step with `cutRef == 0` terminates the scene. `FUN_141f616f0` stops expanding there.

**T2 scene step fields.**

| field | meaning |
|---|---|
| `kind` | OBS: the first step's `kind` byte selects the world anchor (switch in `FUN_140476c50`, §5). Distribution over all 2,267 steps: 3 (1,874), 0 (248), 1 (142), 4 (3). |
| `fade` | 0.5 in 2,164 of 2,267 steps; otherwise 0.0 (80), 0.3 (15), 0.1 (5), 0.075 (2), 1.0 (1). INF med: transition time in seconds. `FUN_141f616f0` defaults the same field to 0.5. |
| `flags` | **Scene attribute bits**, tested by `FUN_140a7c800(DB, bit, sceneId, step)` and `FUN_140a7cce0` / `FUN_140a7ce90` (collect or filter scenes by bit) on the *first* step. OBS. Goal bit meanings (INF from the names and from the code that queries them, §4.3): bit 6 = generic random celebration pool; bits 11/12/13 = goal level 0/1/2; bits 0/1 = hug A/B; bit 5 = run scenes; bit 14 is common; bits 21/22 always appear together. |
| `c0, c1, w0, w1` | Unknown. OBS: in ent, w0 ∈ {240, 270, 60} with w1 negative as i16 (e.g. −30, −90). INF lo: duration/overlap ticks. |
| `trip[]` | Up to 10 `{u8,u8,u8}` triplets, e.g. `[3,1,0]`, `[4,12,0]`. Copied verbatim into the 0x1e0-byte expanded step (`FUN_141f616f0`). Meaning open. |
| `w2` | i16. −1 (0xffff) in 2,212 of 2,267 steps; elsewhere 15..1190, values that look like 60 Hz ticks. Copied as `(float)(short)w2` into the step (default −1.0). INF lo: a step start time or length override. |

**T1 cut variant fields.**
- `key[0..9]`: condition keys (§4.2). Variants are pre-sorted by key column, so the matcher can narrow contiguous ranges.
- `fdc[0..9]`: the `.fdc` files that play together. The directory is the category's `cut_data/` (pointer table 0x1434a4c00).
- `b0`, `f`, `b1`: OBS copied into the expanded step (`FUN_141f616f0`: row +0x68/+0x6c/+0x70). For goal, `f` is a large float (e.g. 1586.39) shared by a group's variants. `b1` ∈ {1, 3}. Meanings open.

Example chain (OBS), goal scene `GOAL_CELEBRATE_0178`:
1. T2 step 0 is `kind 3, fade 0.5, cutRef 0x30000|idx+1, flags = …`; step 1 is the terminator.
2. T1 group `GOAL_CELEBRATE_0178` has 3 variants, keyed by `0x0e00`, `0x0e01`, `0x0e02`.
3. Each variant lists `goal_celebrate_0178_base.fdc`, a common camera pack (`goal_cmnCam_outM00.fdc` / `…outH00` / `…noSwitch10`), a "maya" layer (`…_mayaL0x/1x.fdc`), an outro (`…_outH1x.fdc`) and `goal_Effect_h.fdc`. The same pattern holds for `GOAL_CELEBRATE_0006/0007/0161`, all printed by `tbl.py`.

**Goal extras (OBS).**
- **`goalEditScene[162]`** maps a 1-based celebration index to a sceneId (the first entries are `GOAL_CELEBRATE_0006…`).
  - `FUN_140475a90` / `FUN_140478190` index it with the scorer's celebration setting bytes: player qword at +0x10, `>>16 & 0xff` for setting 1 and `>>24 & 0xff` for setting 2.
- **`playerId → sceneId` (35 entries)** holds the signature celebrations of specific real players. The scenes are `GOAL_CELEBRATE_00xx`.
  - The key is a player id from `FUN_140a62d90(player, …, 2)`, falling back to `…, 0`.
  - Only ids are stored. No names are involved, and none are listed here.
- **`GOAL_SCENE_INFO[338]`**, one row per goal scene: `{u8[5], f32[4]×3, u8[4]}`. Most rows are `{0,0,1,1,1}` with zeros. Meaning open; the dev dump format is `"{%d,%d,%d,%d,%d,%.1f…}"` at 0x142b9f8a0.
- **T3 Ctrl groups**: `GOAL_AREA_A..F`, `GOAL_GOLAZO_OFFSET` and `GOAL_GOLAZO_OK`.
  - Rows use condition keys of types 0x46..0x51 (event-geometry query results).
  - Each row resolves to one u32 (a sceneId) plus optional floats.
  - They are selected by the same matcher through `FUN_141f4fc30` / `FUN_141f4ace0`.

Dev-dump format strings (OBS, 0x142b9f350..0x142b9f8a0) confirm the naming: `fixdemo::DB::SceneInfoIndex/SceneInfo`, `CutInfoIndex/CutInfo {{names…},0x%x,%.2ff,0x%x}`, `CtrlInfoIndex/CtrlInfo`, `GOAL_EDIT_ID`, `GOAL_PLAYER_ID_PLAYER`, `GOAL_PLAYER_ID_SCENE` and `GOAL_SCENE_INFO`.
- **Killed hypothesis:** that the first name list in the file is the scene list. The format strings and the DB offsets show T1 is the CUT table, `CutInfo{{fdc…},0x%x,%.2f,0x%x}`, and T2 is the SCENE table.

---

## 3. `.fdc` cut file format (Q2)

### 3.1 Container (OBS: corpus scan `fdcsurvey.py`, 4,925/4,925 parse; agrees with GF `camera_cut.py`)

```
u32 n
n x { u32 offset, u32 size, u32 nameOffset }      # all offsets relative to the container start
```

- **Entry 0** is always the cut block. Its name is the file's own path, `cpk_dat/common/demo/fixdemo/<cat>/cut_data/<name>.fdc`.
- **Entries 1..n−1** are resources named by path:
  - `.gani`: 4-byte stub (14,165) = external, loaded from the VFS by stripping `cpk_dat/`; or embedded (1,184), starting with the gani magic.
  - `.seq`: 6,463, always embedded.
  - `.canm`: 5,330, always embedded, with magic `01 00 01 ff`.
  - `.ask`: 834 stubs, 54 embedded.
- **Cut block:** the same `{offset, size, typeOffset}` table relative to the block start. `typeOffset` points at a 1-byte record type inside the block's small "name" pool, so the "names" are the single bytes 0x00..0x08.

**Record type → class (OBS: switch in `FUN_141f36200`, the binary `.fdc` loader called from `FUN_141f2fcf0` when the path ends in `.fdc`):**

| type | size (B) | count | class / adder fn | evidence for content |
|---|---|---|---|---|
| 0 | 64 (56/60 in 37 older files) | 2,428 | Cut → `FUN_141f26d50` | DemoPlayCutData order |
| 1 | 156 | 1,444 | Env → `FUN_141f3e1f0` | light-config path + 39 flag bytes |
| 2 | 92 | 3,507 | Timing → `FUN_141f3f330` | event/sound names (`demo_goal_level3`) |
| 3 | 188 | 5,881 | Effect → `FUN_141f3e050` | floats; DemoPlayEffectData order |
| 4 | 364 | 6,924 | Human → `FUN_141f3e830` | gani + seq paths |
| 5 | 444 | 4,410 | Object → `FUN_141f3ecd0` | fpk + ask + gani paths |
| 6 | 284 (440/444 not seen as 6) | 9,302 | Camera → `FUN_141f3dd00` | `.canm` path |
| 7 | 324 | 624 | Function → `FUN_141f3e530` | id arrays |
| 8 | 96 | 3 | HumanControl → `FUN_141f3e6d0` | IK-control floats |
| 99 | 444 | 15 | not handled by the switch (dropped) | object-sized, editor-disabled |

- **Killed hypothesis:** that the record-type order equals the `FixDemo*Data` string order in the exe (Cut, Env, Timing, Effect, Function, Camera, Human, Object, HumanControl). Human records (which carry gani paths) are type 4, not 6; the switch gives the true mapping.
- **Record semantics source (OBS).** Each class has a reflection descriptor, `fixdemo::DataClassInfo`, registered by:
  - `FUN_141f66840` (Cut), `FUN_141f67430` (Env), `FUN_141f67cc0` (Timing), `FUN_141f66b10` (Effect);
  - `FUN_141f67c10` (Function), `FUN_141f6df80` (Camera), `FUN_141f73d70` (Human);
  - `FUN_141f781b0` (Object), `FUN_141f736f0` (HumanControl).
  These name every field with offsets in the in-memory `DemoPlay*Data` structs (`regparse.py` output, summarised below).
- **INF hi: binary layout.** The binary records follow the **same field order**, packed, with these differences:
  - strings are inline `char[128]`, not 32-byte std::strings;
  - there is no `m_memo`;
  - a leading id or slot field is added.
  The offsets below were fixed per field from corpus statistics (`recstats.py`) and agree with that order.

### 3.2 Record layouts

Times are in **1/60 s ticks**. Evidence (OBS, corpus statistic): for single-cut/single-camera files, `Cut.maxTime / canm.frameCount` is 2.0 in 113 of the files (the mode), and `.canm` says 30 fps. The example below has `maxTime 700` and a 350-frame gani. Positions are Fox metres (Y up, pitch-centre origin; GF verified `(52.5, 1, 7)` on a goal line). Angles are degrees.

**Cut (type 0, 64 B)**

| off | type | field |
|---|---|---|
| +0x00 | u32 | id (0, 1, 2, 10, …) |
| +0x04 | f32 | cutStartTime |
| +0x08 | f32 | minTime |
| +0x0c | f32 | maxTime |
| +0x10 | f32 | playSpeed (1.0 in 2,170 of 2,391) |
| +0x14 | f32 | movingDistance |
| +0x18 | u8 | cameraPattern (enum RANDOM / COMMON / EXCLUSIVE) |
| +0x19 | u8 | offset (bool) |
| +0x1c | f32[3] | offsetPos |
| +0x28 | f32 | offsetRotY (deg) |
| +0x2c | u8 | offsetFlipX |
| +0x30 | f32 | humanHeightRange (0.001 / 0.1) |
| +0x34 | f32 | humanHeightRangeCenter (1.8 m) |
| +0x38 | u32 | functionNo |
| +0x3c | u32 | 0 |

**Timing (type 2, 92 B)**

| off | type | field |
|---|---|---|
| +0x00 | u32 | packed: high 16 bits = type (`DemoPlayTimingData_Type`, e.g. 0x11, 0x12, 0x0d; enum strings `TYPE_UNIQUE_ACTION`, `TYPE_REF_CARD_Y/R`, `TYPE_REF_WHISTLE_*`, `TYPE_HUMAN_DISP`, `TYPE_REPLAY_RECORD`, …); low 16 bits = id/order (100, 200, 800, …) |
| +0x04 | f32 | startTime |
| +0x08 | f32 | endTime |
| +0x0c | i32 | targetNo |
| +0x10 | i32 | paramNo1 |
| +0x14 | i32 | paramNo2 |
| +0x18 | f32 | value |
| +0x1c | char[64] | actionName (sound/BGM/event: `demo_goal_level3`, `lost_goal_level3`, `demo_enter_clap`, …) |

The type/low-word split is INF med.

**Human (type 4, 364 B)**, in DemoPlayHumanData order:

| off | type | field |
|---|---|---|
| +0x00 | u16 | **slot** (actor binding, §5) |
| +0x02 | u16 | disp (1 in 6,920 of 6,924) |
| +0x04 | f32[3] | position |
| +0x10 | f32 | rotY (deg; GF verified 0 faces +Z, 90 faces +X) |
| +0x14 | char[128] | animePath (.gani) |
| +0x94 | char[128] | animeFacePath (.seq) |
| +0x114 | f32 | animeStartTime (ticks) |
| +0x118 | u8[4] | animeFlip, animeLoop, loopResetPos, pad |
| +0x11c | f32 | correctRateForHeight |
| +0x120 | u8[4] | offsetType (enum), lod (enum), 2 bytes |
| +0x124 | u32 | functionNo |
| +0x128 | u8[4] | ikBone1, ikBone2, ikTargetKind1, ikTargetKind2 |
| +0x12c | u8[4] | ikTargetNo1, ikTargetNo2, ikTargetBone1, ikTargetBone2 |
| +0x134 | f32[3] | ikTargetPos1 |
| +0x140 | f32[3] | ikTargetPos2 |
| +0x14c | f32[2] | ikOnStartTime1, 2 |
| +0x154 | f32[2] | ikOnEndTime1, 2 |
| +0x15c | f32[2] | ikOffStartTime1, 2 |
| +0x164 | f32[2] | ikOffEndTime1, 2 |

- The pair interleaving from +0x128 is INF med: occupancy counts pair up (e.g. 513/542/723/723 vs 196/208/307/307).
- Eyes: the loader `FUN_141f34790` builds `<animePath stem>_eyes_target.gani` (string at 0x142b9fcd0) and loads it with `eyes_target_anim_skel.ask` / `pes_eyes_target_150120.frig`. OBS: string xref only.
- The `.seq` (FHSeq; class `fixdemo::FHSeqDataBody`) is the face/hand event sequence. Per GF `camera_cut.py`, it is "a small event/marker table, u16 frame windows, not motion" (INF med; not re-derived here). The default is `dml_default.seq`.
- Dev pipeline: `.hkx` paths are rewritten to `.gani` on load (`FUN_141f3e830`, which compares against `.hkx` at 0x142b9fa94).

**Object (type 5, 444 B)**

| off | type | field |
|---|---|---|
| +0x00 | u16 | slot (GF: 80/85/90/100/110/200/201/4000+ for walkout props) |
| +0x02 | u16 | disp |
| +0x04 | f32[3] | position |
| +0x10 | f32[4] | rotation quaternion (x, y, z, w) |
| +0x20 | char[128] | modelPath (.fpk) |
| +0xa0 | char[128] | skeletonPath (.ask) |
| +0x120 | char[128] | animePath |
| +0x1a0 | f32 | animeStartTime |
| +0x1a4 | u8[4] | animeLoop, loopResetPos, planeShadow, referStaffTex |
| +0x1a8 | u8[4] | supporterTypeTeam, supporterTypeShirt, supporterTypeFacial, offsetType |
| +0x1ac | | functionNo / modelType area |
| +0x1b4 | u8[4] | parentKind (NONE/HUMAN/OBJECT), parentNo, parentPosBone, parentRotBone |
| +0x1b8 | | parentRotUpdateOnce + pad |

The fields from +0x1a4 on are INF med.

**Camera (type 6, 284 B)**, in DemoPlayCameraData order:

| off | type | field |
|---|---|---|
| +0x00 | u32 | record key (records are kept in a `std::map<int,…>` keyed by this value: `FUN_141f20810` / `FUN_141f369f0`, OBS) |
| +0x04 | f32 | **startCutTime** (ticks) |
| +0x08 | u16 | originalCameraId (0xffff) |
| +0x0a | u16 | envDataId |
| +0x0c | char[128] | animePath (`<cat>/canm/<clip>.canm`, embedded in the same fdc) |
| +0x8c | f32 | animeStartTime |
| +0x90 | f32 | nearClip |
| +0x94 | f32 | farClip (they override the canm's channels 3/4 per GF) |
| +0x98 | f32 | nearClipScale |
| +0x9c | u8[4] | animeTimeEqCutTime, animeLoop, returnTopCamera, pad |
| +0xa0 | f32 | correctRateForHeight |
| +0xa4 | u8[4] | lensDistortion, autoStandClip, offsetType (0..3), pad |
| +0xa8 | u32 | functionNo |
| +0xb0 | u8[4] | targetKind (MAINSIDE_SPOTLIGHT / OTHERSIDE_SPOTLIGHT / MAINSIDE_CAPTAIN / OTHERSIDE_CAPTAIN / HUMAN_NUMBER / OBJECT_NUMBER / HUMAN_CENTER), targetNo[3] (0xff = none) |
| +0xb4 | u32 | zoomingTimeRate (50/90/95) |
| +0xb8 | i32[8] | ZOOM_HEADUP, BUSTUP, WAISTUP, KNEEUP, FULLSHOT, FULLSHOT_X2/X3/X4 (−1 = off; values in ticks) |
| +0xd8 | u8[23] | SelectByRate (100), SelectByTargetFront, SelectByTargetBack, cameraGR / 1F / 2F / Moving, then 16 camera-position flags BackLeft … CornerLeftBack |
| +0xef | | depthOfField, dofTargetNo, dofDistance, dofF, dofFront/RearOffset, dofChangeStartTime, dofChangingTime, then the same set for target 2 |

- **Killed hypothesis** (GF `camera_cut.py`): that +0x00 is "startFrame". In `change_frustration_cam.fdc` the camera has key 10 and startCutTime 270, and the field is a map key in code. The start time is +0x04.
- `.canm` format: see GF `camera_cut.py`. It is a CANM v1 stream at 30 fps with 6 channels: rotation quat, position vec4, vertical FOV in degrees, near, far, aspect. I did not re-derive it.

**Env (type 1, 156 B):**
- +0x00 u16;
- +0x02 `char[128]` lightPath (e.g. a `light_config_*.xml` under `fixdemoobj/`; empty in 1,344 of 1,444);
- +0x82.. 39 u8 flags in DemoPlayEnvData order: stadiumDisp, stadiumBill{Main,Back,Left,Right}, CornerFlag, Cheer, CameraCrane{L,R}, Stand{Main,Back,Left,Right}, Cameraman×4, Others×4, Coach, Bench, EntranceRoute, DemoParts, BackGroundParts, audiDirectionCamera, audiResolutionHigh, audiLod (enum), farGrass, haloType (enum), starFilter, lensFlare, rainType (enum), antiAliasing (enum), vignetting, shadowMap, mobShadow, motionBlur.

Field alignment from +0x82 is INF med.

**Effect (type 3, 188 B):** DemoPlayEffectData order:
- position, rotation, scale, type, no;
- start/end time, directionRange, life (+random), shootInterval/Number, initialTime/Speed (+random), speedChangeRate, moveSpeed, groundHeight;
- scaleLimit, roll (+random/change), 4 opacity keys, 4 colour keys, colorType.

**Function (type 7, 324 B):** id + arrays of u32 function codes, then floats. Not decoded.

**HumanControl (type 8, 96 B, 3 files):**
- type, 3 × (condition enum, value), on/off times, boneOffset;
- bone (IK bone enum), targetKind, targetNo, 3 × (targetBone, targetPos).

### 3.3 Decoded example: `goal/cut_data/goal_celebrate_0178_base.fdc` (2,800 B; `python3 fdcdec.py`)

```
resources:
  dml_goal_celebrate_0178.gani                 external (VFS: common/demo/anime/FoxAnim/FixDemo/Animations/, 33,568 B, 350 frames)
  dml_goal_celebrate_0178.seq                  embedded 436 B
  dml_goal_celebrate_0178_eyes_target.gani     external
  dml_goal_celebrate_0178_sub1.gani            external
  dml_goal_celebrate_0178_sub1.seq             embedded 332 B
  dml_goal_celebrate_0178_sub1_eyes_target.gani external
records (7):
  Cut     id 0, cutStartTime 0, minTime 700, maxTime 700 (=11.67 s), playSpeed 1, movingDistance 1083.73,
          cameraPattern 0, offset 0, offsetPos (0,0,0), rotY 0, humanHeightRange 0.001, center 1.8
  Env     no light path; flag bytes = scene defaults
  Timing  type 0x11 id 100: startTime 410, actionName "demo_goal_level3"
  Timing  type 0x12 id 200: startTime 410, actionName "lost_goal_level3"
  Timing  type 0x0d id 800: startTime 380, endTime 500, no name
  Human   slot 0, disp 1, pos (0,0,0), rotY 0,   anime dml_goal_celebrate_0178.gani, face .seq, offsetType 1, lod 0
  Human   slot 1, disp 1, pos (3.094,0,-2.603), rotY 347.0, anime …_sub1.gani, face …_sub1.seq, offsetType 1, lod 1
```

- The base file carries no camera. Cameras come from the layered camera fdc in the same variant: `goal_cmnCam_outM00.fdc` or `outH00`, etc. (§2.3).
- Slot 0 is the celebrant at the scene origin, which is the anchor (§5). Slot 1 is a team-mate, 4 m away.

---

## 4. Runtime selection (Q3)

### 4.1 Classes (OBS: RTTI names from the image; vtables via `bin/vt.py`)

- **Managers:**
  - `FixDemoManager` (vtable 0x142ba6800; step enum strings `STEP_IDLE / CREATE / LOAD / RESUME_WAIT / WAIT / NEXT_LOAD / EXEC / END`);
  - its subclasses `FixDemoMatch` (0x142ba6bc0), `FixDemoEntrance` (0x142c701f8), `FixDemoReplay` (0x142c70458), `FixDemoResult`, `FixDemoMode`;
  - `FixDemoList` (0x142ba67d0; expands scene ids into steps), `FixDemoMain`.
- **Data:** `fixdemo::DB` (the decoded tables), `SceneCutTableDataBody`, `FixDemoCutFile` (with nested `HumanAnimeData`, `HumanWork`, `AnimationData`, `ObjectData`, `LightData`), `FixDemoCutParameters`.
- **Loader bodies:** `BinDataBody`, `FileDataBody`, `FoxDataBody`, `JsonCutDataBody`, `AnimeDataBody`, `AnimSkelDataBody`, `FHSeqDataBody`; tasks `LoadCutParamTask`, `LoadCutParamAnimeTask`, `LoadLightTask`, `LoadObjectTask`.
- **Players:** `FixDemoHuman`, `FixDemoCamera`, `FixDemoObject`, `FixDemoEnv`, `FixDemoEmptyCamera`; `DemoPlay{Cut,Env,Timing,Effect,Function,Camera,Human,Object,HumanControl}Data`.
- **Randomness:** `fixdemo::Random1000`.
- **Match side:** `match::registry::{DemoInfo, DemoPlayerInfo, FixDemoInfo, DemoControl}`, `match::action::anime::DemoAction`, `match::camera::demo::*`. The in-play camera "demo" director classes (`DmCamera`, `DmCamAnime`, …) are separate from fixdemo.
- **Context singleton:** `FUN_141f500d0` returns `*(0x1437f5d50)`, a 0x52e0-byte object whose RTTI is **`FixDemoEnv`**. It is the condition context (OBS live, §7).

### 4.2 Pipeline (OBS unless marked)

1. **Scene list.** The event builder fills a DemoInfo (`FUN_14047c500` writes the context: subject side `ctx+0x5204`, event position `0x5208/0x5210`, event player `0x5214/0x5218`) with a list of scene requests. Each request is 0x1c bytes, `{sceneId, 0x1b, …}`.
2. **Expansion.** `FixDemoList` (`FUN_141f616f0`) walks each scene's T2 steps until `cutRef == 0`. For every step:
   - it copies the step into a 0x1e0-byte play step;
   - it calls `FUN_141f4fda0(cutRef)` to pick the cut variant row (0x78 B);
   - it copies the variant's `b0`, `f`, `b1` and fdc list into the play step.
   `FUN_141f5ec60(sceneId)` returns a scene's duration using the same expansion.
3. **Variant pick, `FUN_141f4fda0`.**
   - A per-manager `std::map<cutId, rowIndex>` (+0x30) caches the pick, so a cut resolves to the same variant for the rest of that manager's life.
   - On a miss it calls `FUN_141f4b1d0(mgr, cat, rows, nRows, history = 0x1437f5dc0 + cat*0x40, …)`.
4. **Matcher, `FUN_141f4b1d0`.** For key column k = 0..9:
   - It stops when the range has fewer than 2 rows or when the first row's key in column k is 0.
   - It calls `FUN_141f4d2f0(ctx, &accepted, key, cat, 0)` to get the context's accepted values in priority order.
   - It narrows to the block of rows equal to the first accepted value present. If nothing matches, the column is skipped.
   - Type 0x19 keeps the first block with key ≥ a random threshold.
   - Type 0x43 is a weighted random pick: row keys are cumulative percentages; the weight of row i is `cum_i − cum_{i−1}`. Blocks already used are masked through the per-category history at `0x1437f5dc0 + cat*0x40`, so a repeat is avoided.
   - It returns the first row of the final range.
   - Appendix A lists every condition type.
5. **Ctrl rows (goal T3)** are resolved the same way by `FUN_141f4fc30` / `FUN_141f4ace0`, with history at `0x1437f6080 + cat*0x40`.
6. **Load and play.** The manager steps through `STEP_LOAD … STEP_EXEC`. `FixDemoCutFile` loads the fdcs (`FUN_141f36200` → per-type adders). `Human` records go into a map keyed by slot (`FUN_141f3e830`). Camera records go into a map keyed by their +0x00 key (`FUN_141f3dd00`).

**Randomness (OBS).**
- **`FUN_140a92530(rng, n)`:** `s = (s*0xD2DD4D1D + 0x4513) & 0x7fffffff` (the code writes `-0x2d22b2e3`); `r = clamp((int)(s * 4.656613e-10 * n), 0, n−1)`. The state is at `rng+0xc`. The goal builder uses this generator, with its object at goal-ctx+0x108.
- **`FUN_141f7a100(rng, n)`** (condition types 0x19/0x43):
  - `t = (int)(s'*2^-31*684)`, where `s'` is the same LCG on `rng+0x10`;
  - `st = (t + *(rng+0x20) + 0xaa) & 0x3ff`, stored back;
  - the result is `st*n >> 10`.
  - The generator object is `0x1437f6340` when `ctx+0x5228 == 2` (match), else `0x1437f5d90`. Both are static `Random1000` objects (vtable 0x142ba5668, confirmed live).
- **Open:** seeding. The seeds were not traced; the constructor `FUN_140a92430` only sets the vtable.

### 4.3 Goal: which celebration plays (`FUN_140478190` → `FUN_140477bc0` → `FUN_140475a90`; OBS)

1. **Signature celebration.** Look up `playerId → sceneId` (table map, DB+0x438), keyed by the scorer's id type 2, then id type 0. A hit is stored in goal-ctx[0x4e] and preloaded.
2. **Primary scene, `FUN_140475a90`.**
   - If match mode `G+0x270+0x1574` is 5 with a flag set, the scene is `GOAL_S_OFFSIDEGOAL_START` (0x300f9). Any mode other than 3 gives 0.
   - Unless the period is 9 or certain team checks fail, the per-side celebration mode `goal-ctx[0x54 + side*4]` decides:
     - 0: the scorer's edit setting 1 → `goalEditScene[b−1]`;
     - 1: setting 2;
     - 2: `FUN_14047a2c0(...,2,1)`;
     - 3/4/5: random generic;
     - 7: `FUN_1404ba3d0`;
     - 8: special path. It has golazo variants `GOAL_S_GOLAZO_2019/2020` (0x3013b/c), chosen by `rand(100) > 49` or alternation. It retries `FUN_140471280` / `FUN_14047a2c0`, and with `rand(10000)/100 > 29` it may fall to random generic.
   - **Random generic** is `FUN_140477400`:
     - start from the pool of all goal scenes with flag bit 6;
     - filter by bit 11, 12 or 13 according to goal-ctx[0] ∈ {0, 1, 2} (INF med: the goal "level");
     - optionally filter by bit 16, gated by a global and a vtable query;
     - pick uniformly with the LCG;
     - if the pick equals the previous celebration (goal-ctx+0x13c), retry; after 10 tries, give up with the scene 0 default.
3. **Scene list, `FUN_140477bc0`.** The order is:
   1. Own-goal / pickup special ids are handled entirely by `FUN_14047abf0`: 0x300f6–0x300f9 (`GOAL_S_OWNGOAL_*`, offside), 0x3013d–f (`*_PICKUPBALL*`), 0x30151.
   2. An optional pre-scene from `FUN_1404717c0` and flag 9.
   3. An optional team-mates scene from `FUN_140476100`, when flag 0xe is clear and either `FUN_141f50fc0(0x304,3)` holds or level > 1.
   4. The primary scene.
   5. A hug scene, `FUN_140474b20`: pool = scenes with bit 2 or bit 3, filtered by bit 12/13; uniform pick. The names are `GOAL_HUGA/HUGB_LV2/3`.
   6. An interactive "set" scene `GOAL_I_SET00..09` (0x30130..9), chosen by goal-ctx[8] = 1..10.
   7. `GOAL_F_TURN` (0x3011c) if `FUN_140470fd0`.
   8. `GOAL_COACH_INTERACTIVE00` (0x3012f) if `FUN_140471210`.
4. **Total duration**, `FUN_140474cf0`: the sum of `FUN_141f5ec60(scene)` over the scenes that have flag 5, 0xb, 0xc, 0xd or 7. It is forced to 3.0 for `GOAL_S_GOLAZO_2019` and to 0 when goal-ctx+0x131 is set.
5. **Anchor.** `FUN_140476c50` computes the world position of the scene origin (§5).

### 4.4 Other events (OBS: the callers of the DemoInfo builder `FUN_14047c500`)

| builder | event | constants (sceneIds) | selection |
|---|---|---|---|
| `FUN_140478190` | goal | 0x300xx | §4.3 |
| `FUN_1404789c0` | goal follow-up | 0x301b6 | |
| `FUN_140480910` | foul / card | 0x20022 | |
| `FUN_140484a10` | substitution | 0x50010 | |
| `FUN_140488470` | period end | 0x60001, 0x6000d, 0x7002b | by period `*(G+0x270+0x1168)`: 4 → 0x6000d (`TU_FULL_NORMAL_NORMAL`); 6 → 0x7002b; 8/10 → `FUN_140486b10` (full time: picks `TU_FULL_<moodHome>_<moodAway>`, INF); 9 → `FUN_1404878f0`; else 0x60001 (`TU_HALF_CMN`) |
| `FUN_14048ab10` | end of match | 0x7002b | |
| `FUN_140471a80` | goal sequence variant (uses `FUN_140477bc0`, `FUN_140474cf0`) | | |

- The constants 0x20022, 0x50010, 0x7002b and 0x301b6 index **past the end** of the shipped tables (foul has 33 scenes, change 15, end 41, goal 338).
- The DB accessors bounds-check (`FUN_140a7c800`: `idx < count`) and return false/null.
- **Open:** whether these are "no demo" sentinels or scenes added by a later update. INF med: sentinels. 0x20022 and 0x50010 are exactly count+1, 0x7002b is count+2 and 0x301b6 is count+100, so no single rule covers them all.
- **Entrance:** sequenced by `FixDemoEntrance` with `ENT_*` scenes. The ent T1 variants key mostly on 0x01 (stadium), 0x02 (competition), 0x1e/0x1f (team class / home team), 0x12/0x13 (time of day / weather) and 0x43 (random).
- **Open:** I did not trace which `ENT_*` scene sequence is requested per competition.

---

## 5. Actor and camera binding (Q3)

**Slots (OBS: histogram over all 6,924 Human records, `<scratch>/CutsceneRE` one-liner; GF `entrance_pl.py` agrees for ent).**

| category | slots used |
|---|---|
| ent | 0–24 evenly (0–10 home XI, 11–21 away XI, 22–24 officials) |
| goal | 0 (469), 1 (390), 2 (348), 3 (184), 4 (150), … plus 13–17, 21, 26 (24) |
| foul | 1–5, 11–14, 22 (56 = referee) |
| end | 0–10 dominant |
| mode | 0, 11, 22–25 |

- **Binding (INF med):**
  - slot 0 = the subject/event player (the scorer, the carded player);
  - slots 1..10 = his team-mates in team order;
  - slots 11..21 = opponents;
  - slots 22–25 = officials (22 = referee);
  - slot 26 = coach. In goal scenes, `GOAL_COACH_*` scenes exist.
- The slot → match-player mapping code was not traced; this inference rests on the data and GF.
- The code bounds dev-path slots at ≤ 0x1a (26) (`FUN_141f3e830`, OBS).
- The condition context keeps per-side **40-slot** player arrays at `ctx + 0xf8 + side*0x2830 + i*0x100` (Appendix A), with slot 0 = the starting GK.

**Anchor (OBS: `FUN_140476c50`).** The scene origin is chosen by the first step's `kind`, read through `FUN_140a7cba0(DB, sceneId)`:

| kind | anchor |
|---|---|
| 1 | `FUN_140476190` (subject player position) |
| 3 | `FUN_1404b77d0` (goal scenes) |
| 4 | a stadium point (object +0x130..0x138, with a default) |
| 5 | subject player position shifted by `side × const` |
| 6 | position of a specific player chosen from `G+0x270+0x162c` |
| 7 | `FUN_140476010` |
| other | `FUN_1404b6ed0` |

- An entrance can instead use the current event position (`param_4` path).
- Cut-level `offsetPos/offsetRotY/offsetFlipX` and `humanHeightRange*` adjust placement. **INF med:** `correctRateForHeight` scales by actor height around 1.8 m.

**Cameras.**
- The camera layer is a separate fdc in the same variant.
- Camera records select framing targets by `targetKind`/`targetNo`, which refer to Human slot numbers or Object numbers.
- They choose among authored positions through the 23 select/position flag bytes, weighted by `SelectByRate`.
- Zoom levels have start ticks.
- `cameraPattern` (Cut) is RANDOM, COMMON or EXCLUSIVE: how camera variants are shared between cuts (INF lo).

---

## 6. Replays (Q4): recorded state

**Classes (OBS):**
- `match::Replay` (vtable 0x142bd3e50), `ReplayHuman` (0x142bf7d58), `ReplayBall` (0x142bf7a68), `ReplayObject*`, `CameraReplay`, `DmCamEnvReplay`;
- `DataManagerReplay` (0x142654040), which owns **21** `UnitReplay` (0x142653f58) at stride 0x50;
- `HighlightManager`, `RecordReplay`, `RecordReplayListener`, `Unit<ReplayData::Record, RecordDemoDifference>`;
- the processes `ProcessMatchReplay` and `ProcessMatchReplayGallery` (strings `Match/MatchReplay`, `MatchHalftimeHighlight`, `MatchResultHighlight`, `PLAY_DATA_REPLAY_SAVE`, `replay/`).

**Unit (OBS: `FUN_140a8ee20`, `FUN_140a8ed70`; live).**

| unit field | meaning |
|---|---|
| +0x10 | u16 write cursor |
| +0x12 | u16 capacity (frames) |
| +0x14 | u32 total frames written |
| +0x20 | appearance palette: count at +0x7c, 0xa0-byte entries from +0x80 |
| +0x40 | frame array, `capacity × 0x1fb0` bytes |
| +0x48 | a parallel `capacity × 2000`-byte array |

- `FUN_140a8b6a0(mgr, dst, src, …)` copies frames between units (index < 0x15). The copy is gated by `param_2 < 2`.
- **INF med:** units 0–1 are the live rolling buffers; the others hold saved highlight or gallery clips.

**Frame (0x1fb0 = 8,112 B) = 0x264-byte header + 25 × 300-byte human records** (OBS: 0x264 + 25×300 = 0x1fb0; writers index humans < 0x19).

Header fields (OBS: writers `FUN_140a89f20`, `FUN_140a88e00`, `FUN_140a89e00`, `FUN_140a88ce0`):

| off | content |
|---|---|
| +0x00 | u32 (clock/state; copied forward when mode 7) |
| +0x07 / +0x08 / +0x0e | packed state nibbles |
| +0x09..0x0d | ball state bitfields |
| +0x0ac | event block (6/4 sub-entries by kind) |
| +0x0a8 / +0x0aa | u8 + packed value |
| +0x24c | i16[3] ball position × 256 (metres) |
| +0x254 | 16 B: ball rotation quaternion or spin (INF) |

Human record, at `frame + 0x264 + i*300` (OBS: writers `FUN_140a89030`, `FUN_140a893f0`, `FUN_140a898d0`):

| off | content |
|---|---|
| +0xa6 | i16 heading × 100 |
| +0xa8 / +0xaa / +0xac | i16 position x/y/z × 256 (metres). x = 0x7fff marks the human as absent in that frame (`ReplayHuman` reads `frame + i*300 + 0x30c`, and 0x30c − 0x264 = 0xa8) |
| +0xae..+0xb2 | i16 vec3 × 256 (INF: velocity or facing vector) |
| +0xb4 | u16 motion id A |
| +0xb6 | i16 motion time × 1000 |
| +0xb8 | u16 motion id B |
| +0xba, +0xbc | i16 times or weights × 1000 |
| +0xbe / +0xc0 | u16 ids of a second layer |
| +0xc2, +0xc4 | i16 × 1000 |
| +0xce..+0xd0 | packed flags (2-bit, 2-bit, bools, a weight × 62) |
| +0x106 | flags |
| +0x107 | u8 index into the unit's 0xa0-byte appearance palette (deduplicated) |
| +0x108 | i16 × 100 |
| +0x10a..+0x10f | bytes |
| +0x110 / +0x111 | packed |
| +0x112..+0x123 | 3 × u16[3] (INF: head/eye/IK look targets) |
| +0x124 | u32 |
| +0x128 | 5 bool bits |

**Playback (OBS).**
- `ReplayHuman::FUN_14217def0` reads a frame's record for its human index: `frame + i*300 + 0x30c`, then the palette byte at +0x36b.
- It copies the 0xa0-byte palette entry into the actor.
- The pose is rebuilt from motion ids and times. No simulation step runs on this path.
- **INF hi:** replays are recorded state, not re-simulation. **Killed hypothesis:** re-simulation from inputs. Nothing in the units stores inputs; they store positions, motions and ball state per frame.

**Rate and length (OBS live).** The unit-0 cursor went 591 → 45 (wrapping at 660) in about 2.0 s, i.e. 114 frames, ≈ 57 Hz. With 660 frames, the live ring holds ≈ 11 s at the 60 Hz game tick.

**Not decoded:** the 2000-byte parallel array and the exact header bitfield names.

---

## 7. Live observations (Q5)

Method: `sudo` reads of `/proc/<pid>/mem` only (`livescan.py`: scan rw regions for vtable qwords; `livedump.py`).

- **State:** in-play match. The replay ring advanced (above). No fixdemo was playing:
  - no live `FixDemoManager` instance was found;
  - a `FixDemoMatch` hit at 0x2c523b50 had been overwritten with bone-matrix floats by the time it was dumped (heap reuse; a stale hit).
- **Live objects** (one scan; counts include stale heap copies):
  - DB ×1 (0x1bbde3a0; scene counts checked, §2.2);
  - `SceneCutTableDataBody` ×6, `FixDemoCutFile` ×50, `FixDemoMain` ×46, `FixDemoCutParameters` ×46;
  - `FixDemoHuman` ×19, `FixDemoObject` ×107, `FixDemoCamera` ×1, `FixDemoEnv` ×1 (the condition context at 0x2662b570 = `*(0x1437f5d50)`);
  - `Random1000` ×5 (including the statics 0x1437f5d90 and 0x1437f6340);
  - `DataManagerReplay` ×1 (0x382a4f80) with 21 units, `ReplayHuman` ×7, `ReplayBall` ×1, `HighlightManager` ×1.
- **History state** at `0x1437f5dc0 + cat*0x40`: non-empty for categories 1 (ent), 2 (foul), 3 (goal), 6 (timeup) and 8 (result). In this session an entrance, a foul, a goal, a half time and a result demo had already run (INF med).
- Live context: `ctx+0x5204` (subject side) = 0, `ctx+0x5228` = 2 (match RNG) [OBS dump].
- **Not done:** capturing a playing demo's resources. That needs the game in a demo state, and the brief forbids input.

---

## 8. Open items and what was tried

- **Scene step `trip[]`, `c0/c1/w0/w1`, T1 `b0/f/b1`, `GOAL_SCENE_INFO`:** copied verbatim by `FUN_141f616f0`; the playback consumers were not traced.
- **Slot → player mapping code:** not located. The data histogram and GF's ent observation support the convention. The next step is the `FixDemoHuman` creation path from `FixDemoMatch` (vtable 0x142ba6bc0, method `FUN_141f5e630`) and `DemoPlayerInfo` (`ScopedWrite<DemoPlayerInfoRef>`).
- **Entrance sequence choice and the out-of-range scene constants (§4.4):** not traced beyond the builders.
- **RNG seeding:** not traced.
- **`.seq` internals:** not re-derived. GF describes it as an event/marker table.
- **Ctx fields of unknown meaning:** listed at the end of Appendix A.
- **Ghidra:** the GUI MCP was down during this work. All decompiles came from a private headless copy without auto-analysis (pdata bounds). Some functions are split across `.pdata` chunks, so a few decompiles cover only one chunk; this was noted where it mattered (e.g. `FUN_140474f00`).

---

## Appendix A: condition evaluator `FUN_141f4d2f0` (complete)

Produced by a delegated pass over `<scratch>/CutsceneRE/dec/141f4d2f0.c` plus 18 setter decompiles (`<scratch>/CutsceneRE/cond/`). It is reproduced verbatim below.


Scope: every condType handled by `FUN_141f4d2f0(ctx, vec<u32>* accepted, u32 key, u32 category, char staticOnly)`.
Evidence: decompiles in `<scratch>/CutsceneRE/dec/141f4d2f0.c`, `<scratch>/CutsceneRE/dec/141f4b1d0.c`, `<scratch>/CutsceneRE/cond/dec/*.c`; ctx-write scanner `<scratch>/CutsceneRE/cond/scan.py`.
Tags: **OBS** = read directly in the code. **INF** = inferred, with a confidence level (hi/med/lo).

### A. Calling convention and how the result is used (OBS)

- `type = key & 0xff00`. The function clears `accepted` and then pushes full keys `(type | value)` in priority order. Every push goes through `FUN_14047be40`, which is `std::vector<u32>::push_back` with the self-aliasing guard.
- **`param_5` ("staticOnly") early-outs.** In the code, `if (param_5) return;` means "push nothing". The selector `FUN_141f4b1d0`, plus `FUN_141f4ace0`, `FUN_141f41ce0` and `FUN_141f50fc0`, all pass **0**. Only `FUN_141f4b6c0` (call at 0x141f4b875) passes **1**. When it is 1, only these types are evaluated: 0x01 0x02 0x03 0x04 0x05 0x0b 0x0c 0x10 0x11 0x12 0x13 0x1e 0x1f 0x23 0x27 0x40 0x44 0x52 0x53. Every other type returns an empty list.
- `category` (`param_4`) = `(demoId >> 16) & 0xff`. Source: `FUN_141f4fda0` passes `uVar11 = param_2>>16 & 0xff` to `FUN_141f4b1d0`, which passes it through. The demo id's high byte is the cut category: for example, 0x80001 belongs to category 8 and 0x90001..0x9000b belong to category 9.
- What `FUN_141f4b1d0` does with the list:
  - **Column type.** For each of the 10 key columns, the type comes from the key of the *first row of the current range*. If that key is 0, or the range has fewer than 2 rows, the loop stops.
  - **Normal types.** It walks the accepted values in order. For the first value that occurs in the range, the range shrinks to the contiguous block of rows equal to that value. **If the list is empty or nothing matches, the column does not narrow.** Rows whose values can never be pushed therefore act as don't-care fallbacks.
  - **0x19.** The first row whose key is ≥ the accepted value (`0x1900+r`) is kept, together with its block of equal keys.
  - **0x43.** Weighted pick. Row weights are `cum_i - cum_{i-1}` with `cum = key-0x42ff`, and blocks disabled by a per-cut history mask are skipped. The pick is `(accepted-0x42ff)*sumWeights/100`.

### A. Notation

- `ctx`: the singleton returned by `FUN_141f500d0`. Its constructor is `FUN_141f4c950` and its reset is `FUN_141f50b90`.
- `S(s) = ctx + 0xf8 + s*0x2830`: per-side block, with s=0 for home and s=1 for away (array constructed in `FUN_141f4c950` with stride 0x2830, count 2).
- `P(s,i) = S(s) + i*0x100`: player slot i (0..39) of side s. Slot 0 is the starting GK; slots 0..10 are the starting XI.
- `subj = *(int*)(ctx+0x5204)`: subject side. `opp = (subj<2) ? (subj==0) : 0`.
- `push(x)`: append to the accepted list.
- `tblCat = u32[11] @0x1437f5d58 = {0,6,2,7,3,0xf,6,0xc,0xb,0xa,0xa}`, read as `tblCat[category]` (0 if category ≥ 11).
- `rand100()`: `FUN_141f7a100(rng,100)`, which returns **0..99** (details in the helper section). `rng = (ctx+0x5228==2) ? 0x1437f6340 : 0x1437f5d90`.

### A. Condition table

| condType | accepted values pushed (in order) | source (ctx offset / helper) | meaning |
|---|---|---|---|
| 0x01 | `0x100\|v` if v ∈ 1..0x5a (exceptions: v=0x3c pushes 0x11d, v=0x50 pushes 0x12e; any other v pushes no specific value), then `0x100` | int `ctx+0x50` (environment copy +0x00) | OBS: copied from `world+0x35960+0x70` by `FUN_1414c07b0` in `FUN_141f51630`. Mode demos override it: 0x90001 sets it to 0x30; 0x90002..9 and 0x9000b set it to 0x45 (`FUN_141f18b70`). INF hi: **stadium id**; 0x3c/0x50 are variants that alias 0x1d/0x2e. |
| 0x02 | Pre-push: `0x213` if `c<8 && *(int*)(ctx+0x64)==0x21` (c = int `ctx+0x5174`). Then `switch(c)`, with r = byte `ctx+0x5179` and raw = int `ctx+0x5170`:<br>• **8..0xe**: [0x218, 0x20a if r∈4..6] [0x209 if r∈3..7] [0x215 if r∈1..7], 0x217, 0x208<br>• **0xf..0x15**: [0x21a, 0x20d if r∈4..6] [0x20c if r∈3..7] [0x216 if r∈2..7], 0x219, 0x20b<br>• 0x16: 0x21b, 0x20e<br>• 0x17..0x1a: 0x214, 0x20f<br>• 0x1b..0x1e: 0x211<br>• 0x1f: 0x212<br>• 0x20: 0x210<br>• 0x2c: 0x220 if raw==0x2d<br>• 0x30: [0x234 if raw==0x31] [0x235 if raw==0x32]<br>• 0x34: [0x221 if raw==0x35] [0x233 if raw==0x36]<br>• 0x37: 0x222 · 0x38: 0x225<br>• 0x3a: [0x22b if raw==0x3b] [0x236 if raw==0x3c]<br>• 0x3e: 0x22a · 0x41: 0x22c · 0x49: 0x22d · 0x4e: 0x226 · 0x4f: 0x229 · 0x50: 0x21e · 0x51: 0x22f · 0x52: 0x223 · 0x53: 0x228 · 0x54: 0x232 · 0x55: 0x21d · 0x5a: 0x227 · 0x5e: 0x21f<br>• 0x63: [0x230 if raw==100] [0x231 if raw==0x65]<br>• 0x75..0x78: 0x21c · 0x7b: 0x237 · 0x99: 0x238<br>• 0xb0: 0x207 · 0xb1: 0x201 · 0xc0: 0x213, 0x202 · 0xc4: 0x203 · 0xc8: 0x204 · 0xc9: 0x205 · 0xd3: 0x206<br>• any other c: nothing<br>Always last: `0x200` | `ctx+0x5174`, `ctx+0x5179`, `ctx+0x5170`, `ctx+0x64`; helper `FUN_141e55e30(c)` = `c<8` | OBS: `0x5174 = FUN_141e554b0(FUN_14252e6f0(setup+0x498))`, where `FUN_141e554b0` folds sub-ids into a parent id (1,9..e→8; 2,0x10..0x15→0xf; 3,0x18..1a→0x17; 0x29,0x2a→0x28; …). OBS: `0x5179` is set in `FUN_141f51630` from setup flag queries 0xda (`FUN_140a7f370/3b0/3e0`), setup+0xc31a and setup+0xe904 bits; values 1..11. INF hi: **competition id (parent)**. INF med: 0x5170 = raw (child) competition id; 0x5179 = **tournament round/stage**. |
| 0x03 | Evaluated only when `(G1==0 && ctx+0x5188!=0) \|\| (G1!=0 && (u16 ctx+0x5184!=G1s \|\| ctx+0x5188!=G1))`; otherwise only `0x300`. With A = byte `ctx+0x51fc[subj]` and O = `ctx+0x51fc[opp]`:<br>• O=0, A=0: 0x300<br>• O=0, A=1: [0x304 if G2] 0x303, 0x300<br>• O=1, A=1: [0x302 if G2] 0x301 [0x304 if G2] 0x303 [0x306 if G2] 0x305, 0x300<br>• O=1, A=0: [0x306 if G2] 0x305, 0x300 | `ctx+0x5184`(u16)/`0x5188`(i32) = setup+0x30 (8 B); `ctx+0x51fc/0x51fd`; globals G1 = i32 @0x14351ae34, G1s = u16 @0x14351ae30, G2 = i32 @0x14352a3c0 | OBS: `0x51fc[s] = FUN_140a55ef0(G+0x180, s)`, which is true if any of 24 entries (stride 0x74) is valid (`+0x10 != -1` or `+3`), not disabled (`+0xd == 0`), and its byte0 maps to side s (0 maps to side 0, 2 maps to side 1). INF lo: per-side "has pending event entry" flag (cards/injury/sub queue?). INF lo: 0x5184/0x5188 = fixture identity, compared with a global "previous fixture". |
| 0x04 / 0x05 | One value only (no fallback). k = int `S(s)+0x2820` (ctx+0x2918+s·0x2830), a = byte `S+0x2824`, b = byte `S+0x2825`:<br>• k==2: a ? 0x407−b : 0x405−b<br>• k==3: 0x409−b<br>• k==4: 0x40b−b<br>• other: a ? 0x403−b : 0x401−b<br>Here b is 1 if nonzero, and +0x100 for s=1. | helper `FUN_141f4d0f0(ctx,vec,s)`, with s=0 for 0x04 and s=1 for 0x05 | OBS: in `FUN_141f51630`, k = byte teamSetup+0x2e, a = bit4 of teamSetup+0x30, b = bit5 of teamSetup+0x30, where teamSetup = setup+0xc400+s·0x2804 and setup = `G+0x2ad0`. INF lo: team appearance/category enum (unknown). |
| 0x06 | subj==1: (`ctx+0x70`==1 ? 0x601 : 0x602); otherwise 0x600 | `ctx+0x70` (env +0x20), `ctx+0x5204` | INF lo: away subject × environment flag (env+0x20 == 1). |
| 0x07 | `ctx+0x5208` < 0 ? 0x700 : 0x701 | float `ctx+0x5208` | OBS: event x from the event descriptor (`FUN_14047c500`: desc+4) or `FUN_1404b6400`. INF hi: which half of the pitch the event is in (x sign). |
| 0x08 | `ctx+0x5210` < 0 ? 0x801 : 0x800 | float `ctx+0x5210` | OBS: event z (desc+0xc). INF hi: which touchline side of the pitch (z sign). |
| 0x09 | (`ctx+0x51f8` & ~2)==0 ? 0x900 : 0x901 | int `ctx+0x51f8` | OBS: from `period = *(int*)(G+0x270+0x1168)`: {0,1,2}→0, {3,4}→1, 5→2, 7→(x+0x1554!=7)+2, 8→3, 9→4, other→5. INF med: 0x51f8 = **match phase** (0 1st half, 1 2nd half, 2 ET-1, 3 ET-2, 4 PK). 0x900 = first half of normal time or of extra time; 0x901 = otherwise. |
| 0x0a | subj==1: [0xa02 if `ctx+0x70`!=1], 0xa01; otherwise 0xa00 | `ctx+0x5204`, `ctx+0x70` | INF lo: as for 0x06. |
| 0x0b | `ctx+0x70`==1 ? 0xb00 : 0xb01 | int `ctx+0x70` (env +0x20) | OBS: copied from the environment (`FUN_1414c07b0`). INF lo: environment flag (unknown). |
| 0x0c | fc=`ctx+0x51fc`, fd=`ctx+0x51fd`: (0,0)→0xc00 · (0,1)→0xc03 · (1,0)→0xc02 · (1,1)→0xc01; then 0xc04 | bytes `0x51fc/0x51fd` | Same flag as 0x03, home/away absolute. |
| 0x0d | `tblCat[cat]&1` ? 0xd01 : 0xd00 | `param_4` → table @0x1437f5d58 | OBS: depends only on the category. Using the tbl.py numbering (INF med): cat 1→d00, 2→d00, 3→d01, 4→d01, 5→d01, 6→d00, 7→d00, 8→d01, 9→d00. |
| 0x0e | `tblCat%3`: 1→0xe01, 2→0xe02, else 0xe00 | same | cat 1 e00, 2 e02, 3 e01, 4 e00, 5 e00, 6 e00, 7 e00, 8 e02, 9 e01 |
| 0x0f | `tblCat&3`: 1→0xf01, 2→0xf02, 3→0xf03, 0→0xf00 | same | cat 1 f02, 2 f02, 3 f03, 4 f03, 5 f03, 6 f02, 7 f00, 8 f03, 9 f02 |
| 0x10 | byte `ctx+0x5200` ? 0x1001 : 0x1000 | `ctx+0x5200` | OBS: the constructor `FUN_141f4c950` sets it to 1. No other writer was found (displacement and coverage scans of the module, all callers of `FUN_141f500d0`, and 0x140470000-0x1404c0000). UNKNOWN meaning; default gives 0x1001. |
| 0x11 | byte `P(0,0)+0x44` (ctx+0x13c) ==0 ? 0x1101 : 0x1100 | player-slot flag | See P+0x44 below (home slot 0). |
| 0x12 | int `ctx+0x54` ? 0x1201 : 0x1200 | env +0x04 | OBS: mode demo 0x90001 forces it to 1. INF med: time of day (day / night). |
| 0x13 | int `ctx+0x58`: 0→0x1300, 2→0x1302, else 0x1301 | env +0x08 | OBS: 0x90001 forces 1; 0x90002..b force 0. INF lo-med: weather or season (3 states). |
| 0x14 | byte `P(subj,0)+0x44` ==0 ? 0x1401 : 0x1400 | slot flag | Subject side, slot 0. |
| 0x15 | byte `P(opp,0)+0x44` ==0 ? 0x1501 : 0x1500 | slot flag | Opponent side, slot 0. |
| 0x16 | `0x1600 \| (tblCat[cat]&7)` (exactly one value) | table | cat 1 1606, 2 1602, 3 1607, 4 1603, 5 1607, 6 1606, 7 1604, 8 1603, 9 1602 |
| 0x17 | int `ctx+0x5214`==1 ? 0x1701 : 0x1700 | `ctx+0x5214` | OBS: event player side, from desc+0 (`FUN_14047c500`) or demos 0x70026/0x70027 (`FUN_141f60910`). Meaning: **event player is on the away side**. |
| 0x18 | Let Q = P(ctx+0x5214, byte ctx+0x5218). If byte Q+0x00 ≠ 0: **0x1800 only**. Else with pos = byte Q+0x10: pos<4 or pos>12 → 0x1801; 4..8 → 0x1802; 9..12 → 0x1803. Then 0x1804. | event player slot | OBS: Q+0x10 = playerInfo+0x08 (0xf0-B copy in `FUN_141f52ad0`). Q+0 is set for starters whose `teamObj+0x4c00+i*4` is 0, and for subs whose position is 0. INF hi: Q+0 = **is goalkeeper**; pos = **registered position** (0 GK, 1-3 DF, 4-8 MF, 9-12 FW), so 1801/1802/1803 = DF/MF/FW. |
| 0x19 | `0x1900 + rand100()` (exactly one) | RNG | OBS: random threshold (b1d0 keeps the first row with key ≥ accepted). |
| 0x1a | byte `ctx+0x2910` (= S(0)+0x2818) = n: [0x1a05 if n>10] [0x1a04 if n>9] [0x1a03 if n>8] [0x1a02 if n>7] [0x1a01 if n>6], then 0x1a00 if n≥6. **n<6 pushes 0x1a00 neither.** | `FUN_140a3bfc0(matchObj,0)` | OBS: counts nonzero bytes in `matchObj+0x1748+s*11`, 11 entries. INF hi: **players on the pitch, home**. Row value k means "≥ k+6 players". |
| 0x1b | same as 0x1a with byte `ctx+0x5140` (away) | `FUN_140a3bfc0(.,1)` | Players on the pitch, away. |
| 0x1c | d = (u8)\|`ctx+0x5160`\| (fabs via the 0x7fff… mask); if d==0 and `ctx+0x5164`≠0 then d=1. If d≠0: d%3==0→0x1c02, 1→0x1c00, 2→0x1c01. Then 0x1c03 (so d==0 gives only 0x1c03). | `ctx+0x5160`, `0x5164` | Absolute goal difference mod 3 (see 0x45). |
| 0x1d | Same d. If d≠0: d&3: 0→0x1d03, 1→0x1d00, 2→0x1d01, 3→0x1d02. Then 0x1d04. | same | \|goal diff\| 1/2/3/4 → 1d00/1d01/1d02/1d03 (mod 4); level → 1d04 only. |
| 0x1e | h=`ctx+0x2914`==1, a=`ctx+0x5144`==1: (h,a)=(1,1)→0x1e02; (1,0)→0x1e03; (0,1)→0x1e04; (0,0)→0x1e01. Then 0x1e00. | int `S(s)+0x281c` | OBS: `min(teamSetup+0x28,5)` (`FUN_141f51630`) or teamDB(teamId)+0x420 bits 26..28 (`FUN_141f524f0`). INF lo: 3-bit team class (1 = national team?). |
| 0x1f | [0x1f01 if `ctx+0x2914`==1] [0x1f02 if (`ctx+0x2924`>>14)==0x136b], then 0x1f00 | home team class, home team id | OBS: `S+0x282c` = team id word set by `FUN_141f52470`; id in bits 14+. INF hi: 0x2924 = home team id word. |
| 0x20 | byte `S(subj)+0x2818` = n: same ladder as 0x1a, giving 0x2005..0x2000 (nothing if n<6) | subject side players | Subject side players on the pitch. |
| 0x21 | same ladder for `S(opp)+0x2818` → 0x2105..0x2100 | opponent side | Opponent players on the pitch. |
| 0x22 | x=`ctx+0x5208`, z=`ctx+0x5210`: (x<0, z<0)→0x2201 · (x<0, z≥0)→0x2200 · (x≥0, z<0)→0x2203 · (x≥0, z≥0)→0x2202 | event position | Event pitch quadrant. |
| 0x23 | Same code as 0x44 (`FUN_141f50450(ctx,vec,1)`): base 0x2300 instead of 0x4400 (`base = flag ? 0x4400-0x2100 : 0x4400`) | home team id | See 0x44. |
| 0x24 | `ctx+0x2910`+`ctx+0x5140` < 22 ? 0x2401 : 0x2400 | player counts | INF hi: 0x2401 = someone was sent off (fewer than 22 on the pitch). |
| 0x25 | v = int `ctx+0x515c`: if v∈1..5 push 0x2500+v; then 0x2500 | `ctx+0x515c` | OBS: event descriptor int +0x14 (`FUN_14047c500`). INF lo: event sub-kind (timeup only). |
| 0x26 | byte `P(ctx+0x5214, ctx+0x5218)+0x44` ==0 ? 0x2601 : 0x2600 | event player slot flag | See P+0x44. |
| 0x27 | int `ctx+0x5168`==1 ? 0x2701 : 0x2700 | `ctx+0x5168` | Writer not found (only the reset by `FUN_141f50b90` qword @0x5164). Unused by the shipped tables. |
| 0x28..0x32, 0x33..0x3d | One value per type. Type `0x28+i` → home slot i; type `0x33+i` → away slot i (i = 0..10). Pushed value = `type<<8 \| (P(s,i)+0x44 ? 0 : 1)`. | helper `FUN_141f4ff10(ctx,vec,side,slot)` (the decompile's shared tail at `code_r0x141f4ff10`, taking (lVar5=slot, iVar3=side)); formula `side*0xb00 + 0x2801 - (flag!=0) + slot*0x100` | OBS. Mode table uses 0x28 (home GK) and 0x33 (away GK). |
| 0x3e / 0x3f | byte `ctx+0x1f2` / `ctx+0x2a22` (= P(s,0)+0xfa) ? 0x3e01/0x3f01 : 0x3e00/0x3f00 | slot 0, byte +0xfa | OBS: zeroed in `FUN_141f52ad0` (int at P+0xf8). No writer found by the displacement scans. UNKNOWN. |
| 0x40 | n = byte `S(subj)+0x2819` (ctx+0x2911+subj·0x2830): [0x4006 if n>15] [0x4005 if n>14] [0x4004 if n>13] [0x4003 if n>12] [0x4002 if n>11] [0x4001 if n>10], then 0x4000. Not gated by param_5. | `teamObj(s)+0x2eec` | OBS: `teamObj = FUN_140a489d0(G,s)` (`FUN_141f51630`). INF lo: a team-level 0..~16 rating/count. Row value k means "≥ k+10". |
| 0x41 / 0x42 | byte `ctx+0x1f3` / `ctx+0x2a23` ? 0x4101/0x4201 : 0x4100/0x4200 | slot 0, byte +0xfb | Same status as 0x3e/0x3f (UNKNOWN). |
| 0x43 | `0x4300 + rand100()` | RNG | Weighted random; accepted values 0x4300..0x4363 (b1d0 adds 1, giving 1..100). |
| 0x44 | id = `ctx+0x2924`>>14 (home team id). Push the team-specific value (if listed), then 0x4400. Listed ids: 100→0x28 · 0x65→0x17 · 0x66→0x07 · 0x67→0x02 · 0x6c→0x01 · 0x6e→0x13 · 0x70→0x1c · 0x71→0x1d · 0x77→0x09 · 0x78→0x29 · 0x79→0x10 · 0x7a→0x11 · 0x7d→0x08 · 0x7e→0x03 · 0x7f→0x2a · 0x82→0x1f · 0x83→0x20 · 0x84→0x21 · 0x87→0x24 · 0x88→0x18 · 0x89→0x0b · 0x8a→0x0e · 0x8b→0x0f · 0xac→0x06 · 0xb2→0x12 · 0xb5→0x1e · 0xb8→0x0a · 0xc5→0x1b · 0x10f→0x25 · 0x112→0x2b · 0x4b7→0x22 · 0x4c1→0x26 · 0x4c2→0x2e · 0x4d5→0x27 · 0x4d8→0x14 · 0x4dd→0x2c · 0x4df→0x04 · 0x4e0→0x05 · 0x4e2→0x2d · 0x4e6→0x0c · 0x4e7→0x0d · 0x4e8→0x15 · 0x79e→0x23 · 0x8a1→0x16 · 0x8a8→0x1a · 0x8ef→0x19 | `FUN_141f50450(ctx,vec,0)` | OBS. INF hi: per-team special cuts, such as club-specific entrances/celebrations. |
| 0x45 | g = int `ctx+0x5160`, or `ctx+0x5164` if g==0. If subj==1, g = −g. Push g>0→0x4501, g==0→0x4502, g<0→0x4503; then 0x4500. | `ctx+0x5160/0x5164` | OBS: `0x5160 = FUN_140a80ef0(match,0,0,&0x5164)` → `FUN_140a80d10` = (home − away) goals, using score stat 5 plus first-leg goals when two-legged. `0x5164` = away-goals tiebreak difference, set only when the aggregate is level and the away-goals rule bit (setup+0x6284 bit5) is set. Meaning: **subject team winning / level / losing**. |
| 0x46..0x49, 0x4a, 0x4b | Only if byte `ctx+0x521f`≠0: v = byte `ctx+0x5219+(type−0x46)`; v 0/1/2 → push type\|1, type\|2, type\|3 respectively. Then push `type<<8`. | `ctx+0x5219..0x521e`, `0x521f` | OBS: written by `FUN_1404b6400` (0x521f = its param_3; 0x5219+idx = param_7). Same function writes 0x5208/0x5210. INF lo: positional/camera query results. Only goal T3 uses them. |
| 0x4c, 0x4d, 0x4e, 0x4f, 0x50, 0x51 | byte `ctx+0x5220`, `0x5221`, `0x5222`, `0x5223`, `0x5224`, `0x5225` respectively. **Zero → type\|1; nonzero → type\|0.** One push. | same writer | OBS (`FUN_1404b6400`): 0x5220 = `FUN_1404b9890(..)`, 0x5221 = local bool, 0x5222 = `FUN_1404b9730(..)==0`, 0x5223 = float equality test, 0x5224 = obj+0x2f, 0x5225 = obj+0x130. Meaning unknown. |
| 0x52 | byte `ctx+0x5178` ? 0x5201 : 0x5200 (not gated) | `ctx+0x5178` | OBS (`FUN_141f51630`): if setup+0xc31b, then setup+0xc31c 0..3 → 1..4 (else 0); otherwise setup+0xc324 ? 2 : 0. INF med: knockout / multi-leg tie context. |
| 0x53 | [0x5301 if `ctx+0x5144`==1] [0x5302 if (`ctx+0x5154`>>14)==0x136b], then 0x5300 | away team class, away team id | Away counterpart of 0x1f. |
| 0x00, >0x53, other gaps | nothing | n/a | The column is skipped (no narrowing). |

### A. Helpers (one line each, OBS)

- `FUN_14047be40(vec,&v)`: push_back.
- `FUN_14047bca0`: vector grow.
- `FUN_141f4d0f0(ctx,vec,s)`: computes the single type-0x04/0x05 value (formula in the table).
- `FUN_141f4ff10(ctx,vec,side,slot)`: pushes `0x2800+side*0xb00+slot*0x100+(flag?0:1)`, where flag = byte `P(side,slot)+0x44`.
- `FUN_141f50450(ctx,vec,f)`: home team id → specific key (0x44 list), then the base. f=1 shifts the base to 0x2300.
- `FUN_141e55e30(c)`: returns `c < 8`.
- `FUN_141e554b0(id)`: folds a competition sub-id to its parent id (table above).
- `FUN_141f7a100(rng,n)`: `t = FUN_140a924e0(rng,0x2ac,2)`. That helper runs an LCG on `rng+0x10`: `s = (s*0xD2DD4D1D + 0x4513) & 0x7fffffff`, then `t = (int)(s * 2^-31 * 684)`. Next `st = (t + *(rng+0x20) + 0xaa) & 0x3ff`, which is stored back; the result is `st*n >> 10` (0..n−1).
- `FUN_140a3bfc0(m,s)`: counts nonzero bytes in `m+0x1748+11s .. +11`.
- `FUN_140a55ef0(list,s)`: any of 24×0x74 entries valid, active and belonging to side s.
- `FUN_140a80ef0/FUN_140a80d10`: goal difference (home−away, includes first leg) with an away-goals out-param.

### A. ctx field map (layout + setter)

Writers were found with `<scratch>/CutsceneRE/cond/scan.py`, plus coverage scans of fixdemo module 0x141f00000-0x141f80000, 0x140470000-0x1404c0000, and all 86 callers of `FUN_141f500d0`.

| field | meaning | setter | tag |
|---|---|---|---|
| 0x50..0xef | Environment struct copy (0xa0 B) from `world+0x35960+0x70`. +0 stadium id, +4 time of day?, +8 weather/season?, +0x14 (compared with 0x21), +0x20 flag | `FUN_141f51630` (via `FUN_1414c07b0`); overridden for mode demos by `FUN_141f18b70` | OBS copy; INF meanings (stadium hi, others lo) |
| S(s)=0xf8+s·0x2830 | Side block: 40 player slots × 0x100, then a tail at +0x2800 | constructor `FUN_141f4c950` | OBS |
| P+0x00 | Is goalkeeper (starter with formation pos 0, or sub with position 0) | `FUN_141f52ad0` | OBS write / INF hi meaning |
| P+0x01 | Slot == teamObj captain-ish index (`FUN_1408c5020(teamObj+0x8e08)`) | `FUN_141f52ad0` | OBS / INF med "captain" |
| P+0x08..0xf7 | 0xf0-B player info copy (`FUN_141b59e20`). P+0x10 = registered position; P+0x44 = info+0x3c flag | `FUN_141f52ad0` | OBS copy; P+0x44 meaning UNKNOWN |
| P+0xf8..0xfb | int zeroed. Bytes +0xfa/+0xfb read by types 0x3e/0x3f/0x41/0x42 | `FUN_141f52ad0` (zero only) | writer UNKNOWN |
| P+0xf9 | Match player status bit3 (`match+0x40+idx*0xd8 +0xd4`) | `FUN_141f52ad0` | OBS |
| S+0x2818 (0x2910/0x5140) | Players on the pitch | `FUN_141f51630` → `FUN_140a3bfc0` | OBS / INF hi |
| S+0x2819 (0x2911/0x5141) | teamObj+0x2eec byte | `FUN_141f51630` | OBS / meaning UNKNOWN |
| S+0x281c (0x2914/0x5144) | Team class 0..5/0..7 | `FUN_141f51630` (min(teamSetup+0x28,5)), `FUN_141f524f0` (teamDB+0x420 bits 26-28) | OBS / INF lo |
| S+0x2820/0x2824/0x2825 (0x2918/0x291c/0x291d) | teamSetup+0x2e / +0x30 bit4 / bit5 | `FUN_141f51630` | OBS / meaning UNKNOWN |
| S+0x282c (0x2924/0x5154) | Team id word (id = value>>14) | `FUN_141f52470` (called from `FUN_141f51630` with `FUN_140a7f1e0(setup,s)`) | OBS / INF hi |
| 0x5160 | Goal difference home−away (aggregate) | `FUN_141f514d0` | OBS |
| 0x5164 | Away-goals tiebreak difference | `FUN_141f514d0` (out-param) | OBS |
| 0x515c | Event descriptor +0x14 | `FUN_14047c500` | OBS / meaning UNKNOWN |
| 0x5168, 0x5170 | Only the reset (`FUN_141f50b90`) found | n/a | 0x5170 INF med = raw competition id |
| 0x5174 | Parent competition id | `FUN_141f51630` | OBS / INF hi |
| 0x5178 | Tie context 0..4 | `FUN_141f51630` | OBS / INF med |
| 0x5179 | Stage/round 1..11 | `FUN_141f51630` | OBS / INF med |
| 0x5184/0x5188 | setup+0x30 (fixture id?) | `FUN_141f51630`, `FUN_140e9c630` | OBS / INF lo |
| 0x51f8 | Match phase | `FUN_141f51630` (from `G+0x270+0x1168`) | OBS / INF med |
| 0x51fc/0x51fd | Per-side entry flag | `FUN_141f51630` → `FUN_140a55ef0(G+0x180,s)` | OBS / meaning UNKNOWN |
| 0x5200 | Byte, constructor sets 1 | `FUN_141f4c950` | UNKNOWN |
| 0x5204 | Subject side | `FUN_14047c500` (desc+0), `FUN_140478190`, `FUN_141f5a8b0` | OBS |
| 0x5208/0x520c/0x5210 | Event position x/y/z | `FUN_14047c500` (desc+4..0xc), `FUN_1404b6400` | OBS / INF hi |
| 0x5214/0x5218 | Event player side / slot | `FUN_14047c500` (desc+0, desc+0x10 byte), `FUN_141f60910` (demos 0x70026/0x70027) | OBS |
| 0x5219..0x5225 | Query results (types 0x46..0x51) | `FUN_1404b6400` | OBS / meaning UNKNOWN |
| 0x5228 | Init mode: 2 = from match (`FUN_141f514d0`), 0 = `FUN_141f51ed0`, 1 = `FUN_141f51f60`. Selects the RNG instance | as listed | OBS |

`G` = `*(0x1436f3dc8)`: game/match manager. `setup` = `G+0x2ad0` match setup; team blocks at setup+0xc400+s·0x2804. `G+0x270+0x1168` = match period.

### A. Killed or corrected hypotheses

- "0x43 accepted = 0x4300+rand(1..100)" is wrong. `rand100()` returns 0..99, so the accepted values are 0x4300..0x4363; FUN_141f4b1d0 adds 1 via `-0x42ff`.
- "Empty or unmatched list filters every row" is wrong. The column is simply not narrowed.
- `param_4` is not a per-table constant id inside the data. It is the demo category byte. The goal table still contains d00/d01, e00..e02 and f00..f03 rows. Either several categories share a table [INF med], or the non-matching rows act as don't-care fallbacks.

### A. Remaining unknowns

- Meanings of: P+0x44 (types 0x11/0x14/0x15/0x26/0x28..0x3d), P+0xfa/0xfb writer, env +0x08/+0x14/+0x20, 0x2911, 0x2914 semantics, 0x2918/0x291c/0x291d, 0x515c, 0x5168, 0x5200, 0x51fc list semantics.
- The setter cap was reached at 18 decompiles. Next step: decompile `FUN_141b59e20` (player info layout, info+0x3c), `FUN_1414c0b40`, the `world+0x35960` owner (env struct), and `FUN_140a55d40/140a55ef0`'s list owner `G+0x180`.

---

## Appendix B: reproducing

1. Re-extract the fdc corpus. The `.fdc` extractions were deleted to keep tmpfs small; `fdclist.txt` lists the paths.
   ```
   cat fdclist.txt | xargs -P12 -I{} sh -c 'pesfmt_dump cat "{}" --pes21-root "$PES21_ROOT" > "fdc/$(echo {} | sed "s#common/demo/fixdemo/##; s#/#__#g")"'
   ```
2. Decode the tables: `python3 tbl.py fdc/*table_*.bin`. The decoded `*.json` files are kept.
3. Decode one cut file: `python3 fdcdec.py fdc/goal__cut_data__goal_celebrate_0178_base.fdc`.
4. Corpus statistics:
   - `python3 fdcsurvey.py` (types and sizes);
   - `python3 recstats.py <type> <size>` (per-field distributions);
   - `python3 regparse.py` (reflection field lists from `dec/141f6*.c` and `dec/141f7*.c`).
5. Decompile more functions: `bin/dec.py <outdir> <va>…`. It uses a lock-serialised headless Ghidra copy at `<scratch>/CutsceneRE/ghidra`.
6. Find references:
   - `python3 <scratch>/callers.py <va>` for call sites;
   - `bin/lea.py <va>` for RIP-relative references;
   - `bin/vt.py <RTTI-substring>` for vtables.
