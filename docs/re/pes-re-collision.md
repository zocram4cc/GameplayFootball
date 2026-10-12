# PES 2021: the player's physical body for gameplay (rig, collision volumes, ball contact)

Scope: the read-only executable image (`<scratch>/pesmem.bin`, base 0x140000000, function bounds from `.pdata`), the live process (reads of `/proc/<pid>/mem` only; nothing was written and no input was sent), and VFS data read with `pesfmt_dump cat`. Scratch and scripts are in `<scratch>/CollisionRE/` (see §9). Tags: **OBS** means observed, with the method stated. **INF** means inferred, with a confidence level.

## 0. Headline facts

1. **`.frig` holds no collision or physics data.** It is the animation-track rig: units, IK chains, layer masks and a bone→unit map. Its format is decoded in full in §1. OBS: every byte of `body_skel.frig` is accounted for, and the same layout parses the hand, face and ball frigs.
2. **The player collision volumes are hard-coded capsule tables in the exe's `.data`.** No PES file holds them. OBS: there are no player physics files in `allpaths.txt` or in the fpk member index; the tables are in §2.
   - Each human gets three capsule sets, all named by strings in the exe:
     - `Atari_Ball_Hit_NN` ball-hit capsules: 11 for outfielders and officials, 15 for goalkeepers (adds forearms and hands);
     - one `PlayerVsPlayer` capsule (r 40.5 cm, length 60 cm, on `sk_belly`);
     - two `Atari_HumanLeg_Hit_0x` shin capsules (r 7.5 cm) for players only.
   - OBS live: every capsule exists in a running match with exactly these radii and lengths (§5).
3. **Intended ball touches are authored, not simulated.** Every kick, trap or header clip carries `hit_data`: hit frame, hit bone, and the ball centre in that bone's joint frame. At the authored frame the ball sits on the capsule surface: median gap −0.1 cm for inside-foot contacts, over 236 clips (§3.3).
   - The physics capsules handle unintended contacts: deflections, blocks, player-player and leg-leg. Collision kinds, a kind×kind contact mask, and per-bone action modes decide what can touch what (§2.3–2.5).
4. **Units and frames.**
   - Capsule tables and `hit_data` are in **cm**. The ph world is in **m**; the exe divides by 100.0 at 0x14253ee30.
   - Both tables use the **joint frame** `F_b` (X along the bone). This is not the identity-bind anim frame. `F_b` for every capsule bone was read exactly from live memory (§4).
   - GF's `W·Rs` derivation matches `F_b` for thigh, belly, chest, head and upper arm. It is wrong for leg (1.5°), foot (5.8°), forearm and hand (≈22°).
5. **Gani root raw units → metres is 1/16384, not 1/20480.** OBS: this independently confirms `pes-re-animselect.md` §0.3. Over 2,754 hit records, FK joint positions at `hit_frame` match the json `sf_joint_pos` with a median error of 0.10 cm at 1/16384, against 21 cm at 1/20480.

---

## 1. `.frig` format (closed)

There are 65 `.frig` files. The player one is `common/anime/FoxAnim/Body/CharacterAssets/body_skel.frig` (1,532 B, dt13). Everything is little-endian. Offsets are absolute.

**Header (OBS):**

| Off | Type | Meaning | How checked |
|---|---|---|---|
| 0x00 | u32 | StrCode32 of the rig name | `HumanBody` (body), `HumanHand` (hand), `Face`, `prop` (ball): all four hashes match |
| 0x04 | u32 | name string offset | body 0x5c → "HumanBody". The hand and face files point at an empty string, so the name is not always stored |
| 0x08 | u32 | constant 0x66 in all four files | INF (medium): a format version |
| 0x0C | u32 | unitCount | body 15 = every body gani's TrackHeader UnitCount |
| 0x10 | u32 | segment (track) count | body 27 = gani SegmentCount |
| 0x14 | u32 | file size | |
| 0x18 | u32 | bone-map offset | |
| 0x1C | u32 | mask-section offset | also the end of the unit records |
| 0x20 | u32[unitCount] | absolute unit record offsets | |

**Unit record (OBS).** All units start with this layout, in u16 words:

| Word | Meaning |
|---|---|
| [0..1] | u32 type |
| [2] | segCount |
| [3] | boneCount |
| [4] | parent bone (index into the bone map, `0xFFFF` = none) |
| [5] | parent unit (`0xFFFF` = none) |
| [6..7] | 0 |

Types and bodies:

| Type | Size | Segments | Body after the 8-byte header | Used for |
|---|---|---|---|---|
| 1 | 32 B | 2 | `u16 segs[2]` (boneCount 0) | rig root `RIG_ROOT`. The gani segs are QUAT_DIFF + VECTOR_DIFF |
| 5 | 32 B | 2 | `u16 bones[1]`, `u16 segs[2]` | bone with rotation + translation: body unit 1 = `sk_root_hip` (quat + vec3); all face bones |
| 7 | 32 B | 2 | as type 5 | mover in the hand, face and ball rigs |
| 4 | 32 B | 1 | `u16 bones[1]`, `u16 segs[1]` | rotation-only bone |
| 3 | 64 B | 3 | see below | 2-bone IK chain (legs) |
| 8 | 64 B | 4 | see below | 3-bone IK chain (arms) |

**IK chain units (types 3 and 8):**
- +0x10: 16 B of zeros (meaning unknown).
- +0x20: `f32 hingeAxis[3], 0`. Values:
  - left leg (1, 2.0e-4, 9.3e-10);
  - right leg (1, −2.0e-4, 9.3e-10);
  - left arm (0, −1, 0);
  - right arm (0, 1, 0).
  INF (high): the knee or elbow bend axis, in the bone frame.
- +0x30: u16 list made of `chainBones[boneCount]`, then `segs[boneCount]`, then `effectorBone`, then `lastSeg`.
  - Left leg: bones 2, 3 (thigh_l, leg_l), segments 5, 6, effector bone 4 (foot_l), segment 7.
  - Left arm: bones 12, 13, 14 (shoulder, upperarm, forearm), segments 17, 18, 19, effector bone 15 (hand_l), segment 20.

**Segment meaning inside a chain (OBS, gani segment types plus GF's verified track map):**
- leg: [vec3, quat thigh, quat leg];
- arm: [quat shoulder, vec3, quat upperarm, quat forearm].

So the u16 list is **not** a positional bone↔segment pairing. Killed hypothesis: GF `frig.py` pairs bone i with segment i. That puts the vec3 on the thigh, which contradicts the segment types. INF (medium): the chain vec3 is the IK effector target. GF reports it is often 0xFF-filled; not verified.

**Body unit table (OBS).** Each line is unit: type, bones, segments.

| Unit | Type | Bones | Segments |
|---|---|---|---|
| 0 | 1 | root | 0, 1 |
| 1 | 5 | sk_root_hip | 2, 3 |
| 2 | 4 | dsk_hip | 4 |
| 3 | 3 | thigh_l, leg_l → foot_l | 5–7 |
| 4 | 4 | foot_l | 8 |
| 5 | 3 | thigh_r, leg_r → foot_r | 9–11 |
| 6 | 4 | foot_r | 12 |
| 7 | 4 | belly (parent sk_root_hip) | 13 |
| 8 | 4 | chest | 14 |
| 9 | 4 | neck | 15 |
| 10 | 4 | head | 16 |
| 11 | 8 | shoulder_l, upperarm_l, forearm_l → hand_l (parent chest) | 17–20 |
| 12 | 4 | hand_l | 21 |
| 13 | 8 | right arm | 22–25 |
| 14 | 4 | hand_r | 26 |

**Mask section (at the 0x1C offset, OBS):** u32 width (= unitCount), u32 maskCount, `u32 maskOffsets[]` relative to the section start. Each mask is u32 StrCode32(name), char name[12] padded with 0xFE, then `f32 weight[width]`, one weight per unit. The body has 8 masks:
- `MirrorL` = units {3, 4, 11, 12};
- `MirrorR` = {5, 6, 13, 14}. INF (high): left↔right swap sets for mirrored clips;
- `LArm` {11, 12} and `RArm` {13, 14};
- `LRArm` {chest 0.3, 11–14 = 1};
- `UBody` {belly 0.3, chest…hand_r = 1};
- `UBodyLArm` and `UBodyRArm` (UBody minus the other arm).

INF (high): partial-body layer masks.

**Bone map (OBS):** u32 count (20), then `{u32 unit, u32 StrCode32(bone)}` pairs. All 20 names resolve against `body_anim_skel.ask`. GF `frig.py` reads the hash correctly but treats the first u32 as "something"; it is the owning unit.

Not resolved: the gani per-unit name hashes (e.g. 0x39bc5d3e for unit 1). Tried: every string in the exe image, plus a 377k-candidate grammar of bone/side/prefix/suffix names. The frig does not store unit names.

**HumanHand rig: `common/anime/FoxAnim/Hand/CharacterAssets/pes_human_hand_141203.frig` (OBS, same decoder).**

- Header:
  - hash 0x3fc935e1 = StrCode32("HumanHand");
  - 21 units and 23 segments, matching the hand ganis (e.g. `Hand/Animations/normal.gani`: 21 units);
  - mask section: width 21, **0 masks**.
- Bone map: 20 entries. Each hash is **StrCode32 of the left-hand bone name** as listed in `hand_anim_skel.ask`, and every one matches.
  - So the map is left-hand only. INF (high): the right hand plays the same clip through mirroring.
  - The gani *unit* hashes are different names; they are not bone names (the same holds for the body rig). Map gani units to bones **by unit index**.

| Unit | Type | Bone (bone-map index, ask name) | Parent bone | Parent unit | Gani segment ids |
|---|---|---|---|---|---|
| 0 | 1 root | — (RIG_ROOT) | — | — | 0, 1 |
| 1 | 7 mover (rotation + translation) | 0 skh_hand_l | — | 0 | 2 (quat), 3 (vec3) |
| 2 | 4 | 1 skh_thumb_mata_l | 0 | 1 | 4 |
| 3 | 4 | 2 skh_thumb_mcp_l | 1 | 2 | 5 |
| 4 | 4 | 3 skh_thumb_pip_l | 2 | 3 | 6 |
| 5 | 4 | 4 skh_index_mata_l | 0 | 1 | 7 |
| 6 | 4 | 5 skh_index_mcp_l | 4 | 5 | 8 |
| 7 | 4 | 6 skh_index_pip_l | 5 | 6 | 9 |
| 8 | 4 | 7 skh_index_dip_l | 6 | 7 | 10 |
| 9 | 4 | 8 skh_middle_mata_l | 0 | 1 | 11 |
| 10 | 4 | 9 skh_middle_mcp_l | 8 | 9 | 12 |
| 11 | 4 | 10 skh_middle_pip_l | 9 | 10 | 13 |
| 12 | 4 | 11 skh_middle_dip_l | 10 | 11 | 14 |
| 13 | 4 | 12 skh_pinky_mata_l | 0 | 1 | 15 |
| 14 | 4 | 13 skh_pinky_mcp_l | 12 | 13 | 16 |
| 15 | 4 | 14 skh_pinky_pip_l | 13 | 14 | 17 |
| 16 | 4 | 15 skh_pinky_dip_l | 14 | 15 | 18 |
| 17 | 4 | 16 skh_ring_mata_l | 0 | 1 | 19 |
| 18 | 4 | 17 skh_ring_mcp_l | 16 | 17 | 20 |
| 19 | 4 | 18 skh_ring_pip_l | 17 | 18 | 21 |
| 20 | 4 | 19 skh_ring_dip_l | 18 | 19 | 22 |

- Rule: unit u ≥ 2 drives bone-map entry u−1 with one quaternion segment, u+2.
- The finger order is thumb, index, middle, pinky, ring, which is also the `hand_anim_skel.ask` order.
- `hand_anim_skel.ask` is **version 2**, not the version-1 44-byte layout of the body ask: the GF/GodotFootball ask reader rejects it. Its 20 bone names are stored in the same order as the frig bone map.
- Only the static layout was checked. The per-segment types of the hand gani units were not verified against `normal.gani`; INF (high) from the unit types: unit 1 is quat + vec3, the rest one quat each.

**Other rig and physics files (OBS, `allpaths.txt`):**
- `common/anime/FoxAnim/{goal_hit,goal_l,goal_r,hit_stand_0001}.geom`: goal and stand collision.
- `common/render/model/bg/hit/**/*.hkx|geom`: stadium hit meshes.
- `Asset/model/character/uniform/cloth/#Win/fcl_*_sim.fclo`: render cloth, magic "olcf"; not decoded.
- None of these is a per-player gameplay volume.

---

## 2. Player collision volumes (closed)

### 2.1 Definition tables (OBS, `.data`)

Each record is 0x48 bytes:

| Off | Type | Meaning |
|---|---|---|
| +0x00 | u32 | shape: 1 = capsule, 0 = sphere (the `FUN_141b993d0` branches) |
| +0x04 | u32 | 0 |
| +0x08 | char* | name |
| +0x10 | u32 | bone, in the 19-entry skeleton enum `HIP, THIGH_L, LEG_L, FOOT_L, THIGH_R, LEG_R, FOOT_R, BELLY, CHEST, NECK, HEAD, SHOULDER_L, UPPERARM_L, FOREARM_L, HAND_L, SHOULDER_R, UPPERARM_R, FOREARM_R, HAND_R` (the enum names are at 0x14344a990; the same order as the `anim_infos.json` `lists.skeleton`) |
| +0x14 | f32[3] | centre (cm), in the joint frame `F_b` |
| +0x20 | f32[4] | quaternion (x, y, z, w): capsule frame → joint frame. **The capsule axis is the capsule-local Y** (the endpoints built in `FUN_141b993d0` are (0, ∓len/2, 0)) |
| +0x30 | f32[3] | (1, 1, 1) |
| +0x3C | f32 | radius (cm) |
| +0x40 | f32 | full segment length (cm), end-cap centre to end-cap centre |
| +0x44 | u32 | 0 |

Counts come from the getters `FUN_140bff4c0`→11, `FUN_140c14350`→15, `FUN_140a4c0f0`→1 and `FUN_141f05c10`→2.

**Table GK = 0x1437f1d50 (15 records).** The outfield table at 0x1437f19e0 is these same records minus `_11`, `_12`, `_14` and `_15`. Its names run `_01…_10, _12`, so the outfield `_12` slot is `upperarm_r`.

| Name | Bone | Centre (cm, `F_b`) | Quat (x, y, z, w) | r (cm) | len (cm) |
|---|---|---|---|---|---|
| Atari_Ball_Hit_01 | thigh_l | (21.442, 1.250, 0.908) | (−0.4771, 0.5188, 0.4805, 0.5219) | 9.5 | 39 |
| _02 | leg_l | (24.375, −1.568, −1.793) | (−0.5115, 0.4849, 0.5150, 0.4878) | 6 | 32.5 |
| _03 | foot_l | (9.934, −0.004, −0.986) | (−0.5982, 0.3764, 0.5964, 0.3805) | 4.75 | 15 |
| _04 | thigh_r | (21.442, 1.250, −0.908) | (−0.4805, 0.5219, 0.4771, 0.5188) | 9.5 | 39 |
| _05 | leg_r | (24.375, −1.586, 1.793) | (−0.5150, 0.4878, 0.5115, 0.4849) | 6 | 32.5 |
| _06 | foot_r | (9.934, −0.024, 0.986) | (−0.5926, 0.3746, 0.6019, 0.3823) | 4.75 | 15 |
| _07 | belly | (−2.662, −5.568, 0) | (0.4789, 0.5202, −0.4789, 0.5202) | 16 | 23 |
| _08 | chest | (12.018, −2.931, 0) | (0.4781, 0.5210, −0.4781, 0.5210) | 16 | 20 |
| _09 | head | (4.994, −1.812, 0) | (0.5, 0.5, −0.5, 0.5) | 9.5 | 15 |
| _10 | upperarm_l | (14.428, −0.637, 0.707) | (−0.5234, 0.4754, 0.5234, 0.4754) | 5.5 | 29 |
| _11 (GK only) | forearm_l | (19.018, −0.783, 0.207) | (−0.4890, 0.5108, 0.4890, 0.5108) | 4 | 37 |
| _12 (GK only) | hand_l | (4.845, −0.484, 0.207) | (−0.5316, 0.4662, 0.5316, 0.4662) | 5 | 8 |
| _13 (outfield `_12`) | upperarm_r | (14.428, −0.637, −0.707) | (−0.5234, 0.4754, 0.5234, 0.4754) | 5.5 | 29 |
| _14 (GK only) | forearm_r | (19.018, −0.783, −0.207) | (−0.4890, 0.5108, 0.4890, 0.5108) | 4 | 37 |
| _15 (GK only) | hand_r | (4.845, −0.484, −0.207) | (−0.5316, 0.4662, 0.5316, 0.4662) | 5 | 8 |

The other two tables:

| Table | Name | Bone | Centre (cm) | Quat (x, y, z, w) | r (cm) | len (cm) |
|---|---|---|---|---|---|---|
| 0x1437f1d00 | PlayerVsPlayer | belly | (11.207, −0.496, 0) | (−0.4695, −0.5287, −0.4695, 0.5287) | 40.5 | 60 |
| 0x1437f2190 | Atari_HumanLeg_Hit_00 | leg_l | (24.053, −0.644, −1.0) | (−0.5009, 0.4991, 0.5009, 0.4991) | 7.5 | 32.5 |
| 0x1437f2190 | Atari_HumanLeg_Hit_01 | leg_r | (24.053, −0.644, 1.0) | (−0.5009, 0.4991, 0.5009, 0.4991) | 7.5 | 32.5 |

### 2.2 Construction chain (OBS, decompiles)

1. Per human, `FUN_1408c4650`, `FUN_1408c4740` and `FUN_1408c4800` each build one `match::AnimeCollision` (vtable 0x1425ef020, `FUN_14081d630`). It wraps a `collision::Human` (vtable 0x142ad2368, ctor `FUN_141b99050`) with fields +8 = human id and +0xC = collision kind.
2. The sets, by AnimePlayer field:

   | AnimePlayer field | Kind | Table |
   |---|---|---|
   | +0x3aa8 | 4 | ball-hit: GK table if the flag argument is set, else outfield |
   | +0x3ab0 | 5 | PlayerVsPlayer |
   | +0x3ab8 | 3 | HumanLeg |

3. `FUN_14081d7e0` is Setup. For each record it calls `FUN_141b991c0` (AddPart), which:
   - requests a ph body through `collision::Manager` (`FUN_141b98460`, singleton `FUN_141b985e0`);
   - binds it to the bone with an `And::AnimConvertBoneSpacePlugin` "ConvBoneToRigidBodyTransformInfo" (0x50 B):
     - byte1 = bone + 1;
     - +0x10 = centre/100;
     - +0x20 = quat.
   The plugin then re-expresses these in the anim bone frame (§4).
4. `FUN_141b993d0` (CreateObject) computes:
   - radius = r/100·s;
   - endpoints = (0, ±len·0.5/100·s, 0).

   Here `s` = AnimePlayer+0x9b0[bone]. That is the per-bone relative physique scale; per PhysiqueRE it is written by `FUN_141eb5810` (see `local://pes-re-physique.md`).

**Height and physique (open).** OBS live: every capsule has the unscaled table size. This includes a player whose 0x9b0[head] = 1.019: his head radius is 0.0950, not 0.0968. The live bone offsets are also identical for players with root scale 1.05 and 1.106.
- INF (medium): the capsules are built before the physique scales are written, so they effectively never scale.
- Capsule *positions* follow the posed, physique-scaled skeleton, because they are bone-attached.
- For a reimplementation: use the fixed sizes and attach the capsules to the scaled skeleton.

### 2.3 Collision kinds (OBS live census of `collision::Object` [vtable 0x142596818], fields +0x68 id, +0x6c kind, +0x80 radius m)

| Kind | Meaning | Live count and radius |
|---|---|---|
| 1 | ball (id 26) | 1 object, r **0.1087 m** |
| 2 | bone switched to "ball sensor" by an action | none at the time of the read |
| 3 | HumanLeg shin capsules | 44, r 0.075 (22 players × 2; officials have none) |
| 4 | ball-hit capsules | r 0.095 (thighs + heads, 74), 0.16 (belly + chest, 50), 0.06 (legs, 50), 0.055 (upper arms, 50), 0.0475 (feet, 48), 0.05 and 0.04 (GK hands and forearms, 4 each) |
| 5 | PlayerVsPlayer | 25, r 0.405 |
| 7 | AnimePlayer root object (no ph body: `FUN_141b98150` returns 0 for kind 7) | 24 |
| 12 | capsule switched off by an action | 2 feet of one player during the read |
| 6, 8, 9, 10, 11 | static objects, ids 27–31 | kind 11 = 4 objects with r 0.015. INF (low): corner-flag poles |

### 2.4 Contact mask (OBS, `FUN_141b970e0` contact callback, table u32[17] at 0x142ad1ec0)

Object A records a contact with object B when all of these hold:
- `A.id != B.id`;
- bit `B.kind` is set in `mask[A.kind]`.

| A kind | B kinds that get recorded |
|---|---|
| 1 ball | 1, 2, 4, 6, 7, 9, 10, 11, 13, 14, 16 |
| 2 sensor bone | 1 |
| 3 shin | 3, 5, 7 |
| 4 ball-hit | 1 |
| 5 PlayerVsPlayer | 3, 5, 6, 7, 9, 10, 11 |
| 6 | 1, 5 |
| 7 root | 1, 3, 5, 7, 8, 10, 13, 14 |
| 8 | 7, 14 |
| 9, 11 | 1, 5 |
| 10 | 1, 5, 7, 14 |
| 13 | 1, 7, 8 |
| 14 | 1, 7, 8, 10, 14, 15 |
| 15 | 14 |
| 16 | 1 |
| 0, 12 | none |

`0x11` (17) means "restore default".

**Contact record**, linked on A at +0x20, count at A+0x28:

| Off | Meaning |
|---|---|
| +0 | i16 A.id |
| +2 | i16 B.id |
| +4 | B.kind |
| +5 | B part: `FUN_141e53860` maps the 16-part index → skeleton bone using table 0x142b45df0 = {0, 7, 11, 12, 13, 15, 16, 17, 9, 10, 1, 2, 3, 4, 5, 6}; otherwise B+0x7c |
| +6 | — |
| +7 | A part |
| +8 | \|depth\| |
| +0xC | 6 floats: contact point and normal |
| +0x24 | B position |
| +0x30 | 3 floats: relative velocity × 54 / k × timescale |

### 2.5 Per-bone action modes (OBS)

- AnimePlayer+0x2704 is an `int[19]`, one entry per skeleton bone.
- `FUN_1406f02f0(ap, bone, mode)` sets an entry:
  - mode 0x11 restores the capsule's kind to the set default (4);
  - any other mode writes `part+0x6c = mode`, which changes the capsule's kind and so the contact mask.
- 116 call sites in actions. Modes seen: **2**, **12** and **0x11**. Examples:
  - `KickoffKicker`: `foot_r` and `leg_r`;
  - `goal_keeper::PickupBall`: legs, forearms, hands;
  - `AnimeDataDriven`: `thigh_r`, `leg_r`.
- Mode 2 makes the bone a ball-only sensor (mask[2] = {ball}).
- Mode 12 makes it collide with nothing. INF (high): this keeps the kicking leg from deflecting its own ball.
- `FUN_1406edce0` scans the mode-2 bones each tick. A recorded ball contact on one of them, or a kind-1 contact whose mapped part is that bone, marks a body touch. Bones 14 and 18 (hands) take a separate branch gated by flag +0x2460 bit 8. INF (medium): GK hand handling.

---

## 3. Ball–body contact

### 3.1 Authored contacts: `hit_data` (OBS; json glossary verified against FK)

- Source: `common/anime/Mbinfo/json/anim_infos.json`, the developer export of `Mbinfo/bin/HitData.bin` and others.
- 3,254 hit records over 2,948 clips: 2,695 clips with 1 hit, 207 with 2, 40 with 3, 5 with 4, 1 with 5. 48 `sub_hit_data` records.
- Units: cm. Position fields are in the **clip origin frame** (root at frame 0, Fox axes) unless noted.

| Field | Meaning | Evidence |
|---|---|---|
| `hit_frame` | gani frame of contact (not a 30 Hz time) | FK at exactly this frame matches the positions below |
| `hit_bone` | index into `lists.skeleton` (19 entries) | the counts below match the clip names |
| `sf_joint_pos_{x,y,z}` | hit-bone joint position at `hit_frame` | median 0.10 cm against FK (scale 1/16384) over 2,754 records |
| `sf_root_pos_{x,z}` | root XZ at `hit_frame` | median 0.04 cm |
| `sf_root_vec_{x,z}` | root displacement over the frame before the hit (cm/frame) | backward difference: median error 0.05; forward difference: 0.94 |
| `ball_pos_{x,y,z}` | ball centre at contact | ground balls: y = 11.55 (1,003 records) |
| `hit_offset_{x,y,z}` | ball centre in the hit bone's **joint frame `F_b`** | \|ball − joint\| = \|hit_offset\| to a median of 0.04 cm. A per-bone rotation fit leaves 0.1–0.3 cm residuals and equals the `F_b` of §4 |
| `ik_start`, `ik_end` | frame window for IK correction | 1,685 records |
| `ikef_joint_pos_*`, `ikef_root_pos_*` | joint and root positions at `ik_end` | median error 0.14 and 0.06 cm |
| `ik_offset_{front,back,left,right,up,down}` | allowed ball deviation the IK may absorb (cm) | values cluster on 10, 20, 30. INF (high) |
| `hit_body_angle` | — | not the root yaw (tested); open |

Hit bones in use:

| Bone | Records |
|---|---|
| foot_r | 1,972 |
| hand_l | 437 |
| foot_l | 232 |
| hand_r | 204 |
| head | 183 |
| chest | 88 |
| thigh_r | 56 |
| belly | 25 |
| leg_r | 17 |

Left-foot hits are rare because the game mirrors right-foot clips (the frig `Mirror` masks).

**`danger_data` (`danger_start`, `danger_end`).** It appears only in tackle and slide clips (115 records): `tacklefoot` 44, `sliding` 21, `tackleshoulder` 13, `tacklehold` 13, and others. INF (medium): the frame window in which the shin (kind 3) and body contacts count as dangerous, i.e. foul or trip. The consumer was not traced.

### 3.2 Contact zones: classification by name and geometry (data OBS, classifier code open)

Median `hit_offset` (cm, joint frame; X along the bone) for the foot-part tokens in the clip names:

| Token | Bone | n | x | y | z |
|---|---|---|---|---|---|
| inside | foot_r | 236 | 10.4 | 0.7 | −13.9 |
| outside | foot_r | 101 | 11.2 | 9.5 | 9.8 |
| instep | foot_r | 169 | 11.6 | 13.8 | −1.9 |
| infront | foot_r | 43 | 12.0 | 13.6 | −5.1 |
| sole | foot_r | 179 | 23.2 | −9.8 | 0.3 |
| toe | foot_r | 26 | 24.0 | 10.9 | 0.9 |
| heel | foot_r | 43 | 5.2 | −11.0 | −0.3 |
| thigh | thigh_r | 47 | 27.5 | 18.7 | −2.1 |
| head | head | 162 | 18.2 | −15.2 | −2.4 |
| breast/chest | chest | 52 | 25.3 | −18.6 | 7.9 |

The runtime touch-kind names are Lua script-binding enums. The value order is INF from the name tables:
- `INSIDE_BALL, INFRONT_BALL, OUTSIDE_BALL, INSTEP_BALL, HEEL_BALL, SOLE_BALL, TOE_BALL, TOE2, SOLE2, FINGERTIP` (0x1434a5ca0);
- object touches: `NONE, POST, CROSSBAR, CFLAG, POLE, BOARD, SIDENET, UPPERNET, BACKNET, SIDENET_IN, UPPERNET_IN, BACKNET_IN, STAD, CONE, PANEL, PANEL_STAND` (0x1434a5c20);
- foot touches: `CHEST, RIGHT_INSIDE, RIGHT_OUTSIDE, LEFT_INSIDE, LEFT_OUTSIDE` (0x14344b788).

**Open: the code that assigns the touch kind.**
- Tried: `match::record::ObserverBallTouch::Exec` (`FUN_140a17b30`). It only records stats using an already-computed kind.
- Tried: data and code xrefs to the name tables. They are reached only through the script-binding registration (no direct code use).
- INF (medium): the kind comes from the selected clip family and part token. The table above is a geometric fallback for reimplementation.

### 3.3 Cross-check: the authored ball touches the capsules (OBS, computed)

- Test: distance from `hit_offset` to the capsule segment, minus the capsule radius, minus 10.87 cm. Capsule axis taken as `q·Ŷ`.
- Results:

  | Touch | n | Median gap (cm) | p10…p90 (cm) |
  |---|---|---|---|
  | foot_r inside | 236 | −0.1 | −1.8…0.7 |
  | foot_r sole | 179 | −0.3 | — |
  | foot_r toe | 26 | +0.3 | — |
  | foot_r heel | 43 | +0.8 | — |
  | thigh | 47 | −2.0 | — |
  | head | 162 | −2.8 | — |
  | chest | 52 | −6.8 | — |
  | hands | 640 | −3.2 | — (catches hold the ball inside the 5 cm hand capsule) |

- This confirms the table frame, the joint frame and the capsule axis together.

---

## 4. Joint frames `F_b` (OBS live, exact)

- `F_b` is the rotation from joint frame to anim bone frame. In the identity-bind anim (Fox) bone frame, the plugin stores:
  - offset' = `F_b`·centre/100;
  - q' = `F_b`·q.
- So `F_b = q'·conj(q)`. It is identical for every human read (ids 0, 2). The offset reproduction error is 0.0000 m.

| Bone | `F_b` (x, y, z, w) | GF `W·Rs` angle error |
|---|---|---|
| thigh_l / thigh_r | (0.4786, −0.5205, −0.4788, 0.5204) / (0.4788, −0.5204, −0.4786, 0.5205) | 0° |
| leg_l / leg_r | (0.5257, −0.4729, −0.5258, 0.4728) / (0.5258, −0.4728, −0.5257, 0.4729) | 1.5° |
| foot_l / foot_r | (0.1874, −0.6818, −0.1876, 0.6818) / (0.1876, −0.6818, −0.1874, 0.6818) | 5.8° |
| belly | (−0.4789, −0.5202, 0.4789, 0.5202) | 0° |
| chest | (−0.4781, −0.5210, 0.4781, 0.5210) | 0° |
| head | (−0.5, −0.5, 0.5, 0.5) | 0° |
| upperarm_l / upperarm_r | (0.7063, 0.0339, −0.0339, 0.7063) / (−0.0339, 0.7063, 0.7063, 0.0339) | 0° |
| forearm_l / forearm_r | (0.7047, −0.0586, 0.0586, 0.7047) / (−0.0586, −0.7047, −0.7047, 0.0586) | ≈22–28° |
| hand_l / hand_r | (0.7071, 0.0031, −0.0031, 0.7071) / (−0.0031, 0.7071, 0.7071, 0.0031) | ≈22° |

- The Kabsch fit of `hit_offset` gives the same frames independently: foot (0.1875, −0.6827, −0.1867, 0.6811) with n = 1,505; hand_l (0.7008, 0.022, −0.011, 0.7129).
- How to place a capsule in world space:

  ```
  centre_w = jointPos_w(b) + R_animWorld(b)·F_b·(centre/100)
  axis_w   = R_animWorld(b)·F_b·q·Ŷ
  ```

  `R_animWorld` is the posed anim-bone world rotation (identity at bind).
- Bones with no capsule (hip, neck, shoulders): `F_b` was not read. Use `W·Rs` (within about 6° of the shoulder fit).
- Open: which file or rule PES derives `F_b` from. The plugin init is `FUN_141b130b0`; not decompiled.

---

## 5. Live confirmation (OBS)

- State: a match loaded with 24 valid `AnimePlayer`s (vtable 0x1425e34d8, secondary at +0xab8) and per-player id at +0x2458:
  - ids 0 and 11 are the goalkeepers: they use the 15-capsule table, OBS;
  - ids 1–10 and 12–21 are outfielders: 11 capsules;
  - ids 22 and 24 are officials: ball-hit and PvP sets only, no HumanLeg.
- `collision::Human`: +8 id, +0xC kind, +0x10 → part objects, +0x18 → definition table, +0x20 count. All matched §2.
- Part object (`collision::Object`): radius at +0x80, segment endpoints at +0x90 and +0x9C as (0, ∓half, 0).
  - Read: thigh 0.095 ± 0.195; head 0.095 ± 0.075; upper arm 0.055 ± 0.145.
- The bone bindings in `AnimConvertBoneSpacePlugin` (AnimePlayer+0xa08 → +0x120; array at +0xa8, count at +0xa0) list exactly the capsule bones, in table order. They give §4.
- The census in §2.3 was taken in the same session.
- Earlier, in menus: stale `collision::Human` blocks only (0 AnimePlayers).

## 6. Killed hypotheses

| Hypothesis | What killed it |
|---|---|
| The frig contains collision shapes | Full byte accounting (§1) |
| Player volumes are loaded from data files | No ph, geom or hkx files for characters anywhere in the VFS; the tables are exe `.data` |
| `hit_offset` is in the identity-bind anim bone frame | Median position error 25 cm while the lengths agree to 0.04 cm, so it is a rotated frame |
| GF's `W` is the joint frame for hands and feet | 22° and 6° off the live `F_b` |
| Root scale 1/20480 | 21 cm median error against 0.07 cm at 1/16384 |
| The capsule axis is local Z | `FUN_141b98150` passes the endpoints from request +0x28/+0x34, i.e. Y; the live endpoints are (0, ∓h, 0) |
| Per-bone physique scale enlarges the capsules live | The live radius is unscaled for scale 1.019 |
| The floats at 0x1425efa30 are per-bone radii | They are pooled scalar constants used by a movement function |
| My first byte-pattern xref index | False positives (e.g. a bitmap loader attributed to `collision::Human`). Replaced with a capstone RIP-relative index |
| The 16-float block at 0x143519340 (24, 6, 6, 6, 9, …, then 47.12, 60, 2.7, …) is the capsule table | No code reference; unrelated to the live sizes. INF (low): ragdoll part parameters in the 16-part order of 0x142b45df0. Open |

## 7. Open items (what was tried)

1. **Touch-kind classifier code** (§3.2). Tried: the stats observer, the name-table xrefs and the script enum tables.
2. **When the physique scale is written relative to capsule creation**, and whether a later path rescales the capsules. Tried: one live read; it shows unscaled capsules.
3. **`danger_data` and kind-3 consumers.** Not traced.
4. **`hit_body_angle` semantics.** It is not the root yaw.
5. **Gani unit name hashes.** Brute force failed (§1).
6. **Kind 7 parts and the 16-part map.** INF (low): ragdoll bodies from `And::RagdollPlugin` / `match::RagdollManager`. Not traced.
7. **Source of `F_b`** (`FUN_141b130b0`).

## 8. Minimal reimplementation recipe

1. Per human, build the capsules in §2.1:
   - goalkeepers: 15 ball-hit capsules;
   - outfielders and officials: 11;
   - players: + 2 shins;
   - everyone: + 1 PvP.
   Use the fixed sizes in metres = table/100.
2. Attach each capsule to its anim bone using `F_b` (§4).
3. Ball: sphere with r = 0.1087 m. Filter contacts with the kind mask (§2.4). Let actions set per-bone modes: 2 = sensor, 12 = off, 0x11 = default.
4. Intended touches: pick the clip, then align the root so that at `hit_frame` the ball centre is at

   ```
   joint_w + R_animWorld·F_b·hit_offset
   ```

   (equivalently, `ball_pos` in the clip frame). IK may absorb up to the `ik_offset_*` distances between `ik_start` and `ik_end`.
5. Root positions in clips: raw/16384 m.

## 9. Reproduction (`<scratch>/CollisionRE/`)

| Script | What it does |
|---|---|
| `frig.py` | frig dump |
| `kabsch.py` | hit frames; root scale; `hit_offset` frame fit (also `fkfit3.py`, `fquat.py`) |
| `shapes.py <table>` | capsule tables |
| `touchdist_y.py` | the §3.3 gaps |
| `buildxref.py` | exact RIP-relative xref index (`xref_exact.pkl`), queried with `xx.py` |
| `dec.sh` | headless decompile on a private project copy |
| `liveap.py`, `liveparts2.py`, `fb_live.py` | live reads (sudo, `/proc/<pid>/mem` only) |
| `modes.py` | per-action bone modes |

Inputs: `anim_infos.json`, `m0–9.mtar` and `body.skl` (via `pesfmt_dump cat`), and `rig.json` (`pesfmt_dump rig`).
