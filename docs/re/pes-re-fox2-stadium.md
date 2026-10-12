# PES 2021 — fox2 stadium scene: format findings (Step M5)

Verified against this install's own files (the user's PES21_ROOT, never committed).
**OBS** = observed with the method given. **INF** = inferred, with confidence.

## 1. The fox2 (FoxDataFile) binary, inside a `.fpkd`

The stadium data lives in `.fpkd` packs, each a wrapper around one `.fox2` file.
The wrapper is the Fox fpk (GzsTool-compatible) parsed by `FpkArchive::Parse`;
the fox2 is the member whose path ends in `.fox2`.

- **OBS.** `Asset/model/bg/st002/#Win/st002.fpkd` (6400 B, 1 entry) holds
  `/Assets/pes16/model/bg/st002/st002_modelset.fox2` (6240 B);
  `goal_post.fpkd` holds `.../common/goal/goal_post.fox2` (6256 B).

The fox2 layout (already parsed by `src/pes/fox2.cpp`; layout doc
`docs/re/pes-re-fox2-format.md`):

- **OBS.** File header 32 B: bytes `f2 'b' 'o' 'x' '5'` (`\xf2box5`),
  u32 entityCount, u32 stringTableOffset, u32 dataOffset (=32), 12 reserved.
- **OBS.** Entities follow from offset 32 in file order; each has a 64-byte
  header (u32 runtime address at +10, u64 class-hash at +0x1c, static/dynamic
  property counts at +0x24/+0x26, block size at +0x30) then its property
  blocks (32-byte header + data, 16-byte aligned). The last entity ends at the
  string table; the string table is `u64 hash + u32 length + bytes` entries,
  zero hash terminates.
- **OBS.** Name hashes are the low 48 bits of StrCode64 (CityHash64 1.0, the
  same `pes::StrCode64` in `src/pes/strcode.cpp`); every class/property name in
  these files resolves.

### 1.1 Value type ids seen (and how `ParseFox2` decodes them)

| id | type | decode (Fox2Value field) |
|----|------|--------------------------|
| 4  | int32 (`kind`, `direction`) | `.i` / `.u` |
| 5  | u32 bitfield (`flags`) | `.u` / `.i` |
| 11 | String (`name`) | `.s` (resolved) |
| 13 | EntityPtr (`transform`, `parent`) | `.u` = runtime address → `Fox2File::ByAddress` |
| 14 | WideVec3 (`transform_scale`, `transform_translation`) | `.v3` (xyz of 4 floats; 4th is padding) |
| 16 | Quat (`transform_rotation_quat`) | `.q` (x,y,z,w) |
| 20 | FilePtr (`modelFile`, `clothFile`) | `.s` (resolved) |
| 21 | EntityHandle (`dataSet`, `children`) | `.u` address |

- **OBS.** `kind` is id 4 (int32), `direction` id 4, `flags` id 5 (u32, always 7
  in these files), `name` id 11, `modelFile`/`clothFile` id 20, `transform` id 13.

## 2. Which entities place a model

- **OBS.** Across the stadium/pitch/goal/cornerflag packages, the model-bearing
  entity classes are `StadiumModel`, `GoalnetData` (nets) and
  `MobCharacterModel` (the corner-flag "staff"). All three carry the same
  placement fields: `modelFile` (FilePtr), `transform` (EntityPtr → a
  `TransformEntity`), plus `name`, `kind`, `direction`, `flags`.
  `LoadStadiumScene` keys off the presence of a resolved `modelFile`, not the
  class name, so it picks up all three.
- **OBS.** The scene also contains non-model entities (`DataSet`,
  `ShearTransformEntity`, `PivotTransformEntity`, `StadiumAnime`) and the
  `Enlighten*` light containers; none carry `modelFile` and are ignored.

## 3. `Stadium.bin` (common/etc/pesdb/Stadium.bin)

- **OBS.** 21560 bytes = **55 records of 392 bytes**. Each record: u16 id at
  +4, a NUL-terminated folder C string at +370 (the rest of the record is other
  stadium metadata we do not need). Every folder reads `"st" + id` zero-padded
  to 3 digits — 55/55 (id 2 → `st002`, id 5 → `st005`, … max id 84 → `st084`).
  `ParseStadiumRecords` reads exactly this; no id is 0.

## 4. `LoadStadiumScene(vfs, id)` — the sources and how the fmdl maps

Folder = `st<id:03d>`; `bg = Asset/model/bg/`. Sources, in placement order:

| source | VFS path of the .fpkd | required? |
|--------|------------------------|-----------|
| scene | `bg/<folder>/#Win/<folder>.fpkd` | yes (else error) |
| day/fine overlay | `bg/<folder>/#Win/<folder>_df.fpkd` | best-effort |
| pitch | `bg/<folder>/pitch/#Win/pitch_<folder>.fpkd` | best-effort |
| goals (+shadow) | `bg/common/goal/#Win/goal_post.fpkd` | best-effort |
| nets (L, R) | `bg/common/goal/#Win/goalnet_{l,r}.fpkd` | best-effort |
| corner flags | per-stadium, else common (below) | best-effort |

### 4.1 FilePtr → sibling .fpk member (verified)

- **OBS.** A model's `modelFile` FilePtr is e.g.
  `/Assets/pes16/model/bg/st002/scenes/center1.fmdl`. The sibling `.fpk`
  (same path minus the `d`) is a Fox fpk whose `FpkEntry::path` is that same
  path with the leading `/` stripped: `Assets/pes16/model/bg/st002/scenes/center1.fmdl`.
  So **member = modelFile with the leading `/` stripped**, matched directly
  against `FpkEntry::path`. Verified for scene, pitch, goal, net and corner-flag
  members.
- **OBS. — models PES cannot ship are dropped.** The 4cc `st002` scene pack
  defines three `StadiumModel`s (`center1`, `front3`, `right2`) in the fox2, but
  the shipping `st002.fpk` contains only `center1.fmdl` and `front3.fmdl`.
  `LoadStadiumScene` emits a `PlacedModel` only when the fmdl is a real member of
  the sibling `.fpk`, so the unshipped `right2` is dropped. This is why "the
  scene fmdls of st002.fpk" (the test's baseline) equals the placed scene models.
- **OBS. — the day/fine overlay for st002 is an empty override.** `st002_df.fpk`
  is 48 bytes (zero entries); its `st002_df.fpkd` fox2 still lists five models
  (`center1_df`, `front2_df`, `left2_df`, `right2_df`, `back2_df`), none of which
  are shipped, so the overlay contributes 0 placed models. (Report §1.1: a 48-byte
  fpk/fpkd is an intentional empty override.)

### 4.2 Corner flags — the per-stadium number is mined, discovered not hard-coded

- **OBS.** A per-stadium corner-flag override lives at
  `bg/<folder>/cornerflag/#Win/cornerflag_NNNNN.fpk{,d}` (st002 → `00108`,
  st071 → `00117`). The `NNNNN` is a per-stadium id (a mined table) — not hard-
  coded. `LoadStadiumScene` discovers it by `VFS::List` on the
  `bg/<folder>/cornerflag/#Win/cornerflag` prefix and taking the single `.fpkd`;
  when the stadium ships no override (st009/st015 have no `cornerflag/` dir), it
  falls back to `bg/common/cornerflag/#Win/cornerflag.fpkd`.
- **OBS.** The per-stadium `cornerflag_NNNNN` package places four `CornerFlag_*`
  `StadiumModel`s at the four pitch corners (st002: translations
  (±52.48, 0, ±33.99)) plus one `MobCharacterModel`. The common package places
  the same four corner flags (`cf_common_2019E_vatB.fmdl`, identity) + one
  `MobCharacterModel`. The corner-flag package ships the same `cf_…_camp_00.fmdl`
  / `cf_…_camp_vat00.fmdl` under `common/cornerflag/`, so both resolve.

### 4.3 Goal / net / shadow placement (PES wins)

- **OBS.** `bg/common/goal/#Win/goal_post.fpkd` places four `StadiumModel`s:
  `goal.fmdl` at **identity** and `goal.fmdl` at **quat (0,1,0,0) = 180° about Y**,
  plus `shadow.fmdl` at identity and at (0,1,0,0). Source:
  `docs/re/pes-re-stadium-camera.md` §1.3 — the goal mesh is authored at −X and
  mirrored for +X by the 180° Y rotation; the AABB is x −54.584…−52.44. This is
  the "goal_post ×2 via 180° Y" placement; no other source places the goals.
- **OBS.** The nets are separate: `goalnet_{l,r}.fpkd` each place one
  `GoalnetData` with `modelFile` = `goal2{L,R}_net.fmdl` (identity transform).
  The `clothFile` (`goalnet2{L,R}.cloth`) is a file but not a fmdl; the static
  net mesh is the fmdl, which is what the renderer needs (the Godot side does
  not simulate cloth).
- **OBS.** In every scene/pitch/goal/cornerflag transform, the only non-identity
  values are the goal/shadow 180° Y quats and the corner-flag translations
  (models are authored in world space — report §1.2).

## 5. What `LoadStadiumScene(vfs, 2)` returns (st002, this install)

**14 PlacedModel:**

| class | count | source fpk |
|-------|-------|------------|
| StadiumModel | 11 | st002 scene (2), pitch (1), goal_post (4), cornerflag_00108 (4) |
| GoalnetData | 2 | goalnet_l, goalnet_r |
| MobCharacterModel | 1 | cornerflag_00108 |

Per source: `st002.fpk` 2 (center1, front3), `pitch_st002.fpk` 1,
`goal_post.fpk` 4, `goalnet_l.fpk` 1, `goalnet_r.fpk` 1,
`cornerflag_00108.fpk` 5. The `_df` overlay contributes 0 (empty fpk).

Non-identity transforms (6): the two goal_post `StadiumModel`s at
quat `(0,1,0,0)` (`goal.fmdl` and `shadow.fmdl`), and the four `CornerFlag_*`
models with the corner translations above (identity rotation).

## 6. Open / not done (intentionally, out of scope for the scene)

- The net `clothFile` is not placed (no cloth sim on the Godot side); only the
  static net fmdl is.
- Lighting (`light/`), crowd/audience (`audi/`), ad boards, `standsFlag`,
  `big_flag`, `staff`, `Enlighten*` containers are out of scope per the step.
- The per-stadium `cornerflag_NNNNN` number is read from the VFS (discovery), so
  it works for any install without a committed table; it is not otherwise
  validated.
