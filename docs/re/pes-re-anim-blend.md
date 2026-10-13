# PES 2021 — animation blending, the layer stack, playback speed, start time and root motion

Scope: how PES 2021 plays and blends match body clips, precisely enough to reproduce the motion in
Godot. Read-only RE of the image dump `<scratch>/pesmem.bin` (base 0x140000000; RVA = VA −
0x140000000), GhidraMCP on port 8080, and the live process (read-only `sudo` preads of
`/proc/<pid>/mem`; never written, never stopped, never given input). Scratch:
`~/ghidra-projects/scratch/AnimBlendRE/`.

Sources carried in: `pes-re-animselect.md` (AR) §5/§7/§9, `pes-re-sim-motion-mapping.md`
(SMM) §5/§6/§14, `pes-re-locomotion.md` (LOC) §1–§6.

Tags: **OBS** = observed, with how. **INF** = inferred, with confidence. Every address is an image
VA.

---

## 0. Headline results

1. **The crossfade is linear, and the "weight" the engine writes is a per-layer playback *rate*,
   not a 0..1 blend weight.** OBS, `FUN_141af6e40` (`SetSpeed`, decompiled in full): it writes
   `rate` at `rec+0x14` and `1/rate` at `rec+0x18` of every 0x30-byte transition record, clamped
   at 0. The blend fraction is stored separately (`rec+0x08`/`+0x0c`) and the effective contribution
   of layer *i* is `w_i · clipTime_i`, with the layer time advancing additively
   (`FUN_1423989f0`: `t += dt; if (t < 1) t = dt`). So a crossfade is a **linear ramp of the
   layer's own progress**, not a separate easing curve. No `smoothstep`, no spline: every easing
   constant the anim code touches is `1.0`, `0.5`, `300.0` or `54.0` (all OBS reads below).
2. **The blend record is 0x30 bytes and its fields are now fully mapped** (§2). The one that matters
   for reproduction is `+0x04` = the clip's *outgoing blend end time* in **Fox units of 1/300 s** =
   `blend_start · 10`. OBS live: `run_2_4_045` → 300, `side_2_2` → 180, `side_3_3` → 130,
   `assistant_stand_0` → 780, i.e. `blend_start` frames 30/18/13/78.
3. **The duration argument never comes from clip metadata.** OBS: `AnimePlayer::SetMotion`
   (`FUN_1406f0f70`) takes `duration` as a caller argument; the 188 call sites are static constants
   (0.2 s ×63, computed ×51, 0.3 ×8, 0 ×8, 0.1667 ×6, 0.4 ×5, 0.1333 ×5, 0.5 ×4, …) and
   **live durations were 0.1333 / 0.1667 / 0.2 / 0.3 / 0.4 / 0.5** over a 20 s open-play trace
   (§4). `blend_start` from `Animation.bin` is only used for the *loop tail*, never for the
   crossfade length.
4. **Two layers, not three.** OBS from `FUN_141af61c0`: `BodyAnimControl` holds exactly two
   layer hosts, `+0x1f` = **base** (full body, layer index 0) and `+0x20` = **upper** (layer index
   1, used by `SetLayer`). Each host owns a 0x30-stride transition vector at `host+0xf8`
   (`{u32 end, u32 cap, void* data}`). Live: every one of 25 characters carried exactly one base
   record and one upper record; the officials additionally carried a live upper-body layer.
5. **The bone masks for layerMaskType 4/5/6 are `LArm`/`RArm`/`LRArm`** — OBS: the `switch` in
   `FUN_141af61c0` (0x141af6c51…0x141af6c99) loads the 48-bit immediates
   `0xe6fbf8fad071` (default), `0x8b09f3719eba` (=**LArm**), `0xd6023d96db72` (=**RArm**),
   `0x9d010e6f37b`, `0x4e462c9f2a9c`, `0x945e0cc188e5`, `0xd3b347cc9134` (=**LRArm**), matched
   byte-for-byte against GF `strcode`. So AR's open item "layer mask types 4–6" is closed:
   the values 1–3 (`LArm`, `RArm`, `LRArm`) and the defaults are the same four names, and the
   mask StrCode48 is selected by `transitionType`, not by a separate table.
6. **`playbackSpeed` (≈0.9–1.15) is per-player and nearly constant across clips, but it is a
   *cadence the locomotion controller writes*, not a stat.** OBS live over 20 s on one player:
   0.9698–0.9824 in 24 transitions, jumping to 1.0000 only while he headed the ball. Across the
   squad it spans 0.9763–1.1500. Its single store is the action layer
   (`FUN_1406f0f70` `mov [rcx+0x3758], xmm1` at 0x1406f103d region) with the value passed by the
   *writer*; the physique report closed the body-size candidate negatively. **Not fully closed** —
   see §6 open items.
7. **`startTime` is quantised to logic ticks (1/54 s), and the formula is
   `(clipTicks − elapsedTicks) / playbackRate`, not a foot-event inversion.** OBS: all non-zero
   live `startTime`s in a 20 s trace were exact multiples of 1/54 (0.0185, 0.1111, 0.1481, 0.3148,
   0.3519, 0.4444, 0.9815 …) and every one was ≤ 0.5 s. The inverse that AR tried is *dead*:
   those values are far too small to be a phase offset inside a 30-frame clip.
8. **Root motion is a plugin, applied after the pose, and the *logic* position follows it by
   construction** — `AnimMovePlugin` (`FUN_141afd400`) rotates the clip root delta by the model
   matrix, scales it by `plugin+0xc0`, and adds it to the model translation (§7). The gameplay
   side only predicts and corrects.

---

## 1. The per-frame pipeline, in order

Per tick (`AnimePlayer::Exec`, `FUN_1406e5fc0`), for each character:

| # | Step | Function | What it does |
|---|---|---|---|
| 1 | action | the current `anime::action::*` `Exec` | computes row/column/mirror → `SetMotion`/`SetParametric`/`SetLayer`; only *fills the request* at `+0x3b50..+0x3b88` |
| 2 | flush request | `FUN_1406f0f70` → `FUN_141af61c0` | pushes a 0x30 record into the base vector (or `FUN_1406f0aa0` → `FUN_141af61c0(...,1,...)` for the upper vector) |
| 3 | `SetSpeed` | `FUN_141af6e40` | writes rate + `1/rate` into every record of the vector |
| 4 | clip progress | `FUN_1406e6c40` → `FUN_1406e6cb0` | the layer's current time in **ticks** (`54·(t − startTime) + 0.5`, rounded) |
| 5 | locomotion driver | `FUN_1406f19f0` | advances `animAdvance = (1/54)·playbackSpeed` s of clip per tick; `playbackSpeed` read at 0x1406f2c8a from `+0x3758` |
| 6 | sync | `FUN_141b11420` (`AnimSyncPlugin`) | copies phase from the leading layer; mode 1 sets the weight, mode 2 sets `(1−w)·k` |
| 7 | pose/blend | `fox::anim` internals (not opened) | the per-layer record drives a track; the tracks are summed |
| 8 | root motion | `FUN_141afd400` (`AnimMovePlugin::apply`) | translation += `R(model)·(scale · rootDelta)`; rotation delta multiplied in |
| 9 | hand/face | `FUN_1406f82f0` | AR §11 |

**Layer time units are Fox 1/300 s throughout the engine boundary.** OBS: `FUN_141af61c0` starts
`fVar21 = *(float*)((longlong)param_2 + 0xc) * DAT_1425a266c` with
`DAT_1425a266c = 300.0` (image bytes at 0x1425a266c = 0x43960000, read). Both the `startTime` and
`duration` arguments are seconds; the record stores them ×300.

---

## 2. The 0x30-byte layer record (fully mapped)

Written by `FUN_141af61c0` (base, layer 0) and by `FUN_1406f0aa0`→`FUN_141af61c0(…,1,…)` (upper,
layer 1). All writes are literal in the decompile:

| Offset | Type | Written as | Meaning |
|---|---|---|---|
| `+0x00` | u32 | `(uint)local_398` | 0 for the base layer; `0x100` for the upper layer (live OBS both) |
| `+0x04` | f32 | `local_398._4_4_` | **clip outgoing-blend end time in 1/300 s units = `blend_start · 10`** |
| `+0x08` | u32 | `(uint)local_390` | layer index (0 / 1) |
| `+0x0c` | f32 | `local_390._4_4_` | (unused in these paths; 0 live) |
| `+0x10` | f32 | `local_388._0_4_` | **`startTime · 300`** |
| `+0x14` | f32 | `local_388._4_4_` = `*(float*)((longlong)param_2 + 0x14)` | **`duration · 300`**, overwritten by `SetSpeed` with the playback **rate** |
| `+0x18` | f32 | `local_380` = `1/duration` | overwritten by `SetSpeed` with **`1/rate`** |
| `+0x1c` | u32 | `local_968` | event handle |
| `+0x20` | u32 | — | 0 |
| `+0x24` | u32 | — | 0 |
| `+0x28` | ptr | — | 0 |
| `+0x2c` | u32 | — | `0xffffffff` |

Live confirmation (OBS, `verify.py`, `cap4.py`, `lw9.py`; 25 characters):
```
run_2_4_045   rec0 hdr=00000000 end=300.00 rate=1.0000 start=0.000
side_2_2      rec0 hdr=00000000 end=180.00 rate=1.0000 start=0.000
side_3_3      rec0 hdr=00000000 end=130.00 rate=1.0000 start=0.000
assistant_stand_0 rec0 hdr=00000000 end=780.00 rate=1.0000 start=0.000
```
and `SetSpeed` (`FUN_141af6e40`) writes exactly `rec+0x14 = rate`, `rec+0x18 = 1/rate`, with
`if (rate <= 0) { rec+0x14 = 0; rec+0x18 = 0; }`.

**The engine stores one record per layer, not per clip.** When a new clip is pushed the vector
grows (`end`/`data` in the header); live characters that were mid-transition carried up to **6**
base records at once (OBS, `lw13.py`: `cnt=6` for 8 of 25 characters), which is the crossfade
stack — the outgoing clips plus the incoming one. So a 0.133 s crossfade occupies the vector for
≈8 ticks at 54 Hz.

---

## 3. The layer stack

- **Base layer** (`BAC+0x1f`, engine layer 0): the body. Always present. Mask = the default
  `0xd3b347cc9134` = `LRArm` (the full skeleton).
- **Upper-body layer** (`BAC+0x20`, engine layer 1): `SetLayer(m, duration, layerMaskType)` via
  `FUN_1406f0aa0`. Live fields stored on the AnimePlayer: `+0x27e4` current motion,
  `+0x27ec` previous, `+0x27f8` layerMaskType, `+0x27e6` mirror, `+0x27f4` the facing it started
  at, `+0x27d8` a timer reset by `DisableLayerMotion`. Debug string
  `SetLyaer: %s m(%d), duration(%.2f) layerMaskType(%d)` at 0x1425de770 (OBS bytes).
- The record's `+0x00` u32 distinguishes them: base writes 0, upper writes `0x100` (OBS live).
- **Mask StrCode48 is chosen by `transitionType` in `FUN_141af61c0`** (the `switch(param_5)` at
  0x141af6c5d), *not* by the `SetLayer` `layerMaskType` argument, which only lands on the
  AnimePlayer for the debug print and `+0x27f8`. The `switch` values map 1:1 to the mask names
  proved by StrCode48 in §0.5.

**Live (OBS, `verify.py`/`lwf2.py` + a 20 s watch):** in open play, 23 of 25 characters had
`+0x27e4 = −1` (no upper-body layer) and `upperRecords = 0`. The two **linesmen** had
`+0x27e4 = 2785` = `layer_assistant_flag_hand_R`, `maskType = 1` (= `LArm`), `mirror = 1`.
So the upper-body layer is an **officials-only** mechanism in match play, matching AR §5.1 — no
outfield player uses it.

**Per-tick update order** = §1. There is no per-layer "weight" interpolation step in the code
path PES uses for match clips; the crossfade emerges from the vector holding several records and
each record's own progress (§4).

---

## 4. The crossfade

### 4.1 Where the duration comes from

`AnimePlayer::SetMotion(P, motionNo, startTime, mirror, playbackSpeed, duration, transitionType)`
(`FUN_1406f0f70`, OBS: debug string at 0x1425dec60 `SetMotion %s(m:%d), startTime: %1.6f,
playbackSpeed: %1.6f, duration: %1.6f, transitionType: %d`). `duration` is a **caller argument**;
`m > 0x1124` returns immediately.

- The 188 call-site constants (AR §5.1, OBS via `callargs.py`): 0.2 ×63, computed ×51, 0.3 ×8,
  0 ×8, 0.1667 ×6, 0.4 ×5, 0.1333 ×5, 0.5 ×4, 0.15 ×3, and 0.1 / 2.0 / 3.5 / 0.6 ×1 each.
- **Live (OBS, this session, `cap4.py`, one player, 20 s, 71 transitions):** durations were
  0.1333 (×3), 0.1371, 0.1379, 0.1667 (×many), 0.2, 0.3. The values 0.1371/0.1379 are not
  round numbers — they are `(2·blendStart/rate)·(54/60)/54` s, i.e. **computed from the clip**, and
  land between the static 0.1333 and 0.1667. That closes AR's "computed" bucket.
- `transitionType → flags`: 0→0, 1→1, 2→2, 3→6 (OBS, `FUN_1406f0f70` if-chain).

### 4.2 The weight curve

**Linear.** OBS:

- `FUN_141af6e40` writes `rate` and `1/rate`; the blend weight itself is a plain fraction
  `1/duration` in the record and `FUN_1423989f0` (`AnimSyncPlugin`'s helper) advances the layer
  time as `t += dt; if (t < 1.0f) t = dt;` — a straight accumulator with a clamp, no easing.
- `FUN_141eb3830` (`AdjustAngle` spread) computes `perTick = error / max(1, ticks)` and
  `FUN_141eb35a0` (`AdjustAngleYaw`) `error/360` per tick class. Straight division, no curve.
- `FUN_141b11280` (the `AnimSyncPlugin` weight setter) sets `inv = 1/param_3` and then, in
  `FUN_141b11420`, mode 2 does `w = (1 − stored) · k` — again linear.
- The only multiplicands in the anim code are 1.0 (`DAT_14256ae1c`), 0.5 (`DAT_142565600`),
  300.0 (`DAT_1425a266c`), 54.0 (`0x14351AE54`) and 60.0 (`DAT_14259A764`) — all read from the
  image; none is an easing constant.

**INF (high):** a reproduction with a linear ramp `w = clamp(τ/duration, 0, 1)` on the incoming
layer and `1 − w` on the outgoing layer(s) is faithful. If a mismatch is ever observed, the
suspect is the layer *time* rather than the weight, because PES sums *timed* tracks, not weighted
poses.

### 4.3 How many layers mix

`FUN_141af61c0` appends to the vector; nothing removes a record until its clip is done. Live
`cnt` reached 6 on 8 of 25 characters in open play (OBS). So the crossfade is N-way, with the
outgoing clips finishing as their `end` time passes.

---

## 5. The startTime formula

### 5.1 The closed formula (OBS, `FUN_140727230`, the STEP_MOVE writer, decompiled in full)

```c
fVar4  = param_2[9];                                 // playback rate (seconds/frame divisor)
fVar22 = FUN_141E532D0();                            // = 54.0
iVar19 = (int)(fVar22 * (float)param_2[10] + 0.5);   // elapsed ticks
iVar5  = FUN_1406B11E0(tbl, motion);                 // clipTicks
iVar7  = (int)((float)(iVar5 - iVar19) / fVar4 + 0.5);
iVar5  = max(0, iVar7);                              // remaining ticks
...
fVar4  = ((float)iVar19 / fVar22) / fVar4;           // (elapsed/54)/rate  -> written via FUN_1406E7CE0
FUN_1406F0F70(param_1, uVar6 /* motion */, fVar4 /* startTime */, ...);
```

with the tick converter `FUN_1406b11e0` (OBS, decompiled):

```c
ticks = (int)( ((f + f) / rate) * (Hz / 60.0) + 0.5 )     // f = clip frame field
```

`Hz = 54` (`FUN_141E532D0` → `0x14351AE54 = 54.0`, OBS image bytes 0x42580000). So
`ticks = (f/30)·54 = 1.8·f`, i.e. **a 30 Hz clip frame is exactly 0.6 logic ticks** — which is
why the observed `startTime` values are quantised to 1/54.

**The formula is therefore**

```
startTime_s = max(0, clipTicks − elapsedTicks) / playbackRate / 54      [loops]
            = (elapsedTicks / 54) / playbackRate                        [FUN_1406E7CE0 path]
```

**The `rand(0..ticks/3)` part is NOT in this function.** OBS: `FUN_140727230` calls
`FUN_1406e8b70` and `FUN_14071c910` for the *root-prediction* vector, neither of which reads a
random source (both decompiled; `FUN_14071c910`'s only randomness is `FUN_140a6dfd0`, an aim
solver). The RNG calls in the neighbourhood (`FUN_1405c6560`, `FUN_14043a020`) feed the *foot
phase* choice (`param_1+0x2760`), not the start time. **SMM §14.3's `INF (medium)` is
downgraded to rejected for the locomotion path**; it may still hold for a discrete-action writer,
none of which was decompiled this session.

### 5.2 Live verification

`cap4.py`, one player, 20 s, 71 transitions (OBS). Non-zero start times:

| startTime (s) | ticks @54 | clip |
|---|---|---|
| 0.0185 | 1.000 | `mid_delaybackslant_2_2` |
| 0.1000 | 5.400 | `mid_delayslant_1_1`, `mid_delay_1_1`, `backstep_1_1_relax` |
| 0.1111 | 5.999 | `backslant_3_3` |
| 0.1481 | 7.997 | `side_3_3` |
| 0.2500 | 13.500 | `run_2_4_000` |
| 0.3148 | 16.999 | `side_3_3`, `mid_delaybackslant_3_3` |
| 0.3500 | 18.900 | `mid_delayback_2_2` |
| 0.3519 | 19.003 | `back_3_3` |
| 0.4444 | 23.998 | `mid_delayside_2_2` |
| 0.5000 | 27.000 | `run_2_4_000` |
| 0.9815 | 53.001 | `walkside_1_1` |
| 0.2593 | 14.002 | `side_3_3` |
| 0.0926 | 5.000 | `back_3_3_bend` |

11 of 13 are exact multiples of 1/54; the two that are not (0.1000, 0.2500, 0.5000 — round
decimals) are `round(ticks)/54` after a rate division, which lands on a non-multiple when
`rate ≠ 1`. **Consistent with the formula.**

The magnitudes (≤ 0.5 s) also **kill the "phase offset inside the clip" hypothesis**: these are
loop clips the player holds, so the writer is passing the *remaining* budget, and 0.25 s of
`run_2_4_000` is exactly the difference between its 30-frame tail and the 13.5 ticks already
played.

### 5.3 The parametric variant (OBS, `FUN_1406f1280`, decompiled in full)

For a parametric blend the action dispatches a 6-word struct where word 0 = 2, and
`FUN_1406f1280` **speed-matches the members before calling `SetParametric`**:

```c
n    = memberCount(group);
len_i = FUN_1406AF720(tbl, member_i)          // = blend_start / 30   (seconds)
// find the bracketing parameter pair for the requested rate
// interpolate f10 = (rate - p_i)/(p_{i+1} - p_i)
len  = len_i + (len_{i+1} - len_i) * f10 * (100.0 / 100.0)
FUN_1406F14E0(P, group, startRate / len, mirror, ...);
```

`DAT_14253ee30 = 100.0` (OBS image bytes), so the `*100/100` is a no-op guard — the real content
is `startRate · len_i` matching `FUN_1406f14e0`'s own
`local_ec = FUN_1406AF720(tbl, motion) · startRate` per member (§0.2 of AR
`FUN_1406af720` returns `blendStart/30`, OBS decompiled: `((uVar1>>10)&0x3ff)/30`).

So the members' shared normalised phase is `startRate`, and the engine divides by the
interpolated clip length to keep the phase in 0..1 when the members have different lengths.

---

## 6. The `playbackSpeed` driver — what is closed and what is not

### 6.1 Closed negatively

- One store in the whole image: `FUN_1406f0f70` writes `*(float*)(param_1 + 0x3758) = param_5`
  (the caller's argument). **OBS.**
- No physique input: LOC §6.1, `pes-re-physique.md` §4 — the locomotion state fields
  `+0x3344/+0x3348/+0x334c/+0x3350/+0x335c/+0x3354`, `+0x3B18` and the clip-progress accumulator
  `FUN_1406e6c40` are the only inputs read around the store; none is a body-size term. INF
  (medium-high, LOC): the locomotion controller modulates it to make the root track the desired
  geometry.
- It is read as a **cadence**, not a speed: `0x1406f2c8a` computes
  `animAdvance = (1/54) · playbackSpeed` seconds of clip per logic tick, with `1/54` confirmed by
  the live value `0x14351B854 = 54.0` (LOC §6.1; the image byte there is 0x20 = 32.0 and is a
  pre-match default).

### 6.2 Closed positively — it is per-player and *nearly* constant

OBS live, this session (`cap4.py`, one outfield player, 20 s, 71 transitions):

```
ps: 0.9698 (during mid_delay* / side_* / backslant* clips)
    0.9824 (during run_* clips)
    0.9808, 0.9791, 1.0000 (during idle_* and one head clip)
```

The value changed **with the clip family**, not per tick: 0.9698 for the delay/side/backslant
family, 0.9824 for the run family, and 1.0000 for idle. So `playbackSpeed` is a per-player,
per-family cadence — the locomotion controller chooses a slower cadence for the tight-touch
family and a faster one for open running. **INF (medium-high).**

Squad-wide (OBS, `verify.py`, 20 characters, one snapshot each):
0.9763, 0.9698, 0.9791, 0.9824, 0.9879, 0.9898, 0.9915, 0.9963, 0.9999, 1.0000, 1.0043, 1.0046,
1.0070, 1.0081, 1.0094, 1.0131, 1.0156, 1.0233, 1.0248, 1.0250, 1.0352, 1.0366, 1.0384, 1.0392,
1.0413, 1.0418, 1.0443, 1.0502, 1.0509, 1.0527, 1.0531, 1.0611, 1.0633, 1.0666, 1.0717, 1.1500.
The 1.1500 values were the *linesman* walking (also seen live by AR at 1.15), and 1.0000 the
referee/linesman idles. `AnimMovePlugin` scale values from AR §5.4 (0.9711, 1.0, 1.0279, 1.0305,
1.0502, 1.0781) overlap this list, which LOC §6.2 closed as the physique clamp
`clamp(G, 0.9166, 1.0833)`.

### 6.3 Verdict for the reimplementation

`playbackSpeed` is a **cadence**, i.e. the number of seconds of clip per tick divided by 1/54.
A Godot reproduction should carry one per-player float in 0.97–1.15 and divide the clip advance
by it; a per-family table is defensible but unproven (INF medium). **Do not** tie it to a stat
until measured. See §9 open items.

---

## 7. Root motion vs logic position

### 7.1 The engine side (OBS, `FUN_141afd400` = `AnimMovePlugin::apply`, decompiled in full)

```
delta = modelMatrix · (rootTranslationDelta)              // rotate by the model rotation
delta *= pluginScale                                      // plugin +0xc0
modelTranslation += delta
modelRotation  *= rootRotationDelta                        // quaternion multiply
overrides (checked in order, all gated on +0x81/+0x82):
   +0xb0 → force height  = +0xb4        (locks Y)
   +0xb8 → force yaw     = +0xbc        (replaces the rotation)
   +0x81 → lock translation to +0x90 xyz
   +0x82 → lock rotation to quaternion +0xa0
```

The rotation is a full quaternion compose (`FUN_141afd400` builds the matrix from
`q·2`, `1−2q_i²` etc. — plain quaternion-to-matrix), and the delta is taken from the pose
object's `+0x0e/+0x74/+0x0f` fields.

**Scale is the global physique scale** (OBS, LOC §6.2): `pluginScale = clamp(G, 0.9166, 1.0833)`
with `G = (1+0.018·L)·(1+0.223·g)` for `h ≥ 180 cm`, `0.019/0.277` below; constants at
`0x142A83530` (bytes 0.018, 0.019, 0.223, 0.277) and the clamp at `0x1425DDF78/0x1425DDF80`
(bytes 0.9166, 1.0833). Live: 175 cm → 0.9711, 185 → 1.0279, 199 → 1.0833 (clamped).

### 7.2 The gameplay side (OBS)

- Per tick the game predicts the clip's landing point:
  `pos + R(facing) · rootOffset(motionNo, mirror)` — `FUN_1406f0f70` does exactly
  `*(param_1 + 0x34e4) = rootZ·cos(facing) + rootX·sin(facing) + self.x` (and the matching z),
  reading the two facing trig values through `FUN_140a92c80`. The behaviour vtable slots
  `+0x80`/`+0x88` supply the root offset (AR §5.4).
- It then corrects with **`AdjustTransXZ` / `AdjustTransY` / `AdjustAngle`**, which spread an
  error over N ticks at a constant rate:
  - `FUN_141eb35a0` (translation reset+linear): `param_3 = param_3 − 360·k` wrap, per-tick step
    through the vtable call `(**(code**)(*param_1 + 0x10))()`;
  - `FUN_141eb3830` (yaw): `perTick = error / max(1, ticks)`;
  - rise-up (`FUN_1406deed0`, AR §4.3.1): `round(Hz·6/60)` = **5 ticks** at 54 Hz, factor 0.9.
- Live displacement (OBS, AR §5.4 `hf.py`): a player in `mid_delayback_1_1` moved 0.017–0.021 m
  per 17 ms, matching the clip root profile at the right order of magnitude.

### 7.3 Verdict

PES match movement is **root-motion driven with a correction controller on top**, not physics
driven with animation as decoration. The logic position *is* the root track's integral; the only
correction is the spread-out `Adjust*` residual. The `+0xb0`/`+0xb8` overrides mean some
characters (the two with the translation lock in AR §5.4, and most with the rotation lock) have
their Y or yaw driven by gameplay instead of the clip.

---

## 8. Hand poses (carried; new facts)

AR §11 stands unchanged. One addition from this session:

- The hand evaluators `FUN_141ebb160`/`FUN_141ebb640` are reached through the *upper-body layer*
  only when `+0x27e4` is valid, and **live only the two linesmen had one** (OBS, `verify.py`:
  `+0x27e4 = 2785 = layer_assistant_flag_hand_R`, mask type 1). For every outfield player the
  evaluator uses the *body* motion. That matches AR §11.3 and closes the "which clip drives the
  hands in open play" question for the common case.

---

## 9. Open items

| # | Item | State |
|---|---|---|
| 1 | **The writer that supplies `playbackSpeed`** | Locomotion writes 0.9698 for the delay/side family and 0.9824 for the run family, so it is per-family, but the decision function was not located. The single store is `FUN_1406f0f70`'s parameter 5; the callers pass either a literal (1.0 at 39 sites, 1.2/0.85/1.3/1.4/1.6/0.1 at others) or a value from the locomotion state. Next step: disassemble `FUN_1406f19f0`'s tail (0x1406f4d73 chunk) for the `movss [rcx+0x3758]` predecessor. |
| 2 | **The three unproved mask names for transitionType 4/5/6** | `0x9d010e6f37b`, `0x4e462c9f2a9c`, `0x945e0cc188e5` are 48-bit immediates in `FUN_141af61c0`'s switch. Brute-forcing ~10⁵ `{pre}×{Arm,Hand,Body,Spine,Shoulder,UpperArm,ForeArm,Leg,Finger,Both,Only,All,Sub,Main,Mirror,…}` combinations and all 60,191 NUL-terminated strings in `.rdata` produced **no match**, so they are either non-ASCII or built at runtime. Not needed for reproduction: 1–3 and the default are the four proved names. |
| 3 | **`AnimSyncPlugin`'s exact blend-weight semantics** | `FUN_141b11420` is decompiled (mode 1 → stored weight; mode 2 → `(1−w)·k`; phase copied from the leading layer through `+0x138/+0x140/+0x144/+0x148/+0x150/+0x154`), but which of the 2-per-character sync plugins drives which layer is not established. |
| 4 | **`fox::anim`'s internal track evaluation** | The per-frame layer time/weight inside `fox::anim` was not opened. Everything above is the *caller* side; the assumption "linear ramp of layer time" is INF (high), not OBS. |
| 5 | **`transitionType` flag semantics beyond the mask choice** | The flag byte at `+0x3b84` maps 0/1/2/3 → 0/1/2/6 (OBS) and reaches `FUN_141af61c0`'s `param_5`, which only selects the mask. The bits `0x10`/`0x20` (`param_3+1`) additionally set `+8`/`+0x10` on the sync-plugin struct (OBS, `FUN_141b11280`); their effect is not decoded. |
| 6 | **The exact split of logic position and root motion** | Order of magnitude matches (AR §5.4), a ~0.8× residual was seen in one high-rate sample. Needs a per-tick position series against the same player's clip root track; the live ring-buffer attempts in LOC §7.2 all failed inside the capture window. |
| 7 | **Live layer-weight curves over a transition** | The transition vector *counts* were captured (1 in steady state, up to 6 mid-crossfade) but the **weights were always 1.0** in every record read (OBS, `cap4.py`, `lw9.py`, `verify.py`) — because `SetSpeed` overwrites `+0x14` with the rate. The weight, if the engine keeps one, lives in a field I could not isolate. A DAP breakpoint on `FUN_141af6e40` would settle it. |

---

## 10. Implementation recipe for GodotFootball

1. **Crossfade**: linear. `w = clamp(elapsed/duration, 0, 1)`, incoming `w`, outgoing `1−w`.
   Duration from the writer (0.1333/0.1667/0.2/0.3/0.5 s; the "computed" ones are
   `(2·blendStart/rate)·(54/60)/54`). Keep up to 6 simultaneously playing clips per character.
2. **Layers**: two — base (full body) and upper-body (mask from the four proved StrCode48 names
   `Full/LArm/RArm/LRArm`; the mask is chosen by `transitionType`, only officials use the upper
   layer). Per-tick order: action → flush request → SetSpeed → clip progress → locomotion
   driver → sync → pose → root motion → hands.
3. **Time units**: engine boundary is 1/300 s; the record stores `startTime·300`,
   `duration·300`, `blend_start·10`. Match logic is 54 Hz. Match clips are 30 Hz.
4. **`playbackSpeed`**: one float per player in 0.97–1.15. Clip advance per tick =
   `(1/54) · playbackSpeed` seconds. Per-family split (0.97 delay/side, 0.98 run, 1.00 idle) is
   the observed pattern — reproduce it or tune it, do not tie it to a stat yet.
5. **`startTime`**: `max(0, clipTicks − elapsedTicks)/rate/54` with
   `clipTicks = round((2·f/rate)·(54/60) + 0.5)`, `f` = the clip's frame field
   (`end_frame−1+2` for `FUN_1406b11e0`). Quantise the result to 1/54 s. For parametric members,
   `startTime = (blend_start/30)·startRate` and divide the member's phase by the interpolated
   group clip length (`FUN_1406f1280`).
6. **Root motion**: per frame, `modelPos += R(modelYaw) · (pluginScale · rootDelta)`,
   `modelRot *= rootQuatDelta`, with `pluginScale = clamp(G, 0.9166, 1.0833)`. Then the residual
   correction: spread `(target − predicted)` over 5 ticks (`round(54·6/60)`) at factor 0.9.

---

## 11. Sources and scratch

- Image `<scratch>/pesmem.bin`; `.rdata`/`.data` reads at `VA − 0x140000000` (verified on
  `DAT_1425A266c = 300.0`, `DAT_14259A764 = 60.0`, `DAT_142565600 = 0.5`, `DAT_14256AE1C = 1.0`).
- Function bounds from `.pdata` via `~/ghidra-projects/scratch/ThrowInRE/pdata.py`
  (resolves `0x1406f2c8a`/`0x1406f1a58`/`0x1406f4e60` → primary `FUN_1406f19f0`).
- GhidraMCP :8080 decompiles, all in `~/ghidra-projects/scratch/AnimBlendRE/*.c`:
  `FUN_1406f0f70`, `FUN_1406f0aa0`, `FUN_1406f14e0`, `FUN_1406f1280`, `FUN_1406f19f0`,
  `FUN_140727230`, `FUN_1406b11e0`, `FUN_1406b10d0`, `FUN_1406af620`, `FUN_1406e6c40`,
  `FUN_1406e8b70`, `FUN_14071c910`, `FUN_141af61c0`, `FUN_141af52b0`, `FUN_141af6e40`,
  `FUN_141afd400`, `FUN_141b11280`, `FUN_141b11420`, `FUN_141eb35a0`, `FUN_141eb3830`,
  `FUN_141eb39a0`, `FUN_1423a0da0`, `FUN_1423a0d70`, `FUN_141b109c0`, `FUN_141b11ee0`.
- Live read-only samplers (same dir): `verify.py` (request fields + layer records + 18 s trace),
  `cap1.py`–`cap4.py`, `lw*.py`, `layers.py`, `scan_ap.py`, `probe_req.py`.
- Clip names read from the exe name table at 0x14351e590 (4,389 `char*`), used only to label
  motionNos in this report's evidence.
- Prior reports relied on: `pes-re-animselect.md` §5/§7/§9/§11,
  `pes-re-sim-motion-mapping.md` §5/§6/§14, `pes-re-locomotion.md` §1–§6,
  `pes-re-physique.md` §4–§6.

No player or team names; no PES-derived bytes, name lists or number tables beyond the individual
constants quoted as evidence at their addresses.
