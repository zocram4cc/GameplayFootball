# PES 2021 — match lighting, sky, weather and post-processing (RE report)

Scope: how PES 2021 lights a match. Covers the stadium light rig and time of day, the sun, sky, ambient and reflection probes, shadows, weather (rain, snow, fog) and the HDR→LDR post chain. For each part the report gives the PES data source, the formula, and a mapping onto Godot 4 `Environment` / `WorldEnvironment` / lights, or says that a compositor pass is needed. Character material shading (G-buffer, BRDF, sun lighting) is in `pes-re-shaders.md`. Stadium assembly and nested-cpk mounting are in `pes-re-stadium-camera.md` §1.

Tags:
- **OBS** = observed, with the method given.
- **INF** = inferred, with a confidence level.

VAs are those of the live image (base 0x140000000; dump `pesmem.bin`). The live process was only read through `/proc/<pid>/mem`: nothing was written and no input was sent. No names from the user's save or player tables appear here.

Scripts are in `<scratch>/LightingRE/`:

| Script | What it does |
|---|---|
| `ex.py` | extract members of a nested `Asset/model/bg/*.cpk` inside `Data/dt30_g4.cpk` |
| `tods.py` | table of every `TimeOfDaySettings` in a fox2 dump |
| `lights.py` | light hierarchy summary |
| `survey.py` | all vanilla rigs |
| `fitrise.py` | collects the atmosphere params of all rigs |
| `solar.py` | NOAA sun position plus the fitted PES sun model `pes_sun_dir` |
| `vtfind.py` | RTTI name → vtable |
| `lscan*.py`, `sscan.py`, `fscan.py` | live heap scans |
| `samp_sun.py`, `sunlog.py` | live sun sampling |

Two sections were written by sub-agents and are appended below (their scratch dirs are `PostChainRE/`, `SkyFogRE/`):
- Part B = sky, ambient, fog, weather shaders and the rig binary files.
- Part C = the post chain.

---

## 0. Headline facts

1. **The whole look is data-driven per stadium by one fox2 light rig**, `Asset/model/bg/stNNN/light/#Win/light_stNNN_{af|ar}.fpk(d)`. `af` = fine weather, `ar` = rain (and snow, INF medium, §2.2). The rig holds:
   - a `TimeOfDay` with `TimeSpan`s, each pointing to a `TimeOfDaySettings` (exposure, tonemap, colour grading, fog, sun mode);
   - `TppAtmosphere` (Bruneton sky coefficients, latitude/longitude/date, sun lux, shadow ranges);
   - `TppSky` (cloud dome);
   - `PesPluginSettings` (bloom, adaptation speed);
   - `GrShadowSettings`;
   - `ColorGrading` (four LUT paths per mood);
   - the floodlight `SpotLight`/`PointLight` set, switched per span by `TimeOfDayObjectProxy`;
   - `ReflectionProbe`s with baked cubemaps (`.rpd`), sky SH per time of day (`.atsh`), and a sky inscatter slice (`.pcsp`).
2. **The match picks one span** by suffix:
   - `_day` / `_night` (both seasons), or `_day_s`, `_day_w`, `_night_s`, `_night_w`;
   - keyed by time-zone setting (day/night) × season (summer/winter) (§2.1, OBS code).
3. **Sun direction is astronomical** (latitude, longitude, date, span hour:minute, GMT offset, northAngle), but with PES's sign conventions. The fitted model reproduces the live sun vector to 0.25° and 108 authored rise/fall vectors to a mean of 0.6° (§3).
4. **Post (Part C):**
   - The scene is **pre-exposed** (`×2^EV`).
   - Bloom is Kawase with a hard threshold.
   - The tonemap is **UE4 FilmToneMap** using the rig's `acesFilm*` parameters, followed by a white-balance matrix and `pow(1/2.2)`.
   - Grading uses a 16³ LUT strip in the gamma-2.2 domain.
   - Then FXAA or TAA, then LumaSharpen + grain.
5. **Rain:**
   - a darker/glossier G-buffer filter;
   - a scrolling screen-rain layer (`MultiUvScrollScreen`) and `RainDemo` particle quads, configured per stadium in `effect_config.xml`;
   - puddle decals (`center1_rain.fmdl`, `pes3DDC_Puddle_GGX`);
   - cloudier sky tables (`cloudiness`) and a `cloudy` grading LUT.

   There are no snow shaders. Snow is textures only (`tex_snow_screen_00`, `tex_snow01_alp`, stadium `cm000_snow001_*`).

---

## 1. Files

### 1.1 Per stadium (OBS, nested cpk `Asset/model/bg/stNNN.cpk` inside `dt30_g4.cpk`; see the stadium report §1.1 for mounting)

| File | Content |
|---|---|
| `light/#Win/light_stNNN_af.fpkd` | fox2 rig, fine weather. Present for 54 vanilla stadiums (`rig_inventory.txt`) |
| `light/#Win/light_stNNN_ar.fpkd` | fox2 rig, rain. 48 stadiums. Stadium 48 is forced to `af` by code (§2.2) |
| `light/#Win/light_stNNN_a{f,r}.fpk` | `light_stNNN_a?.atsh` (5136 B, sky SH9 × 40 times × 2 weathers), `.pcsp` (65540 B, inscatter slice), `.rpd` (reflection-probe BC6H cubemaps: af 63 entries = 7 probes × 9 states; ar 14 = 7 × 2), `/Assets/pes16/effect/vfx_model/fxm_sky/sky_dome/Scenes/sky_dome.fmdl`. Layouts: Part B §9 |
| `#Win/stNNN_{df,dr,nf,nr}.fpk/.fpkd` | weather/time model overlays. st002: only `left3_dr`/`right3_dr` scoreboard meshes (`fox3DDF_GGX`) |
| `#Win/stNNN.fpk` → `scenes/center1_rain.fmdl` | puddle decal mesh in the **base** stadium pack: technique `pes3DDC_Puddle_GGX`, params `PuddleCurTime`, `PuddleSpeed1U/V`, `PuddleSpeed2U/V`; textures `sourceimages/tga/rain_{bsm_alp,msk_alp,nrm,srm}` (44 stadiums ship them) |
| `effect/#Win/effect_stNNN_{df,dr,nf,nr}.fpk` | `effect_config.xml` + star/flare locator models (§4.2) |
| `#Win/stNNN.fpkd` | `EnlightenSystemContainer`, `EnlightenProbeSet` (precomputed GI, `EnlightenOutput/*.sse`; not decoded) |
| `sourceimages/tga/#windx11/lut_s_*.ftex` | per-stadium SDR LUT copies (st002, 027, 028, 033, 052). Not referenced by st002's rig, which points to `common/lut` (OBS fox2) |
| `sourceimages/tga/#windx11/stNNN{df,dr,nf,nr}_{D,G}_FILTERLUT.ftex`, `lut_Match.ftex` | st028, 048, 065, 069, 070. Consumer not traced. INF: D/G = demo/game filter LUT |

### 1.2 Common (OBS, `Asset/model/bg/common.cpk`)

| Path | Content |
|---|---|
| `common/lut/#windx11/lut_{s,h}_{day,evening,night,cloudy}_{game,demo}.ftex` (+ `lut_sepia_*`) | 33×33×33 RGBA16F volumes, pf 12, 1 mip (OBS ftex header). Layout `L[b][g][r]`. `lut_s_day_game` and `lut_h_day_game` are byte-identical (OBS md5). Grey axis 0.0208 → 0.616 (mid) → 0.689. Domain unresolved (Part C §4) |
| `common/effect/texture/#windx11/` | `tex_rain_screen_00/02`, `tex_rain_particle_00`, `tex_snow_screen_00`, `tex_snow01_alp`, `tex_waterdrop_on_lens_0{1,2}_alp`, `fx_wtrdrp*`, `tex_halo_{D,N,S}*`, `tex_star_*`, `tex_ghost_*`, `circle`, `sample_sky` (TppSky cloud texture), `filter/filter` (vignette) |
| `cpk_dat/common/render/model/light/{Default,EditFace,EditSpike,…}.xml` | menu/edit-mode light presets (OBS exe strings @0x142b90128). Not used in matches |

### 1.3 Exe constants used
- Path pieces `/Assets/pes16/model/bg/`, `st%03d`, `/light/light_`, `_af.fpk` / `_ar.fpk` (@0x142b900e8…).
- Overlay suffixes `_df/_dr/_nf/_nr.fpk` (@0x142b8ba68…).
- Span suffixes `_day`, `_night`, `_day_s`, `_day_w`, `_night_s`, `_night_w` (@0x142a709d4…).

---

## 2. Selection: which rig, which span, which overlay

### 2.1 Match settings (OBS exe enum strings)
- `TIME_ZONE_TYPE_{NONE,DAY,NIGHT,RANDOM}`.
- `SEASON_TYPE_{NONE,SUMMER,WINTER,RANDOM}`.
- `WEATHER_TYPE_{NONE,FINE,RAIN,SNOW,RANDOM}`.
- `PLAY_DATA_CHANGE_WEATHER_{NONE,RAIN_AFTER,RAIN_ALWAYS}`.
- Strings `fixed_timezone`, `fixed_weather`, `random_weather`.

### 2.2 Rig and overlay choice (OBS disassembly)
- `0x141ed2f60(out, stadiumId, night, rain)` builds `/Assets/pes16/model/bg/st%03d/light/light_st%03d` + (`rain ? "_ar.fpk" : "_af.fpk"`).
  - `out+0x24 = rain`, forced to 0 when `stadiumId == 0x30` (48). `out+0x20 = night != 0`.
  - Called from `0x141eacdf9` with `{id, night, rain}` = `[obj+0x10, +0x14, +0x18]`.
- `0x141ebe660(…, night=r8, rain=r9)` appends `_df` / `_dr` / `_nf` / `_nr` to the scene overlay with the same two flags.
- INF (medium): `rain` is `weather != FINE`. The code only tests non-zero, so snow also loads the rain rig and overlays. Not observed live.

### 2.3 Span choice: `TimeOfDaySlotMapper` (OBS `0x14191c040` ctor, `0x14191c800` lookup; vtable 0x142a71088)
- **Ctor.** For span *i* in `TimeOfDay.timeSpans` order:
  - push `startHour·60 + startMinute` into a vector;
  - test the span name's suffix (match function 0x1403d33c0; INF ends-with, since `_day` and `_day_s` are tested separately):

  | Suffix | Slots set to *i* |
  |---|---|
  | `_day` | `daySummer` (+0x28) and `dayWinter` (+0x2c) |
  | `_night` | `nightSummer` (+0x30) and `nightWinter` (+0x34) |
  | `_day_s` / `_day_w` / `_night_s` / `_night_w` | the one matching slot |

  `this+8 = timeAdvanceMode`.
- **Lookup(req, ctx).** `req+0` = kind:
  - **0 day:** `req+4` season 0 → `daySummer`, 1 → `dayWinter`.
  - **1 night:** season 0 → `nightSummer`, 1 → `nightWinter`. **Then +1 (the next span) if `ctx+0x18 > 2`.** INF (medium): `ctx+0x18` is the match phase, so a night match moves to the next, darker span later in the match.
  - **2 explicit time:** `req+0xe` hour, `req+0x10` minute. Take the last span with `start ≤ time`. If `timeAdvanceMode == 1` and `ctx+0x18 > 2`, then +1.
  - The index is clamped to `count − 1`.
- **Enums** (OBS strings @0x142a70a28 and @0x142a71150):
  - `TimeAdvanceMode {NONE, PROGRESSIVE, REAL-TIME}`.
  - `TimeOfDaySettings_SunLightMode {ALL, PRECOMPUTE_ONLY, DISABLE, CASCADE_SHADOW_ONLY}`.
- **Live (OBS, st071 4cc rig).** Mapper at heap 0x25f20d40: mode 1; starts [720, 780, …, 1200] (12:00…20:00); slots `(daySummer 3, dayWinter 3, nightSummer 6, nightWinter 7)`. These are spans `1500_day`, `1800_night_s`, `1900_night_w`.
- **Vanilla suffix census (OBS `survey.json`, 100 rigs).**
  - Most `af` rigs have 9 spans 12:00–20:00 with `…_day` at 13–16 h, `…_night_s` at 17–19 h and `…_night_w` at 19–20 h. Some use plain `_night`.
  - `ar` rigs have two spans, `TimeSpanHHMM_day` (12–16 h) and `TimeSpanHHMM_night` (18–20 h).
  - Hours differ per stadium, so they must be read from the file.

### 2.4 Settings override rule (OBS live)
- The global `TimeOfDaySettings` (parent 0) holds defaults. A span's settings replace a group only when its `override<Group>` flag is True.
- Groups: DateTime, SunLightMode, Atmosphere, GameExposure, DemoExposure, Tonemap, ColorGrading, GlobalFog, AtmosphericFog, AmbientObscurance, ModelIntensity, AlphaModelReflections.
- **OBS:** during a day match on the 4cc st071 rig, the spans have `overrideTonemap = False`. The live `PluginTonemap+0x64` held the **global** filmic set (0.88, 0.45, 0.55, 0, 0.02, 6500, …), not the span set (0.88/0.55/0.26/0/0.04).
- `game*` vs `demo*` exposure and the `sdr/hdr × Game/Demo` LUTs: INF (high) game = live play, demo = cutscenes/replays; sdr/hdr = display output mode.

---

## 3. Sun and moon

### 3.1 Inputs
- From `TppAtmosphere`: `latitude`, `longitude`, `gmtTimeDifference`, `year/month/day`, `northAngle`.
- From the active span's `TimeOfDaySettings`: `dateTimeHour/Minute` (when `overrideDateTime`).
- Intensity: `sunLux` (span, else atmosphere) × atmospheric transmittance (`Sky_SunColor`, Part B §2). `moonLux`, `moonColor`.
- Below-horizon fade: about 2° (Part B).
- `sunLightMode` per span:
  - **ALL** = dynamic sun with cascaded shadows;
  - **PRECOMPUTE_ONLY** = sun only through precomputed/Enlighten lighting (no dynamic direct sun);
  - **DISABLE** = no sun: used at night, when the floodlights take over;
  - **CASCADE_SHADOW_ONLY**.

  Enum OBS; behaviour INF (medium).
- `interpolateTimeInSecondOfDirLightSteppedMove` 3 s and `divisonNumOfDirLightSteppedMove` 175: INF, the sun moves in 175 steps per day, each blended over 3 s. No movement was seen during 4 min of play (`sunlog.txt`).

### 3.2 Code (OBS headless decompiles `solar_deca.txt`)
- `0x1419539f0`, Julian centuries. Date is `ushort{year, month, day, hour, minute, second}`:
  - `dayFrac = (((sec/60 + min)/60 + hour) − gmt)/24 + day`;
  - if month < 3, then `year −= 1`, `month += 12`;
  - `JD = 1720996.5 − ⌊Y/100⌋ + ⌊Y/400⌋ + ⌊365.25·Y⌋ + ⌊30.6001·(M+1)⌋ + dayFrac`;
  - `T = (JD − 2451545 [+0.000752315 = ΔT 65 s when flag]) / 36525`.
- `0x141953890` / `0x141953580` (twin `0x14181ac70`):
  - GMST = `4.894961 + 230121.675315·T`; LMST = GMST + lon·π/180;
  - obliquity = `0.409093 − k·T`;
  - sun mean longitude = `4.895048 + 628.331951·T`; mean anomaly = `6.24 + 628.302·T`;
  - equation of centre = `(a − b·T)·sin M + c·sin 2M`;
  - a full lunar series for the moon;
  - rotated by the north matrix (`+0x30`) into world space.
- Live struct (heap, found by its `(lon, lat, ?, north)` floats):
  - `+0x20 = (90, 45, 11, 90)`;
  - date `+0xd0 = (2019, 3, 6, 17, 30)`;
  - to-sun `+0x90 = (0.56439, 0.70551, −0.42862)`;
  - light direction `+0xb0` = negated;
  - lux `+0xa0 = 150000`;
  - engine-computed rising/falling sun/moon directions at `+0x30…+0x60` (y = 0.1374 = sin 7.9°).

  The same light direction is at `SceneAtmosphereParameter+0x110`.

### 3.3 Fitted model (INF high; `solar.py: pes_sun_dir`)
- Evaluate the standard NOAA solar position with:
  - **latitude negated** (φ' = −lat);
  - hour angle **H = lon − GHA☉(UT)** (NOAA uses GHA + lon);
  - UT = local − gmt.
- Then in Fox space (Y up), with the to-sun angle measured from +Z toward +X:
  ```
  a = northAngle − azimuth
  toSun = (cos(el)·sin a, sin(el), cos(el)·cos a)
  lightDir = −toSun
  ```
- Evidence:
  - **Live:** model (0.5672, 0.7051, −0.4256) vs observed (0.56439, 0.70551, −0.42862), 0.25° apart. The rig is 4cc st071: lat 45, lon 90, gmt 1, 2019-03-06, 17:30, north 90.
  - **Authored rising/falling sun vectors** in all 54 vanilla `af` rigs (`fixedRisingSunDir`/`fixedFallingSunDir` at elevation 7.9°): mean 0.62°, max 2.0° (108 vectors).
  - The engine labels the crossing as "rising" where the model's elevation **decreases** with clock time; the hour-angle mirror flips the time direction.
  - The authored set pins φ sign and north convention, independent of time. The live point pins the hour-angle convention.
- **Killed hypotheses:**
  1. "Sun = true astronomical sun for the rig's lat/lon." True sun for (45, 90) at 17:30 is below the horizon; the observed elevation is 44.9°. For st009's Madrid parameters, Mar 6 max elevation is 43.98° < 44.87° observed.
  2. "Sun comes from `TimeOfDaySettings`/`TimeSpan` transforms." All transforms are identity (OBS).
  3. "Sun from the rig's `fixed*Dir` fields." The 4cc file has junk `(0, 0, 90)` there, and the engine recomputes them at runtime (live `+0x30…`).
- **Godot.** `DirectionalLight3D` basis −Z = `lightDir`.
  - `light_intensity_lux = sunLux × T_sun` (with `use_physical_light_units`), or energy = the same × exposure scale.
  - `light_temperature` off; colour from the transmittance or the `.atsh` light colour (Part B §9).
  - `light_angular_distance = 2°` (Fox disk radius 1°).
  - Disable it when `sunLightMode == DISABLE`.

---

## 4. Light rig content (st002 `af` as the worked example; OBS fox2 dump `af.txt`)

### 4.1 TimeOfDaySettings (OBS `tods.py af.txt`; S = global entity)

| Field | S | 12 h | 13 h | 14 h | 15 h `_day` | 16 h | 17 h `night_s` | 18 h | 19 h `night_w` | 20 h |
|---|---|---|---|---|---|---|---|---|---|---|
| sunLightMode | 0 | (0) | (0) | (0) | (0) | (0) | 1 | 2 | 2 | 2 |
| sunLux | 150000 | 150000 | 150000 | 150000 | 150000 | 70000 | 40000 | 2000 | 150000 | 150000 |
| skyLightSunScale | 1 | 0.5 | 0.5 | 0.5 | 0.4 | 0.3 | 0.05 | 0.1 | 1 | 1 |
| gameExposureCompensation | 0 | 0.66 | 0.66 | 0.66 | 0.66 | 0.66 | 0.1 | 0 | 0 | 0 |
| gameMin/MaxExposure (EV) | −14/−7 | −13/−9 | −13/−9 | −13/−9 | −13/−9 | −13/−9 | −10/−7 | −7.3/−7 | −7.3/−7 | −7.3/−7 |
| gameInitialExposureValue | −14 | −11 | −11 | −11 | −11 | −10 | −10 | −7 | −7 | −7 |
| keyValue / maxLuminanceValue | 0.18/32 | (all spans the same) | | | | | | | | |
| acesFilm Slope/Toe/Shoulder/Black/White | 0.88/0.45/0.55/0/0.02 (override on) | 0.88/0.55/0.26/0/0.04 with override off in every span | | | | | | | | |
| whiteTemperature / whiteTint | 6500 / 0.075 | 6500 / 0.05 | | | | | | | | |
| colorGrading | — | day | day | day | day | day | evening | night | night | night |
| globalFog colour / density / heightScale (g = override off → global values apply) | (1,1,1)/0.03/4 | g | g | g | g | g | g | (0.18,0.2,0.3)/0.3/2 | (0.2,0.3,0.5)/0.1/2 | (0.05,0.07,0.1)/0.1/1 |
| atmosphericFog startDepth / rangeNear / rangeFar / ampNear / ampFar (— = override off; 0 = overridden to all-zero) | 40/1000/4000/1/800 | — | — | — | — | 40/1000/4000/1/800 | 40/1000/4000/1/800 | 0 | 0 | 0 |
| objFogRgbAmp / objFogRgbAmpFuncScale | (1,1,1,0.1) / 1000 | — | — | — | — | (1,0.8,0.4,0.2) / 1000 | (1,0.6,0.2,0.2) / 100 | 0 | 0 | 0 |
| modelIntensity Tv/Bill/BillMovie/Ad | 2000/3000/20000/1000 | — | — | — | — | — | 2000/1000/6000/1000 | (same) | (same) | (same) |

- Other rigs differ (`survey.json`):
  - 50 of 100 global entities carry 0.88/0.55/0.26/0/0.04; 49 carry 0.88/0.45/0.55/0/0.02.
  - PesPluginSettings `(bloomSize, brightnessExtraction, weight, tonemapSpeed)` is most often (0.4, 0.4, 0.3, 30); st002 has (0.8, 0.4, 0.3, 30).
  - Always read the values from the file.
- `modelIntensity*` are emissive scales for TV/billboard/ad materials (INF high; also `draw_parameter_{df,…}.xml` `<tv_model_intensity>`).
- `PesPluginSettings.flags` 2055 = 0x807: bits isTonemap, isBloom, isMotionBlur, plus bit 11 (unnamed). INF: bit order = the property order in the exe (@0x142a7de18).
- `GrShadowSettings`:
  - `method` enum {CascadedShadowMaps, PrecomputedVSM, PrecomputedPCSS} (OBS strings) = 1, so **precomputed VSM** for the static stadium;
  - opacity 1, blur radius 2.5, masked opacity 0.85, masked blur 3, depthBias 1e-5, temporalSampleCount 4.
- `TppAtmosphere` shadow fields: `shadowRange` 240 (st009: 120), `shadowRangeExtra` 80, `hiResShadowRange` 35, `shadowProjectionRange` 30, `selfShadowBias` −15. INF: CSM extents for dynamic objects.

### 4.2 Floodlights and stadium lights (OBS `lights.py af.txt`)
- **Hierarchy.** Locators group the lights:
  - `Light_Pitch_{F,B,L,R}`;
  - `Light_Stand/{Light_Stnd/{Upper,Middle}_Group, Light_Roof, Light_Plate/*, LG_F_Room, Light_Tv, Light_Gate, Light_F_Cons}`.

  `TimeOfDayObjectProxy` children of each `TimeSpan` enable groups for that span:

  | Span | Groups enabled |
  |---|---|
  | 12 h | ReflectionProbe only |
  | 13–16 h | + Light_Pitch_F |
  | 17 h | Light_Pitch_F, Light_Pitch_B |
  | 18–20 h | all four pitch groups + Light_Stand |
  | `ar` day and night | all |

- **Transforms.** Child transforms are local to the parent (INF high: room lights have small local translations).
- **Spot axis is local −Y** (OBS: rotated −Y of every pitch spot points at the pitch; `reachPoint` (0, −5, 0)).
- `GlobalLightArray.spotLights` = the 10 pitch spots. INF: the lights applied to characters/forward passes.

| Group | n | lumen | K | outerRange (m) | innerRange | umbraAngle° | penumbra° | attenuationExponent | shadow / specular / lightSize |
|---|---|---|---|---|---|---|---|---|---|
| Pitch F, B (e.g. F_02 at (−50, 34, 60)) | 3 + 3 | 4,000,000 | 7200 | 140 | 1 (F) / 30 (B) | 110 | 0 | 1.2 | no / yes / 0 |
| Pitch L, R | 2 + 2 | 1,000,000 | 7200 | 85 | 40 | 90 | 0 | 1.2 | no / yes / 0 |
| Stand upper | 23 | 1.9e6 ×15, 1.6e5, 1e5, 8e3 | 7200/6500 | 20 | 0.05 | 160 | 0 | 1.2 | no / no / 0.8 |
| Stand middle | 12 | 1e6 / 4e5 / 2e5 | 7200 | 80 | 40 | 140 | 0 | 1.2 | no / yes / 10 |
| Roof | 24 | 2e4–4e4 | 6500 | 20 | 0.05 | 160 | 120 | 1.2 | no / yes / 0.8 |
| Point `PL_Plate*` | 50 | 3000 | 4200 | 3 | 0.05 | — | — | — | no / no |
| Room / gate / console / TV spots | 3–13 each | 3560–10000 | 4000–5200 | 4–20 | — | 160–179.5 | — | 1.5 | — |

- **Godot (INF, the spot shader was not decoded):**
  - `SpotLight3D` with `light_intensity_lumens = lumen` (physical units), `light_temperature = temperature`;
  - `spot_range = outerRange`, `spot_angle = umbraAngle/2`, `spot_attenuation ≈ attenuationExponent`;
  - `light_size = lightSize`, `shadow_enabled = castShadow`, `light_specular = hasSpecular ? 1 : 0`;
  - visibility toggled per span by the proxy lists.
- **`LightOccludeCube`** (19 in st002): box occluders (`occlusionRate` 0.4, inner scales) that dim the lights inside them. Godot: none, approximate by hand.
- **`ReflectionProbe`s:** 7 in st002, with priority, intensity (`RP_F_Stnd_0002` = 4), parallax inner scales and a capture position. Formula and Godot mapping in Part B §6; cubemaps in `.rpd`.

### 4.3 Stadium effects (`effect_config.xml`, OBS `effect_st002_{dr,nf}`)
- `<setting type>` blocks with `<param id type index value>` (types path/float/uint/rgba/angle/vector3), plus `<create type setting floor dir>` instances.
- st002 night-fine: 70 `Halo` (textured light-shaft billboards at the floodlights), 4 `LightBillboard` (star sprites from locator models), 4 `LensFlare` (14 layers), 1 `Vignetting` (`filter.ftex`, alpha 0x80).
- Day-rain adds `MultiUvScrollScreen` and `RainDemo` (§5).
- Exe setting classes (RTTI, `vts.txt`): RainScreen, MultiUvScrollScreen, RainDemo, LightBillboard, LensFlare, Vignetting, AtmosphericFog, SmokeBomb(2), SmokeJet, PaperJet(Camera), Confetti, Halo, SmokeScreen, Firework, BloodDropsOnCameraLens, ToneAndMannerBackground, LensDistortion.

---

## 5. Weather

| Effect | Data | Formula / Godot |
|---|---|---|
| Rig swap | `light_stNNN_ar` + `stNNN_{d,n}r` + `effect_stNNN_{d,n}r` (§2.2) | `ar` changes: `cloudiness` (sky/fog tables blend to cloudy, Part B §1–2), the `cloudy` LUT, TppSky `cloudCover` 0.6 and `textureRepeats` 2, smaller shadow ranges (`shadowRangeExtra` 0, `hiResShadowRange` 2, `selfShadowBias` −8), lower `sunLux` (100000 at 15 h; 20000 at 18 h), all floodlights on |
| Wet G-buffer | `TppRainFilterGBufferMaterial` (+ `RainFilter` entity class) | albedo × k, roughness compressed, masked by an overhead roof-occlusion capture (Part B §8). Godot: per-material wetness uniform |
| Puddles | `scenes/center1_rain.fmdl`, `pes3DDC_Puddle_GGX` | two scrolling normal maps × `PuddleCurTime` (Part B §8). Godot: decal-like material |
| Kit/skin wet | material param `Wet` (shirt: `alb *= 1 − 0.1·Wet`, roughness × (1 − 0.15·Wet); `pes-re-shaders.md` §3.1) | INF: the engine raises `Wet` in rain (`MaterialModifier2`). The live session had 0 (fine weather) |
| Screen rain | `MultiUvScrollScreen` (st002 dr): 2 layers of `tex_rain_screen_00`, `speed_u/v` (−0.02, −2.0) and (−0.12, −3.5) /s, `wrap_rate` (4, 3) and (2.5, 2), colours `0xffffff10…15` (alpha 16–21/255), `dist` 5 | shader `Primitive_ScreenMultiUV` (Part B §8). Godot: CanvasLayer quad shader |
| Rain particles | `RainDemo`: `tex_rain_particle_00`, 200 units, fov 45, quads 1.8 × 0.5, near zone speed 84 / dist 0.7 / area 30 × 5 / colour `0xffffff18`, far zone 42 / 1.1 / 30 × 5 / `0xffffff22`, fade_speed 20 | INF: camera-attached streak quads (name: Demo = cutscene/replay). Godot: GPUParticles3D attached to the camera |
| Lens drops | `Draw2D_CameraLensWaterDrops`, `tex_waterdrop_on_lens_0{1,2}_alp` | refraction offset buffer (Part B §8) |
| Snow | no `_s` rig, no snow shader (OBS fsop search). Textures `tex_snow_screen_00`, `tex_snow01_alp` (exe paths @0x142a9a11d), stadium `cm000_snow001_{bsm_alp,nrm,srm,lbm,mtl}` (21 stadiums), `stNNN_snow001_*`. Code flag `_ForSnow` on named objects (`0x1419b1730` sets `obj+0x50` if the name contains `_ForSnow`) | INF (medium): snow = rain rig and overlays + a snow screen layer using the same MultiUvScroll path with the snow textures, + `_ForSnow` scene models |
| Fog | global height fog and atmospheric fog per span (§4.1) | formulas Part B §3–4 |
| Weather change | `PLAY_DATA_CHANGE_WEATHER_RAIN_AFTER/ALWAYS`, `ObserverWeatherChange@record@match` | not traced |

---

## 6. Live readings (pid <pid>, read-only, match in progress at 21:04–21:10)

| Object (RTTI) | Heap | Values |
|---|---|---|
| rig in use | — | `light_st071_af` (4cc: atsh/pcsp/rpd paths say st009; all spans 17:30; lat 45, lon 90, north 90, 2019-03-06, gmt 1; shadowRange 512/256 in file, 120/80 live) |
| `TimeOfDay`, `TimeOfDayBody` | 0x25ac2720, 0x26b50320 | 9 spans; body → slot mapper 0x25f20d40 (§2.3) |
| `TimeOfDaySettings` ×10, `TODSettingsBody` ×10, `TimeOfDayObjectProxy` ×9 | 0x25ae0120… | — |
| `TppAtmosphere` | 0x262f0120 | Rayleigh (0.0058, 0.0135, 0.0331) ×2, Mie 0.004, cloudy Mie (0.05, 0.05, 0.03); heights 4/1200; absorption 0.1; g 0.8/0.2; north 90; lat 45; lon 90; shadow 120/80/35/30/1/−15; sunLux 150000; numBands 3; date 2019-03-06; `localFlags` **2111 live vs 3135 in file** (bit 0x400 cleared at runtime; meaning unknown); divisions 175 |
| atmosphere/sun struct | 0x26bd20c0 | §3.2 |
| `SceneAtmosphereParameter` | 0x242a3110 | coefficients copy; sun light dir `+0x110 = (−0.5644, −0.7055, 0.4286)`; `+0x120 = (0, −1, 0)` (moon/off); `+0x1c0 = (11, 0.6283, 0.27, 0.6283)` unknown |
| `PluginTonemap` | 0x1ff9c10 | global filmic set + span set; key 0.18, EV −14/−7 (Part C §3) |
| `TonemapFilterDx11` | 0xb345470 | 3×3 white-balance matrix (Part C §3) |
| `ColorGrading` ×3, `ColorGradingParameter` ×3, `GlobalLightArray`, `PluginSky`, `SkyParameter` ×2 | — | not decoded |

Not readable: the current exposure value. Adaptation runs on the GPU (`Tonemap_Lum*`) and on the CPU (law unknown, Part C §1).

---

## 7. Godot reimplementation recipe (summary)

1. **Load.** For stadium N, weather W (fine / other) and time zone Z (day / night), with season S:
   - parse `light_stNNN_a{f|r}.fpkd`;
   - choose the span by suffix (§2.3);
   - merge its `TimeOfDaySettings` over the global entity per override group (§2.4).
2. **Sun.**
   - `DirectionalLight3D` from `pes_sun_dir` (§3.3) for the span hour:minute.
   - Lux = `sunLux` × transmittance, or use the `.atsh` light colour for that time (Part B §9).
   - Off when `sunLightMode == DISABLE`.
   - Shadows: PSSM with max distance ≈ `shadowRange`, blur ≈ `shadowBlurRadius`.
3. **Floodlights.** Instantiate the SpotLight/PointLight sets with physical units (§4.2). Enable the groups listed by the span's proxies.
4. **Sky and ambient.**
   - Approximate: `PhysicalSkyMaterial` (`mie_eccentricity` 0.8, `ground_color` 0.5).
   - Exact: a custom sky shader with Bruneton tables generated offline from the TppAtmosphere coefficients, plus `.atsh` SH9 for ambient.
   - `ambient_light_source = SKY`; `ReflectionProbe`s per fox2 probe (Part B §5–7).
5. **Fog.**
   - Approximate: `fog_mode` exponential with height; density = `globalFogDensity`/1000 per m; `fog_height_density` = `globalFogHeightScale`/1000; colour = `globalFogColor`; aerial perspective on.
   - Exact: a compositor pass running the Part B integrals.
6. **Exposure.** Fox pre-exposes the scene by 2^EV, with EV clamped to [min, max]; key 0.18; compensation (Part C §1).
   - With physical light units: `CameraAttributesPractical.auto_exposure_enabled`, scale = 0.18·2^comp, min/max sensitivity from the EV clamps.
   - Or fixed exposure: `exposure_multiplier = 2^EVinitial` scaled by Godot's physical normalization (INF, needs a frame check).
7. **Post.**
   - Environment `tonemap_mode = LINEAR`.
   - A `CompositorEffect` does, in order: Kawase bloom with a hard threshold (or approximate with `glow_*`), the white-balance matrix, UE4 FilmToneMap with the merged `acesFilm*`, `pow(1/2.2)`, the 16³ grading strip, LumaSharpen + grain.
   - AA: `screen_space_aa = FXAA` (Fox preset 12, no subpixel) or `use_taa`.
   - Formulas: Part C.
8. **Weather.** Rain = rig `ar` + `cloudiness` + material `Wet` + puddle decal + screen-rain quad + camera particles (§5).

---

## 8. Killed hypotheses (main part; Parts B and C list their own)

1. **"d/n f/r overlays carry the lighting."** Killed: st002 `_dr`/`_df` overlays are two scoreboard meshes. Lighting is entirely in `light_stNNN_a{f,r}` (OBS).
2. **"There is a DirectionalLight entity in the rig."** Killed: no such entity in any rig. The sun is computed by `TppAtmosphere` (OBS entity census, §3).
3. **"Each match picks a span by clock hour."** Killed: the span is chosen by name suffix and slot, keyed by time zone and season. The hour only feeds the sun position (OBS `0x14191c040/c800`).
4. **"The span tonemap applies."** Killed live: spans have `overrideTonemap = False`, and the global set is the one uploaded (OBS `PluginTonemap`).
5. **"The live atmosphere is the 4cc st009 rig."** Killed: lat/lon (45, 90) and the all-17:30 spans match the 4cc st071 file, which reuses st009's atsh/pcsp/rpd (OBS file + live).
6. **Sun hypotheses:** see §3.3.

## 9. Open (what was tried)

1. **Time progression and `ctx+0x18`.** Mapping +1 for night (and in explicit-time PROGRESSIVE mode) when `ctx+0x18 > 2`. Tried: lookup disassembly. The caller and the field's meaning (match phase?) were not traced. A 4-minute live log showed no sun movement.
2. **Snow path.** No snow shaders exist. Whether snow loads `_ar` (code tests `rain != 0`) and where `tex_snow_screen_00` is set up was not observed live (the session was fine weather).
3. **Spot/point light attenuation formula** (`SSLighting_Spot*`/point shaders), `LightOccludeCube` and `GlobalLightArray` usage were not decoded. The Godot mapping in §4.2 is INF.
4. **`localFlags`** bits (3135 → 2111 live), the `SceneAtmosphereParameter+0x1c0` values, and the `PesPluginSettings.flags` bit 11.
5. **Enlighten** probe sets / `.sse` and the per-stadium `*_FILTERLUT` / `lut_Match` consumers.
6. **`Wet` at runtime in rain** (MaterialModifier2) was not observed (fine-weather session).
7. Remaining Part B / Part C Open lists. The adaptation law and the bloom mapping are now OBS (Part C). Still open: the 33³ LUT encoding (live path = `ColorCorrection_CubeLut`, whose extracted body is a stub), sky and fog cbuffer bindings, and the default AA.

---

# Part B — sky, ambient, fog, weather shaders, rig binaries (sub-agent SkyFogRE)

## Sky, ambient, fog and weather (PES 2021 / Fox DX11)

Sources: `<scratch>/ShaderMaterialRE/asm/*.asm` (vkd3d), RDEF via `<scratch>/LightingRE/SkyFogRE/rdef.py` (prints bindings, texture dimension and cbuffer variables). Light rig data comes from `af.txt`/`ar.txt` (st002). Files were unpacked into `<scratch>/LightingRE/SkyFogRE/{af,ar}/Assets/...`.
Tags: **OBS** = I read it in the cited asm or bytes. **INF** = inference, with a confidence level.

Global facts:
- **OBS (RDEF):** all cbuffers are anonymous `float4` arrays (`g_psMaterial`, `g_psScene`, `g_psObject`, …). Field names therefore cannot be read from the shaders. Every fox2-field binding below is INF and rests on value or shape matches. Only material shaders get names, from the `PesShaders_dx11.lua` packing.
- **OBS:** `cb2[23].z` (PS scene) multiplies every radiance output (sky, fog, SH, probes, clouds). INF (high): this is the pre-exposure / scene-luminance scale and is the same constant the PostChain report handles.
- **OBS:** depth linearisation everywhere is `zView = cb2[21].z / (rawDepth - cb2[21].w)` (metres). Reversed‑Z: `rawDepth == 0` means sky.
- **OBS:** the atmosphere uses km: `Rg = 6360`, `Rt = 6420` (6421 for the ray-exit test, i.e. Bruneton's `RL`), `Rt²-Rg² = 766800`, `H = sqrt(Rt²-Rg²) = 875.671143`.

### 1. Precomputed atmosphere (Bruneton & Neyret 2008)

The precompute passes are the reference implementation (Bruneton's `transmittance.glsl`, `irradiance1`, `inscatter1`, `copyInscatter1`, `inscatterS`, `irradianceN`, `inscatterN`, `copyIrradiance`, `copyInscatterN`). Only resolutions and sample counts were changed:

| table | layout (OBS from texel→param math) | samples |
|---|---|---|
| transmittance `inTransmittance` | 256×64. `mu = -0.15 + tan(1.5·x/256)·0.0815520659` (= 1.15/tan 1.5). `r = 6360 + 60·(y/64)²` | 50 (`Sky_Transmittance.ps.asm` loops, `ilt 50`) |
| irradiance ΔE | 64×16 (`Sky_CopyIrradiance`: ×1/64, ×1/16). `muS = -0.2 + x/(64-1)·1.2`, `r` from `y·4` | IrradianceN: 2 nested loops, step π/16 (`1.96349546e-01`) |
| inscatter ΔS / S | 256×2048 2D atlas of a 4D table. **RES_NU=8, RES_MU_S=32, RES_MU=128, RES_R=16**. `x = nu_i·32 + muS_i`, `y = r_i·128 + mu_i` (`Sky_Inscatter1.ps.asm`: `y/128` floor → layer, `r = sqrt(Rg² + (layer/15)²·766800)`. Layer 0 gets +0.01 and layer 15 gets −0.001.) | Inscatter1/N: 12 steps (`ilt 12`). InscatterS: angular loops step π/... (`1.54212564e-01`, `8.33333313e-01`) |

- **OBS:** `muS = tan((2·f - 1 + 0.26)·1.1)·0.186929032` (= 1/tan(1.386)). `nu = -1 + nu_i·0.285714298` (2/7). The mu split is Bruneton's ground/sky halves (`2.03174615 = 128/63`).
- **OBS:** the Mie extinction used in transmittance is `cb4[1].xyz / cb4[2].y`.
- **INF (high):** `cb4[2].y = 1 - mieAbsorptionRate = 0.9`, which is Bruneton's `betaMEx = betaMSca/0.9`.
- **OBS:** InscatterS and IrradianceN use the Cornette–Shanks Mie phase: `0.119366206 = 3/(8π)`, `(1-g²)(1+μ²)/((2+g²)(1+g²-2gμ)^1.5)`, with `g = cb4[2].x`.
- **OBS:** `cb4[3].x == 1` selects the first-order branch (`eq … l(1.0)`).
- **OBS:** `Sky_CopyInscatterN` divides by the Rayleigh phase `(1+nu²)·0.0596831031` (= 3/(16π)) before accumulating.

Precompute cbuffer (`cb4`, same layout in all `Sky_*` precompute PS):

| field | meaning | fox2 source |
|---|---|---|
| cb4[0].xyz | β_R scattering (km⁻¹) | `TppAtmosphere.rayleighScatteringCoefficient` (0.0058, 0.0135, 0.0331). These are exactly Bruneton's values. INF (high) |
| cb4[0].w | H_R (km) | `rayleighHeightScale`, used **verbatim** (4 km) for the clear tables. The cloudy tables use an engine constant of 8 km. OBS live (§10) |
| cb4[1].xyz | β_M scattering | `mieScatteringCoefficient` 0.004 (Bruneton's value). INF (high) |
| cb4[1].w | H_M (km) | `mieHeightScale` 1200 m → 1.2 km. INF (high); 1.2 also appears hard-coded in §3 |
| cb4[2].x | Mie g | `mieAnisotropy` 0.8 (cloudy sky: `mieAnisotropyOfCloudySky` 0.2). INF |
| cb4[2].y | Mie scattering/extinction ratio | `1 - mieAbsorptionRate` (0.9). INF |
| cb4[3].x | scattering order flag | `multiScatteringOrder` loop index. INF |

Two sets of tables exist: clear (`inTransmittance`/`inInscattering`, t1/t3) and cloudy (`g_CloudyTransmittance`/`g_CloudyInscatter`, t5/t7). **OBS:** every consumer computes `lerp(clear, cloudy, cb5[10].x)`.
- **INF (high):** `cb5[10].x = TppAtmosphere.cloudiness` (af 0.0, ar 0.7).
- **INF:** the cloudy tables are built with the `…OfCloudySky` coefficients (Mie 0.05/0.05/0.03, g 0.2).

### 2. Sky radiance (`Sky_Draw.ps.asm`; `Sky_Draw_SunOnly`, `Sky_DrawOnlySky` and `_ExHL` are subsets)

The VS emits a full-screen triangle; `v1` = world view ray built from `cVSWork[4..10]`. OBS:

```glsl
r   = 6360 + max(cb4[4].y, 0.001);                 // camera altitude (km)
v   = normalize(v1); mu = v.y;                       // Y-up
// Bruneton texture4D, but only the 4 lowest R layers are uploaded:
uR  = min(4*(0.03125 + rho*1.07060745e-3), 1);     // rho = sqrt(r²-Rg²); 0.5/16 + rho/H*(15/16)
uMu = 0.5 ± 0.4921875 * (...)/(rho+H) (+0.496093750 / 0.503906250)  // RES_MU=128
uMuS= 0.015625 + 0.484375*(atan(max(muS,-0.1975)*5.34962368)/1.1 + 0.74)  // RES_MU_S=32
nuI = (nu+1)*3.5; x = (floor(nuI)+uMuS)/8;  y = (layer + uMu)/4   // runtime atlas: 256 x (4*128)
S   = lerp(texInscatter(t3), texInscatter(t7), cloudiness);       // bilinear in nu, linear in layer
mie = S.rgb * S.a / max(S.r,1e-4) * (betaR.r / betaR);           // Bruneton getMie()
L   = S.rgb * 0.0596831031*(1+nu²)                               // Rayleigh 3/(16π)
    + mie   * (1-g²)*0.0795774683 / (1 - g*nu)²;                 // Schlick-style phase, g=cb4[2].x (NOT HG/CS)
L   = max(L,0) * cb2[23].z * I_light;                            // I_light = cb4[6].w (light A) or cb4[3].w (light B)
// sky = L(lightA, dir cb4[6].xyz) + L(lightB, dir cb4[3].xyz)
disk = (dot(v, Ldir) >= 0.999847710 /*cos 1°*/) ? 10 * I_light * cb2[23].z * T(v) : 0;  // T = lerp(clear,cloudy) transmittance
sky += disk(lightA) + disk(lightB) + cb4[7].x*cb2[23].z*vec3(3.284e-3, 4.11e-3, 6.24e-3);  // night airglow
if (v.y < 0) sky = cb4[5].xyz, alpha = 0;                         // flat colour below horizon
alpha = max(diskTerm, dot(sky, (0.2125,0.7154,0.0721)/32));      // 6.64062519e-3, 2.23562494e-2, 2.25312496e-3
fog   = sampleFroxelAtlas(inFogVolume, slice = last(31));        // same atlas math as §4
sky   = sky*(1 - (1-fog.a)*cb4[5].w) + fog.rgb*cb4[5].w;
out   = sky + bayer8x8(inMesh)*(1/128);                          // 7.8125e-3 dither on all four channels
```

| Sky_Draw cbuffer | meaning | fox2 |
|---|---|---|
| cb4[0].xyz | β_R | rayleighScatteringCoefficient. INF |
| cb4[2].x | Mie g | mieAnisotropy 0.8. INF |
| cb4[3].xyz/.w | light B dir / intensity. `Sky_Draw_SunOnly` uses only this one (OBS), so this is the **sun** | sunLux 150000 × unknown scale. INF |
| cb4[6].xyz/.w | light A dir / intensity (moon when present) | moonLux. INF (medium) |
| cb4[4].y | camera altitude (km) | INF |
| cb4[5].xyz | below-horizon colour | groundColor (0.5)? INF (low) |
| cb4[5].w | froxel-fog weight on sky | `influenceOfFog` (0.0). INF (medium) |
| cb4[7].x | night-sky glow | `starLight·π·skyColorSunScale`. OBS live (§10) |
| cb5[10].x | clear↔cloudy blend | cloudiness. INF (high) |

- **OBS:** `Sky_SunColor.ps.asm` produces the directional-light colour: `T_sun = lerp(T_clear, T_cloudy, cloudiness)` at the sun direction, using the Cornette–Shanks phase.
- **OBS:** for a sun below the horizon the colour fades with `sat(1 + 28.5053635·(π/2 - acos|y|))`. 28.5 rad⁻¹ is about 2° of fade.
- **OBS:** `Sky_Extract` is a point copy scaled by `cb5[10].xy` (sub-rect).
- **OBS:** `EnlightenSky` evaluates world SH9 (constants below) × albedo.a × `cb2[23].z`. It discards stencil IDs 234 and 200.

**Godot mapping.**
- `PhysicalSkyMaterial` cannot express this exactly: it has no precomputed multiple scattering, uses a different phase, a different Rayleigh parameterisation and no flat ground colour.
- Closest settings: `mie_eccentricity = 0.8` (equals mieAnisotropy), `mie_coefficient ≈ 0.004–0.005`, `rayleigh_coefficient` at its default 2 with `rayleigh_color` tuned by eye, `ground_color = (0.5,0.5,0.5)`, `sun_disk_scale = 1`.
- Sun disk: set `DirectionalLight3D.light_angular_distance = 2.0°`, which gives Fox's 1° radius (OBS: cos 1°).
- Exact route: a Godot `shader_type sky` running the Bruneton lookup above, with transmittance (256×64) and inscatter (256×2048, or the 4-layer 256×512 slice) generated offline in Python by porting the precompute shaders.
- Sky energy: `sky_energy_multiplier` = sun intensity × exposure scale.

### 3. Aerial perspective / atmospheric fog (`Primitive_AtmosphericFog_NonSky.ps.asm`)

This runs as a deferred full-screen pass. t1 = depth, t0 = inscatter atlas (as in §2), t12 = froxel volume. OBS:

```glsl
z = linearDepth(m); if (raw < cb4[2].w) discard; if (z - cb4[2].y < 0) discard;   // start depth
d  = (raw>0) ? max(z*0.001, 1.0) : 100.0;          // km; never shorter than 1 km
r0 = 6360.00977;                                   // camera hard-coded at 10 m altitude
v  = normalize(viewRayWS); vY = max(v.y,0);        // ground-clamped; geometry uses max(v.y,0.15) for mu at x0
S0 = inscatter(r0, mu, muS, nu);  S1 = inscatter(|x0|, mu1, muS1, nu);   // fixed layer lerp 0.519895613
T  = analyticTransmittance(r0, mu, d)               // Proland opticalDepth(), constants baked:
   = exp(-(vec3(3.27535748,7.62367678,18.6921272)*D(H=8km) + 0.965360880*D(H=1.2km)));
// D(H): a=sqrt(0.5*r/H) -> 19.937418 (H=8) / 51.4781876 (H=1.2); erfc approx 2.3193*|a|+sqrt(1.52a²+4);
//       exp(-d/H*(d/(2r)+mu)) with 1/H = 0.125 and log2e/1.2 = 1.20224583
S  = max(S0 - T*S1, 0);                            // Bruneton aerial perspective
mie= S.rgb*S.a/max(S.r,1e-4)*(betaR.r/betaR);
Lr = 0.0596831031*(1+nu²)*S.rgb;   Lm = Schlick(g=cb4[2].x,nu)*mie;
L  = Lr*(1-cb4[4].y) + cb4[4].y*0.1*(0.8*length(S) + 0.2*Lm) + Lm;         // "whiteness" mix
if (geometry) {                                   // not for sky pixels
  L = log(1 + L*cb4[1].w) * cb4[1].xyz;            // log-compressed amplitude
  h = 1 + (z*vY + cb2[20].y)*(cb4[4].x-1)/cb4[4].z; L *= clamp(h, 1, cb4[4].x);  // height amp
}
L *= cb2[23].z * cb4[3].w;                          // exposure × sun intensity
F  = froxel(inFogVolume, z);  L = L*F.a + F.rgb*min(length(L),1);
x  = z - cb4[2].y;  w = (x < cb4[4].w) ? sat(cb4[7].y*x) : sat(cb4[7].z*x + cb4[7].w);  if (w<0.001) discard;
out.rgb = w*L;  out.a = w*(1 - min(T.r,1));         // presumably blended ONE, INV_SRC_ALPHA (blend state not in DXBC)
```

**OBS:** the hard-coded fog coefficients equal `sqrt(2π·H·r)·β`. Rayleigh: 0.0058/0.0135/0.0331 × 565.4, which reproduces 3.275/7.624/18.69 to within 0.15%. Mie: 0.96536 = 0.00441 × 218.97. So the fog **ignores** the cbuffer β/H and always uses H_R = 8 km, H_M = 1.2 km and β_Mext ≈ 0.0044 (= 0.004·1.1).

| cb4 (fog) | meaning | fox2 (INF) |
|---|---|---|
| [0].xyz | β_R for getMie ratio | rayleighScatteringCoefficient |
| [1].xyz | log-amp RGB | `objFogRgbAmp.rgb` (1,1,1). medium |
| [1].w | log-amp input scale | `objFogRgbAmpFuncScale` 1000 or `objFogRgbAmp.w` 0.1. low |
| [2].x | Mie g | mieAnisotropy |
| [2].y | start distance (m) | `startDepth` 40. high (discard below) |
| [2].w | raw-depth cutoff | engine |
| [3].xyz / .w | sun dir / intensity | sun |
| [4].x / [4].z | height-amp max / height | `objFogHeightAmpMax` 2 / `objFogHeightAmpMaxHeight` 200. high (shape: clamp(1..max) over height) |
| [4].y | whiteness mix | `whiteness` 0. medium |
| [4].w, [7].yzw | piecewise-linear distance weight | `rangeDepthNear` 1000 / `rangeDepthFar` 4000 (and `fogAmpNear` 1 / `fogAmpFar` 800 as slopes?). low |
| cb2[20].y | camera world Y | engine |

**Godot mapping.**
- Approximate with `fog_enabled`, `fog_mode = FOG_MODE_EXPONENTIAL` and `fog_aerial_perspective = 1` (tints fog from the sky radiance cubemap). Also set `fog_sun_scatter ≈ 0.2–0.4` (Fox's Schlick g = 0.8 Mie lobe) and `fog_density ≈ 1e-4/m`, tuned so the 1 km floor and ramp look right. `fog_light_color` comes from the sky.
- Exact: a CompositorEffect (post-opaque) running the pseudocode above with the same inscatter atlas as the sky shader.

### 4. Height / global fog

- **OBS, `Fog_DrawVolume.ps.asm`** (fog volume meshes; the same formula is inlined in `Sky_DrawClouds`). Analytic exponential height fog, km units:
```glsl
D  = (P - cam)*0.001;  d = length(D);  k = cb3[7].y;  rho = cb3[7].x;
od = rho * d * exp(-cam.y*0.001*k) * (1 - exp(-k*D.y)) / max(k*D.y, 1e-5);
T  = min(exp(-od),1);   out.rgb = (1-T)*cb2[27].rgb;  out.a = T;      // fog colour cb2[27]
```
  INF (high): `cb3[7].x = globalFogDensity` (0.03 km⁻¹), `cb3[7].y = globalFogHeightScale` (4 km⁻¹, i.e. a 250 m e-fold), `cb2[27] = globalFogColor` × exposure.
- **OBS, `DeferredRendering.ps.asm`** (non-volfog composite): `lit = (albedo*diffAcc + specAcc)*occl` and `out = lerp(lit, cb2[27], clamp(z*cb2[24].x + cb2[24].y, cb2[24].z, cb2[24].w))`. This is linear depth fog with min/max clamps.
- **OBS, `DR_VolFog_TppTonemap.ps.asm` (fog part only)**: `out = lit*F.a + F.rgb` with F from a froxel volume stored as a 2D atlas:
  - `slice = clamp(log(z)*cb2[26].y + cb2[26].z, 0, cb2[26].w)` (log-depth slices).
  - Atlas: 32 slices per row, `u = 0.0146484403·ndc.x + 0.015625 + 32·frac(slice/32)·cb2[25].z`, `v = cb2[25].x·ndc.y + cb2[25].y + floor(slice/32)·cb2[25].w`. The cell is 64 px wide with a 1/64 border.
  - Linear between adjacent slices. The froxel *injection* shader is not among the extracted PS (likely compute) → Open.

**Godot mapping.**
- Global height fog: Godot's `fog_height` / `fog_height_density` is `max(1-exp(-ρd), 1-exp(k·(y-h0)))`, not the integral. Use `fog_density = globalFogDensity/1000`, `fog_height = 0`, `fog_height_density = globalFogHeightScale/1000` as an approximation, or a compositor pass with the 4-line integral above (exact).
- `DeferredRendering` linear fog = `fog_mode = FOG_MODE_DEPTH` (`fog_depth_begin = -b/a`, `fog_depth_end = (1-b)/a`, `fog_depth_curve = 1`, `fog_density` = max clamp).
- Froxel volume = `volumetric_fog_enabled` (Godot also uses exponential depth slices: `volumetric_fog_length`, `volumetric_fog_detail_spread`). Densities are unknown until the injection shader is found.

### 5. Ambient: SH sky light

- **OBS, `SH_SphereMap.ps.asm` / `SH_DynamicSphereMap`**: these bake irradiance into a sphere-map atlas (`g_sphericalMaps`). Each texel is decoded with an equal-area mapping (scale `1/0.8125 = 1.23076928`) to a direction `n`, rotated by `cb2[8..10]`. Then:
```glsl
n4 = vec4(n,1);  s = cb2[23].z;
E  = vec3(dot(n4*s, M_R*n4), dot(n4*s, M_G*n4), dot(n4*s, M_B*n4));   // cb7[0..3],[4..7],[8..11]
w  = sat(dot(n4, M_w*n4));                                             // cb7[12..15] scalar quadratic form
E += w * vec3(n4ᵀ M2_R n4, …)*s;                                       // cb7[16..27] second SH set
out = vec4(E,w);   // Dynamic variant also adds w*texture(inSphereMap).rgb
```
  These are Ramamoorthi–Hanrahan 4×4 irradiance matrices, i.e. **3-band, 9-coefficient SH** (numBands 3).
- **OBS, `SSLighting_SH_SkyLight.ps.asm`**: `uv = 0.5 + 0.40625·(-n.xy/|n.xy|)·sqrt(0.5-0.5n.z)` (Lambert azimuthal equal-area), × atlas scale `cb5[9].zw`. Then `out = max(tex,0)`.
- **OBS, `_Encode`**: `out = lerp(diffAcc.rgb, skySH, sat(diffAcc.a)) · (1 + cb5[15].y·exp(-(p13²+p14²)/cb5[15].x))`. p13/p14 are `dot(cb5[13|14], viewPos)`: a Gaussian ambient boost around a 2D point. INF: pitch-centre lift.
- **OBS, `EnlightenSky`**: explicit SH9 with constants 0.282094806, 0.488602519, 1.09254849, 0.315391570, 0.546274245 in the standard order. The basis z axis is the up axis (Y20 uses r.x after the `cb2[12..14]` rotation).
- **OBS, atsh data**: `L1,0` (index 2) is ≫0 at noon (blue 7092 vs L00 18800). So SH "z" = world up (Fox Y).

**Godot mapping.**
- `ambient_light_source = AMBIENT_SOURCE_SKY` and `ambient_light_sky_contribution = 1`. Godot projects the sky's radiance cubemap to SH itself, which is the same idea.
- To get the baked Fox values exactly: in the custom sky shader, `if (AT_CUBEMAP_PASS) COLOR = sh9(atsh[t], EYEDIR.xzy) * skyLightLuminanceScale * exposure`. This holds because atsh stores *radiance* SH (INF high: L00/√(4π) ≈ 5300 cd/m² at noon). Godot then does the cosine convolution like Fox's M matrices.
- `ambient_light_energy` ↔ `daySkyAmbientScale`. The Gaussian boost (Encode) needs a compositor.

### 6. Reflection probes

**OBS, `ReflectionProbe_ParallaxCorrected.ps.asm`:**
```glsl
N = octDecode(gbuf normal);  rough = spec.x;  specOcc = spec.y;  P = viewPos(depth);
R  = reflect(V, N);  a = sqrt(rough);  R = lerp(N, R, (1-a)*(sqrt(1-a)+a));   // dominant direction
Rw = mat3(cb2[8..10]) * R;
pl = (cb5[9..11]) * P;                                     // to unit probe box
fade = sat(min3(min((pl-0.5)*cb5[15], (pl+0.5)*cb5[16])));  // per-face inner-box fade
pb = clamp((cb5[12..14])*P, -0.5, 0.5);  rb = normalize(mat3(cb5[12..14])*R);
t  = min3(max((-0.5-pb)/rb, (0.5-pb)/rb));  hit = (cb5[4..6]) * (pb + rb*t);
dir= lerp(Rw, normalize(hit - cb5[8].xyz), max(1 - sqrt(rough+1e-5), 0));     // capture pos cb5[8].xyz
c  = textureLod(cube, vec3(-dir.x, dir.y, dir.z), rough*5);                     // X mirrored, 6 mips
(A,B) = texture(g_LookupTexture, vec2(sat(dot(N,-V)), 1-rough)).xz;             // split-sum LUT
F0/tint: matID 1 -> (0.04+0.96*spec.z, white); matID 3 -> (same, tint (0.9,1,0.8)); else from material LUT row
out.rgb = cb2[23].z * cb5[8].w /*intensity*/ * specOcc * (F0*A*tint + B) * c;  out.a = albedo.a * fade;
```
- INF (high): `cb5[15]/[16]` are built from `innerScale{X,Y,Z}{Positive,Negative}`. Fade is 1 inside `innerScale·box` and falls to 0 at the box face.
- `SSLighting_EnvmapLight` (global/sphere probe), OBS:
  - Sphere-proxy intersection with centre/radius `cb5[9]`.
  - The cube is decoded as `(rgb/a)²` (RGBM-like, squared).
  - Fresnel comes from LUT `g_lut1`. Tint is `cb5[10].rgb`.
  - The result is scaled by `sat(lum(diffuseAcc)/(cb2[23].z·cb5[10].w))` (darkened where diffuse light is low) and clamped to 1e4.

**Godot mapping.** `ReflectionProbe` per Fox probe:
- `size = transform_scale` (e.g. 260×72×90), `origin_offset = capturePos - centre`, `box_projection = true`, `intensity = intensity`, `interior = false`.
- `blend_distance ≈ min((1-innerScale)·size/2)`. Godot only supports symmetric blending, so asymmetric inner scales are approximated.
- `update_mode = ONCE`.
- Roughness-dependent parallax fade and the X mirror are not expressible. Godot cannot load the prebaked `.rpd` into a ReflectionProbe; use it only via a custom material or compositor `samplerCube`.

### 7. Cloud dome (TppSky): `tpp3dfw_constant_sky` (mesh `sky_dome.fmdl`, scaled 8000×2000×8000) and `Sky_DrawClouds`

**`tpp3dfw_constant_sky` VS, OBS:** channel weights from `c = cb4[0].w`: `w = (sat(3c), sat(3(c-1/3))·[c≥1/3], sat(3(c-2/3))·[c≥2/3])`. INF (high): `c = TppSky.cloudCover` (0.3 → w = (0.9,0,0)). The cloud texture's R/G/B are cumulative cover layers.

**PS, OBS:**
```glsl
uv   = (vtxUV + vec2(cb4[4].w, cb4[1].x)) * cb4[2].z;          // wind offsets, textureRepeats
dens = dot(w, texture(cloud, uv).rgb) / (0.2*v.y + 0.8);
T    = exp(-dens*cb4[1].z);  a = 1-T;                           // cb4[1].z = density (0.6)
Lsky = inscatter(r0=6360.01, horizon-clamped)*cb4[3].w + night glow(cb4[7].x) + cb3[1]*cb2[23].z (sky light)
Lin  = lerp(Lsky_rayleigh, 0.1*(0.8|S|+0.2*mie), cb4[4].y)      // in-cloud scatter gain
glare= cb4[1].w*cb4[7].y*2.38418579e-7/(4π(1-nu)²+1e-10)         // pinpoint sun
silver = 0.5*cb4[1].w * schlick(cb4[1].y,nu)²·(1+…) * cb3[1]     // dirLightGain*0.5 (=1) forward lobe
Lc   = (Lin + silver + cb4[4].z*cb3[1]*cb2[23].z) * (0.05 + 0.95*T);
out  = (Lsky+glare)*(1-a³) + Lc*a⁵;   then froxel fog + dither (as §2)
```
INF mapping: `cb4[1].z = density` (0.6), `cb4[1].w = dirLightGain` (2), `cb4[1].y = diffusion`-derived g (low), `cb4[4].y = inCloudScatterGain` (0.4), `cb4[1].x/cb4[4].w = cloudWindInfluence × time`.

**`Sky_DrawClouds` (low cloud layer `inLowClouds`, lat-long), OBS:**
- `u = atan2(v.z, v.x)/2π + 0.5 + cb4[6].x`. `v` comes from the elevation param with `cb4[5].xyzw`.
- The texture is sRGB-decoded manually. `density = lerp(G, R, k)` with `k = sat((0.258 - sunY)·0.795)`.
- Lighting: `(cb4[0]·dot(horizSunDir, v) + cb4[1]) + cb4[2]·cb3[2].x·max(1 - 4·dot(sunToCloud, v), 0)`.
- Distance fog uses the §4 integral with `cb3[7]` and `cb2[27]`.
- The output is **sRGB-encoded** (LDR cloud buffer), alpha = `cb4[0].w`·(B-channel coverage).

**Godot mapping:** none built in. Port to the custom sky shader: dome UV = lat-long of `EYEDIR`, with the formula above.

### 8. Weather

- **`TppRainFilterGBufferMaterial` (OBS):** G-buffer rain darkening.
  - World pos from depth. Overhead "viewport capture" occlusion map at `uv = worldXZ·0.015 + 0.5` (66.7 m window) holds roof heights `(r,g,b,a)`.
  - `covered = (h ≥ cap.x ? 1 : cap.y)·(h ≥ cap.z ? 1 : cap.w)`, with `h = 1 - sat(worldY·cb4[0].w)`.
  - `wet = sat((z - cb4[2].x)·cb4[3].x) · covered`.
  - `albedo' = lerp(albedo, albedo·cb4[0].x, wet)` (sRGB re-encoded).
  - `mat.x' = mat.x ≥ cb4[0].z ? lerp(mat.x, cb4[0].z + cb4[3].y·(mat.x - cb4[0].z), wet) : mat.x`. This compresses roughness above a threshold, i.e. glossier when wet.
  - **Godot:** none. Use a compositor on the G-buffer (not exposed in Godot 4) or a per-material `wetness` uniform: `ALBEDO *= mix(1, k, wet); ROUGHNESS = mix(r, t + s·(r-t), wet)`.
- **`pes3ddc_wet` (deferred decal, OBS, names from lua packing):**
  - cb4[0] = (Specular, Roughness, AlbedoCubeScale, SubNormal_Blend), cb4[1] = (URepeat, VRepeat, UScroll, VScroll), cb4[2].xyz = Dirty_Mask (RGB channel selector), cb4[2].w = TimeSecond.
  - `mask = dot(Dirty_Mask, srgbEncode(srm.rgb))`. Discard if `mask < bayer8x8·cb0[0].w` or `vtx.a < 0.9` (dithered coverage).
  - `N = normalize(N_tex + SubNormal_Blend·N_sub((uv + Scroll·Time)·Repeat))`.
  - `albedo = cube(reflect).r·AlbedoCubeScale` (grey, alpha = mask). `rough/spec = (Roughness, Specular)`.
- **`pes3ddf_puddle_ggx_decal` (technique `pes3DDC_Puddle_GGX`, rain_bsm/msk/nrm/srm, OBS):**
  - Same dithered discard against `Mask_Tex.a` and `vtx.a ≥ 0.9`.
  - `albedo = Base_Tex`, `N = normalize(N1(uv + cb4[1].xy·cb4[2].x) + N2(uv2 + cb4[1].zw·cb4[2].x))`. INF (high): `cb4[1] = PuddleSpeed1U/V, PuddleSpeed2U/V` and `cb4[2].x = PuddleCurTime`.
  - Normals are DXT5nm-style (`x·w`). `roughness/spec = srm.yx`. Material id 0.01, motion vectors written.
  - **Godot:** decal-like `StandardMaterial3D` / shader with two scrolling normal maps, alpha-scissor dither.
- **`Primitive_ScreenMultiUV` (rain screen layer `MultiUvScrollScreen`, OBS):**
  - VS passes two sRGB→linear vertex colours and two UV sets (scrolling is done CPU-side or in the VS input).
  - PS: `a = a0·c0.a + (1 - a0·c0.a)·a1·c1.a`. Discard below 0.001. `rgb = min(lerp(t0.rgb, t1.rgb, a1·c1.a)·a, 0.25)` (premultiplied).
  - **Godot:** full-screen `CanvasLayer`/quad shader with the same 5 lines.
- **`Draw2D_CameraLensWaterDrops` (OBS):**
  - `tex = lerp(t0, t1, sat(v1.x))`; discard if a < 0. Rim shading from two offset samples (±0.4·screen-centre offset): `s = 0.45 + 0.55·sat(x·1.21951222)`.
  - `out.rgb = 0.25·(tex.r·s1, tex.g·s1, tex.b·s2)`, `a = tex.a·v1.w`. This is a refraction/normal-offset buffer (INF), not colour.
- No `*Snow*` shaders exist in the asm dir. FxShaders lua has 0 entries (not extracted).

### 9. Light-rig binary files (both `light_st002_af.fpk` and `_ar.fpk`)

**`.atsh` (5136 B), "AtmosphereSHCoefficients". Verified: header strings, offsets, decoded values.**
- 0x00 `u32 1`, `u32 0x20`, `u32 filesize`, hash. 0x50 type name. 0x70–0x8f: two params `{hash, u32 type 0x1c, u32 value}`: `numDiv = 40`, `numWeathers = 2`.
- 0xB0: `u32 time[40]`, seconds of day, step 2160 (0…84240).
- 0x150: `entry[numWeathers=2][numDiv=40]`, each 30×fp16 = 60 B (2·40·60 = 4800 = rest of file ✓):
  - `sh[9][3]` RGB radiance SH9 (L00, L1-1, L10(up), L11, L2…). Units cd/m²·√(4π); noon L00 = (11296, 13728, 18800).
  - `light[3]` = direct-light illuminance colour. Moon at night = `moonColor` (0.996, 0.988, 0.843) exactly ✓.
- **The fp16 here is non-IEEE:** exponent 31 is used as a normal exponent (no Inf/NaN), saturating at 0x7FFF = 131008. Daytime sun reaches (131008 sat, 118912, 87232) lux ≈ `sunLux` 150000 × transmittance ✓.
- In af and ar both weather slots are identical. ar (cloudiness 0.7) has noon L00 (17648, 18016, 20784), whiter as expected.

**`.pcsp` (65540 B).** Verified layout statistically; consumer not found.
- `u32 0xDEAD0001`, then 128×64 RGBA16F (8192 texels).
- RGB = Rayleigh single scatter, A = Mie.r (A/R ≈ 0.3).
- x = 8 ν-blocks × 16 μ_s cells. The first ~3–5 cells of each block are 0 (sun below horizon); there is a period-16 periodicity.
- y = 64 μ rows: 32 ground and 32 sky, discontinuity at row 32.
- So this is a single-altitude Bruneton inscatter slice (RES_NU=8, RES_MU_S=16, RES_MU=64, RES_R=1). No extracted PS uses these resolution constants (searched `4.6875e-01`). INF: CPU-side use (SH/sun colour per time of day or Enlighten).
- af and ar pcsp are byte-identical. Their Rayleigh/Mie inputs are identical too.

**`.rpd` (af 3,617,888 B / ar 804,000 B), reflection-probe cubemaps. Verified by decoding with Pillow BC6H and viewing faces.**
- Header `u32 3, u32 0x20, u32 filesize, u32 count` (af 63, ar 14), 16 B misc.
- Entries of 32 B at 0x20: `u64 probeNameHash(48-bit), u32 faceSize (32/64/128), u32 maxLod=5, f32 -12.0, u32 dataOffset, u64 stateHash`.
- The data is BC6H (UF16) cubemaps, **face-major** (+X,−X,+Y,−Y,+Z,−Z, each with its own full mip chain).
- Sizes: 32 → 8352, 64 → 32832 = 6 faces × (64…2), 128 → 131040 = 6 × (128…4) + 96.
- `maxLod 5` matches the shader's `lod = rough·5` ✓.
- af: 7 probes × 9 states (9 `stateHash` values, likely time-of-day/weather keys). ar: 7 × 2 states. One state hash (`0x4cc9a7c93b52`) is shared by af and ar.
- Decoded `rpd_probe0_state4_faces.png` (RP_B_Stnd) shows stand / sky(+Y) / crowd(−Y) / pitch(+Z) as expected.
- INF: −12.0 = bake exposure EV (stored ≈ L·2⁻¹²; day values ≈ 0.5–1).

`sky_dome.fmdl` is identical in af and ar (23234 B). Not parsed further.

### 10. CPU-side bindings (live process read-only + Ghidra, follow-up)

Method: live constant buffers were located in pid <pid> by scanning RW memory for float patterns (β_R triple, `0.03,4.0`, froxel `0.03125`) and dumped. Code was read with headless Ghidra; 6 functions were decompiled: `0x141941c40`, `0x141955000`, `0x141939ed0` (thunk, empty), `0x1417f7200`, `0x1417f6410`, `0x1416c5f40`. The property table offsets come from disassembling the TppAtmosphere property registration `0x14193ec00` (`mov r9d, off`).

Caveat: the live process had a **night** rig loaded (sun below horizon, moon active; `skyColorSunScale = 0.2`, `daySkyAmbientScale = 0.4`, `cloudiness = 0`). Values differ from af.txt where the rigs differ; the bindings are what matters.

**TppAtmosphere field offsets (OBS, `0x14193ec00`):** rayleighHeightScale `+0x1f0`, mieHeightScale `+0x1f4`, mieAbsorptionRate `+0x1f8`, mieAnisotropy `+0x1fc`, mieAnisotropyOfCloudySky `+0x200`, sunLux `+0x23c`, moonLux `+0x240`, starLight `+0x244`, skyLightSunScale `+0x248`, skyColorSunScale `+0x24c`, daySkyAmbientScale `+0x250`, cloudiness `+0x264`, skyLightLuminanceScale `+0x268`, influenceOfFog `+0x210`, numBands `+0x284`, β_R `+0x70`, β_R cloudy `+0x80`, β_M `+0x90`, β_M cloudy `+0xa0`, groundColor `+0x130`, moonColor `+0x140`, sunColorExtinctionOfClearSky `+0x150`, sunColorOfCloudySky `+0x160`. A live entity was found at `0x262f0120` (vtable `0x142a7aa40`).

**Sun/moon intensity (OBS, `FUN_141941c40` + `FUN_141955000`):**
- The manager copies sunLux, moonLux, skyLightSunScale and starLight, and copies skyColorSunScale to a light-param object at `+0x98`.
- `FUN_141955000` computes the horizon fade from `e = dot(sunDir, up)`. Up is `(0,-1,0)` (`0x1429dc110`), so the stored dir points from the light.
```
fadeSun  = e < -0.137444 ? sat(1 - (-0.137444 - e)/0.00518455) : 1      // ~0.3° fade band
fadeMoon = e < -0.142629 ? sat(1 - (-0.142629 - e)/0.0103569)  : 1
I_sun  = sunLux  * fadeSun            -> mgr+0x1c0
I_moon = moonLux * (1 - fadeMoon)     -> mgr+0x1c4
SceneAtmosphereParameter(+0x110) = dir of active light (sun if I_sun>0 else moon)
SceneAtmosphereParameter(+0x1c4) = I_active * π * skyColorSunScale     // sky/atmosphere light intensity
SceneAtmosphereParameter(+0x1cc) = starLight * π * skyColorSunScale
```
- Live check: `SAP+0x1c4 = 0.62832 = 1(moonLux)·π·0.2(skyColorSunScale)` ✓. The live Sky_Draw / constant_sky `cb4[3].w = 0.62832` ✓ and `cb4[7].x = 0.62832` ✓ (= starLight·π·0.2; this confirms the night-glow term is starLight).
- **So the sky shader's light intensity = lux·π·skyColorSunScale (sky units), × `cb2[23].z` (exposure).** Day (af): 150000·π·1.0 = 471239.
- **Answer to "+0x1c0" (`11, 0.62832, 0.27, 0.62832`):** `.y` = active-light intensity (lux·π·skyColorSunScale) and `.w` = star intensity, both OBS. `.x = 11` and `.z = 0.27` are not written by `FUN_141955000` and remain unknown. 0.27 also appears as Sky_Draw `cb4[6].w`, the light-A intensity with dir (0,1,0) at night.
- `cb2[23]` live = `(9.4993e-5, 4.0, 3.7997e-4, 0)`. `.z` = exposure multiplier (log2 = −11.36, inside gameMin/MaxExposure −14…−7); `.x = .z/4`.
- **The atmospheric-fog material sets `cb4[3].w = 1/cb2[23].z`** (live 2631.78 × 3.7997e-4 = 1.00000004; it tracked an exposure change, 2376 earlier). So aerial-perspective fog is **exposure-invariant**: the inscatter-table radiance (unit sun) is output directly as display-referred value. OBS.

**Precompute cbuffer source (OBS, `FUN_1417f6410`, PrecomputeSkyDx11):**
- Builds `cb4[0..3]` from an atmosphere param block; the flag `param[0x75]` selects the clear or cloudy set.
- `cb4[0] = (β_R, H_R)`, `cb4[1] = (β_M, H_M)`, `cb4[2] = (g, 1 - mieAbsorptionRate)`, `cb4[3].x = scattering order`.
- The block's layout matches the live SceneAtmosphereParameter `0x242a3110 + 0x10`, value for value. INF high on the base; the values are OBS:

| set | β_R | H_R (km) | β_M | H_M (km) | absorption | g |
|---|---|---|---|---|---|---|
| clear | 0.0058/0.0135/0.0331 | **4.0** (= rayleighHeightScale, verbatim) | 0.004 | **1.2** (= mieHeightScale/1000) | 0.1 | 0.8 |
| cloudy | …OfCloudySky | **8.0** (no fox2 field → engine constant) | 0.05/0.05/0.03 | **3.0** (engine constant) | 0.1 | 0.2 |

→ "rayleighHeightScale 4 vs 8" is resolved: the clear tables use 4 km verbatim. 8 km is the engine's cloudy-sky constant and the fog's hard-coded value. `multiScatteringOrder` live = 3.

**Draw-time Mie constant (OBS, live):** Sky_Draw / fog / constant_sky have `cb4[2].x = 0.9584` = Schlick `k = 1.55g − 0.55g³` for g = 0.8 (1.24 − 0.2816). So the draw phase `(1-k²)/(4π(1-kν)²)` is Schlick's Henyey–Greenstein approximation (Fox's `mieAnisotropy` = HG g).

**Atmospheric fog cb4 (live, OBS values):** `[0]=(β_R, 0.98)`, `[1]=(0.1,0.1,0.1, 1000)`, `[2]=(0.9584, 40, 1.25, 0)`, `[3]=(sunDir, 1/exposure)`, `[4]=(2.0, 0.0, 200, 1000)`, `[7]=(0.62832, 0.001, 0.26633, −265.333)`. Bindings:

| cb4 | value | fox2 field | status |
|---|---|---|---|
| [1].w | 1000 | objFogRgbAmpFuncScale | OBS value match |
| [1].xyz | 0.1 | objFogRgbAmp.rgb × objFogRgbAmp.a (1×0.1) | INF high |
| [2].y | 40 | startDepth | OBS |
| [4].x / [4].z | 2 / 200 | objFogHeightAmpMax / …MaxHeight | OBS |
| [4].y | 0 | whiteness | OBS (value) |
| [4].w | 1000 | rangeDepthNear (ramp breakpoint) | OBS |
| [7].y | 0.001 | fogAmpNear / rangeDepthNear = 1/1000 | OBS (arithmetic) |
| [7].z | 0.26633 | (fogAmpFar − fogAmpNear)/(rangeDepthFar − rangeDepthNear) = 799/3000 | OBS (arithmetic) |
| [7].w | −265.333 | fogAmpNear − [7].z·rangeDepthNear (continuous at the breakpoint) | OBS |

So the fog opacity weight is:
```
x = zView - startDepth
w = x < rangeDepthNear ? sat(x*fogAmpNear/rangeDepthNear)
                       : sat(fogAmpNear + (x-rangeDepthNear)*(fogAmpFar-fogAmpNear)/(rangeDepthFar-rangeDepthNear))
```
With af values, w reaches 1 at ≈1000 m and fogAmpFar = 800 only saturates. In Godot this is `fog_depth_begin = startDepth`, `fog_depth_end = startDepth + rangeDepthNear/fogAmpNear`, `fog_depth_curve = 1`.

**Global fog (OBS, live cPSLight `0x1bfc0640`):** `cb3[7] = (0.03, 4.0, 0.00025, 4000)` = (globalFogDensity, globalFogHeightScale, 1/rangeDepthFar, rangeDepthFar). Density and height scale are fox2 af values verbatim, so `Fog_DrawVolume`/`Sky_DrawClouds` use them in **km⁻¹** (density 0.03/km, height falloff 4/km: e-fold 250 m). Godot: `fog_density = 3e-5`, `fog_height_density = 0.004` (per metre).
- Other cPSLight rows: `cb3[1] = (8340, 7668, 5720)` is the sky-light colour (constant_sky / clouds `cb3[1]·cb2[23].z`). `cb3[3] = 1.4974e6 ×3` is unknown. `cb3[4]` = camera pos, `cb3[5]` = view dir.
- `cb2[27]` (fog colour in Fog_DrawVolume / DeferredRendering) was live (0,0,0) at night. `cb2[24] = (0.00625, 0, 0, 0)` → the DeferredRendering linear fog is disabled (max clamp 0).

**Froxel volume (OBS, live cPSScene):** `cb2[26] = (1.0675, 15.312, 0, 127)` → **128 exponential slices**, `slice = 15.312·ln(z)`. Slice 127 ↔ z = e^8.29 ≈ 3990 m = far plane 4000 ✓. `cb2[25] = (0.123047, 0.125, 0.03125, 0.25)` → atlas of 4 rows × 32 slices. Godot: `volumetric_fog_length = 4000`, `volumetric_fog_detail_spread` ≈ 2 (exponential).

**pcsp consumer:** found the resource class `tpp::ef::atmosphere::PrecomputeSkyFile` (RTTI `0x14368f9b8`, vtable `0x142a78d68`, type string "PrecomputeSkyFile"/"pcsp" at `0x142a78c60`, registered from `0x14193a0c0`) and `PrecomputeSkyDx11` (vtable `0x142a34028`). The load/use path was not traced: the vtable slot `0x141939ed0` is a thunk into an unmapped/packed region, and no texture creation with 128×64 was found. Still Open.

**Godot sky energy (updated):** `sky radiance = inscatter·phase·(sunLux·π·skyColorSunScale)·exposure`. With Godot's physical light units this means `PhysicalSkyMaterial.energy_multiplier ∝ skyColorSunScale` and `DirectionalLight3D.light_intensity_lux = sunLux` (the π factor is Bruneton's irradiance → radiance convention).

### Killed hypotheses
- **"atsh = Bruneton precompute tables"**: killed. 5 KB is SH9 × 40 times × 2 weathers.
- **"atsh entry = 28 halves (9 RGB + pad)"**: killed. The period is 30 halves; the 3 extra halves are light colour.
- **"atsh NaNs are garbage"**: killed. They are saturated extended-exponent halves of a monotonic sun-illuminance curve.
- **"pcsp = runtime `inInscattering`"**: killed. The runtime table is 256×(4·128) with MUS 32 / MU 128, and pcsp is 128×64 with MUS 16 / MU 64.
- **"rpd entries carry a 96-B header"**: killed. Block data starts at `dataOffset` and the extra bytes are the tail mips.
- **"rpd is mip-major"**: killed. Face-major gives the +Y = sky face.
- **"Sky_Draw Mie phase = Henyey–Greenstein / Cornette–Shanks"**: killed for the draw/fog passes. They use `(1-g²)/(4π(1-gν)²)`. CS is only in precompute and SunColor.
- **"fog uses the cbuffer β/H"**: killed. Hard-coded constants equal Bruneton defaults with H_R = 8.
- **"SH ambient evaluated per pixel from coefficients"**: killed. Coefficients are baked into an equal-area sphere-map atlas (SH_SphereMap) and sampled by SSLighting_SH_SkyLight.

### Open
1. Remaining fox2→cbuffer INF rows: Sky_Draw `cb4[5].xyz = 0.18` (below-horizon colour; live 0.18, not groundColor 0.5), `cb4[6]` light A (dir (0,1,0), 0.27 at night), `SAP+0x1c0 = 11` and `+0x1c8 = 0.27`, `cb3[3] = 1.4974e6`, fog `cb4[2].z = 1.25`, `cb4[5] = (0.005,0.005,0.01,0.005)`. Bound so far: sun/moon/star intensity, precompute cb, fog ramp, objFog*, global fog.
2. Why cloudy-sky H_R = 8 / H_M = 3 are not data-driven. They come from the param block, but where it is filled was not traced.
3. Froxel fog injection shader (density/scatter into `inFogVolume`) is not in the extracted PS set (likely compute).
4. pcsp consumer: the class is identified (PrecomputeSkyFile), but its load path sits behind a thunk to unmapped code. Not traced.
5. rpd: the meaning of the 9 state hashes (which hour/weather) and the exact role of −12.0 were not verified. Only an 8-bit Pillow decode was used (no float BC6H decoder available), so the absolute radiance scale was not checked.
6. Blend states (fog ONE/INV_SRC_ALPHA, probe weighting) are not present in DXBC.
7. `Sky_DrawClouds` elevation→v mapping constants `cb4[5]` and `TppSky.diffusion` were not tied to fields.
8. FxShaders: none extracted, so no FX rain/snow particles were analysed.

# Part C — post chain (sub-agent PostChainRE)

## Post chain (HDR→LDR): exposure, bloom, tonemap, grading LUT, AA/sharpen, display

Sources: `<scratch>/ShaderMaterialRE/asm/*.asm` (vkd3d), RDEF via `rdef.py` (this dir), exe strings from `PES2021.exe` (`exe_strings.txt`, `strings -t x`, file offsets), fox2 dump `<scratch>/LightingRE/af.txt`.
Scripts (this dir): `rdef.py` (cbuffer/binding dump), `lutcheck.py` + `lutfit.py` (LUT numeric checks), `wb.py` (white-balance matrix, self-checking).

**Cbuffer names.** RDEF gives no field names for any post shader. Every cbuffer is one opaque array: `cPSObject{g_psObject[18]}` (cb5), `cPSSystem{g_psSystem[5]}` (cb0), plus `cPSScene` (cb2) and `cPSMaterial` (cb4) (OBS `rdef.py`). So every field meaning below comes from how the code uses it. Field→fox2 links are INF unless they say otherwise.
- `cb0[2].zw` = 1/render-target size in every pass: `uv = SV_Position.xy·cb0[2].zw` (OBS).
- `cb5[11].zw` = maximum UV (a viewport sub-rect clamp) wherever it appears (OBS: always used as `min(uv, cb5[11].zw)`).

### 0. Pass order

Profiler markers (OBS exe strings):
- `TonemapFilter` @0x2a377e0 is followed by `Generate Bloom Texture`, `Generate Luminance Texture`, `Tone Mapping and Bloom Composition`.
- `AntiAliasing` @0x2a1f0d8 is followed by `FXAA`, `Temporal Pass`, `Cancel Temporal Pass`, `Sharpen Filter`, `Depth Upsampling`, `Update Tiled Velocity History`.
- Plugins: `GrPluginTonemap` (fields `Exposure`, `EnableBloom`) and `GrPluginColorCorrection` (with `GrColorGradingParameter`).

The shader names themselves are not plaintext in the exe, so the order below is **INF** (marker order + data dependencies):

```
HDR scene (already pre-exposed, §1)
 ├─ [Generate Luminance Texture]  Tonemap_Lum  |  Tonemap_Lum_Lacc (isLightAdaptationFromLACC)
 │     → reduction + adaptation: no shader found → CPU or another pass (INF)
 ├─ [Generate Bloom Texture]      (DownSampleColorQuarterSize?) → TonemapBrightpass → blur_kawase ×N
 │                                 (blur_kawase_add = same PS, additive blend) → blur_kawase_copy (×weight)
 ├─ [GrPluginColorCorrection]     DrawLut_* / BlndLut_* → 256×16 LUT strip (when grading changes)
 ├─ [Tone Mapping and Bloom Composition]  one of Tonemap / Tonemap_Chromatic_Aberration / Tonemap_1DLUT /
 │                                 ColorCorrection[_Chromatic_Aberration|_Hdr|_1DLUT] / ColorCorrection_CubeLut
 │                                 → display-referred (gamma 1/2.2), ×0.25 when a display pass follows
 ├─ [AntiAliasing]  FXAA | TemporalAA (+CancelTemporalAA) → SharpenFilter (+film grain)
 └─ GammaControl (SDR) | HdrDisplayMapping (HDR10 PQ)   (both start with ×4 = undo the ×0.25)
```

Why FXAA, TAA and sharpen run after the tonemap (INF high):
- FXAA uses an absolute edge threshold of 0.15 on green.
- Sharpen and grain use `1 − luma`, which only makes sense on [0,1] values.
- TAA's anti-stall nudge is 0.001 ≈ 1/1023, i.e. one step of a 10-bit UNORM target, not a float HDR buffer.

`MotionBlurComposition` placement is not established (INF low: before the tonemap, as in Fox/MGSV).

### 1. Exposure and eye adaptation

**HDR buffer is pre-exposed (INF high).** `DbgView_Light_FalseColor.ps.asm:30-36` decodes the light buffer and divides by `cb2[23].x` to get absolute luminance:
```
L = (rgb·max(16·a, 1)) / cb2[23].x
```
It then takes log10 into decade colours. So the scene is stored as physical × `cb2[23].x`.
- Light, sky and cubemap shaders multiply their emission by `cb2[23].z` (`SSLighting_Sun`, `Primitive_Cubemap`: `o = c·cb2[23].z`; 192 PS read `cb2[23]`).
- No post pass multiplies by an exposure scalar. Tonemap applies only a 3×3 matrix (§3).
- INF: `cb2[23].x` = `2^EV` (EV = `gameMin/MaxExposure` −14…−7, `InitialExposureValue` −14).
  - Sanity check: sunlit grass ≈ 150000 lx·0.2/π ≈ 9.5e3 cd/m², times 2^−11 gives ≈ 4.6.

**Tonemap_Lum** (OBS `Tonemap_Lum.ps.asm`; bindings `inImage` t0, point sampler):
```
uv  = (ndc·(0.5,−0.5)+0.5)·cb5[12].zw + cb5[12].xy        // sub-rect of source
c   = tex(inImage, uv)
lum = max( dot(c.rgb, (0.2125, 0.7154, 0.0721)), 0.001, 32·c.a² )   → o.rgb, o.a = 1
```
- The `32` is a literal. It equals fox2 `game/demoMaxLuminanceValue = 32`; whether the CPU uses that field for anything else is unknown.
- The alpha term is INF: the scene alpha carries a `sqrt(L/32)`-style bright-pixel code. Not resolved.

**Tonemap_Lum_Lacc** (OBS; `inImage` t0 = LACC light buffer, `inDepth` t1). Exe flag: `isLightAdaptationFromLACC` @0x2a3ffa0.
```
uv  = (…same…)·cb5[11].zw
lum = depth(uv) > 0 ? max(dot(lacc.rgb, Y709), 0) : cb5[11].x     // sky/far (reversed-Z 0) → constant
```
- It meters incident light (the buffer before albedo), so pitch colour does not affect exposure.
- It ignores the LACC alpha multiplier (`max(16a,1)` in the debug view), so pixels lit above 1 are clipped (OBS: only `.xyz` is read).

**Adaptation law: OBS, CPU** (`FUN_1416c7930`, called from `PluginTonemap` vtable[8] `FUN_1416c7ba0` once per frame; Ghidra decompile `d2a.txt`). The GPU side is `TonemapFilterDx11` vtable[5] `FUN_141805840` ("Generate Luminance Texture"): `Tonemap_Lum[_Lacc]` → a reduction copied into a double-buffered texture at viewport `+0x5e8[vp+0x660]`. INF: it is read back into `vp+0x410` (rgba, all = Y). `logf`/`powf` are the imports `0x1415a17c8`/`0x1415a17ce`.

Inputs:
- viewport: `vp+0x4c4` EV (current), `+0x4c8` minExposure, `+0x4cc` maxExposure, `+0x4d0` exposureCompensation, `+0x410` Lmeas, `+0x6c4` a 3-point EV→offset table g(), key from `FUN_1416821c0` = (`vp+0x6ac` < −90 ? `vp+0x6b0` : `vp+0x6b8`).
- PluginTonemap: param block at `+0x60` (copies of the viewport fields) and speed at `+0x10c`.

```
EV0 = clamp(EV, minEV, maxEV)
Lc  = Lmeas · 2^−(g(EV0) + comp)                     // comp = vp+0x4d0 (−0.33 live), g ≡ 0 live (table all zero)
tgt = clamp( EV0 + 0.5·( log2(key + 0.001) + c2 − log2(Lc + 0.001) ), minEV, maxEV )   // c2 = block+0x74 = 0
Δ   = tgt − EV0
step = (1 − 0.98^(60·dt)) · speed · min(|Δ|, 0.5) · Δ  // dt = (double)tod+0x120 (1/60 live); speed = plugin+0x10c
EV  = (|step| < |Δ|) ? EV0 + step : tgt              // FUN_141683180 stores EV at vp+0x4c4, previous EV at vp+0x65c, Lmeas copy at vp+0x420
plugin+0x108 = Lc                                      // debug/readout
```
- The fixed point is Lc = key, i.e. **Lmeas = key·2^comp** (averaged pre-exposed luminance). Positive compensation brightens.
- The target only takes half the log error per frame (×0.5). The step is quadratic in Δ for |Δ| < 0.5 EV.
  - At 60 fps with speed 10: per-frame step = 0.02·10·min(|Δ|,0.5)·Δ, so a 2 EV error closes at 0.1 EV/frame.
  - It is slow near the target: 0.002 EV/frame at Δ = 0.1.
- **Live** (viewport 0x2195f10):
  - EV = −11.010, min −14, max −5, comp −0.33, Lmeas 0.15629, key 0.18.
  - Lc = 0.1967, so the target is −11.07 (near equilibrium).
  - `speed = 10`. INF high: this is `PesPluginSettings.tonemapSpeed` (st002 fox2 says 30; the live st009 rig is unread).
- INF high: `cb2[23].x = 2^EV` (Lmeas is measured on the pre-exposed buffer, and the loop only converges if exposure scales Lmeas by 2^EV). Not observed directly.
- `maxLuminanceValue` (32) and `initialExposureValue` are not read here. INF: 32 is the shader literal in `Tonemap_Lum`, and initial EV is applied on `ResetAdaptation`.
- The reduction type (arithmetic vs log mean) is in the GPU copy chain and was not decoded.

**Godot mapping**
- `CameraAttributesPractical.auto_exposure_enabled = true`.
- `auto_exposure_scale` = key·2^comp: 0.18·2^−0.33 = 0.143 for the live comp −0.33.
- `auto_exposure_speed`: Godot uses exponential smoothing, not Fox's quadratic step. Exact behaviour needs a script driving `CameraAttributesPractical.exposure_multiplier` with the law above, fed from a compositor luminance readback.
- `auto_exposure_min/max_sensitivity`: map the EV clamp [−14, −5] through Godot's ISO→luminance conversion (not verified).
- Godot meters the final colour, not LACC. Metering from LACC (sky = constant) needs a compositor pass.

### 2. Bloom

**TonemapBrightpass** (OBS):
```
b = min( max(scene(uv) − cb5[9].x, 0), 128 )      // per channel, subtractive threshold, cap 128
```
The output is not divided by exposure, so the threshold acts on pre-exposed values. **OBS** (`FUN_141804af0` = TonemapFilterDx11 vtable[4], "Generate Bloom Texture", `d1a.txt:177-312`): `cb5[9].x = block+0x84` = `PluginTonemap+0xe4` ← viewport `+0x4e4` (live 0.3). INF high: this is `bloomBrightnessExtraction`.

**blur_kawase / blur_kawase_add** (OBS: byte-identical PS):
```
p   = SV_Position.xy·cb5[12].xy                     // dest pixel → source UV
o   = 0.25·cb5[12].z · Σ tex(clampUV(p + (cb5[9].{x|y}, cb5[9].{z|w})))   // 4 taps: (x,z),(y,z),(x,w),(y,w)
```
- The `_add` variant differs only in blend state (INF: additive accumulation into the bloom target).

**blur_kawase_copy** (OBS): `o.rgb = tex(p)·cb5[12].w`, a final scale.

**CPU loop** (OBS, same function):
```
s = block+0x80 (vp+0x4e0, live 0.3);   w = block+0x88 (vp+0x4e8, live 0.3)
if (0.1 ≤ s < 0.2)  → one pass with zero offsets (plain copy)
else N = max(1, (uint)F(10·s))         // F = import 0x1415a1876 (round/floor; INF), live N = 3; st002 0.8 → 8
for i in 0..N−1:
    cb5[9]  = (−(i+0.5)·tx, +(i+0.5)·tx, −(i+0.5)·ty, +(i+0.5)·ty)    // Kawase offset grows by 1 texel per pass
    cb5[12] = (tx, ty, i==0 ? w : 0.999999, 0.999999·w/N)
    kawase pass → temp, then (i>0: additive blend) copy/accumulate into the bloom target
```
- Bloom is skipped when `|w|`, `|threshold|` or `|vp+0x4d8|` < 1e-5, or when a global enable (`DAT_143514544`) is 0. That flag also zeroes w.
- INF high: `bloomSize` = vp+0x4e0, `bloomBrightnessExtraction` = +0x4e4, `bloomWeight` = +0x4e8. They are consecutive in both PesPluginSettings and the viewport, and the code semantics match.
- vp+0x4d8 (0.2) and +0x4dc (0.1) are unidentified. Candidates: Fox `bloomWhiteOffset` / `bloomColorCapacity`.
- The exact pass graph (which of `pfVar8[0x12]`/`[0x13]` is kawase vs kawase_add vs copy) is INF medium.

`blur_Gaussian1D_X/Y` (taps −3.5, −1.5, −0.5, +1.5, +3.5 with weights 1,2,4,2,1 /10) has no link to the bloom markers. Excluded.

**Composition** (OBS, Tonemap.ps.asm:12-27). Bloom is added unweighted, before white balance and tonemap, with a lateral chromatic split:
```
d = 8·cb5[10].y · cb0[2].zw          // OBS FUN_141804200: cb5[10].y = 0.999999 constant ⇒ always an 8-texel R/B bloom split
hdr = scene(uv) + float3( bloom(uv+d).r, bloom(uv).g, bloom(uv−d).b )
```

**Godot mapping**
- `glow_enabled = true`, `glow_blend_mode = ADDITIVE`, `glow_hdr_threshold` = bloomBrightnessExtraction in Godot's exposure space, `glow_hdr_scale = 1`, `glow_hdr_luminance_cap = 128`, `glow_intensity ≈ bloomWeight` (0.3).
- Use `glow_levels` 1-5 as an approximation of Kawase N = 10·bloomSize passes (N = 3 → levels 1-3; N = 8 → levels 1-6).
- Godot's threshold is a soft knee on luminance, Fox's is a hard per-channel subtract: approximate only.
- The bloom chromatic split needs a compositor.

### 3. Tonemap: UE4 FilmToneMap, confirmed

The code is instruction-for-instruction UE4 `FilmToneMap` (TonemapCommon.ush): sRGB→AP1 (`0.613191485, 0.33951208, 0.0473663323` …), RRT saturation 0.96, log10 via `log2·0.30103001`, toe/straight/shoulder with smoothstep blend, post-desaturation 0.93, AP1→sRGB (`1.70505154, −0.621790707, −0.0832583979` …), AP1 luma `(0.272228718, 0.674081743, 0.0536895171)`.
- No ACES blue-correction or gamut-expand extras (OBS: no other matrices). UE4 bakes this into a LUT; here it is evaluated per pixel.
- Then `pow(1/2.2)`, with no sRGB piecewise curve (OBS).

Code (OBS Tonemap.ps.asm:29-90):
```glsl
vec3 c = mat3(cb5[14].xyz, cb5[15].xyz, cb5[16].xyz) * hdr;      // rows; WB matrix (INF, see below)
vec3 a = max(sRGB_2_AP1 * c, 0.0);
a = mix(vec3(dot(a, AP1_Y)), a, 0.96);
vec3 L = log10(a);
float ToeMatch=cb5[12].x, StraightMatch=cb5[12].y, ShoulderMatch=cb5[12].z, Slope=cb5[12].w;
float Black=cb5[13].z, White=cb5[13].w, ToeScale=cb5[14].w, ShoulderScale=cb5[15].w;
vec3 straight = Slope*(L + StraightMatch);
vec3 toe = -Black + 2.0*ToeScale / (1.0 + exp(-2.0*Slope/ToeScale*(L - ToeMatch)));
vec3 sh  = (1.0+White) - 2.0*ShoulderScale / (1.0 + exp(2.0*Slope/ShoulderScale*(L - ShoulderMatch)));
toe = mix(straight, toe, lessThan(L, vec3(ToeMatch)));
sh  = mix(straight, sh, greaterThan(L, vec3(ShoulderMatch)));
vec3 t = clamp((L - ToeMatch)/(ShoulderMatch - ToeMatch), 0.0, 1.0);
if (ShoulderMatch < ToeMatch) t = 1.0 - t;
t = (3.0 - 2.0*t)*t*t;
vec3 tone = mix(toe, sh, t);
tone = mix(vec3(dot(tone, AP1_Y)), tone, 0.93);
vec3 outc = pow(max(AP1_2_sRGB * tone, 0.0), vec3(1.0/2.2));        // display-referred, gamma 2.2
o.rgb = (cb5[10].x == 0.0) ? outc*0.25 : outc;   o.a = scene.a;
```

| cb5 | meaning | fox2 source / value (CPU-derived, INF) |
|---|---|---|
| [9].xy | CA strength / falloff exponent (CA variant only) | OBS `FUN_141804200`: `PluginTonemap+0xc0/+0xc4` ← TimeOfDay `+0x180/+0x184` (live 3, 0.65). CA variant selected by byte TimeOfDay `+0x188` → plugin `+0xbc` (live 0 ⇒ **CA off**) |
| [10].x | 0 ⇒ write ×0.25 (headroom for GammaControl/HdrDisplayMapping, which ×4) | OBS: `(vp+0x650 & 0xf) == 2`. Live vp+0x650 = 0x304f0 ⇒ 0 ⇒ ×0.25 + display pass |
| [10].y / .z / .w | chromatic split amount / 1/.y / extra | OBS: 0.999999, 1/0.999999, `plugin+0xb8` (0.5) |
| [11].xy / .zw | max UV scene / bloom | viewport |
| [12] | ToeMatch, StraightMatch, ShoulderMatch, FilmSlope | OBS: `func_0x14175f430` + slope = block[1]. Spans (0.88/0.55/0.26/0/0.04): **−0.39028, 0.90164, −0.60619, 0.88**; global entity (0.88/0.45/0.55/0/0.02, **live**): **−0.23491, 0.85991, −0.23491, 0.88** |
| [13] | (toe, shoulder, FilmBlackClip, FilmWhiteClip) | OBS: block[2..5] = `acesFilm*`, live 0.45, 0.55, 0, 0.02 |
| [14].w / [15].w | ToeScale = 1+Black−Toe / ShoulderScale = 1+White−Shoulder | 0.45/0.78 (spans), 0.55/0.47 (global) |
| [14..16].xyz | 3×3 linear-sRGB matrix | OBS: built by `func_0x14175f590` (not decompiled). Live value in TonemapFilterDx11+0x10 (below) |

- The `acesFilm*` names and defaults match UE4 (`FilmSlope/Toe/Shoulder/BlackClip/WhiteClip`). ToeMatch etc. are the UE4 CPU formulas (OBS shader structure; values computed by `lutcheck.py`'s `film()` from the same equations).
- Global-entity params have toe + shoulder = 1, so ShoulderMatch = ToeMatch. The blend divide is x/0: the GPU gives ±inf, saturate gives 0/1, so the result is a hard switch between toe and shoulder at ToeMatch. Handled in `film()`.
- Span params: ShoulderMatch < ToeMatch, so there is no straight segment and the `1−t` flip branch is active.
- **Live (parent memory read): the span has `overrideTonemap=False`, so the global set 0.88/0.45/0.55/0/0.02 is active.** Godot must therefore implement the hard toe/shoulder switch.
- White-balance matrix (INF, UE4 CAT02 formula, `wb.py`; 6500 K with tint 0 gives identity, asserted):
  - tint 0.05: `[[0.99191,−0.02428,0.00032],[0.00127,1.01100,0.00002],[−0.00051,−0.00387,0.97833]]`
  - tint 0.075: `[[0.98781,−0.03678,0.00048],[0.00193,1.01672,0.00003],[−0.00077,−0.00583,0.96741]]`
  - **Live check** (parent's memory read during a match, st009 day span):
    - `TonemapFilterDx11+0x10` holds (rows, 16 B stride) M = `(1.234,−0.0223,0.01165) (0.4398,0.8271,0.06639) (−0.007835,−0.01944,1.679)`.
    - `PluginTonemap+0x64` holds `0.88,0.45,0.55,0,0.02,6500,1`, i.e. the global entity's film params and **tint 1 at runtime**, not fox2's 0.075.
  - Fits (`wb.py` variants, ad hoc):
    - M as stored matches no white balance (best residual 0.44).
    - **Mᵀ ≈ WB_UE4(6500 K, tint −1)·1.00** (CAT02, max residual 0.067).
    - Bradford gives 0.062. A free fit gives `1.036·WB(6674 K, −0.976)`, residual 0.035.
  - ⇒ INF, medium: the matrix is a UE4-style white balance with Fox tint sign opposite to UE4 (Fox +1 = UE4 −1, magenta: R ×1.23, B ×1.68 on the diagonal). It is stored transposed relative to the cb row order (or uploaded transposed). Not reproduced exactly (~5 %), so the CAT/isotherm details differ.
  - **Use the live matrix as-is** (check transpose with one frame of output).
  - Other live floats at `PluginTonemap+0xb8` (`0.5,0,3,0.65,0.18,−14,−7,0,0.2,0.1,0.3,0.3,0.3`) and `+0x108` (`0.7098, 10`) include key 0.18 and EV −14/−7. The rest is unmapped.

**Variants**
- `Tonemap_Chromatic_Aberration` (OBS diff) adds a radial CA on the scene:
  ```
  p = 2·uv·cb5[10].z − 1
  s = cb5[9].x·(|p|²/2)^cb5[9].y·cb5[10].y
  off = p·texel·s
  R = scene(uv+off).r;  B = scene(uv−off).b
  ```
  The split is used only if the CA'd colour (saturated) passes `(r+b) < 5g && g < 2(r+b)`; otherwise the unshifted R/B are kept. So strongly green pixels (grass) and green-free pixels get no fringe. Bloom gets the same ±8·`cb5[10].y` split.
- `Tonemap_1DLUT` / `ColorCorrection_1DLUT` (OBS):
  ```
  v = outc·cb4[0].xyz
  ch_i = tex1D_128(dot(v, cb2[12+i].xyz)·0.9921875 + 0.00390625)
  ```
  This is a 3×3 mix followed by a per-channel 128-entry curve (127/128 scale + 0.5/128 offset = texel centres). INF: a user brightness/display curve.
- `DR_VolFog_TppTonemap` adds nothing to tonemapping (OBS lines 135-170):
  ```
  o = max((albedo·diffuse + spec)·AO·fog.a + fog.rgb, 0)
  ```
  The froxel fog is sliced by `log(depth)·cb2[26].y·ln2 + cb2[26].z` into 32 slices. "TppTonemap" names the MGSV (Tpp) light model whose linear HDR output feeds §3.

**Godot mapping**
- Godot has no UE4 filmic tonemapper (ACES/AgX/Filmic have different toe/shoulder). Exact path:
  - Environment `tonemap_mode = LINEAR`, `tonemap_exposure = 1`, `tonemap_white = 1`; glow and adjustments off.
  - A `CompositorEffect` at `EFFECT_CALLBACK_TYPE_POST_TRANSPARENT` runs bloom-add + WB + film + LUT + grain.
  - It writes `srgb_to_linear(v_fox)`, so Godot's sRGB encode reproduces Fox's display value `v` (Fox writes gamma-2.2 code values straight to UNORM).
- Approximate path: `tonemap_mode = ACES`. The white balance cannot be ignored: the live matrix is strong (B ×1.68). Apply it in a compositor, or bake it into lights/sky.

### 4. Colour grading LUT

**Runtime LUT is a 16³ strip, 256×16 2D** (OBS ColorCorrection.ps.asm:84-104). Input = post-FilmToneMap, gamma-1/2.2 values clamped to [0,1]:
```
v = min(outc, 1)                                        // display domain, gamma 2.2
s = floor(v.b*15);  f = v.b*15 − s
uv0 = (v.r*15/256 + s/16 + 0.5/256,  v.g*15/16 + 0.5/16)       // 0.05859375, 0.9375, 0.001953125, 0.03125
uv1 = uv0 + (1/16, 0)                                          // 0.064453125 = 0.5/256 + 1/16
graded = mix(tex(inColorLUT,uv0), tex(inColorLUT,uv1), f)      // linear sampler; half-texel offsets exact
o = (cb5[10].x==0) ? graded*0.25 : graded
```
- Output encoding is the same gamma-2.2 display value (no conversion after).
- `ColorCorrection_Hdr`: if `m = max(v) > 1`, then `v /= m`, apply the LUT, `*= m` (hue-preserving HDR highlights, OBS).

**Strip generation** (OBS DrawLut/BlndLut). Strip pixel (px, py) encodes `i = (px mod 16)/16`, `j = py/16`, `k = floor(px/16)`:
- `DrawLut_Gen` (identity): `x = sat((i·16/15, j·16/15, k/15)·cb4[0].xyz)`.
- `DrawLut_Tex`: `x = tex3D(inBaseLut, (i, j, k/16)·cb4[0].xyz + 1/32)`. This is a 16³ sampling grid with texel-centre offset 1/32.
- Both then apply a cubic Hermite with end tangents:
  ```
  y = (3x²−2x³) + cb4[1].x·(x³−2x²+x) + cb4[1].y·(x³−x²)      // m0, m1; identity when both = 1
  ```
- INF high: from exe `ColorCorrectionData{textureLUT, startSlope, endSlope, colorScale, showBaseLUT, showFilterLUT}` @0x2a40c18: `cb4[1].x/y` = `startSlope/endSlope`, `cb4[0].xyz` = `colorScale`.
- `_BrightEx` adds:
  ```
  y = pow(max(y,0), 1/cb4[4].w)·cb4[4].x
  y = mix(Y709(y), y, cb4[4].y)
  y = 0.5 + cb4[4].z·(y − 0.5)
  ```
  INF: gamma, brightness, saturation, contrast, matching exe `adjustColorBrightness/Contrast/Saturation` @0x29b0a5c.
- `BlndLut_*`:
  ```
  o = mix(Hermite_A(LUT0), Hermite_B(LUT1), cb4[3].x)
  ```
  - Per-LUT `colorScale` is `cb4[0]` / `cb4[1]`; per-LUT slopes are `cb4[2].xy` / `cb4[2].zw`.
  - `T_T` = two 3D LUTs, `T_G` = 3D and identity, `G_G` = two identities.
  - `Add_*` = the previous strip (2D) blended toward a new LUT, i.e. temporal crossfade between moods (INF).
- The fox2 `ColorGrading` 33³ volumes (`sdr/hdrGame/DemoGradingLut`) are not sampled with 33-size constants by any of the 977 PS (OBS: no `1/33`, `32/33` literals).
  - Only `ColorCorrection_CubeLut` declares a `texture3d g_CubeLut` (t6), and its code never samples it. The shipped code is just `o = sRGB_encode(scene)·(0.25 | 1)` with piecewise 12.92/1.055/2.4 (OBS).
  - **OBS: the CubeLut bytecode is a stub.** Its STAT chunk reports 70 instructions, but SHEX is 656 B (~17 instructions). `ColorCorrection` STAT says 93, matching its 93-line disassembly. So the shipped `ColorCorrection_CubeLut` body does not match its own compile stats. The live process has a `ColorCorrection_CubeLut` name entry (name pool @0x1b78f20), so the technique is loaded.
- **CPU binding (OBS, Ghidra `d1a.txt:866-883`, `d3a.txt`, `d4a.txt`):**
  - Composition (`FUN_141804200`) looks up a `ColorGradingParameter` on the viewport by hash `0xcb716e233439` (`FUN_1416a44b0`).
  - If one is found, it binds that object's `Texture` (+0x28 → handle +0x20) to **t6**. It switches the shader to technique-table slot `+0x74` (instead of `+0x50` plain / `+0x54` CA). It overwrites `cb5[13] = (vp+0x500, vp+0x508, 0, 0)` and `cb5[14] = (300, …)`.
  - Because the film constants in cb5[13..14] are overwritten, slot +0x74 cannot be the FilmToneMap `ColorCorrection` shader. INF high: it is `ColorCorrection_CubeLut` sampling the 3D LUT directly.
  - **Live:** three `ColorGradingParameter` objects with that hash exist (0x23a71390, 0x26a95190, 0x23a71410), all pointing at one `Texture` 0x1e01a660. Its +0x10 qword `0x34093d3d64665f0` looks like a Fox file PathCode (INF), i.e. a loaded `.ftex`, not a render target. **So in matches the 33³ grading LUT is bound directly to t6 and the strip path is bypassed** (INF high).
  - The real CubeLut body (stubbed in the extracted pack) therefore holds the whole HDR→LDR transform. That explains why the LUT bakes a full tone curve (grey 0.021→0.69, H5: ~14-EV log-like input domain) and why no FilmToneMap bake fits (H2–H4).
  - Exact domain/encoding: Open 2.
- `ColorGradingParameter` vtable[14] (`FUN_1416a4530`) does the same lookup. If found, it binds slot 6 and returns, so DrawLut/BlndLut are skipped.
- Otherwise the strip builder `FUN_1416a40b0` runs DrawLut/BlndLut with:
  - `cb4[0]` = colorScale (`ColorCorrectionData+0x30`, default from the object's +0x70; identity `DAT_142552690` = (1,1,1,1));
  - `cb4[1].xy` = startSlope/endSlope (+0x40);
  - `inBaseLut` = `ColorCorrectionData.textureLUT`.
  - `cb4[4]` = (1, vp+0x504, vp+0x508, vp+0x500) = (brightness 1, saturation, contrast, gamma). The `_BrightEx` variants run when vp+0x510 ≠ 0.
  - Live vp+0x500.. = gamma 1.0, saturation 1.0, contrast **1.06**, enable 1.
  - No `ColorCorrectionData` (hash `0xcdec1515e022`) exists in live memory (qword scan: 0 hits).

**Numeric LUT-domain check** (`lutcheck.py`, `lutfit.py` on `lut_s_day_game.npy`). `lut_s_day_game.ftex` and `lut_h_day_game.ftex` are byte-identical (same md5, OBS).
- Grey axis (k=0…32): 0.0210 … 0.4194 (k=10) … 0.6182 (k=16) … 0.6898 (k=32). Max is 0.69, alpha is 1.
- Results:

| hypothesis | result |
|---|---|
| H1 identity grade on the strip domain (L(t)≈t) | **killed**: max\|L−t\| = 0.31 |
| H2 LUT = FilmToneMap(x(t)) with fox2 params, output gamma2.2 / linear / sRGB, x log-spaced (invert film per entry, check log2 x is linear in k) | **killed**: residual 1.29–3.36 EV, implied x only 0.003…0.4 |
| H3 UE4 LUT log domain `x = 0.18·2^(14(t−444/1023))` + film | **killed**: max err 0.33 (gamma) / 0.42 (linear) |
| H4 film(t^2.2·s)^(1/2.2), best s | **killed**: max err 0.17 |
| H5 any scaled film: `g·enc(film(2^(a t+b))) + o`, 4 free params | fits to 0.012–0.021 with a ≈ 13–14 EV/unit, b ≈ −6, g ≈ 0.63–0.76. **Not discriminative**: any smooth sigmoid fits with 4 params. Notably, the 14-EV span equals UE4's LUT log range. |
| H6 shader path as observed (33³ → strip with colorScale = 1, slopes = 1; input = film^(1/2.2)) | gives grey: scene 0.18 → 0.596, 0.5 → 0.665, 1 → 0.676, ∞ → 0.688. Everything above scene 0.5 collapses into a 0.02 band. **Implausible for gameplay**, so the runtime `colorScale`/slopes or the consuming path differ (Open). |

- Conclusion: the strip's input domain is **OBS**: post-FilmToneMap, gamma 2.2, [0,1].
- The 33³ grading LUT is **not** a FilmToneMap bake for either fox2 parameter set (H2–H4 killed). Its own input domain is unresolved. Its shape is consistent with a ~14-EV log-encoded scene input that already contains a filmic curve and a 0.69 output gain (H5, weak).

**Godot mapping**
- `Environment.adjustment_enabled = true`, `adjustment_color_correction` = a Texture3D. Godot applies it to the clamped tonemapped colour in sRGB encoding (INF; check against the Godot tonemap shader).
- Re-bake the 16³ strip G (gamma-2.2 domain) for Godot as:
  ```
  G_godot(v) = srgb_encode( pow( G( pow(srgb_decode(v), 1/2.2) ), 2.2 ) )
  ```
- In the exact compositor path, sample the strip exactly as above instead.
- Do not feed the 33³ volume to Godot as-is until its domain is known (H6).

### 5. AA, sharpening, grain

**FXAA** (OBS Fxaa.ps.asm): FXAA 3.11 Quality, PC path.
- Luma = green (gather `.y`).
- Edge threshold 0.16666 relative, 0.15 absolute minimum (`max(lumaMax·0.16666, 0.15)`). That is a high minimum, so few edges are processed.
- Search steps 1.0, 1.5, 2, 4, 12 = `FXAA_QUALITY__PRESET 12`.
- No sub-pixel term (end-of-edge offset only: `max(0.5 − d/len, 0)`) ⇒ `fxaaQualitySubpix = 0`.
- Godot: `screen_space_aa = FXAA`. Godot's FXAA is not configurable and adds sub-pixel blur, so it is softer than Fox's.

**TemporalAA** (OBS TemporalAA.ps.asm; textures colour, prevColour, velocity, depth, tileVelocityHistory, noReprojectionMask, prevAlpha):
- Velocity is taken at the nearest of 5 depth taps (centre + 4 diagonals; larger reversed-Z wins), minus jitter `cb5[10].zw`.
- The current sample is unjittered with a sharpened bilinear: `frac' = smoothstep((frac−0.2)/0.6)`.
- Neighbourhood: centre + 4 taps at ±(1.2, 0.4)·texel and ±(0.4, −1.2)·texel. From them, μ and σ.
- **Asymmetric variance box**: `[μ − (2.25−m)σ, μ + (1.25 + 4(1−m))σ]`
  - `m = max(noReprojMask, sat(‖max(tileVel, 3|v|)‖ − 0.02))`
  - Static: [μ−2.25σ, μ+5.25σ]. Moving: ±1.25σ.
  - The history is **clipped** toward the box centre (ray–AABB, not clamped).
- History: bilinear, then de-blurred with the current-frame cross taps using weights `0.6·f(f−1)` per axis (negative bicubic-like lobe).
- Blend toward current:
  ```
  α = sat( min(t_clip, cb5[12].z)·(0.9 + 0.1m) + 0.8·(0.45·min(|v|/8,1) + 0.4·min(66.7·m'·|Δdepth|,1)·sat(1.5 − prevDepth)) )
  ```
  - `prevDepth` = min of 3 prevAlpha taps (alpha holds depth/127).
  - Then `out = mix(out, current, cb5[10].y)`. This is the base current weight, i.e. the minimum feedback; the value is a CPU constant.
  - Then ±0.001 anti-stall (10-bit LSB). Alpha out = depth/127 when |v| ≥ 0.2, else the previous alpha.
- Godot: `use_taa = true`. Godot's TAA uses its own clamp/feedback, so this is an approximation; exact behaviour needs a custom TAA.

**CancelTemporalAA** (OBS): colour pass-through. `o.a = sat(round(cb2[21].z/(dmax − cb2[21].w))/255)` = linear view depth of the nearest of 5 taps, quantised to 8 bits. It provides depth history where TAA is skipped.

**SharpenFilter** (OBS): exactly SweetFX **LumaSharpen** + film grain.
```glsl
vec3 blur = 0.25*(tex(uv±(1.2,0.4)t) + tex(uv±(0.4,−1.2)t));          // t = cb5[9].zw
float edge = |A(uv−p1)−A(uv+p1)| + |A(uv+p2)−A(uv−p2)|;              // A = inAlphaTexture.r
float ac = A(uv);
float k  = cb5[13].x * max(0.25*ac − 0.25, 1.0 − 128.0*edge*sat(1.05 − ac));   // mask-aware strength
vec3  coef = k * vec3(0.108426, 0.364752, 0.0368220) * (0.5/cb5[13].y);      // = 0.51·Rec709·strength
float sl = sat(dot(vec4(c − blur, 1.0), vec4(coef, 0.5)));
sl = 2.0*cb5[13].y*sl − cb5[13].y;                                        // clamp ±sharp_clamp
vec3 s = c + sl;
float n = hashNoise(pixel >> int(cb5[14].z), cb5[14].w) * 2.0 − 1.0;       // two-frame lerp by frac(time)
o.rgb = s + s*n*pow(max(1.0 − dot(s, vec3(0.2126,0.7152,0.0722)), 1e-4), cb5[14].y)*cb5[14].x;
```
- Fields: `cb5[13].x` = sharp_strength, `.y` = sharp_clamp; `cb5[14].x` = grain amount, `.y` = shadow bias of the grain, `.z` = grain cell shift, `.w` = time.
- `inAlphaTexture` is a mask (INF: UI/alpha-preservation or a player mask). Where it has edges, sharpening is suppressed.
- Godot: no luma sharpen outside FSR `fsr_sharpness`, and no grain. Use a compositor pass with the code above.

**MotionBlurComposition** (summary, OBS): outputs the half-res blurred colour, sampled with point snap `floor(pos·cb5[9].x)+0.5`.
```
α = sat((|v_px| − 3)·0.1 + 0.25) · min(f(a_mb), 1)
f(a) = n·0.02, with n = 100(a + 0.05); if n > 50, n = 2n − 50
```
Composited by blend state. `motionBlurConvolutionLevel 2` is CPU-side. Godot: none (custom compositor if wanted).

### 6. Display stage: gamma, HDR, vignette

**GammaControl** (OBS): `o = cb5[10]·pow(|4·x|, cb5[11]) + cb5[9]` per channel including alpha. INF: the options `brightnessSetting` / `SetGamma` (@0x2a082c0) drive gain, gamma and offset. Godot: `adjustment_brightness/contrast` approximate it, or use one line in a compositor.

**HdrDisplayMapping** (OBS):
```
lin = |4x|^2.2
c2020 = M · lin,   M rows (0.627404, 0.329282, 0.043314), (0.069097, 0.919540, 0.011361), (0.016392, 0.088013, 0.895595)
Y = c2020 · cb5[9].x · 1e−4                    // paper-white nits / 10000
PQ = ((0.8359375 + 18.8515625·Y^0.1593017578) / (1 + 18.6875·Y^0.1593017578))^78.84375
```
This is the ST 2084 encode. Godot: only relevant if the HDR10 output path is targeted.

**Vignetting** is not part of the always-on chain. `Primitive_Vignetting` is a vertex-coloured primitive (`o.rgb = 0`, alpha-blended) used by the `cobra::rd::EffectVignetting` screen effect, listed with RainScreen, LensFlare, Confetti, … (exe @0x2a9a0e0, `.?AVEffectVignetting@rd@cobra@@`). Formula (OBS):
```
c = (0.8, uv.y ≤ 0.5 ? 1 : 0)
d = (uv.x ≤ 0.8) ? |uv − c| : 0
a = pow(sat((d/1.28062 − 0.780869)/0.219131), 2·col.z)·col.w      // 1.28062 = |(0.8,1)|, 0.780869 = sqrt(1 − (0.8/1.28062)²)
discard if a < 5e−4
```
Godot: compositor or a CanvasLayer overlay, only for event effects.

**Chromatic aberration** is the `_Chromatic_Aberration` tonemap variants (§3). Whether the stadium build selects them is unknown.

### Killed hypotheses
- The tonemap is generic ACES RRT+ODT or Hill's fit: killed. The code is UE4 FilmToneMap with UE4 parameter names; no ODT and no blue correction.
- The tonemap pass applies exposure: killed. No exposure scalar; only a 3×3 matrix. Exposure is pre-applied in lighting (`cb2[23]`).
- The 33³ LUT is never sampled directly, only via the strip: **revised.** The ColorCorrection strip shader indeed reads a 256×16 strip, but CPU code binds the ColorGradingParameter texture (live: a file texture) straight to t6 and switches to technique slot +0x74 (INF: CubeLut). The earlier conclusion "only via the strip" holds only when no ColorGradingParameter is registered.
- Adaptation is an exponential `1 − exp(−tonemapSpeed·dt)` blend: killed. The decompiled step is `(1 − 0.98^(60dt))·speed·min(|Δ|,0.5)·Δ` toward a half-error target (§1).
- The 33³ LUT bakes FilmToneMap(fox2 params) or the UE4 log-domain LUT: killed numerically (H2–H4).
- The LUT input is sRGB piecewise: killed for the strip. The input is `pow(1/2.2)`; the only piecewise sRGB is in `CubeLut`.
- Bloom uses the Gaussian1D blur: not supported. The bloom-named passes use Kawase.
- `Primitive_Vignetting` is a global post vignette: killed. It is a cobra event-effect primitive.

### Open
1. **Runtime cbuffer values still unread:**
   - `cb4` of the strip passes, `cb2[23]`, Sharpen `cb5[13..14]`, TAA `cb5[10].y`/`cb5[12].z`.
   - The WB matrix builder `func_0x14175f590` and film helper `func_0x14175f430` were not decompiled.
   - vp+0x4d8/+0x4dc (0.2/0.1) are unidentified.
2. **CubeLut body, 33³ LUT input domain and output encoding.** The extracted SHEX is a stub (STAT 70 instructions vs ~17 present).
   - Next step: dump the live D3D pixel shader for `ColorCorrection_CubeLut` (find the shader object for name entry @0x1b78f20 → bytecode), or check whether a patch `.fsop` overrides GrSystemShaders.
   - Also: what `cb5[13] = (gamma 1.0, contrast 1.06)` and `cb5[14].x = 300` mean in that shader.
3. **Luminance reduction type** (arithmetic vs log mean) in the GPU copy chain after `Tonemap_Lum`, and the readback into vp+0x410.
   - The adaptation law itself is closed (§1, OBS).
   - `cb2[23].x = 2^EV` is INF.
   - The EV→offset table vp+0x6c4 is zero live; its source is unknown.
4. **Exact meaning of the alpha term** `32·a²` in `Tonemap_Lum`, and what `inAlphaTexture` is in SharpenFilter.
5. **Default AA**: not established. The `AntiAliasing` plugin's FXAA/TAA selection was not decompiled. The tonemap variant is answered: live = non-CA, colour-grading path (slot +0x74 = CubeLut, INF high), ×0.25 + display pass.

**Ghidra work this round** (headless copy `gh/`; 15 functions decompiled):
- TonemapFilterDx11 vt[1..6]: `141804af0` bloom, `141805840` luminance, `141804200` composition, `141806040`, `141805ff0`.
- PluginTonemap vt[8,9,14]: `1416c7ba0` param copy + exec, `1416c7f30`, `1416c8080`.
- `1416c7930` adaptation; `1416821c0` key getter.
- ColorGradingParameter vt[7,8,14,19]: `1416a4040`, `1416a44a0`, `1416a4530`, `1416a4500`.
- `1416a40b0` strip builder.
- Disassembled by hand: `1416821b0`/`141682130` (3-point table), `141683180` (EV store), `1416a44b0` (hash).
- Live reads: PluginTonemap 0x1ff9c10, viewport 0x2195f10 (located via the `−14,−5` pair at +0x4c8), ColorGradingParameter objects.
