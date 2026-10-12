# PES 2021: player physique (height, weight, body-shape sliders)

Scope: how height, weight and the 14 body/head sliders change the animated skeleton and the rendered body.

Method: static analysis of the unpacked image dump `pesmem.bin` (base 0x140000000), using headless Ghidra decompiles and capstone. Live reads of the running `PES2021.exe` went only through `/proc/<pid>/mem`; nothing was written. The game was in a match: 24 `match::AnimePlayer` (21 players + 3 officials) and 25 `cobra::rd::SceneCharacterNode`. The EDIT save was decrypted in memory with the save cipher from kit report §8.1. Only numeric fields were used; no names appear here.

Tags: **OBS** = observed (decompile, memory or data); **INF** = inferred, with confidence. Addresses are VAs in the PES2021.exe image. Rule from the stadium report: for .rdata, file offset = VA − 0x140000A00.

---

## 0. Headline facts

1. **Mechanism = per-bone scale, nothing else.** There are no blend shapes and no morph targets for body physique.
   - OBS: no physique/morph data file exists in the VFS. The only hit is the menu flow `EditPlBodyPhysiqueSetting.json`.
   - OBS: every effect comes from three hard-coded scale tables in the exe (§3).
2. **One 0x44-byte "physique object" drives everything.** It is `float[16]` + `u8 disable`:
   - `+0x00` height in cm;
   - `+0x04` normalised weight;
   - `+0x08..+0x3C` fourteen sliders in −1..1;
   - `+0x40` disable flag (non-zero → all bone scales = 1).
3. **Height → global scale G.** With leg length `L` (`+0x30`): `G = (1 + k_L·L)·(1 + k_h·g(height))`.
   - k_h = 0.223 when g ≥ 0, 0.277 when g < 0.
   - k_L = 0.018 when L ≥ 0, 0.019 when L < 0.
   - g = (h − 180)/40 when h ≥ 180, (h − 132)/48 − 1 when h < 180.
   - OBS: live G matches to 1e-7 on 24 characters. Examples: 175 cm → 0.9711, 185 → 1.0279, 194 → 1.0781.
4. **Animation side.**
   - Every anim-skeleton bone's bind translation is multiplied by its **parent's** absolute length scale (table A, x component).
   - The root (`sk_root_hip`) is multiplied by G.
   - Root motion (world displacement) is multiplied by `clamp(G, 0.9166, 1.0833)` (`AnimMovePlugin+0xC0`).
   - All OBS live, error < 1.3e-7.
5. **Render side.**
   - The model matrix is scaled uniformly by G (`SceneCharacterNode+0x108`, OBS).
   - Each render bone gets a non-uniform local scale (table B, xyz in bone-local axes). OBS: exact for the 18 `sk_*` bones + `dsk_hip` on 25 nodes.
   - The shoulder-height slider additionally rotates `dsk_clavicle_l/r` by ±8° and offsets `dsk_collar_l/r` by up to 1 cm (OBS code; live survival not confirmed, §5.3).
6. **Weight** is normalised against a height-indexed table `{Wmin, Wstd, Wmax}` to −1..1 (OBS, exact rationals live). It thickens or thins the trunk and limbs (table B entry 1). It does **not** change G or the animation bone lengths.

---

## 1. Source fields (Q1)

### 1.1 Height and weight: player record (OBS)

- EDIT save player record (kit report §8.1, records from `data.dat+0x7C`, stride 0x138):
  - `+0x0A` u8 = **height in cm**;
  - `+0x0B` u8 = **weight in kg**.
- OBS:
  - 21/21 live players have physique-object `+0x00` = save height.
  - The weight normaliser applied to save (h, w) reproduces live `+0x04` to 3.4e-8.
  - Save stats: height 125–205 (mode 180); weight 30–129 (mode 80, 4cc placeholder default).
- In the match, the in-memory player struct holds height at `+0x39` and weight at `+0x3A`. OBS: read by `FUN_1408be270` / `FUN_141f58600`.
- `common/etc/pesdb/Player.bin` in this install is the 4cc placeholder file:
  - 1,506,960 B = 4830 × 0x138; ids at record +8;
  - heights all zero.
  - INF (high): the live values come only from the EDIT save. Vanilla `Player.bin` was not inspected.

### 1.2 Sliders: 72-byte appearance record (OBS)

The sliders sit in the 72-byte appearance record. Offsets below are appearance-record offsets; the same bytes are at save record +0xF0+off and at `PlayerAppearance.bin` record offset (off − 8).

- One nibble each, stored value n ∈ 0..14.
- **Decoder `FUN_141ad98b0(obj, body)`**: `v = 2n/14 − 1`, and **n = 15 → 0**. 7 is neutral.
- Its callers:
  - the character build `FUN_141ec4560` (draw side; `obj` = request+0x10);
  - `FUN_141e586f0` (edit preview). This one first fills a default body of all-7 nibbles, then copies the record's 56-byte body over it.

| Appearance off:bit | 4ccEditor name | → physique obj | Engine effect (from tables, bone-local axes) |
|---|---|---|---|
| 0x0C:0 | neck_len | +0x08 | `sk_neck` x 0.8 … 1.1 |
| 0x0C:4 | neck_size | +0x0C | `sk_neck` yz, collars |
| 0x0D:0 | shldr_hi | +0x10 | `sk_shoulder` z 0.8 … 1.55, deltoid z; render: clavicle roll ±8°, collar offset |
| 0x0D:4 | shldr_wid | +0x14 | `sk_shoulder` x 0.9 … 1.2, chest/clavicle z |
| 0x0E:0 | chest | +0x18 | neck/chest y, collars |
| 0x0E:4 | waist | +0x1C | hip/belly/thigh/hem/pants yz (B only) |
| **0x0F:4** | arm_len | **+0x20** | upper/fore-arm x 0.95 … 1.05 |
| **0x0F:0** | arm_size | **+0x24** | upper/fore-arm yz 0.92 … 1.08 |
| 0x10:0 | thigh | +0x28 | hip/thigh yz, hem, pants |
| 0x10:4 | calf | +0x2C | leg yz 0.9 … 1.2, `dsk_leg`; knees inverse |
| 0x11:0 | leg_len | +0x30 | enters G (§0.3); belly x 0.87 … 1.15, thigh/leg x ±0.2 % |
| 0x11:4 | head_len | +0x34 | `sk_head` x 0.9 … 1.1 |
| 0x12:0 | head_wid | +0x38 | `sk_head` z 0.9 … 1.1 |
| 0x12:4 | head_dep | +0x3C | `sk_head` y 0.9 … 1.1 |

- The arm pair is stored **swapped** relative to the object order. The decoder reads `+0x20 = byte 0x0F >> 4` and `+0x24 = byte 0x0F & 0xF`. OBS: live 74008 (nib6 = 14, nib7 = 7) has `+0x20 = 0`, `+0x24 = 1`.
- The 4cc names agree with the engine effect.
- **Bone axes** (OBS, `body.skl` global matrices): local **x = along the bone**. Spine/head: y = front-back (depth), z = lateral (width). Legs: y = depth, z = lateral. Arms in the A-pose bind: y = depth, z ≈ vertical.
- **Edit flag.** X-field bits 0–3 of the appearance record are 4cc's `b_edit_face/hair/phys/strip`.
  - OBS: the decoder never tests them; nibbles are always applied.
  - INF (medium): neutral 7s are what makes "not edited" a no-op.
- Save stats (4830 players): 4600+ have every nibble at 7. The edited ones are mostly 0 or 14 extremes.
- OBS (counter-check): the edit save holds 14 nibbles here. 4ccEditor `pes20.cpp` reads the same 14, in the same order, at the same place (its `edit_bits.txt` offsets carry a +1-byte skew).

### 1.3 Weight normaliser `FUN_14195c580(h, w)` (OBS)

- Clamp h to [132, 219.999].
- Find the segment of the table at .rdata **0x142a82900**. It has 12 rows of `{f32 h, u32 Wmin, u32 Wstd, u32 Wmax}`:

  | h | 132 | 140 | 148 | 156 | 164 | 172 | 180 | 188 | 196 | 204 | 212 | 220 |
  |---|---|---|---|---|---|---|---|---|---|---|---|---|
  | Wmin | 26 | 29 | 32 | 36 | 40 | 44 | 48 | 53 | 57 | 62 | 67 | 72 |
  | Wstd | 38 | 43 | 48 | 53 | 59 | 65 | 71 | 77 | 84 | 91 | 98 | 106 |
  | Wmax | 54 | 60 | 67 | 75 | 83 | 91 | 100 | 109 | 119 | 129 | 139 | 150 |

- Lerp the three values linearly in h.
- `w' = clamp(w, Wmin, Wmax)`.
- `v = (w' − Wstd)/(Wmax − Wstd)` if `w ≥ Wstd`; else `(w' − Wmin)/(Wstd − Wmin) − 1`.
- Clamp v to [−1, 1].
- Live check (exact rationals): 180/55 → −16/23; 185/99 → 194/247; 194/113 → 123/137; 175/80 → 102/217.

### 1.4 Height normalisers (OBS)

- `g(h)` = `FUN_14195c4b0`: piecewise 132/180/220, **not clamped** outside. Tables A/B entries 0 and 16 use it, and G uses it.
- `g2(h)` = `FUN_14195c440`: piecewise 155/180/210, clamped to ±1. Only table C entry 0 (hem) uses it.

---

## 2. Data flow (OBS)

**Match player (anim side):**
- `FUN_1408bfb20` → cache getter `FUN_141e92e80` (record list keyed by `+0xB0`).
- The record is built by `FUN_1408be270`: height from struct+0x39, weight via `FUN_14195c580`.
- The sliders arrive from the appearance decode; live owner objects carry them.
- `FUN_1408bfaa0` copies the object to owner+0x784.
- `FUN_1406eb210(AnimePlayer, obj)` → **`FUN_141eb5810`**, per bone `i` of the BodyAnimControl skeleton (20 bones, `body_anim_skel.ask` order):
  - `p = parent(i)`;
  - `absS[i] = A(name(p)).x` → `AnimePlayer+0x960[i]`;
  - `relS[i] = B(name(p)).x` → `AnimePlayer+0x9B0[i]`;
  - `FUN_141af4be0(BAC, i, absS[i])`, which writes `skelLocalT[i] = bindLocalT[i] · absS[i]` (all 4 components). It also stores `BAC+0x298[i] = absS[i]`, and for i = 0 `BAC+0x2B4 = 1/absS[0]`.
  - The root uses `FUN_141ebc930(0x13)` = skeleton index 0 (map at 0x1437f4e60 = {1..19, 0}) with the name `"root_main"`. That name is in no table, so `absS[0] = G·1`, `relS[0] = G·1`.
  - `AnimePlayer+0xA00 = clamp(absS[0], 0.9166, 1.0833)` (constants 0x1425ddf78/80). Then `FUN_141af4eb0` → `AnimMovePlugin+0xC0`.
- `BAC+0x2A0..0x2B0` = A(...)x for the first 5 bones (consumer not traced).

**Draw side:**
- `FUN_141ec4560` → `FUN_141ad98b0(request+0x10, body)` → character build.
- `SceneCharacterNode+0x114` = the physique object.
- `FUN_141a21670` → `FUN_141a1f520`. For each of the 175 body-skeleton entries (list at node+0x88: count +0x30, entries +0x28, stride 0xC0; entry +0x10 = bone hash, +0x18 → transform record {scale4, quat4, trans4}):
  - record.scale = **B(bone)** (all 4 components);
  - entry+0x28..0x30 = **C(bone)** xyz;
  - shoulder height `s = obj+0x10`:
    - `dsk_clavicle_l`: quat about local Y by `8·s`°; `dsk_clavicle_r` by `−8·s`°. The quat is (0, sin θ/2, 0, cos θ/2); INF high on the component placement (sincos-polynomial select).
    - `dsk_collar_l` translation = (0, 0, s ≥ 0 ? 0.01·s : 0.005·s); `dsk_collar_r` = (0, 0, s ≥ 0 ? −0.01·s : −0.005·s). Units are metres (INF high). Constants are at 0x14259bdf4 / 0x1425c7ba4 / 0x14256ae18 / 0x14260a24c / 0x1425b8708 / 0x1425e6820.
  - Then `node+0x108 = G`, `node+0x10C = 1/G`.
- `FUN_141a21670` builds the node's model matrix with rows 0–2 × G (uniform scale; translation unscaled). The cull/shadow radius is also × G.

Edit preview: `FUN_141ee33f0` → `FUN_141e586f0` (decode sliders) + height/weight → same object.

---

## 3. The scale tables (OBS: values from the image dump; headers on disk in .rdata)

- **Table header** (32 B per entry): `{u64 recordArray, u32 count, u32 0, u64 getter, f32 a, f32 b}`.
  - A: 16 entries at 0x142a83050.
  - B: 21 entries at 0x142a83250.
  - C: 2 entries at 0x142a834f0.
- **Record** (48 B): `{u32 boneHash, 12 B pad, f32[4] negVec, f32[4] posVec}`. `boneHash = StrCode64(boneName) & 0xFFFFFFFF` (OBS: all 45 hashes resolved).
- **Evaluation** (`FUN_14195c710` / `c830` / `c950`): start from `R = (1,1,1,1)`; for every entry whose array contains the bone:

  ```
  v = getter(obj)
  X = (v >= 0) ? b * posVec : a * negVec
  R *= lerp(X, C, 1 - |v|)        // C = (1,1,1,0) at 0x1425a0c50, i.e. R *= 1 + |v|*(X - 1) for xyz
  ```

  If `obj+0x40 != 0`, return (1,1,1,1).
- a and b are 1 everywhere except table A entry 0 (a = 0.723, b = 1.223, the height factor) and entry 12 (0.981, 1.018, the leg-length factor). So **A = B with the G factors folded into the length**: A is the absolute scale, B is relative to the G-scaled model.
- G (`FUN_14195ca70`): constants at .rdata 0x142a83530 = {0.018, 0.019, 0.223, 0.277}, 0x142a83540 = {132, 220}.

**Where the per-bone records live (OBS):**
- They are in **runtime-initialised .data** (0x1437c92d0…0x1437caf90, beyond the on-disk raw size).
- 23 static initialisers at 0x14020c130…0x14020dbd0 build them. Each record is `StrCode(name)` (name string in .rdata) + two vec4 constants loaded from .rdata.
- The PES2021.exe `.text` is packed on disk (raw size 0x600), so the name↔vector pairing exists only in unpacked code.
- An automatic source map (`initdec.py`, capstone pattern-tracker) resolved 250/750 record fields to their .rdata source, with 0 mismatches. The rest use store patterns it does not follow.
- Consequence for GodotFootball's "no PES numbers committed" rule:
  - commit only the structural pairing (bone name + .rdata VA of each vec4), and read the numbers from the user's exe;
  - the header tables, weight table and constants can be read from .rdata at file offset VA − 0x140000A00 (OBS: byte-identical to the dump).

The full tables are in Appendix A. Values are verbatim from `pesmem.bin`; vectors are (x, y, z) in bone-local axes; w is unused.

---

## 4. Interaction with animation (Q3)

- **Bone lengths (OBS).** In the anim skeleton, `local translation[i] = bind[i] · absS[parent(i)]`. absS is the table A x component (length along the parent bone).
  - Height enters through table A entry 0. Its x factor is `1 + 0.223·g`, or `1 − 0.277·|g|` below 180, exactly G's height factor, for every listed sk bone except the head (0.99 → net 1.211 at g = 1).
  - So a 194 cm player has ≈ 7.8 % longer limbs. The arm/neck/leg-length sliders lengthen individual chains.
- **Root (OBS).** `sk_root_hip`'s bind translation (hip height) × G.
- **Rotations.** Gani rotations are untouched (INF high: only translations are scaled).
- **Root motion / stride (OBS).** `AnimMovePlugin` adds `R · rootDelta · scale` to the world transform (animselect report §5.4). Its scale = `clamp(G, 0.9166, 1.0833)`.
  - Live: all 24 plugins equal this; 199 cm gives G = 1.1059 → plugin 1.0833.
  - So stride grows with height up to about ±8 %. Above roughly 193 cm (or 3/4 of that range below 180) the pose scale exceeds the motion scale. INF: slight foot sliding for very tall players.
- **Anim-graph root nodes (OBS live).** Nodes 1, 11 and 13 of the BAC graph get `(G, 1/G)` at node +0x20/+0x24 or +0xB0/+0xB4 (`FUN_141af4480`).
  - INF (medium-high): the gani root (RIG_ROOT/MOTION) position track is scaled by G, and 1/G is used to normalise back to model units.
  - Practical rule: **multiply sampled root positions by G**.
- **Weight and the other sliders** do not touch animation bone lengths except through table A entries (arm length, leg length, shoulder width, neck length, head) and G (leg length).
- **Not traced.** `playbackSpeed` (≈ 0.9–1.15, animselect report) is not set by any physique function seen here.

---

## 5. Live confirmation (Q4)

### 5.1 Anim side (OBS, `apcheck3.py`)

- For all 24 AnimePlayers (21 players, 10 with non-neutral sliders, + 3 officials), the reference implementation `phys.py` reproduces from the owner's physique object:
  - `+0x960[20]`, `+0x9B0[20]` (parent rule) and `+0xA00`;
  - max error 1.2e-7.
- `objcheck.py`:
  - the physique object rebuilt from **save fields only** (height, weight, 14 nibbles) equals the live object (max 3.4e-8, 21/21);
  - BAC skeleton local translations = `body_anim_skel.ask` local × `absS` (max 5.3e-8, 21/21).
- Sample values:

  | player (anonymised) | height | G | MoveScale |
  |---|---|---|---|
  | id 1 | 175 | 0.9711 | 0.9711 |
  | id 2 | 185 | 1.0279 | 1.0279 |
  | id 3 | 194 | 1.0781 | 1.0781 |
  | id 4 | 199 | 1.1059 | 1.0833 (clamped) |
  | id 5 | 180, leg_len nibble 0 | 0.981 | 0.981 |

- Player id 6 (shoulder width nibble 14): `absS[upperarm] = A(shoulder).x = 1.2 · 1.0279 = 1.2334` (OBS).

### 5.2 Render side (OBS, `drawall.py`, 25 SceneCharacterNodes, 9 with sliders)

- `node+0x108 = G`: max error 8.3e-8.
- Entry+0x28 = table C: max error 2.8e-8.
- Transform-record scale = table B for the 18 `sk_*` bones + `dsk_hip`: exact in all nodes.
- `dsk_hem_*`, `dsk_knee_*` match at some times and not at others.
- Other `dsk_*` helpers (deltoid, elbow, pectoralis, scapula, trapezius, pants, belly_*, …) differ pose-dependently and asymmetrically left/right (e.g. elbow x 1.79 vs 1.28 in one frame).
  - INF (high): the 50+ `cobra::rd::*Constraint` classes (`BellyScaleConstraint`, `DeltoidConstraint`, `ElbowConstraint`, …) recompute helper scales every frame from their drivers. The table value is only their initial value/input.
  - Several helpers show `B · (1/G)` in y/z (e.g. trapezius, collar, pectoralis on the leg_len = −1 player). INF: some constraints cancel the model-root scale.

### 5.3 Not confirmed

- The clavicle rotation and collar translation written at build time could not be observed: at runtime the record holds the posed model-space transform (positions ≈ 1.3–1.5 m), overwritten by animation/constraints.
- The writes are OBS in code (bone identity confirmed: entry+8 equals the bone-name ids at 0x1437cdaf0/cdb40/cdb90/cdaa0 = `dsk_clavicle_l/r`, `dsk_collar_l/r`). Whether they survive into the final pose is open.

---

## 6. Recipe (reimplementation)

1. Read save record `+0x0A` (h cm) and `+0x0B` (w kg), plus the 14 nibbles at appearance `+0x0C..+0x12` (low nibble first; note the arm pair order in §1.2).
2. Build `obj`:
   - `[0] = h`;
   - `[1] = wnorm(h, w)` (§1.3);
   - `[2..15] = (n == 15) ? 0 : 2n/14 − 1` in object order (§1.2 table).
3. `G = (1 + (L ≥ 0 ? 0.018 : 0.019)·L) · (1 + (g ≥ 0 ? 0.223 : 0.277)·g)`, with `L = obj[12]` and `g = g(h)` (§1.4).
4. **Anim skeleton** (`body_anim_skel.ask`):
   - for i > 0: `local[i] *= A(name(parent(i))).x`;
   - `local[0] *= G`;
   - root-motion world displacement *= `clamp(G, 0.9166, 1.0833)`;
   - sampled gani root positions *= G (INF, §4).
5. **Render.**
   - Model root transform *= uniform G.
   - For each render bone b in the main 19: skin matrix = posed bone matrix · Scale(B(b).xyz) (local axes, x along the bone; not inherited, INF high: posed positions come from the anim skeleton).
   - Helper `dsk_*` bones: use B(b) as a static approximation. PES runs per-bone constraints (not decoded).
   - Shoulder height: optionally the clavicle roll ±8·s° and the collar z offset (§2).
6. Reference code: `<scratch>/PhysiqueRE/phys.py` (`hnorm`, `hnorm2`, `wnorm`, `evaluate(TA|TB|TC, obj, hash32)`, `G`, `obj`). Validation scripts: `apcheck3.py`, `objcheck.py`, `drawall.py`.

---

## 7. Killed hypotheses

- **"Body shape uses morph targets / a VFS data table."** Killed: the VFS search found only the menu json, and the code applies hard-coded bone tables.
- **"`AnimMovePlugin` scale 0.97–1.08 = height/180."** Partly killed: it is G (piecewise, 0.223/0.277 slopes, × leg-length factor), clamped to [0.9166, 1.0833]. Example: 175 cm → 0.9711, not 0.9722.
- **"Weight = BMI-linear."** Killed: the slopes differ above/below a per-height standard weight. The exact rule is the 12-row table (§1.3).
- **"Match players ignore the sliders."**
  - Early read: all live slider floats were 0 (transient state before the match physique was set).
  - Later reads show the sliders present and matching the save exactly.
  - Also killed: "only `FUN_1408be270` builds the record" — that builder zeroes the sliders, but they are present live.
  - The live object matches the save, but which writer fills the match-side sliders was not identified.
- **"AnimePlayer+0x960[i] = scale of bone i."** Killed: it is the scale of **parent(i)** (the bone length that bone i's offset represents).
- **"Draw transform record = offset list."** Killed: it is the posed transform; only its scale lane is physique-static.
- **"4ccEditor arm_size/arm_len labels are swapped relative to the engine."** Killed: the decoder swaps the storage order, and the labels match the effect.

## 8. Open

- Math of the `rd::*Constraint` helper bones (pose-dependent helper scales).
- Node types/consumers of the BAC graph `(G, 1/G)` parameters; the exact root-track scaling.
- Survival of the clavicle/collar build-time offsets.
- The writer that fills the match-side slider fields (the observed values are exact; the path is unknown).
- Consumer of table C (entry+0x28; hem vs height, chest/sleeves vs leg length).
- Full automatic initializer→.rdata source map (250/750 fields mapped).

---

## Appendix A: tables (verbatim PES values; bone names resolved via StrCode64 low 32 bits)

#### Table A (absolute, FUN_14195c710), header at VA 0x142a83050

| # | slider (getter) | a (neg) | b (pos) | bone: neg vec → pos vec (x,y,z) |
|---|---|---|---|---|
| 0 | h(132/180/220) `0x14195c4b0` | 0.723 | 1.223 | sk_head: (1.191, 1.191, 1.191) → (0.99, 0.99, 0.99)<br>sk_neck: (1.191, 1.05, 1.05) → (0.99, 0.99, 0.99)<br>sk_chest: (1, 1.02, 1.03) → (1, 1, 1)<br>sk_belly: (1, 1.01, 1.01) → (1, 0.98, 0.98)<br>dsk_hip: (1, 1.01, 1.01) → (1, 0.98, 0.98)<br>sk_thigh_l: (1, 1.01, 1.01) → (1, 0.98, 0.98)<br>sk_leg_l: (1, 1.02, 1.02) → (1, 0.98, 0.98)<br>sk_foot_l: (1, 1.02, 1.02) → (1, 0.95, 0.95)<br>sk_thigh_r: (1, 1.01, 1.01) → (1, 0.98, 0.98)<br>sk_leg_r: (1, 1.02, 1.02) → (1, 0.98, 0.98)<br>sk_foot_r: (1, 1.02, 1.02) → (1, 0.95, 0.95)<br>sk_shoulder_l: (1, 1.01, 1.01) → (1, 0.98, 0.98)<br>sk_upperarm_l: (1, 1.03, 1.03) → (1, 0.98, 0.98)<br>sk_forearm_l: (1, 1, 1) → (1, 0.98, 0.98)<br>sk_hand_l: (1, 1, 1) → (1, 0.98, 0.98)<br>sk_shoulder_r: (1, 1.01, 1.01) → (1, 0.98, 0.98)<br>sk_upperarm_r: (1, 1.03, 1.03) → (1, 0.98, 0.98)<br>sk_forearm_r: (1, 1, 1) → (1, 0.98, 0.98)<br>sk_hand_r: (1, 1, 1) → (1, 0.98, 0.98) |
| 1 | +0x04 weight `0x14195c570` | 1 | 1 | sk_belly: (1, 0.9, 0.9) → (1, 1.1, 1.1)<br>dsk_hip: (1, 0.9, 0.9) → (1, 1.05, 1.05)<br>sk_thigh_l: (1, 0.85, 0.85) → (1, 1.05, 1.05)<br>sk_leg_l: (1, 0.85, 0.85) → (1, 1.05, 1.05)<br>sk_foot_l: (1, 1, 1) → (1, 1.05, 1.05)<br>sk_thigh_r: (1, 0.85, 0.85) → (1, 1.05, 1.05)<br>sk_leg_r: (1, 0.85, 0.85) → (1, 1.05, 1.05)<br>sk_foot_r: (1, 1, 1) → (1, 1.05, 1.05)<br>sk_shoulder_l: (1, 0.9, 0.9) → (1, 1.1, 1.1)<br>sk_upperarm_l: (1, 0.9, 0.9) → (1, 1.1, 1.1)<br>sk_forearm_l: (1, 0.9, 0.9) → (1, 1.1, 1.1)<br>sk_hand_l: (1, 1, 1) → (1, 1.1, 1.1)<br>sk_shoulder_r: (1, 0.9, 0.9) → (1, 1.1, 1.1)<br>sk_upperarm_r: (1, 0.9, 0.9) → (1, 1.1, 1.1)<br>sk_forearm_r: (1, 0.9, 0.9) → (1, 1.1, 1.1)<br>sk_hand_r: (1, 1, 1) → (1, 1.1, 1.1) |
| 2 | +0x08 neck len `0x1420e3a50` | 1 | 1 | sk_neck: (0.8, 1, 1) → (1.1, 1, 1) |
| 3 | +0x0C neck size `0x14195c520` | 1 | 1 | sk_neck: (1, 0.9, 0.9) → (1, 1.025, 1.1)<br>dsk_collar_l: (1, 1, 1) → (1, 1.02, 1)<br>dsk_collar_r: (1, 1, 1) → (1, 1.02, 1) |
| 4 | +0x10 shoulder height `0x14195c530` | 1 | 1 | sk_shoulder_l: (1, 1, 0.8) → (1, 1, 1.55)<br>sk_shoulder_r: (1, 1, 0.8) → (1, 1, 1.55) |
| 5 | +0x14 shoulder width `0x14195c540` | 1 | 1 | sk_shoulder_l: (0.9, 1, 1) → (1.2, 1, 1)<br>sk_shoulder_r: (0.9, 1, 1) → (1.2, 1, 1) |
| 6 | +0x18 chest `0x1420553a0` | 1 | 1 | sk_neck: (1, 1, 1) → (1, 1.1, 1)<br>sk_chest: (1, 0.92, 1) → (1, 1.1, 1)<br>dsk_collar_l: (1, 1, 1) → (1, 1.15, 1)<br>dsk_collar_r: (1, 1, 1) → (1, 1.15, 1) |
| 7 | +0x1C waist `0x14195c560` | 1 | 1 | sk_thigh_l: (1, 1, 1) → (1, 1, 1)<br>sk_thigh_r: (1, 1, 1) → (1, 1, 1) |
| 8 | +0x20 arm length `0x14195c400` | 1 | 1 | sk_upperarm_l: (0.95, 1, 1) → (1.05, 1, 1)<br>sk_forearm_l: (0.95, 1, 1) → (1.05, 1, 1)<br>dsk_sleeve_l: (1, 1.05, 1.05) → (1, 1, 1)<br>sk_upperarm_r: (0.95, 1, 1) → (1.05, 1, 1)<br>sk_forearm_r: (0.95, 1, 1) → (1.05, 1, 1)<br>dsk_sleeve_r: (1, 1.05, 1.05) → (1, 1, 1) |
| 9 | +0x24 arm size `0x14195c410` | 1 | 1 | sk_upperarm_l: (1, 0.92, 0.92) → (1, 1.08, 1.08)<br>sk_forearm_l: (1, 0.92, 0.92) → (1, 1.08, 1.08)<br>sk_upperarm_r: (1, 0.92, 0.92) → (1, 1.08, 1.08)<br>sk_forearm_r: (1, 0.92, 0.92) → (1, 1.08, 1.08)<br>dsk_sleeve_l: (1, 0.95, 0.95) → (1, 1.1, 1.1)<br>dsk_sleeve_r: (1, 0.95, 0.95) → (1, 1.1, 1.1) |
| 10 | +0x28 thigh `0x14195c550` | 1 | 1 | dsk_hip: (1, 1, 1) → (1, 1.1, 1.095)<br>sk_thigh_l: (1, 0.9, 0.95) → (1, 1.1, 1.075)<br>sk_thigh_r: (1, 0.9, 0.95) → (1, 1.1, 1.075)<br>dsk_hem_l: (1, 0.94, 0.97) → (1, 1.1, 1.075)<br>dsk_hem_r: (1, 0.94, 0.97) → (1, 1.1, 1.075)<br>dsk_pants_l: (1, 0.94, 0.97) → (1, 1.1, 1.075)<br>dsk_pants_r: (1, 0.94, 0.97) → (1, 1.1, 1.075) |
| 11 | +0x2C calf `0x14195c420` | 1 | 1 | sk_leg_l: (1, 0.9, 0.9) → (1, 1.2, 1.2)<br>sk_leg_r: (1, 0.9, 0.9) → (1, 1.2, 1.2)<br>dsk_leg_l: (1, 0.9, 0.9) → (1, 1.2, 1.2)<br>dsk_leg_r: (1, 0.9, 0.9) → (1, 1.2, 1.2)<br>dsk_knee_l: (1, 1.1, 1.1) → (1, 0.8, 0.8)<br>dsk_knee_r: (1, 1.1, 1.1) → (1, 0.8, 0.8) |
| 12 | +0x30 leg length `0x1407b5fe0` | 0.981 | 1.018 | sk_head: (1.019, 1.019, 1.019) → (0.9823, 0.9823, 0.9823)<br>sk_neck: (1.019, 1.019, 1.019) → (0.9823, 0.9823, 0.9823)<br>sk_chest: (1.019, 1.019, 1.019) → (0.9823, 0.9823, 0.9823)<br>sk_belly: (1.146, 1.019, 1.019) → (0.8665, 0.9823, 0.9823)<br>dsk_hip: (0.9978, 1.019, 1.019) → (1.002, 0.9823, 0.9823)<br>sk_thigh_l: (0.9978, 1.019, 1.019) → (1.002, 0.9823, 0.9823)<br>sk_leg_l: (0.9978, 1.019, 1.019) → (1.002, 0.9823, 0.9823)<br>sk_foot_l: (1.019, 1.019, 1.019) → (0.9823, 0.9823, 0.9823)<br>sk_thigh_r: (0.9978, 1.019, 1.019) → (1.002, 0.9823, 0.9823)<br>sk_leg_r: (0.9978, 1.019, 1.019) → (1.002, 0.9823, 0.9823)<br>sk_foot_r: (1.019, 1.019, 1.019) → (0.9823, 0.9823, 0.9823)<br>sk_shoulder_l: (1.019, 1.019, 1.019) → (0.9823, 0.9823, 0.9823)<br>sk_upperarm_l: (1.019, 1.019, 1.019) → (0.9823, 0.9823, 0.9823)<br>sk_forearm_l: (1.019, 1.019, 1.019) → (0.9823, 0.9823, 0.9823)<br>sk_hand_l: (1.019, 1.019, 1.019) → (0.9823, 0.9823, 0.9823)<br>sk_shoulder_r: (1.019, 1.019, 1.019) → (0.9823, 0.9823, 0.9823)<br>sk_upperarm_r: (1.019, 1.019, 1.019) → (0.9823, 0.9823, 0.9823)<br>sk_forearm_r: (1.019, 1.019, 1.019) → (0.9823, 0.9823, 0.9823)<br>sk_hand_r: (1.019, 1.019, 1.019) → (0.9823, 0.9823, 0.9823) |
| 13 | +0x34 head length `0x14195c430` | 1 | 1 | sk_head: (0.9, 1, 1) → (1.1, 1, 1) |
| 14 | +0x38 head width `0x1422fba00` | 1 | 1 | sk_head: (1, 1, 0.9) → (1, 1, 1.1) |
| 15 | +0x3C head depth `0x141889a90` | 1 | 1 | sk_head: (1, 0.9, 1) → (1, 1.1, 1) |

#### Table B (relative, FUN_14195c830), header at VA 0x142a83250

| # | slider (getter) | a (neg) | b (pos) | bone: neg vec → pos vec (x,y,z) |
|---|---|---|---|---|
| 0 | h(132/180/220) `0x14195c4b0` | 1 | 1 | sk_head: (1.191, 1.191, 1.191) → (0.99, 0.99, 0.99)<br>sk_neck: (1.191, 1.05, 1.05) → (0.99, 0.99, 0.99)<br>sk_chest: (1, 1.02, 1.03) → (1, 1, 1)<br>sk_belly: (1, 1.01, 1.01) → (1, 0.98, 0.98)<br>dsk_hip: (1, 1.01, 1.01) → (1, 0.98, 0.98)<br>sk_thigh_l: (1, 1.01, 1.01) → (1, 0.98, 0.98)<br>sk_leg_l: (1, 1.02, 1.02) → (1, 0.98, 0.98)<br>sk_foot_l: (1, 1.02, 1.02) → (1, 0.95, 0.95)<br>sk_thigh_r: (1, 1.01, 1.01) → (1, 0.98, 0.98)<br>sk_leg_r: (1, 1.02, 1.02) → (1, 0.98, 0.98)<br>sk_foot_r: (1, 1.02, 1.02) → (1, 0.95, 0.95)<br>sk_shoulder_l: (1, 1.01, 1.01) → (1, 0.98, 0.98)<br>sk_upperarm_l: (1, 1.03, 1.03) → (1, 0.98, 0.98)<br>sk_forearm_l: (1, 1, 1) → (1, 0.98, 0.98)<br>sk_hand_l: (1, 1, 1) → (1, 0.98, 0.98)<br>sk_shoulder_r: (1, 1.01, 1.01) → (1, 0.98, 0.98)<br>sk_upperarm_r: (1, 1.03, 1.03) → (1, 0.98, 0.98)<br>sk_forearm_r: (1, 1, 1) → (1, 0.98, 0.98)<br>sk_hand_r: (1, 1, 1) → (1, 0.98, 0.98) |
| 1 | +0x04 weight `0x14195c570` | 1 | 1 | sk_belly: (1, 0.9, 0.9) → (1, 1.1, 1.1)<br>dsk_hip: (1, 0.9, 0.9) → (1, 1.05, 1.05)<br>sk_thigh_l: (1, 0.85, 0.85) → (1, 1.05, 1.05)<br>sk_leg_l: (1, 0.85, 0.85) → (1, 1.05, 1.05)<br>sk_foot_l: (1, 1, 1) → (1, 1.05, 1.05)<br>sk_thigh_r: (1, 0.85, 0.85) → (1, 1.05, 1.05)<br>sk_leg_r: (1, 0.85, 0.85) → (1, 1.05, 1.05)<br>sk_foot_r: (1, 1, 1) → (1, 1.05, 1.05)<br>sk_shoulder_l: (1, 0.9, 0.9) → (1, 1.1, 1.1)<br>sk_upperarm_l: (1, 0.9, 0.9) → (1, 1.1, 1.1)<br>sk_forearm_l: (1, 0.9, 0.9) → (1, 1.1, 1.1)<br>sk_hand_l: (1, 1, 1) → (1, 1.1, 1.1)<br>sk_shoulder_r: (1, 0.9, 0.9) → (1, 1.1, 1.1)<br>sk_upperarm_r: (1, 0.9, 0.9) → (1, 1.1, 1.1)<br>sk_forearm_r: (1, 0.9, 0.9) → (1, 1.1, 1.1)<br>sk_hand_r: (1, 1, 1) → (1, 1.1, 1.1) |
| 2 | +0x08 neck len `0x1420e3a50` | 1 | 1 | sk_neck: (0.8, 1, 1) → (1.1, 1, 1) |
| 3 | +0x0C neck size `0x14195c520` | 1 | 1 | sk_neck: (1, 0.9, 0.9) → (1, 1.025, 1.1)<br>dsk_collar_l: (1, 1, 1) → (1, 1.02, 1)<br>dsk_collar_r: (1, 1, 1) → (1, 1.02, 1) |
| 4 | +0x10 shoulder height `0x14195c530` | 1 | 1 | sk_shoulder_l: (1, 1, 0.8) → (1, 1, 1.55)<br>sk_shoulder_r: (1, 1, 0.8) → (1, 1, 1.55) |
| 5 | +0x14 shoulder width `0x14195c540` | 1 | 1 | sk_shoulder_l: (0.9, 1, 1) → (1.2, 1, 1)<br>sk_shoulder_r: (0.9, 1, 1) → (1.2, 1, 1) |
| 6 | +0x18 chest `0x1420553a0` | 1 | 1 | sk_neck: (1, 1, 1) → (1, 1.1, 1)<br>sk_chest: (1, 0.92, 1) → (1, 1.1, 1)<br>dsk_collar_l: (1, 1, 1) → (1, 1.15, 1)<br>dsk_collar_r: (1, 1, 1) → (1, 1.15, 1) |
| 7 | +0x1C waist `0x14195c560` | 1 | 1 | sk_thigh_l: (1, 1, 1) → (1, 1, 1)<br>sk_thigh_r: (1, 1, 1) → (1, 1, 1) |
| 8 | +0x20 arm length `0x14195c400` | 1 | 1 | sk_upperarm_l: (0.95, 1, 1) → (1.05, 1, 1)<br>sk_forearm_l: (0.95, 1, 1) → (1.05, 1, 1)<br>dsk_sleeve_l: (1, 1.05, 1.05) → (1, 1, 1)<br>sk_upperarm_r: (0.95, 1, 1) → (1.05, 1, 1)<br>sk_forearm_r: (0.95, 1, 1) → (1.05, 1, 1)<br>dsk_sleeve_r: (1, 1.05, 1.05) → (1, 1, 1) |
| 9 | +0x24 arm size `0x14195c410` | 1 | 1 | sk_upperarm_l: (1, 0.92, 0.92) → (1, 1.08, 1.08)<br>sk_forearm_l: (1, 0.92, 0.92) → (1, 1.08, 1.08)<br>sk_upperarm_r: (1, 0.92, 0.92) → (1, 1.08, 1.08)<br>sk_forearm_r: (1, 0.92, 0.92) → (1, 1.08, 1.08)<br>dsk_sleeve_l: (1, 0.95, 0.95) → (1, 1.1, 1.1)<br>dsk_sleeve_r: (1, 0.95, 0.95) → (1, 1.1, 1.1) |
| 10 | +0x28 thigh `0x14195c550` | 1 | 1 | dsk_hip: (1, 1, 1) → (1, 1.1, 1.095)<br>sk_thigh_l: (1, 0.9, 0.95) → (1, 1.1, 1.075)<br>sk_thigh_r: (1, 0.9, 0.95) → (1, 1.1, 1.075)<br>dsk_hem_l: (1, 0.94, 0.97) → (1, 1.1, 1.075)<br>dsk_hem_r: (1, 0.94, 0.97) → (1, 1.1, 1.075)<br>dsk_pants_l: (1, 0.94, 0.97) → (1, 1.1, 1.075)<br>dsk_pants_r: (1, 0.94, 0.97) → (1, 1.1, 1.075) |
| 11 | +0x2C calf `0x14195c420` | 1 | 1 | sk_leg_l: (1, 0.9, 0.9) → (1, 1.2, 1.2)<br>sk_leg_r: (1, 0.9, 0.9) → (1, 1.2, 1.2)<br>dsk_leg_l: (1, 0.9, 0.9) → (1, 1.2, 1.2)<br>dsk_leg_r: (1, 0.9, 0.9) → (1, 1.2, 1.2)<br>dsk_knee_l: (1, 1.1, 1.1) → (1, 0.8, 0.8)<br>dsk_knee_r: (1, 1.1, 1.1) → (1, 0.8, 0.8) |
| 12 | +0x30 leg length `0x1407b5fe0` | 1 | 1 | sk_head: (1.019, 1.019, 1.019) → (0.9823, 0.9823, 0.9823)<br>sk_neck: (1.019, 1.019, 1.019) → (0.9823, 0.9823, 0.9823)<br>sk_chest: (1.019, 1.019, 1.019) → (0.9823, 0.9823, 0.9823)<br>sk_belly: (1.146, 1.019, 1.019) → (0.8665, 0.9823, 0.9823)<br>dsk_hip: (0.9978, 1.019, 1.019) → (1.002, 0.9823, 0.9823)<br>sk_thigh_l: (0.9978, 1.019, 1.019) → (1.002, 0.9823, 0.9823)<br>sk_leg_l: (0.9978, 1.019, 1.019) → (1.002, 0.9823, 0.9823)<br>sk_foot_l: (1.019, 1.019, 1.019) → (0.9823, 0.9823, 0.9823)<br>sk_thigh_r: (0.9978, 1.019, 1.019) → (1.002, 0.9823, 0.9823)<br>sk_leg_r: (0.9978, 1.019, 1.019) → (1.002, 0.9823, 0.9823)<br>sk_foot_r: (1.019, 1.019, 1.019) → (0.9823, 0.9823, 0.9823)<br>sk_shoulder_l: (1.019, 1.019, 1.019) → (0.9823, 0.9823, 0.9823)<br>sk_upperarm_l: (1.019, 1.019, 1.019) → (0.9823, 0.9823, 0.9823)<br>sk_forearm_l: (1.019, 1.019, 1.019) → (0.9823, 0.9823, 0.9823)<br>sk_hand_l: (1.019, 1.019, 1.019) → (0.9823, 0.9823, 0.9823)<br>sk_shoulder_r: (1.019, 1.019, 1.019) → (0.9823, 0.9823, 0.9823)<br>sk_upperarm_r: (1.019, 1.019, 1.019) → (0.9823, 0.9823, 0.9823)<br>sk_forearm_r: (1.019, 1.019, 1.019) → (0.9823, 0.9823, 0.9823)<br>sk_hand_r: (1.019, 1.019, 1.019) → (0.9823, 0.9823, 0.9823) |
| 13 | +0x34 head length `0x14195c430` | 1 | 1 | sk_head: (0.9, 1, 1) → (1.1, 1, 1) |
| 14 | +0x38 head width `0x1422fba00` | 1 | 1 | sk_head: (1, 1, 0.9) → (1, 1, 1.1) |
| 15 | +0x3C head depth `0x141889a90` | 1 | 1 | sk_head: (1, 0.9, 1) → (1, 1.1, 1) |
| 16 | h(132/180/220) `0x14195c4b0` | 1 | 1 | dsk_pos_belly_l: (1.05, 0.976, 0.976) → (0.95, 1.048, 1.048)<br>dsk_pos_belly_r: (1.05, 0.976, 0.976) → (0.95, 1.048, 1.048)<br>dsk_sleeve_l: (1, 1.05, 1.05) → (1, 1.1, 1.1)<br>dsk_sleeve_r: (1, 1.05, 1.05) → (1, 1.1, 1.1)<br>dsk_trapezius_l: (1, 1.02, 1.03) → (1, 1, 1)<br>dsk_trapezius_r: (1, 1.02, 1.03) → (1, 1, 1)<br>dsk_clavicle_l: (1, 1.02, 1.03) → (1, 1, 1)<br>dsk_clavicle_r: (1, 1.02, 1.03) → (1, 1, 1)<br>dsk_pectoralis_l: (1, 1.02, 1.03) → (1, 1, 1)<br>dsk_pectoralis_r: (1, 1.02, 1.03) → (1, 1, 1)<br>dsk_scapula_l: (1, 1.02, 1.03) → (1, 1, 1)<br>dsk_scapula_r: (1, 1.02, 1.03) → (1, 1, 1)<br>dsk_collar_l: (1, 1.02, 1.03) → (1, 1, 1)<br>dsk_collar_r: (1, 1.02, 1.03) → (1, 1, 1) |
| 17 | +0x04 weight `0x14195c570` | 1 | 1 | sk_neck: (1, 0.9, 0.9) → (1, 1.025, 1.05)<br>sk_chest: (1, 0.9, 1) → (1, 1.05, 1.05)<br>dsk_clavicle_l: (1, 0.9, 0.9) → (1, 1.05, 1.05)<br>dsk_clavicle_r: (1, 0.9, 0.9) → (1, 1.05, 1.05)<br>dsk_trapezius_l: (1, 0.9, 0.9) → (1, 1.05, 1.05)<br>dsk_trapezius_r: (1, 0.9, 0.9) → (1, 1.05, 1.05)<br>dsk_pectoralis_l: (1, 0.9, 0.9) → (1, 1.05, 1.05)<br>dsk_pectoralis_r: (1, 0.9, 0.9) → (1, 1.05, 1.05)<br>dsk_scapula_l: (1, 0.9, 0.9) → (1, 1.05, 1.05)<br>dsk_scapula_r: (1, 0.9, 0.9) → (1, 1.05, 1.05)<br>dsk_collar_l: (1, 0.9, 0.9) → (1, 1.1, 1.05)<br>dsk_collar_r: (1, 0.9, 0.9) → (1, 1.1, 1.05)<br>dsk_sleeve_l: (1, 0.9, 0.9) → (1, 1.1, 1.1)<br>dsk_sleeve_r: (1, 0.9, 0.9) → (1, 1.1, 1.1)<br>dsk_hem_l: (1, 0.9, 0.875) → (1, 1.05, 1.05)<br>dsk_hem_r: (1, 0.9, 0.875) → (1, 1.05, 1.05)<br>dsk_thigh_l: (1, 0.9, 0.9) → (1, 1.05, 1.05)<br>dsk_thigh_r: (1, 0.9, 0.9) → (1, 1.05, 1.05)<br>dsk_leg_l: (1, 0.9, 0.9) → (1, 1.05, 1.05)<br>dsk_leg_r: (1, 0.9, 0.9) → (1, 1.05, 1.05)<br>dsk_pos_belly_l: (1, 1.1, 1.05) → (1, 1.05, 1.1)<br>dsk_pos_belly_r: (1, 1.1, 1.05) → (1, 1.05, 1.1) |
| 18 | +0x1C waist `0x14195c560` | 1 | 1 | dsk_hip: (1, 0.96, 0.96) → (1, 1.03, 1.05)<br>sk_belly: (1, 0.9, 0.87) → (1, 1.07, 1.11)<br>sk_thigh_l: (1, 0.93, 0.9) → (1, 1, 1)<br>sk_thigh_r: (1, 0.93, 0.9) → (1, 1, 1)<br>dsk_hem_l: (1, 0.93, 0.9) → (1, 1, 1)<br>dsk_hem_r: (1, 0.93, 0.9) → (1, 1, 1)<br>dsk_pants_l: (1, 0.93, 0.9) → (1, 1.05, 1.07)<br>dsk_pants_r: (1, 0.93, 0.9) → (1, 1.05, 1.07) |
| 19 | +0x10 shoulder height `0x14195c530` | 1 | 1 | dsk_clavicle_l: (1, 1, 0.8) → (1, 1, 1.2)<br>dsk_clavicle_r: (1, 1, 0.8) → (1, 1, 1.2)<br>dsk_trapezius_l: (1, 1, 1.2) → (1, 1, 0.9)<br>dsk_trapezius_r: (1, 1, 1.2) → (1, 1, 0.9)<br>dsk_deltoid_l: (1, 1, 0.4) → (1, 1, 1.2)<br>dsk_deltoid_r: (1, 1, 0.4) → (1, 1, 1.2) |
| 20 | +0x14 shoulder width `0x14195c540` | 1 | 1 | sk_chest: (1, 1, 0.975) → (1, 1, 1.025)<br>dsk_clavicle_l: (1, 1, 0.95) → (1, 1, 1.1)<br>dsk_clavicle_r: (1, 1, 0.95) → (1, 1, 1.1) |

#### Table C (entry+0x28 vector, FUN_14195c950), header at VA 0x142a834f0

| # | slider (getter) | a (neg) | b (pos) | bone: neg vec → pos vec (x,y,z) |
|---|---|---|---|---|
| 0 | h2(155/180/210) `0x14195c440` | 1 | 1 | dsk_hem_l: (1.05, 1, 1) → (0.95, 0.98, 0.98)<br>dsk_hem_r: (1.05, 1, 1) → (0.95, 0.98, 0.98) |
| 1 | +0x30 leg length `0x1407b5fe0` | 1 | 1 | sk_chest: (1.019, 1.019, 1.019) → (0.9823, 0.9823, 0.9823)<br>dsk_sleeve_l: (1.019, 1.019, 1.019) → (0.9823, 0.9823, 0.9823)<br>dsk_sleeve_r: (1.019, 1.019, 1.019) → (0.9823, 0.9823, 0.9823) |
