# PES 2021 cloth / soft-body systems — evidence report

Scope: shirt/pants cloth (`uniform/cloth/*`: `fcl_*_sim.fclo`, `*_cloth.skl`), goal
nets, corner flags and other physics props. Sources: static analysis of the
fully-analysed `pesmem.bin` Ghidra project (VAs are absolute, image base
0x140000000), the user's own install read through the repo's `pesfmt_dump`,
and read-only `/proc/<pid>/mem` reads of the live game (mid-match). Nothing
was ever written to the game.

Legend: **OBS** = observed (decompile / bytes / live memory, with how),
**INF** = inferred (confidence given). Killed hypotheses recorded. Verbatim
PES values appear as evidence.

Related reports: `pes-kit-assembly.md` §3/§7.3/§8.4 (cloth-vs-nocloth part
selection, `ClothShirtParts`), `pes-re-collision.md` §1 (player volumes, the
`.geom`/`.frig` file census). This report takes the cloth/net *simulation*.

---

## 0. Headline facts

1. **OBS — cloth shirts are a Verlet particle sim bound to a per-part
   skeleton.** The 3 shipped cloth files are
   `Asset/model/character/uniform/cloth/#Win/fcl_{pants,shirt_in,shirt_out}_sim.fclo`
   (the exe references them as
   `/Assets/pes16/model/character/uniform/cloth/fcl_*_sim.fclo`,
   strings at 0x258fa00 region). The magic is `"olcf"` + 12 pad bytes (a
   second accepted magic `"fclo"` with no padding is rejected as an error by
   the loader, `FUN_1423d0de0`).
2. **OBS — the `.fclo` layout is fully accounted for, byte-exact, in all three
   files** (§1).
3. **OBS — every free particle sits exactly on a `csk_*` bone of the matching
   `*_cloth.skl`** (distance ≈ 1e-12, §2). The sheet's bones are driven by
   the sim; the fmdl mesh is skinned to them.
4. **OBS — pinning is a second particle class inside the same file**: the
   header's `c1` entries duplicate specific `csk_*` rest positions (the
   boundary ring of the sheet) and carry the same bind records (§1.3).
5. **OBS — the solver is Verlet integration (`FUN_1423d0cd0`)**: particle
   record stride 0x80 (current pos / prev pos / force accumulator),
   `x' = x + (x − x_prev)·k + F·dt²`, with
   `k = (DAT_14256ae1c − obj+0x20)·dt·(obj+0xa4)` and `dt²·DAT_142565600` on
   the force term.
6. **OBS — particles are bound to the body by 4-bone linear-blend skinning**:
   the 32-byte bind records are `{4×u32 bone-palette index, 4×f32 weight}`
   (weights sum to 1), evaluated in `FUN_1423d0e60` as a weighted blend of
   four 64-byte matrix rows (§2.2).
7. **OBS — the bone palette is a StrCode32 name list in the file trailer**
   (§1.6): e.g. `0x630bb05a = strcode32("dsk_hip")`, resolved against the
   `*_cloth.skl` / body skeleton.
8. **OBS — cloth vs nocloth selection is a run-time condition, not a config:
   `uStack_1d0 = (+0xB8 == 0 && !tight && !FUN_141a80400 && !(f bit2 &&
   !bit1)) && FUN_141a7ff50 == 0`** (kit report §3, OBS). When set,
   `shirt_in`/`shirt_out` are replaced by cloth objects (`FUN_141a828b0`),
   each `ClothShirtParts` holding BOTH the nocloth model (+0x20) and the
   cloth model (+0x78); the nocloth one is the one in the draw list.
   Live §7.3 state: **all 22 players of a team carried both** during a match
   preview; the report's §8.4 match census still saw nocloth drawn.

---

## 1. The `.fclo` format (closed)

### 1.1 Header

| Off | Type | Meaning |
|---|---|---|
| 0x00 | char[4] | `"olcf"` (the `"fclo"` variant is rejected) |
| 0x04 | 12 B | zero pad — all sections start at 0x10 |
| 0x10 | u32 | c0 = free particle count |
| 0x14 | u32 | c1 = pinned particle count |
| 0x18 | u32 | c2 = constraint count (§1.4) |
| 0x1C | u32 | c3 = primitive count (§1.5) |
| 0x20 | u32 | c4 = bone-palette count (§1.6) |

Three files:

| File | Size | c0 | c1 | c2 | c3 | c4 | §1.7 u32 count |
|---|---|---|---|---|---|---|---|
| `fcl_pants_sim.fclo` | 3872 | 40 | 12 | 81 | 40 | 13 | 120 |
| `fcl_shirt_in_sim.fclo` | 3436 | 24 | 24 | 60 | 36 | 10 | 108 |
| `fcl_shirt_out_sim.fclo` | 5480 | 60 | 12 | 120 | 60 | 17 | 180 |

`c0+c1` is the total particle record count; the integrator
(`FUN_1423d0cd0`) iterates the sim object's own particle counter (INF high:
`c0`, with pinned records skipped by mass, or the same counter over all
`c0+c1` with pins re-posed — pending the solver pass).

### 1.2 S1 — rest positions, (c0+c1) × 16 B

Record = `f32 x, y, z` + `f32 w`. The xyz are the particle's rest position in
**Fox space (Y up, metres, body-local)**. Examples (pants): p0
(0.050698, 0.616643, 0.121377), p51 (0.167506, 0.902935, −0.004647). The 4th
float is **not used**: it reads as uninitialised garbage in every file
(NaN, ±1e22, 3482462.0, …) — OBS by census over all 172 records of the three
files; INF (high): padding or a stripped field.

The c0 free particles and the c1 pinned particles are contiguous: records
`[0, c0)` are free, `[c0, c0+c1)` are pinned. The pinned records' positions
**duplicate specific `csk_*` bone rest positions exactly** — pants' pin
records 40..51 = `csk_00_0`, `csk_07_0`, `csk_02_0`, `csk_03_0`, … i.e. the
belt/hem ring of the sheet. OBS (position equality to 1e-12).

### 1.3 S2 — skin binds, (c0+c1) × 32 B

Record = `u32 bone[4]` + `f32 weight[4]`; weights sum to 1.0 (checked: 17/17
spot values). Example (pants): p0 = bones (0,1,2,3), weights
(0.005, 0.365, 0.02, 0.61); p5 = (8,6,7,0) weights (0.27, 0.09, 0.64, 0).
The bone indices address the §1.6 palette (NOT the skl directly).

### 1.4 S3 — constraints, c2 × 8 B

`u32 a, b` particle-index pairs (full 32-bit each, values < c0+c1).
Counts: pants 81 (of which 12 involve a pinned index), shirt_out 120 (12),
shirt_in 60 (24). OBS.

### 1.5 S4 and 1.7 — index arrays

- **S4** = c3 × u32. Read as 4-tuples they are plausible quads
  (pants: (27,25,7,8), (24,25,27,26), (44,43,24,26), (16,0,6,23), …; entries
  may also reference pinned indices), so INF (medium): the sheet's quad
  lattice / render primitive list. Some entries are ≥ c0, so pinned
  particles take part.
- **§1.7 tail array** = an unheadered run of u32 (120/108/180 for
  pants/shirt_in/shirt_out; its count is only derivable from total file size
  − trailer). Values are particle indices; read as pairs they look like a
  second constraint set (pairs include e.g. (33,34), (51,44), (28,38) which
  are NOT in S3). INF (medium): the ClothVConstraint / long-range or bend
  constraint pairs, complementing S3.

### 1.6 Trailer — bone palette, c4 × 4 B

StrCode32 of each bind bone, in order of first use in S2. Verified 100%:
every one of the 13/10/17 values equals `strcode32` of a bone of the matching
`*_cloth.skl`; **no unmatched values** (OBS).

| File | Palette (in order) |
|---|---|
| pants | dsk_hip, dsk_hem_f_l, dsk_hem_ba_l, dsk_hem_i_l, dsk_hem_o_l, dsk_hem_o_r, dsk_hem_ba_r, dsk_hem_i_r, dsk_hem_f_r, dsk_pants_l, dsk_pants_r, dsk_hip_l, dsk_hip_r |
| shirt_in | dsk_hip, sk_belly, dsk_belly_scale, dsk_upperarm_r, dsk_pectoralis_r, sk_chest, dsk_scapula_r, dsk_upperarm_l, dsk_pectoralis_l, dsk_scapula_l |
| shirt_out | dsk_belly_f_l, dsk_belly_o_l, dsk_hip, sk_belly, dsk_belly_f_r, dsk_hem_f_r, dsk_belly_o_r, dsk_belly_ba_r, dsk_belly_ba_l, sk_chest, dsk_pectoralis_r, dsk_scapula_r, dsk_upperarm_r, dsk_pectoralis_l, dsk_scapula_l, dsk_upperarm_l, dsk_belly_scale |

The palette list is exactly the first-use order over S2's records (OBS, by
construction check). `dsk_*` bones are the *driver* bones (the engine poses
them from the body rig); `sk_*` entries are real body bones.

### 1.7 Byte accounting (closed)

`0x10 + 20 + (c0+c1)·48 + c2·8 + c3·4 + tail·4 + c4·4 = file size`
exactly for all three files (pants 3872, shirt_in 3436, shirt_out 5480).
OBS.

---

## 2. How the cloth binds to the body

### 2.1 The two skeletons

- `*_cloth.skl` (parsed, v12, 56-byte records, global bind matrices):
  - `pants_cloth.skl`: 65 bones — `dsk_hip`, 8× `dsk_hem_*`
    (f/o/ba/i × l/r), `dsk_hip_{l,r}`, `dsk_pants_{l,r}`, then the `csk_*`
    grid: `csk_00_0` root at (0.0002, 0.8832, 0.1761) with a ring
    `csk_00..13_0`, then 3 further rows `_1/_2/_3` down the legs to
    y≈0.61.
  - `shirt_out_cloth.skl` / `shirt_in_cloth.skl`: 68 / 71 bones — `sk_belly`,
    `sk_chest`, `dsk_upperarm_{l,r}`, `dsk_pectoralis_{l,r}`,
    `dsk_scapula_{l,r}`, `dsk_belly_*`, `dsk_hem_*`, plus the `csk_*` grid of
    the torso (rows `_0.._5`, y 0.88→1.35).
  - `csk_*` bones are parented in a ring/lattice shape; the `dsk_*`/`sk_*`
    bones are parentless (driven, like every other PES helper rig).
- The fmdl meshes `uniform/cloth/#Win/{shirt_in,shirt_out,pants_00x}.fmdl`
  are skinned to the `csk_*` bones (INF high: the mesh + skl + fclo ship as a
  set; the fmdl's bone group resolves against the cloth skl).

### 2.2 Particle → bone (the bind evaluation, `FUN_1423d0e60`)

`FUN_1423d0e60(obj, float out[16], record*)` blends
`rows[rec.i0]·w0 + rows[rec.i1]·w1 + rows[rec.i2]·w2 + rows[rec.i3]·w3`
where `rows` is the object's 64-byte-matrix array (`obj+8`) and the
indices/weights come from the S2 record. The constructor
(`FUN_1423d1eb0`) calls it once per particle to compute each particle's rest
position from the bind (a fourth-weight position transform:
`p = w0·M3row + ... + ` scale terms, OBS from the decompile) and the sheet's
rest quaternions, then normalises (rsqrtps) and fixes quaternion signs by
comparison masks. Per-particle work is 0x20 = 32 B per record.

### 2.3 Integration (`FUN_1423d0cd0`, OBS)

Per call (dt): `k = (G − drag)·dt·mass_scale` where `G = DAT_14256ae1c`
(gravity-ish global; INF: −9.81-scale value in Fox units — value not yet
read), `drag = *(float*)(obj+0x20)`, `mass_scale = *(float*)(obj+0xa4)`;
force term = `dt²·DAT_142565600`. Particle record (stride 0x80):
`+0x00` current pos (vec3+pad), `+0x10` previous pos, `+0x20` force
accumulator (zeroed every step), `+0x30..0x7F` not yet decoded (INF: bind /
mass / pin data). Update: `prev = cur; cur = cur + (cur − prev)·k + F·dt²`.

---

## 3. Cloth vs nocloth — when is the sim used

From `pes-kit-assembly.md` §3 (OBS, decompile): the cloth path replaces the
`shirt_in`/`shirt_out` parts when
`uStack_1d0 = (+0xB8 == 0 && !tight && !FUN_141a80400 && !(f bit2 && !bit1))
&& FUN_141a7ff50 == 0`, i.e. the default (non-tight, non-referee, non-GK)
case. The part objects are class `ClothShirtParts` (0x80 B) and hold two
models: +0x20 the nocloth `uniform/nocloth/shirt_{in,out}.fmdl` (in the draw
list in the observed match state) and +0x78 the cloth
`uniform/cloth/shirt_{in,out}.fmdl`.

Open (to close in §7): which flag decides which of the two is *drawn*, and
whether the cloth fmdl's vertices are driven by the fclo particles (the
`csk_*` bones) at runtime — plus the sleeve cloth
(`fcl_sleeve_{long,short}_sim.fclo`, `sleeve_{long,short}_cloth.skl`, both
referenced by the exe but **not present in this install's VFS** — OBS, VFS
census).

---

## 4. Goal net

*(in progress — GoalNetScout)*

---

## 5. Corner flags and other physics props

*(in progress — FlagPropScout)*

---

## 6. Live reads

*(pending)*

---

## 7. Open items

- S4 / §1.7 index arrays' exact role (quads vs strips; which constraint
  class each belongs to).
- Pin handling in the integrator (skipped by mass? re-posed from bones?).
- The `+0x30..0x7F` half of the particle record.
- The gravity/drag constant values in `DAT_14256ae1c` / `DAT_142565600`.
- The 5 cloth files the exe names but the VFS lacks: `fcl_sleeve_long_sim.fclo`,
  `fcl_sleeve_short_sim.fclo`, `sleeve_{long,short}_cloth.skl`.
- `pants_cloth_%03d_prep.hkx` — a Havok prep path pattern the exe carries;
  no `.hkx` ships in the VFS for cloth (OBS, census).
