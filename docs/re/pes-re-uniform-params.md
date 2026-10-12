# PES 2021 uniform material parameters at run time — evidence report

Main session, 12-10-2026. Sources:
- the fully analysed image (headless decompiles on a copy of the analysed Ghidra project);
- the install through `src/pes`;
- read-only `/proc/<pid>/mem` reads of the running game, mid-match.

Tags: OBS (observed, and how) or INF (inferred, with confidence).

## 1. What the fmdl carries (OBS, `matprobe` on the install)

Uniform materials in the files carry defaults that the engine overrides at run time.

| Material (file) | Shader / technique | Params as stored |
|---|---|---|
| `uni_shirts`, `uni_collar` (`nocloth/shirt_out.fmdl`) | `pes_3ddf_shirt_nb` / `pes3DDF_Shirt_NB_NC`, `pes_3ddf_collar` / `pes3DDF_Collar_NC` | MatParamIndex_0 40, BlendNormalXParam 0, BlendNormalYParam 0.666, RepetitionParam 80, BlendCoeff 0, BlendBoost 0, PatchAnisoRoughness 0.4, PatternIndex 1; tex `Pattern_Tex_LIN` = `uni_pattern.dds` only |
| `uni_socks` (`common/socks_middle.fmdl`) | `pes_3ddf_uniform` / `pes3DDF_Uniform` | MatParamIndex_0 147, Repetition 50, PatchAnisoRoughness 0.4, PatternIndex 1; tex Normal `socks_nrm`, Spec `dummy_srm`, Pattern `uni_pattern` |
| `uni_pants`, `uni_pants.001` (`common/pants_out_sub.fmdl`) | `pes_3ddf_pants_nb` / `pes3DDF_Pants_NB_NC` | MatParamIndex_0 39, BlendNormalX 1, BlendNormalY 0.666, Repetition 35, PatchAnisoRoughness 0.230159, PatternIndex 1, NumberScaleX/Y 12.8, NumberOffsetX −11, NumberOffsetY −1.78; tex Normal `pants_normal`, Pattern `uni_pattern`, Number `dummy` |

- The live values (pes-re-shaders.md §6) differ in `Repetition.yzw`, `BlendNormalX.*` and `MatParamIndex`. So the runtime writer decides those values.
- UV1 = fmdl vertex attribute type **9**. OBS: every mesh of `sleeve_short_001.fmdl` has a UV1 set as long as its positions, distinct from UV0 (GodotFootball test `FmdlInstall.UniformMeshesCarryUv1`).

## 2. Who writes the params (OBS, StrCode32 immediates in the image)

| Param | StrCode32 | Code sites (containing function, by `.pdata`) |
|---|---|---|
| RepetitionParam | 0x40547b2f | `FUN_141a85770`, `FUN_141a85c70`, `FUN_141a86830`, `FUN_141a874a0`, `FUN_141a87d50`; also 0x141a96f70 in `FUN_141a96d50` (marking) |
| BlendNormalXParam | 0xaa51f6fe | `FUN_141a84010` (0x141a8554b, 0x141a8555c, 0x141a85576) |
| BlendNormalLegParam | 0x7b9e39d1 | `FUN_141a84010` (0x141a8558a) |
| MatParamIndex_0 | 0xe38487ff | `FUN_141a87f50`; also 0x1417009f5, 0x141a3592e, 0x141a41286 |
| PatternIndexParam | 0xf5536710 | **no** immediate anywhere: never written at run time |

The setter is `FUN_141697f30(materialSet, materialId, paramHash, const float4*)`; textures use `FUN_141697f70`. The getter of the current value is `FUN_141695460(set, id, hash)`.

## 3. `RepetitionParam` (OBS)

- `FUN_141a85770` (one part family) and `FUN_141a874a0` (the shirt family: it also sets Base 0x7be81b61, Spec 0x6b98b10e unless material 0x2310a08c, Badge 0xc45cb7b2, Stain 0xcbe9de46, StainMask 0x87143ee6, and four more vectors 0x49a1638d / 0x59d31422 / 0x2e2f30c1 / 0xf750380c) build Repetition as:
  ```
  Repetition = default (DAT_142a83c70.. = 80.0, 0.8, 1.25, 0)       // on disk, VA − 0x140000A00
  if kit:  p = FUN_141960c70(kit) = *(kit+8)->+0x14   (= KitConfig ShirtPattern, pes-kit-assembly.md §1)
           FUN_141961050(table, p, &x, &y, &w):  x = T[+0xd8 + 4p], y = T[+0xf4 + 4p], w = T[+0x110 + 4p]
           Repetition = (x, y, float(p), 0)
  ```
  The table pointer is `*(FUN_1419a49a0(mgr)+...)+0x10`, where `FUN_1419a49a0(m) = *(*(m+8)+8)`.
- `FUN_141a87d50(parts, flag)` rewrites `.w` only: `.w = flag ? 0 : DAT_142565600`, keeping `.xyz`.
- **Live check (mid-match):**
  - kit `752_DEF_3rd` has ShirtPattern 1 and kit `754_DEF_2nd` has 0, matching the live shirt `Repetition.z` 1 / 0 (pes-re-shaders.md §6).
- **The table, live:** at heap 0x1f375c80 (= T+0xd8), seven x = 25.0, seven y = 50.0 and seven w = 1.25, one entry per ShirtPattern 0..6. With the live shirt `(25, 50, p)`, Repetition is fully explained for shirts and collars.
  - The block around it (T) is not an object (no vtable): its first u32s are 0x153, 0, 0x42.
  - INF (high): T is a loaded data file, with the pattern table at file offset 0xd8. Search for the source file: §5.
- **Still open for pants and socks:** live `(35, 0, 7, 0)` and `(50, 0, 8, 0)`. Their `.x` equals the fmdl default (35 / 50), but `.z` = 7 / 8 is not a kit pattern. The writers `FUN_141a85770` / `FUN_141a85c70` / `FUN_141a86830` (pants, socks) are decompiled but not yet attributed.

## 4. Idle hand fallback (cross-reference)

Resolved in pes-re-animselect.md §11.3.1 (same session).

## 5. Open / in progress

- **The file holding T:** a byte search for the 0x54-byte x/y/w table is running over 2,608 small `.bin` files under `uniform`/`character0`/`constant`. Recorded here when it finishes.
- **BlendNormalX/Leg values** (`FUN_141a84010`) and **MatParamIndex** 147–151 (`FUN_141a87f50`): decompiled but not yet read.
- **GhidraMCP:** does not load in Ghidra 12.1.2, because the extension was built for 11.3.2 and needs a rebuild.
