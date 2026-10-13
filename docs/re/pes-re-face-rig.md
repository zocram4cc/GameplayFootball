# PES 2021 face rig, face animation channels, expression, and edit-face composition

Scope: the face rig the engine builds for a match head, the gani channels that drive jaw / eyes /
brows / lids, what (if anything) is blended as a morph, the `face_diff.bin` record layout, and how
the appearance data selects between a real and an edit face. Read-only reverse engineering of the
shipped data (VFS through `build/apps/pesfmt_dump`) and of `PES2021.exe` (memory image
`<scratch>/pesmem.bin`, image base 0x140000000; function bounds from the in-memory `.pdata`, function
disassembly produced by hand with `objdump` at `RVA = VA - 0x140000000`).

Legend: **OBS** = observed (bytes, VFS file, or disassembly named in the text). **INF** = inferred,
with confidence. This report extends `docs/re/pes-re-face.md` (§2 real faces, §3 montage faces) and
does not repeat its live-memory observations; where the two agree both are cited.

Environment notes (why some evidence is weaker than the earlier sessions): the headless Ghidra
script provider was broken this session (`DecompAt.java` → `GhidraScriptLoadException: Failed to
get OSGi bundle`, `DecompAt.py` → `Ghidra was not started with PyGhidra`, both at 04:27–04:57
today). Everything engine-side below is therefore read from raw disassembly with
`<scratch>/FaceRigRE/dis.py`; no Ghidra decompile was used. The game was not running
(`pgrep -f PES2021.exe` empty) and nothing was written to `/proc`.

---

## 0. Headline answers

1. **OBS: the match face rig is two Fox `.ask` skeletons plus a calibration record, not a single
   tree.** The engine loads, by literal path (string table at 0x142b679c0–0x142b67ea8,
   `cpk_dat/` prefix):
   | File | Role |
   |---|---|
   | `common/anime/FoxAnim/Face/CharacterAssets/face_anim_skel.ask` | animation rig, 31 bones |
   | `common/anime/FoxAnim/Face/CharacterAssets/face_render_skel.ask` | render rig, 31 bones |
   | `common/anime/FoxAnim/Face/CharacterAssets/face_skel.frig` | track/constraint rig for the gani unit mapping |
   | `common/anime/FoxAnim/Face/Animations/{Base,Add}/*.gani` | 181 face clips |
   The rig is anchored on **`sk_head`**: `face.skl` (in `common_package.fpk`) binds
   `sk_chest → sk_neck → sk_head` and every `skf_*` bone's position is expressed in the `sk_head`
   frame. The engine applies `face_diff.bin` to that rig with `FUN_1419f1b00`
   (`local += R_head·(d − prev)`, OBS disassembly below).
2. **OBS: a face gani has 32 track units and they are positional, not name-matched.** Unit 0 is
   `RIG_ROOT` (`StrCode32("RIG_ROOT") == 0xd3c3ff90`), units 1–31 are the 31 bones of
   `face_anim_skel.ask` in file order. Every unit carries one rotation track; the 24 "shape" bones
   (brows, nasolabial folds, cheeks, glabella, nose, nosewings, lips) additionally carry a second
   vector track; `skf_jaw`, the four eyelids, the two orbicularis-oculi, `skf_head` and `RIG_ROOT`
   do not. Ranges are given literally in §2.
3. **OBS: no vertex morph targets exist on the character face path.** `face_high.fmdl`,
   `hair_high.fmdl`, `common/facial.fmdl`, `eye(_new).fmdl`, `neck.fmdl` and `mouth.fmdl` carry the
   identical section-0 block inventory (0x0 bones … 0x14, no extra block type), and the exe has no
   character morph/blendshape string (`MorphShape`/`afp-morph` are Scaleform GUI strings;
   `Expression` is a gameplay-data enum). Expression is bone-driven (§2); the *edit* face adds a
   30-bone lattice deformation on top (`skull.bin` + `*FaceData.bin`, `pes-re-face.md` §3.6).
4. **OBS: `face_diff.bin` is a 960-byte real-face rig calibration record** (33 bone offsets, eye
   scale, eye/lid angles, jaw offset, 6 ear transforms), read only on the real-face branch; the
   layout and the exact formula were re-verified here on pack 100117 (max error 3.7e-5 m).
5. **OBS: the appearance record does not choose real vs edit face.** `FUN_141a72250` returns
   whether the head's pack exists; the appearance data chooses *hair, skin tone and montage part
   indices*, never the model class.

---

## 1. The face rig

### 1.1 Which file the engine reads (OBS)

Path strings in the exe image (`<scratch>/Fesmem.bin` at 0x142b679c0 onward; all prefixed
`cpk_dat/`, i.e. a Fox path table + the archive's own folder):

```
cpk_dat/common/anime/FoxAnim/Face/CharacterAssets/face_skel.frig      @0x142b679c0
cpk_dat/common/anime/FoxAnim/Face/CharacterAssets/face_render_skel.ask @0x142b67a10
cpk_dat/common/anime/FoxAnim/Face/Animations/Base/                      @0x142b67a58
cpk_dat/common/anime/FoxAnim/Face/CharacterAssets/face_anim_skel.ask    @0x142b67a90
cpk_dat/common/anime/FoxAnim/Face/Animations/Add/                       @0x142b67ae0
```
The VFS equivalents are `common/anime/FoxAnim/Face/...` (`pesfmt_dump ls`). The rig *bind* itself
additionally comes from `common/face.skl` inside `Asset/model/character/#Win/common_package.fpk`
(49 bones, 3,409 B) — OBS file, INF (high) as the engine's source, because no reference to a
`face.skl` path or hash exists in the image (searched: raw string, StrCode32, StrCode64&48-bit,
`gz_hash`), while the pack mesh (`face_high.fmdl`) does carry the same bone names and positions.
A parallel set of strings loads the hand equivalents
(`FoxAnim/Hand/CharacterAssets/{pes_human_hand_141203.frig,hand_render_skel.ask,hand_anim_skel.ask}`).

### 1.2 Bone list and hierarchy (OBS)

`face.skl` — 49 bones, only three have a parent:

| index | bone | parent | position (m, Fox: +Y up, +Z fwd, +X = character left) |
|---|---|---|---|
| 0 | `sk_chest` | −1 | (0, 1.26277316, 0.013793258) |
| 1 | `sk_neck` | 0 | (0, 1.53205490, 0.033468589) |
| 2 | `sk_head` | 1 | (0, 1.64005995, 0.054332241) |
| 5–7 | `dsk_ear_t_l`, `dsk_ear_b_l`, `dsk_earlobe_l` | −1, −1, 6 | ears (lobe follows the top ear) |
| 8–10 | `dsk_ear_t_r`, `dsk_ear_b_r`, `dsk_earlobe_r` | −1, −1, 9 | mirror |
| … | 34 `skf_*` + `dsk_hair`, `dsk_clavicle_l/r`, `dsk_collar_l/r`, `dsk_neckback`, `dsk_scm` | −1 | model-space positions |

`face_anim_skel.ask` / `face_render_skel.ask` — 31 bones, the actual rig the animation drives:

```
skf_head               (root, local (0,0,0))
├─ skf_jaw             local (−0.00315, −0.02522, 0)
│   ├─ skf_lip_b_l     local (−0.012519, −0.084585, −0.018455)
│   ├─ skf_lip_b_r     local (−0.012519, −0.084585, +0.018455)
│   └─ skf_lip_b_c     local (−0.014883, −0.093261, 0)
├─ skf_brow_o_l, skf_brow_o_r, skf_brow_i_l, skf_brow_i_r
├─ skf_nasolabialfold_t_l/r, skf_orbicularisoculi_b_l/r, skf_glabella
├─ skf_eyelid_{t,b}_{l,r}, skf_nose, skf_nosewing_l/r
├─ skf_cheek_l/r, skf_cheek_s_l/r, skf_lip_{t_c,t_l,t_r,s_l,s_r}
└─ NMdriverB           local (0,0,0)   (normal-map driver, see §2.4)
```
(`skf_head` and `skf_eye_l/r` have no `face.skl` entry; `skf_head` is the anchor, the eyes are the
separate `common/eye(_new).fmdl` model.)

**How the head is parented to the neck (OBS).** `face.skl` and `body.skl` agree exactly on the 15
shared bones: positions equal to ≤ 3.56e-5 m, and `body.skl`'s own chain is
`sk_belly → sk_chest → sk_neck → sk_head` (max position diff 3.6e-5). So the head is
`sk_head` under `sk_neck` under `sk_chest` under `sk_belly` under the body root — the *same*
hierarchy the body uses. The 34 face-only bones hang off `sk_head` at runtime: the animation rig
declares them children of `skf_head` (the ask's root) and every ask local is the head-relative
offset below.

### 1.3 Bind pose: the ask frame is `sk_head`'s frame (OBS, verified numerically)

`face.skl` stores a row-major 3×4 bind matrix per bone; for `sk_head` the rotation part is

```
R = | 0  0 -1 |      local x -> model +Y      model = R · local
    | 1  0  0 |      local y -> model -Z
    | 0 -1  0 |      local z -> model -X
```

Verified on this install (pack 100117, `face.skl` + `face_anim_skel.ask`): for all 30 ask bones
with a `face.skl` entry,

```
ask.local == Rᵀ · ( face.skl[b] − face.skl[sk_head] )          (max error 2e-5 m)
ask.local == Rᵀ · ( face.skl[b] − face.skl[skf_jaw] )           for the three lip_b bones
```
so the *whole* face animation rig lives in `sk_head`'s local frame with `skf_head` at its origin.
`face_render_skel.ask` has the same 31 bones and the same 28 positions; it differs from
`face_anim_skel.ask` only in the three `skf_lip_b_*` positions (render `(0.06133, 0.05959, ±0.01845)`
vs anim `(−0.012519, −0.084585, ±0.01845)`), a 0.144 m disagreement against `face.skl`. **Use
`face_anim_skel.ask`'s positions; the render ask's lip_b values are inconsistent with the mesh.**
(Prior session's §2.3 documents the same frame for the fmdl: `fmdl.local == Rᵀ(p − sk_head) + d`.)

### 1.4 How the engine applies `face_diff.bin` to that rig (OBS, disassembly)

`FUN_141a76e70` (`0x141a76e70-0x141a770ea`) is the real-face calibration entry:

* `rcx` = head object (`HeadUnit`), `rsi` = pointer-to-pointer whose `*rsi` is the skeleton,
  `r8` = the `face_diff.bin` raw bytes.
* `version = *(u32*)(r8+4)`; if it is 5 or 6 → global at `0x1437cdef0 = 0x142aaae40` (the
  33-entry bone-name table) and `HeadUnit+0x118 = 1` (→ the head takes `common/eye_new.fmdl`).
* `boneCount = *(u32*)(r8+0x48)`; the loop at `0x141a77050` walks the u32 offset table at
  `r8+0x50`, and for each record does
  `FUN_1419f1030(*rsi, ORDER[i])` → the bone record, then
  `FUN_1419f1b00(skeleton, bone, &d_i, sk_head_bone)`.
* `FUN_1419f1030` (`0x1419f1030-0x1419f10bc`) is the name lookup: it first scans a hash list at
  `skeleton+0x158..0x160` of `{u32 StrCode32}` keys and then a bone table at
  `skeleton+0x28` with `0xC0`-byte stride, matching `StrCode32(name)` at `boneRecord+0x10`.
* `FUN_1419f1b00` (`0x1419f1b00-0x1419f1be6`) applies one offset:
  * `xmm7 = boneRecord[0x80..0x88]` = the **prev** vector (initially 0),
  * `xmm1 = d` (the face_diff record, `f32[4]`, w = 0),
  * if `r9` (the reference bone) is non-null it calls `0x1419f1130(r9+0x40, &stack, …)` to get that
    bone's rotation matrix and computes `R_ref · (d − prev)`,
  * `boneRecord[0x60] += R_ref·(d − prev)` (the bone's local translation),
  * `boneRecord[0x80..0x88] = d` (stores `d` as `prev`, which makes the call idempotent),
  * the bone's matrix at `boneRecord+0x18` gets `translation = translation − prev + d`.

Equivalently, for an implementer: **local = rest_local + R_head·d**, and because the skl local is
the model-space position (parent −1), the model-space result is
`sk_head_pos + R_head·(ask_local + d)`, identical to the fmdl's bind (verified in §4.2).

The rest of `FUN_141a76e70` pushes face_diff scalars into the owning object:
`HeadUnit+0x100 = R·(jaw offset from +0x3C)` (computed from the object's own matrix at
`+0x460/+0x470/+0x480`), eyeball scale (`+0x8`) into `EyeConstraint+0x80` via
`FUN_141a70aa0 → FUN_141a66090`, and eyelid angles (`+0x2C`) into `EyeConstraint+0xF0` via
`FUN_141a70ad0 → FUN_141a660a0`; eye rotations (`+0x14`, `+0x20`, degrees) are converted with the
`π/180` constant at `0x1425a0c48` (= 0.017453292) and passed to `FUN_141a70a90`.

### 1.5 Coder recipe (real face, no edit path)

1. Rest pose of each `skf_*` bone: `p = sk_head_pos + R_head · ask.local` (R_head from `face.skl`,
   or the matrix above), with `ask.local` from `face_anim_skel.ask`.
2. If the pack has `face_diff.bin`: `p += R_head · d[i]` where `i` is the bone's index in the
   33-name `ORDER` table (§4.2). Bones absent from the table keep `d = 0`.
3. Parent every `skf_*` bone to `sk_head` (its own animation is composed under the head's live
   transform); parent `skf_lip_b_*` to `skf_jaw`; keep `sk_chest → sk_neck → sk_head` as in the
   body rig. `skf_head` itself is the identity anchor at `sk_head`'s origin.
4. Eyes: `common/eye_new.fmdl` when `face_diff.version ∈ {5,6}`, else `common/eye.fmdl`; scale the
   eyeball by face_diff `+0x8` about `skf_eye_l/r`. Mouth: the pack's `oral.fmdl` if present,
   otherwise `common/mouth.fmdl`, placed at `sk_head_pos + R_head·(jawOffset)`.
5. Collapsing onto a body-only rig is exact at rest (all motion is lost): attach `skf_*` to
   `sk_head` and `dsk_clavicle_*`/`dsk_collar_*` to `sk_chest` — `pes-re-face.md` §2.3.

---

## 2. The animation channels a face uses

### 2.1 Clip set and track layout (OBS)

181 clips: `common/anime/FoxAnim/Face/Animations/Base/*.gani` (150) +
`Animations/Add/*.gani` (31), sizes 1.3 KB–89 KB. Names fall into families that map onto the match
event vocabulary: `base_g_*` (in-play: shoot, tackle, sliding, catch, punch), `base_d_*` (drama /
celebration / entrance / manager / card), `angr_*`, `pain_*`, `pow_*`, `loss_*`, `smil_*`,
`neut_*`, `bitter_*`, `dml_*`, `supris_*`, `tire_*`, `gk_*`, `head_*`, `spit_*`; `Add/add_*`
are additive one-shots (`add_eyeblink_single`, `add_kuti*`, `add_mayu_up/down`, `add_me_toji`,
`add_ago_open`, `add_miken_yose`).

A gani is the Fox `MOTION` track set (`magic 0x0BFCA2D2`, TrackHeader at `MOTION+0x40`:
`u32 unitCount, u32 segmentCount, u32 trackId, u8, u8, u32 frameCount, u32 frameRate`), decoded
with the project's existing reader (`src/pes/gani.cpp`, ported from GF `gani.py`). Per unit:
`{u32 nameHash, u8 segmentCount, u8 flags, u16 pad}`, then per segment
`{i32 dataOffset(self-relative), i16 msId, u8 type(low nibble)|nextEntry(high nibble),
u8 componentBitSize}`. Types: 0 quat, 6 quat-diff (12-bit dequant), 3 vector3 / 2 vector2 (16-bit
`AnimHalf` = 128 × fp16), 5 float. Unit flags: 1 loop, 2 hermite, 4 static.

**Unit → bone map (positional, OBS).** Unit 0 = `RIG_ROOT`; units 1…31 = the 31 bones of
`face_anim_skel.ask` in file order. Evidence:
* the face gani unit count is exactly 32 = 1 + 31 ask bones, and unit 0's hash is
  `StrCode32("RIG_ROOT")` in every clip;
* `add_eyeblink_single.gani` animates exactly units 15–18 (`skf_eyelid_t_r/_l`,
  `skf_eyelid_b_r/_l`) plus 9–10 (orbicularis oculi) and nothing else — a blink cannot be anything
  else;
* `pow_brwnit_wink_r.gani` animates the right lid pair (15, 17) far more than the left pair
  (16, 18);
* `base_g_head_kutiake.gani` ("open mouth") rotates unit 2 (`skf_jaw`) by −6.6° about local z;
* the same convention is documented for body ganis (unit 0 = `RIG_ROOT`, then the `.frig` order)
  in GF `tools/pes21_import/retarget.py` `PES_TRACK_MAP`.

The unit name hashes themselves are **not** `StrCode32` of bone names (checked for all 32 × both
hashes against `face.skl`, the ask, body bone names, `strcode32`, `StrCode64&0xFFFFFFFF`,
`gz_hash&0xFFFFFFFF`): only `RIG_ROOT` matches. So the mapping must come from the `.frig`, not
from the hash. `face_skel.frig`'s own records are not bone-name hashes either (all 32 records
checked) — its named records are groups (`Face`, `Eye`, `Eyelid`, `Mouth`, `MirrorL`, `MirrorR`);
it is the constraint/IK rig, and decoding it fully is open (§5).

### 2.2 What each channel does, with ranges (OBS: all 181 clips decoded)

Defaults: in every static clip (`base_normal.gani`, 1 frame) all 32 units are identity
(quat 0,0,0,1; the vector tracks are ~0). `RIG_ROOT` (unit 0) and `skf_head` (unit 1) are
identity in **all 181 clips** — a face clip never moves the head; head motion comes from the body
clip's `sk_head` track.

| unit | bone | rotation track | rotation range (deg, over all clips) | vector track | vector range (raw AnimHalf) |
|---|---|---|---|---|---|
| 2 | `skf_jaw` | quat about local z | **−24.0 … +1.8** (open) | – | – |
| 3/4 | `skf_brow_o_l/r` | quat about local y | **−6.5 … +6.6** (raise/lower) | vec3 | x −118 … +133 |
| 5/6 | `skf_brow_i_l/r` | quat about local y | **−15.8 … +16.8** (frown/raise) | vec3 | x −147 … +141, y ±41, z −102 … +101 |
| 7/8 | `skf_nasolabialfold_t_l/r` | identity in all clips | 0 | vec3 | x ±175, y ±192, z ±148 |
| 9/10 | `skf_orbicularisoculi_b_l/r` | quat about local y | **−5.0 … 0** (squint) | vec3 | x −14 … +98, y −100 … 0 |
| 11/12 | `skf_lip_b_l/r` | quat, 3 axes | up to **68 / 85.5** | vec3 | ±223 |
| 13 | `skf_lip_b_c` | quat, 3 axes | up to **77.3** | vec3 | x ±211, y ±209 |
| 14 | `skf_glabella` | identity in all clips | 0 | vec3 | x −99 … +87 |
| 15/16 | `skf_eyelid_t_l/r` | quat about local z | **−2.4 … +23.1** (upper lid down) | – | – |
| 17/18 | `skf_eyelid_b_l/r` | quat about local z | **−7.0 … +7.4** (lower lid) | – | – |
| 19 | `skf_nose` | identity in all clips | 0 | vec3 | x −29 … +126 |
| 20/21 | `skf_nosewing_l/r` | identity in all clips | 0 | vec3 | x ±126 |
| 22/23 | `skf_cheek_l/r` | identity in all clips | 0 | vec3 | x −88 … +308, y ±80, z ±121 |
| 24/25 | `skf_cheek_s_l/r` | identity in all clips | 0 | vec3 | x ±282, y ±213, z ±127 |
| 26/28 | `skf_lip_t_l/r` | quat, 3 axes | up to **60.0** | vec3 | ±244 |
| 27 | `skf_lip_t_c` | quat, 3 axes | up to **64.3** | vec3 | x ±217, y ±148 |
| 29/30 | `skf_lip_s_l/r` | quat, 3 axes | up to **52.4** | vec3 | x ±249, y ±303 |
| 31 | `NMdriverB` | quat | **120.0** (a 120° rotation about the (0.5,0.5,−0.5) axis) | vec3 | z 0 … 16384 (else 0) |

Timing: `frameCount` from the TrackHeader, `duration = frameCount / 59.94 s` (the project's
existing `PES_FRAME_MS` convention, validated against the body clips). Clip lengths measured:
1 frame (`base_normal`) … 1,065 frames = 17.77 s (`base_d_yusyou_hoe`). `frameRate` low byte is 10
in every face clip, as in the body clips.

Sign convention (OBS, from the ask↔fmdl↔skl agreement): the rotation axis "local z" of the face
rig is model **−x** (the character's right-hand side, horizontal), so positive `qz` on an eyelid
closes it and negative `qz` on the jaw opens it; "local y" on a brow is model **−z** (rearward),
so a brow rotates forward/backward about the horizontal axis.

### 2.3 Who drives the face animation (OBS / open)

* The sequence tables live in `common/anime/FHSequence/bin/`: `Facebase.bin` (65,172 B, 5,431
  12-byte records `{u16 key, u16 pad, u32 id, u16 flags, u16 arg}`; 1,820 distinct keys in
  13…61,941; ids all < 0x10000; flags mostly 0x1c00/0xc400/0x8b60), `Faceadd.bin` (3,672 B, 306
  records), `Sequenceinfo.bin` (30,840 B, 3,855 8-byte records), plus `Handlbase`, `Handrbase`,
  `Hitframes`. All six are loaded in one loop at `FUN_141ed75a0` (OBS disassembly: six path
  strings at 0x142b90578…0x142b90688, each pushed through the resource constructors at
  `0x1403fb410` / `0x1414a8790` and registered with the holder at 0x142b909f0). The record
  semantics are **open** (§5): the u32 field is not a clip-name hash (checked against all 181 face
  clip names and all 4,389 body clip names) and the key range does not match the clip list.
* **Eye aim is per body clip (OBS).** `common/anime/EyesTarget/bin/Eyestarget.bin` is 17,556 B =
  4,389 u32 — exactly the number of body clips in `body_anime_file0..9.mtar` (counted from the
  VFS) — indexed by that archive order. Default value 256 (= no override) for 4,155 entries;
  234 clips carry a target id (0…40; e.g. `dm_oop_faceup_0_0_idle_appeal_notouch_045` → 1,
  `dm_layer_gk_free_lookpost` → 0, `dm_goal_jog_2_4_dashGuts_000` → 2). The named clips are all
  drama/`dm_*`, `block_*` and `backslant_*` clips, i.e. clips whose head should look somewhere
  other than the default.
* A separate eye animation archive exists (`common/anime/FoxAnim/Eye/eye_anime_file0.mtar`,
  256 clips, 3 track units each: `RIG_ROOT` + two unnamed units 0xad010da0 / 0x52db01ef); the
  unit names do not resolve to any face/body bone name (open, §5).
* Live re-confirmation from the prior session (`pes-re-face.md` §5): `EyeConstraint`
  (`CharacterObject+0x48 → FaceController → +0x18`) carries the face_diff eye scale and eyelid
  angles exactly; no edit-face head was loaded.

### 2.4 `NMdriverB` — the second vector track on the shape bones (OBS, semantics partly open)

Every clip's unit 31 is `NMdriverB` with a quaternion track that always sits at
(0.5, 0.5, −0.5, 0.5) — a 120° rotation about the bone's own local z — and a vector track whose
z component is 0 in the neutral clips and up to 3,164/16,384 in expressive ones. INF (medium):
it is the head normal-map animation driver (the head skin shader set includes
`head_normal_animation`, `head_normal_mask`, `head_rotation_mask`), and the 120° quaternion is the
basis in which that parameter is expressed. Do not wire it into the skeleton; treat it as a
material parameter (open).

The per-bone vector tracks on the 24 shape bones (brows, nasolabial folds, cheeks, glabella,
nose, nosewings, lips) are a genuine second channel: they are ~0 in `base_normal` and move
monotonically in expressive clips (e.g. `pow_brwnit_wink_r.gani`, `skf_brow_o_l` x:
+4.6 → −74.6 over frames 0…46). They are read with the same `AnimHalf` decoder as the root
position track. **Scale is unresolved**: at the root-position convention (metres =
AnimHalf/128/1000) the values are sub-millimetre; at AnimHalf/128 they reach ±2.4 m, which is
impossible for a brow. INF (medium): the channel is a small offset/jitter track and the metre
scale is between 1/1280 and 1/12800; treat as 0 when in doubt.

---

## 3. Morph targets / expression: what is actually blended

**Status: closed — there are none on the real-face path.**

* OBS: the section-0 block inventory of every face-related model is identical and contains no
  morph/deform block:
  | model | blocks present |
  |---|---|
  | `face_high.fmdl` (100117) | 0x0 bones(38), 0x1, 0x2, 0x3 meshinfo(7), 0x4, 0x5 bonegroups(4), 0x6 textures(13), 0x7 params(35), 0x8 materials(5), 0x9, 0xa, 0xb, 0xc strings(95), 0xd, 0xe, 0x10, 0x11 lod(56 = 7×8), 0x12, 0x14 |
  | `hair_high.fmdl` | same block types (33 bones, 32 = 4×8 lod) |
  | `common/facial.fmdl` | same block types (33 bones, 1600 verts, 32 = 4×8 lod) |
  | `common/eye_new.fmdl`, `eye.fmdl`, `neck.fmdl`, `mouth.fmdl` | same block types |
  (All read with the project's own `pes/pes/fmdl.cpp` layout constants: bones 48 B,
  `local` at +0x10, `world` at +0x20; block ids from its `Block0` enum. A morph block would be a
  block id outside this set — none exists in any of the six files.)
* OBS: no character morph strings in the image (`MorphShape`, `BitmapDataChannel`, `afp-morph`
  are Flash/Scaleform; `Expression` is a gameplay enum; no `BlendShape`).
* Therefore **all facial expression on a real face is bone animation** (§2.2), plus three
  non-skeletal effects that are not vertex morphs:
  * the eyeball is a separate model scaled per face (`face_diff +0x8`),
  * the eyelid/mouth positions come from the eyelid rotations and the jaw offset,
  * the head shader set carries animated normal maps (`head_normal_animation/ mask/ rotation_mask`)
    driven by the `NMdriverB` channel.
* The **edit** (montage) face adds a 30-bone lattice deformation (`montage/skull/skull.bin`
  4-weight skinning × 1,600 vertices, deformed by `*FaceData.bin` 3×4 matrices) — the montage
  analogue of morph targets, decoded in `pes-re-face.md` §3.6; the patch blend formula and the
  slider→lattice mapping remain open there and are unchanged by this session.

---

## 4. `face_diff.bin` and the appearance table

### 4.1 Layout (OBS, re-verified on pack 100117: `FACE`, version 5, 960 bytes)

| offset | type | field | use (OBS, `FUN_141a76e70` disassembly) |
|---|---|---|---|
| 0x00 | char[4] | `FACE` | – |
| 0x04 | u32 | version (5 in the sampled vanilla packs; 6 selects the same code path) | 5 or 6 → sets the global name table (0x1437cdef0 = 0x142aaae40) and `HeadUnit+0x118 = 1` → `common/eye_new.fmdl` |
| 0x08 | f32[3] | eyeball scale (uniform; 100117: 1.04, 1.04, 1.04) | `EyeConstraint+0x80` (via `FUN_141a70aa0 → 0x141a66090`) |
| 0x14 | f32[3] | eye rotation A, degrees (100117: 0,0,0) | × π/180 (`0x1425a0c48`), → `FUN_141a70a90` |
| 0x20 | f32[3] | eye rotation B, degrees | same call |
| 0x2C | f32[4] | eyelid angles, degrees (100117: 36.33, 36.68, −8.21, −13.87) | `EyeConstraint+0xF0` (via `FUN_141a70ad0 → 0x141a660a0`) |
| 0x3C | f32[3] | jaw offset, model space (100117: 0.00114, 0.01645, −0.01458) | `HeadUnit+0x100 = R_head · off` (mouth/oral placement) |
| 0x48 | u32 | n1 = 33 (bone-offset records) | loop count |
| 0x4C | u32 | n2 = 6 (ear transforms) | not read by this function |
| 0x50 | u32[39] | absolute file offset of each record: 33 × 16 B from 0xF0, then 6 × 32 B from 0x300 | – |
| 0xF0 | f32[4] × 33 | `d[i]`, offset in the **`sk_head` frame**, w = 0 | see 1.4 |
| 0x300 | f32[8] × 6 | `{t.xyz, s.xyz, 0, 0}` for `dsk_ear_t_l`, `dsk_ear_t_r`, `dsk_ear_b_l`, `dsk_ear_b_r`, `dsk_earlobe_l`, `dsk_earlobe_r` (bone-name order INF high) | unused (all identity in 100117) |

`ORDER` (the 33-entry `StrCode32` table at 0x142aaae40 — dumped and resolved against `face.skl`
this session, matches the previous session exactly):
`skf_brow_o_r, skf_brow_i_r, skf_brow_i_l, skf_brow_o_l, skf_orbicularisoculi_b_r,
skf_orbicularisoculi_b_l, skf_eye_r, skf_eye_l, skf_eyelid_b_r, skf_eyelid_b_l, skf_eyelid_t_r,
skf_eyelid_t_l, skf_glabella, skf_nosewing_r, skf_nosewing_l, skf_cheek_r,
skf_nasolabialfold_t_r, skf_nasolabialfold_t_l, skf_cheek_l, skf_cheek_s_r, skf_cheek_s_l,
skf_lip_t_r, skf_lip_s_r, skf_lip_s_l, skf_lip_t_l, skf_nose, skf_lip_t_c, skf_lip_volume,
skf_jaw, skf_lip_b_l, skf_lip_b_r, skf_doublechin, skf_lip_b_c`.

### 4.2 Verification (OBS, this session, pack 100117)

For every one of the 33 `ORDER` bones present in `face_high.fmdl`:

```
fmdl.local[b] == Rᵀ · (face.skl[b] − face.skl[sk_head]) + d[b]
```
max error **3.72e-5 m** (the file's 1e-4 quantisation), 31/31 bones matched. The same relation
holds between `face_anim_skel.ask` and `face.skl` with `d = 0` (max 2e-5 m). So
`fmdl.local − ask.local == d` exactly, and an implementer who reads the fmdl bind and the ask
need only *add* `d` — no recomputation.

### 4.3 How real vs edit is selected (OBS)

* `FUN_141ec53d0(faceId, isReferee)` (`0x141ec53d0-0x141ec5523`): rejects `id == 0`; calls
  `0x141e532b0` (an availability test); masks the id `& 0x800bffff`; picks the format string
  `"referee%03d"` when `isReferee` (string at 0x142b8c3d0) else `"%d"` (0x142b8c334); formats into
  the head dir under `…/face/real/` (string block at 0x142a9bb2e gives
  `/Assets/pes16/model/character/face/real/` and `/Assets/pes16/model/character/face/edithair/`),
  appends `"/face.fpk"` (0x142b8c3e0) and returns `FUN_1414a8d60(path)` — the file-exists test.
* `FUN_141a72250` (`0x141a72250-0x141a72291`): reads `HeadProperty+0x18 → +0x18`, verifies the
  resource type against the type id at `0x1437cddf4`, and returns the byte at
  `((*(void**)(holder)) + 0x10) + 0x58` = the **real-face flag**. `FUN_141a72250`'s sibling
  `FUN_141a72200` returns byte `+0x59` = the **use-edit-hair flag**. Both are cached on the head
  and consumed by the assembler `FUN_141a7b020` (slot table in `pes-re-face.md` §2.2): real →
  pack `face_high.fmdl` + `face_diff.bin`; otherwise → `common/facial.fmdl` + a `Montage` object.
* Killed earlier and re-confirmed by the code path above: **no appearance field selects the model
  class.** The decision is "does the pack exist", tested at `0x141ec53d0`'s tail.

### 4.4 What the appearance record *does* select (OBS map, prior session; INF binding)

`PlayerAppearance.bin` (`common/character0/model/character/appearance/`, 60-byte records
`{u32 playerId, u8 body[56]}`), identical to bytes 0x0C–0x43 of the EDIT save's 72-byte entry
(PA offset = EDIT offset − 8). Head-relevant fields (bit offsets are in the EDIT numbering):

| field | drives |
|---|---|
| `0x2D:0` 3 bits skin colour 1–6 | `FaceSKIN<c><tt>Montage` textures, `arm_<c>_bsm`, `thigh_<c>_bsm`, `head_<c>_srm` |
| `0x2D:3` cheek type, `0x2E:0` forehead type, `0x2E:3` facial-hair type, `0x2F:0` laughter lines, `0x2F:3` upper lid, `0x30:0` lower lid, `0x32:0` eyebrow type, `0x34:0` nose, `0x35:0` upper lip, `0x35:3` lower lip | montage `FaceParts*.bin` indices (`FUN_14198ed30`: EyelidU ≤ 8, EyelidL ≤ 7, Cheek 0→XX / 1–3→00 / 4→01, Nose 0→XX / 1–7, MouthU/B 0→XX / 1–4, UnderChin 0→XX / 1–3) |
| `0x16:4…0x2C:4` (eight 4-bit face sliders) | the 27 montage slider pairs → the 30-bone lattice |
| `0x30:4/0x31:0/0x31:4` ear length/width/angle | `dsk_ear*` FaceData |
| `0x35:6/0x36:4/0x37:2` hair RGB, `0x38:0` hairstyle, `0x38:0`… `0x43:4` hair length/wave/variation/front/combed/cropped/hairline/forehead | the edit-hair model/shell/parts names resolved into `HeadProperty+0x30/+0x38/+0x40/+0x48` and the `HairColor` material vector |
| `0x40:0` iris colour | `eye_color_sub_*` (eye shader §3.9) |
| `0x04:0` "edited face" bit | INF (medium): gates the *slider* path, not the model class |

**For `src/gdext/pes_character.cpp`:** the appearance table should be used exactly as it already is
for the body (`pes::DecodeLook` → skin colour, boots, colours) and **not** to choose the face model.
The face model choice is `Asset/model/character/face/real/<id>/#Win/face.fpk` exists (with the
`referee%03d` variant for officials). Concretely:
1. `face_id` masked `& 0x800bffff`; `id == 0` → no face pack;
2. pack exists → real-face recipe (§1.5); else → `common/facial.fmdl` + montage inputs (currently
   out of scope for GF: a body-only rig collapses `skf_*` onto `sk_head`, which is exact at rest);
3. skin colour from the appearance record picks `arm_<c>_bsm` / `thigh_<c>_bsm` / `head_<c>_srm`
   (a face folder's own `arm_bsm.ftex` still wins, as the existing code already does);
4. `face_diff.bin` version 5/6 ⇒ the eye model is `common/eye_new.fmdl`, else `common/eye.fmdl`.

---

## 5. Open items and what was tried

| Item | Status | Tried / next step |
|---|---|---|
| `.frig` (track/constraint rig) record format | open | All 32 records of `face_skel.frig` parsed (`{u32,u32,i32,u32,u32,u32,u32,u32}` @0xB0, 0x20 stride, plus a group record format `{u32 0x20, u32 childCount, u32 childOffsets[n], u32 nameHash}` near 0x4C0 with the names `Face/Eye/Eyelid/Mouth/MirrorL/MirrorR`); no field equals `StrCode32` of any bone name, so it is not a bone list. It is the animation-constraint/IK rig used to order the gani units (the positional map in §2.1 is validated instead). Next: find the loader of `face_skel.frig` and dump its record parser. |
| `Facebase.bin` / `Faceadd.bin` / `Sequenceinfo.bin` record semantics | open | 12-byte `{u16 key, u16, u32 id, u16 flags, u16 arg}` records; the u32 is not `StrCode32`/`StrCode64`/`gz_hash` of any face-clip or body-clip name; the keys span 13…61,941 with 1,820 distinct values and do not index the 181 clip list. Loader located (`FUN_141ed75a0`, six paths at 0x142b90578–0x142b90688). Next: decompile the record parser (Ghidra scripting is currently broken; use `objdump`). |
| Eye animation archive (`eye_anime_file0.mtar`) | open | 256 clips × 3 units; unit hashes 0xad010da0 and 0x52db01ef match no bone name under any hash variant; 0/256 clip names resolve from `anim_infos.json`. The `Eyestarget.bin` per-clip target id is closed (§2.3). |
| The per-bone vector track's metre scale (§2.4) | open | Decoded with the gani `AnimHalf` reader; at the root-position convention (×1/128 → mm) the values are sub-mm in the base clip and up to ±2.4 m at face value in expressive clips (impossible), so the truth is between ×1/1280 and ×1/12800. Next: find the consumer (`ModelMontage`/`SkullMove` users, or the face-rig pose applier) and read its multiply. |
| Edit-face heads were never observed live | open (unchanged from `pes-re-face.md`) | No head had the real-face flag 0 in either observed live state. The montage recipe stays static-only. |
| `NMdriverB` semantics | open | Quat fixed at (0.5,0.5,−0.5,0.5) = 120° about local z; vector z 0…16384. INF (medium): head normal-map animation parameter. Next: find its consumer in the head material update path. |

Reproduction (all under `<scratch>/FaceRigRE/`, deleted after the report): `fanistats.py` (per-unit
channel statistics over the 181 face clips, using GF `tools/pes21_import/gani.py`),
`fanitable.py` (per-clip table), `fmdlbones.py`/`fblocks.py` (fmdl bones and block inventory),
`dis.py` (function disassembly from `<scratch>/pesmem.bin`), `fpkx.py` (fpk/fmdl inspection).
