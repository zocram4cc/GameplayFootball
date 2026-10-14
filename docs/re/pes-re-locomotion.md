# PES 2021 player movement kinematics, as formulas driven by attributes

Scope: how PES 2021 turns player attributes (Speed, Acceleration, Dribbling, Balance,
Stamina, height/physique) into per-tick player displacement, and how the animation
playback parameters (per-player `playbackSpeed`, `AnimMovePlugin` scale) are set.
Sources: the image dump `pesmem.bin` (base 0x140000000; file offset = VA − 0x140000000),
GhidraMCP on port 8080, the VFS reader (`build/apps/pesfmt_dump`), the live process
(read-only `/proc/<pid>/mem` preads) and the earlier reports in this folder.

Tags: **OBS** = observed (decompile, raw bytes, or a live read, with the method stated).
**INF** = inferred, with a confidence. Every address is a VA in the image.

## 0. Headline result, and why it is negative

**PES 2021 has no player speed formula.** The literal per-tick displacement of a player is
produced by an **animation root track**, scaled by three multiplies, and steered by a
discrete clip-selection state machine. The attributes enter that pipeline only as **(a)**
the multipliers of the live-stat pipeline (`FUN_14092AF10`, §2) and **(b)** the tier
buckets that pick a speed class. There is no `v = f(Speed)` curve anywhere in the image.

Concretely, OBS:

- the per-tick displacement read by the game itself (`FUN_140a57450`, §6) is the difference
  of two positions held in a ring buffer;
- the value that displaces the player is the root delta of the current clip, times the
  plugin scale, times `playbackSpeed` (`FUN_1406f2c8a`, §5.4);
- the only "speed constant" the locomotion code ever compares against is the raw per-tick
  displacement scaled into km/h (×54×3600/1000, §3.1), bucketed by
  `{2.5, 8.0, 13.5, 22.0}` km/h (`FUN_1406deb30`, table at `0x14344E250`, OBS bytes:
  2.5, 8.0, 13.5, 22.0).

So the correct model for GodotFootball is **speed-class selection + parametric clip
blend**, not an acceleration curve. §3, §5 and §6 give the exact machinery; §7 lists what
is still open and what a live validation would need.

## 1. Time base and units (OBS)

- `GetGameSpeed()` = `FUN_141E532D0` → the float at **0x14351AE54 = 54.0**.
  Image constant read: `0.14351AE54` = 54.0.
- Tick `DAT_14351AD20` = **0.01851851866 = 1/54 s** in-match. Read live in-match and from
  the image dump (both 1/54; the on-disk file default is 1/48 and is *title-screen only* —
  see `pes-re-injury-fatigue.md` §7).
- **km/h → m per tick** (`sub_1404C8DC0` as used by `FUN_1407f7370` line 214
  `((gameSpeed·60·60)/1000)·|dxz|`): the factor is
  `k = (f·60·60)/1000` with `f = 54.0`, `60.0` at `0x14259A764`, `1000.0` at
  `0x14256AE34` → **k = 194.4**, i.e. `km/h = 194.4 · metres_per_tick`.
  Verified numerically in `FUN_1407fba90` (`0x1407fba90` line 18) and `FUN_1407f7370`
  (line 214). So a displacement of 1 m per tick = 194.4 km/h = 54 m/s, as expected.
- All speeds below are quoted in km/h, converted with `×3.6` to m/s only where useful.

## 2. How attributes reach movement (OBS, complete)

### 2.1 The pipeline

`FUN_14092AF10` (the per-frame live-stat writer, one instance per player) recomputes each
**in-match ability byte** as

```
live = clamp( base × A1 × A2 × A3 × A4 × A5 , min, max )
```

where, OBS from the decompile (`<scratch>/stat.txt` lines 466–494):

- `base` = the ability byte at `lVar17 + 8 + idx` (the `0x128`-byte in-match block,
  `matchlogic` §1.3; bytes `+0x14..+0x25` and `+0x27..+0x2D`);
- **A1** = 1.0 always (unknown, array at `param_1+0xC798`);
- **A2** = condition arrow (array at `param_1+0xC888`, filled from `0x142632D70`
  when `iVar29` — the tier index — is in 0..4 and ≠ 2);
- **A3** = position familiarity (array at `param_1+0xC978`, filled from `0x142632E68`
  via `FUN_1408CAFA0(block+8, local_1c4)`);
- **A4** = super-sub (array at `param_1+0xCA68`, filled from `0x142632F74`);
- **A5** = live stamina drain (array at `param_1+0xCB58`) — the only remaining writer of
  `+0xCB58` was not located (see §8, open item O1).

The three `.rdata` modifier tables, read from the image (OBS bytes):

| table | VA | values | tier map (idx → tier) |
|---|---|---|---|
| condition arrows | `0x142632D70` | 1.12, 1.06, 1.00, 0.92, 0.84 / 1.09, 1.05, 1.00, 0.94, 0.88 / 1.06, 1.03, 1.00, 0.94, 0.88 | `0x142632CC0`: `20,21,22,34,35,36,37,42,43 → 0`; `23,24,26,27,28,29,30,32,33,39,44 → 1`; `40,41,45 → 2` |
| familiarity | `0x142632E68` | 0.80, 0.92, 1.00 / 0.82, 0.94, 1.00 / 0.84, 0.96, 1.00 | `0x142632EB0`: `20,21,22,34,35,36,37,42,43 → 1`; `23,24,26,27,28,29,30,32 → 0`; `40,41,45 → 2` |
| super-sub | `0x142632F74` | ×1.064 (`0x3F883127`) for 21,22,34,35,36,37,42,43,39,44,40; ×1.048 (`0x3F80074C`) for 41,45; ×1.03 for 23,24,26,27,28,29,30,32,33 | active when the player's match-period value is 1..4 |

Clamp table `0x142613C60` (OBS bytes; 60 × {u8 min, u8 max}): abilities clamp to
**40..99**, height 132..220, weight 30..129, age 15..63, form 0..7.

Note the *index* identity caveat from `matchlogic` §1.3: Speed/Jump (0x27/0x2C) and
KP/Acc (0x2A/0x2B) are the two pairs the `.rdata` modifier tables can never separate — the
labels rest on the third-party listing. Everything below depends on 0x27 = Speed and
0x2B = Acceleration being right; see open item O4.

### 2.2 Live confirmation of the pipeline

Read live (OBS, sudo pread, `MATCH_FROZEN` spell, ctx = `0x3B870010`,
rosters at `*(ctx+(t+0xA3)*40)` = `0x1E149408` / `0x1D5677F8`): the 22 starters carry
flat ability bytes (77 / 82 / 88 / 93 / 99 per player), heights 175–191 cm, weight 80,
ages 15–23. Team 0: heights 189, 185, 185, 175, 185, 175, 175, 185, 185, 185, 175 cm with
Speed=Acc={77,77,77,82,77,82,93,99,88,99,93}. Team 1: 189, 191, 191, 175, 175, 180, 180,
180, 185, 185, 185 cm with Speed=Acc={77×6, 88×3, 99×2}. So the live match gives a spread
of Speed 77…99 and height 175…191 cm — enough for a peak-speed comparison (§6) but not a
*curve* fit (only 4 distinct Speed values).

## 3. Speed

### 3.1 The intended design and its evidence (INF, informed)

The parametric locomotion blend is the carrier of "speed". Group 80 is the canonical plain
run, members `walk_1_1` (rate 0), `run_1_1` (0.15), `run_2_2` (0.3), `run_3_3` (0.5),
`run_4_4` (0.9), `run_5_5` (1.0) (`animselect` §2.6, OBS).

Natural root speeds of the members, measured by GF's `fk_pose` on PES clips at 30 fps
(`animselect` §4.3.3, OBS): walk **1.36**, run_1 **1.15**, run_2 **3.08**, run_3 **3.95**,
run_4 **7.47**, run_5 **8.96** m/s. The engine interpolates linearly between bracketing
parameter points (INF: the lerp itself was not read), so the **blend rate → speed** curve is
piecewise linear with knots

```
r:  0.00   0.15   0.30   0.50   0.90   1.00
m/s: 1.36   1.15   3.08   3.95   7.47   8.96
km/h: 4.90   4.14  11.09  14.22  26.89  32.26
```

Live mapping (OBS, `animselect` §4.3.3): rate 0 ↔ 1.4 m/s, ≈0.3 ↔ 3.1 m/s,
0.5 ↔ 3.9–4.6 m/s, ≈0.8 ↔ 6.5–7.7 m/s — i.e. `rate ≈ speed / 8.96` holds to the
measurement noise (INF, high, `sim-motion-mapping` §3).

### 3.2 The top-of-scale number and its relation to attributes (INF, medium)

There is **no** Speed→km/h coefficient in the image. The closest thing to a ceiling is
`run_5_5`'s 8.96 m/s = 32.26 km/h and the speed-class upper bound 22.0 km/h
(`FUN_1406deb30`). Cross-check: the throw-in band (`throwin.o`, `FUN_14081c640`) ships
`baseMinKph 35`, `baseMaxKph 50` — of the same order, which supports the reading that
8.96 m/s is a clip property and not the game's speed cap for a 99-Speed player.

Therefore, for a Speed attribute, the only defensible statement is:

```
v_top ≈ 8.96 m/s (clip-limited)              [the ceiling, independent of Speed]
```

and Speed's contribution must be *measured*, not derived: that measurement is the missing
live fit (open item O2).

### 3.3 Walk / jog / run / sprint / dribble — the literal curves

There are no literal curves. What exists is a **five-way speed class** and the blend that
the class selects.

**Speed class** — `FUN_1406deb30`, table `0x14344E250` (OBS bytes 2.5, 8.0, 13.5, 22.0):

```
class = 0 (still)  if km/h <  2.5
        1 (walk)   if km/h <  8.0
        2 (jog)    if km/h < 13.5
        3 (run)    if km/h < 22.0
        4 (dash)    otherwise
```

Speeds are converted as `km/h = metres_per_tick · 54 · 3600 / 1000` (OBS,
`animselect` §4.2). Class 5 exists only as `run_5_5`, the top of the speed blend.

| class | member | natural m/s | natural km/h |
|---|---|---|---|
| 0 | (still) | 0 | 0 |
| 1 | `walk_1_1` | 1.36 | 4.90 |
| 2 | `run_1_1`/`run_2_2` | 1.15 / 3.08 | 4.14 / 11.09 |
| 3 | `run_3_3`/`run_4_4` | 3.95 / 7.47 | 14.22 / 26.89 |
| 4 | `run_5_5` | 8.96 | 32.26 |

**Dribble** is a separate family, not a slower run: `DRIBBLE` (id 4) has its own Animetable
rows and its own parametric group; `pesSmart.o` carries `dribbleTired 2` (OBS,
`matchlogic` §3.5). No dribble-speed coefficient was recovered (open item O3).

## 4. Acceleration and deceleration

### 4.1 There is no explicit acceleration variable (OBS)

Searching the locomotion path, the only per-tick terms are the clip root delta and the
`AdjustTransXZ/Y/Angle` corrections. **No `accel`/`decel` float is read on the movement
path.** The Acceleration ability (in-match index 0x2B) reaches the sim only through the
modifier tables of §2.1 (tier 0 in all three: condition ×~1.0–1.12, familiarity
×0.80–1.00, super-sub ×1.064) and through the tick-level gates that decide when a
*transition clip* starts.

### 4.2 What "acceleration" actually buys (OBS, tick gates)

Transition clips are single animations; the tick budget for them is computed from clip
event frames:

```
ticks = ( (2·f / playbackSpeed) · (Hz/60) + 0.5 )            [FUN_1406b10d0, f = frame]
ticks = ( (2·f / playbackSpeed) · (Hz/60) + 0.5 )            [FUN_1406b11e0, f = other frame]
```

with `Hz = 54` (`FUN_141E532D0`) and `60.0` at `0x14259A764`. `0.5` is `0x142565600`,
`1.0` is `0x14256AE1C`, `2.0` is `0x142565604`. So a clip's start window is
`f/30 s` worth of logic frames (OBS, `animselect` §1): faster `playbackSpeed` → fewer
frames in the window → **quicker response**, which is the physical meaning "Acceleration"
has in PES.

### 4.3 The stop model (OBS)

The brake is a clip too:

- `FUN_1406f19f0` (`<scratch>/pbs.txt`, the locomotion update) maintains a state machine with
  constants `0.14` (`0x1425CFA1C`), `0.13` (`0x1425CB298`), `0.12` (`0x1425CB294`),
  `0.11` (`0x1425CB28C`), `0.03` (`0x1425E4A08`), `0.0015` (`0x1425E49F8`),
  `0.00075` (`0x1425E49F4`), `0.2` (`0x142598578`), `0.25` (`0x142592228`),
  `0.06` (`0x1425CC238`), `0.01` (`0x14256AE18`), `5.0` (`0x14259222C`),
  `0.15` (`0x1425C48A0`), `0.05` (`0x1425A8A28`), `0.1` (`0x142588200`).
  All OBS reads from the image. The state variable is `param_1+0x3330` (a per-tick
  accumulator) with the threshold `+0x3B18` metres/tick; below
  `0.3B18` the player is "at rest" and the code selects the stop clip.
- `cancel_speed` per clip (json `anim_infos.json`, median 4.04 cm/frame = 1.21 m/s,
  `sim-motion-mapping` §7) is the speed PES itself carries past the cancel point, which
  is the clip-level deceleration contract: use `cancel_speed · 30 / 100` m/s as the
  cross-check (INF from the json value distribution).
- `AdjustTransXZ` spreads position error over `round(Hz·6/60)` ticks (OBS,
  `animselect` §4.3.1) — 5 ticks at 54 Hz — with factor 0.9.

## 5. Turning

### 5.1 The turn-rate calculation (OBS, closed)

`FUN_14081b7B0` writes the desired yaw into an action state. Its arithmetic, re-read from
the decompile:

```
dx = target.x - self.x ;  dy = target.y - self.y
r       = hypot(dx, dy)                                  # FUN_1415A1780
scale   = clamp(r / 5.0, 0, 1)                           # 0 if r ≤ 0, 1 if r ≥ 5 m
rate    = 24000.0 / (54.0 · 3600.0)                      # 0.12346 rad per "unit"
yaw     = atan2(dx, dy) → wrapped to 0..2π → degrees
apply   = FUN_140A8F7C0(&pos, yaw, rate · scale)
```

Constants OBS: `24000.0` at `0x1425E5B3C`, `3600.0` at `0x142598590`, `5.0` at
`0x14259222C`, `54.0` from `FUN_141E532D0`, and `57.2958` (`0x14259BE40`, rad→deg) in the
yaw conversion. The result: **0.1235 rad/tick ≈ 6.67 rad/s ≈ 382°/s at 54 Hz** as the
asymptotic yaw rate, scaled to zero as the target gets closer than 5 m. This is a
**geometric aiming rate, not a player turn limiter** — it is how an AI face-target
converges, and it does not read any attribute.

### 5.2 The turn bucket that drives locomotion transitions (OBS, closed)

`FUN_14081E460(id)` reads a table at **0x1425EF1E8** of 16 floats (OBS bytes):

```
0, 0, 0, 0, 45, 90, 135, 180, 0, -45, -90, -135, -180, 0, -180, -168.75
```

`FUN_14081E2F0(a)` buckets a wrapped angle into 5 windows from **0x1425EF2B8**
(11 floats: −180, −157.5, −112.5, −67.5, −22.5, 0, 22.5, 67.5, 112.5, 157.5, 180) and
maps them to action ids from **0x1425EF2E8** (12 ints: 12, 11, 10, 9, 8, 3, 4, 5, 6, 7,
17, 16). So the **turn buckets for locomotion are 0/45/90/135/180°, mirrored on the
negative side** (digits 5..9 of the Freemove key, `sim-motion-mapping` §2.2, OBS over all
252 motions and 408 states).

The Freemove key is solved and closed:

```
state = [family][speedIn][sep][style][speedOut][sep][angleDigit]
key   = state·10 + turnDigit      →  (motionNo, mirror, nextState)
```

- family 0 run/walk/stop/idle/near, 1 delay/side-loop, 2 parallel, 3 side, 5 back;
- speedIn/speedOut 0 still .. 4 dash;
- style 0 run/plain, 1 walk, 2 parallel, 3 side, 4 rotate-180, 5 back;
- angleDigit 0..4 = +0/45/90/135/180°, 5..9 = the mirrored side.

`Freemove.bin` = 3,874 × 12 B, key-sorted, 408 distinct states, 252 distinct motions,
62 terminal rows (`next = 0`). `FreemoveOption.bin` (105 × 8 B, 96 distinct keys) is a
secondary disambiguation for the step logic, not a new clip source — all 105 of its
motions are also reachable through the main table (OBS).

### 5.3 With-ball vs off-ball difference (OBS, structural)

The ball carrier runs a different action id: `*(AnimePlayer+0xAC8) = 4` = `DRIBBLE`
(0x1407fa2c0 line ~340 sets `*param_5 = 0` for action 4), while the off-ball player is in
`MOVE` (2) or `STEP_MOVE` (3). The turn request for the carrier is computed against a
**rotated frame**: `FUN_1407FA2C0` rotates the target by `−angle` (`0x14259858c` = 360.0
wrap, `0x142598580` = 180.0) through `FUN_1404583B0`/`FUN_14046F230` before bucketing —
i.e. the pick is made in the body frame, and the *same* 0/45/90/135/180 buckets apply.
Off-ball, the same function gates on `+0x6B3` ("near the ball") and `+0x680 = 1` to swap
to the `near_*` locomotion states.

**No Agility or Balance term appears in the turn path.** Both abilities exist in the
roster (0x21 Tight Possession at block 0x17 and 0x29 Balance at block 0x29 — both in the
"physique" group of the modifier tables) but the locomotion turn never reads them; the
only consumers found are the physical-contact/duel and stagger paths. The absence of an
Agility/Balance term in turning is **OBS** (decompiles of `FUN_1406f19f0`,
`FUN_1407FA2C0`, `FUN_14081B7B0`, `FUN_14081E2F0`, `FUN_1407F7370` read in full).

## 6. The playbackSpeed driver and the AnimMovePlugin scale

### 6.1 `playbackSpeed` (OBS closed negative, then re-closed positive)

- `+0x3758` on the AnimePlayer is the playback speed, set by `FUN_1406f4E60`
  (`mov [rcx+0x3758],xmm1` at `0x1406f5860`, then `FUN_141AF6E40(BAC, speed, 0, -1)`).
- It is read at **`0x1406f2c8a`**: `xmm6 = (const at 0x14256B81C) / xmm0` then
  `× *(rsi+0x3758)`, where `xmm0 = FUN_141E53CD0()` = the float at **0x14351B854**.
  OBS the value at `0x14351B854` is **54.0 live** (and 0.0 in the image, which is the
  pre-match default), so in-match the factor is **1/54**.
  → `timeStep = (1/54) · playbackSpeed` seconds of clip per logic tick
  (OBS read of the constant and the multiply).
- The image shows **one** store site (`FUN_1406f19f0` region, `0x1406f1a58`), whose
  inputs are the locomotion state fields `+0x3344/+0x3348/+0x334c`,
  `+0x3350/+0x335c…`, `+0x3B18` (metres/tick), `+0x3354`… and the clip progress
  accumulator `FUN_1406e6C40` (`(fVar4 = 54.0) · (elapsed − startTime) + 0.5`), plus
  `FUN_141E532D0()`. The full expression at `0x1406f2c8a` is

  ```
  animAdvance = 0.006/elapsed · playbackSpeed        # where elapsed is the clip-progress
                                                      # counter in ticks from FUN_1406e6C40
  ```

  INF (medium-high, from the surrounding state machine): `playbackSpeed` is a
  **per-player, constant-across-clips cadence** that the locomotion controller modulates
  to make the root-motion track the desired geometry, not a body-size term.
- **Physique doc closed the alternative negatively** (OBS, `pes-re-physique.md` §4):
  `playbackSpeed` has exactly one store in the whole image and its inputs are the action
  layer (`+0x3B54`, `+0x3B88`, …), never the physique object → *cadence does not scale
  with body size*.
- **What sets it then** — the honest answer: **not found**. Live values observed at
  0.9–1.15 per player, constant across clips (`animselect` §6/§9, OBS live
  `SetMotion … playbackSpeed %1.6f` debug prints; the static sites are 1.0, 1.2, 0.85,
  1.3, 1.4, 1.6, 0.1). My candidates were Speed/stamina; the physique report's "shape"
  candidate is dead. Open item O5.

### 6.2 `AnimMovePlugin` scale (OBS closed)

`FUN_141AF7840` region / `And::AnimMovePlugin` apply (`FUN_141AFD400`):

- `AnimePlayer+0x960[i]` = absolute bone length scales (table A x component, parent
  rule) — **NOT** the plugin scale; it is the scale of *parent(i)* (OBS,
  `pes-re-physique.md` §6 killed hypothesis).
- `AnimePlayer+0xA00 = clamp(absS[0], 0.9166, 1.0833)` (constants `0.1425DDF78` /
  `0x1425DDF80`, OBS bytes 0.9166 and 1.0833), and `absS[0] = G·1` for the root
  `root_main` (map at `0x1437F4E60` = {1..19, 0}, OBS).
- `FUN_141AF6E40(BAC, speed, 0, -1)` is the only other store, and it writes the plugin's
  speed, not its scale.

So the **root-motion scale is the global physique scale G, clamped**
(closed, `pes-re-physique.md` §4/§5.1):

```
G = (1 + k_L·L) · (1 + k_h·g)
k_L = 0.018 (L ≥ 0) / 0.019 (L < 0)      # L = leg-length slider, -1..1
k_h = 0.223 (g ≥ 0) / 0.277 (g < 0)
g   = (h − 180)/40   (h ≥ 180 cm)
      (h − 132)/48 − 1   (h < 180 cm)
pluginScale = clamp(G, 0.9166, 1.0833)
```

Constants at `0x142A83530` (OBS bytes: 0.018, 0.019, 0.223, 0.277) and clamp
`{132, 219.999}` at `0x142A83540`. Live values (OBS, 25 plugins, `pes-re-physique.md`
§5.1): 175 cm → 0.9711, 180 → 1.0, 185 → 1.0279, 194 → 1.0781, 199 → 1.0833 (clamped),
180 cm + leg_len nibble 0 → 0.981. Max error to the reference implementation: 1.2e-7.

**Consequence: stride grows with height, up to ±8 %.** For GodotFootball: root-motion
displacement must be multiplied by `clamp(G, 0.9166, 1.0833)` where G comes from the same
physique function the skeleton uses.

## 7. Live validation

### 7.1 What was done

Read-only (sudo preads of `/proc/<pid>/mem`, no writes, no ptrace, no input — the sampler
script is `~/ghidra-projects/scratch/LocomotionRE/samp.py`).

Play state was `OPEN_PLAY` (`STATE=OPEN_PLAY ctx=0x3b870010 step=0.018519`, verified with
`pes_state.py`). Captured:

| item | result |
|---|---|
| per-player stat blocks (height, weight, abilities) | OBS, 22 starters, §2.2 |
| per-tick displacement family | OBS formula, `FUN_140a57450`: `rec[n] − rec[n−1]`, base `*(ctx+0x40) + 0x88`, stride `0x28`, `n < 0xFA`; ball is `n = 0` via `p+0x2948/0x294c/0x2950 ÷ 54.0` |
| head-of-buffer displacement | sampled at ~200 Hz for 20 s: mean per-sample Δ 0.0100 m, path length 59.87 m over 20.0 s → **mean speed 2.99 m/s = 10.8 km/h** (a moving entity's trail) |
| high-rate structural scan | 9.4 M samples in 25 s of the buffer head: only **1,295** position changes → the array is **not** a per-tick position array; it is a ring/scrub buffer whose head is written only on real moves |

### 7.2 Why the per-player peak-speed comparison is **not** reported as a numeric error

The array at `*(ctx+0x40) + 0x88 + n·0x28` was probed as "one record per player
(0..21)", which produced the tell-tale linear ramp in per-slot `max` displacement
(0.70 → 10.02 m/tick across slots 1..24, i.e. +0.45 m/tick per slot). That ramp is a
**geometric artefact of the buffer layout**, not 22 players' speeds: the records at
`p+0x88 + i·0x28` are 40-byte entries of one entity's *history* (the head moves,
everything else is stale), and `FUN_140a6cf70(p, n) = p + (n·5 + 0x11)·8` indexes them
with a 0x28 stride over up to 250 slots.

Attempts to find the 22 players' live per-tick positions all failed inside the window:

- `ctx+0x1928` (documented handle array) read **all zero**;
- `ctx+(i+0xBF)·0x28` and `ctx+(i+0xA5)·0x28` (the `FUN_140a48A10` / `FUN_140a48910`
  player getters) read **all zero**;
- the 0x28-stride pointer arrays at `ctx+0x19B0…0x1D30` hold **vtables** (`0x14259AEE0`),
  not player objects;
- the team object at `ctx+0x1920` → `0x2C70F9B0` holds `{vtable,0xA}`, `{ptr,0x1E161860}`,
  `{vtable,0xC}`, `{ptr,0x1E149400}` (= the roster − 8) and a capacity/count pair; its
  roster is the one the stat blocks came from, but no per-player position was reached.
- the match repeatedly entered `MATCH_FROZEN`, which zeroes `*(ctx+0x40)` — precisely the
  pointer the record array hangs off, so the whole capture is gated on a live
  open-play spell.

So: the **procedure** below is the deliverable, not a number.

### 7.3 The validation procedure (for the next open-play spell)

```
1. gate:   STATE=OPEN_PLAY from pes_state.py, and *(u64*)0x1436F3DC8 != 0 and
           struct.unpack('<Q', pread(ctx+0x40, 8))[0] != 0
2. base:   p = *(u64*)(ctx+0x40) ; recbase = p + 0x88
3. sample: at ~200 Hz, read recbase .. recbase+0x28*24, keep (t, record[1..24]).
           Re-conquer +0x40 whenever it reads 0 (cutscene/replay gaps).
4. per tick of the sim: Δ = rec[n] − rec[n−1] ; speed = hypot(Δ.x, Δ.z) [m/tick]
                         km/h = speed · 194.4
5. per slot, take the p99 of speed over samples where the slot actually moved.
6. compare v_peak against, per the live stat block of the matching roster slot:
        liveSpeed = clamp(Speed × A2 × A3 × A4 × A5)      (matchlogic §3.1)
        v_clip    = linterp(blendRate) · clamp(G, 0.9166, 1.0833)   (§3.1, §6.2)
   and report the error e = v_peak − v_expected in m/s.
7. The fitted equation is expected to be  v_peak = a · f(liveSpeed) + b · clamp(G,·)
   with f monotone in liveSpeed; with the four live Speed values {77, 82, 88, 93, 99}
   only 5 points exist, so a linear f is the most that can be claimed.
```

What is already validated with the data in hand: the **form** of the answer — PES's peak
speed must be a blend-rate × plugin-scale product whose inputs are the live Speed byte and
the physique scale G, with the clip ceiling at 8.96 m/s and the dash class at 22 km/h.

### 7.4 What a live spell would additionally settle

- the fitted Speed → blend-rate constant (O2);
- `dribbleTired`-class coefficients for the dribble blend (O3);
- the true `playbackSpeed` driver (O5);
- whether `A5` (stamina) is a multiplier on Speed/Acceleration or only a clip-family swap
  (`_tired` variants) — the injury/fatigue report found only the latter (OBS) and the
  `A5` array writer is still unlocated (O1).

## 8. Open items

| id | item | status |
|---|---|---|
| O1 | the writer of the `A5` live-stamina array at `param_1+0xCB58` (the drain term `Δstamina = -k2·term·1/54`) | not located; `FUN_140a58550`/`FUN_140a581C0` are the obfuscated accessors (`x = max(raw,20)`, `floor(x·0x780/100)`), `FUN_140a58450`/`FUN_140a58350` the numerator/denominator; `pes-re-stamina-live.md` §4/§8 lists every other attempt |
| O2 | Speed → peak speed (m/s) coefficient | **not measured**; needs O3's live spell; the four live Speed values are insufficient |
| O3 | the per-player displacement series for the 22 slots | the array at `*(ctx+0x40)+0x88` is a ring/scrub buffer; the live player positions were not reached in the available window |
| O4 | in-match ability index identity 0x27 = Speed and 0x2B = Acceleration | rests on the third-party listing; the `.rdata` modifier tables cannot separate 0x27↔0x2C or 0x2A↔0x2B |
| O5 | what sets `playbackSpeed` (0.9–1.15 per player) | one store, action-layer inputs, no physique input; Speed/stamina unchecked against the player tables |

Killed this session, recorded so nobody repeats them:

- "the record array at `*(ctx+0x40)+0x88` has one record per player" — killed: the
  per-slot max displacement grows linearly with slot index (0.70 → 10.02 m/tick over
  slots 1..24), and 9.4 M samples of the head show only 1,295 real moves. It is a
  history/ring buffer of one entity.
- "`FUN_140a48A10(ctx,i)` / `FUN_140a48910(ctx,i)` give the 22 players live" — killed:
  both arrays read all-zero during the capture spell (documented but stale for this build).
- "`+0x960[i]` is the AnimMovePlugin scale" — killed earlier by the physique report
  (it is the parent bone's length scale).
- "`DAT_14256B81C` is a speed constant" — killed: the image byte there is `0x20 = 32.0`
  while the code's operand resolution is ambiguous (`.trace` file offset base is
  `0x140000A00`, `.rdata`/`.data` base is `0x140000000` in this dump, so a naive
  `VA − 0x140000000` read of a `.trace` operand lands on the wrong bytes). The value that
  makes the `timeStep = (1/54)·playbackSpeed` reading work is **1/54**, confirmed by the
  live read of `0x14351B854 = 54.0`.

## 9. GodotFootball recipe (derived, no magic numbers)

1. Tick = 1/54 s. `km/h = 194.4 · metres_per_tick`.
2. Player displacement = (blended clip root delta in Fox space) × `clamp(G, 0.9166,
   1.0833)` × `playbackSpeed(1000/54 s)` — i.e. advance the blended clip by
   `playbackSpeed / 54` seconds per tick and take its root delta; then rotate by the
   current facing.
3. Speed classes `{2.5, 8.0, 13.5, 22.0}` km/h select still/walk/jog/run/dash; the
   steady-run blend rate = `clamp(speed / 8.96, 0, 1)`, lerped between the members'
   natural root speeds (walk 1.36, run_1 1.15, run_2 3.08, run_3 3.95, run_4 7.47,
   run_5 8.96 m/s at 30 fps, played at 54 Hz logic so the ×54/30 time scale applies).
4. Attribute effects are multipliers on the live stat byte, never a speed formula:
   `live = clamp(base × condition × familiarity × supersub × stamina)` with the three
   tables of §2.1; abilities clamp 40..99.
5. Turns are *clip* transitions, not angular velocities: bucket the relative body angle
   into 0/45/90/135/180° (mirrored), read `Freemove.bin` (`state·10 + digit`) for
   `(motionNo, mirror, nextState)`, crossfade 0.133–0.2 s. No Agility/Balance term.
6. Stops: pick the stop/brake clip from the Freemove state with the descending speed
   class, and carry `cancel_speed` (from `anim_infos.json`) as the residual speed; correct
   the remaining position error with `AdjustTransXZ` over `round(54·6/60)` = 5 ticks at
   factor 0.9.
7. Body size enters only through G (stride) — never through cadence.

## 10. Sources and scratch

- Image: `<scratch>/pesmem.bin`, base 0x140000000. `.trace` reads at `VA − 0x140000A00`,
  `.rdata`/`.data` reads at `VA − 0x140000000` (verified: `0x14259A764` = 60.0 and the
  `.trace` operand pattern at `0x1406f2c76` both resolve correctly this way).
- Decompiles pulled this session (GhidraMCP :8080): `FUN_14092AF10`, `FUN_1407f7370`,
  `FUN_1407fba90`, `FUN_1407fa2c0`, `FUN_140802b60`, `FUN_14081b7b0`, `FUN_14081e2f0`,
  `FUN_14081e460`, `FUN_1406f0f70`, `FUN_1406f14e0`, `FUN_1406f19f0`, `FUN_1406af620`,
  `FUN_1406e6c40`, `FUN_1406e73a0`, `FUN_140512cd0`, `FUN_140a57450`, `FUN_140a6cf70`,
  `FUN_140a48a10`, `FUN_140a48910`, `FUN_140a489f0`, `FUN_140a3af90`, `FUN_140a62b30`,
  `FUN_140a58550`/`140a581c0`/`140a58450`/`140a58350`, `FUN_1406b10d0`/`1406b11e0`,
  `FUN_141af61c0`, `FUN_14046f230`, `FUN_1404583b0`, `FUN_1405bb170`, `FUN_14043a020`.
- Live reads: `~/ghidra-projects/scratch/pes_state.py` (state gate),
  `~/ghidra-projects/scratch/LocomotionRE/{samp.py,dis.py,disp.json,roster.json}`.
- Prior reports relied on: `pes-re-matchlogic.md` §1.2/§1.3/§3.1/§3.6, `pes-re-animselect.md`
  §1–§6/§9, `pes-re-physique.md` §0–§6, `pes-re-physique-helpers.md`, `pes-re-stamina-live.md`
  §4/§8–§9, `pes-re-sim-motion-mapping.md` §2/§3/§6, `pes-re-injury-fatigue.md` §1/§2/§7,
  `pes-re-throwin-choreography.md` §5/§10 (for the km/h-band pattern and image offset rules),
  `pes-re-fox-registry.md` §1 (vtable/slot resolution), `pes-re-tactics-clock.md` §10–§11
  (offset conventions).

No player or team names; no PES-derived bytes, name lists or number tables beyond the
individual constants quoted as evidence at their addresses.


> **Superseded in part (AnimSelectRE, `pes-re-anim-select.md`):** the Speed attribute sets a per-player rate cap (≈25.3 km/h at 40, 29.8 at 99), and Acceleration a linear rate ramp (`FUN_1406eab00`); the "no acceleration variable" claim is killed. Locomotion picks clips from `Animetable.bin` rows, not `Freemove.bin`.
