# PES 2021 cutscene / replay camera data

`Match::UpdateIngameCamera` currently carries this comment:

> pre-kickoff cutscene: a slow orbit around the centre spot [...] (PES ships no
> camera data, so the camerawork is ours)

That is wrong. PES 2021 ships **1 274 880 frames of hand-authored camera
animation** — 11 hours 48 minutes at 30 fps — as per-frame position,
quaternion and field-of-view streams. This document says exactly where it is,
how it is laid out, and how to put it on GF's camera.

Tools: `tools/pes21_import/camera_cut.py` (the `.fdc` / `.canm` parser) and
`tools/pes21_import/camera_pickup.py` (the Mbinfo pickup windows).

---

## 1. Where it lives

Everything is in **one** cpk:

```
PES21/Data/dt12_g4.cpk
  common/demo/fixdemo/<category>/cut_data/*.fdc          4804 files
  common/demo/fixdemo/<category>/table_<category>.bin       9 files
  common/demo/anime/FoxAnim/FixDemo/Animations/*.gani    5484 files (actors, not cameras)
```

`<category>` is one of `change end ent foul goal mode pk result timeup`.
Konami's vocabulary: a **cut** is one camera shot, `.fdc` = *fix demo cut*
table, `.canm` = *camera animation*. Camera streams are embedded **inside** the
`.fdc` files — there are no standalone `.canm` files in any cpk, which is why
earlier greps for `camera` came up empty.

Extract with the existing tool:

```
python3 tools/pes21_import/cpk.py "PES21/Data/dt12_g4.cpk" /tmp/fdc --filter=fixdemo
python3 tools/pes21_import/camera_cut.py --gf /tmp/fdc/common/demo/fixdemo/pk/cut_data/pk_00_intro_spider00.fdc
```

A secondary, unrelated table lives in `PES21/Data/dt13_all.cpk`:
`common/anime/Mbinfo/bin/CameraPickupInfo.bin` (section 7).

### Coverage survey

| category | .fdc files | with camera | .canm streams | camera cuts | camera frames |
|---|---|---|---|---|---|
| `change` (substitutions) | 192 | 78 | 94 | 444 | 16 175 |
| `end` (final whistle) | 526 | 142 | 189 | 261 | 50 547 |
| `ent` (entrances, line-ups, anthems) | 962 | 420 | 1 906 | 2 455 | 407 036 |
| `foul` (cards, injuries) | 78 | 2 | 2 | 101 | 362 |
| `goal` (goals + celebrations) | 1 691 | 733 | 2 015 | 4 139 | 515 278 |
| `mode` (menu / award / card-pack scenes) | 923 | 256 | 509 | 689 | 173 973 |
| `pk` (penalty shootouts) | 128 | 21 | 28 | 327 | 3 658 |
| `result` (post-match, trophy) | 106 | 65 | 337 | 374 | 101 437 |
| `timeup` (half/full time) | 198 | 9 | 24 | 241 | 6 414 |
| **total** | **4 804** | **1 726** | **5 104** | **9 031** | **1 274 880** |

All 4 804 files parse with zero errors and every one of the 5 104 streams has an
identical channel signature (section 4). 311 cut files are generic (`st000`),
the rest are stadium-specific (`st002` x62, `st014` x55, `st011` x51, `st016`
x49, `st007` x46, `st062` x46 ...). Longest single shot: 7 200 frames = 240 s
(`result_002_cam_mid.fdc` -> `ent_001_result_st000_cam0030.canm`). Most common
lengths: 90, 180, 300, 150, 240, 420 frames.

---

## 2. The FDC container

A generic, nestable Fox-style container. All offsets are relative to the
container's own slice.

```
u32  entryCount
entryCount x {
    u32 dataOffset      # container-relative; entries tile the buffer contiguously,
    u32 dataSize        # first one starting immediately after this table
    u32 nameOffset      # container-relative, into the name pool
}
<entry data>
<name pool: NUL-terminated names>
```

The contiguity rule is strict, which makes container-vs-leaf sniffing reliable
(`read_container` returns `None` on any gap or overlap).

Every `.fdc` root holds:

* exactly **one nested container** whose name is the file's own cpk path
  (`cpk_dat/common/demo/fixdemo/goal/cut_data/foo.fdc`) — the **cut table**;
* **zero or more CANM blobs**, magic `01 00 01 ff`, named
  `<category>/canm/<clip>NNNN.canm`;
* leaf blobs named `*.gani` / `*.seq` / `*.ask` — actor animation payloads,
  frequently 4-byte stubs that just reference
  `common/demo/anime/FoxAnim/FixDemo/Animations/`.

Files carry 0–16 camera streams each: 3 078 have none (pure actor cuts), 503
have one, 507 have two, 286 have three, and a long tail up to 16.

---

## 3. Cut-table records

Inside the cut table, each entry's *name* is a **single binary byte** — the
record type — and the type fixes the record size exactly. Verified across all
4 804 files with no exceptions:

| tag | size | count | meaning (from the name field at `+0x0c`) |
|---|---|---|---|
| `0x00` | `0x040` | 2 389 | sequence header, one per table (name is the empty string) |
| `0x01` | `0x09c` | 1 399 | scene object ref (`*.xml` under `common/demo/fixdemoobj/`) |
| `0x02` | `0x05c` | 3 447 | ? |
| `0x03` | `0x0bc` | 5 850 | ? |
| `0x04` | `0x16c` | 6 274 | actor animation cut (`*.gani`) — **decoded, section 3.1** |
| `0x05` | `0x1bc` | 4 973 | model / skeleton ref (`*.fpk`, `*.skl`) |
| **`0x06`** | **`0x11c`** | **9 031** | **camera cut (`*.canm`)** |
| `0x07` | `0x144` | 626 | ? |
| `0x08` | `0x060` | 3 | ? |

### Camera cut record, tag `0x06`, 284 bytes

| offset | type | meaning |
|---|---|---|
| `+0x00` | u32 | **start frame** in the demo timeline. Records ascend, typically in `0/10/100/110/200/210/...` pairs |
| `+0x04` | f32 | unknown; `0.0` in 5 430 of 9 031 records, elsewhere 120 / 150 / 180 or multiples of 840 that step up across consecutive cuts |
| `+0x08` | u16 | `0xffff` in every observed record |
| `+0x0a` | u16 | unknown; `0` in 8 415 records |
| `+0x0c` | char[] | **canm name**, NUL-terminated, zero-filled to `+0x90`. 6 620 of 9 031 records name a clip; empty means the cut has no camera of its own |
| `+0x90` | f32 | **near clip, metres** — *overrides* the clip's own channel 3. Differs from the clip in 5 960 of 6 620 cases |
| `+0x94` | f32 | **far clip, metres** — overrides channel 4 |
| `+0x98` | f32 | `1.0` (8 711), else `0.5` / `0.75` / `0.25`; looks like a weight |
| `+0xb0` | u8 | small enum: `4` (7 900), `6` (609), `5` (518) |
| `+0xb4` | u32 | unknown: `50` (6 718), `90` (1 519), `95`, `80`, `60`... It equals the clip's frame count in only 17 records, so it is **not** the shot duration |
| `+0xb8`..`+0xd8` | u32[] | `0xffffffff`-filled, unused slots |
| `+0xd8`..`+0xf0` | u8[24] | values in `{0, 50, 100}` — per-slot blend/visibility percentages |
| `+0xf0` | f32[11] | `{0,0,0, 2.8, 2.8, aspect..., 0.0, 30.0}`; the last element is the frame rate |

The cut table is therefore the **shot list**: it says at which demo frame each
shot starts, which clip it plays, and what clipping planes to use. Several cuts
can reference the same clip (a cut back to an earlier camera), and clips may be
referenced out of order.

### 3.1 Actor cut record, tag `0x04`, 364 bytes — the `_pl` player packs

The `*_pl*.fdc` variants of a family (`ent_020_order01_pl.fdc`,
`ent_009_st000_south_pl_home.fdc`, ...) hold no cameras: their cut table is a
list of tag-`0x04` records, one per **actor slot**, staging the players of the
entrance/warm-up/anthem scene. Parsed by `camera_cut.ActorCut`; verified
across the `ent` category (all 109 actor records of the `ent_020` /
`ent_007_passage01` / `ent_009_st000` packs decode to on-pitch placements):

| offset | type | meaning |
|---|---|---|
| `+0x00` | u16 | **actor slot**: 0–10 home XI (0 = keeper), 11–21 away XI, 22–24 officials |
| `+0x02` | u16 | `1` in every observed record |
| `+0x04` | f32[3] | **spawn position** `(x, y, z)`, Fox metres, Y up, centre-spot origin |
| `+0x10` | f32 | **spawn yaw, degrees** about +Y; 0 faces +Z, 90 faces +X (the `ent_020_order02` keeper at `(-40, 0, 0)` yaw 90 faces the centre spot) |
| `+0x14` | char[0x80] | **gani path** (`.../FoxAnim/FixDemo/Animations/dml_ent_*.gani`). The fdc carries the same path as a 4-byte stub leaf; the real gani ships loose in dt12 under that path (5 484 files, standard GZ ganis — same decoder as the mtar bodies, frame = 1/59.94 s) |
| `+0x94` | char[0x80] | **seq path**, same stem. The `.seq` leaves *do* have content (200–1100 bytes): u16 frame windows and sync markers — an event table, **not** motion |
| `+0x114` | f32 | **phase offset into the clip, gani ticks.** The clip loops over the demo; 107 of 109 offsets are less than the clip length, the rest wrap (980 on a 931-frame clip), and one is negative (−60) |
| `+0x118` | u32 | flags: 0 / 1 / 256 observed |
| `+0x120` | u8[] | small flag bytes (`01 04 0a 00 ...`) |

**Where the walk is.** The clips are near-in-place: over all 375 `dml_ent_*`
ganis the RIG_ROOT translation spans at most **2.9 m** (`walk` clips carry a
metre or two of genuine shuffle, `warmup`/`idle`/`anth` clips less). PES
stages an entrance as *placements plus near-in-place clips*, re-placing the
actors between packs (tunnel → walk-on → line-up) and hiding every reposition
behind a camera cut. There is no long baked walk path anywhere in the data —
the "walk-out" is spawn transforms composed with a few metres of authored
root motion per shot.

Example — `ent_020_order01_pl.fdc` (the family the engine's smoke config
plays): 22 records, slots 0–21, both XIs scattered around their own halves in
`dml_ent_kickoff*_warmup/stretch/jog` clips with staggered phase offsets so
nobody claps in sync; `ent_020_order02_pl.fdc` stages the same 22 with the
keepers on their goal lines and a passer/receiver pair
(`kickoff02_center06_passer/receiver`, both phase 30) knocking a ball about
the centre spot.

### 3.2 Exporting and playing the choreography

`tools/pes21_import/entrance_pl.py` converts a family's `_pl` packs to open
text formats next to the exported camerawork:

```
python3 entrance_pl.py <cut_data-dir> <FixDemo/Animations-dir> \
        data/media/cutscenes/ent --ids 020
  data/media/cutscenes/ent/020/ent_020_order01_pl.chor   (per-slot root tracks)
  data/media/cutscenes/ent/020/anims/dml_ent_*.anim      (in-place clips)
```

Each clip is converted with the root motion **stripped** (`gani_to_anim.py
--strip-root`: the RIG_ROOT yaw+translation are removed, any root tilt
kept), and the `.chor` carries per slot the spawn transform composed
with the clip's own root motion as a baked world-space track — GF space,
10 ms frames, one clip cycle, phase pre-applied — plus the phase for the clip
frame itself. Engine side (`src/utils/entrancechoreo.cpp`,
`Match::UpdateEntranceChoreo`, `HumanoidBase::SetChoreoPose`): while the
entrance runs, cast players are driven kinematically — the fed world
transform goes into `animApplyBuffer.position/orientation` with the in-place
clip on `noPos`, exactly the seams `Animation::Apply` already has — and when
the feed stops the humanoid hands control back through `ResetPosition` from
wherever the choreography left it. Players without a slot (and, for now, the
officials' slots 22+) keep the scripted walk fallback.

---

## 4. CANM — the per-frame camera stream

```
0x00  u16  version      always 1
0x02  u16  magic        0xff01
0x04  u32  frameCount   last frame index; keyCount == frameCount + 1
0x08  u32  frameRate    always 30
0x0c  u32  trackCount   always 1
0x10  u32  trackOffset  always 0x18
0x14  u32  pad

track @ trackOffset:
  u16  channelCount     always 6
  u16  0xff01
  u32  pad
  u32  channelOffsets[channelCount]    # relative to trackOffset

channel:
  +0x00  u32  type            1 = quaternion, 4 = vector4, 8 = float
  +0x04  u16  keyCount
  +0x06  u16  channelIndex
  +0x08  u32  timesOffset     channel-relative -> u16 frame index per key
  +0x0c  u32  valuesOffset    channel-relative -> 4 floats (type 1/4) or 1 float (type 8)
```

Values are **raw little-endian float32** — no quantisation, unlike `.gani`
(`gani.py`), which bit-packs its curves. The last channel's value block is
padded up to a 16-byte boundary. Keys are dense: every frame `0..frameCount` is
present in every stream observed, so no interpolation is strictly needed, but
`camera_cut.py` slerps rotations and lerps the rest anyway.

All 5 104 streams share one channel signature,
`0:1/16 | 1:4/16 | 2:8/4 | 3:8/4 | 4:8/4 | 5:8/16`:

| idx | type | meaning |
|---|---|---|
| 0 | quat | **rotation** `(x, y, z, w)`, always *exactly* unit length (min and max norm both 1.00000 over 1.27 M keys) |
| 1 | vec4 | **position** `(x, y, z, 1.0)` in **metres** |
| 2 | float | **vertical field of view, degrees** |
| 3 | float | near clip, metres (single key) |
| 4 | float | far clip, metres (single key) |
| 5 | float | aspect ratio (single key) |

Note the source data freely flips quaternion sign between adjacent frames
(`q` and `-q` are the same rotation); use a dot-product-aware slerp.

### Coordinate system

Right-handed, **Y up**, **origin at the pitch centre spot**, **metres**, and
the camera looks down its local **-Z** with **+Y** up (the usual Maya/OpenGL
eye basis).

The units were pinned down by the penalty-shootout cameras in
`pk_01_intro_setBall00_cam01.fdc`, which sit at literally exact pitch
landmarks:

* `(52.500, 1.000, 7.000)` — dead on the goal line, half-length 52.5 m of a
  105 m pitch, camera 1 m off the ground;
* `(-55.550, 1.000, 0.600)` — 3.05 m behind the *other* goal line, i.e. the
  depth of a goal, essentially on the centre axis.

X runs along the pitch length (goal lines at +-52.5), Z along the width
(sidelines at +-34).

### Lens values

FOV is a **vertical** angle in degrees, ranging 0.13 .. 94.98 over the corpus.
The hot values are exactly Maya vertical FOVs for a 24 mm (0.945") vertical
film aperture, `FOV = 2*atan(12 / focal_mm)` — the match is exact to two
decimals:

| FOV (deg) | keys | implied focal |
|---|---|---|
| 46.40 | 75 491 | 28 mm |
| 41.11 | 54 745 | 32 mm |
| 43.60 | 50 830 | 30 mm |
| 53.13 | 43 931 | 24 mm |
| 51.28 | 42 422 | 25 mm |
| 22.62 | 38 196 | 60 mm |
| 37.85 | 36 319 | 35 mm |
| 57.22 | 36 085 | 22 mm |
| 26.99 | 33 167 | 50 mm |
| 49.55 | 25 265 | 26 mm |

Aspect is `1.49995` (4 194 streams) or `1.5` (907) = 36 mm / 24 mm. So these
are **Maya camera bakes at 3:2**, which several filenames confirm outright
(`*_mayaL0x`, `*_mayaL1x`, 206 and 128 files respectively).

Near clip: `1.0` (2 214), `0.005` (1 057), `0.01` (552), `0.5` (372),
`0.001` (312), `0.1` (295). Far clip: `400` (2 154), `200` (1 222), `1000`
(563), `100` (496), `300` (323), `250` (114). Remember the cut record at
`+0x90`/`+0x94` usually overrides both.

### Position envelope per category

| category | X | Y (height) | Z |
|---|---|---|---|
| `goal` | -132.0 .. 132.9 | -0.4 .. 65.0 | -95.9 .. 110.0 |
| `change` | -77.7 .. 75.8 | 0.3 .. 23.9 | -86.6 .. 80.9 |
| `pk` | -55.5 .. 52.5 | 1.0 .. 15.0 | -28.0 .. 32.0 |
| `ent` | -391.7 .. 1122.3 | -1.6 .. 450.0 | -440.7 .. 871.1 |
| `mode` | -11.3 .. 30.0 | -2.4 .. 4.3 | -11.6 .. 30.0 |
| `result` | -391.7 .. 1122.3 | 0.2 .. 450.0 | -440.7 .. 871.1 |
| `end` | -76.6 .. 94.7 | 0.0 .. 29.9 | -44.8 .. 51.8 |
| `timeup` | -9.6 .. 7.2 | 1.2 .. 5.9 | -12.0 .. 40.9 |
| `foul` | -4.4 | 3.5 | 3.1 |

The wild `ent` / `result` extremes are all `*aerial*` files — helicopter
establishing shots up to 450 m altitude and several hundred metres out
(`ent_001_aerial_st007_cam_nf.fdc` starts at `(359.4, 150.0, 171.2)`).
`mode` shots are menu/card-pack scenes staged near the origin, not on a pitch.

---

## 5. Worked examples

### 5.1 A pitchside broadcast camera that pans and zooms

`goal/cut_data/goal_A_celebrate_0229_LM.fdc`, 117 909 bytes, 3 streams,
6 camera cuts:

```
   -- cut timeline (tag 0x06 records) --
      start     0  kind 4  near 0.5  far 400   (no clip)
      start    10  kind 4  near 0.2  far 400   goal/canm/goal_A_celebrate_0229_cam_00.canm
      start    11  kind 4  near 0.5  far 400   goal/canm/goal_A_celebrate_0229_cam_01.canm
      start    20  kind 4  near 0.5  far 400   goal/canm/goal_A_celebrate_0229_cam_02.canm
      start    30  kind 4  near 0.2  far 400   goal/canm/goal_A_celebrate_0229_cam_00.canm
      start    31  kind 4  near 0.5  far 400   goal/canm/goal_A_celebrate_0229_cam_01.canm

   -- goal/canm/goal_A_celebrate_0229_cam_00.canm --
      480 frames @ 30 fps (16.00 s), 481 keys, near 1 far 100 aspect 1.5
      frame  position (m, Fox Y-up)        quaternion x y z w                fov
      0      (  34.500,   1.000,  37.000)  (-0.003,-0.238, 0.001,-0.971)  19.46
      120    (  34.500,   1.000,  37.000)  (-0.012, 0.096,-0.001,-0.995)  21.58
      240    (  34.500,   1.000,  37.000)  ( 0.030, 0.073, 0.002,-0.997)  43.60
      360    (  34.500,   1.000,  37.000)  (-0.030, 0.014,-0.000,-0.999)  38.13
      480    (  34.500,   1.000,  37.000)  (-0.015,-0.120, 0.002,-0.993)  17.06
```

The position never moves: it is a camera operator on a tripod at Z = 37 m —
3 m outside the 34 m sideline — 1 m off the ground, at X = 34.5 m towards one
goal. The rotation pans, and the FOV racks 19.5 deg (a 70 mm long lens) out to
43.6 deg (30 mm wide) and back in to 17.1 deg. That is a real TV camera move,
not a mathematical orbit, and it is exactly the kind of thing GF cannot
synthesise.

### 5.2 Spidercam

`pk/cut_data/pk_00_intro_spider00.fdc` — the shot is even named after the rig:

```
   -- cut timeline --
      start 0  kind 6  near 5  far 300   pk/canm/pk_00_intro_spider_00.canm

   -- pk/canm/pk_00_intro_spider_00.canm --
      120 frames @ 30 fps (4.00 s), 121 keys, near 1 far 100 aspect 1.5
      frame  position (m, Fox Y-up)        quaternion x y z w               fov   ground hit
      0      ( -15.000,   8.000,  -0.000)  (-0.142, 0.693, 0.142, 0.693)  43.60  (-33.7, 0.0) at 20.3 m
      30     ( -17.240,   6.771,   0.096)  ( 0.130,-0.694,-0.122,-0.698)  43.60  (-35.4, 0.0) at 19.4 m
      60     ( -19.769,   6.186,   0.001)  (-0.123, 0.698, 0.124, 0.695)  43.60  (-36.7, 0.0) at 18.0 m
      120    ( -25.000,   6.000,  -0.000)  (-0.148, 0.694, 0.149, 0.689)  43.60  (-38.3, 0.1) at 14.6 m
```

A cable camera descending from 8 m to 6 m while tracking 10 m down the centre
axis (Z stays ~0) towards the penalty area, always aimed at the ground near
X = -34 .. -38, i.e. the penalty spot / goalmouth. Note the quaternion sign
flipping frame to frame — same rotation, opposite representative.

### 5.3 Telephoto zoom on the goal line

`pk/cut_data/pk_01_intro_setBall00_cam01.fdc`, stream `..._04.canm`: a static
camera at `(52.500, 1.000, 7.000)` — on the goal line — looking down the pitch
with FOV ramping `0.55 -> 1.38` deg over 240 frames. A 0.55 deg vertical FOV is
a ~2 500 mm lens: this is the extreme long-lens shot down the length of the
pitch used in shootout intros. Its sibling `..._03.canm` sits at
`(-55.550, 1.000, 0.600)`, 3.05 m behind the opposite goal line, racking
4.58 -> 11.42 deg.

### 5.4 A multi-camera aerial sequence

`ent/cut_data/ent_001_aerial_st002_cam_nf.fdc` (93 268 bytes) holds 5 streams
(`cam0000..cam0003`, `cam0005` — `cam0004` is absent) and 10 camera cuts that
alternate between them at frames 0/10, 100/110, 200/210, 300/310, 400/410, each
420 frames long. Its cut records override near/far to `10.0 / 8500.0`, versus
the clips' own values — an 8.5 km far plane for a shot that frames the whole
stadium and skyline. Sample from `cam0002`:

```
   f0    pos=(-177.746, 210.071, 318.661) fov=41.11 fwd=( 0.412,-0.513,-0.753) ground hit (-9.2, 10.2)
   f210  pos=(-129.191, 187.061, 292.390) fov=41.11 fwd=( 0.342,-0.515,-0.786) ground hit (-5.0,  7.2)
   f420  pos=( -80.636, 164.051, 266.119) fov=41.11 fwd=( 0.239,-0.514,-0.824) ground hit (-4.3,  2.8)
```

A helicopter descending from 210 m to 164 m while closing on the stadium, the
view ray staying locked near the centre circle throughout.

---

## 6. Mapping onto GameplayFootball's camera

GF's camera is **right-handed, Z up, metres, pitch centred at the origin**, and
— crucially — it also looks down its local **-Z** with **+Y** up. That was
verified against the existing pre-kickoff orbit in
`Match::UpdateIngameCamera` (`src/onthepitch/match.cpp:1131`): at `a = 0` the
camera sits at `(0, -42, 16)` with `cameraOrientation = Rx(69.15 deg)`, whose
`-Z` forward is `(0, 0.934, -0.356)`, and that ray meets `z = 0` at exactly
`(0, 0, 0)` — the centre spot. Same eye basis as Fox; only the **world** basis
differs.

### 6.1 Basis change

A single +90 deg rotation about X:

```
X_gf = X_fox        Y_gf = -Z_fox        Z_gf = Y_fox
```

as a quaternion, `q_x90 = (sin45, 0, 0, cos45) = (0.7071, 0, 0, 0.7071)`,
applied on the left to **both** position and rotation:

```
p_gf = q_x90 * p_fox
q_gf = q_x90 * q_fox
```

`camera_cut.to_gf_position()` / `to_gf_quaternion()` / `to_gf()` do this;
`camera_cut.py --gf` prints both spaces side by side.

### 6.2 Feeding the values in

GF composes the camera's world rotation as
`cameraNodeOrientation * cameraOrientation` (`cameraNode->SetRotation`,
`camera->SetRotation`, with the camera's local position pinned to the origin at
`match.cpp:1693`). So the simplest wiring for a baked shot is:

```cpp
cameraNodePosition    = p_gf;                 // metres, Z up
cameraNodeOrientation = QUATERNION_IDENTITY;
cameraOrientation     = q_gf;
cameraFOV             = fov_deg;              // see 6.3
cameraNearCap         = std::max(0.1f, near); // see 6.4
cameraFarCap          = far;
```

`buf_camera*` snapshot buffers then interpolate as usual, so no other plumbing
changes.

### 6.3 FOV is a straight copy

`Matrix4::ConstructProjection` (`src/base/math/matrix4.cpp:293`) computes
`top = zNear * tan(fov * pi/360)`, i.e. GF's `cameraFOV` is the **full vertical
angle in degrees** — identical semantics to canm channel 2. Copy it across
unchanged.

The one caveat: shots were composed at **3:2**, and GF renders at the window
aspect. Because both use a *vertical* FOV, a 16:9 window simply reveals more at
the sides — usually harmless, and arguably correct. To preserve the intended
*horizontal* framing instead, use `camera_cut.gf_fov(fov, 1.5, screen_aspect)`:

```
fov_gf = 2 * atan(tan(fov_fox / 2) * 1.5 / screen_aspect)
```

### 6.4 Clipping planes

Take near/far from the **cut record** (`+0x90` / `+0x94`), not the clip, since
the record overrides in 90% of cases. Clamp near to `>= 0.1`: 1 921 streams ask
for `0.005`, `0.01` or `0.001`, and GF derives shadow/depth parameters from
`cameraNearCap` (`r3d_messages.cpp:44`), where a sub-centimetre near plane will
wreck depth precision. Far planes up to 8 500 m appear in aerial shots and are
fine to honour.

### 6.5 Pitch size and handedness

PES's pitch is 105 x 68 m (`+-52.5`, `+-34`); GF's is 110 x 72
(`pitchHalfW = 55`, `pitchHalfH = 36` in `src/gametypes.hpp:34`). Passing those
two constants to `to_gf_position()` scales X by `55/52.5 = 1.0476` and Y by
`36/34 = 1.0588`, and height by the mean of the two, so pitch landmarks line up
instead of the camera framing the wrong patch of grass. Leaving them out keeps
raw metres.

Handedness/side is already in the data: shots ship as `_fromL` / `_fromR`,
`_home` / `_away`, `_Z_fromL` / `_Z_fromR` variants, so pick the variant that
matches `teams[i]->GetSide()` rather than mirroring X yourself.

### 6.6 Recommended first target

Replace the synthetic pre-kickoff orbit with a real entrance sequence. The
`ent` category is the richest (1 906 streams, 407 036 frames) and
`ent_001_aerial_st000_*` shots are stadium-agnostic. The mechanism needed is
small: hold a shot list of `(startFrame, canm)` from the cut table, advance a
frame counter at 30 fps, sample, convert, assign. Sequencing 9 031 cuts is a
later problem; a single 420-frame aerial is a self-contained first step.

Goal celebrations (`goal`, 2 015 streams, 515 278 frames) are the biggest prize
but need the actor `.gani` playback to be worth cutting to, since the shots are
framed around specific celebration animations.

### 6.7 A goal camera is not authored in world space

The position envelope in §4 reads a `goal` track as if its numbers were places on
a pitch, and they are not. PES authors a goal camera in the *celebration's own*
space: the scorer stands at the origin and the camera is set around him. Measured
over the 516 goal tracks imported into `data/media/cutscenes/goal`, taking the
first frame of each:

| | min | p25 | median | p75 | max |
|---|---|---|---|---|---|
| distance from the origin | 1.0 | 9.6 | **12.6** | 19.2 | 80.1 |
| framed height at that distance | 0.28 | 0.95 | **1.85** | 2.40 | 37.2 |
| lens (vertical FOV, degrees) | 0.53 | 3.66 | **9.04** | 13.61 | 51.28 |
| camera height | 0.01 | 0.67 | **0.72** | 1.02 | 20.0 |

448 of the 516 aim within ten degrees of `(0, 0, 1)` — chest height on a man
standing at the origin — and the median lens frames 1.85 m at the median
distance, which is that man. The families separate cleanly too: the 27
`_Z_fromL` tracks sit at x = +40.6 and the 27 `_Z_fromR` at x = -39.1, both at
y = -18.7, and the 355 numbered `goal_celebrate_NNNN` cameras sit dead in front
at x = 0.0, y = -11.6. The camera is on the -Y side in 349 of those 355, so
local -Y is the way the scorer faces.

That explains the shot the engine used to take. Reading the frame as a world
position put every goal camera near the centre spot regardless of where the goal
was scored, and `RetargetCamTrackFrame` then re-aimed it at the scorer and opened
the lens far enough to cover him — so PES's 0.9-degree telephoto, which is right
only because its camera is a hundred metres out, became a 40-degree lens jammed
against a scorer's head.

`StageCamTrackFrame` (`src/utils/camtrack.cpp`) treats the frame as what it is: a
composition to put down. The authored offset is turned about world Z by the yaw
that lays local -Y along the scorer's facing, added to his feet, and the same turn
is applied to the rotation. Nothing else is touched — distance, lens, clip planes
and the authored camera move all survive. The yaw is taken once, when the goal
goes in (`Match::UpdateIngameCamera`), and held: re-read every frame it swings the
camera round the player as he turns.

The old `goalCamAuthoredSides` x-mirror went with it. It was there to flip a
world-space track to whichever end the goal was scored at, and in celebration
space there is no end to flip to — the yaw already puts the shot where it belongs.

Staging alone films the stand, though, because PES pans these cameras off the
origin as the shot develops. On `goal_celebrate_0303_mayaL0x` the aim is 3 degrees
off the local origin at frame 0, 31 by frame 70 and 60 by frame 105, and the lens
opens from 11 to 25 degrees with it - PES's scorer *arrives* into that shot. Ours
does not: every one of the 266 installed `pes_dml_goal_celebrate_*.anim` clips has
a root that moves at most 6 mm, so a literal replay of the pan ends up looking at
the sky over the stand, which is precisely what the first staged capture did.

So the position is staged and the aim then follows the scorer
(`RetargetCamTrackFrame` after `StageCamTrackFrame`). That is not the old re-aim of
a world position: the camera is already at PES's authored distance, so the authored
lens is wide enough to frame him on 365 684 of the library's 472 077 frames (77.5%)
and is kept untouched there; where it is widened it is by a median of 2.7 degrees.

Two caveats. The *other* incident-local categories are not the same shape: only
2 of 16 `change` tracks and neither `card` track aims at its origin, so those are
multi-actor stagings whose authored aim points at whichever actor PES framed, and
they keep the re-aim (`CutsceneViewer::Anchoring::IncidentLocal`) rather than pure
staging. And the camera follows the scorer's live position rather than the spot he
scored from, which costs nothing: the celebration clips do not travel. Over 120 of
the 266 installed `pes_dml_goal_celebrate_*.anim`, the root moves a median of 0.0 m
and at most 6 mm across the whole clip, so the staged composition holds still
exactly as PES framed it - and if a scorer is ever moved by something else, the
shot goes with him rather than letting him walk out of a nine-degree frame.

---

## 7. `CameraPickupInfo.bin` (a separate, smaller table)

In `PES21/Data/dt13_all.cpk`:

```
common/anime/Mbinfo/bin/CameraPickupInfo.bin      980 bytes, md5 8a4f48d119ea91c97f58131fdb3790bf
common/anime/Mbinfo/json/anim_infos.json     15 702 852 bytes
```

Konami ship the animation metadata table **twice**: as the packed `.bin` the
engine reads, and as plain JSON in the same cpk. The JSON's
`camera_pickup_info` member is the same data:

```json
"camera_pickup_info": [ { "start_frame": 80, "end_frame": 120 } ]
```

This is the frame window of an animation **worth putting a camera on** — the
replay/highlight director's "the interesting part happens here" marker.

* 240 of 4 907 animations carry exactly one window; none has two.
* 234 of the 240 are `dm_*` demo/reaction animations (`dm_oop_*` out-of-play,
  `dm_miss_*`, `dm_tu_*` time-up, `dm_change_*`); the rest are 4 `run_*`,
  1 `idle_*` and 1 `kick_long_*`.
* Windows run 12 .. 494 frames (0.40 .. 16.47 s at 30 fps), mean 65 frames.
* 239 of 240 lie inside the animation's own `start_frame..end_frame` span.

Examples (`python3 tools/pes21_import/camera_pickup.py <extracted-dt13-dir>`):

```
anim id  animation                             pickup frames  seconds      anim span
2077     kick_long_0_0_infront_y0_135          5..28          0.17..0.93   15..39
4020     dm_tu_walk_1_1_walk_handclap          30..120        1.00..4.00   0..204
4087     dm_miss_back_1_1_walk_head_f045       20..105        0.67..3.50   0..132
4115     dm_miss_idle_0_1_walk_bendBack_090    8..33          0.27..1.10   0..45
```

Useful for GF as a cheap replay heuristic: when a reaction animation plays, cut
to it only during its pickup window and hold for exactly that long.

### The `.bin` byte layout is *not* decoded

Recorded so nobody repeats the search. It is **not** a plain array of fixed
32-bit records, which is the obvious first guess. Established:

* 980 bytes = 245 u32 words for 240 entries = **32.67 bits per entry**. Every
  sibling table in `Mbinfo/bin` is a whole number of u32 words, so the stream
  is u32-aligned.
* The family cannot be fixed-record: `PersonalizedData.bin` holds **97 entries
  in 71 words** — fewer words than entries. So these are bit-packed streams
  with variable-width (probably delta/varint) keys. Consistent with that, no
  sibling's size divides evenly by its entry count: `RotData.bin` 7.084
  bytes/entry over 4 907 entries, `BallData.bin` 17.709,
  `DemoConnectData.bin` 4.004.
* Ruled out by exhaustive search over every contiguous bitfield (offsets 0..27,
  widths 4..19, both LE and BE word order):
  * no field reproduces the JSON animation ids — best set overlap 14 of 240;
  * no field is monotone across records, so it is not a sorted id-keyed array;
  * no field equals an entry's `start_frame` or `end_frame` positionally at any
    word alignment, so it is not a positional array either;
  * no non-overlapping `(start, end)` field pair, with or without a `/5` scale,
    reproduces even 180 of the 240 known pairs.

Cracking it would mean reading the bit-reader out of `PES2021.exe`. Since the
JSON carries the identical data, that is not worth doing for asset import.

---

## 8. Where earlier searches went wrong

For the record, so the trail is not re-walked:

* `.canm` never appears as a file in any cpk — the streams are embedded inside
  `.fdc`. Listing filenames and grepping for `canm` or `camera` finds nothing.
* Grepping cpk listings for `camera` finds only UI flow scripts
  (`MatchCamera.json`, `MenuCamera.json`, `MatchMenuSubCameraSettings.json`) —
  the user camera *settings* menus, not camera data.
* The word that actually matters is **`cut`**, as in `cut_data/`, and the
  extension is `.fdc`. `dt12_g4.cpk` is the only cpk that has them.
* The 162 `.xml` files under `common/demo/fixdemo*` are fmdl model descriptors
  for props and extras, not cut descriptions.
* `PES2021.exe` embeds the `.fdc` and `Mbinfo/bin` paths as literals but no
  field-name strings for either format, so the exe is no help on layout beyond
  confirming the file list.
