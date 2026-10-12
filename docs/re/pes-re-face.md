# PES 2021: how a player's head is built (real-face packs and edit/montage faces)

Scope: read-only reverse engineering of the shipped data (VFS through `pesfmt_dump`) and of `PES2021.exe` (memory image `<scratch>/pesmem.bin`, image base 0x140000000; function bounds from the in-memory `.pdata`), plus reads of the live process (`/proc/<pid>/mem`, pid <pid>; nothing written). Decompiles are private headless Ghidra runs (`<scratch>/CutsceneRE/bin/dec.py`; output in `<scratch>/FaceHeadRE/dec/<va>.c`). Evidence scripts are in `<scratch>/FaceHeadRE/`: `fm.py` (fpk/fmdl/skl readers), `fdiff.py` (face_diff parser and the bone check), `mb.py` (montage stream files), `parts.py`, `skull.py`, `pa.py` (PlayerAppearance fields), `live*.py`.

The extracted PES files used by these scripts (`common_package.fpk`, the `montage/*.bin` files saved as `m_*.bin` / `mp/`, the vanilla `PlayerAppearance.bin`) were deleted after decoding. To rerun, re-extract them with `pesfmt_dump cat <VFS path>`; the vanilla appearance file is `Data/dt00_x64.cpk` → `common/character0/model/character/appearance/PlayerAppearance.bin`.

Legend: **OBS** = observed (decompile, bytes, or live memory; the method is named). **INF** = inferred, with a confidence.

Coordinates are Fox model space: metres, +Y up, +Z forward, +X = the character's left.

---

## 0. Headline answers

1. **OBS: `face_diff.bin` is not an edit-face file.** It exists only inside real-face packs (`face/real/<id>/#Win/face.fpk`), and the game reads it only on the real-face branch of the head loader `FUN_141a7b020`. It is a 960-byte rig-calibration record:
   - 33 facial-bone offsets that move the generic face rig (`face.skl`) onto that scanned head;
   - the eyeball scale, the eye rest rotations and the eyelid angles (these go to `EyeConstraint`);
   - the jaw offset used to place the mouth/oral model;
   - 6 ear-bone transforms.

   Its 33 offsets equal the scanned head's own skf-bone positions in `face_high.fmdl` to within 4e-5 m on 16 of 16 vanilla packs.
2. **OBS: edit/generic faces use the montage system.** Inputs:
   - the common mesh `common/facial.fmdl` (front face, 1,600 vertices) plus `common/neck.fmdl`;
   - a shape (`montage/skin/Skin_<colour>_<type>.bin`) and 7 per-region shape patches (`montage/parts/FaceParts*.bin`), mapped onto the vertices through `montage/parts/parts.bin`;
   - a 30-bone lattice deformation (`montage/skull/skull.bin`) driven by a FaceData pose (`montage/skull/preset/*FaceData.bin`, or a per-player FaceData);
   - face-rig bones re-anchored to mesh vertices (`montage/facebonepoint.bin`);
   - hair from `face/edithair/model/<style>/#Win/face.fpk`;
   - a composited skin texture `montage/face_skin/FaceSKIN<colour><type>Montage.ftex` plus layers.

   Every file format is decoded (§3). The patch blend formula and the slider → lattice mapping are only partly closed (§3.6, §6).
3. **Skin tone and hair (OBS + INF):**
   - The appearance record (PlayerAppearance.bin, or the EDIT save's 72-byte entry) carries skin colour 1–6, the hair fields and the RGB colours.
   - The runtime `HeadProperty` holds the resolved hair model name, shell texture, parts texture and hair colour as floats.
   - The skin colour picks `FaceSKIN<c>..`, `arm_<c>_bsm`, `thigh_<c>_bsm`, `head_<c>_srm` (§4).
4. **Live (OBS):** two states were observed (§5).
   - In a 25-character match, every head was a real-face head.
   - The face_diff values (eye scale, eyelid angles, jaw offset) were read back in live memory exactly where §2.4 says they go.
   - No edit-face head was loaded in either observed state, so the montage recipe is static-only.

---

## 1. Assets involved

| Asset (VFS path, `Asset/model/character/…`) | Role |
|---|---|
| `face/real/<id>/#Win/face.fpk` | Real-face pack. Members (census over 3,566 winning packs, `<scratch>/fpk_inner.tsv`): `face_diff.bin` (always), `face_high.fmdl`, `hair_high.fmdl`, `oral.fmdl` (183 packs). Vanilla `dt36_g4.cpk` has 2,123 packs: 1,955 have diff+face+hair, 133 have diff+face+hair+oral. The 528 diff-only packs and other odd combinations are all 4cc mod packs. |
| `face/real/<id>/sourceimages/#windx11/*.ftex` | Pack textures: `face_bsm_alp`, `face_nrm`, `face_srm`, `face_trm`, `hair_parts_{bsm_alp,nrm,srm,trm}`, `eye_occlusion_alp`, optional `arm_bsm` (overrides the arm/hand skin, see kit report §7.2) and `eye_sclera_bsm` (exe string `/sourceimages/eye_sclera_bsm.ftex`). Older/mod packs use `face.ftex`, `head.ftex`, `hair*.ftex`. |
| `#Win/common_package.fpk` → `common/face.skl` | Face skeleton, 49 bones. 15 are shared with `body.skl` (`sk_chest`, `sk_neck`, `sk_head`, `dsk_clavicle_l/r`, `dsk_ear_{t,b}_{l,r}`, `dsk_earlobe_{l,r}`, `dsk_collar_l/r`, `dsk_neckback`, `dsk_scm`), with identical matrices (max diff 3.6e-5). 34 exist only here: `dsk_hair` and 33 `skf_*`. All non-chain bones have parent −1. |
| `common/facial.fmdl` | Montage face (shader `pes_3ddf_skin_montage`). 4 meshes: 64 / 40 / 156 / 1,340 vertices (skin patch, eye occlusion, eyelashes, skin). 33 bones (`skf_*` + `sk_neck`, `sk_head`). |
| `common/neck.fmdl` | Montage neck + back of head (`skin_head`, 630 vertices) and `collar_ao`. Bones `sk_chest…dsk_*`, `skf_jaw`. |
| `common/eye.fmdl`, `common/eye_new.fmdl` | Eyeballs (`skf_eye_l/r`). |
| `common/mouth.fmdl` | Teeth/tongue (`oral_mat`), used when the pack has no `oral.fmdl`. |
| `face/montage/…` | Montage data (§3): `skull/skull.bin`, `skull/preset/{resetFaceData,baseFaceData1}.bin`, `parts/parts.bin`, `parts/FaceParts*.bin` (40), `skin/Skin_<1-6>_<00-05>.bin` (36), `facebonepoint.bin`, `vexport.bin`, `texproj.bin`. Textures: `face_skin/FaceSKIN<1-6><00-05>Montage{,Nml}`, `eyebrow/Eyebrow<N>{Thin,Middle,Thick}`, `eyelid/Eyelid{U,L}<NN>C<c>`, `beard/Beard<N>`, `forehead/Forehead<N>C<c>`, `wrinkles/Wrinkles0{1,2}`, `chin/cleft_chin`, `mark`. |
| `face/edithair/model/<style>/#Win/face.fpk` | Edit hair (896 styles). Each holds one `hair_high.fmdl` with a scalp skin mesh (`fox_skin_edit_mat`), hair cards (`fox_hair_mat`) and a head shell (`fox_head_shell_edit_mat`). Texture slots are empty in the fmdl, so they are bound at runtime. |
| `face/edithair/shell/#windx11/<shell>_bsm_alp.ftex`, `face/edithair/parts/#windx11/<base>_sb<N>_bsm_alp.ftex` + `<base>_{nrm,srm,trm}.ftex` | Edit-hair textures (§4). |
| `face/common/#windx11/` | `head_color_{w,y,o,r,d,b}`, `head_{1..6}_srm`, `head_translucent_1`, `head_normal_{animation,detail,mask}`, `head_rotation_mask`, `EyeOcclusion00..03`, `face_grass_*`. All are hard-coded paths in the exe string table at 0x142aa9c28–0x142aaa480. Their binding was not traced. |

---

## 2. Real-face heads

### 2.1 Selection (OBS)
- `FUN_141ec53d0(id, isReferee)` builds `<real dir>/%d/face.fpk` (id masked `& 0x800bffff`) or `referee%03d/face.fpk`, then calls the file-exists test `FUN_1414a8d60`.
- The runtime `HeadProperty` object (§5, holder at `Head.property+0x18 → +0x18`) carries:
  - `+0x10` the fpk path and `+0x18` the id string;
  - `+0x68` u8 **real-face flag** (`FUN_141a72250`) and `+0x69` u8 **use-edit-hair flag** (`FUN_141a72200`).
- **INF (high):** "real face" = that fpk exists.
  - **Killed hypothesis:** a dedicated "real face" bit in PlayerAppearance.bin. No field separates the 1,985 real-face ids from the 15,797 others (bit census over every documented and unknown field, `pa.py`).

### 2.2 Head assembly: `FUN_141a7b020` (OBS, `dec/141a7b020.c`)

Member names come from the table at 0x143517ae0: `face_high.fmdl`, `hair_high.fmdl`, `face_edithair_high.fmdl`, `face_diff.bin`, `oral.fmdl`, `fcl_hair.fmdl`, `fcl_hair_sim.skl`, `fcl_hair_sim.fclo`. Common models are looked up by `StrCode64(bare stem) & 0xFFFFFFFFFFFF` (resolved: 0x6da0591dc14e `facial`, 0x548468701e80 `neck`, 0x29278658a049 `eye`, 0xc52853a24c2e `eye_new`, 0x7175e13d2373 `mouth`; 0xf93ca219a4a9 is loaded first and stays unresolved).

| Slot | Real face (`+0x68 == 1`) | Edit face (`+0x68 == 0`) |
|---|---|---|
| face | pack `face_high.fmdl`, or `face_edithair_high.fmdl` when `+0x69`; then `face_diff.bin` → `FUN_141a76e70` | `facial` + a `Montage` object (`FUN_14198a3e0`) |
| hair | `+0x69 == 0`: pack `hair_high.fmdl`; else `hair_high.fmdl` from the edit-hair package | edit-hair package `hair_high.fmdl` |
| neck | not loaded (`face_high` contains the neck and `collar_ao`) | `neck` |
| eyes | `eye_new` if face_diff version is 5 or 6 (sets HeadUnit `+0x118`), else `eye` | `eye` (INF medium: `+0x118` is only set by face_diff) |
| mouth | pack `oral.fmdl`, else `mouth` | `mouth` |
| cloth hair | `fcl_hair.fmdl` + `fcl_hair_sim.skl` + `fcl_hair_sim.fclo` from the pack, only if all three exist | — |

### 2.3 Skeleton and skinning (OBS)
- `face_high.fmdl` bones (vanilla 100117, 38 bones): `sk_chest`, `sk_neck`, `sk_head`, `dsk_clavicle_l/r`, `dsk_neckback`, `dsk_scm`, and 31 `skf_*`. `hair_high` uses a subset; the hair cards are skinned to `sk_head` only.
- All of these names are in `face.skl`. The body-shared ones sit at the `body.skl` positions.
- **Bone positions in the fmdl:** for every `skf_*` the fmdl bone `local` field (record +0x10) is the position **relative to `sk_head`, in `sk_head`'s frame**. The `world` field (+0x20) is a constant (0, 1.0961, 0) and is not a usable position.
  - **Killed hypothesis:** "the fmdl `world` field is the model-space bind position" (constant for every skf bone).
- **`sk_head` bind rotation** from `face.skl` (row-major 3×4, rotation columns = local axes in model space): local x → model +Y, local y → model −Z, local z → model −X. As a matrix, `R = [[0,0,-1],[1,0,0],[0,-1,0]]`, so `model = R·local`.
- **LODs:** inside the fmdl, there are no separate LOD files.
  - Block 16 (LOD info) starts with u32 `lodCount` = 8.
  - Block 17 has `meshCount × lodCount` entries of `{u32 firstIndexOffset, u32 indexCount}`, mesh-major. LOD 0 equals the mesh-info index range.
  - Vanilla `face_high` repeats LOD 0 for all 8 LODs. `hair_high` LOD ≥ 1 drops a few triangles (mesh 0: 1,722 → 1,716 indices).
- **Recommendation for a body-only rig (GodotFootball has no face rig):**
  - skin `skf_*`, `dsk_hair`, `dsk_ear*`, `dsk_neckback`, `dsk_scm` to `sk_head` (or through the existing helper-collapse table);
  - skin `dsk_clavicle_*` and `dsk_collar_*` to `sk_chest`.

  The mesh is in model space at rest, so the result is exact at rest. Only facial animation is lost.

### 2.4 `face_diff.bin` layout, version 5, 960 bytes (OBS)

| Offset | Type | Field | Consumer (OBS: `FUN_141a76e70` decompile + live read) |
|---|---|---|---|
| 0x00 | char[4] | `FACE` | — |
| 0x04 | u32 | version. 5 in all vanilla packs sampled (40/40). 944/976-byte 4cc packs exist (8 total), versions not decoded. | version 5 or 6 → bone-name table 0x142aaae40 and HeadUnit `+0x118 = 1` (→ `eye_new`) |
| 0x08 | f32[3] | eyeball scale (uniform; vanilla 0.98–1.20) | `EyeConstraint+0x80` (OBS live: 75201 → 1.1307, 75415 → 1.3) |
| 0x14 | f32[3] | eye rotation A (degrees; vanilla y ∈ {0, ±1…±6.9}) | × π/180 (constant at 0x1425a0c48) → `FUN_141a70a90(EyeConstraint, A, B)` |
| 0x20 | f32[3] | eye rotation B (degrees; mirror of A in all samples) | same call |
| 0x2C | f32[4] | eyelid angles in degrees; vanilla [20.4–35.7, 20.3–35.6, −20.3–0.5, −19.8–1.2]. INF (medium): upper L, upper R, lower L, lower R. | `EyeConstraint+0xF0` (OBS live, exact) |
| 0x3C | f32[3] | jaw offset, model space. **Equals `R·d[skf_jaw]`** (max error 2.6e-6, 60 vanilla packs) | rotated by the inverse `sk_head` matrix → HeadUnit `+0x100` (mouth/oral placement). OBS live: 75201 file (0, −0.0018, 0.001) → +0x100 (−0.0018, −0.00099, 0) = `Rᵀ·off` |
| 0x48 | u32 | n1 = 33 (bone offsets) | loop count |
| 0x4C | u32 | n2 = 6 (ear transforms) | not read by `FUN_141a76e70` |
| 0x50 | u32[n1+n2] | absolute file offsets of each record (33 × 16 B from 0xF0, then 6 × 32 B from 0x300) | — |
| 0xF0 | f32[4] × 33 | `d[i]` = offset in **`sk_head` local frame** (w = 0) for bone `ORDER[i]` | for each bone present in the skeleton: `FUN_1419f1b00(skel, bone, d, sk_head)` adds `R_head·(d − prev)` to the bone translation and stores `d` as `prev` (so it is idempotent) |
| 0x300 | f32[8] × 6 | `{t.xyz, s.xyz, 0, 0}` for `dsk_ear_t_l, dsk_ear_t_r, dsk_ear_b_l, dsk_ear_b_r, dsk_earlobe_l, dsk_earlobe_r` (bone table at 0x142a8a9a8; INF high) | always identity (0,0,0 / 1,1,1) in 60/60 vanilla samples |

**`ORDER`** (StrCode32 table at 0x142aaae40, resolved against `face.skl` names):
`skf_brow_o_r, skf_brow_i_r, skf_brow_i_l, skf_brow_o_l, skf_orbicularisoculi_b_r, skf_orbicularisoculi_b_l, skf_eye_r, skf_eye_l, skf_eyelid_b_r, skf_eyelid_b_l, skf_eyelid_t_r, skf_eyelid_t_l, skf_glabella, skf_nosewing_r, skf_nosewing_l, skf_cheek_r, skf_nasolabialfold_t_r, skf_nasolabialfold_t_l, skf_cheek_l, skf_cheek_s_r, skf_cheek_s_l, skf_lip_t_r, skf_lip_s_r, skf_lip_s_l, skf_lip_t_l, skf_nose, skf_lip_t_c, skf_lip_volume, skf_jaw, skf_lip_b_l, skf_lip_b_r, skf_doublechin, skf_lip_b_c`.

**Validation (OBS, `fdiff.py check`):** `fmdl.local[b] == Rᵀ·(faceSkl[b] − faceSkl[sk_head]) + d[b]` for every one of the 33 bones present in `face_high`/`hair_high`/`oral`. Over 16 vanilla packs the error stays ≤ 3.73e-5, which is the file's 1e-4 rounding.

Killed hypotheses:
- "face_diff = per-vertex deltas or morph parameters for edit faces": it holds 33 bone vectors plus eye and jaw scalars, and it is read only on the real-face path.
- "the offsets are in model space": left/right pairs mirror in z, not x. They are in the `sk_head` frame.

**4cc observation (OBS live + file):** packs 75415 and 75223 set the jaw offset to (0, −20.0, −0.006). That pushes the mouth model 20 m down and hides it. They ship no or empty `face_high`, so the head is drawn by the pack's `hair_high` (or by nothing).

### 2.5 Real-face recipe

1. Mount the VFS. Path = `Asset/model/character/face/real/<id>/#Win/face.fpk`; referees use `face/real/referee%03d/…`. If it is missing → §3.
2. Load `face_high.fmdl` and `hair_high.fmdl`. Load `oral.fmdl`, or `common/mouth.fmdl` from `common_package.fpk` if the pack has none. Load `common/eye_new.fmdl`, or `eye.fmdl` if face_diff version ∉ {5, 6}. Load no `neck.fmdl`.
3. Resolve textures by the fmdl directory/name:
   - `/Assets/pes16/model/character/face/real/<id>/sourceimages/<n>.dds` → `Asset/model/character/face/real/<id>/sourceimages/#windx11/<n>.ftex`;
   - `common/sourceimages/*` → `Asset/model/character/common/sourceimages/#windx11/*.ftex`.
   - Bind the pack's `arm_bsm.ftex` to the body `arm_mat` when present.
4. Build the face rig. Start each `face.skl` bone at its rest position. For each `face_diff` bone: `pos = rest + R_head·d`. This is identical to taking `sk_head + R·fmdl.local`.

   Without a face rig, collapse the skf bones onto `sk_head` (§2.3). Then skip step 4: the mesh is already in model space.
5. Place the mouth/oral model at `+ off` (face_diff 0x3C), following `sk_head`.
6. Scale the eyeballs by `scale` about `skf_eye_l/r`. **INF (medium):** `EyeConstraint` applies it. Not traced further.

   The eye and eyelid angles only matter for eye/blink animation.
7. Use mesh LOD 0. Skip the antiblur/outline duplicates (4cc), as in GodotFootball `pack_rules`.

---

## 3. Edit (montage) faces

### 3.1 Runtime inputs (OBS)

**`FUN_14198ed30(montage, req, …)`** queues the loads. Path base = the FaceManager montage dir (`/Assets/pes16/model/character/face/montage/`):

| Request field | File |
|---|---|
| — | `skull/skull.bin` |
| `req+0x10` (u32 skull id) | `FUN_14198cde0` → `FUN_1419db5b0`: `type = id >> 30`, `fake = (id >> 29) & 1`, `idx = id & 0x1FFFFFFF`. `type 0` → `skull/preset/` + preset table 0x143516980 (32 names). `type 1/2` → `skull/player/[referee/]Id%dFaceData.bin` (`Id%dFakeFaceData.bin` if fake). |
| — | `parts/parts.bin` |
| `req+0x20` (colour), `req+0x24` (type) | `skin/Skin_<colour 0→1>_<type %02d>.bin`. Colour < 7, type < 6, else the default resource. |
| `req+0x28` | `parts/FacePartsEyelidU%02d.bin` (`v+1`, v < 8) |
| `req+0x2C` | `parts/FacePartsEyelidL%02d.bin` (`v+1`, v < 7) |
| `req+0x30` | Cheek: v = 0 → `XX`, 1–3 → `00`, 4 → `01` (`FUN_14198c740`) |
| `req+0x34` | Nose: 0 → `XX`, 1–7 → `%02d` = v |
| `req+0x38` | MouthU: 0 → `XX`, 1–4 → `%02d` = v−1 |
| `req+0x3C` | MouthB: same rule as MouthU |
| `req+0x44` | UnderChin: 0 → `XX`, 1–3 → `%02d` = v−1 |
| — | `facebonepoint.bin` |
| `+0x68/+0x6C/+0x70` | passed to `FUN_14198f700` (texture/colour; not traced) |

Notes:
- **Presets:** the VFS only has `resetFaceData.bin` and `baseFaceData1.bin`. The 31 named presets (`FRA0114FaceData.bin` …) and every `skull/player/*` file are absent from this install (`allpaths` grep). PES then gets the default resource (`FUN_141b33670`). **INF (medium):** the shape comes out as the reset pose.
- **Editor job:** `MontageSkullJob` (`FUN_14198b570`) uses the same files plus `baseFaceDataZ%02d.bin` (also absent), and 27 slider pairs (`param+0x28C`, `{f32, f32}` × 27). They are written into a 44-entry lattice-control table at 0x143516ee8 (`FUN_1419d9b50`).
- **Slider table labels**, slider id → lattice bone:

  | Slider id | Lattice bone | Label |
  |---|---|---|
  | 0 | `bone_01` | eyebrow_c |
  | 1 | `bone_03` | nose |
  | 2 | `bone_17` | lip_l |
  | 3 | `nose_r` | — |
  | 4 | `bone_18` | eyebrow_out_l |
  | 5 | `bone_20` | cheekbone_l… |
  | 12 | `bone_28` | eye_out_l |
  | 13–26 | — | the right-side mirrors |
  | 15 | `bone_16` | lip |

  Full dump script in the session log; the region label is the 3rd column of the table.

### 3.2 `facial.fmdl` vertex space (OBS)
All montage files index the 1,600 `facial.fmdl` vertices in **mesh order concatenated: mesh 0 (64), mesh 1 (40), mesh 2 (156), mesh 3 (1,340)**. Proof: `skull.bin` stores 2,840 triangles, the same set (up to rotation) as the concatenated fmdl index buffers (`skull.py`).

### 3.3 File formats (all OBS from bytes; offsets absolute unless stated)

**Shape stream files** (`Skin_*.bin`, `FaceParts*.bin`). Read by `FUN_1419d5b40`:

| Offset | Content |
|---|---|
| header (0x00) | `u32 headerSize = 0x10, u32 streamCount = 4, u32 recordStride = 12, u32 vertexCount` |
| records (from 0x10) | `{u32 dataOffset, u32 usage, u32 format = 5 (f32x3)}` |
| data | each stream is `vertexCount × f32[3]` |

| Usage | Stream |
|---|---|
| 2 | position |
| 3 | normal |
| 0x10 | tangent |
| 0x0F | bitangent |

The usages are stored to patch +0x10/+0x18/+0x20/+0x28. Names INF (high) from the values: unit vectors, x- / y-dominant.

Vertex counts:

| File | Vertices |
|---|---|
| Skin | 1,712 |
| EyelidU | 384 |
| EyelidL | 260 |
| Cheek | 133 |
| Nose | 205 |
| MouthU | 193 |
| MouthB | 209 |
| UnderChin | 163 |

`Skin_<c>_<t>.bin` is byte-identical across c (`cmp`). Geometry depends only on t; c selects the texture.
- **Killed hypothesis:** "Skin's first index is a geometry variant".

**`parts.bin`:**
- Header `{u32 12, u32 9, u32 28}`.
- 9 records `{u32 n, u32 nameOff, u32 offA, u32 offW, u32 0, u32 offC, u32 offD}`; names are absolute offsets.
- Parts, in order: `_ALL_` (n = 1,813), `bone_face` (627), `bone_eyes_u` (443), `bone_eyes_b` (301), `bone_nose` (205), `bone_cheek` (133), `bone_mouth_u` (193), `bone_mouth_b` (209), `bone_underchin` (163).
- Arrays (`n` each):

  | Array | Content |
  |---|---|
  | `A` | u32 facial vertex index (0–1,599) |
  | `W` | f32 weight (0, 1] |
  | `C` | u32 (unknown; max 1,569) |
  | `D` | u32 index into the matching stream file (`_ALL_` → Skin, 0–1,711; nose → 0–204 sequential) |

- **Part ↔ file:**

  | Part | Stream files |
  |---|---|
  | `_ALL_` | Skin |
  | `bone_eyes_u` | EyelidU |
  | `bone_eyes_b` | EyelidL |
  | `bone_nose` | Nose |
  | `bone_cheek` | Cheek |
  | `bone_mouth_u` | MouthU |
  | `bone_mouth_b` | MouthB |
  | `bone_underchin` | UnderChin |

  `bone_face` has no file of its own. INF: it is a mask.

  Check: `facial[A] ≈ stream[D]` to within 1–7 mm for every file (`parts.py`; 77 files).
- **Killed hypothesis:** "facial.fmdl is the composite of the `XX` parts" (Nose XX max 4.8 mm off).

**`skull.bin`:**

| Offset | Content |
|---|---|
| 0x00 | `{u32 12, u32 0x108, u32 0x11C}` |
| 0x0C | `{u32 12, u32 boneCount = 30, u32 8}` |
| 0x18 | 30 × `{u32 nameOff, u32 dataOff}`, both relative to 12. Names `bone_00…bone_29`. Data = f32[12] row-major 3×4 **inverse bind**; `−translation` = lattice point. |
| 0x108 | `{u32 vertexCount = 1600, u32 offW, u32 offJ, u32 offMirrorFlag, u32 offMirror}`, offsets relative to 0x108 |
| W | f32[4] per vertex; sums to 1 |
| J | u32[4] lattice indices 0–29 |
| C | u32 per vertex, mostly 0 (unknown) |
| mirror | u32 per vertex, a permutation of 0–1,599: the left/right mirror vertex |
| 0x11C | `{u32 triCount = 2840, u32 off}` (relative to 0x11C) → u32[3] triangles |

- **Killed hypothesis:** "all skull.bin offsets are relative to 12"; the vertex block is relative to 0x108.

**`*FaceData.bin`** (`resetFaceData`, `baseFaceData1`; per-player files have the same layout, INF high):
- `{u32 12, u32 count = 40, u32 8}`, then 40 × `{u32 nameOff, u32 dataOff}` (absolute).
- Each record is f32[24]: `M` 3×4 row-major (world transform), `t` xyz, pad, `s` xyz, pad, `r` xyz (degrees), pad.
- Names: `bone_00, bone_01, bone_03 … bone_28` (no `bone_02`, `bone_29`), `dsk_ear_b_l/r`, `dsk_ear_t_l/r`, `dsk_earlobe_l/r`, `eyeball_l/r` (`baseFaceData1` scale 1.3), `skf_eyelid_{b,t}_{l,r}` (rotation-only, e.g. `eyelid_t` r.z = 16.24°, `eyelid_b` r.z = −7.62°).
- `resetFaceData` translations equal the `skull.bin` bind points to ≤ 3 mm (most exact). So reset ≈ identity deformation (`skull.py` comparison).

**`facebonepoint.bin`:**
- `{u32 12, u32 31, u32 28}` + 31 records in the `parts.bin` layout.
- Each `skf_*` bone has n = 1: the facial vertex its position is taken from, W = 1. Its rest position equals that vertex **exactly** (error 0.0 on all 30 skf bones).
- The last record is `sk_head` with 1,424 vertices (INF: head-centroid reference).
- This is the montage analogue of face_diff: after the mesh is deformed, the face-rig bones follow their anchor vertices.

**`vexport.bin`:** `{12, 2, 12}`, then `{u32 count, u32 dataOff, u32 nameOff}` (relative to 12): `faceShape` (1,374 facial vertex indices) and `eyelashShape` (174). INF: the export subsets used by the texture projection.

**`texproj.bin`:** `{u32 12, u32 1374, u32 4, …}`. Not decoded.

### 3.4 Runtime classes (OBS, RTTI)
- `Montage` / `MontageImpl` (vtable 0x142a8b738), `ModelMontage`, `ModelMontageSkull`, `Skull`, `SkullMove` (0x142a94080), `WeightMaskedPatch` (0x142a94140; one per part file), `FaceTexProj`, `FacialControlPoint`, `MontageSkullJob`, `FacialTextureBlender`.
- Config classes `CharacterMontageSkullInfo` and `CharacterMontageParamDeform` hold the slider → lattice mapping; their source file was not found.
- Skull lattice evaluation: `FUN_1419cee70` returns a lattice bone matrix (mode 1 = override, mode 2 = override × FaceData).
- `FUN_1419d6020` applies the lattice to a model's bones using 4-weight records (lattice index 0x1d is remapped to 0x10).

### 3.5 Edit-face recipe (static; ordering from the loaders, INF where marked)
1. Load `facial.fmdl` (+ `neck.fmdl`, `eye.fmdl`, `mouth.fmdl`) from `common_package.fpk`. Concatenate the `facial` mesh vertices in mesh order (§3.2).
2. **Shape (INF medium):** for each part p in [`_ALL_` ← `Skin_<c>_<t>`, eyelid U, eyelid L, cheek, nose, mouth U, mouth B, underchin], for each record i:
   `v[A_i] = lerp(v[A_i], S_p[D_i], W_i)` for position, normal, tangent and bitangent; renormalise the directions.
   - Evidence: the class name *WeightMaskedPatch*, W ∈ (0, 1], and `_ALL_` covering all 1,600 vertices with W = 1.
   - Not verified against a live montage mesh (none was loaded).
3. **Skull (INF high for the form):** `v' = Σ_k W_k · (FaceData[bone_k].M · skull.invBind_k) · v` (4 influences). Missing FaceData bones (`bone_02`, `bone_29`) keep `M = bind` (identity deformation).
4. **Rig:** each `skf_*` bone goes to its `facebonepoint` vertex after steps 2–3. `eyeball_*`, `skf_eyelid_*` and `dsk_ear*` take their FaceData transforms.
5. **Texture:**
   - base `face_skin/FaceSKIN<c><tt>Montage.ftex` + `…MontageNml.ftex`;
   - layers `eyebrow`, `eyelid`, `beard`, `forehead`, `wrinkles`, `chin`, `mark`, with colours from the appearance record.
   - The blend is done by `FacialTextureBlender` on the GPU and is **not decoded**. A plain base texture is the faithful minimum.
6. **Hair:** §4.

---

## 4. Skin tone, hair and colours (generic players)

**Appearance record.** `PlayerAppearance.bin` (`common/character0/model/character/appearance/`) has 60-byte records: `u32 playerId` + the 56-byte body. The body is identical to bytes 0x0C–0x43 of the EDIT save's 72-byte appearance entry, so **PA offset = EDIT offset − 8**. This was confirmed by the prior kit report (sleeves at EDIT 0x14:6 = PA u16 @12 bits 6–7).

- Vanilla `dt00_x64.cpk` holds 17,782 records. 4cc's `4cc_01_base` holds 5,336 placeholder records.
- Field map: Rigged Wiki "PES 2021/Edit file", player appearance entry (supplied by the user). Fields used for the head:

| EDIT off:bit | Bits | Field |
|---|---|---|
| 0x04:0 | 1 | edited face (0 = default face settings) |
| 0x16:4 … 0x2C:4 | 4 each | face sliders ("in-game + 7"): jaw, cheekbones, brows, eyes, eyelids, nose, mouth, chin. These are the 27 slider pairs of §3.1 (INF high). |
| 0x2D:0 | 3 | **skin colour** 1–6 (0/7 = special) |
| 0x2D:3 | 5 | cheek type (→ Cheek part) |
| 0x2E:0 | 3 | forehead type (→ Forehead texture) |
| 0x2E:3 | 5 | facial hair type (→ Beard) |
| 0x2F:0 | 3 | laughter lines (→ Wrinkles) |
| 0x2F:3 | 3 | upper eyelid type (→ EyelidU) |
| 0x30:0 | 3 | lower eyelid type (→ EyelidL) |
| 0x30:4, 0x31:0, 0x31:4 | 4 each | ear length / width / angle (→ `dsk_ear*` FaceData) |
| 0x32:0 | 3 | eyebrow type |
| 0x33:0, 0x33:4 | 4 each | face size, face height |
| 0x34:0 | 3 | nose type (→ Nose part) |
| 0x35:0 | 3 | upper lip type (→ MouthU) |
| 0x35:3 | 3 | lower lip type (→ MouthB) |
| 0x35:6 / 0x36:4 / 0x37:2 | 6 each | hair colour R / G / B (0–63) |
| 0x38:0 | 3 | hairstyle (1 normal … 7 special) |
| 0x39–0x3B | — | eyebrow colour RGB (6 bits each) + style / density / thickness |
| 0x3C:0 | 3 | hair length |
| 0x3C:3 | 3 | wave |
| 0x3D:4 / 0x3E:2 / 0x3F:0 | 6 each | facial-hair colour RGB |
| 0x40:0 | 4 | iris colour |
| 0x40:4 | 5 | hair variation |
| 0x42:0 | 2 | front (up / down / back) |
| 0x42:3 | 3 | combed |
| 0x42:6 | 3 | cropped |
| 0x43:1 | 2 | hairline |
| 0x43:4 | 2 | forehead width |

Statistics: `pa.py` over vanilla. Undocumented fields 0x2F:6, 0x30:3, 0x32:3, 0x32:7, 0x38:3, 0x38:7, 0x42:2, 0x43:3, 0x43:6 are always 0. 0x34:3 (5 bits) is 8 for 97 % of records.

**Runtime resolution (OBS live, `HeadProperty` holder):**

| Field | Content |
|---|---|
| +0x30 | edit-hair **model** name, e.g. `vs_nr_up_00_NM` |
| +0x38 | **shell** texture, e.g. `S_st1_O_NM_pt10` |
| +0x40 | **parts** texture, e.g. `st1_sb3` |
| +0x48 | parts **base**, e.g. `st1` |
| +0x70 | f32[4] **hair colour** RGBA (OBS e.g. (56, 34, 23)/255) |
| +0x80 | f32[4] second colour (INF: eyebrow/facial hair) |
| +0x68 / +0x69 | real / edit-hair flags |
| +0xC8 | → `Holder<MontageProperty>` |

Every live head (real ones too) carried default edit-hair strings.

**Edit-hair files:**
- model `face/edithair/model/<model>/#Win/face.fpk` → `hair_high.fmdl`;
- shell `face/edithair/shell/#windx11/<shell>_bsm_alp.ftex` → `fox_head_shell_edit_mat`;
- cards `face/edithair/parts/#windx11/<parts>_bsm_alp.ftex` + `<base>_{nrm,srm,trm}.ftex` → `fox_hair_mat`;
- scalp `fox_skin_edit_mat` = the montage skin (INF);
- tint via the material vector `HairColor` (present in the fmdl, default 1,1,1,1; INF: set to `+0x70`).

**Model-name grammar** (INF high from the 896 names): `<length>_<kind>_<front>_<variant>_<forehead>[_acc]`.

| Token | Values |
|---|---|
| length | `vs`, `st`, `md`, `lg`, `sh` (vs/st 144 each, md/lg 288 each, sh 3), or `sp_ty<N>` (29 specials) |
| kind | `nr`, `sg`, `af` (afro), `dd` (dreadlocks), `cr` (braided), `mh` (mohican) |
| front | `up`, `dw`, `bk` |
| variant | `00/19/37/55/73/91` |
| forehead | `NM` / `NW` / `WD` = forehead width |
| `_acc` | hair accessory |

The exact appearance-field → token function was not traced (search `edithair/` string users if needed).

**Skin tone (OBS paths + INF binding):** colour c ∈ 1–6 selects:
- `face_skin/FaceSKIN<c><tt>Montage{,Nml}` (edit faces);
- `common/sourceimages/#windx11/arm_<c>_bsm` and `thigh_<c>_bsm` (body skin; kit report §7.2 saw `arm_1_bsm` / `thigh_1_bsm` bound);
- `face/common/head_<c>_srm` (INF).

Real faces carry their own skin in `face_bsm_alp`. Their arms use `arm_<c>_bsm` unless the pack ships `arm_bsm`.

---

## 5. Live observations

State A (≈20:20 local): a 2-character preview.
- One `HeadUnit` (vtable 0x142aaade8) for face id 75223 (4cc pack: `face_diff` + 6.8 KB `hair_high` only). Slots: `hair_high` (pack), `common/eye_new`, `common/mouth`. `+0x708` → the face_diff `RawFile` (BlockPackage = that fpk). `+0x118 = 1`.
- One idle `MontageImpl` owned by `FaceManager`, with no target.

State B (≈20:45): a match. 25 `HeadUnit`s, 25 `UniformUnit`s (22 players + 3 referees), 50 `MontageProperty` holders (defaults).
- All 25 heads have real flag 1, edit-hair flag 0, a face_diff loaded, `+0x118 = 1`, and the montage pointer `+0x790[+0x728]` = 0.
- Packs are 4cc 752xx/754xx. 10 had `face_high` + `hair_high`, the rest only `hair_high` or nothing.
- Every head loaded `common/eye_new` + `common/mouth`; none loaded `neck`.
- `EyeConstraint` (via `CharacterObject+0x48` → `FaceController`, impl `+0x18`) holds face_diff scale and eyelid angles exactly. HeadUnit `+0x100` holds `Rᵀ·jawOffset` (§2.4), which confirms the real-face recipe on 75201 and 75415.

Edit-face confirmation: **not possible in either state.** No head had `+0x68 == 0`.

---

## 5a. Case 76711 (team 767, helmet face): what binds to `fox_skin_mat`

**Facts (OBS, `dump.py` + ftex decode via GF `ftex.py` in scratch):**
- `face/real/76711/#Win/face.fpk` comes from `4cc_40_faces.cpk`. It holds `face_diff.bin`, `face_high.fmdl` (1 mesh, 194 vertices, y 1.465–1.631, z −0.03…0.11, material `fox_skin_mat` / `pes3DDF_Skin_Face`) and `hair_high.fmdl` (4,083 vertices, y 1.539–1.839, material `fox_skin_mat` / `pes3DDF_Hair2`, Base `head.dds`).
- The `face_high` mesh uses the same bone group as vanilla `face_high` mesh 0, the **neck** part (`sk_chest…dsk_scm`, `skf_jaw`…). Vanilla mesh 0 covers y 1.385–1.718 and is always drawn.
- `face_high`'s `fox_skin_mat` references `…/76711/sourceimages/face.dds` (Base), `face_nrm`, `face_srm` (also as RoughnessMap), and `face_trm`.
- The folder holds **both** `face.ftex` (64×64 solid (0.55, 0.78, 0.26, α ≈ 1)) and `face_bsm_alp.ftex` (64×64 solid (0.29, 0.24, 0.16, α 0.32)). `head.ftex` is 1024² greyscale.

**Does PES swap the albedo? (OBS: no per-face swap exists for `fox_skin_mat`)**
1. Exe strings: the only face-folder texture names the engine composes are `sourceimages/arm_bsm.ftex`, `/sourceimages/eye_sclera_bsm.ftex` and the edit-hair suffixes. There is no `face_bsm*`, `face.ftex` or `face.dds` string (search over the whole image).
2. Live, state B: each face pack's `BlockPackage+0x88` holds one `MaterialModifier2`. Dumped for a loaded pack (`live20.py`): material key 0x57948546 (the arm material).
   - Parameter entries are 8 bytes, `{u32 StrCode32(param), u8 slot, 0, 0, u8 kind}`, with kind 0x40 = texture and 0x20 = vector.
   - Bindings: `Base_Tex_SRGB` → that pack's `arm_bsm.ftex`, 0x7bcbb6cb → `common/sourceimages/skin_dtm.ftex`, plus `Incidence_Roughness`, `Incidence_Color`, `Dirty_Mask`.
   - Nothing in it targets `fox_skin_mat`.
3. Live: the `MaterialModifier2`s keyed to `fox_skin_mat` (21 instances) bind only common head overlays: `common/sourceimages/head_dtm` (dirt/sweat mask, B/G channels only), `face/common/head_normal_animation`, `head_normal_mask`, `head_rotation_mask`. None binds a face-folder texture.
   - The slot pairing in those 4 records is uncertain: head_dtm decoded to a mask, so it is not albedo whatever the label.
4. Same-structure packs loaded live (75201, 75203, 75208, 75211, 75219: their `face_high` also references `face.dds` and the folder has both files) show the 4cc convention:
   - `face.ftex` is a tiny solid colour (4×4) equal to the character's skin or suit tone: 75208 yellow vs its `face_bsm_alp` mean yellow, 75219 light blue vs blue, 75201 (1, 0.83, 0.65) skin.
   - So `face.ftex` is the authored albedo of these plate meshes, and `face_bsm_alp` is unused by them.

**Answer:**
- **INF (high):** PES binds `face.ftex` to `face_high`'s `fox_skin_mat` Base. It draws the 194-vertex plate as the character's **neck**, shaded by `pes3DDF_Skin_Face` with that pack's `face_nrm`/`face_srm`/`face_trm`.
- So PES really draws a green neck under the helmet, wherever the collar and helmet leave it visible.
- No code path hides or culls `face_high` per player. The only head-model gating is the slot logic in §2.2.
- Not OBS: 76711 was not loaded in either live state, and this work cannot take screenshots.
  - To close it: when 767 is loaded, read its `face_high` Model's bound Base texture. That route was not found here: `FmdlFile` holds no `Texture*` within 3 pointer levels, and 4cc pack textures do not resolve by path code. Alternatively, compare a game capture.
- **Recommendation for GodotFootball:** bind exactly what the fmdl names (`face.ftex` → green). Do not substitute `face_bsm_alp` or a skin tone.

## 6. Open items and what was tried

| Item | Status | Tried / next step |
|---|---|---|
| Patch blend formula (§3.5 step 2) | INF medium | Found the registration (`FUN_1419d3ae0`, `FUN_1419ce8f0` → `ctx+0x230+i*0x10` map and `ctx+0x2C0+i*8` active slot). No reader of the active slots matched a displacement scan of 0x1419c0000–0x1419e0000. **Next:** decompile `ModelMontage` users or `FUN_1419ced10`, or load an edit-face player and diff a live `facial` vertex buffer. |
| Slider → lattice deformation | open | Table 0x143516ee8 and setter `FUN_1419d9b50` are known; the config classes `CharacterMontage{SkullInfo,ParamDeform}` have no located data file. |
| Missing presets | OBS absent | 31 preset FaceData files and `skull/player/*` are not in this install's VFS. **INF:** edit faces render at the reset/base shape. Verify with a live edit-face head. |
| Montage texture blend | open | `FacialTextureBlender` (GPU) not decoded. |
| face_diff 0x14/0x20 exact axis semantics | INF medium | Stored to `EyeConstraint` via `FUN_141a70a90` (sin/cos build of two rotation matrices). |
| face_diff ear records (0x300) | INF high bone names; always identity in vanilla | — |
| `C` arrays in `parts.bin` / `facebonepoint.bin`, `texproj.bin`, `skull.bin` flag array | not decoded | — |
| Resource 0xf93ca219a4a9 (first load in `FUN_141a7b020`) | unresolved | Not a stem of any fpk member or common model name. |
| Hair-name builder (appearance → `vs_nr_up_00_NM`) | not traced | Grammar inferred from file names only. |
