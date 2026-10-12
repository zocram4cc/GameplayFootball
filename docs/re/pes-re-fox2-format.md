# PES 2021 fox2 (binary FoxDataFile) layout (RE report)

Scope: the binary `.fox2` payload inside fpkd packs for stadiums, pitch,
goals, corner flags and the light rig. All facts verified against the real
files below; the FoxKit/FoxTool summary (magic, StrCode64 table, type ids)
was only the starting hypothesis. Offsets are byte offsets into the inner
`.fox2` payload (fpkd entry data), except where "fpkd offset" is stated.

Evidence tags: **OBS** = observed in the bytes. **INF** = inferred, with
confidence. No PES-derived strings, hashes or tables are transcribed here —
only offsets, sizes and structural facts.

Unresolved: whether `ndyn > 0` ever occurs (0 in all 514 entities read); the
meaning of the 4th WideVec3 float (padding/garbage); EntityLink (22) field
semantics beyond the 32-byte element; the 8 trailer bytes after the string
table; the `ent` bytes at entity +2 and the u32 at +0x28.

---

## 1. fpkd wrapper (OBS)

- A stadium `.fpkd` is a `foxfpkdw` pack (see `src/pes/fpk.cpp`) with one
  entry: the `.fox2` payload. The entry table is one 48-byte record at fpkd
  offset 0x30 (u32 offset, u32 size at +0/+8; path string at +16/+24).
- OBS: `st002.fpkd` (6,400 B) entry = {offset 160, size 6,240},
  path `/Assets/pes16/model/bg/st002/st002_modelset.fox2` at fpkd 96..144.
  `pitch_st002.fpkd` (2,576 B): inner at 160, size 2,416.
  `goal_post.fpkd` (6,416 B): inner at 160, size 6,256.
  `cornerflag_00108.fpkd` (9,328 B): inner at 160, size 9,168.
  `goalnet_l.fpkd` (1,792 B): inner at 160, size 1,632.
  `light_st002_af.fpkd`: inner at 160, size 458,864.
- ParseFox2 takes the inner payload (from `FpkArchive` entry bytes), not the
  fpkd itself.

## 2. File header, 32 bytes at offset 0 (OBS)

- Bytes 0..8 of every file read: `f2 62 6f 78 35 00 00 00`
  (`\xf2box5` + 3 NULs). OBS at st002/pitch/goal/corner/st071/goalnet/light
  offset 0.
- u32 at +8 = entity count; u32 at +12 = string-table offset; u32 at +16 =
  entity-data offset (32 in all 7 files); 12 reserved bytes at +20 are zero.
- OBS: st002 offset 8: {11, 5088, 32}; pitch: {3, 1600, 32};
  goal_post: {9, 5344, 32}; cornerflag: {12, 7536, 32}; st071: {9, 3712, 32};
  goalnet_l: {3, 1024, 32}; light_af: {479, 444128, 32}.
- The header comment in `fox2.py` ("FOX2/FOXF magic, n_ent/strtab/dataoff")
  is confirmed except the literal magic, which is `\xf2box5`, not `FOX2`.

## 3. Entity blocks (OBS)

- Entities start at offset 32, in file order, each with a 64-byte header
  (the u16 `hs` field at +0 is 64 in all 514 entities across the 7 files).
- u32 address at +10 (a 32-bit runtime address, e.g. st002 entity 0 at file
  offset 32 has address `0x3172c40`, read at offset 42).
- u64 class-name hash at +0x1c (e.g. st002 offset 60 =
  `0x35116a93df3c`, the table entry `DataSet`).
- u16 static count + u16 dynamic count at +0x24 (dynamic is 0 in all 514
  entities; INF (high): the ndyn path exists for mutated live files only).
- u32 block size `esize` at +0x30 (e.g. st002 offset 80 = 240); the next
  entity starts at block start + esize. OBS entity spans in st002:
  (32:240), (272:1104), (1376:256), (1632:1104), (2736:256), (2992:1104),
  (4096:256), (4352:160), (4512:208), (4720:160), (4880:208), ending at 5088
  = the string table. The last entity ends exactly at the string table in
  all 7 files (light_af: entity region 32..444128, 479 entities).
- Bytes at +2..+10, +14..+0x1c and +0x28..+0x40 vary per entity class but
  were not decoded (open question above); the rest of the header (+0x34..)
  is zero.

## 4. Property blocks (OBS)

- After the 64-byte header come (nstat + ndyn) property blocks. Each block:
  u64 name hash at +0, u8 type id at +8, u8 container at +9, u16 element
  count at +10, u16 data offset at +12 (32 in all 8,963 properties read),
  u16 block size at +14, 16 zero pad bytes at +16..+32, then value data.
- OBS: st002 entity-0 property 0 at file offset 96: hash `0x3391ed17a03a`
  (`name`), type 11, container 0, count 1, data offset 32, block size 48,
  with the 8-byte value at 128. Property 2 at 192: hash `0x63846213e2a3`
  (`dataList`), type 13, container 2, count 3, block size 80, data at 224.
- Block size law (OBS, holds for all 8,963 properties in all 7 files):
  `blockSize = align16(32 + count * stride)` where stride = element bytes
  for containers 0/1/3 and (8 + element bytes) for container 2 (StringMap
  items are u64 key + value). E.g. count-1 u64 scalar: 32 + 8 = 40, aligned
  to 48; the `dataList` above: 32 + 3 * 16 = 80.
- Container ids (OBS): 0 scalar (single value; every StadiumModel field);
  1 dynamic array (`models` 13×4 in cornerflag entity 11, `animeFiles`
  20×1, `spotLights` 13×10 and `timeSpans` 22×9 in light_af);
  2 StringMap (`dataList` in every DataSet: keys are entity-name hashes,
  values are u64 addresses — st002 offset 224: 3 × (key, address));
  3 static list (`children` 21×N: packed u64 addresses, no keys).
- Property-header bytes +16..+32 are zero in all 8,963 properties (OBS);
  the parser requires data offset ≥ 32 and the block inside its entity.

## 5. Value encodings (OBS)

Type ids follow the FoxKit TNames order (confirmed by the observed widths):

- 1 u8 (`captureBounceCount` = 3, light_af), 4 i32 (`drawRejectionLevel` = 8,
  `direction`, `kind`, `drawMode`…), 5 u32 (`flags` = 7, `customBits`),
  8 f32 (`lodFarSize`/`lodNearSize` = −1.0, `lodPolygonSize`),
  10 bool (`isIsolated`/`isDynamic`, 1 byte).
- 11 String / 12 Path / 20 FilePtr: u64 hash resolving through the embedded
  table (OBS §6). E.g. st002 entity 1 (file offset 272) `modelFile` value
  bytes `b6 40 bd b2 d4 34 00 00` = `0x34d4b2bd40b6`, the table entry for the
  center-scene fmdl path.
- 13 EntityPtr / 21 EntityHandle: u64 runtime address (32-bit values, e.g.
  st002 `transform` = `0x31730a0`), dereferenced through `ByAddress`.
  `0` = null (shear/pivot/parent slots). OBS: all 23 + 7 + 25 + 36 + 17 + 7
  EntityPtr/Handle refs in the six small files resolve to an entity start;
  light_af has 6 dangling refs (`shearTransform` → `0x7700/0x7b00/0x8300`,
  `parent` → `0x17800/0x1d200/0x1d600`), which are authoring references to
  entities absent from the shipped file (OBS, §8).
- 14 WideVec3 / 15 Vec4 / 16 Quat / 19 Color: 4 little-endian f32s; Vec3
  takes xyz (the 4th float varies: 0.0 in 248 + 248 cases, 1.0 in 4
  `transform_translation` cases, ~5e−37 denormals in shear/scale cases —
  OBS census, semantics open). E.g. cornerflag entity 2 translation =
  (−52.4866…, 0, 33.9936…, 0.0); goal_post 180°-Y quats = (0, 1, 0, 0).
- 22 EntityLink (light_af only, 322 scalar + 21 arrays): 32-byte elements =
  4 × u64 (a, b, name-or-class hash, target). Scalar `globalSettings` =
  (unresolved, unresolved, `TimeOfDaySettings`-hash, `0x500`); array
  `timeSpans[0]` = (unresolved, unresolved, `TimeSpan1200`-hash, `0x2900`,
  an entity start). Kept raw; only the 4th slot was verified as an address
  (INF, medium — 4th slots outside the entity set, e.g. `0x8300`, coincide
  with the dangling EntityPtr refs, so some 4th slots may be indices).
- Type ids 0/2/3/6/7/9/17/18/24 never occur in the 7 files; the parser still
  decodes 0/1/2/3/4/5/6/7/8/9 by width (OBS widths from `fox2.py`, confirmed
  for 1/4/5/8/10 by the values above).

## 6. String table and hash scheme (OBS)

- The table at the header offset holds {u64 hash, u32 length, bytes} entries
  with no terminator, ended by an 8-byte zero hash. OBS: st002 table at
  5088..6201 (41 entries; first = hash `0x35116a93df3c`, length 7,
  `DataSet`), terminator `00×8` at 6201, then 31 trailer bytes
  (`00×23 + 65 6e 64 00…`, same `end`-flavoured tail in goalnet_l at
  1595..1632 — purpose open).
- Hashes are the low 48 bits of StrCode64 (`src/pes/strcode.hpp`,
  CityHash64 1.0 with seeds): OBS `StrCode64("StadiumModel") &
  0xFFFFFFFFFFFF = 0x212d37ce42ab`, the stored class hash. Value hashes
  match too (OBS §5 `modelFile`; `StadiumModel_C1` → `0x9acd88ea6720`).
  File-name hashes are NOT `gz_hash` (which strips the extension and gives
  a different value — tested and rejected).
- The table carries every resolvable name: class hashes (all 514 entities),
  property hashes (all 8,963), StringMap keys (all `dataList` keys in the 6
  small files) and String/FilePtr values resolve, except one: every
  `DataSet.name` = `0xb8a0bf169f98` (all 7 files), a dataset id with no
  table entry. Unresolved hashes render as `#` + 12 hex digits (the RE
  tools' convention). The parser keeps unresolved String/FilePtr hashes as
  u64 so callers can match them later.

## 7. Object/transform entities (OBS)

- `DataSet` (entity 0 in all 7 files): `name` (String), `dataSet`
  (EntityHandle, 0), `dataList` (EntityPtr StringMap keyed by entity name).
- `StadiumModel` (22 props when complete): `name`, `dataSet`, `parent`,
  `transform`/`shearTransform`/`pivotTransform` (EntityPtr),
  `children` (21/3, empty), `flags` (u32 7), `modelFile` (FilePtr),
  `isIsolated`/`isDynamic` (bool), `lodFarSize`/`lodNearSize`/`lodPolygonSize`
  (f32), `color` (Color 1,1,1,1), `drawRejectionLevel` (8),
  `drawMode`/`rejectFarRangeShadowCast` (0/2), `direction`, `kind`,
  `demoGroup`, `customBits`. OBS: st002 entity 1 at file offset 272
  (address `0x3173030`, esize 1104).
- `TransformEntity` (4 props): `owner` (EntityHandle),
  `transform_scale`/`transform_translation` (WideVec3),
  `transform_rotation_quat` (Quat). OBS: st002 entity 2 at file offset 1376.
- `ShearTransformEntity` (`shearTransform_shear`) and
  `PivotTransformEntity` (`pivotTransform_pivot`,
  `pivotTransform_pivotTranslation`) accompany world-authored models.
  OBS: st002 entities 7..10 at file offsets 4352/4512/4720/4880.
- `GoalnetData` (goalnet_l entity 1): the StadiumModel fields through
  `flags`, plus `modelFile` and `clothFile` (FilePtr), no LOD/color/draw
  fields. `MobCharacterModel` adds `sklFile`; `StadiumAnime` carries
  `animationGroup` (i32), `animeFiles` (FilePtr array), `anmSklFile`,
  `renderSklFile`, `rigFile`, `sklFile`, `models` (EntityPtr array).
  OBS: cornerflag entities 9/11.
- Light rig classes (`TimeOfDay`, `TimeOfDaySettings`, `SpotLight`,
  `PointLight`, `Locator`, `ReflectionProbe`, `TppSky`, `TppAtmosphere`,
  …): same envelopes (name/dataSet/parent/transform/handles/children),
  covered by the generic property decoder; only EntityLink (22) stays raw.

## 8. Dangling references (OBS)

- light_af entity 70/72/76 (`SpotLight.shearTransform`) → `0x7700/0x7b00/`
  `0x8300`; entities 205..215 (`Locator`/`SpotLight.parent`) → `0x17800`;
  entities 471..477 (`Locator`/`ReflectionProbe`/`LightOccludeCube.parent`)
  → `0x1d600/0x1d200`. None of these addresses starts an entity (verified
  against the 479-address set), while the structurally walked parse
  (header → esize chain → `pos == strtab`, 479/479, then 493 table strings)
  is exact. So the file ships references to entities that are not in it —
  ByAddress returns null for them and the parser still succeeds.
