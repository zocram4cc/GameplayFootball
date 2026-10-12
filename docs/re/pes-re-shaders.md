# PES 2021 character shading — evidence report

Scope: how PES 2021 (DX11 build) shades character materials: uniform cloth, skin, hair, eyes and boots. The goal is Godot materials that match.

Tags:
- **OBS** = observed, with the method given.
- **INF** = inferred, with a confidence level.

Evidence lives in `<scratch>/ShaderMaterialRE/`:

| File | Content |
|---|---|
| `fsop.py` | shader-pack extractor and RDEF reader |
| `dxbc/` | extracted blobs |
| `asm/` | `vkd3d-compiler -x dxbc-tpf -b d3d-asm` disassembly of every blob |
| `material_census.json` | fmdl material census |
| `mm2v.py`, `mm2v.json`, `mm2v.txt` | live MaterialModifier2 dump |
| `*.lua` | technique/packing tables |

Shader names below are fsop names; the disassembly for each is in `asm/<name>.ps.asm` / `.vs.asm`.

Live state used for §6: pid <pid>, read only through `/proc/<pid>/mem` at about 20:28 local time. It held two kits: `u0752p3` (team 752, 3rd) and `u0754p2` (team 754, 2nd). There were 73 `MaterialModifier2` instances, plus face/hair/eye materials for one face and arm/thigh/socks materials (INF: a match or preview with two teams). Nothing was written and no input was sent.

---

## 0. Headline facts

1. **OBS: shaders are in four VFS packs** `shaders/dx11/{PesShaders,GrModelShaders,GrSystemShaders,FxShaders}_dx11.fsop`.
   - Each record is `u8 nameLen(incl. NUL), name, u32 vsSize, VS-DXBC, u32 psSize, PS-DXBC`.
   - Every DXBC byte is XOR `0x9C`.
   - All 977 records hold one `vs_5_0` and one `ps_5_0`, and the packs parse end to end with no slack.
   - Material techniques (the fmdl "technique" string) map to fsop shader names through `ShaderAssignList` in the matching `*.lua` (§1.2).
2. **OBS: the renderer is deferred.**
   - The material PS writes a 5-target G-buffer: albedo, octahedral normal + shading-model ID + translucency, roughness/specular/metal + material-preset index, velocity, and a constant.
   - Lighting runs in screen space (`SSLighting_*`). It uses a GGX/Smith BRDF with a Fresnel LUT, plus a translucency wrap term and a per-preset specular tint.
   - Final colour = `albedo·diffuseLight + specularLight` (`DeferredRendering`).
3. **OBS: one texture convention runs through every character shader.**
   - `*_SRGB` = colour (sampled sRGB → linear).
   - `*_NRM` = tangent-space normal with **n.x = R·A·2−1, n.y = G·2−1, n.z = √max(1−x²−y²,0)+1e-4**. This decodes BC5 (A=1) and DXT5nm (R=1) alike.
   - **SRM: R = specular intensity, G = roughness, B = unused** by every character shader examined.
   - **TRM: G = translucency** (quantised to 6 bits).
4. **OBS: "roughness" in SRM.G is remapped** for skin, face, generic Blin, hair and eye:
   - `f(r) = 0.15 + 0.271277r + 0.271277r² + 0.180851r³ + 0.090426r⁴ + 0.036170r⁵` (= 0.15 + 0.85·(15r+15r²+10r³+5r⁴+2r⁵)/47).
   - It is **not** remapped for uniform cloth and boots.
   - Lighting then uses α = max(roughness, 0.02)².
5. **OBS: uniforms take the kit atlas as `Base_Tex_SRGB` on UV0.**
   - Its alpha is a "kit-coverage" mask `a`. Where `a=1` the kit atlas + kit `_srm` decide colour/roughness/specular. Where `a=0` a fabric-pattern texture-array slice decides them.
   - Shirt/collar/sleeve/pants: albedo × `lerp(pattern.r, 0.85, a)`. So an opaque kit is rendered at **85 % of its texel colour**.
   - The fabric weave normal is always added on UV1.
6. **OBS: vertex colour is not used** by any deferred character PS.
   - The COLOR.w the PS reads is replaced in the VS by an object constant (per-object fade).
   - Only `pes3dfw_eyeocclusion` reads vertex COLOR.r (and the pass-through COLOR).
7. **OBS (live):** the engine **overrides `MatParamIndex_0`** for uniform parts to presets 147–151, stored ÷255 (§6). Those presets carry cloth wrap-lighting (0.698) and F0 ≈ 0.035–0.051.

---

## 1. Shader binaries (Q1)

### 1.1 Pack format (OBS: `fsop.py` parses all three packs to their exact end)

| Pack | Records | Size | Holds |
|---|---|---|---|
| `PesShaders_dx11.fsop` | 244 | 2,362,648 B | PES character/uniform/pitch shaders |
| `GrModelShaders_dx11.fsop` | 528 | 6,482,945 B | Fox generic model shaders, `SSLighting_*`, forward |
| `GrSystemShaders_dx11.fsop` | 205 | 1,000,431 B | post, debug views, SSS, sky |
| `FxShaders_dx11.fsop` | not parsed | 815,539 B | effects |

Record layout:

```
u8   nameLen             // includes the terminating NUL
char name[nameLen]       // case as in ShaderAssignList, e.g. "pes3ddf_shirt_nb_nc"
u32  vsSize; u8 vs[vsSize] ^ 0x9C   -> "DXBC" vs_5_0
u32  psSize; u8 ps[psSize] ^ 0x9C   -> "DXBC" ps_5_0
```

- Each blob has a standard RDEF chunk, so texture slot names, cbuffer names and ISGN/OSGN semantics are recoverable (`fsop.py rdef_bindings`).
- Disassembler: system `vkd3d-compiler` 1.17 (`-x dxbc-tpf -b d3d-asm`).
- Two more `cs_5_0` blobs are embedded in the exe image at 0x142a34b80 and 0x142a363c0. They are not character-related.
- **Swizzle-reading caveat** (an earlier misread of mine, corrected): in vkd3d syntax `sample rD.zw, uv, tN.zwxy` writes `rD.z = tex.swz[2] = x` and `rD.w = tex.swz[3] = y`. The destination lane *i* takes resource lane `swz[i]`.

### 1.2 Technique → shader routing (OBS: `ShaderAssignList` in the lua)

Format: `{ technique="X", type="Deferred|Forward|ShadowMap|Deferred_Decal", variation="", shader="fsopName" }`.

- **OBS:** `*_LNM` techniques map to the non-`_ln` shaders in the `*NoLnm_dx11.lua` tables, e.g. `pes3DDF_Boots_LNM → pes3ddf_boots`.
- The `*_ln` names in the plain `*.lua` are absent from the fsops (621 assignments unresolved).
- INF (high): the NoLnm tables are the live ones.

Each lua `ParameterPackingList` entry gives:
- `params`: the cbuffer order of material vectors, each a float4 in source order `p[k] = floats[4k..4k+3]`;
- `packing`: per destination float of `cPSMaterial` (`cb4`), which source float fills it;
- `textures`: the slot order.

Character techniques and their deferred shader:

| Technique (fmdl) | Deferred shader | Other passes |
|---|---|---|
| `pes3DDF_Shirt_NB_NC` / `_Collar_NC` / `_Sleeve_NC` / `_Pants_NB_NC` | `pes3ddf_{shirt_nb,collar,sleeve,pants_nb}_nc` | ShadowMap `ModelForward_ShadowOpaque` |
| `pes3DDF_Shirt_NB` / `Collar` / `Sleeve` / `Pants_NB` (cloth sim parts) | same names without `_nc` | — |
| `pes3DDF_Uniform` (socks, `uni_socks`) | `pes3ddf_uniform` | — |
| `pes3DDF_SleeveInner_NrmUV_NC` | `pes3ddf_sleeveInner_nc` | — |
| `pes3DDF_Skin`, `pes3DDF_Skin_Occ` (thighs, neck) | `pes3ddf_skin`, `pes3ddf_skin_occ` (byte-identical PS bodies) | — |
| `pes3DDF_Skin_Face` | `pes3ddf_skin_face` | — |
| `fox3DDF_Blin` (arms/hands `arm_mat`, many 4cc parts) | `fox3ddf_blin` | Forward `fox3dfw_blin` |
| `pes3DDF_Hair2` / `_NrmUV` | `pes3ddf_hair2` / `_nrmuv` | Forward `pes3ddf_hair2_HairReconstruction`; Decal `pes3ddf_hair2_parts_decal`; Shadow `ModelForward_Shadow_Hair` |
| `pes3DDC_Hair2` (head shell) | Deferred_Decal `pes3ddf_hair2_decal` | — |
| `pes3DDF_Eye` | `pes3ddf_eye` | — |
| `pes3DFW_EyeOcclusion` | Forward `pes3dfw_eyeocclusion` | — |
| `pes3DDC_Adjust[_100]` (`collar_ao`) | Deferred_Decal `pes3ddc_adjust_111` / `_100` | — |
| `pes3DDC_Wet` (`eyeline_SG`) | Deferred_Decal `pes3ddc_wet` | — |
| `pes3DDF_Boots` / `_LNM` | `pes3ddf_boots` | — |
| `pes3DDF_Boots_GGX`, `fox3DDF_GGX` | `pes3ddf_boots_ggx`, `fox3ddf_ggx` | — |

### 1.3 Shader census of character fmdls

OBS: `material_census.json`. The sample covers 1,120 fmdls:
- all 72 members of `common_package.fpk`;
- all 316 loose `Asset/model/character/**.fmdl`;
- 400 random face fpks, 80 boots fpks and 40 glove fpks.

Total: 3,312 materials.

| Count | Technique | Typical material | fmdl texture roles |
|---|---|---|---|
| 511 | `pes3DDF_Hair2` | `fox_hair_mat`, `fox_head_shell_mat` | Base, Normal, Specular, CubeMap, Translucent, (Roughness) |
| 510 | `pes3DDF_Skin_Face` | `fox_skin_mat` | Base, Normal, Specular, Translucent, (Roughness) |
| 228 | `fox3DDC_Blin` | `skin_dirt` decal | Base, Normal, Specular |
| 206 | `pes3DFW_EyeOcclusion` | `fox_eyeOcclusion_mat` | Base |
| 205 | `pes3DDC_Wet` | `eyeline_SG` | CubeMap, Normal, Mask, SubNormal |
| 204 | `pes3DDC_Adjust_100` | `collar_ao` | Base, Normal, Specular |
| 201 | `fox3DDF_Blin` | `arm_mat`, boots/gloves | Base, Normal, Specular |
| 144 / 72 / 34 / 19 | `Collar_NC` / `Shirt_NB_NC` / `Sleeve_NC` / `Pants_NB_NC` | `uni_*` | only `Pattern_Tex_LIN` (+Badge/Number dummies). The kit maps are bound at runtime (§6) |
| 93 | `pes3DDC_Hair2` | head shell | CubeMap |
| 59 | `pes3DDF_Hair2_NrmUV` | `fox_hair_transfer_mat` | + `Base_Tex_2_SRGB` |
| 31 / 5 / 3 | `pes3DDF_Boots` / `_GGX` / `_LNM` | `basic_L_fx` | Base, Normal, Specular (+Metalness) |
| 16 | `pes3DDF_SleeveInner_NrmUV_NC` | `uni_sleeve_inner` | Base, Normal, Specular |
| 14 | `pes3DDF_Uniform` | `uni_socks` | Base/Pattern, Normal, Specular, (Detail*) |
| 13 / 5 | `pes3DDF_Skin_Occ` / `pes3DDF_Skin` | `thigh_mat`, `skin_head` | Normal, Specular, Translucent |
| 2 | `pes3DDF_Eye` | `eye_mat` | 9 roles (§3.10) |

Also present: 4cc/edit-only techniques (`fox3DDF_Cloth*`, `pes3DDF_Cloth_*`, constant/unlit `fox3DFW_Constant*`, `fox3DDF_GGX`, `fox3DFW_Glass_LNM`, `fox3DDF_Blin_Translucent` teeth/mouth, `fox3DDF_Blin_Fuzzblock` antiblur).

Two fmdls (face 72209 `hair_high`) have garbage material strings, i.e. string-index corruption in that 4cc file. INF (high): a mod-authoring defect, not a format variant.

**OBS: fmdl parameter names differ from the shader's packing names.**
- Example: nocloth uniform fmdls carry `BlendNormalYParam / BlendCoeffParam / BlendBoostParam / PatchAnisoRoughnessParam / PatternIndexParam`.
- The `pes3DDF_Shirt_NB_NC` packing has `BlendNormalXParam, Wet, RepetitionParam, Stain, FNumberScaleXParam, …`.
- Unmatched fmdl names never reach the shader. The engine supplies the packing names through `MaterialModifier2` (§6).

---

## 2. G-buffer and the lighting model (needed to read every role table)

### 2.1 G-buffer layout

OBS: decoded from the `DbgView_GBuf_*` debug shaders (`GrSystemShaders`) and from `SSLighting_Sun*`. Every material PS writes it identically.

| RT | Channel | Content |
|---|---|---|
| GB0 (o0) | rgb | albedo, **linear**. `DbgView_GBuf_Albedo` applies the sRGB OETF to display it. |
| GB0 | a | 1 |
| GB1 (o1) | xy, w | octahedral normal (world, z negated in lighting), 11 bits per axis: 10 bits in x/y as `/1023`, LSBs packed in w as `/3` |
| GB1 | z | 10-bit field: **low 4 bits = shading-model ID; high 6 bits = translucency T (`/63`)** |
| GB2 (o2) | x | **roughness** (`DbgView_GBuf_Roughness`) |
| GB2 | y | **specular intensity** (`…_Specular`) |
| GB2 | z | metalness. Read only for IDs 1 and 3 (`…_Metalness`). |
| GB2 | w | **material-preset index ÷255** (`…_MaterialIndex`). Read for IDs other than 1/3. |
| GB3 (o3) | xy | screen-space velocity |
| GB4 (o4) | — | constant (0,0,0,1) |

Shading-model IDs seen:

| ID | Used by | Encoded as |
|---|---|---|
| 0 | every character shader except GGX | uniform cloth writes `o1.z = 96/1023` → T = 6/63 ≈ 0.095. Skin/hair/eye write T = round(TRM.G·63). Blin/boots write T = 0. |
| 1 | `fox3ddf_ggx`, `pes3ddf_boots_ggx` | `1/1023` |
| 3 | none among characters | lighting gives it a fixed spec tint (0.9, 1.0, 0.8) and wrap 1 |

### 2.2 Material-preset table `Fox/Asset/#Win/material_params.fmtt`

OBS: 8,192 B = 512 float4 = 256 presets × 2 texels, no header. Preset *k* = texels 2k (**A**) and 2k+1 (**B**).

The lighting shaders sample `g_MaterialTexture` at `(u = GB2.w, v = 0.75)` and take `.x` as the Fresnel-LUT F0 coordinate. They sample at `v = 0.25` and take `.rgb` as the specular tint and `.w` as the wrap weight.

Killed hypothesis: file = two 256-texel rows (row 0 = texels 0–255).
- Killed because under it the uniform presets 147–151 would get specular tint (0,0,0), i.e. no specular at all.
- Under the pair layout, preset 1 reads A.x = 0.886 with tint B = (0.80, 0.53, 0.16) (gold metal), dielectric presets read A.x ≈ 0.04, and B = (1,1,1).

INF (high): A ↔ v=0.75 and B ↔ v=0.25. The engine builds a 256×2 texture from the pairs; the upload code itself was not seen.

Presets used by characters (this install's file; read it at runtime, do not transcribe):

| Index | Used by | A.x (F0) | B.rgb (spec tint) | B.w (wrap) |
|---|---|---|---|---|
| 0, 250 | default (boots, blin with 0) | ≈0.039 | 1 | 0 |
| 10 | skin (face, thigh, neck, **arms/hands** `arm_mat`) | ≈0.035 | 1 | 0 |
| 11 | eye | ≈0.020 | 1 | 0 |
| 64 | hair | ≈0.035 | 1 | 0 |
| 147 | socks (live) | ≈0.028 | 1 | 0 |
| 148–151 | pants/shirt/collar/sleeve (live, per kit) | 0.035–0.051 | 1 | **0.698** |
| 39 / 40 | fmdl defaults of nocloth uniform parts | — | — | overridden at runtime |
| 1, 2 | GGX metals | 0.89 / 0.95 | (gold) / 1 | 0 |

- A.y / A.z / A.w are not read by any character-relevant lighting shader checked (`SSLighting_Sun*`, `SSLighting_EnvmapLight`, `ReflectionProbe`).
- They are read (A.x and A.z) by some `fox3ddf_blin_2mt_*_vr` stadium shaders. Meaning not determined.

### 2.3 Directional light (`SSLighting_Sun_NoShadow`, OBS)

Inputs:
- N from GB1;
- V = −normalize(viewPos);
- L = light dir (`cPSLight[0].xyz`), colour·intensity = `cb3[1].xyz · cb2[23].z`;
- angular radius `cb3[1].w`, used as a Karis representative-point sphere light: L' = normalize(L + sat(r/|R−L|)·(R−L)), with R = reflect(−V, N).

```
NdotL = dot(N,L)  (unclamped);  NdotV = sat(dot(N,V));  H = normalize(L'+V);  NdotH = sat(dot(N,H))
ID 1:    F0c = 0.04 + 0.96·GB2.z;  tint = 1;            wrapW = 0;  T = 0
ID 3:    F0c = 0.04 + 0.96·GB2.z;  tint = (0.9,1,0.8);  wrapW = 1;  T = GB1.T
else:    F0c = A[k].x;             tint = B[k].rgb;     wrapW = B[k].w;  T = GB1.T     (k = round(GB2.w·255))
diffuseLight  = lightColor · max_sat( NdotL + T·(1 − NdotL²),  wrapW·T )
r   = max(GB2.x, 0.02);  a = r²
D   = a² / (NdotH²(a²−1)+1)²                       // GGX, no 1/π
k   = a/2;  Vis = 1 / ((sat(NdotL)(1−k)+k)·(NdotV(1−k)+k))   // Smith-Schlick
F   = LUT(u = dot(H,L), v = F0c)                   // g_lut1 sampled linear-clamp
specularLight = lightColor · F·D·Vis·sat(NdotL)·0.25 · GB2.y · tint     (clamped to 1e4)
```

- Neither term carries 1/π (INF: lights are pre-scaled by π), so the diffuse:specular ratio equals standard Lambert + Cook-Torrance.
- INF (medium): `g_lut1` is Schlick, `F0 + (1−F0)(1−LdotH)^5`.
  - The LUT texture was not found in the VFS or the image. Its coordinates (`u = H·L`, `v = F0`) are what a Schlick table needs.
  - The only Fresnel generator seen (`SH_FresnelMap`) is Schlick with F0 = 0.02, power 5.
- Composition (`DeferredRendering`): `out = GB0.rgb·diffuseLight + specularLight`, then height fog.
- **Metals (ID 1) keep their albedo in the diffuse term.** The GGX PS does not darken albedo by metalness. In PES, metalness only moves F0.

### 2.4 Skin subsurface (`SubSurfaceScattering`, OBS)

Separable screen-space blur of the diffuse light buffer:
- **Pixel selection:** the skin-mask buffer value ×255 ∈ {10, 110}, i.e. preset 10 (INF: the mask buffer is the GB2.w material index).
- **Kernel:** 13 taps at ±k/6 of a width scaled by `cb5[10].xy` and by `√(1.6e-5 / (|v.z|³·d²))`.
- **Per-RGB tap weights** (falloff: red widest):

  | Tap | R | G | B |
  |---|---|---|---|
  | 0 | 1 | 1 | 1 |
  | 1 | 0.987 | 0.965 | 0.943 |
  | 2 | 0.948 | 0.866 | 0.791 |
  | 3 | 0.887 | 0.723 | 0.591 |
  | 4 | 0.807 | 0.561 | 0.392 |
  | 5 | 0.716 | 0.406 | 0.232 |
  | 6 | 0.618 | 0.273 | 0.122 |

- **Depth falloff:** `exp(−Δz²·(313629, 515248, 655770))`.
- **Second (vertical) pass:** blends back the unblurred light with `w = sat(1 − (1/N·V + 2)·(0.15, 0.18, 0.20))`.

So skin, arms, hands and thighs (all preset 10) get SSS; hair (64) and cloth do not.

---

## 3. Per-shader role tables and formulas (Q2)

Conventions for all rows:
- *uv0*/*uv1* = fmdl UV0/UV1.
- `nrm(t)` = the §0.3 decode.
- `TBN`: `n_world = T·n.x + B·n.y + N·n.z`, with B = cross(N,T)·tangent.w (VS).
- `f()` = the §0.4 remap.
- `cb4` registers come from the lua packing.

Every PS first runs the **visibility test**: `discard if (alphaSrc − mesh.w·cb0[0].w) < 0 or (COLOR.w − mesh.x) < 0`.
- `mesh` = `g_tex_mesh` (t15), point-sampled at `(SV_Position.xy − 0.5)/8` (an 8×8 screen tile).
- `COLOR.w` = per-object fade constant (`cb5[8].w`).
- `alphaSrc` = 1 for shaders without alpha.

### 3.1 Shirt / collar / sleeve / pants: `pes3ddf_{shirt_nb,collar,sleeve,pants_nb}_nc`

OBS: full disassembly of `shirt_nb_nc`; the others differ only in the marking block and the param registers.

`_NC` = no cull:
- the vertex normal is flipped on back faces;
- GB2.y (specular) is forced to 0 on back faces;
- non-NC versions skip both.

| Slot | Role (fmdl/live) | UV | Use |
|---|---|---|---|
| t0 | `Base_Tex_SRGB` = **kit atlas** `uniform/texture/u####pN` | uv0 | albedo; `.a` = kit-coverage `a0` |
| t1 (Texture2DArray) | `NormalMap_Tex_NRM` = `common/sourceimages/shirts_normal` (6 slices) / `pants_normal` (7) | uv1 | cloth wrinkle normals, masks |
| t2 | `SpecularMap_Tex_LIN` = kit `u####pN_srm`, or the engine default texture `0xb8a0bf169f98` when the kit has none | uv0 | R spec, G roughness |
| t3 (Texture2DArray) | `Pattern_Tex_LIN` = `uni_pattern` (18 slices, 256², BC7) | uv1 × `Repetition.xy` | fabric weave |
| t4 | `Stain_Tex_LIN` = `uni_stain_g` | uv0 | stain colour |
| t5 | `StainMask_Tex_LIN` = `uni_stain_mask_g` (8192×1024 = 8 columns) | uv0 | stain masks |
| t6 | `Marking_Tex_LIN` (shirt/collar), `Badge_Tex_LIN` (sleeve), `Number_Tex_LIN` (pants) | uv0, affine | name/numbers/badges |

`cb4` map for shirt and collar (lua packing):

| Register | Lanes |
|---|---|
| r0 | MatParamIndex.x, BlendNormalX.x, BlendNormalX.y, Wet.x |
| r1 | Repetition.xyzw |
| r2 | Stain.xyzw |
| r3 | FNumberScaleX.xyzw |
| r4 | BNumberScaleX.xyz, BlendNormalX.z |
| r5 | NameScaleX.xyz, BlendNormalX.w |
| r6 | BadgeMatrix0.xyw, BlendNormalLeg.x |
| r7 | BadgeMatrix1.xyw, BlendNormalLeg.y |

- Sleeve: r3–r6 = BadgeMatrix0..3 (.xyw); BlendNormalX.zw sit in r3.w / r4.w.
- Pants: r0 = MPI, BlendNormalLeg.xy, Wet; r3 = NumberScaleX.xyzw; r4 = BlendNormalX.xyz.

**Marking atlas** (t6, OBS, shirt/collar):

| Part | Atlas region | Placement |
|---|---|---|
| name | u ∈ [0, 0.64) | `u'=(uv.x−0.5)·NameScale.x+0.5`, `v'=uv.y·NameScale.y+NameScale.z`; sampled at `(0.64u', v')` |
| back number | u ∈ [0.64, 0.88) | `u'=(uv.x−0.5)·BNumberScale.x+0.5` (centred like the name; `.z` is **not** a u offset), `v'=(160/255)·uv.y·BNumberScale.y+BNumberScale.z`, `.w` unused; sampled at `(0.64+0.24u', v')`. (Made precise from `pes3ddf_shirt_nb_nc.ps` lines 46–57 by the main session: the earlier wording read as the chest's plain affine form, which mis-placed the number in GodotFootball.) |
| front number | u ∈ [0.88, 1.0), v ∈ [0, 0.5) | `sat(uv·FNumberScale.xy + FNumberScale.zw)`; sampled at `(0.88+0.12x, 0.5y)` |
| badge | u ∈ [0.88, 0.96), v ∈ [0.5, 1) | `(sat(BadgeMatrix0.xyw·(uv,1)), sat(BadgeMatrix1.xyw·(uv,1)))`; sampled at `(0.88+0.08x, 0.5+0.5y)` |

- Each part is masked to its [0,1]² rectangle (and its LOD bias is −1).
- They combine premultiplied: back number × (1 − name.a), front number × (1 − badge.a).
- Result `M` (rgb premultiplied, `M.a`).
- Sleeve: two badges via BadgeMatrix0/1 → u ∈ [0, 0.5) and BadgeMatrix2/3 → u ∈ [0.5, 1).
- Pants: one number, `sat(uv0·NumberScale.xy + NumberScale.zw)` over the full atlas.
- Live kits had the default (0xb8a0bf169f98) bound to all three.

**Stain** (all uniform shaders, OBS):

```
for c in xyzw: d_c = (Stain_c/10) mod 1 · 10            // units digit
mX = StainMask( uv0.x/8 + trunc(Stain.x/10)/8, uv0.y )   // column chosen by tens digit of Stain.x
mY = StainMask( uv0.x/8 + trunc(Stain.y/10)/8, uv0.y )
s  = sat( sum( mX·dot(mX, d) + mY·dot(mY, d) ) )
```

- Live `Stain = 0.001` → s = 0.
- The 0.99/0.5 offsets in the code round the digit (`ceil(d−0.99)`, `ceil(d−0.5)`).

**Albedo:**

```
a   = max(M.a, base.a)
alb = lerp(base.rgb, M.rgb, M.a)
alb = lerp(alb, Stain.rgb·|alb|, s)              // |alb| = length(rgb)
alb *= 1 − 0.1·Wet
p0  = Pattern[slice 2·Repetition.z](uv1·Repetition.xy)    // .x,.y,.w used
GB0 = alb · lerp(p0.x, 0.85, a)
```

**Normal:**

```
nA = nrm(NormalArray[BlendNormalX.y](uv1))                 // + Repetition.w added to uv1.x for the collar
nB = nrm(NormalArray[(BlendNormalX.y+1)·ceil(BlendNormalX.x−0.5)](uv1))
n  = normalize(lerp(nB, nA, 1 − 2|BlendNormalX.x − 0.5|))
s1 = NormalArray[1](uv1);  n1 = (s1.w·2−1, s1.y·2−1, z)    // slice-1 normal stored in .w/.y
n  = normalize(lerp(n, n1, dot(BlendNormalX.zw, s1.xz)))
s3 = NormalArray[3](uv1);  n  = normalize(lerp(n, n1, dot(BlendNormalLeg.xy, s3.xz)))
np = ((p0.x·p0.w)·2−1, p0.y·2−1, z)                         // weave normal
nd = normalize(lerp(np, n, a))
N  = TBN( normalize( (nd.xy + n.xy, n.z) ) )
```

With an opaque kit (a=1) this is `(2·n.xy, n.z)`, i.e. the wrinkle normal at double strength.

Live values: shirts `BlendNormalX = (0 or 0.112, 2, 0.038, 0.043)`, `BlendNormalLeg = (0.058, 0.017)` or `(0, 0.046)`.

**SRM:**

```
srm' = lerp(srm.rg, (0.27, 0.629), M.a)                    // markings: spec 0.27, rough 0.629
p1   = Pattern[2·Repetition.z + 1](uv1·Repetition.xy)
wf   = 1 − 0.15·Wet
GB2.x (rough) = lerp(p1.y·wf, srm'.g, a)                    // no f() remap
GB2.y (spec)  = lerp(p1.x, srm'.r, a·wf) · frontFace
GB2.z         = lerp(max(p1.w, Wet), 1, a·wf)               // unused by ID-0 lighting
GB2.w         = MatParamIndex (live 148..151 /255)
GB1.z         = ID 0, T = 6/63
```

### 3.2 Socks: `pes3ddf_uniform` (`pes3DDF_Uniform`, OBS)

`cb4` registers: r0 = (MPI, –, –, Wet); r1 = Repetition; r2 = Stain.

| Slot | Role | UV | Use |
|---|---|---|---|
| t0 | Base = kit atlas | uv0 | albedo; `.a` = a |
| t1 | Normal (`socks_nrm`, DXT5) | **uv1** | main normal |
| t2 | SRM (kit `_srm` / default) | uv0 | R spec, G rough |
| t3 (array) | Pattern | **uv0 × Repetition.x** (both axes), slices 2·Rep.z, 2·Rep.z+1 | weave |
| t4 / t5 | Stain / StainMask | uv0 | as §3.1 |

```
alb = lerp(base, Stain.rgb·|base|, s) · (1 − 0.1·Wet)
GB0 = alb · max(base.a, p0.x)                                // different from the shirt family
nd  = lerp(np(p0), nrm(t1), base.a);  N = TBN(normalize((nd.xy + n.xy, n.z)))   with n = nrm(t1)
GB2.x = lerp(p1.y, srm.g, base.a);  GB2.y = lerp(p1.x, srm.r, base.a);  GB2.z = lerp(p1.w, 1, base.a)
GB1.z = ID 0, T = 6/63
```

Live: `Repetition = (50, 0, 8, 0)` → slices 16/17; `MatParamIndex = 147/255`.

### 3.3 Inner sleeve: `pes3ddf_sleeveInner_nc` (OBS)

- `GB0 = Base(uv0)·(1−0.1·Wet)`.
- Normal **and SRM on uv1** ("NrmUV").
- `GB2.x = srm.g` (no remap); `GB2.y = srm.r·frontFace`.
- ID 0, T = 0.

### 3.4 Skin limbs: `pes3ddf_skin_occ` = `pes3ddf_skin` (thighs, neck; OBS)

`cb4` registers: r0 = (MPI, Occlusion_Mask.x [unused by PS]); r1 = (Incidence_Roughness.x, Dirty_Mask.xyz); r2 = Incidence_Color.

| Slot | Role | UV | Use |
|---|---|---|---|
| t0 | Base (`thigh_1_bsm`, or face-folder `arm_bsm` override) | uv0 | albedo |
| t1 | Normal | uv0 | `nrm` |
| t2 | SRM | uv0 | R spec, G rough |
| t3 | Translucent (TRM) | uv0 | **G** → T |
| t4 | `Dirty_Tex_LIN` = `skin_dtm` (4×4 DXT1) / `head_dtm` | uv0 | dirt/sweat mask |

```
dirt  = sat(dot(Dirty_Mask.xyz, dtm.rgb))                    // live Dirty_Mask = 0 → 0
g     = srm.g
dk    = clamp(0.93 − 0.008·(255g − 205), 0.73, 0.93)
alb   = lerp(base, base·dk, dirt)
rim   = pow(sat(1 − dot(N, V)), Incidence_Roughness.x) · srm.r · Incidence_Color.w
GB0   = lerp(alb, Incidence_Color.rgb, rim)                  // live: (0.725, 0.61, 0.555, 0.7), exponent 6.5
gd    = lerp(g, g·min(sat((255g − 170)·0.011), 0.66), dirt)
GB2.x = f( max(gd, min(g, 143/255)) );  GB2.y = srm.r;  GB2.z = 1;  GB2.w = 10/255
GB1.z = ID 0, T = round(TRM.g·63)/63
```

### 3.5 Face: `pes3ddf_skin_face` (OBS)

`cb4` registers: r0 = (MPI, Wrinkle3.x); r1 = (Incidence_Roughness.x, Dirty_Mask.xyz); r2 = Incidence_Color; r3 = Wrinkle1; r4 = Wrinkle2; r5 = Rotations.xyz.

Slots: t0 Base, t1 Normal, t2 SRM, t3 TRM, t4 Dirty, t5 `NormalMap_Animation_Tex_NRM`, t6 `Wrinkle_Mask_LIN`, t7 `Rotation_Mask_LIN`. All on uv0.

Albedo, rim, dirt, T and GB2 are as in §3.4, with three differences:
- the albedo mip is clamped `lod ≤ 5`;
- `GB2.x = f(gd)`, without the 143/255 floor;
- specular = `srm.r`.

Wrinkles:

```
w  = sat( dot(Wrinkle1, mask(uv0.x, uv0.y/2)) + dot((uv0.x<0.5 ? Wrinkle3.x : Wrinkle2.x, Wrinkle2.yzw), mask(uv0.x, uv0.y/2+0.5)) )
n  = nrm(t1) + w·(nrm(t5) − (0,0,1))
rm = RotMask(uv0)
n.y += −0.01·rm.r·Rotations.x − 0.0375·rm.b·min(Rotations.y−10, 0) − 0.0375·rm.g·min(Rotations.z−10, 0)
```

Live: `Wrinkle1 = 0.125, Wrinkle2 = −0.0067, Wrinkle3 = −0.016`, `Rotations = (−5.71, 11.8, 11.1)`.

### 3.6 Arms / hands / generic: `fox3ddf_blin` (`arm_mat`, OBS)

- `GB0 = Base(uv0).rgb`, unmodified.
- Alpha test uses `base.a` (§3 prologue).
- Normal `nrm(uv0)`.
- **Specular AA:** `σ² = 2·min(½(|ddx n|+|ddy n|), 0.30001)²` (max over two projected axes); `GB2.x = f(√(srm.g² + σ²))`.
- `GB2.y = srm.r`; `GB2.z = 1`; `GB2.w = MatParamIndex`. arm.fmdl uses **10 → skin preset → SSS applies**.
- ID 0, T = 0.
- OBS (live): the engine also sets `Dirty_Tex_LIN = skin_dtm`, `Incidence_Roughness = 6.5`, `Incidence_Color` and `Dirty_Mask` on `arm_mat`. `fox3ddf_blin` has no slots for these, so INF (high) they are ignored: arms get **no rim tint and no translucency**, unlike thighs.
- fmdl textures: Base `dummy` (replaced live by `common/sourceimages/arm_1_bsm` or the face folder's `arm_bsm`), Normal `arm_nrm` (BC5), SRM `arm_srm`.

### 3.7 Hair: `pes3ddf_hair2` (`fox_hair_mat`, `fox_head_shell_mat`; OBS)

`cb4` registers:

| Register | Lanes |
|---|---|
| r0 | MPI, SpecularCubeScale.x, AlbedoCubeScale.x, CubeAttenuation.x |
| r1 | Incidence_Color |
| r2 | HeadPosition.xyz, Incidence_Roughness.x |
| r3 | HairColor |
| r4 | HairColor_Screen.xyz |
| r5 | HairColor_Multiply.xyz |
| r6, r7 | Velocity, PreviousVelocity (live: set per frame) |

| Slot | Role | UV | Use |
|---|---|---|---|
| t0 | Base | uv0 (lod ≤ 5) | colour + alpha |
| t1 | Normal | uv0 | `nrm` |
| t2 | `g_tex_srm_hair` (`SpecularMap_Tex_LIN`) | uv0 | R spec/rim, G rough, B → GB2.z (unused) |
| t3 | TRM | uv0 | G → T |
| t4 (cube) | `CubeMap_Tex_LIN` | world reflection | highlight cube, `.r` |

```
if HairColor.w == 0:                                          // recolour (edit hair)
   c = screen(base.rgb, HairColor_Screen) = b + s − b·s
   c = (c < 0.5) ? 2·c·HairColor.rgb : 1 − 2(1−c)(1−HairColor.rgb)   // overlay, per channel
   base.rgb = c · HairColor_Multiply
α'    = max(base.a·157/255, base.a ≥ 250/255 ? 1 : 0)
discard if α' < 0.7·cb0[0].w·mesh.w  or  fade test
cube  = Cube(R).r · srm.r · (frontFace ? 1 : exp(−∞·CubeAttenuation))
GB2.y = cube · AlbedoCubeScale · srm.r · SpecularCubeScale     // specular = cube-driven, not srm.r directly
GB2.x = f(srm.g);  GB2.z = sat(1 − srm.b);  GB2.w = 64/255
lumaBoost = YUV→RGB of (1.5·Y, U, V)                           // brighter, same chroma
GB0   = lerp(base, lumaBoost, (1 − N·V)^4 · srm.r) + cube·AlbedoCubeScale
GB1.z = ID 0, T = round(TRM.g·63)/63
```

Alpha handling:
- The deferred pass is **dithered alpha** (stochastic, resolved by TAA). `pes3ddf_hair2_parts_decal` re-writes the G-buffer with blend weight α'.
- Forward `pes3ddf_hair2_HairReconstruction` (for α' > 0) blurs the lit scene with a 5-tap kernel (0.1, 0.4, 0.9, 0.4, 0.1). Its output alpha is `α'^0.75`, or 1 where the underlying material index is 64 (hair on hair: `t9.w ∈ (0.247, 0.255)`).
- INF (high): in Godot, `ALPHA_HASH` (or scissor at ~0.6 plus a depth-prepass blended pass) on `α'` gets closest.

### 3.8 Head shell: `pes3ddf_hair2_decal` (`pes3DDC_Hair2`)

- Same maths as §3.7, drawn as a deferred decal.
- Blend weight = base.a.

### 3.9 Eye: `pes3ddf_eye` (`eye_mat`, `common/eye_new.fmdl`; OBS)

`cb4` registers: r0.x = MPI (11); r1.x = HeightScale; r2.x = AlbedoCubeScale.

| Slot | Role (fmdl file) | Use |
|---|---|---|
| t0 | Base `cm_eyes0_def_bsm` | sclera albedo; `.a` alpha test |
| t1 | Normal `cm_eyes0_v00_def_nrm` | G-buffer normal |
| t2 | SRM `cm_eyes0_v00_def_srm` | R spec, G → `f()` rough |
| t3 | Translucent `cm_eye1_lens_trm_alp` | G → T; A → cube mask |
| t4 | `NormalMap2_Tex_NRM` `eye_catch_nrm` | normal used only for the reflection |
| t5 | `LensHeight_Tex_LIN` `cm_eye1_lens_hgm` | G = height h |
| t6 | `Base_Tex2_SRGB` iris colour (`eye_color_sub_*`; live: `eye_color_sub_01_brown`) | parallax iris |
| t7 (cube) | `CubeMap_Tex_LIN` `eye_catch_cbm` (live `eye_catch_cbm_d`) | catch-light |

`ViewReflection_Tex_LIN` is in the fmdl but is not a shader slot.

```
vT  = normalize(TEXCOORD2 · (1,−1,1))                  // tangent-space view
uvI = uv0 − vT.xy·h·HeightScale·0.25 / vT.z
alb = lerp(base.rgb, Iris(uvI).rgb, 1 − (1−h)^4)
GB0 = alb + Cube(reflect(−V, TBN(nrm(t4)))).rgb · AlbedoCubeScale · trm.a
N   = TBN(nrm(t1));  GB2 = (f(srm.g), srm.r, 1, 11/255);  ID 0, T = trm.g
```

All on uv0. The eye VS passes only TEXCOORD0, although the mesh also has UV1.

### 3.10 Eye occlusion: `pes3dfw_eyeocclusion` (forward, premultiplied; OBS)

```
α   = (Base.a + min(1 − SSAO, 0.15)) · sat(1 + 0.2·(lerp(Rotations.y, Rotations.x, COLOR.r) − 1))² · fade
out = (Base.rgb·sceneScale·fogTransmit + fogInscatter) · α,  alpha = α
```

- **The only character shader using vertex colour** (COLOR.r = eyelid blend).
- Live: `Rotations = (0, 33.8, 33.1)`.

### 3.11 Collar AO: `pes3ddc_adjust_100` (`collar_ao` decal on the neck; OBS)

- Writes **GB0 only**: `rgb = Base.rgb`, `alpha = Base.a·AlbedoBlendRate`.
- Zeros into GB1/GB2/GB4, which the decal blend state masks out (INF).
- Discards when fade < 0.9.
- Live: `Base_Tex_SRGB = collar_105_alp`. INF (high): it darkens the neck under the collar.

### 3.12 Boots: `pes3ddf_boots` / `pes3ddf_boots_ggx` (OBS)

`cb4` registers: r0.x = MPI; r1 = Stain.

Slots: t0 Base, t1 Normal, t2 SRM, t3 Stain (`boots_stain_g`; `_ggx`: t3 = `MetalnessMap_Tex_LIN`, t4 = Stain). All on uv0.

```
sa  = StainTex(uv0).a;  st = sa · (1 − sat(4·((1.25 − sa) − Stain.x)))   // param Stain.x ramps it in; live Stain.x = 0 → st = 0 for every sa ≤ 1
GB0 = lerp(base.rgb, base.rgb·StainTex.rgb, st)
GB2.x = srm.g                     // no remap;  _ggx: max(srm.g, 0.004)
GB2.y = srm.r·(1 − st)
GB2.z = 1                         // _ggx: Metalness.r·(1 − st)
GB2.w = MPI (fmdl 0 → default preset)
GB1.z = ID 0  (_ggx: ID 1)
```

Alpha test on `base.a` (§3 prologue).

---

## 4. Vertex data (Q3)

**OBS: vertex formats** (fmdl vertex-format usage IDs). Data types: 1 = float3, 6 = half4, 7 = half2, 8 = unorm8×4, 9 = u8×4.

| Material group | Attributes |
|---|---|
| uniform cloth (shirt/collar/sleeve/pants/socks/inner) | POS, NRM, TAN, [COL], BW0, BI0, **UV0, UV1** |
| skin (face, limbs), hair, eye-occ, wet, adjust | POS, NRM, TAN, [COL], BW0, BI0, **UV0** only |
| hair NrmUV, eye | UV0 + UV1 |
| boots | UV0 + UV1 (some unskinned: no BW/BI) |

**OBS: UV per texture** (from the VS routing `o2.xy = TEXCOORD0`, `o2.zw = TEXCOORD1`, and the PS sample coordinates):

| Shader | UV0 | UV1 |
|---|---|---|
| shirt / collar / sleeve / pants | Base (kit), SRM, Stain, StainMask, Marking/Badge/Number | NormalMap array, Pattern array (× Repetition.xy) |
| socks (`uniform`) | Base, SRM, Stain, StainMask, Pattern (× Repetition.x) | NormalMap |
| sleeve inner | Base | NormalMap, SRM |
| skin / face / blin / hair / eye / boots | every texture | not used (the eye VS drops UV1) |

**OBS: vertex colour.**
- Every deferred character VS passes COLOR.rgb through (or writes 1), and **overwrites COLOR.w with `cb5[8].w`** (object fade).
- No deferred character PS reads COLOR.rgb.
- The PS's COLOR.w is compared against `g_tex_mesh.x` for dithered fade-out.
- Only `pes3dfw_eyeocclusion` reads COLOR.r.
- For Godot: ignore vertex colour except on eye occlusion.

**OBS: tangent frame** (VS): `B = normalize(cross(N, T))·T.w`. The PS builds `n = T·x + B·y + N·z`. INF (medium): this equals Godot's convention when the fmdl tangent.w is passed straight through. Confirm green-channel orientation on one frame.

---

## 5. Texture facts

OBS: ftex headers of `common/sourceimages/#windx11/*`.

- **Array ftex:** `textureType` at **+0x1C** has bit 0x10 for 2D arrays, and the u16 at **+0x22** is the slice count. (Corrected per pes-re-ftex.md §1: this line first gave +0x18 / +0x1A. The u32 at +0x18 is 0 in every file.)
  - `uni_pattern` 18 slices 256², pf 11 (BC7).
  - `shirts_normal` 6 slices 1024×2048, BC7.
  - `pants_normal` 7 slices 1024², BC7.
  - Bit 0x8 marks normal maps (`arm_nrm` 0x9, pf 9 = BC5; `socks_nrm` 0x9, pf 4 = DXT5).
  - Bit 0x4 = cube (`eye_catch_cbm` type 0x7).
  - The current GF/GodotFootball ftex reader treats every non-cube ftex as one image, so **array slices are lost**. Godot needs `Texture2DArray` for Pattern/NormalMap arrays.
- `uni_stain_mask_g` 8192×1024 (8 columns of 1024²); `skin_dtm` 4×4 DXT1; `dummy_srm` 16×16.
- **OBS: the engine default bound when a kit lacks `_srm`, or for Marking/Badge/Number,** has path code `0xb8a0bf169f98`.
  - It was not resolved against any VFS path stem (tried: every ftex/dds/tga/png path in 4 spellings, plus `dummy*`).
  - INF (medium): a built-in fallback texture. Its content is unknown, so kit specular on those kits is unknown.

---

## 6. Live MaterialModifier2 vectors (Q4)

**OBS: object layout** (`fox::gr::MaterialModifier2`, vtable 0x142a19150):

| Offset | Field |
|---|---|
| +0x10 | StrCode32(material name) |
| +0x20 | u32 paramCount |
| +0x28 | → param table of 8-byte entries `{u32 StrCode32(name), u8 slot, u8 0, u8 0, u8 kind}` |
| +0x30 | u32 vectorCount |
| +0x38 | → float4[vectorCount] |
| +0x40 | u32 textureCount |
| +0x48 | → `{u16 1, u16 textureIndex}` |

- `kind & 0x20` = vector (slot indexes +0x38); `kind & 0x40` = texture (slot indexes +0x48).
- Each material has two MM2 instances, and exactly one of them has bit **0x80** set on all its params. INF (medium): a per-instance "pending upload/active" flag.
- This corrects the earlier report: 0xA0/0xC0 are the 0x80-flagged values of kinds 0x20/0x40.
- An MM2 holds only the params the engine overrides. Everything else comes from the fmdl.

**OBS: StrCode32 names resolved** against the 3,188 quoted strings of the lua tables plus the census names:
- `0x16f09b71` = `Marking_Tex_LIN`;
- `0x7bcbb6cb` = `Dirty_Tex_LIN` (the previous report's unresolved "skin_dtm" slot).

Every param hash in the 73 live MM2s resolved.

Values (two kits; full dump in `mm2v.txt`):

| Material | Param | Value(s) |
|---|---|---|
| `uni_shirts`, `uni_collar` | `MatParamIndex_0` | 151/255 (kit 752), 150/255 (kit 754) |
| | `RepetitionParam` | (25, 50, 1 \| 0, 0) |
| | `BlendNormalXParam` | (0, 2, 0.0376, 0.0434) / (0.1115, 2, 0, 0) |
| | `BlendNormalLegParam` (shirt only) | (0.0579, 0.0166) / (0, 0.0461) |
| | `FNumberScaleXParam` | (26.67, 26.67, −10.17, −19.64 \| −19.86) |
| | `BNumberScaleXParam` | (−5.5, −11, 2.667, 2.667) / (−4.857, −9.714, 2.355, 2.355) |
| | `NameScaleXParam` | 0 |
| | `BadgeMatrix0/1` | (1,0,0,0) / (0,1,0,0) |
| | `Stain` | 0.001×4 |
| | `Wet`, `Dirty_Mask` | 0 |
| | textures | Base = kit atlas, Normal = `shirts_normal`, Spec = kit `_srm` or default, Stain = `uni_stain_g`, StainMask = `uni_stain_mask_g`, Marking = default |
| `uni_sleeve` | `MatParamIndex_0` | 151 / 150 |
| | `BadgeMatrix0..3` | identity rows (1,0,0,0) / (0,1,0,0) |
| | others | as the shirt; `Badge_Tex_LIN` = default |
| `uni_pants` (+`.001`) | `MatParamIndex_0` | 149/255 / 148/255 |
| | `RepetitionParam` | (35, 0, 7, 0) |
| | `NumberScaleXParam` | (−14.93, −14.93, 14.35, 12.57) / (−15.56, −15.56, 15.03, 13.37) |
| | `SelfColor` (not in the pants packing; ignored by the PS) | (1, 0.6, 0, 1) / (0.157, 0.157, 0.157, 1) |
| | `Number_Tex_LIN` | default |
| `uni_socks` | `MatParamIndex_0` | 147/255 |
| | `RepetitionParam` | (50, 0, 8, 0) |
| | `NumberScaleXParam` | 0 |
| | textures | Base = kit atlas, Spec = kit `_srm`/default |
| `arm_mat`, `thigh_mat` | `Incidence_Roughness` | 6.5×4 |
| | `Incidence_Color` | (0.725, 0.61, 0.555, 0.7) |
| | `Dirty_Mask` | 0 |
| | textures | `Dirty_Tex_LIN` = `skin_dtm`; Base = `arm_1_bsm` / `thigh_1_bsm`, or face-folder `arm_bsm` |
| `fox_skin_mat` (face) | `Wrinkle1/2/3` | 0.1245 / −0.0067 / −0.0162 |
| | `Rotations` | (−5.714, 11.835, 11.114) |
| | textures | `Wrinkle_Mask_LIN` = `head_normal_mask`, `Rotation_Mask_LIN` = `head_rotation_mask`, `NormalMap_Animation_Tex_NRM` = `head_normal_animation`, `Dirty_Tex_LIN` = `head_dtm` |
| `fox_hair_mat`, `fox_head_shell_mat` | `HeadPosition` | (−0.012, 1.672, 0.063, 1) |
| | `Velocity` / `PreviousVelocity` | per-frame motion |
| `eye_mat` | textures | `Base_Tex2_SRGB` = `eye_color_sub_01_brown`, `CubeMap_Tex_LIN` = `eye_catch_cbm_d` |
| `fox_eyeOcclusion_mat` | `Rotations` | (0, 33.835, 33.114) |
| `collar_ao` | textures | Base = `collar_105_alp` |
| `skin_dirt` | `Dirty_Mask` | 0 |
| | `Wet` | 0 |
| | textures | Base/Spec = `limb_stain_g` / `limb_stain_g_srm` |
| `basic_L_fx` (boots) | `Stain` | 0 |
| | textures | `Stain_Tex_LIN` = `boots_stain_g` |

**Consequences (INF high):**
- The fmdl `MatParamIndex_0` of nocloth uniform parts (40/39) is replaced at runtime by a per-kit cloth preset (147–151). Those presets differ in F0 and A.w, and all have wrap 0.698.
- The engine computes number/name placement (`*ScaleXParam`) from the kit config. The vectors are affine UV transforms into the marking atlas (§3.1).
- Specular for hair comes from the highlight cube, not from SRM alone.

---

## 7. Godot recipe

Prefer a `ShaderMaterial` with a custom `light()` to reach exact parity. `StandardMaterial3D` approximations are given where they hold.

- **Albedo:** `Base_Tex_SRGB` as sRGB albedo.
  - Uniform shirt/collar/sleeve/pants with an opaque kit: × 0.85 (pattern slice r when kit alpha < 1).
  - Socks: × max(base.a, pattern.r).
  - Skin: rim tint `lerp(alb, Incidence_Color.rgb, pow(1−N·V, Incidence_Roughness)·srm.r·Incidence_Color.a)` (needs a shader).
- **Normal:** pre-convert every `_NRM` to RG with `R ← R·A`, `G ← G`, and let Godot reconstruct z.
  - Uniforms: add the wrinkle-array slice normal plus the weave normal on UV1 as in §3.1. With the live params this is roughly slice 0 at double xy strength.
- **Roughness:** `SRM.g`. Through `f()` for skin, face, blin (arms), hair and eye; raw for uniform and boots. Godot's `roughness` is perceptual, matching PES's α = r².
- **Specular:** PES specular = `SRM.r · tint · F(F0_preset)`. With `StandardMaterial3D`: `specular = sqrt(SRM.r·F0_preset/0.16)` per pixel (INF: Godot dielectric F0 = 0.16·specular²). That loses the grazing cap at SRM.r; a custom `light()` reproduces it exactly with the §2.3 formulas.
- **Metallic:** 0 everywhere except the GGX shaders (`MetalnessMap.r`). Even there PES keeps the albedo diffuse, so Godot `metallic` darkens more than PES does.
- **Translucency/wrap** (in `light()`): `diffuse = max(sat(NdotL + T(1−NdotL²)), wrapW·T)`.
  - Cloth: T = 6/63, wrapW = 0.698.
  - Skin and hair: T = TRM.g, wrapW = 0.
- **SSS:** skin preset 10 (face, neck, thighs, arms, hands) → Godot `subsurface_scattering` with red-wide falloff (§2.4 weights). Hair and cloth: none.
- **Alpha:** hair → hashed/dithered on `α' = max(0.616·a, a ≥ 0.98)`; blin/eye/boots → test on base.a (threshold §8); cloth and skin are opaque.
- **Vertex colour:** ignore (except eye occlusion).

---

## 8. Killed hypotheses and open items

Killed:
- *Material table = two 256-texel rows.* Killed by the black-specular result for live uniform presets (§2.2).
- *SRM = specular/roughness/metal with R → roughness.* Killed by the debug views: GB2.x = roughness is fed from `srm.g` in every shader, and GB2.y = specular from `srm.r`.
- *Socks use a separate pattern texture for colour.* Killed: socks colour = kit atlas Base on UV0 (live), with the pattern only modulating.
- *0xA0/0xC0 are the param kinds.* Corrected: kinds are 0x20/0x40; 0x80 is a per-instance flag.
- *`_LNM` shaders exist.* Killed: absent from the fsops; NoLnm tables route them to the base shaders.

Open (what was tried):
1. **`g_tex_mesh` (t15) content and `cb0[0].w`** fix the exact alpha-test threshold and the dither pattern.
   - Tried: `Asset/effect/gr_pic/#windx11/gr_dither_nrt_32b.ftex`. It is an 8×8 Bayer in B only with A = 1, while the shaders read .x and .w, so it is probably not this binding.
   - Not found in the image strings.
   - Bounded: in the uniform shaders `1 − mesh.w·cb0[0].w ≥ 0` must hold for opaque cloth to draw, so the product is ≤ 1.
2. **`g_lut1` Fresnel LUT values.** Not in the VFS (`lut*` files are colour grading) and not generated by any fsop shader found. Schlick is INF (medium).
3. **Material table A.y/A.z/A.w meaning.** Unused by the character lighting paths checked.
4. **Ambient terms:** SH sky (`SSLighting_SH_*`), env/probe reflections and SSR were not decoded beyond which material-table lanes they read.
5. **Default texture `0xb8a0bf169f98`:** path and content unresolved (§5).
6. **Live technique of `arm_mat`:** whether the engine swaps `fox3DDF_Blin` for a skin technique to use the Dirty/Incidence params it sets. The fmdl says Blin, and no swap was observed. Checking needs the live Model → shader binding, which was not traced.
7. **FxShaders pack** not parsed (not character-related).
