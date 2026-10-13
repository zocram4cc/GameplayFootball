# PES 2021 pitch / turf / lines — RE report

Scope: the in-play pitch materials of `Asset/model/bg/stNNN/pitch/#Win/pitch_stNNN.fpk`:
`pitch_ed` (technique `pes3DDF_Pitch2020`), `line_alp_dds1` and `pitch_scratch`
(both technique `fox3DDC_Blin`), the turf maps they read, and the material-preset
index. Goal: a port-ready formula for `godot/shaders/pes_pitch.gdshader`.

**Status: priorities 1–4 closed and numerically verified. Priority 5 (the turf3d
grass mesh) is partially decoded and listed as open with what was tried.**

Tags:
- **OBS** = observed, with the method and the address/file that makes it readable.
- **INF** = inferred, with confidence.

All VAs are the live image (base 0x140000000, 64 MB `/tmp/pesmem.bin`).
The game was **not running** and was not launched; `/proc/<pid>/mem` was never
needed because the shaders are on disk in the install's `shaders/dx11/*.fsop`.

**Verification method (this is what backs every §3 formula).**
The DXBC blobs are free of any decompiler's variable names, so each PS was
**mechanically translated to executable Python** and run against the real
decoded textures:

| Artefact | What it is |
|---|---|
| `/tmp/ShaderMaterialRE/asm/pes3ddf_pitch_2020.ps.asm` | 187-line vkd3d `d3d-asm` disassembly of `pes3ddf_pitch_2020` |
| `/tmp/PitchGrassRE/sym/xlat.py` | IR translator (correct DXBC swizzle semantics) |
| `/tmp/PitchGrassRE/sym/run_ps.py` | executes the IR; emits GB0..GB4 |
| `/tmp/PitchGrassRE/sym/final.py` | independent hand-written port of §3.7–3.8 |
| `/tmp/PitchGrassRE/sym/cmp5.py` | cross-checks the two |
| `/tmp/PitchGrassRE/tex/*.png` | decoded st002 pitch textures (also on disk at `~/ghidra-projects/scratch/PitchGrassRE/tex/`) |

**Result: `checked 315 pixels, 0 mismatches`** (8 camera positions × 7×5 UV grid),
comparing GB0.rgb, GB2.x (roughness), GB2.y (specular) and GB2.w exactly. The
formulae in §3.7–3.9 are therefore not inferred but reproduced.

**Self-correction notes.** This report's first two drafts contained four wrong
readings, each found by the numeric run and corrected here:
1. The **DXBC swizzle rule** — a source swizzle describes the four destination
   lanes xyzw and the write-mask selects by absolute index. Getting this wrong
   inverted `r1.x/r1.y` at asm 98 and the slope channels at asm 174.
2. `MatParamIndex_0 = 200` was first assumed to drive the pitch's green sheen and
   wrap. It does not: `cb4[0].x` is read nowhere and the shader emits
   shading-model ID 3, so the green sheen comes from the ID-3 hard-coded tint.
3. The Withers curve's green/blue gains are `WithersParameter1/2` (0.78/0.96), not
   `AOParamY`/`RepairColor`.
4. `GB2.w` is a slope × wither × density scalar, **not** the material-preset
   index, for this shader.

---

## 0. Headline facts

1. **OBS: the pitch PS is `pes3ddf_pitch_2020`** — the Deferred variant named by
   the `ShaderAssignList` row
   `{ technique="pes3DDF_Pitch2020", type="Deferred", variation="",
   shader="pes3ddf_pitch_2020"}` (`PesShaders_dx11.lua:2817`). Its DXBC is the
   record `pes3ddf_pitch_2020` in `shaders/dx11/PesShaders_dx11.fsop`
   (OBS: `fsop.py` parses the pack to its exact end). Disassembly:
   `/tmp/ShaderMaterialRE/asm/pes3ddf_pitch_2020.ps.asm` (187 lines) and
   `.vs.asm` (141 lines), produced with system `vkd3d-compiler 1.17`.
   The same technique also has a ShadowMap row → `ModelForward_ShadowOpaque`
   (`PesShaders_dx11.lua:2821`), i.e. the pitch draws into the shadow map with a
   generic forward shader.
2. **OBS: the line layer and the scratch layer share one shader** —
   `fox3DDC_Blin` → **`fox3ddf_blin_decal`** (Deferred_Decal,
   `GrModelShaders_dx11.lua:8214`), disassembled at
   `asm/fox3ddf_blin_decal.ps.asm` (84 lines). `MatParamIndex_0` does **not**
   select a code branch in it: the shader declares only `cb0` and **no `cb4` at
   all**, so the value cannot reach it from the material. The line layer and
   `pitch_scratch` are therefore the *same* shader with *different textures*, and
   the difference between them lives entirely in the deferred-decal render state
   (INF, high — see §4).
3. **OBS: the pitch PS hard-codes nothing but the maths.** Every material number
   arrives through `cPSMaterial` (`cb4`) in the order given by the
   `ParameterPackingList` for `pes3DDF_Pitch2020`
   (`PesShaders_dx11.lua:26-33`), reconstructible mechanically.
4. **OBS (and a correction to this report's first drafts): `MatParamIndex_0 = 200`
   selects nothing at runtime.** The pitch PS reads `cb4[0].x` in **no
   instruction**, and it emits **shading-model ID 3**, so
   `SSLighting_Sun_NoShadow` takes the hard-coded ID-3 branch and never indexes
   `g_MaterialTexture`. Preset 200 does exist in
   `Fox/Asset/#Win/material_params.fmtt` (`A = (0.0353, 0.298, 0, 0)`,
   `B = (0.898, 1.0, 0.8, 0.9373)`) and is packed at `cb4[0].x` like every other
   material's preset — it is simply not consumed by this shader build. What
   *does* light the pitch is the ID-3 hard-coded `F0 = 0.04`, tint
   `(0.9, 1, 0.8)`, `wrapW = 1`, `T = 0`.
5. **OBS: the pitch is a flat deferred material whose albedo comes from
   `Base_Tex_2_SRGB`** (the shared `turf_bsm_alp_rgba32`, RGB mean
   `(65, 85, 12)` = dark olive-green) re-coloured through a three-pair Withers
   curve, with the **alpha of that map as the master density scalar** driving
   roughness and specular. The stadium's own `pitch_alp` contributes only its
   **red channel as the density multiplier** and its **blue channel as an on/off
   mask** (OBS: decoded 4096×2048 DXT5, B = 255 on 77.8 % of texels and 0 on
   19.4 %, i.e. a crisp border frame; R and G are noise).
6. **OBS: the mowing stripes are not in this material at all.** They live on the
   separate `turf3dtexlodu4.fmdl` grass mesh (technique `pes3DDF_Turf3D2020`),
   whose deferred-decal VS samples a `Grass_MaskMap_Tex_LIN` through an affine
   **world-space** UV. `turf_bsm`'s alpha is pure noise, so no stripe source
   exists in the pitch shader.
7. **OBS: `Line_alp_dds1` is a flat grey 0.82 with a mild normal and
   roughness/specular both ≈ 0.71** (decoded from the common `line_alp/nrm/srm`
   maps), alpha-blended by `line_alp.a`.

## 1. Material parameters and slots, exactly as packed

### 1.1 fmdl values (OBS)

`build/apps/pesfmt_dump fmdl 'Asset/model/bg/st002/pitch/#Win/pitch_st002.fpk'`:

```
pitch_ed [pes3DDF_Pitch2020 / pes_3ddf_pitch2020]
  tex Base_Tex_SRGB      = .../st002/sourceimages/tga/pitch_alp.tga
  tex NormalMap_Tex_NRM  = .../st002/sourceimages/tga/turf_nrm.tga
  tex SpecularMap_Tex_LIN= .../st002/sourceimages/tga/turf_srm_alp.tga
  tex Base_Tex_2_SRGB    = .../common/pitch/sourceimages/turf_bsm_alp_rgba32.tga
  tex GrainNormal_Tex_LIN= .../st002/sourceimages/tga/pitch_detail.tga
  tex Grain_Tex_LIN      = .../st002/sourceimages/tga/pitch_grain_alp.dds
  tex SlopeTable_Tex_LIN = .../common/pitch/sourceimages/grass_slopetable_alp.tga
  MatParamIndex_0=200 AOParamX=1 AOParamY=0.1 AOParamZ=1.5 AOParamW=0.85
  NormalFlatness=0.5 CameraVectorBlend=0
  WithersParameter0..5 = 0.78, 0.78, 0.96, 0.1, 0.07, 0.04
  SrmParameter0..5     = 1, 0.24, 0.2, 0, 0.76, 0.2
  URepeat_UV=24  VRepeat_UV=16
  RepairColor=0.31 0.4 0 1   EmissiveColor=0 0 0 1
  EmissiveParam0=0.05 EmissiveParam1=0.1 EmissiveParam2=17.09
```

The fmdl mesh is **unskinned** (`cb5[8].x == 0` takes the `else` branch of the VS,
so the skinning block is skipped; OBS `pesfmt_dump fmdl` reports 0 bones). It has
**UV0 only**.

---

## 2. Vertex stage (OBS, `asm/pes3ddf_pitch_2020.vs.asm`)

Inputs: `v0` pos, `v1` nrm, `v2` tang(+w), `v3` blend indices, `v4` blend weights,
`v5` uv0, `v6` uv1. Outputs:

- `o0` clip pos; `o1.xy` = uv0, `o1.zw` = uv1 (**only uv0 is sampled**).
- `o2` = `-normalize(viewDir)`, `o3` = normalize(tangent),
  `o4` = normalize(cross(cross(tangent,worldNormal)·tangent.w)) (B = `cross(N,T)·T.w`,
  same convention as the character VS), `o5` = normalize(worldNormal).
- `o6.xyzw` = current world pos, `o7.*` = prev-world pos.
- **`o7.z` / `o7.w`** are a **height-fog colour computed in the VS**, from
  `r5.y = max(viewDist·(-1.7) - log(12·dist/(-cb2[5].y)), 0)` clamped, then a
  4-term polynomial in `r5.y` (coefficients `(0.0693, 4.877, -0.00453, 0.000109)`
  and `(0.3929, 1.667, 0.001178, -0.000208)`), written as `±0.5`. The PS adds
  these back to `v7.zw` and into GB3 (asm 166).
  **INF (medium):** GB3 for the pitch is not a pure screen-space velocity but a
  fog-vector delta. For a Godot port this whole block can be **dropped** — Godot's
  own height fog replaces it.

---

## 3. `pes3ddf_pitch_2020` PS — the verified formula

Everything below was derived from the raw instructions in
`/tmp/ShaderMaterialRE/asm/pes3ddf_pitch_2020.ps.asm` (187 lines) and then
**verified numerically**: the shader was mechanically translated to Python
(`/tmp/PitchGrassRE/sym/run_ps.py` + `xlat.py`, correct DXBC swizzle semantics) and
executed against the real, decoded st002 textures, while an independent
hand-written port (`/tmp/PitchGrassRE/sym/final.py`) was run on the same inputs.
**315/315 pixels across 8 camera positions agree exactly**
(`python3 /tmp/PitchGrassRE/sym/cmp5.py` → `checked 315 pixels, 0 mismatches`).
The outputs compared are GB0 (albedo RGB), GB2.x (roughness), GB2.y (specular)
and GB2.w.

### 3.0 DXBC swizzle rule used (OBS, needed to read every line below)

A source operand's swizzle describes the **four destination lanes xyzw**; the
destination write-mask selects lanes by **absolute index**, so
`mad r0.yz, r1.xxyx, l(0,.5,.5,0), l(0,.5,.5,0)` gives
`r0.y = r1.x*.5+.5` (src lane 1 = 'x') and `r0.z = r1.y*.5+.5` (src lane 2 = 'y').
A shorter swizzle repeats its last char for the remaining lanes.
Getting this wrong inverts several lane assignments; the two corrected readings
are flagged inline below.

### 3.1 cb4 → material param (OBS, mechanical)

The lua `packing` array maps **byte offset → source vector**; each register row of
32 entries maps to `cb4[0..7]` by `index / 4`
(`/tmp/PitchGrassRE/PesShaders_dx11.lua:26-33`). So:

| cb4 row | params | values (st002) |
|---|---|---|
| `cb4[0]` | `MatParamIndex_0`, `EmissiveParam0/1/2` | 200, 0.05, 0.1, 17.09 |
| `cb4[1]` | `AOParamX/Y/Z/W` | 1, 0.1, 1.5, 0.85 |
| `cb4[2]` | `URepeat_UV`, `VRepeat_UV` | 24, 16 |
| `cb4[3]` | `NormalFlatness` | 0.5 |
| `cb4[4]` | `RepairColor.xyz`, `CameraVectorBlend` | 0.31, 0.4, 0, 0 |
| `cb4[5]` | `WithersParameter0..3` | 0.78, 0.78, 0.96, 0.1 |
| `cb4[6]` | `WithersParameter4/5`, `SrmParameter0/1` | 0.07, 0.04, 1, 0.24 |
| `cb4[7]` | `SrmParameter2..5` | 0.2, 0, 0.76, 0.2 |

**`cb4[0].x = MatParamIndex_0` is read by no instruction in this shader.** See §3.8.

### 3.2 Slot → resource, UV and filter (OBS)

`fsop.rdef_bindings` on `dxbc/pes3ddf_pitch_2020.ps.dxbc`:

| Slot | RDEF name | fmdl role | UV | Sampler |
|---|---|---|---|---|
| t0 | `g_tex_diffuse` | `Base_Tex_SRGB` (`pitch_alp`) | uv0 | `s14` aniso wrap HQ |
| t1 | `g_tex_normal` | `NormalMap_Tex_NRM` (`turf_nrm`) | uv0 | `s1` aniso wrap |
| t2 | — | `SpecularMap_Tex_LIN` (`turf_srm_alp`) | — | **not bound by this PS** |
| t3 | `g_tex_base` | `Base_Tex_2_SRGB` (`turf_bsm_alp_rgba32`) | uv0 × (24,16) | `s3` **point wrap** |
| t4 | `g_tex_turfNormal` | `GrainNormal_Tex_LIN` (`pitch_detail`) | uv0 × (24,16) | `s12` aniso wrap |
| t5 | `g_tex_turfSlopTable` | `SlopeTable_Tex_LIN` (`grass_slopetable_alp`) | derived (below) | `s11` linear **clamp** |
| t6 | `g_tex_mask` | `Mask_Tex_SRGB` | uv0 | `s6` point wrap |
| t7 | `g_tex_grain` | `Grain_Tex_LIN` (`pitch_grain_alp`) | uv0 | `s7` point wrap |
| t15 | `g_tex_mesh` | 8×8 screen dither | SV_Position/8−0.5 | `s8` point |

Constant buffers: `cPSSystem` = cb0, `cPSScene` = cb2, `cPSMaterial` = cb4.

### 3.3 Visibility test (asm 33–43)

```
screenTile = (SV_Position.xy - 0.5) * 0.125
d           = tex15(screenTile).z                    // sampler .yxwz
alphaSrc    = t0(uv0).z                               // Base_Tex_SRGB, BLUE channel
discard if (d.z*cb0[0].w - alphaSrc < 0) or (v8.w - mesh.x < 0)
```

**OBS (file):** `st002/.../pitch_alp.ftex` is 4096×2048 DXT5. Its **B channel is a
crisp on-pitch mask** (77.8 % of texels at 255, 19.4 % at 0, the rest dithered; the
zero region is a border frame around the playable area). **A ≈ 0** (0.73 % nonzero)
so the alpha channel is unusable as coverage. R and G are high-frequency noise.
**INF (high):** the pitch is discarded outside the mask; RGB is dead weight.

### 3.4 The grain tangent frame (asm 44–55)

```
ng  = normalize( 2 * (t7.r, t7.b, t7.g) - 1 )        // .xzy channel order!
T   = orthonormalised frame from ng                   // asm 49-55, Gram-Schmidt
```
`ng` is built from `Grain_Tex_LIN` with the **swizzle `.xzy` (asm 45)**, so the
blue texel drives `.y` and the green texel drives `.z`.

**OBS (file):** `st002_pitch_grain_alp.ftex` is 2048×1024 **BC7** (ftex pf 11, 10
mips); channels R 4–231 mean 127.5, G 16–235 mean 126.9, **B 132–255 mean 210.8**,
**A 0–255 mean 16.3**. `|2·ng−1| ≈ 1.00`, consistent with a real normal.

### 3.5 View vector and `CameraVectorBlend` (asm 58–66)

```
Vw = normalize(v8.xyz - cb2[20].xyz)        // v8 = world pos, cb2[20] = camera pos
L  = normalize(cb2[10].xyz)                 // sun direction
V  = normalize( Vw + CameraVectorBlend * (-L - Vw) )     // asm 62-66
```
With `CameraVectorBlend = 0` (this install) `V = Vw`.

### 3.6 The slope table lookups (asm 71–75) — `SlopeTable_Tex_LIN`

```
dVN = dot(V, ng) ;  dVT = dot(V, T)
s0  = tex5( 0.5 + 0.5*dVN , 0.5 + 0.5*dVT )        // asm 72-73
s1  = tex5( 0.5 + 0.5*dVN , 0.5 + 0.5*dVT ).y      // asm 74-75 (same uv, .y lane)
```
Note `s0` is the only lookup; both `r0.y` and `r0.z` receive swizzled channels
`.x`, `.z` of the same sample. **INF (medium):** the 128² table is a smooth
view-tilt LUT; the live value channel could not be pinned to R or A without a GPU
run (both are monotone ramps).

### 3.7 Height terms, density scalar and the wither curve (asm 71–96)

```
h     = log2(V.y)
Qt    = 2^(h * AOParamX)                    // 1.0     -> = V.y
Pw    = 2^(h * EmissiveParam0)              // 0.05
lin   = Base2.a ^ (1/2.2)                   // asm 83-85, exponent 0.454545
v     = lin * pitch_alp.r                   // asm 86: Base_Tex_SRGB.RED
vlog  = log2(v)
aoy   = 2^(vlog * AOParamY)                 // 0.1     (asm 88, lane .x of cb4[1].yyzy)
aoz   = 2^(vlog * AOParamZ)                 // 1.5     (asm 88, lane .z -> AOParamZ)
em1   = 2^(vlog * EmissiveParam1)           // 0.1     (asm 89, cb4[0].zwzz lane 0)
em2   = 2^(vlog * EmissiveParam2)           // 17.09   (asm 89, cb4[0].zwzz lane 1)
wither= 1 - ( Qt*(aoz - aoy) + aoy )        // asm 93-94
match = 1 - wither * AOParamW               // asm 95
spend = 1 - s1*match                        // asm 96
```

**Two corrections vs. a naive read of `cb4[1].yyzy`:** lane 0 is `cb4[1].y`
(AOParamY) and lane 2 is `cb4[1].z` (**AOParamZ**, not AOParamY). The two
exponents are therefore 0.1 and 1.5. This was pinned by the numeric run
(`aoy = 0.892252`, `aoz = 0.180848` for one probe pixel).

**`Base_Tex_2_SRGB.a` is the master density scalar** and `Base_Tex_SRGB.r` scales it.
The shading source of the grass is `Base2.rgb` (`turf_bsm_alp_rgba32`).

### 3.8 GB2 outputs (asm 162–179) — exact, verified

```
S0  = sat( lin * SrmParameter0 + SrmParameter3 )     // 1 , 0
S1  = sat( lin * SrmParameter1 + SrmParameter4 )     // 0.24, 0.76
rrr_x = S0 ; rrr_y = S1 ; rrr_w = S0                 // asm 164: lanes .x,.y,.w
r6z = Pw * (em2 - em1) + em1                         // asm 172-173
r0x = s0.x * match * S0                               // asm 174-176
r0y = s0.z * r6z                                      // asm 174-175
m2  = 1 - sat(mask.b * 0.1)                           // asm 108-109
m3  = 1 - sat(mask.b * 0.5)
m4  = 1 - sat(mask.b * 1.0)
GB2.x (roughness) = S1
GB2.y (specular)  = m4 * r0x
GB2.z (metalness) = 0                                // asm 179, always
GB2.w              = r0y * lin                        // NOT the preset index
T (translucency)   = m3 * S0                          // asm 165
GB1.z              = (int(T*63 + 0.5) * 16 + 3) / 1023 // asm 167-171
```

**OBS and important:** `GB2.w` is **not** the material-preset index for the pitch.
It is a slope-table × wither × density scalar. And `GB1.z` encodes
**shading-model ID 3** in the low nibble with `T` in the high bits.

**Therefore `MatParamIndex_0 = 200` has no effect on this shader build's lighting.**
Two independent reasons, both OBS: (a) the PS reads `cb4[0].x` nowhere, and (b)
the pitch emits **shading-model ID 3**, so `SSLighting_Sun_NoShadow.ps.asm`
(lines 42–53) takes the hard-coded ID-3 branch and never samples
`g_MaterialTexture`. The data *does* place preset 200 at `cb4[0].x` exactly where
every other material puts its preset, and the install's
`Fox/Asset/#Win/material_params.fmtt` reads
`A = (0.0353, 0.298, 0, 0)`, `B = (0.898, 1.0, 0.8, 0.9373)` at 200 — it is simply
unconsumed here.

**What actually lights the pitch** (`asm/SSLighting_Sun_NoShadow.ps.asm:50-53, 79-81`):
```
F0    = 0.04 + 0.96 * GB2.z = 0.04          // metalness = 0
tint  = (0.9, 1.0, 0.8)
wrapW = 1 ;  T = (GB1.z >> 4) / 63
diffuse = lightColor * max(saturate(NdotL + T*(1-NdotL^2)), wrapW*T)
```
i.e. a white-green specular tint with **full wrap and zero translucency** for the
pitch — the green sheen of PES grass. Preset 200's `(0.898, 1, 0.8)` and
`wrap 0.9373` describe the same intent but are not the code path.

### 3.9 Albedo (asm 97–112) — exact, verified

```
// asm 97: r7 = 1 - Base2.rgb
// asm 98: c  = 1 - (1 - Base2.rgb) * spend          (r1.xyw lanes .x .y .w)
// asm 99-100: repaired = Base2 + Grain.a * (RepairColor - Base2)   (blend weight = Grain.a)
// asm 101:   curve.r = sat( Base2.r * WithersParameter0 + WithersParameter3 )   // 0.78, 0.1
// asm 102:   curve.g = sat( Base2.g * WithersParameter1 + WithersParameter4 )   // 0.78, 0.07
//            curve.z = sat( Base2.b * WithersParameter2 + WithersParameter5 )   // 0.96, 0.04
// asm 103:   tint    = curve - repaired
// asm 105:   a  = max( Base_Tex_SRGB.a , mask.r )     // AO
//            gW = max( Base_Tex_SRGB.g , mask.g )      // grain weight
// asm 106:   y  = gW * tint + repaired
// asm 107:   n  = c * y            (lane-wise, c.x*y.y etc.)
// asm 108-109: m2 = 1 - sat(mask.b * 0.1)
// asm 110:   t  = n * m2
// asm 111:   u  = 0.8 - t           (lane .w of the immediate is 0.8)
// asm 112:   ALBEDO = saturate( a * u + t )   per channel
```

**The three Withers channels are three distinct pairs**, and the green/blue gains
come from `WithersParameter1/2` (0.78/0.96), not from
`AOParamY`/`RepairColor` — my first reading of `cb4[5].yyzy`/`cb4[6].xxyx` was
wrong and was corrected by the numeric run.

With `Mask_Tex_SRGB` unbound (st002) `a = Base_Tex_SRGB.a` (= 0 in this install,
so `ALBEDO = sat(t)`) and `gW = Base_Tex_SRGB.g`.

### 3.10 Normal (asm 114–152)

```
n1    = 2 * t4(uv0 × repeat).xy - 1              // GrainNormal_Tex_LIN, RG only
grainN= n1 + NormalFlatness * ((0,0,1) - n1)     // asm 126
... orthonormal re-projection of grainN against the grain frame (asm 128-135) ...
nBlend= mix(projFrame, grainN, curve.b)          // asm 137, weight = the albedo curve's blue
N     = project nBlend into the view basis (cb2[8],[9],[10])   // asm 141-143
GB1.xy= octahedral encode N                      // asm 144-160, 11-bit
```
**INF (high):** worn / discoloured grass (high `curve.b`) gets a flatter, dirtier
normal. `NormalFlatness = 0.5` halves the detail-normal strength.

**OBS (file):** `st002_pitch_detail.ftex` is 2048×1024 **DXT1**; B and A are
constant 255, R 82–156 mean 117.4, G 85–154 mean 116.2, so the xy slope spans only
**±0.36** — a deliberately flat normal map for a broadcast-camera view of grass.

### 3.11 GB3 / GB4 (asm 180–186)

```
GB3.xy = (0.5 + 0.5 * ((prevWorld/curWorld) remap ...)) * cb0[2].xy
GB4    = (0,0,0,1)
```
The VS also writes a **height-fog colour into `o7.z`/`o7.w`** from a 4-term
polynomial in `saturate(2 - 12·dist/(-cb2[5].y))/2` (fog ramp), which the PS adds
back to the GB3 channels (asm 166). **INF (medium):** GB3 carries a fog-vector
delta rather than a pure screen-space velocity. For a Godot port the whole block
can be dropped — Godot's fog and depth replace it.

---

## 4. `fox3ddf_blin_decal` — the line layer and `pitch_scratch` (OBS)

`asm/fox3ddf_blin_decal.ps.asm`, 84 lines. Pure deferred decal: writes
GB0/GB1/GB2 only (`o4 = (0,0,0,0)`, no GB3 in this build).

```
// --- visibility (lines 24-32) ---
screenTile = (SV_Position.xy - 0.5) * 0.125                     // (x-0.5)/8
ditherUV.y = screenTile.y + (int(screenTile.y) & 1)              // lines 26-28: Bayer offset
d          = t15(ditherUV).y
base       = t0(uv0)
discard if (base.a - d*cb0[0].w < 0) or (v8.w - v1.w < 0)        // v1.w = cb5[8]w object fade
discard if (v1.w < 0.9)                                          // lines 28-29: decal fade gate

// --- albedo (lines 31-33) ---
GB0.rgb = base.rgb                                              // NO scaling (no cb4 here)
GB0.a   = base.a
o1.w = o2.w = base.a                                            // decal blend weight

// --- normal (lines 34-66) ---
N = TBN( nrm( t1(uv0) ) )                                       // standard PES decode
o1.xy = octahedral(N) ; o1.z = 0                                // shading ID 0, T = 0

// --- specular AA + roughness/spec (lines 67-78) ---
sigma2 = 2 * min(0.5*(|ddx N| + |ddy N|), 0.30001)^2             // per screen axis
rough  = f( sqrt( srm.g^2 + sigma2 ) )                          // f = §0.4 Blin remap
GB2.x = rough ; GB2.y = srm.r ; GB2.z = 1
GB2.w = base.a                                                  // blend weight, NOT MPI
GB4   = (0,0,0,0)                                               // line 116
```

**Important, OBS:** this shader declares `cb0[1]` only — **no `cb4` and no
`MatParamIndex_0` at all** (compare `asm/pes3ddc_blin.ps.asm`, which declares
`cb4[2]` and reads `cb4[0].y` as an albedo scale). The material-preset index is
not written by this PS; the deferred-decal render type supplies it outside the
shader (INF, high — `asm/Decal.ps.asm:88` writes `o2.z = cb4[0].x` in the
*forward* decal path, which confirms the index is a per-decal constant, not a
texture-derived value).

**Line/scratch channel facts (OBS from the install's common maps):**

| Map | Decoded | Used for |
|---|---|---|
| `line_alp` (512², RGBA32) | R=G=B=**208** constant, A = 0..135 | flat grey 0.816 albedo; alpha = coverage |
| `line_nrm` (512², RG/B) | R=**255** const, G 32..219, B=0, A 36..219 | RG normal; R fixed at 1 → BC5-style `.x` decode |
| `line_srm` (512²) | R 123..206 mean 182, G 174..203 mean 182, **B=0**, A=255 | R spec 0.71, G rough 0.71; **A is a constant 255** → alpha test always passes, so the alpha from `line_alp` decides |

So the lines are a **uniform mid-grey (0.82) with a small normal perturbation
and a fairly rough, quite reflective surface** (spec and rough both ≈0.71),
blended over the pitch by `line_alp.a`.

**Answering the brief's question directly:** no, `line_alp_dds1` is **not** the
same shader as `pitch_ed`, and `MatParamIndex_0 = 29` is **not** a branch
selector. It is a different technique (`fox3DDC_Blin`, a Blin family PBR decal)
whose only material slot is `MatParamIndex_0`, and since this shader declares no
`cb4`, the 29 is consumed by the deferred-decal render state rather than the PS.
The branch-like difference between `line_alp_dds1` (29) and `pitch_scratch` (0)
is therefore entirely in whatever the engine binds per decal. In
`material_params.fmtt` the two rows differ as 29 →
`A = (0.1608, 0.498, 0, 0.898)`, `B = (1,1,1,0)` (**F0 0.1608, white tint,
wrap 0, and a nonzero A.w = 0.898**) versus 0 →
`A = (0.0392, 0.6, 0, 0)`, `B = (1,1,1,0)` (**default dielectric F0 0.039,
no wrap**). **INF (high):** the lines are meant to be a bright, fairly
reflective paint and the scratch patch ordinary dirt; if a port wants to honour
`MatParamIndex_0` for the two decals it should use exactly those two F0 values,
since the PS itself will not.

---

## 5. The turf

### 5.1 `MatParamIndex_0 = 200` — what it selects (OBS, and a correction)

Two independent facts, both OBS:

1. **In the shader it selects nothing.** `pes3ddf_pitch_2020.ps` reads
   `cb4[0].x` in **no instruction** (`grep cb4` over the asm: the only uses are
   `cb4[0].y/.z/.w`, `cb4[1].*`, `cb4[2].xy`, `cb4[3].x`, `cb4[4].xyz/.w`,
   `cb4[5].*`, `cb4[6].*`, `cb4[7].*` — see §3.8). And the pitch emits
   **shading-model ID 3**, which makes `SSLighting_Sun_NoShadow` take the
   hard-coded ID-3 branch (`asm/SSLighting_Sun_NoShadow.ps.asm:49-53`) and never
   index `g_MaterialTexture`. So the value never reaches the BRDF.
2. **In the data it is the material-preset index.** The lua packing places
   `MatParamIndex_0` at `cb4[0].x`, exactly where every other material puts its
   preset (compare `asm/pes3ddf_boots.ps.asm:97` `mov o2.w, cb4[0].x`). The
   install's `Fox/Asset/#Win/material_params.fmtt` (8192 B, 256 float4 pairs)
   reads at preset 200: `A = (0.0353, 0.298, 0, 0)`,
   `B = (0.898, 1.0, 0.8, 0.9373)`.

**INF (high):** the preset exists for the *other* pitch variants and for a
forward/`_EN` path, and the ID-3 branch in `Pitch2020` is what actually renders.
The family in the table (200–204) all share `A.y = 0.298` and differ in F0/tint:
200 `(0.0353, 0.898/1/0.8, wrap 0.9373)`, 201 `(0.0431, same tint, wrap 0.8)`,
202 `(0.0275, white tint, wrap 0.8)`, 203 `(0.0431, 0.8/1/0.6, wrap 0.8)`,
204 `(0.0431, same tint as 200, wrap 0.898)`. Those look like *turf condition*
variants (dry / lush / worn / night). **The report's earlier draft claimed
preset 200 supplied the pitch's green sheen; that is superseded — the sheen
comes from the ID-3 hard-coded tint (0.9, 1, 0.8).**

The `draw_parameter` XML is unrelated: `common/bg/model/bg/draw_parameter/stNNN/
draw_parameter_{df,dr,nf,nr}.xml` holds `<tv_model_intensity box line>`
(3 scalars) per `docs/re/pes-re-stadium-camera.md` §1.2 — no pitch branch.

### 5.2 The mowing pattern (OBS + OBS decode)

**The mowing stripes are NOT in the pitch material.** Two independent
confirmations:

1. **OBS (data):** the pitch's own colour source, `common/pitch/sourceimages/
   #windx11/turf_bsm_alp_rgba32.ftex` (1024², RGBA32), has an **alpha channel
   that is pure noise** — no banding at any scale. Box-downsampled to 32×32 the
   values are 84–116 with no periodic structure; a 1-D FFT of one row peaks at
   short wavelengths (3–8 px), i.e. per-texel grain, not mow bands. Its RGB is
   `(65, 85, 12)` mean, i.e. a dark olive-green grass colour.
2. **OBS (shader structure):** the pitch PS samples `Base2` at `uv0 * (24, 16)`
   (asm 80) — a fixed tiling of the shared 1024² map, which cannot produce
   stadium-wide stripes.

**Where the stripes are:** the separate grass mesh
`Asset/model/bg/common/turf3d/#Win/turf3dtexlodu4.fmdl` (OBS from the
common.cpk listing, `docs/re/pes-re-stadium-camera.md` §1.1; the pitch fmdl AABB
is `y 0 … 0.008`, `fbox.py`, so the turf fins sit just above it). Its technique
is `pes3DDF_Turf3D2020` → `ShaderAssignList` row
`{ technique="pes3DDF_Turf3D2020", type="Deferred", shader="pes3ddf_turf3d_2020"}`
(`PesShaders_dx11.lua:3142`), packing
`{ MatParamIndex_0, DriedColor, PitchOffset, UvOffset, Parameter0, Parameter1 }`
with 8 textures including **`Grass_MaskMap_Tex_LIN`** and
**`Grass_DiffuseMap_Tex_SRGB`** (`PesShaders_dx11.lua:380-384`,
`numvertextextures = 7`).

**OBS (decoded):** the deferred-decal turf VS
(`/tmp/PitchGrassRE/sh/turf2020.vs.asm`, 237 lines) contains the wind animation
and the stripe source:

- **Wind** (lines 27–41): the blade offsets from `v0.y + 10` are scaled by
  `0.3·|cameraPos - pitchCentre|² + 0.0125` — i.e. **wind sway amplitude grows
  with the camera's distance from the pitch centre**, so distant grass moves
  more. The blade is displaced by `±16·dist` along the ground plane.
- **Stripe/mask lookup** (lines 46–53): `t0` sampled at
  `worldXZ·cb4[1].xy + cb4[1].zw` — **an affine world-space UV with an offset**,
  so the mask map is laid out **in world space**, not per-mesh UV. This is what
  gives clean, pitch-wide mow bands.
- **Dried/worn blends** (lines 118–160): `t4`/`t6` (the diffuse/detail maps) are
  combined with the LUT `t3`, then mixed through *three* colour ramps toward a
  dried brown `(0.145, 0.322, 0.035)`, a mid green `(0.271, 0.302, 0.227)` and a
  lush `(0.312, 0.402, 0)` — **so the turf3d shader owns the dry/lush gradient**,
  not the pitch material.
- **`o1.z`** (line 99) = `1 - sum(t5.rgba)` — the planar-shadow density.
- **GB1.z** (line 78) = `bfi(28, 4, (int)(v1.w·63 + 0.5), 3)` → the same
  ID-3 + T encoding as the pitch PS. **The turf shares the pitch's shading
  model.**

**So the mow pattern is authored in `Grass_MaskMap_Tex_LIN` in world space on
the turf3d mesh.** `turf3dtexlodu4` was not decoded further in this session
(only the deferred-decal VS/PS at `/tmp/PitchGrassRE/sh/turf2020.*`; the full
`pes3ddf_turf3d_2020` PS is 237 lines and remains open, §8).

### 5.3 Grass colour (OBS)

`Base_Tex_2_SRGB` = `common/pitch/sourceimages/#windx11/turf_bsm_alp_rgba32.ftex`:
1024² RGBA32, RGB mean `(65, 85, 12)` (a uniform dark olive-green, no spatial
structure) and **alpha = per-texel density in 0..255 with mean 97.7, std 61.6,
and ~1.2 % fully transparent** (OBS: `pixchk.py`). That alpha is the master
scalar: it is linearised (`a^(1/2.2)`), multiplied by `pitch_alp.r`, then
exponentiated into roughness (`v^AOParamY = v^0.1`) and the wither/specular terms
(`v^AOParamZ = v^1.5`, `v^EmissiveParam1/2`) — §3.7.

### 5.4 Wear / the slope table (OBS + file decode)

`SlopeTable_Tex_LIN` = `common/pitch/sourceimages/#windx11/grass_slopetable_alp.ftex`:
**128×128**, R 74–255 mean 132.9, G 195–255 mean 216, B 156–228 mean 187.9,
**A 0–253 mean 97.1** (OBS: `pixchk.py`). Sampled with `g_samplerLinear_Clamp`:

```
s0 = table( 0.5 + 0.5*dot(V, ng) , 0.5 + 0.5*dot(V, T) )     // asm 72-73
s1 = table( same uv ).y                                         // asm 74-75
```
`ng` is the **grain normal from `Grain_Tex_LIN`** (not the fine
`GrainNormal_Tex_LIN`), so the table is indexed by *how grazing the view is
relative to the local grass-blade orientation*. Its outputs scale **roughness
and specular** (asm 174–178) and the albedo `spend` term (asm 96).

**The pitch's `SpecularMap_Tex_LIN` (`turf_srm_alp`) is declared in the lua
packing and in the fmdl but is NOT bound in the PS's RDEF (OBS: t2 is absent).
It is therefore unused by `pes3ddf_pitch_2020`.** (Its decoded content:
R 0..255 mean 89, **G 0..255 mean 212.6**, B 0..255 mean 6.9, A 0..255 mean
195.8 — a proper spec/rough map, but only the *line* and *scratch* materials
consume theirs.)

### 5.5 A stadium's own pitch maps vs the common turf maps (OBS)

| Texture | Scope (OBS fmdl path / VFS) | Slot | Role (verified) |
|---|---|---|---|
| `stNNN/.../pitch_alp` | per stadium | `Base_Tex_SRGB` t0 | **R = density multiplier**; **B = on-pitch mask**; G = grain weight |
| `stNNN/.../pitch_detail` | per stadium | `GrainNormal_Tex_LIN` t4 | RG fine normal, decoded slope ±0.36 |
| `stNNN/.../pitch_grain_alp` | per stadium | `Grain_Tex_LIN` t7 | BC7; `(R,B,G)` = blade-orientation normal (drives the tangent frame + slope table) and **A = repair-colour blend weight** |
| `common/.../turf_bsm_alp_rgba32` | common | `Base_Tex_2_SRGB` t3 | RGB = grass colour (olive), **A = density** |
| `common/.../grass_slopetable_alp` | common | `SlopeTable_Tex_LIN` t5 | 128² view-tilt LUT |
| `common/.../turf_nrm`, `turf_srm_alp` | common | t1, t2 | **t1 and t2 unbound by the PS** |
| `common/.../line_{alp,nrm,srm}` | common | the line decal's t0/t1/t2 | grey 0.82 albedo + coverage, flat normal, spec/rough 0.71 |
| `stNNN/.../pitch_scratch_{bsm_alp,nrm,srm}` | per stadium | the scratch decal's t0/t1/t2 | re-turf patch |

So a stadium customises **three** things — the on-pitch mask / density, the fine
grain normal, and the blade-orientation grain map — while grass colour, density
scale, slope response and the lines are common.

---

## 6. `line_alp_dds1` and `pitch_scratch` — status

- **OBS:** `line_alp_dds1` uses the *common* `line_alp/line_nrm/line_srm` maps
  (all three live in `common/pitch/sourceimages/#windx11/`), so every stadium
  draws identical white markings. `line_alp` RGB is a **constant 208** = linear
  0.66 / sRGB 0.816 grey; its alpha (0–135, 85 % of texels zero) is the coverage.
- **OBS:** `pitch_scratch` has its own per-stadium
  `pitch_scratch_{bsm_alp,nrm,srm}` and `MatParamIndex_0 = 0`.
- **INF (medium, needs one examined frame):** `pitch_scratch` is a **re-turf
  patch** — fresh grass painted over the worn goalmouths / centre circle. The
  st002 pitch fmdl has 3 meshes and 5162 verts (OBS `pesfmt_dump fmdl`); the
  scratch material's alpha decides where it draws. **Not confirmed in play**: the
  game was not running and no frame was examined this session.

---

## 7. Godot recipe

The pitch is **albedo + a half-flattened normal + a roughness/specular scalar +
ID‑3 lighting**. Port it as a `ShaderMaterial` with a custom `light()`. The VS
height-fog block and GB3/GB4 can be dropped.

### 7.1 Inputs

```glsl
uniform sampler2D base2_tex;    // Base_Tex_2_SRGB    turf_bsm_alp_rgba32
uniform sampler2D grain_nrm;    // GrainNormal_Tex_LIN pitch_detail
uniform sampler2D grain;        // Grain_Tex_LIN       pitch_grain_alp
uniform sampler2D slope_tbl;    // SlopeTable_Tex_LIN  grass_slopetable_alp
uniform sampler2D pitch_map;    // Base_Tex_SRGB       pitch_alp  (R = density, B = mask)
uniform sampler2D mask_tex;     // Mask_Tex_SRGB       (unbound in st002 -> 0)

uniform float ao_x = 1.0, ao_y = 0.1, ao_z = 1.5, ao_w = 0.85;   // AOParamX..W
uniform float emis0 = 0.05, emis1 = 0.1, emis2 = 17.09;          // EmissiveParam0..2
uniform float u_repeat = 24.0, v_repeat = 16.0;                  // U/VRepeat_UV
uniform float normal_flatness = 0.5;                            // NormalFlatness
uniform vec3  repair_color = vec3(0.31, 0.4, 0.0);              // RepairColor
uniform float wither0 = 0.78, wither1 = 0.78, wither2 = 0.96;    // WithersParameter0..2
uniform float wither3 = 0.1,  wither4 = 0.07,  wither5 = 0.04;  // WithersParameter3..5
uniform float srm0 = 1.0, srm1 = 0.24, srm2 = 0.2;               // SrmParameter0..2
uniform float srm3 = 0.0, srm4 = 0.76, srm5 = 0.2;               // SrmParameter3..5

// shading-model ID 3, hard-coded in SSLighting_Sun_NoShadow.ps.asm:50-53,79-81
const vec3  ID3_TINT = vec3(0.9, 1.0, 0.8);
const float ID3_WRAP = 1.0;
const float ID3_F0   = 0.04;      // 0.04 + 0.96*metalness, and metalness = 0
```

### 7.2 Per-pixel — the verified port

```glsl
// ---- 1. density scalar -------------------------------------------------
vec4  base2 = texture(base2_tex, UV * vec2(u_repeat, v_repeat));
vec4  p0    = texture(pitch_map, UV);
float lin   = pow(base2.a, 1.0 / 2.2);          // sRGB -> linear
float v     = lin * p0.r;                        // x pitch_alp.RED

// ---- 2. height terms ---------------------------------------------------
float h  = log2(max(viewDirWorld.y, 1e-5));      // log2 of the view's height
float Qt = exp2(h * ao_x);                       // = viewDirWorld.y
float Pw = exp2(h * emis0);

// ---- 3. roughness / specular exponents --------------------------------
float vlog = log2(max(v, 1e-5));
float aoy  = exp2(vlog * ao_y);                  // 0.1
float aoz  = exp2(vlog * ao_z);                  // 1.5
float em1  = exp2(vlog * emis1);                 // 0.1
float em2  = exp2(vlog * emis2);                 // 17.09

float wither = 1.0 - (Qt * (aoz - aoy) + aoy);
float match  = 1.0 - wither * ao_w;               // = 1 - wither*0.85

// ---- 4. slope table ---------------------------------------------------
vec4  g    = texture(grain, UV);
vec3  ng   = normalize(vec3(g.r, g.b, g.g) * 2.0 - 1.0);   // .xzy channel order
// both asm lookups use this UV in this build; one sample of (r,g,b,a) feeds
// lane .y to `spend`, and lanes .x/.z to the roughness/specular chain below
vec4  st4  = texture(slope_tbl,
                    vec2(0.5 + 0.5 * dot(viewDirWorld, ng),
                         0.5 + 0.5 * dot(viewDirWorld, ng)));
float spend = 1.0 - st4.y * match;

// ---- 5. albedo ---------------------------------------------------------
vec3  base2c    = base2.rgb;
vec3  repaired  = base2c + g.a * (repair_color - base2c);
vec3  curve;
curve.r = saturate(base2c.r * wither0 + wither3);
curve.g = saturate(base2c.g * wither1 + wither4);
curve.b = saturate(base2c.b * wither2 + wither5);
vec3  tint = curve - repaired;
float a    = max(p0.a, texture(mask_tex, UV).r);   // AO
float gW   = max(p0.g, texture(mask_tex, UV).g);   // grain weight
vec3  y    = gW * tint + repaired;
vec3  c    = vec3(1.0) - (vec3(1.0) - base2c) * spend;
vec3  n    = c * y;
float m2   = 1.0 - saturate(texture(mask_tex, UV).b * 0.1);
vec3  tOut = n * m2;
vec3  uOut = vec3(0.8) - tOut;
ALBEDO     = saturate(a * uOut + tOut);

// ---- 6. normal ---------------------------------------------------------
vec2 n2      = texture(grain_nrm, UV * vec2(u_repeat, v_repeat)).xy * 2.0 - 1.0;
vec3 n1      = vec3(n2, sqrt(max(1.0 - dot(n2, n2), 0.0)));
vec3 grainN  = n1 + normal_flatness * (vec3(0.0, 0.0, 1.0) - n1);
NORMAL      = normalize(mix(grainN, n1, curve.b));   // approximate; see §3.10

// ---- 7. GB2 (roughness / specular) ------------------------------------
float S0 = saturate(lin * srm0 + srm3);
float S1 = saturate(lin * srm1 + srm4);
float m4 = 1.0 - saturate(texture(mask_tex, UV).b * 1.0);
float r6z = Pw * (em2 - em1) + em1;
float r0x = st4.x * match * S0;                  // slope .x * match
float r0y = st4.z * r6z;                          // slope .z * r6z

ROUGHNESS = S1;
SPECULAR   = m4 * r0x;
METALLIC   = 0.0;
```

> The two slope lookups in the asm (asm 72–73 and 74–75) share the same UV in this
> build, and `s0` delivers channels `.x` and `.z` to the two results. Above,
> `st4` is sampled once and read as `.xz`/`.y`. A port that wants the exact
> separation should sample twice with the documented UVs.

### 7.3 Lighting (shading-model ID 3)

```glsl
void light() {
    float NdotL = dot(NORMAL, LIGHT);
    float NdotV = clamp(dot(NORMAL, VIEW), 0.0, 1.0);
    vec3  H     = normalize(LIGHT + VIEW);
    float NdotH = clamp(dot(NORMAL, H), 0.0, 1.0);
    float T     = 0.0;                             // GB1.T for the pitch
    float wrap  = ID3_WRAP;

    ALBEDO = ALBEDO * ATTENUATION;
    // diffuse = lightColor * max(sat(NdotL + T*(1-NdotL^2)), wrap*T)  with T = 0, wrap = 1
    DIFFUSE = LIGHT_COLOR * NdotL;

    float r = max(ROUGHNESS, 0.02), a2 = r * r;
    float D = a2 * a2 / pow(NdotH * NdotH * (a2 * a2 - 1.0) + 1.0, 2.0);
    float k = a2 * 0.5;
    float Vis = 1.0 / ((saturate(NdotL) * (1.0 - k) + k) * (NdotV * (1.0 - k) + k));
    float F = ID3_F0 + (1.0 - ID3_F0) * pow(1.0 - clamp(dot(H, LIGHT), 0.0, 1.0), 5.0);
    SPECULAR = LIGHT_COLOR * F * D * Vis * saturate(NdotL) * 0.25 * SPECULAR * ID3_TINT;
}
```
(The Schlick `F` is **INF (medium)** — `g_lut1` was never found, per
`docs/re/pes-re-shaders.md` §8.2; its `(H·L, F0)` coordinates are what a Schlick
table needs.)

### 7.4 Head-start approximation (no custom `light()`)

`StandardMaterial3D` with `albedo_texture = base2_tex`, `roughness ≈ 0.55`,
`metallic = 0`, `normal_enabled` with `grain_nrm`, and `specular_mode` on the
greenish tint. This gets the hue and the flatness right; it loses the
`Base2.a`-driven density roughness and the wrap term.

### 7.5 The line layer (`line_alp_dds1`) and `pitch_scratch`

`fox3ddf_blin_decal` is a plain PBR deferred decal — `StandardMaterial3D` is enough:

```glsl
albedo_texture = line_alp;       // constant 208 grey; .a = coverage
albedo_color   = vec3(0.816);
normal_texture = line_nrm;       // RG, slope ~±0.37
roughness      = 0.71;           // line_srm.g through f() (the Blin remap, §0.4)
metallic       = 0.0;
```
plus alpha scissor on `line_alp.a` (the asm's dither alpha test at
`fox3ddf_blin_decal.ps.asm:24-32` is a stochastic version of scissor).

- **OBS:** `line_alp` RGB is a **constant 208**; its alpha (0–135, 85 % zero) is the
  coverage. `line_nrm`: R constant 255, G 32–219, B 0. `line_srm`: R mean 182,
  G mean 182, **B 0**, **A constant 255** so the alpha test always passes and
  `line_alp.a` decides.
- **INF (medium):** `MatParamIndex_0 = 29` → F0 0.1608, white tint, wrap 0 in
  `material_params.fmtt`; `MatParamIndex_0 = 0` → F0 0.039, white tint, no wrap.
  Since the decal PS declares no `cb4`, a port that wants to honour the presets
  must apply those F0 values explicitly.
- Draw `pitch_scratch` as a **decal over the pitch** where its alpha is set.

### 7.6 Skip entirely

The VS height-fog polynomial (§2) and GB3/GB4 — Godot's fog and depth replace
them. `t1`/`t2` (`turf_nrm`, `turf_srm_alp`) are unbound by the PS. `t6`
(`Mask_Tex_SRGB`) has no fmdl texture in st002; keep the slot with a zero default.

---

## 8. Open items (what was tried)

1. **`t6` / `Mask_Tex_SRGB` is never bound in st002.** Tried: the pitch fpk lists
   no such texture role, and the PS still samples it (`t6.xyzw` at asm 104).
   Its lanes feed `a = max(Base.a, mask.r)` and `gW = max(Base.g, mask.g)`
   (asm 105), the albedo scale `m2 = 1 - sat(mask.b*0.1)` (asm 108–109) and the
   Srm curves (asm 162–164). **INF (medium):** other stadiums' pitch fpks supply
   it, or it is a built-in default. A port should default it to `vec4(0)`, which
   makes `a = Base_Tex_SRGB.a`, `gW = Base_Tex_SRGB.g`, `m2 = 1` and the Srm
   curves the bare `SrmParameter*`.
   To resolve: dump the pitch fpk of every stadium and list the texture roles
   (the current dump only covers st002).
2. **`MatParamIndex_0` never reaching the BRDF is surprising; the upload was not
   observed.** Tried: confirming no `cb4[0].x` read in the asm (OBS), and
   confirming the ID-3 branch skips `g_MaterialTexture` (OBS). What was *not*
   checked: whether the engine's deferred-decal or GB-pass material-index path
   overrides `o2.w` after the PS (the PS writes a scalar there, but a blend stage
   could replace it). **INF (high) that it does not**, because
   `fox3ddf_blin_decal` writes the base *alpha* to the same slot and the decal
   blend needs it.
3. **`SlopeTable_Tex_LIN` value channel: R or A.** Both are smooth 0.2–1.0
   ramps in the 128² file; the PS reads `.x`/`.y`/`.z` of the swizzled result
   (OBS that the swizzle is `t5.yxzw`), so the channel used per lane is fixed by
   the asm. Not decidable without a GPU run. A port uses R for the `.x` lane.
4. **`Grain_Tex_LIN` channel order `(R,B,G)`.** OBS the asm swizzle
   `t7.xyzw` → `.xzy` at asm 45; the decoded magnitudes are the same either way
   (both ≈1.002), so the file cannot disambiguate. **INF (medium)** on xzy from
   the asm.
5. **`pes3ddf_turf3d_2020` (the full grass mesh, not the deferred-decal variant)
   was not decoded.** Tried: the record exists in `PesShaders_dx11.fsop`
   (`fsop.py` lists `pes3ddf_turf3d_2020`), and the deferred-decal pair
   `turf2020.vs.asm` (237 lines) is in `/tmp/PitchGrassRE/sh/`. The full PS is
   the priority-2 remainder; it holds the actual blade geometry, the
   `Grass_MaskMap_Tex_LIN` stripe lookup and the dry ramps — i.e. the mowing
   pattern itself (§5.2).
6. **`pes3DDF_Pitch2020_EN`** (`pes3ddf_pitch_2020_en`) uses `t14` (an extra
   texture, RDEF-unverified) and `o0.a = t14.w` — a different normal and alpha
   path. OBS from `asm/pes3ddf_pitch_2020_en.ps.asm:117-119,130-133`; INF (high)
   it is the edit/preview pitch (`edit_pitch_sourceimages/ed_02u_alp.ftex`,
   decompiled at `/tmp/PitchGrassRE/out/pp2.txt`).
7. **`pitch_scratch` visibility in play** not confirmed — needs one examined
   frame; the game is not running and the assignment forbids launching it.
8. **`g_tex_mesh` (t15) content** is the same open item as
   `docs/re/pes-re-shaders.md` §8.1; not re-attempted.
9. **The `Fresnel` `g_lut1` LUT** remains unfound (INF: Schlick,
   `F0 + (1-F0)(1-LdotH)^5`), per `docs/re/pes-re-shaders.md` §8.2. It affects
   only the specular highlight's shape, not the diffuse colour the port needs.

### Parameter registration (bonus, OBS)

`/tmp/PitchGrassRE/out/pitchparam.txt` is the headless decompile of
`FUN_141924640` (VA `0x141924640`, image) — the `PitchMaterialParameter`
registration function. It names, in order, `emissiveParameter0/1/2`,
`aoHightPower`, `aoMin`, `aoMax`, `aoBottomUp`, `uRepeat`, `vRepeat`,
`normalFlatness`, `repairColor`, `cameraVectorBlend`, `withersParameter0..5`,
`srmParameter0..5` (OBS: strings at `0x142a73a9f`…`0x142a73f90`, referenced from
that one function). **This confirms the fmdl parameter set and the spelling
`aoHightPower`** (sic) that `AOParamZ` corresponds to, and confirms
`FUN_141924080` is the parameter-class constructor
(`FUN_14026cc20(lVar1, FUN_141924080)`).
