# PES 2021 `.ftex`: texture arrays, cubes, volumes, `.ftexs`, chunk rules, and a full census

Scope: the ftex variants that `src/pes/ftex.cpp` (a port of GF `ftex.py`) does not handle, plus the reader bugs found while measuring them.
Evidence: a full decode of all **65,339** `.ftex` files reachable by the game.
- 51,215 come from the VFS: Data/ cpks, then download/ 4cc cpks, then sider.
- 14,124 come from the 54 nested stadium cpks `Asset/model/bg/*.cpk` inside `Data/dt30_g4.cpk`, which pesfmt_dump does not index.

Tags:
- **OBS** = observed, with the method given.
- **INF** = inferred, with a confidence level.

"Official" means the file wins from a `Data/dt*.cpk` (or a nested bg cpk). "Mod" means it wins from a `download/4cc_*.cpk`. Every download cpk in this install is a 4cc pack. No `.ftex` is served from the sider root (OBS, `origin.tsv`).

---

## 0. Summary for the reimplementer

1. **The header field offsets in the current reader are wrong.**
   - `textureType` is the u32 at **+0x1C**, not +0x18. The u32 at +0x18 is 0 in all 65,339 files.
   - The array slice count is the u16 at **+0x22**.
   - So the current reader never detects a cube. It returns face 0 only, and it returns slice 0 only of every array (OBS, §6).
2. **Arrays** (`textureType & 0x10`, slices = u16 @+0x22):
   - The mip table is image-major: entry `i` → slice `i / mipCount`, mip `i % mipCount`.
   - The table holds `slices × mipCount` 16-byte entries.
   - Every entry has its own chunk table and zlib chunks. There is no array-specific compression.
3. **Cubes** (`textureType & 0x4`): the same layout with 6 faces. Arrays of cubes never occur.
4. **Volumes**: there is no type bit. `depth` (u16 @+0x0E) > 1 marks a volume. Each mip blob holds `depth` consecutive 2D slices.
5. **`.ftexs`: not used by PES 2021.**
   - 0 files in the install carry `ftexsCount > 0`.
   - 0 of 553,302 mip entries carry a nonzero ftexs number.
   - The install contains no `.ftexs` path.
   - The current "reject" behaviour is therefore correct for this install (§4).
6. **Two chunk-rule bugs in the current reader** affect official files (OBS, §3.3):
   - **Bug 1.** A chunk whose compressed size equals its uncompressed size, with the raw flag not set, is treated as stored. On official files, every such chunk is zlib: 3,056 chunks in 2,949 files. 997 of those files are BC7 kit textures under `character/uniform`.
   - **Bug 2.** A stored chunk whose u16 size fields are 0 actually holds 65,536 bytes. The reader copies 0 bytes, so `face_nrm` mip 0 comes out 65,536 bytes short (20 official files).

---

## 1. Header (64 bytes, little-endian)

OBS: every field below was tabulated over all 65,339 files (`census2.tsv`). The value distributions are shown as `value:count`.

|Off|Type|Field|Values observed|Notes|
|---|---|---|---|---|
|0x00|char[4]|magic|`FTEX`|all|
|0x04|f32|version|2.04 (`0x40028F5C`) 46,561 · 2.03 (`0x4001EB85`) 18,778|Both versions are official; the layout is identical.|
|0x08|u16|pixelFormat|2:26,043 · 4:20,475 · 11:13,780 · 9:4,334 · 3:322 · 0:238 · 12:96 · 14:31 · 10:20|1, 8, 13 and 15 never occur.|
|0x0A|u16|**width**|1…8192|OBS: shirts_normal +0x0A=1024, +0x0C=2048, and its decoded slices are portrait (§5). GF's docstring says "height, width" but its code reads width first; the code is right.|
|0x0C|u16|height| | |
|0x0E|u16|depth|1:61,178 · 0:3,931 (all mod) · 16:134 · 33:96|>1 = volume texture (§2.3). Treat 0 as 1.|
|0x10|u8|mipCount|1…14|Mips per image (face/slice).|
|0x11|u8|nrtFlag|2:65,330 · 0:9|0 only on the 9 official `*_nrt*` files (e.g. `gr_dither_nrt_32b`, `palette_h_nrt_nmp_32b`). INF (medium): a "no render-target / no streaming" marker. The decode ignores it.|
|0x12|u16|flags|0x0111 (2.04 official) · 0x0011 (2.03) · 0x0000 (53 official)|Decode ignores it.|
|0x14|u32|unknown|1 (3 mod files: 0)|Decode ignores it.|
|0x18|u32|unknown|**0 in all 65,339**|Not textureType (§7, killed hypothesis).|
|**0x1C**|u32|**textureType**|see §1.1| |
|0x20|u8|ftexsFileCount|0 in all|§4|
|0x21|u8|additionalFtexsFileCount|0 in all|§4|
|**0x22**|u16|**arraySize**|0:65,318 · 2:16 · 6:3 · 7:1 · 18:1|0 = not an array.|
|0x24|12 B|zero|0 in all| |
|0x30|16 B|hash|58,211 nonzero / 7,128 all-zero|Not needed for decode.|

Then the mip table at 0x40: `images × mipCount` entries, where `images = (type&4 ? 6 : 1) × max(1, arraySize)`. Pixel data follows (§2).

### 1.1 textureType bits (u32 @ 0x1C)

OBS counts (all files):
- 0x01 30,511
- 0x09 17,895
- 0x03 16,411
- 0x05 198
- 0x07 192
- 0x1000003 107
- 0x11 21
- 0x0B 2
- 0x00 2

Each value below is cross-tabbed against file-name suffixes over the official files only (OBS):

|Bit|Meaning|Evidence|
|---|---|---|
|0x01|INF (high): "2D-style texture, has data". Set on everything except 2 files.|Type 0x00 occurs only on `face/montage/mark.ftex` and `Fox/Asset/ggx_lookup_rgba.ftex`. Both decode as plain 2D.|
|0x02|INF (high): **sRGB colour**|Official type 0x03 = 11,211 `_bsm`. Type 0x01 = `_srm` 10,856, `_trm` 4,297, `_alp` 2,586. Type 0x07 = sRGB cubes (`_cbm`); type 0x05 = linear cubes (reflection).|
|0x04|**cube** (OBS)|Every 0x05/0x07 file has exactly 6×mipCount entries, and all of them decode to exact sizes.|
|0x08|INF (high): **normal map** (linear, tangent space)|Official type 0x09 = 9,840 `_nrm`, 91 other, 2 `_bsm`.|
|0x10|**2D array** (OBS)|All 21 type-0x11 files have arraySize > 0, and all of them decode to exact sizes.|
|0x1000000|unknown, mod-only|107 files, all from 4cc packs. Decoding ignores it.|

---

## 2. Mip table and data layout

### 2.1 Mip entry (16 bytes)

|Off|Type|Field|Observed|
|---|---|---|---|
|+0|u32|offset of the mip block (from file start)|16-byte aligned in every official entry checked|
|+4|u32|uncompressed size|OBS: equals the exact D3D size of that mip for every official entry (§3.1)|
|+8|u32|"compressed size"|OBS: = byte extent of the mip block (chunk table + chunk data), rounded up to 16. In other words, the block is `[offset, offset+compSize)` and is zero-padded. It does **not** equal sum(chunk cs) or sum+8n (§5). Decode does not need it.|
|+12|u8|mip index|OBS: equals `i % mipCount` in all 553,302 entries|
|+13|u8|ftexs number|0 in all 553,302 entries (§4)|
|+14|u16|chunkCount|553,289 entries chunked. Only 13 entries (2 files, type 0x00) use chunkCount=0 with compSize=0, meaning raw data at offset.|

**Table order (OBS):** image-major, mip-minor. Entry `i` has image `i / mipCount` and mip `i % mipCount`, so all mips of slice/face 0 come first, then slice/face 1, and so on.
- This is exactly the D3D11 subresource index order: `D3D11CalcSubresource(mip, slice, mipLevels)`.
- INF (high): the engine uploads entries 1:1 as subresources.

**Data order in the file (OBS):** it differs from the table order.
- Official files (49,549 multi-entry files): **smallest mip first, and within one mip level all slices/faces in order**. The file holds all slices' 4×4 mip, then all slices' 8×8 mip, …, then all slices' largest mip.
- 4,872 mod files store per image, largest mip first.
- A reader must follow the table offsets and never assume adjacency. The current reader already does.

Example (OBS, `uni_pattern`):
- entry 0 (slice 0, mip 0) is at 0x4FED0;
- entry 6 (slice 0, mip 6) is at 0x820;
- entry 13 (slice 1, mip 6) is at 0x840;
- the first data starts at tableEnd = 0x40 + 126×16 = 0x820.

### 2.2 Chunked mip block

At `offset`: `chunkCount` × 8-byte chunk records, then chunk data.

|Off|Type|Field|
|---|---|---|
|+0|u16|compressed size (`cs`). **0 = 65,536**|
|+2|u16|uncompressed size (`us`). **0 = 65,536**|
|+4|u32|bit 31 = stored raw; bits 0–30 = data offset relative to the mip block start|

OBS: the sample is 795 chunk tables, from the array files plus the §3.3 picks; one mod file (`bismarck so cute`) is among them.
- In 790 of them the first chunk's relative offset is `8 × chunkCount`, so the data follows the table directly.
- The chunk size is **not fixed**, so a reader must use each chunk's `us`. `us = min(chunkSize, remaining)` holds in every sampled official table:
  - official 2.04 files use 64 KiB (618/618 sampled mip tables);
  - official 2.03 files use 64 KiB (most) or 16 KiB: ball `*_c_bsm_alp`, `effect_*` (OBS);
  - mod tools use 16 KiB or irregular splits.
- 39,613 files contain at least one chunk with `us` field 0 (= 64 KiB).
- Each chunk is an independent zlib stream (`78 xx` header) or stored bytes.

**Chunk decode rule. This is the correct one; the current one is wrong (§3.3):**

```
cs  = u16 @+0 or 65536 if 0
us  = u16 @+2 or 65536 if 0
raw = rel & 0x80000000
src = blockOffset + (rel & 0x7FFFFFFF)
if raw:  out += bytes[src : src+cs]               # flagged chunks: cs == us always (0 mismatches census-wide)
else:    out += inflate(bytes[src : src+cs])      # official: always succeeds and yields us bytes
         # mod tolerance only: if inflate fails and cs == us, take the bytes raw
```

The concatenated chunks of one entry make up that mip's full data. Its length equals the entry's uncompressed size (+4), with no exception among official files.

### 2.3 Per-variant image layout

|Variant|How detected|Entries|Bytes of one entry|
|---|---|---|---|
|2D|type & 0x14 == 0, depth ≤ 1|mipCount|`mipSize(w>>m, h>>m)`|
|Cube|type & 4|6 × mipCount, face-major (face f = entries f·mipCount…)|same as 2D|
|2D array|type & 0x10, slices = u16@0x22|slices × mipCount, slice-major|same as 2D|
|Volume|depth > 1 (type 0x01, no extra bit)|mipCount (only 1 occurs)|`mipSize(w>>m, h>>m) × max(1, depth>>m)`: `depth` consecutive XY slices|

`mipSize` for block formats is `ceil(w/4)·ceil(h/4)·blockBytes`. blockBytes is 8 for pf 2 (BC1) and pf 8 (BC4), and 16 for pf 3/4/9/10/11. Uncompressed formats use w·h·bpp: pf 0 → 4, pf 1 → 1, pf 12 → 8, pf 13 → 16, pf 14 → 4, pf 15 → 4. OBS: this formula matches the u32 at +4 for every one of the official entries.

INF (high): cube face order is the table order, which is D3D11's +X, −X, +Y, −Y, +Z, −Z (subresource order, see §2.1). Godot's `Cubemap` layer order is the same. It is not verified visually.

---

## 3. Verification

### 3.1 Census-wide strict decode (`<scratch>/FtexArrayRE/census.cpp`)

The census links the project's own `libpesfmt.a` for VFS/cpk access and inflate. Per file it:
- parses the header as in §1;
- walks all `images × mipCount` entries and decodes every chunk with the §2.2 rule;
- checks: mip index byte == `i % mips`; ftexs number == 0; inflated chunk length == `us`; mip length == u32 @+4; and u32 @+4 == the exact `mipSize × depth` (§2.3);
- checks byte-range coverage (no overlap) and data order;
- calls the current `pes::ParseFtex` for comparison.

Nested bg cpks are read with VFS → scratch file → `CpkArchive` and deleted after each one.

Result (OBS):
- **official 50,686 / 50,686 OK**: every entry exact, no overlap, no out-of-bounds, no inflate failure.
- mod: 14,170 OK and 483 fail. Every failure is a 2.03/0x0011 file from a 4cc pack. The causes:
  - mips below 4×4 are stored with the w·h/… size instead of the block minimum;
  - some chunks have `us=0` that means 0 bytes (an empty zlib stream);
  - one file (`st019/.../bismarck so cute.ftex`, width 4084) is truncated, and the current reader also fails on it.
  - INF (high): these are mod-tool artefacts, not format variants.
- Trailing bytes: official 2.04 files end within 16 bytes of the last block. 4,657 official 2.03 files have a zero tail exactly `tableEnd` bytes long after 16-byte padding (OBS). It is unused.

### 3.2 Full decode of every array texture (`<scratch>/FtexArrayRE/verify.py`)

`verify.py` is pure Python. It asserts `idx == mip`, the ftexs number == 0, each chunk's inflated length == `us`, each mip's length == u32 @+4 == the D3D size, and that every zlib stream terminates. Then it decodes mip 0 of every slice to RGBA (Pillow BC7) and writes a contact sheet. Verbatim output:

```
uni_pattern.ftex: size 1256032 ver 2.04 pf 11 256x256 depth 1 mips 7 nrt 2 flags 0x0111 u14 1 u18 0 type 0x11 ftexs 0/0 arraySize 18 -> images 18 entries 126 tableEnd 0x820
  all 126 entries decoded; decoded bytes 1572768 == expected 1572768
  data order: mip smallest->largest, images interleaved inside each mip level; first data 0x820; last end 0x132a59 / file 0x132a60; gap 1093 B; overlap 0 B
   {'chunks': 126, 'raw_flag': 28, 'raw_eq': 0, 'us0': 18, 'cs0': 0, 'zlib': 98, 'zlib_tail': 0, 'comp_eq_sum': 0, 'comp_eq_sum8': 1, 'off_al16': 126, 'chunk_al16': 0}
  mip0 distinct images: 15/18; sheet uni_pattern_slices.png
shirts_normal.ftex: size 11644928 ver 2.04 pf 11 1024x2048 depth 1 mips 10 nrt 2 flags 0x0111 u14 1 u18 0 type 0x11 ftexs 0/0 arraySize 6 -> images 6 entries 60 tableEnd 0x400
  all 60 entries decoded; decoded bytes 16777248 == expected 16777248
  data order: mip smallest->largest, images interleaved inside each mip level; first data 0x400; last end 0xb1aff9 / file 0xb1b000; gap 452 B; overlap 0 B
   {'chunks': 294, 'raw_flag': 19, 'raw_eq': 0, 'us0': 252, 'cs0': 0, 'zlib': 275, 'zlib_tail': 0, 'comp_eq_sum': 0, 'comp_eq_sum8': 3, 'off_al16': 60, 'chunk_al16': 29}
  mip0 distinct images: 6/6; sheet shirts_normal_slices.png
shirts_tight_normal.ftex: size 9017280 ver 2.04 pf 11 1024x2048 depth 1 mips 10 nrt 2 flags 0x0111 u14 1 u18 0 type 0x11 ftexs 0/0 arraySize 6 -> images 6 entries 60 tableEnd 0x400
  all 60 entries decoded; decoded bytes 16777248 == expected 16777248
  data order: mip smallest->largest, images interleaved inside each mip level; first data 0x400; last end 0x8997be / file 0x8997c0; gap 515 B; overlap 0 B
   {'chunks': 294, 'raw_flag': 17, 'raw_eq': 0, 'us0': 252, 'cs0': 0, 'zlib': 277, 'zlib_tail': 0, 'comp_eq_sum': 0, 'comp_eq_sum8': 0, 'off_al16': 60, 'chunk_al16': 27}
  mip0 distinct images: 6/6; sheet shirts_tight_normal_slices.png
shirts_wet_normal.ftex: size 11612544 ver 2.04 pf 11 1024x2048 depth 1 mips 10 nrt 2 flags 0x0111 u14 1 u18 0 type 0x11 ftexs 0/0 arraySize 6 -> images 6 entries 60 tableEnd 0x400
  all 60 entries decoded; decoded bytes 16777248 == expected 16777248
  data order: mip smallest->largest, images interleaved inside each mip level; first data 0x400; last end 0xb1317d / file 0xb13180; gap 451 B; overlap 0 B
   {'chunks': 294, 'raw_flag': 18, 'raw_eq': 1, 'us0': 252, 'cs0': 0, 'zlib': 275, 'zlib_tail': 0, 'comp_eq_sum': 0, 'comp_eq_sum8': 2, 'off_al16': 60, 'chunk_al16': 31}
  mip0 distinct images: 6/6; sheet shirts_wet_normal_slices.png
pants_normal.ftex: size 6886560 ver 2.04 pf 11 1024x1024 depth 1 mips 9 nrt 2 flags 0x0111 u14 1 u18 0 type 0x11 ftexs 0/0 arraySize 7 -> images 7 entries 63 tableEnd 0x430
  all 63 entries decoded; decoded bytes 9786672 == expected 9786672
  data order: mip smallest->largest, images interleaved inside each mip level; first data 0x430; last end 0x69149f / file 0x6914a0; gap 467 B; overlap 0 B
   {'chunks': 189, 'raw_flag': 22, 'raw_eq': 0, 'us0': 147, 'cs0': 0, 'zlib': 167, 'zlib_tail': 0, 'comp_eq_sum': 0, 'comp_eq_sum8': 1, 'off_al16': 63, 'chunk_al16': 19}
  mip0 distinct images: 7/7; sheet pants_normal_slices.png
shirt_pattern_65334.ftex: size 146912 ver 2.04 pf 11 256x256 depth 1 mips 7 nrt 2 flags 0x0111 u14 1 u18 0 type 0x11 ftexs 0/0 arraySize 2 -> images 2 entries 14 tableEnd 0x120
  all 14 entries decoded; decoded bytes 174752 == expected 174752
  data order: mip smallest->largest, images interleaved inside each mip level; first data 0x120; last end 0x23ddf / file 0x23de0; gap 93 B; overlap 0 B
   {'chunks': 14, 'raw_flag': 2, 'raw_eq': 0, 'us0': 2, 'cs0': 0, 'zlib': 12, 'zlib_tail': 0, 'comp_eq_sum': 0, 'comp_eq_sum8': 0, 'off_al16': 14, 'chunk_al16': 0}
  mip0 distinct images: 2/2; sheet shirt_pattern_65334_slices.png
eye_catch_cbm.ftex: size 13808 ver 2.03 pf 2 128x128 depth 1 mips 6 nrt 2 flags 0x0011 u14 1 u18 0 type 0x7 ftexs 0/0 arraySize 0 -> images 6 entries 36 tableEnd 0x280
  all 36 entries decoded; decoded bytes 65520 == expected 65520
  data order: mip smallest->largest, images interleaved inside each mip level; first data 0x280; last end 0x3365 / file 0x35f0; gap 222 B; overlap 0 B
   {'chunks': 36, 'raw_flag': 10, 'raw_eq': 0, 'us0': 0, 'cs0': 0, 'zlib': 26, 'zlib_tail': 0, 'comp_eq_sum': 0, 'comp_eq_sum8': 8, 'off_al16': 36, 'chunk_al16': 0}
  mip0 distinct images: 6/6; sheet eye_catch_cbm_slices.png
lut.ftex: size 183872 ver 2.04 pf 12 33x33 depth 33 mips 1 nrt 2 flags 0x0111 u14 1 u18 0 type 0x1 ftexs 0/0 arraySize 0 -> images 1 entries 1 tableEnd 0x50
  all 1 entries decoded; decoded bytes 287496 == expected 287496
  data order: mip smallest->largest, images interleaved inside each mip level; first data 0x50; last end 0x2ce39 / file 0x2ce40; gap 0 B; overlap 0 B
   {'chunks': 5, 'raw_flag': 0, 'raw_eq': 0, 'us0': 4, 'cs0': 0, 'zlib': 5, 'zlib_tail': 0, 'comp_eq_sum': 0, 'comp_eq_sum8': 0, 'off_al16': 1, 'chunk_al16': 0}
```

Notes on this output:
- `lut.ftex` is `Asset/model/bg/common/lut/#windx11/lut_h_day_game.ftex`: a 33³ RGBA16F volume. 33·33·33·8 = 287,496 (OBS).
- The `raw_eq: 1` in `shirts_wet_normal` comes from this throwaway script's own GF-style rule. The census with the corrected rule shows that this chunk is zlib.
- The "gap" is the zero padding between 16-byte-aligned mip blocks.
- Slice sizes match exactly:
  - uni_pattern: 18 × (65536+16384+4096+1024+256+64+16) = 18 × 87,376 = 1,572,768.
  - shirts: 6 × 2,796,208 = 16,777,248.
  - pants: 7 × 1,398,096 = 9,786,672.

**Visual check (OBS, contact sheets opened and examined):**
- Every slice is a coherent image; none shows shear or chunk-misordering garbage.
- `shirts_normal` slices are portrait 1024×2048 (confirms width at +0x0A).
- `uni_pattern` mip 0 has 15 unique slices out of 18: slices 0 = 2 = 12 and 6 = 10 are byte-identical (OBS, md5 of the compressed block).

Per-slice channel means of mip 0 (OBS, decoded RGBA, 0–255). These are reported for the shader/material owner; this report does not interpret them:
- `shirts_normal`: slices 0, 2, 4, 5 have R ≈ B ≈ 255 with G and A ≈ 127, i.e. a 2-channel normal in G/A. Slices 1 and 3 have R and B ≈ 6–28 (masks / other data; colourful in the sheet).
- `pants_normal`: slices 0–4 are normal maps in the same G/A packing. Slices 5 and 6 are other data.
- `uni_pattern`:
  - even slices: R ≈ B ≈ 220–240, G and A ≈ 122–131 (G/A normal packing);
  - odd slices: G and A nearly constant (G ≈ 194, A ≈ 186, or 223/255), with R and B carrying the pattern;
  - slice 13 differs again: A ≈ 52.
  - INF (medium): 9 (normal, aux) pairs. The shader report decides the semantics.

### 3.3 Current `pes::ParseFtex` versus the truth (`<scratch>/FtexArrayRE/harness.cpp`, OBS)

```
eye_catch_cbm.ftex   cube=0 mips=6 sizes=8192,2048,512,128,32,8,          <- 1 of 6 faces (type read at +0x18 = 0)
uni_pattern.ftex     cube=0 mips=7 sizes=65536,16384,...,16,              <- 1 of 18 slices
lut_h_day_game.ftex  cube=0 mips=1 sizes=287496,                          <- 33x33x33 volume returned as one 33x33 "2D" mip;
                                                                             Image::create_from_data(33,33,RGBAH) would reject the size
face/real/100393/.../face_nrm.ftex  mips=9 sizes=983040,262144,...        <- mip0 must be 1048576 (1024² BC5): a stored
                                                                             64 KiB chunk (cs=us=0) copied as 0 bytes
```

**Bug 1: `cs == us` treated as raw.** It was killed by a census-wide trial inflate (`census2.tsv`). For each chunk with `cs == us` and no raw flag, the census tried zlib:

|Origin|Unflagged `cs==us` chunks that are zlib (inflate OK, output = us)|that are not zlib (truly raw)|
|---|---|---|
|official|**3,056 chunks / 2,949 files**|**0**|
|mod|432 chunks / 417 files|1,068 chunks / 224 files|

- 15 sampled official chunks all started with `0x78`; their mips ranged from 8×4 to 32×32.
- Affected official files by area:
  - `character/uniform` BC7: 997 files;
  - `character/face`: 378 BC5, 373 BC1, 166 BC7, 124 BC3;
  - stadium/demo BC1 and BC3: the rest.
- Effect: the current reader puts zlib bytes into those small mips. The sizes stay correct, so nothing fails loudly, but the pixels are garbage.
- Flagged raw chunks always have `cs == us` (0 mismatches across 137,589 flagged chunks).

**Bug 2: stored 64 KiB chunks.**
- 21 files have a flagged-raw chunk with `cs = us = 0`, meaning 65,536 bytes stored: 20 official `dt36_g4` face_nrm/hair_parts_nrm files and 1 mod file.
- The census decodes them correctly with 0 → 65536.
- The current reader yields a mip 64 KiB short.

---

## 4. `.ftexs` external mip files (question 2)

OBS, install-wide:
- 0 / 65,339 files have u8 @+0x20 or u8 @+0x21 ≠ 0.
- 0 / 553,302 mip entries have byte +13 ≠ 0.
- No `.ftexs`, `.N.ftexs` or `.pftxs` path exists in the VFS (`<scratch>/allpaths.txt`: 0 matches). None exists in the 54 nested bg cpks either; the census extension tally lists none.
- **Conclusion: PES 2021 PC data never uses ftexs. The reader can keep rejecting `ftexsCount > 0`.**

What the engine still knows (OBS, `<scratch>/pesmem.bin`, image base 0x140000000):
- The Fox file-type extension table is a pointer array at `0x143445B90…0x143445DF8`. It contains `ftex` (→ 0x142590770), `pftxs` (→ 0x14259079C), `1.ftexs` … `5.ftexs` (→ 0x142590870…0x142590890) and `6.ftexs` (→ 0x1425908D0). These sit in a row among `lua`, `fmdl`, `fsop`, `ask`, ….
- The format string `".%d.ftexs"` appears 5 times in `.rdata` (0x142A12178, 0x142A13938, 0x142A606B0, 0x142A60760, 0x142A60D90). Three of them neighbour CRI-style tool error strings (`E0006 file write error!!(%s)`, `ps3 file read.`).

INF (medium, from the format string, the extension table and the community FtexTool format for MGSV:TPP):
- When used, mip data for entries with ftexs number N lives in `<same dir>/<stem>.N.ftexs`, N = 1…6.
- Header +0x20 counts those files and +0x21 counts "additional" ones.
- The per-entry offset is relative to that `.ftexs` file.

Not resolved:
- No code xref to `".%d.ftexs"` or the extension table was found. A RIP-relative disp32 brute-force scan over the whole 64 MB image found only one false hit inside `.rdata` itself. An absolute-pointer scan found only the extension table.
- The loader code is therefore not identified, and the split rule (which mips go external) cannot be observed in this install. Nothing here needs it.

---

## 5. Census (question 3)

Source: `<scratch>/FtexArrayRE/census2.tsv`, one row per file. Origins are joined from `origin.tsv`, which records the archive each VFS path wins from. Nested files are listed as `dt30_g4>stNNN.cpk`. All official files pass the strict check; the mod failures are explained in §3.1.

|category|origin|files|textureType (+0x1C)|pixelFormat|arrays|cubes|volumes|ftexs>0|strict-check fails|
|---|---|---|---|---|---|---|---|---|---|
|`asset/model/character/face`|official|21418|0x1:12052 0x3:4745 0x9:4604 0x11:16 0x0:1|2:9122 11:6581 9:4215 4:1500|16|0|0|0|0|
|`asset/model/bg`|official|14478|0x1:5723 0x3:4732 0x9:3732 0x5:166 0x7:123 0xb:2|2:8547 4:5702 12:74 11:60 0:56 14:26 10:8 3:5|0|289|105|0|0|
|`asset/model/character/uniform`|official|8605|0x1:8605|11:6903 2:1365 4:337|0|0|0|0|0|
|`common/demo`|official|5328|0x1:2152 0x3:1750 0x9:1390 0x7:33 0x5:3|2:3322 4:1936 0:46 10:12 12:5 14:5 9:1 11:1|0|36|26|0|0|
|`asset/model/character/face`|mod|5052|0x9:2634 0x3:1848 0x1:569 0x1000003:1|4:3700 2:1212 3:119 0:10 11:7 9:4|0|0|0|0|38|
|`asset/model/bg`|mod|3932|0x3:2168 0x9:904 0x1:708 0x1000003:96 0x7:28 0x5:28|4:2819 2:903 11:103 0:89 12:16 3:2|0|56|96|0|323|
|`asset/model/character/boots`|mod|2688|0x9:1971 0x3:582 0x1:132 0x1000003:3|4:2134 2:530 0:13 3:11|0|0|0|0|67|
|`asset/model/character/uniform`|mod|1550|0x9:1472 0x3:64 0x1:14|4:1090 2:297 3:159 11:2 0:2|0|0|0|0|44|
|`asset/model/character/glove`|mod|581|0x9:422 0x3:146 0x1:13|4:496 2:77 3:4 0:4|0|0|0|0|0|
|`asset/model/character/common`|mod|436|0x9:401 0x3:35|4:385 2:28 3:22 0:1|0|0|0|0|3|
|`asset/model/character/boots`|official|286|0x1:122 0x3:82 0x9:82|2:122 11:82 9:82|0|0|0|0|0|
|`common/demo`|mod|267|0x1:96 0x3:82 0x9:79 0x1000003:7 0x7:3|2:165 4:100 0:2|0|3|2|0|8|
|`asset/model/ball`|official|182|0x1:81 0x3:51 0x9:50|2:109 4:33 11:20 9:20|0|0|0|0|0|
|`asset/model/character/common`|official|156|0x1:81 0x9:34 0x3:31 0x11:5 0x7:4 0x5:1|2:99 4:45 11:8 0:3 9:1|5|5|0|0|0|
|`asset/model/ball`|mod|145|0x9:79 0x3:65 0x1:1|4:140 2:4 11:1|0|0|0|0|0|
|`asset/model` (other)|official|68|0x1:54 0x9:8 0x3:6|2:54 4:14|0|0|0|0|0|
|`asset/model/character` (other)|official|63|0x1:33 0x9:21 0x3:9|2:40 4:22 11:1|0|0|0|0|0|
|`asset/model/game2d`|official|44|0x1:44|2:23 4:14 0:7|0|0|0|0|0|
|`asset/model/character/glove`|official|35|0x1:13 0x3:11 0x9:11|2:13 11:11 9:11|0|0|0|0|0|
|`asset/effect`|official|16|0x1:13 0x3:2 0x9:1|2:9 4:6 0:1|0|0|0|0|0|
|`fox`|official|6|0x1:4 0x7:1 0x0:1|0:4 4:1 2:1|0|1|0|0|0|
|`asset`|official|1|0x3:1|2:1|0|0|0|0|0|
|`asset/model/game2d`|mod|1|0x3:1|4:1|0|0|0|0|0|
|`asset/model` (other)|mod|1|0x1:1|12:1|0|0|1|0|0|

Totals: 65,339 files (50,686 official, 14,653 mod). Mip entries: 553,302. Maximum mipCount is 14 and the largest dimension is 8192.

Header variants:
- official: 2.04/0x0111 45,985 · 2.03/0x0011 4,648 · 2.04/0x0000 36 · 2.03/0x0000 17.
- mod: mostly 2.03/0x0011.

### 5.1 Every array texture (all 21, all official, all BC7, type 0x11)

|Path|Size|Slices|Mips|Winning cpk|
|---|---|---|---|---|
|`Asset/model/character/common/sourceimages/#windx11/uni_pattern.ftex`|256²|18|7|dt00_x64|
|`…/common/sourceimages/#windx11/shirts_normal.ftex`|1024×2048|6|10|dt00_x64|
|`…/shirts_tight_normal.ftex`|1024×2048|6|10|dt00_x64|
|`…/shirts_wet_normal.ftex`|1024×2048|6|10|dt00_x64|
|`…/pants_normal.ftex`|1024²|7|9|dt00_x64|
|`Asset/model/character/face/real/{65334-65337,65339-65349,65351}/sourceimages/#windx11/shirt_pattern.ftex` (16 files)|256²|2|7|dt36_g4|

### 5.2 What each consumer needs

- **Characters (bodies, kits, faces, boots, gloves, balls).**
  - 2D handles everything except the following.
  - **2D arrays**: the 5 common cloth arrays (Pattern/NormalMap bindings, see pes-re-shaders.md §3) and the 16 `shirt_pattern` arrays in face packs.
  - **Cubes**, all in `character/common`, dt00_x64:
    - `eye_catch_cbm`, `eye_catch_cbm_d` and `eye_catch_cbm_n`: 128², BC1, type 0x7;
    - `haircube_cbm`: 128², BC1, type 0x5;
    - `reflection_preset`: 256², BC1, type 0x7.
  - Formats used by characters: 2/3/4/9/11 and 0.
  - Must-fix chunk bugs (§3.3): 997 official kit BC7 files and ~1,041 face files carry zlib `cs==us` chunks, and 20 face normal maps carry stored 64 KiB chunks.
- **Stadiums (`asset/model/bg`, mostly the nested bg cpks) and demo sets.**
  - **Cubes**: 289 bg + 36 demo official (`*_cbm*`, `reflection_*`), pf 2/4 and **pf 10 (BC6H)** for HDR cubes.
  - **Volumes**:
    - `bg/common/lut/lut_*` (96 + mod): 33³ RGBA16F (pf 12), 1 mip;
    - `stNNN*_FILTERLUT` (134): 16³ RGBA8 (pf 0), 1 mip.
  - **pf 14**, INF R10G10B10A2 (GF FORMATS table, DXGI 24): 31 `*_vatB_pos/rot_nomip`, `standsFlag` vertex-animation textures.
  - No arrays.
- **Godot mapping.** INF (high), from Godot 4 API semantics and not executed here:
  - arrays → `Texture2DArray.create_from_images()`, one `Image` per slice carrying all mips;
  - cubes → `Cubemap` with the same 6 images in table order;
  - volumes → `ImageTexture3D.create(format, w, h, depth, false, images)`, where each `images[z]` is the `z`-th `w·h·bpp` slice of the single mip.

---

## 6. Corrected reader algorithm (drop-in spec)

```
hdr: pf=u16@08 w=u16@0A h=u16@0C depth=max(1,u16@0E) mips=u8@10
     type=u32@1C ftexs=u8@20 slices=max(1,u16@22)
reject if ftexs>0 (never occurs in PES21)
faces  = (type & 4) ? 6 : 1
images = faces * slices                    # (type&0x10) is implied by u16@22 > 0
for i in 0 .. images*mips-1:               # table at 0x40, 16 B each
   off,usize,_,mipIdx,_,chunks = <IIIBBH>
   image = i / mips ; mip = i % mips
   data = chunks==0 ? bytes[off : off+usize]          # compSize==0 here in all 13 occurrences
                    : concat(decodeChunk(c) for c in chunk table at off)   # §2.2 rule
   assert len(data) == usize
   out[image][mip] = data                  # volume: data holds max(1,depth>>mip) XY slices
```

---

## 7. Killed hypotheses

1. **"textureType at +0x18 (bit 0x10 = array), slice count u16 at +0x1A"** (pes-re-shaders.md §5). Killed: u32 @+0x18 is 0 in all 65,339 files. The type values 0x11/0x7/0x9 sit at +0x1C, and the slice counts 18/6/7 sit at +0x22 (hex dumps of uni_pattern/shirts/pants/eye_catch_cbm, then the census). The report's bit meanings are right; its offsets are 4 bytes early.
2. **"cube = `u32@0x18 & 4`"** (GF `ftex.py`, `src/pes/ftex.cpp` `FTEX_TEXTURE_TYPE = 0x18`). Killed by the same evidence: the current reader returns `cube=0` and 6 mips for `eye_catch_cbm` (§3.3).
3. **"cs == us without the raw flag means stored, because the smallest mips are stored plain"** (GF `_read_mip` comment). Killed for official data: 3,056 of 3,056 such chunks inflate to exactly `us` bytes, and 0 are raw. It holds only for 1,068 chunks in 224 mod files. The correct order is: try inflate first, fall back to raw.
4. **"u16 chunk sizes are literal"**. Killed: 64 KiB chunks store `us=0` (39,613 files), and stored 64 KiB chunks store `cs=0` as well (21 files). The sizes check only once 0 is read as 65,536.
5. **"Mip data follows the table order"**. Killed: official data is smallest-mip-first with slices interleaved (§2.1). It does not matter for a reader that follows offsets.
6. **"compSize (+8) = sum of chunk compressed sizes (+ table)"**. Killed: it matches in only 1–8 of 36–126 entries per file. It equals the padded block extent (§2.1).
7. **"Volume textures carry a type bit"**. Killed: all 230 volumes are type 0x01 and are recognised by depth > 1 only.
8. **"PES21 uses ftexs sidecars like TPP"**. Killed for this install (§4).

---

## 8. Reproduction (scratch: `<scratch>/FtexArrayRE/`)

The bulk ftex copies have been deleted. Scripts and TSVs are kept.

- `census.cpp`: links `build/libpesfmt.a`. Build and run:
  ```
  g++ -std=c++20 -O2 -I<repo>/src census.cpp <repo>/build/libpesfmt.a -lz -o census
  PES21_ROOT=… ./census <scratchdir> > census2.tsv
  ```
  Runtime is about 6–9 min. The nested bg cpks are copied to `<scratchdir>/nested.cpk` one at a time.
- `origin.cpp`: winning archive per VFS `.ftex` → `origin.tsv`.
- `summarize.py`: produces the census table in §5.
- `verify.py <file.ftex>…`: the strict full decode, exact-size asserts and slice contact sheets (§3.2). Extract its inputs with `pesfmt_dump cat`.
- `harness.cpp`: runs the current `pes::ParseFtex` on a list of VFS paths (§3.3).
- `xref.py`: the RIP-relative xref brute-force over `<scratch>/pesmem.bin` (§4).
