# PES 2021 — particle, effect, weather and post chains (RE report)

Scope: the particle/effect layer for the match: the engine's particle classes,
the data files that define emitters, the runtime path from a trigger to a
visible particle, weather, and the post chain. It cross-references
`pes-re-lighting.md` (weather + post), `pes-re-stadium-camera.md` (effect packs),
`pes-re-shaders.md` and `pes-re-sim-motion-mapping.md`.

Tags: **OBS** = read in the cited file/bytes; **INF** = inference with a
confidence. Every runtime address is a VA in the shipped
`$PES21_ROOT/PES2021.exe` (image base `0x140000000`); sections verified from the
PE header with `objdump -h`: `.trace` va `0x140001000`, `.rdata` `0x14252f000`,
`.data` `0x14339b000`. For all three, **file offset = VA − 0x140000A00** (they are
contiguous). VFS paths are read with `build/apps/pesfmt_dump` from this repo's
`src/pes`.

The game was **not** running (checked; no live read in this report). Static
sources: `<scratch>/pesmem.bin` (image snapshot, base `0x140000000`), the exe, the
VFS, and the shipped data.

---

## 0. Headline

1. **PES 2021 ships *two* particle engines. Only one has data.**
   - `fx@fox`, a node-graph FX system (`.vfx` / `.vfxbin` / `.vfxdb` / `.vfxlf`
     extensions, 150 classes). It is compiled in and initialised
     (`FxDaemon.Initialize()` in `Fox/Scripts/Gr/init.lua`), **but no `.vfx`,
     `.vfxbin` or `.vfxlf` file exists in the install** (OBS: full VFS scan,
     §4.2). It is dead for a 1:1 port.
   - `rd::cobra` "Tpp Effect", the one that is actually used: an XML-driven
     multi-stage particle emitter with CPU-built billboard quads. **All shipped
     effect data is here.**
2. **The used system's data** is:
   - `effect_config.xml` inside `effect/#Win/effect_stNNN_{df,dr,nf,nr}.fpk`
     (per stadium × weather × time-of-day) and
     `Asset/model/bg/common/effect/#Win/effect_demo.fpk`;
   - `.fdc` cut files carrying **type-3 Effect records** (`DemoPlayEffectData`,
     188 B) for cutscenes (see `pes-re-cutscenes.md` §3.2);
   - five pre-built effect **models** in `Asset/effect/#Win/common_effect.fpk`.
3. **There is no per-particle grass-spray, ball-strike flash or impact-puff
   emitter in the shipped data.** The emitters that exist are the
   cutscene/demo set (smoke bomb, smoke jet, paper jet, fireworks, camera
   flashes) plus per-stadium light effects and the rain layers. See §1.6 for the
   evidence and §2.2 for what a slide actually produces in PES (shader/material
   level, not particles).
4. **Weather and post** are fully covered by `pes-re-lighting.md`; §2 and §3 of
   this report add the values that report leaves open (the literal
   `MultiUvScrollScreen` / `RainDemo` parameter blocks, the snow path, the
   weather-selection code, the AA/grain reads) and the runtime VFS paths.

---

## 1. The particle engine that is actually used (`rd::cobra` "Tpp Effect")

### 1.1 Class census (OBS: RTTI type-descriptor names in the image)

The type-descriptor names live in `.data` in one block,
`0x143693c58 … 0x143697c00`. Reading it gives the complete class map:

```
.?AVEffectConfig@cfg@cobra@@                 vtable 0x142a9b450  (Impl)
.?AVEffectConfigImpl@EffectConfig@cfg@cobra@@
.?AVEffectSettingRainScreen@cfg@cobra@@          (and …SettingMultiUvScrollScreen,
.?AVEffectSettingRainDemo@cfg@cobra@@            …RainDemo, …LightBillboard,
.?AVEffectSettingLightBillboard@cfg@cobra@@      …LensFlare, …Vignetting,
.?AVEffectSettingLensFlare@cfg@cobra@@           …AtmosphericFog, …SmokeBomb,
.?AVEffectSettingVignetting@cfg@cobra@@          …SmokeBomb2, …SmokeJet, …PaperJet,
.?AVEffectSettingAtmosphericFog@cfg@cobra@@      …PaperJetCamera, …SmokeScreen,
.?AVEffectSettingSmokeBomb@cfg@cobra@@           …Confetti, …Halo, …Firework,
.?AVEffectSettingSmokeBomb2@cfg@cobra@@          …BloodDropsOnCameraLens,
.?AVEffectSettingSmokeJet@cfg@cobra@@            …ToneAndMannerBackground,
.?AVEffectSettingPaperJet@cfg@cobra@@            …LensDistortion)
.?AVEffectSettingPaperJetCamera@cfg@cobra@@
.?AVEffectSettingConfetti@cfg@cobra@@
.?AVEffectSettingHalo@cfg@cobra@@
.?AVEffectSettingSmokeScreen@cfg@cobra@@
.?AVEffectSettingFirework@cfg@cobra@@
.?AVEffectSettingBloodDropsOnCameraLens@cfg@cobra@@
.?AVEffectSettingToneAndMannerBackground@cfg@cobra@@
.?AVEffectSettingLensDistortion@cfg@cobra@@

.?AVEffectSmokeJet@rd@cobra@@        vtable 0x142ab3740
.?AVImpl@EffectSmokeJet@rd@cobra@@   vtable 0x142ab37a0
.?AVEffectFirework@rd@cobra@@        vtable 0x142ab4928
.?AVImpl@EffectFirework@rd@cobra@@   vtable 0x142ab4958
   — the same pattern for EffectRainDemo, EffectLightBillboard, EffectLensFlare,
     EffectVignetting, EffectAtmosphericFog, EffectPaperJet,
     EffectPaperJetCamera, EffectHalo, EffectSmokeBomb2,
     EffectBloodDropsOnCameraLens, EffectToneAndMannerBackground,
     EffectMultiUvScrollScreen, EffectRainScreen — one `Impl@…` per setting class.

.?AVParticleBase@?A0x2eedcb02@rd@cobra@@    vtable 0x142ab3750   (SmokeJet's)
.?AVSmoke@?A0x2eedcb02@rd@cobra@@           vtable 0x142ab3780
.?AV?$RingBuffer@PEAVParticle@ParticleBase@?A0x2eedcb02@rd@cobra@@@rd@cobra@@
.?AVParticleBase@?A0x7d4bc54b@rd@cobra@@    vtable 0x142ab3ae8   (PaperJet's)
.?AVPaper@?A0x7d4bc54b@rd@cobra@@           vtable 0x142ab3b08
.?AVParticleBase@?A0xb0586254@rd@cobra@@    vtable 0x142ab3f10   (PaperJetCamera's)
.?AVPaper@?A0xb0586254@rd@cobra@@           vtable 0x142ab3f30
.?AVParticleBase@SmokeBomb2Impl@rd@cobra@@  vtable 0x142ab4558
.?AVSmoke@SmokeBomb2Impl@rd@cobra@@         vtable 0x142ab4590
.?AVParticleEffect@FireworkImpl@rd@cobra@@  vtable 0x142ab4938
```

Read: **each effect type owns a nested `ParticleBase` template instantiation and
its own particle data class** (`Smoke` for SmokeJet/SmokeBomb2, `Paper` for
PaperJet/PaperJetCamera, `ParticleEffect` for Firework), all pooled through
`RingBuffer<Particle*>`. The effect object (`EffectSmokeJet`) holds several such
pools, one **per particle stage** (`<param index="k">` in the XML, see §1.3).

Vtables found with the project's `vt.py` on `<scratch>/pesmem.bin` (COL walk; the
`off=0x0` and COL addresses are printed by the tool).

### 1.2 Runtime path, trigger → visible quad (OBS)

```
.fdc type-3 record (DemoPlayEffectData, 188 B)
        ↓  FixDemoEffectData / FixDemoEffectDataBody  (vtables 0x142bab118 / 0x142bab098)
           type 3 adders: FUN_141f3e050, registered FUN_141f66b10
        ↓  PlayEffect  (ScriptBind name string @0x1436bbda8, @0x142ba5284)
        ↓  EffectSetting<Foo>  (cfg::cobra)  ← parsed from effect_config.xml
        ↓  Effect<Foo> / Impl@Effect<Foo>  (rd::cobra)   vtable 0x142ab37a0 etc.
        ↓  ParticleBase<k> → Smoke / Paper / ParticleEffect  (one pool per stage)
        ↓  CPU-built 36-byte vertices + u16 indices → Primitive_* shader
```

`0x141f66b10` is `RegisterEntity` for `FixDemoEffectData`. Disassembling it shows
the **enum registration verbatim** (`0x141f88520(map, value, name)` calls), which
gives the trigger enum with exact values (OBS):

| value | name |
|---|---|
| 0 | `TYPE_SMOKE_BOMB` |
| 1 | `TYPE_SMOKE_BOMB2` |
| 2 | `TYPE_SMOKE_BOMB2_S2` |
| 3 | `TYPE_SMOKE_BOMB2_F1` |
| 4 | `TYPE_SMOKE_BOMB2_F2` |
| 5 | `TYPE_SMOKE_BOMB2_F3` |
| 6 | `TYPE_SMOKE_JET` |
| 7 | `TYPE_PAPER_JET` |
| 8 | `TYPE_PAPER_CAMERA` |
| 9 | `TYPE_PAPER_STAND` |
| 0x0a | `TYPE_STAND_FLASH_HIGH` |
| 0x0b | `TYPE_STAND_FLASH_MIDDLE` |
| 0x0c | `TYPE_STAND_FLASH_LOW` |
| 0x0d | `TYPE_FIREWORK_RED1` |
| 0x0e | `TYPE_FIREWORK_ORANGE1` |
| 0x0f | `TYPE_FIREWORK_ORANGE2` |
| 0x10 | `TYPE_FIREWORK_YANAGI1` |
| 0x11 | `TYPE_FIREWORK_ROMANCANDLE` |
| 0x12 | `TYPE_FIREWORK_FIRE` |
| 0x13 | `TYPE_FLASH_LIGHT` |

and the colour enum: 0 `COLOR_WHITE`, 1 `COLOR_TEAM_MAIN`, 2 `COLOR_TEAM_SUB`.

`DemoPlayEffectData`'s field names (OBS, string table `0x142ba9780…0x142ba9a58`)
and the 188-B `.fdc` layout are in `pes-re-cutscenes.md` §3.2; the fields are
`m_type, m_no, m_startTime, m_endTime, m_position, m_rotation, m_scale,
m_directionRange, m_life, m_lifeRandom, m_shootInterval, m_shootNumber,
m_initialTime, m_initialSpeed, m_initialSpeedRandom, m_speedChangeRate,
m_moveSpeed, m_groundHeight, m_scaleLimitTime/Min/Max, m_roll, m_rollRandom,
m_rollChange, m_opacity1..4Time, m_opacity1..4Value, m_color1..4,
m_colorTime1..4, m_colorType`.

INF (high): `m_type` selects the `<setting type>` **class** and `m_no` selects
the `<setting index>`. Evidence: the setting classes are exactly the type names
minus the `TYPE_` prefix (`SmokeBomb`, `SmokeBomb2`, `SmokeJet`, `PaperJet`,
`PaperJetCamera`, `PaperStand`→`PaperJet`, `Firework`, `Confetti`), and
`effect_demo.fpk` indexes three `SmokeBomb2` settings (0..2) that correspond to
`TYPE_SMOKE_BOMB2 / _S2 / _F1`.

### 1.3 The data file: `effect_config.xml`

Format (OBS, the `Sample` template is a comment inside
`Asset/model/bg/st002/effect/#Win/effect_st002_dr.fpk/xml`):

```xml
<effect version="2">
  <setting type="SmokeBomb2" index="0">
    <param id="texturePath"     type="path"   value="…ftex" />
    <param id="emitNum"         type="uint"   value="1" />
    <param id="emitNum" index="1" type="uint" value="11" />   <!-- per stage -->
    <param id="color_values0"   type="float"  index="2" value="1.0" />
    <param id="color_values3"   type="rgba"   index="3" value="0xffffffff" />
    <param id="spread"          type="angle"  index="0" value="13.5" />
    <param id="base_pos"        type="vector3" value="0, 0, 0" />
  </setting>
  <create type="SmokeBomb2" setting="0" />
</effect>
```

`type="angle"` is **degrees**, converted by the engine
(`fox::math::DegreeToRadian(90.0f)` in the template comment). `type="rgba"`
is `rs::Color(r,g,b,a)` — the UPPER byte is the first channel, i.e.
`0xRRGGBBAA` (OBS: template comment `color = rs::Color(0x11,0x22,0x33,0x44)` for
`0x11223344`).

Where the files are: §4.

### 1.4 Emitter parameters — the full vocabulary with units

Two naming generations exist side by side. **New generation** (used by
`SmokeBomb`, `Firework`, `SmokeJet`, `PaperJet`, `PaperJetCamera`, `Confetti` in
`effect_demo.fpk`): `base_*` + per-index `*`. **Old generation** (used by
`SmokeBomb2`): flat single-stage names. The parameter-name string table is at
`0x142a9a2e8 … 0x142a9aa18` (OBS: `strscan.py` on the exe); the exact list and
which names each type accepts are OBS from the configs in §4 and below.

| param | unit | meaning |
|---|---|---|
| `texturePath[i]` | VFS path | sprite sheet for stage *i* (`.ftex`) |
| `base_pos` | m (Fox: Y up, +Z fwd) | emitter origin |
| `base_dir` | unit vec3 | emitter axis |
| `base_scale` | m | emitter size |
| `base_initial_time` | s | delay before the first emission |
| `base_emit_interval` | s | period between emission bursts |
| `base_emit_num` | count | particles per burst |
| `base_emit_type` | uint | emit shape/gather mode (0 in all shipped data) |
| `emitNum` / `emit_num` | count | particles per burst (stage) |
| `emitSync` / `emit_interval` | frames / s | burst period. `emitSync` in `SmokeBomb` is a **frame** count (values 9, 75, 59 → at 60 fps ≈ 0.15/1.25/0.98 s) |
| `emitAcc` | m/s² | per-particle constant acceleration |
| `emitV0` | m/s | per-particle initial velocity |
| `particleNum` / `particle_num` | count | pool size for the stage (hard cap) |
| `initialTime` / `initial_time` | s | stage start delay |
| `life` | s | particle lifetime |
| `life_range` | s | random lifetime addend, ± |
| `speed` | m/s | initial speed along `dir` |
| `speed_range` | fraction | random speed multiplier |
| `drag` | 1/s | velocity decay. OBS from the update code: `v ← v·drag` per frame, so `drag` is a **per-frame** multiplier, not per second (values 0.993…0.997 in `SmokeBomb2`, 0.4/0.1 in `Firework`) |
| `spread` | degrees | half-angle of the emission cone around `dir` |
| `spread_range` | degrees | random extra cone angle, ± |
| `turn_spread_range` | degrees | per-step re-randomisation of the direction cone |
| `turn_spread_drag` | fraction | damping of the turn |
| `turn_interval` | s | period of the direction re-randomisation |
| `scale` | m | particle size |
| `scale_range` | m | random size multiplier |
| `scale_offset` | m | random size addend, ± |
| `rotate` / `rotate_range` / `rotate_offset` | degrees | sprite rotation |
| `roll` / `rollRandom` / `rollChange` | degrees/s | spin (Paper/PaperJet) |
| `opacity_times0..3` | 0..1 (fraction of life) | alpha key times |
| `opacity_values0..3` | 0..1 | alpha key values |
| `color_times0..3` | 0..1 | colour key times |
| `color_values0..3` | rgba | colour key values |
| `sprite_num_w`, `sprite_num_h` | count | sprite-sheet grid |
| `sprite` | index | base cell index |
| `sprite_offset` | index | random cell offset added per particle |
| `sprite_interval` | s | per-cell animation period |
| `blendMode` / `blend_mode` | enum | 0 = alpha blend, 1 = additive (OBS: the two modes used) |
| `emit_num_on_die` | count | particles spawned in another stage when this one dies |
| `emit_type_on_die` | stage index | which stage the death spawns into |
| `pos`, `dir`, `scale`, `spriteNum`, `pivot`, `fixRotY`, `alphaAngle` | — | old-generation aliases present in the string table |

Chained stages: a stage's `emit_type_on_die` points at another stage's index, so
one trigger can produce a chain (the `Firework` setting below is a 4-stage chain:
seed → flash → burst → sparkle).

### 1.5 The literal update rule (OBS, disassembly)

`Smoke@?A0x2eedcb02::vtable[2]` at **`0x141aba070`** is the per-particle update.
The particle record (`rdx`) is:

```
+0x00  float3 v      velocity (m/s), used as the position addend
+0x14  float  age    life counter, counts DOWN
+0x18  float  f      per-particle multiplier applied to age and to v
+0x1c  float  (as used) — see note
+0x20  ptr    vb     36-byte × 4 vertex block (144 B = 0x90)
+0x28  ptr    mb     per-particle material record (alpha/uv)
```

The code, decoded instruction by instruction:

```
age'  = age · f                                  // movss [rdx+0x14]; mulss [rdx+0x1c]
if (0.001 > age')  age' = 0                      // death: comiss vs 0x1425a9668 = 0.001f
[rdx+0x14] = age'
p     = p + v                                    // vertex block corners +0x00/+0x04/+0x08
                                               // and the same p repeated at +0x24/+0x48/+0x6c
v     = v · f                                    // mulps broadcast([rdx+0x1c])
mb[+0x04] = (2.0 − age') · [rdx+0x18]            // const 0x142565604 = 2.0f
mb[+0x08] = age' · 0.90196079f                   // const 0x1429eb208 = 0.90196079f
```

So, in implementable form:

- **lifetime**: `age ← age·m` each update; the particle is dead when
  `age·m < 0.001`. With `m` chosen as `exp(-dt/τ)`, the lifetime is geometric;
  the simplest exact port is the literal rule above (multiply, then threshold).
  The spawn code sets the initial `age` from the `life`/`life_range` params.
- **position**: `p ← p + v` per update — **not** dt-scaled in this function; it
  is a fixed per-tick step, so the host drives it at a fixed tick rate (INF
  high: 60 Hz, the Fox default; `dt = 1/60` appears in the adaptation code,
  `pes-re-lighting.md` Part C §1).
- **velocity**: `v ← v·drag` (per frame). `drag` is therefore the per-frame
  retention: `drag = 1` = no decay, `drag = 0.4` = 40 % retained per frame
  (~4-frame stop).
- **fade**: `alpha_field = (2 − age)·k`, i.e. a linear ramp whose end value is
  `(2−0)·k`. INF (medium): `age` starts at 1.0, so this is a ramp from
  `k` to `2k` and the *visible* alpha is the second field
  `age·0.90196079` = `age·230/255`, which goes `0.9019 → 0` as the particle
  ages. The two material channels are `mb+0x04` and `mb+0x08`.
- **quad**: the four vertices all get the particle's *centre* position; the
  sprite size is **not** in the vertex (it comes from the shader/`m_world`
  scale). The UVs are written once at spawn from a `±h, ±k` rectangle
  (`[rcx+8]`, `[rcx+0xc]`); see §1.5b.

#### 1.5b The spawn / quad builder (OBS, `0x141ab9440`)

Called once per particle per burst from the stage constructor
(`0x141ab8e40`, `Impl@EffectSmokeJet` region). It writes, per particle, a 144-byte
vertex block built as 4 vertices × 36 B:

```
+0x00  float3  position      (written by the update function)
+0x0c  u32     colour        = 0xff000aff  (packed ABGR=ff,00,0a,ff → orange)
+0x10  float   (0)
+0x14  float   random ∈ {0, 1}
+0x18  float2  uv            = (±h, ±k) corners of the sprite rect
+0x20  u8      0
+0x21  u8      1
```

The random comes from an **xorshift32** generator whose state is the emitter's
`+0x10` (OBS, instruction-for-instruction):

```
s ^= s << 13;  s ^= s >> 7;  s ^= s << 5;  return s & 1;
```

which picks the vertex winding order between `(0,1,2,3)` and `(2,3,0,1)`, i.e. a
90° rotation of the quad so the sprite cell varies per particle. The
half-extents `h,k` (`[rcx+8]`, `[rcx+0xc]`) are the emitter's texture rectangle
in UV space; the sign mask is at `0x1425656d0`.

#### 1.5c Which shader

The exe carries the technique name **`Primitive_TppFxSmoke_LitDP3_NS`**
(string VA `0x142a8...`, exe offset 44564440) and the FX shader pack
`shaders/dx11/FxShaders.fsop` contains the matching `Prim_Poly_LitDP3_NS`
(`<scratch>/FxShadersRE/dxbc/`). Its input signature is
`POSITION, COLOR, TEXCOORD, TEXCOORD` (OBS: DXBC ISGN chunk), matching the 36-byte
vertex above. The pack's other particle shaders (all OBS from the DXBC dump):

```
Prim_Sprite_Lit1NrmFk{,_TexAnm}  Prim_Sprite_Lit3NrmFake  Prim_Sprite_Lit3NrmRef
Prim_Sprite_Lit3ScatNrmFake      Prim_Sprite_Lit3ScatNrmRef
Prim_Sprite_Liquid               Prim_OldSpriteLighting   Prim_OldSp_Flat
Prim_Poly_LitDP1/2/3{,_NS,_TA,_TA_NS}   Prim_Poly_RainScroll{,_Gm2_2}
Prim_Poly_SunshineScroll         Prim_Poly_CamDotCurveSizAlp  Prim_Poly_CamDotNrmAlp
Prim_Poly_FastDraw{,_DL}         Prim_Poly_Softblend{,_DL}   Prim_Poly_TexAnmSoftBlend
Prim_Poly_Distortion             Prim_Poly_BulletLine{,_Lit,_Dist}
Prim_Trail_Lit1  Prim_Trail_Softblend  Prim_Trail_Distortion
Prim_Decal  Prim_Decal_Blood  Prim_Decal_BloodAnm   Prim_LocusBlade  Prim_CuttingFan
```

Names: `LitDPn` = n directional lights; `TA` = texture animation; `_NS` = no
shadow; `DL` = distance light; `VF` = vertex-fetch (instanced) variant.

### 1.6 What is *not* in the shipped data (and what was tried)

**No per-particle grass/turf spray, ball-strike flash or impact puff exists.**

Tried, all OBS:
1. Full VFS listing (121,656 paths, `pesfmt_dump ls ""`): the only `.fpk` under
   `Asset/effect/#Win/` is `common_effect.fpk`; the only `effect_config.xml`
   files are the 61 stadium `effect_stNNN_*` packs + `effect_demo.fpk` +
   `points_lines_triangles_001.fpk`. No grass/spray/dust/smoke-emitter config
   exists anywhere else.
2. Every one of the 61 stadium `effect_config.xml` files contains **only**
   these setting types (OBS, census): `Halo`, `LensFlare`, `LightBillboard`,
   `MultiUvScrollScreen`, `RainDemo`, `Vignetting`, `PointsLinesTriangles`.
   No emitter-stage parameter (`emit_*`, `life_*`, `sprite_*`, `opacity_*`) ever
   appears in a stadium config.
3. `effect_demo.fpk`'s settings are `SmokeBomb(1)`, `SmokeBomb2(3)`,
   `SmokeJet(1)`, `PaperJet(1)`, `PaperJetCamera(1)`, `Confetti(1)`,
   `Firework(5)` — all cutscene/crowd effects.
4. No `.vfx`/`.vfxbin` file ships, so the `fx@fox` node-graph emitters
   (`FxConstNumEmitNode`, `FxSpriteShapeNode`, …) have no data either.
5. A search of the exe for effect-name tables found no grass/spray/dust effect
   identifiers. The only grass-named strings are the **material** textures
   `face_grass_bsm_alp.ftex` / `face_grass_nrm.ftex`
   (`/Assets/pes16/model/character/face/common/…`, exe string VA
   `0x142a6d420`), i.e. grass stained onto the face, not particles in the air.

**What a slide does produce instead (INF, high, and §2.2):** PES renders the
turf as a *mesh* (`Asset/model/bg/common/turf3d/turf3dtexlodu4.fmdl` +
`grassfin_bsm_{lg,md,st}.ftex`, `turf_nrm.ftex`, `turf_srm_alp.ftex`,
`grass_slopetable_alp.ftex`) with a sway/blend shader; the visible disturbance on
a slide is the *displaced/de-blended turf* plus the leaf-quads in
`pitch_leaf.fmdl` / `pitch_ground.fmdl` (see §1.7), not a particle system.

### 1.7 The model-based effects (`Asset/effect/#Win/common_effect.fpk`)

The pack (14,048 B, OBS `pesfmt_dump ls Asset/effect/#Win`) holds
`vfx_class_db.vfxdb` + five effect models. Parsed with the repo's
`src/pes/fmdl.cpp` (OBS):

| file | mesh | material / technique | purpose |
|---|---|---|---|
| `vfx_class_db.vfxdb` | 2,220 B, magic `vfdb` | binary class table (§5, open) | name → effect record map |
| `3d_rain.fmdl` | 8 v / 12 i | `urethane_coat_a_nr`, `fox3DFW_ParallaxReflection`, `env_nf_clp` + `env_hgt` (height 0.2) + `effect_3d_rain_bsm_alp` | wet-roof/parallax reflection overlay |
| `effect_rainModel.fmdl` | 17 v / 72 i | `effect_rain04_alp_nomip`, `fox3DFW_ConstantSRGB`, `SelfColorIntensity = 5000` | world-space rain splash streaks |
| `net_splash.fmdl` | 108 v / 288 i | `albedoview`, `DBG_BaseTexView` | goal-net splash when the ball hits the net |
| `pitch_ground.fmdl` | 12 v / 18 i | `leaf_trans_a`, `fox3DDF_Blin_Translucent_LNM`, `effect_leaf_{bsm_alp,nrm,srm,trm}` | ground impact/debris quad on the pitch |
| `pitch_leaf.fmdl` | 8 v / 12 i | `leaf_trans_a`, `fox3DDF_Blin_Translucent_NC_LNM`, same 4 textures | leaf/grass-debris quad (`_NC` = no cloth) |

All five are **static meshes with no bones, one material, one mesh** — i.e.
instanced/spawned effect quads, not emitters. The engine references
`/Assets/pes16/effect/common_effect.fpk` at exactly one site,
`0x14020e690` (`mov $2,%edx; lea "…/common_effect.fpk",%rcx; call 0x1403d2a20;
mov %rax,[0x1437cb628]`) — a static global-initialisation that loads the pack
once and keeps the result in `0x1437cb628` (OBS, xref scan).

---

## 2. Weather

`pes-re-lighting.md` §2 and §5 is the base; this section records what it leaves
open: the literal parameter blocks, the code that selects the weather, and the
snow path.

### 2.1 Everything weather changes (OBS unless marked)

| thing | data | where |
|---|---|---|
| light rig swap | `Asset/model/bg/stNNN/light/#Win/light_stNNN_{af,ar}.fpk(d)` | path built by `0x141ed2f60(out, stadiumId, night, rain)`; `out+0x24 = rain` forced to 0 when `stadiumId == 0x30` (stadium 48 is always fine) |
| scene overlays | `stNNN_{df,dr,nf,nr}.fpk` | suffix chosen by `0x141ebe660(…, night, rain)` with the same two flags. INF (medium): the code only tests `rain != 0`, so **snow loads the rain rig** as well |
| effect config | `Asset/model/bg/stNNN/effect/#Win/effect_stNNN_{df,dr,nf,nr}.fpk` → `effect_config.xml` | the `dr`/`nr` variants add the two rain effects (OBS, diff of st002_dr vs st002_df) |
| G-buffer wetness | `TppRainFilterGBufferMaterial` + `RainFilter` entity | Part B §8 of the lighting report |
| puddle decals | `scenes/center1_rain.fmdl` in `#Win/stNNN.fpk`, `pes3DDC_Puddle_GGX`, `rain_{bsm_alp,msk_alp,nrm,srm}` | 44 stadiums ship the textures |
| kit/skin wet | material param `Wet` | not observed in a live fine-weather session |
| screen rain | `MultiUvScrollScreen` | §2.2 below |
| rain particles | `RainDemo` | §2.2 below |
| lens drops | `Draw2D_CameraLensWaterDrops`, `tex_waterdrop_on_lens_0{1,2}_alp` | Part B §8 |

### 2.2 The literal rain parameter blocks (OBS, `effect_st002_dr.fpk/xml`)

The two rain settings are **verbatim** in the day-rain config of every stadium
that ships them (26 of 61 packs carry `RainDemo` + `MultiUvScrollScreen`).

```xml
<setting type="MultiUvScrollScreen" index="0">
  <param id="texturePath" type="path" index="0" value="…/tex_rain_screen_00.ftex" />
  <param id="texturePath" type="path" index="1" value="…/tex_rain_screen_00.ftex" />
  <param id="speed_u" type="float" index="0" value="-0.02" />
  <param id="speed_v" type="float" index="0" value="-2.00" />
  <param id="speed_u" type="float" index="1" value="-0.12" />
  <param id="speed_v" type="float" index="1" value="-3.5" />
  <param id="wrap_rate_u" type="float" index="0" value="4.0" />
  <param id="wrap_rate_v" type="float" index="0" value="3.0" />
  <param id="wrap_rate_u" type="float" index="1" value="2.5" />
  <param id="wrap_rate_v" type="float" index="1" value="2.0" />
  <param id="colors" type="rgba" index="0" value="0xffffff10" />
  … index 1..3 = 0xffffff10, index 4..7 = 0xffffff15
  <param id="dist" type="float" value="5.0" />
</setting>
<create type="MultiUvScrollScreen" setting="0" />

<setting type="RainDemo" index="0">
  <param id="texturePath" type="path" value="…/tex_rain_particle_00.ftex" />
  <param id="num_unit"   type="uint"  value="200" />
  <param id="fov"        type="float" value="45" />
  <param id="width"      type="float" value="1.8" />
  <param id="height"     type="float" value="0.5" />
  <param id="fade_limit" type="float" value="1.0" />
  <param id="fade_speed" type="uint"  value="20" />
  <param id="near_zone_speed"   type="float" value="84.0" />
  <param id="near_zone_dist"    type="float" value="0.7" />
  <param id="near_zone_area_x"  type="float" value="30.0" />
  <param id="near_zone_area_z"  type="float" value="5.0" />
  <param id="near_zone_colors"  type="rgba" index="0..3" value="0xffffff18" />
  <param id="far_zone_speed"    type="float" value="42.0" />
  <param id="far_zone_dist"     type="float" value="1.1" />
  <param id="far_zone_area_x"   type="float" value="30.0" />
  <param id="far_zone_area_z"   type="float" value="5.0" />
  <param id="far_zone_colors"   type="rgba"  index="0..3" value="0xffffff22" />
</setting>
<create type="RainDemo" setting="0" />
```

Implementation of `MultiUvScrollScreen` (OBS `Primitive_ScreenMultiUV`, Part B §8):
two full-screen quads at `dist = 5 m` in front of the camera, each sampling
`tex_rain_screen_00` twice with a UV scroll of `(speed_u, speed_v)` and a repeat
of `(wrap_rate_u, wrap_rate_v)`; alpha from the `colors[i]` alpha byte
(16/255 and 21/255). Shader: `Primitive_ScreenMultiUV`.

Implementation of `RainDemo` (INF, medium; the class is `EffectRainDemo` +
`ParticleBase`-style pool): `num_unit = 200` streak quads of `width × height` m,
distributed in a camera-relative box `area_x × area_z` (30 × 5 m) at
`near_zone_dist`/`far_zone_dist` m, falling at `near_zone_speed`/`far_zone_speed`
m/s, recycled at `fade_speed` and faded by `fade_limit`. Godot: `GPUParticles3D`
parented to the camera, or a shader quad.

### 2.3 Snow (closing the lighting report's open item #2 as far as the data goes)

OBS:
- No `_s` (snow) light-rig variant exists; `0x141ed2f60` only builds `_af`/`_ar`.
- No snow shader: `FxShaders.fsop`, `GrModelShaders.fsop`, `GrSystemShaders.fsop`
  and `PesShaders.fsop` contain no `*Snow*` entry (checked against the extracted
  shader-name lists).
- Snow textures that do exist: `tex_snow_screen_00.ftex`,
  `tex_snow01_alp.ftex` (exe strings at `0x142a9a0f0` / `0x142a9a290`), and the
  21 stadium `cm000_snow001_{bsm_alp,nrm,srm,lbm,mtl}` /
  `stNNN_snow001_*` texture sets.
- The engine tags scene objects whose **name** contains `_ForSnow`:
  `0x1419b1730` sets `obj+0x50` when the name matches.

INF (medium): snow = the `_ar` (rain) rig + `_nr` overlays, a
`MultiUvScrollScreen`-style screen layer using the snow textures, and the
`_ForSnow` scene models shown instead of their non-snow counterparts. **Not
verified live** (the earlier session ran in fine weather; the game is not running
now).

### 2.4 Weather selection

OBS (strings in the exe): the enum names
`WEATHER_TYPE_{NONE,FINE,RAIN,SNOW,RANDOM}` (string block around `0x1425…`) and
`PLAY_DATA_CHANGE_WEATHER_{NONE,RAIN_AFTER,RAIN_ALWAYS}`, plus the
fox2/scene keywords `fixed_timezone`, `fixed_weather`, `random_weather`.
`pes-re-lighting.md` §2.2 records the two path-builder functions
(`0x141ed2f60`, `0x141ebe660`). **Open:** which code reads the weather enum and
which decides `rain = (weather != FINE)` — the two builders take a boolean, the
caller was not traced.

---

## 3. Post chain and screen effects

`pes-re-lighting.md` Part C owns this. It is complete for bloom, tonemap, the
grading LUT, AA, sharpen and grain, and it kills the vignette hypothesis. The
only items this report adds are the two screen-effect parameter blocks and the
list of the always-on pass graph.

### 3.1 The render/post plugin graph (OBS, `Fox/Scripts/Gr/gr_init_dx11.lua`)

Read in full from the VFS (17,128 B). The order is the registration priority:

```
MODEL_SETUP(1)  PRECOMPUTE_SKY(7)  CANVAS_RENDERER(8)  CROWD_IMPOSTOR(8)
PITCH_EFFECT(8)
DEFERRED(10)
  GEOMETRY_PASS(10)  CROWD_DRAW_DEPTH(4)  DEPTH_PREPASS(5)  PLANAR_SHADOW(6)
    CROWD_PASS(7)  OPAQUE_PASS(8)  MASK_PASS(10)
    PRIMITIVES_DEFERRED(27)   ← all particle primitives land here
    DECAL_PASS(30)  PES_SHADOW(135)
SHRINK_GBUFFER(12)  AMBIENTOBSCURANCE_SSAO(18, isActive=false)
SHADING_PASS(20)  ENLIGHTEN(4)  REFLECTION_PROBE(5)  LOCALREFLECTION_TRACE(6)
  PLUGIN_SPHERICALHARMONICS(10)  PLUGIN_LIGHTOCCLUDER(15)  LOCAL_LIGHTS(20)
  GLOBAL_LIGHTS(25)  PLANAR_SHADOW_COMBINER(27)  SUN_SHADOW(30)  SUNLIGHT(40)
  …transparencies, volumetrics…
PRIMITIVES(30, shrinkLevel=0, shrinkFilter=BILINEAL)
PRIMITIVES_POST_PASS(31)
… AA (priority < OVERLAY < PRIMITIVES_UNFILTERED), PRIMITIVES_UNFILTERED(950)
… tonemap/colourcorrection/sharpen/grain (PluginTonemap chain, Part C)
PRIMITIVES_CANNOT_REPROJECT_MASK(890)  PRIMITIVE_DEBUG(3100)
```

### 3.2 Screen effects that are event-driven, not always-on

`Vignetting` (`filter/filter.ftex`, `alpha = 0x80`) and
`BloodDropsOnCameraLens` are `rd::cobra` event effects registered in the exe's
RTTI list at `0x142a9a0e0` next to RainScreen, LensFlare, Confetti, SmokeBomb,
Halo, SmokeScreen, Firework, ToneAndMannerBackground, LensDistortion. The
lighting report killed the "global post vignette" hypothesis — `Primitive_Vignetting`
is a vertex-coloured primitive (`o.rgb = 0`, alpha-blended) used by the
`EffectVignetting` event effect. Same for `LensDistortion`
(`distortionColorScale`, `lensDistortion`, `lensCubicDistortion` params).

### 3.3 Motion blur / DoF

- `DemoPlayEnvData` carries `m_motionBlur`, `m_shadowMap`, `m_mobShadow`,
  `m_antiAliasing`, `m_vignetting`, `m_haloType`, `m_starFilter`, `m_lensFlare`,
  `m_rainType`, `m_farGrass` (OBS, `0x142ba8ee0…0x142ba9088`), i.e. the demo
  layer can turn each of them on/off per cut.
- No depth-of-field class or shader exists in the image (OBS: RTTI census +
  shader-name lists). **DoF is not implemented in PES 2021.**

---

## 4. Where the values live at run time

### 4.1 VFS paths (OBS, `pesfmt_dump ls`)

| path | size | content |
|---|---|---|
| `Asset/effect/#Win/common_effect.fpk` | 14,048 | `vfx_class_db.vfxdb` + the 5 effect models (§1.7) |
| `Asset/effect/#Win/common_effect.fpkd` | 26,080 | its directory |
| `Asset/effect/sourceimages/#windx11/*.ftex` | 22 files | `effect_{3d_rain_bsm_alp,breath_alp,cam_rain01_alp,leaf,leaf_bsm_alp,leaf_nrm,leaf_srm,leaf_trm,splash_leve0..3_nomip}.ftex`, `env_{hgt,nf_clp}.ftex`, `net_splash.ftex` |
| `Asset/effect/vfx_model/fxm_sky/sky_dome/Scenes/#Win/sky_dome.fmdl` | 23,234 | the sky dome (see lighting report) |
| `Asset/model/bg/common/effect/#Win/effect_demo.fpk` | 55,344 | **`effect_config.xml` (55,207 B)** — the cutscene emitter set |
| `Asset/model/bg/common/effect/texture/#windx11/*.ftex` | 20 files | `tex_firework_{00,03}`, `tex_flash_00`, `tex_ghost_00..06`, `tex_halo_{D,N,S}*`, `circle`, `sample_sky`, `kickoff_gas`, `fx_wtrdrp*`, `fx_wtrrin*`, `tex_star_*` |
| `Asset/model/bg/common/effect/texture/#windx11/tex_rain_screen_00.ftex` | — | screen-rain layer |
| `Asset/model/bg/common/effect/texture/#windx11/tex_rain_particle_00.ftex` | — | rain streaks |
| `Asset/model/bg/common/effect/texture/#windx11/tex_snow_screen_00.ftex` | — | snow screen layer |
| `Asset/model/bg/common/effect/texture/#windx11/tex_snow01_alp.ftex` | — | snow particles |
| `Asset/model/bg/common/effect/texture/#windx11/tex_waterdrop_on_lens_0{1,2}_alp.ftex` | — | lens drops |
| `Asset/model/bg/common/effect/texture/#windx11/filter/filter.ftex` | — | vignette |
| `Asset/model/bg/stNNN/effect/#Win/effect_stNNN_{df,dr,nf,nr}.fpk` | 162 packs | `effect_config.xml` + `locator/locstar_*.model` (star billboards) |
| `Asset/model/effect/#Win/points_lines_triangles_001.fpk` | 320 | an **empty** `<setting type="PointsLinesTriangles">` (debug primitive) |
| `common/effect/texture/#windx11/tex_rain_screen_00.ftex` etc. | — | the un-prefixed mirror copy |
| `shaders/dx11/FxShaders_dx11.fsop` | 815,539 | the particle primitive shaders |

The `df/dr/nf/nr` suffix is **(d)ay/(n)ight × (f)ine/(r)ain** and is chosen from
the same `(night, rain)` pair that picks `light_stNNN_a{f|r}` (OBS,
`0x141ebe660`).

### 4.2 What does *not* exist

- No `.vfx`, `.vfxbin`, `.vfxlf`, `.fxp` file in the install (full scan).
  `fx@fox` is compiled in but unused.
- No `common/effect/...` directory (the path prefix `/Assets/pes16/effect/…` is
  what the exe's `0x14020e690` builds; the VFS path is `Asset/effect/…`).
- `common/match/constant/constant_match.bin` (WESYS container, magic
  `\xff\x10QWESYS` at +4, zlib from +0x10) contains **27 objects**, none of them
  an effect: `animeAgingDribble, animeAgingFreeMove, animeAgingKick,
  animeAgingTrap, ball, cameraInplay, cameraSetplay, command, cpuLevel, cursor,
  feintCommand, injury, inplayDemo, mlScreenShot, modeMatchup, pes15Test,
  pesSmart, rating, setplayGuideCommon, setplayGuide{CornerKick,FreeKickFar,
  FreeKickMiddle,FreeKickNear,GoalKick}, teamEmotion, throwin,
  userPlayTendencyTest` (OBS, parsed with the repo's
  `pes::ParseUniformTable`). There is **no effect/particle constant object**.

### 4.3 Image constants an implementation reads

| value | address | meaning |
|---|---|---|
| death threshold 0.001f | `0x1425a9668` | `age·m < 0.001` kills the particle (§1.5) |
| fade numerator 2.0f | `0x142565604` | `mb[+4] = (2 − age)·k` |
| second channel 0.90196079f (= 230/255) | `0x1429eb208` | `mb[+8] = age · 0.90196079` |
| sign mask (negate) | `0x1425656d0` | corner UV signs in the quad builder |
| smoke vertex colour `0xff000aff` | inlined at `0x141ab94f5` | orange RGBA packed |
| xorshift32 state | emitter +0x10 | the per-particle 0/1 vertex-order pick |

### 4.4 GodotFootball-side VFS keys

Nothing new to mount: `Asset/effect/#Win/common_effect.fpk`,
`Asset/model/bg/common/effect/#Win/effect_demo.fpk` and the per-stadium
`Asset/model/bg/stNNN/effect/#Win/effect_stNNN_*.fpk` all resolve through the
existing `PesVfs` (they are ordinary `Data/*.cpk` members) — OBS, the paths
above were produced by `pesfmt_dump ls` against `PES21_ROOT`.

---

## 5. Per-effect-kind implementation statement

For each effect: **source** (file), **trigger**, **emitter parameters**,
**update rule**, **lifetime**.

### 5.1 Stadium light effects — `Halo`, `LightBillboard`, `LensFlare`

- **Source** `Asset/model/bg/stNNN/effect/#Win/effect_stNNN_{df,dr,nf,nr}.fpk`
  → `effect_config.xml`; textures `tex_halo_{D,N,S}*`, `tex_ghost_*`, `circle`,
  `tex_star_*`; locator models `locstar_*.model` in the same pack.
- **Trigger** `<create type=… setting=…>` — built **once with the stadium**, no
  runtime event. Counts (OBS, per stadium, max over the 4 variants): st010 has 59
  `Halo` + 4 `LightBillboard` + 4 `LensFlare`; st002 has 2 `LensFlare`,
  1 `Vignetting`, 1 `Sample` and (in the rain variants) 2 `LightBillboard`,
  1 `MultiUvScrollScreen`, 1 `RainDemo`; 8 of the 13 packed stadiums have no
  `Halo` at all.
- **Emitter parameters** (`Halo`, OBS verbatim, st010 setting 0):

  ```xml
  <param id="texturePath" type="path"   value="…/tex_halo_S00.ftex" />
  <param id="pivot"       type="vector3" value="0, 0, 0" />            <!-- m -->
  <param id="scale"       type="vector3" value="-9.76, -118.58, -11.73" /> <!-- m -->
  <param id="quat"        type="vector4" value="0.187, 0.585, 0.771, -0.170" />
  <param id="trans"       type="vector3" value="-106.92, 63.71, -130.56" /> <!-- m -->
  <param id="color"       type="vector4" value="1, 1, 1, 0.125490" />   <!-- alpha -->
  <param id="fixRotY"     type="uint"    value="1" />
  <param id="alphaAngle"  type="angle"   value="0.0" />
  <param id="alphaFalloff" type="float"  value="0.0" />
  ```

  `LightBillboard` adds `locatorPath` (a `locstar_*.model` locator),
  `scaleAtt`, `scalePhi`, `scaleTheta`, `scaleFalloff`, `scaleMin`,
  `scaleMax`, `alphaAtt`, `alphaPhi`, `alphaTheta`, `alphaMin`, `alphaMax`
  (the angular size/attenuation curve) and `rot`. `LensFlare` adds
  `intensity`, `innerAngle`, `outerAngle`, `falloff`, `layerNum` (14–16 layers)
  and per-layer `layerPosition`, `layerScale`, `layerColor`, `layerUV0..3`,
  `layerRotation`, `layerAutoRotation` (see the 16-layer st002 block quoted in
  `pes-re-lighting.md`).
- **Update rule**: camera-facing billboard at a fixed world position;
  `LensFlare` = `layerNum` stacked quads along the light→eye axis with per-layer
  UV rects and an additive blend.
- **Lifetime**: permanent (destroyed with the stadium).

### 5.2 `Vignetting`

- **Source** the same pack; texture `…/effect/texture/filter/filter.ftex`.
- **Trigger** `<create type="Vignetting" setting="0">`.
- **Params** `texturePath`, `alpha` (uint, 0x80 = 50 %).
- **Update rule** screen-space quad, `Primitive_Vignetting`
  (`o.rgb = 0`, alpha-blend) — darkens the edges.
- **Lifetime** per the `<create>` block (stadium lifetime; demo env can toggle
  `m_vignetting`).

### 5.3 Rain screen layer — `MultiUvScrollScreen`

See §2.2 for the literal block. Source: `effect_stNNN_{d,n}r.fpk`.
**Trigger**: presence of the setting in the rain overlay.
**Emitter**: 2 full-screen quads.
**Update**: `uv += (speed_u, speed_v)·t` with `fract(uv·wrap_rate_{u,v})`.
**Lifetime**: while the rain overlay is loaded.

### 5.4 Rain streaks — `RainDemo`

See §2.2. Source: `effect_stNNN_{d,n}r.fpk`.
**Trigger**: presence of the setting.
**Emitter**: `num_unit = 200` quads, `width × height` = 1.8 × 0.5 m, in a
30 × 5 m camera-relative box.
**Update**: `y -= speed·dt`, wrap at the box bounds; near zone 84 m/s at 0.7 m,
far zone 42 m/s at 1.1 m.
**Lifetime**: recycled; `fade_speed = 20` fades them in/out at the recycle
boundary with `fade_limit = 1.0`.

### 5.5 The cutscene emitters — `SmokeBomb`, `SmokeBomb2`, `SmokeJet`, `PaperJet`, `PaperJetCamera`, `Firework`, `Confetti`

- **Source** `Asset/model/bg/common/effect/#Win/effect_demo.fpk` →
  `effect_config.xml`; textures under
  `Asset/model/bg/common/effect/texture/{smoke_bomb,smoke_jet,tex_smoke_00,tex_firework_*,tex_flash_00,circle,kickoff_gas}`.
- **Trigger** a `.fdc` cut file's type-3 Effect record (`DemoPlayEffectData`,
  188 B) at `m_startTime`, keyed by the `DemoPlayEffectData_Type` enum in §1.2.
- **Emitter parameters**: the full §1.4 table, per stage index. **Verbatim
  worked example** — `Firework` setting 0 (a 4-stage chain):

  | stage | name | life (s) | life_range | speed (m/s) | drag | scale (m) | spread (deg) | on-die |
  |---|---|---|---|---|---|---|---|---|
  | 0 | 種 seed | 1.2 | 0.3 | 90 | 0.4 | 0.0 | spread_range 12 | 1 → flash |
  | 1 | キラキラ sparkle | 0.75 | 0.15 | 0 | – | 1.25 ±1.0 | rotate_range 90 | – |
  | 2 | 閃光 flash | 0.1 | 0.1 | 0.1 | – | 15 +10 | – | 25 × stage 3 |
  | 3 | 炸裂 burst | 0.75 | 0.25 | 7 | 0.1 | 1.10 ±0.5 | 45 +135 | 1 × stage 1 |

  plus `base_emit_interval = 0.05 s`, `base_emit_num = 1`,
  `particle_num = 900`, and per-stage sprite animation (`sprite_num_w/h` 2×2 or
  4×4, `sprite`, `sprite_offset`, `sprite_interval`) and 4-key opacity/colour
  curves in normalised life time.
- **Update rule**: §1.5 verbatim (`p ← p + v`, `v ← v·drag`, `age ← age·m`,
  death at 0.001, `alpha = age·230/255`).
- **Lifetime**: `life ± life_range` seconds, or infinite while the cut plays for
  the per-stage `initial_time`.
- **Confetti** is the degenerate case (OBS, `effect_demo.xml`):
  `emitSync = 1, emitNum = 1, particleNum = 1600` and nothing else — a single
  burst of 1,600 paper quads with default-scale physics.

### 5.6 World-space effect meshes (`pitch_ground`, `pitch_leaf`, `net_splash`, `3d_rain`, `effect_rainModel`)

- **Source** `Asset/effect/#Win/common_effect.fpk`.
- **Trigger**: selected by name through `vfx_class_db.vfxdb` (the exact lookup
  path is **open**, §6). INF (high): `pitch_ground`/`pitch_leaf` are spawned at
  a ball impact position on the pitch, `net_splash` at a ball/net contact,
  `3d_rain`/`effect_rainModel` belong to the rain weather.
- **Emitter parameters**: none in the emitters' sense — the mesh is a fixed
  12/8-vertex quad with a `fox3DDF_Blin_Translucent{,_NC}_LNM` material
  (`effect_leaf_{bsm_alp,nrm,srm,trm}`, `MatParamIndex_0 = 43`) or
  `fox3DFW_ConstantSRGB` with `SelfColorIntensity = 5000`
  (`effect_rainModel`) or `fox3DFW_ParallaxReflection` with `HeightScale = 0.2`
  (`3d_rain`).
- **Update rule**: none (static mesh), or the shader's own scroll
  (`Primitive_ScreenMultiUV` / parallax).
- **Lifetime**: one-shot per spawn; the puddle decals in
  `scenes/center1_rain.fmdl` scroll two normal maps by `PuddleCurTime` (lighting
  report Part B §8).

### 5.7 Wetness and puddles (material-level, not particles)

- **Wet G-buffer** `TppRainFilterGBufferMaterial` + `RainFilter` entity:
  `albedo *= k`, roughness compressed toward the wet value, masked by an
  overhead roof-occlusion capture (OBS lighting report Part B §8). Godot: a
  per-material `wetness` uniform.
- **Puddles** `scenes/center1_rain.fmdl`, technique `pes3DDC_Puddle_GGX`,
  textures `rain_{bsm_alp,msk_alp,nrm,srm}`, two scrolling normal maps ×
  `PuddleCurTime` with `PuddleSpeed1U/V`, `PuddleSpeed2U/V`. Godot: a decal
  material.
- **Kit/skin wet**: material param `Wet` (shirt `alb *= 1 − 0.1·Wet`, roughness
  × `(1 − 0.15·Wet)`); not observed live.

---

## 6. Open items (and what was tried)

1. **The grass/turf spray on a slide or a strong kick, the ball-strike flash and
   the impact puff are not shipped effects.** §1.6 lists the five searches that
   establish this. What PES 2021 shows at those moments is: the turf3d mesh
   blend, the `pitch_ground` / `pitch_leaf` quads (§1.7), the `net_splash` quad,
   and the G-buffer wetness. A 1:1 port must therefore build **its own** emitter
   using the §1.4 parameter set and the §1.5 update rule — which is possible with
   shipped data and no new format work. Not tried: reading the 4cc/modders'
   community effect packs (the install has none).
2. **`vfx_class_db.vfxdb` decoding.** 2,220 B, magic `vfdb` at +0, then what
   looks like 0x20-byte records of `{u16, u16, u32…}` with many repeated
   4-byte hashes. Tried: hexdump and a manual record split; the model names
   themselves are *not* in the file (they are the fpk member names), so it must
   be a name-hash → parameter/record map. Not decoded.
3. **`m_type` → `<setting type>` mapping is INF**, not OBS. Tried: looking for a
   string switch; the setting classes are loaded by name from the XML
   (`effect_config.xml` is opened by the cfg layer), and no name table for the
   setting types was found in the exe. Confidence is high because the class set
   and the enum set coincide exactly.
4. **`emitSync` unit (frames vs seconds) is INF.** `SmokeBomb` uses 9 / 75 / 59
   while `SmokeBomb2` uses `emit_interval` in seconds (0.3333, 2.105, 1.25,
   0.33); the two names coexist in the same string table, and the new-generation
   `base_emit_interval` is clearly seconds (0.05). Tried: looking for a
   `60`-multiplier near the setting parse; the parse is hash-driven through the
   globals registered at `0x140210920…0x1402127xx`, so the multiply is not
   visible at the string site.
5. **The quad's size.** The 4 vertices all carry the particle *centre*; the
   sprite size comes from elsewhere (shader / `m_world` scale). Tried: reading
   `Prim_Poly_LitDP3_NS.vs.dxbc` RDEF — the VS has `m_world` / `m_viewWorld` /
   `m_localParam`, so the quad is expanded on the GPU. The scale source is not
   pinned.
6. **Snow** stays open (§2.3) for the same reasons the lighting report gives: no
   snow rig, no snow shader, and the live session was fine weather. The game is
   not running now, so nothing new could be observed.
7. **Weather-selection caller** not traced (§2.4).
8. **`common_effect.fpk`'s loader site** (`0x14020e690`) is a static initialiser;
   the *user* of the loaded object (`0x1437cb628`) was not traced. Tried: one
   xref scan for the address `0x1437cb628` (no code reads it statically; it is
   reached through the object's own vtable).

---

## 7. Method

- Sections: `objdump -h` on `$PES21_ROOT/PES2021.exe`; file offset = VA −
  `0x140000A00` for `.trace`/`.rdata`/`.data` (verified: string at file offset
  44453624 = VA `0x142a658f8` reads `Tpp Effect DirtyModel`).
- String search: `strscan.py` over the whole image; xrefs by brute-force scan of
  `.trace` for `RIP-relative` displacements that resolve to the target.
- Vtables: RTTI type-descriptor walk in `<scratch>/pesmem.bin` (project `vt.py`),
  then `objdump -D -b binary -m i386:x86-64 --adjust-vma=<VA>` on bytes lifted
  from `<scratch>/pesmem.bin`.
- Data: `build/apps/pesfmt_dump {ls,cat}` against `PES21_ROOT`; `.fpk` members
  with GF's `tools/pes21_import/fpk.py`; `.fmdl` with this repo's
  `src/pes/fmdl.cpp`; WESYS with `src/pes/uniform.cpp`.
- All scratch under `<scratch>/EffectsRE/` (< 9 MB). Nothing was written to the repo
  or to the game's memory; the game process was confirmed absent before any
  work.

---

## 8. Corrections to earlier reports

None that contradict a prior report. Two additions:
- `pes-re-lighting.md` §5 lists `RainDemo`'s `fade_limit`/`fade_speed` and
  `num_unit`; the **complete** block including `fov`, `width`, `height` and both
  zone areas is in §2.2 here.
- `pes-re-lighting.md` §4.3 says "st002 night-fine: 70 `Halo`, 4
  `LightBillboard`, 4 `LensFlare`, 1 `Vignetting`; day-rain adds
  `MultiUvScrollScreen` and `RainDemo`". Re-checked on all four st002 packs (OBS
  `<setting type=…>` census): st002 has **no** `Halo` in any variant. Its real
  counts are 2 `LensFlare`, 1 `Vignetting`, 1 `Sample` (the commented template)
  in the fine variants, plus 2 `LightBillboard` + `MultiUvScrollScreen` +
  `RainDemo` in the two rain variants. The 70-halo figure belongs to the
  stadiums that do ship halos: max per stadium over the 13 packed stadiums is
  st010 = 59, st061 = 35, st034/st020 = 12, st054/st029 = 4, and 8 of the 13
  have none. Halos are floodlight glow billboards and belong to the floodlit
  stadiums, not to st002.

