# PES 2021 — how the AI picks and blends locomotion and action clips (MOVESTEP FSM)

Scope: read-only RE of the PES 2021 image (Ghidra headless decompiles via `dec.sh`, raw disassembly via
`pdis.py`/`refs.py`, constants read from the flat image) and of the live process (sudo `pread` of
`/proc/<pid>/mem` only). Scratch: the AnimSelectRE-2 scratch dir (`mv1..mv5.a.c`, `al1/al2.a.c`,
`cx1.a.c`, `live.py`, `samp.py`). No PES table is reproduced in full; every table is given by VA and
layout, with only the individual values needed as evidence.

Tags: **OBS** = observed (decompile + raw bytes, or live). **INF(x)** = inferred, confidence x.
Addresses are image VAs (base 0x140000000). `P` = the `match::AnimePlayer` (primary vtable
0x1425e34d8). `req` = the per-tick move request struct (`uint*`, word offsets `req[n]`).

Carried in (not re-derived): `pes-re-animselect.md` (AR), `pes-re-anim-blend.md` (AB),
`pes-re-locomotion.md` (LOC), `pes-re-sim-motion-mapping.md` (SMM).

---

## 0. Headline results

1. **Locomotion is a 24-state FSM, `MOVESTEP_*`, and it selects clips from `Animetable.bin` rows,
   not from `Freemove.bin`.** OBS: the state names are a string table at 0x1425dd698
   (`MOVESTEP_INIT … MOVESTEP_DEMO`); the dispatcher `FUN_1406ba900` switches on the state byte
   `P+0x245c` with one handler per state (§2). Every handler that picks a clip calls the Animetable
   accessors (`FUN_1406abb30`/`abd60`/`abdd0`) on locomotion rows: `step_*`, `goto_run_*`,
   `run_stop_*`, `run_turn_run_*`, `run_*_side_*`, `run_*_back`, `side_l_*_run`, `back_*_run`, … .
   The clips AR saw live (`idle_0_2_run_045`, `run_2_4_000`, `walk_1_0_090`, `back_2_2`,
   `side_3_3`, `mid_delay_1_1`) are all cells of those rows; `run_2_4_000` is **not in
   `Freemove.bin` at all** (OBS, table scan). This corrects AR §4.3.3 / SMM §0.6 / LOC §5.2
   ("locomotion = Freemove FSM"). INF(high): nothing on the AI move path reads `Freemove.bin`
   (no handler reaches the anime-table manager's Freemove word, §10.6); the `[FMOVE_*]` debug
   strings have no code xrefs (OBS, RIP-relative scan).
2. **Steady running = one parametric blend whose rate comes from an exe speed table divided by the
   physique scale.** OBS `FUN_1406b9e80`: `rate = PL( (km/h / 3.6) / G )` with an 11-knot
   piecewise-linear table at 0x1425de010..0x1425de0ac (§3.2), `G` = the AnimMovePlugin scale
   (`*(*(P+0xa08)+0xf0)+0xc0`, LOC §6.2). The knots are the group-80 members' natural speeds
   (3.056 m/s ↔ 0.295, 3.85 ↔ 0.484, 7.3 ↔ 0.887, 9.16 ↔ 1.0). The group itself is chosen per
   tick from 17 locomotion styles by `FUN_1406ba2f0` (§3.3), mapped through `DAT_14344e0e0` (SMM §6.8).
3. **`playbackSpeed` is closed (AB open item 1).** OBS two writers:
   - step / stop clips: `FUN_1406ba070` → `0.95 + 0.15·t`, `t = clamp((1.0833 − G)/0.1667, 0, 1)`;
   - turn / accelerate / decelerate clips: `FUN_1406e9f10` → `0.95 + 0.10·p² + 0.05·t`,
     `p = clamp((stat − 40)/59, 0, 1)` from an in-match ability byte (§4.2).
   Check against AB's one-player live trace: step family 0.9698 ⇒ `t = 0.132`; turn family 0.9824 ⇒
   `p = 0.508` ⇒ stat = 70.0 exactly (an integer ability), INF(high). Short players and good
   athletes step faster; 1.0 is the parametric-loop and idle default.
4. **Crossfade lengths are per transition kind, fixed in the handlers** (§5): step start 0.3 s,
   loop entry 0.3 s (0.1667 s when the feet are already placed), loop style switch 0.1333 s
   (0.2 s back to plain), every turn / accelerate / decelerate / run-turn clip 0.1667 s, stop 0.2 s,
   turn-stop 0.1333 s, slip turn 0.15 s. Kicks: `clamp(min(cancelTime, f(turn, recency)), 0.1, 0.4)`.
5. **Interruption is by dwell timers, not by clip end.** Each transition writes a minimum dwell
   (`req[0x1b]`, `req[0x2e]`, seconds) that the next decision must wait out; both timers advance
   by `1/54` s per tick (`FUN_1406ba900` head). Discrete actions are cut at their
   `CancelData.cancel_frame` (§5.3).
6. **Ball alignment for placed kicks** (§6, OBS `FUN_140733a30` + helpers): the game solves where
   the root must be at the hit frame (`ball − R(yaw)·(hitBall − hitRoot)·0.01·scale`) and spreads
   the residual against a two-velocity root prediction over `hitTicks + 1` ticks
   (`AdjustTransXZ`), with the yaw spread the same way (`AdjustAngle`). The hit record layout in the
   runtime motion object is decoded (§6.1).
7. **The Speed attribute sets each player's top running speed through a rate cap** (§3.5, OBS raw
   disassembly at 0x1406f99f0): `rateMax = 1 − 0.085·u(G) − 0.1·(1 − q²)`, `q = clamp((Speed −
   20·fatigue − 20)/79, 0, 1)`. Composed with the speed table: about 25.3 km/h at Speed 40, 27.4 at
   77, 29.8 at 99, nearly independent of height (the 0.085·u term cancels the root scale G).
   Closes LOC O2 statically.
8. **The Acceleration attribute sets a linear rate ramp** (§3.6, OBS `FUN_1406eab00`):
   `1/(1.4 − 0.15a²)` rate/s below rate 0.6, `(rateMax − 0.6)/(1.9 − 0.15a²)` above, deceleration
   `1/(1.25 − 0.2a²)`; turns pull the target rate toward 0.5 linearly up to 90°. Kills LOC §4.1.
9. **Live (§7), 22 players + 3 officials in open play:** the step-loop playback matches §4 to 4
   decimals for every player; `rateMax` inverts to integer Speed bytes for all 25 (officials 60);
   the engine blend rate (`BAC+0x1b4`) moves in exactly the per-tick steps of §3.6; steady speed
   = `G·PL⁻¹(rate)` within 0.2 % in game time. The logic ticks 57 times per wall second
   (1/54 s of game time each), so game time runs 5.6 % fast on this machine.

---

## 1. Where it runs

`MOVE`/`STEP_MOVE` action exec `FUN_1406b8340` (AnimePlayer action kinds 2/3), once per logic tick
(54 Hz), in this order (OBS):

| Step | Function | What |
|---|---|---|
| 1 | `FUN_1406b68d0` | init `req` on the stack (defaults: `req[0]=3`, duration `req[0x32]=0.2`, playback `req[0x35]=1.0`, param state `req[0x37]=0x11` = "single clip") |
| 2 | `FUN_1406b7560` | fill the move inputs from the AI command `cmd` (§1.1) |
| 3 | `FUN_140708750` | early-out for other owners (returns without a clip) |
| 4 | `FUN_1406cbf70` | (only in states 3 / 13) per-tick rate update toward the target with the Acceleration ramp (§3.6), and the loop's representative motion `DAT_1425ddcf0[style][band]` |
| 5 | `FUN_1406cbc10` | lateral drift damping for `run_2_4_000` / rate ≥ 0.3 (`req+0x134/0x138`) |
| 6 | `FUN_1406ba900` | **the MOVESTEP FSM** (§2): writes either a single clip or a parametric request |
| 7 | tail of `FUN_1406b8340` | `req[0x37]==0x11` → `SetMotion(m=req[0x30], startTime=req[0x33], mirror=req[0x31], playback=req[0x35], duration=req[0x32], transition=req[0x36])` (if `duration < 0.25` and `FUN_140a6e290` → 0.3); else `SetParametric(group=DAT_14344e0e0[req[0x37]], startRate=req[0x4f], mirror, …, duration=req[0x32], rate=req[0x4a])`. New state written to `P+0x245c` when `req[0x38]` is set, previous kept at `P+0x245d`. |

### 1.1 Inputs (`FUN_1406b7560`, OBS)

| `req` | Source | Meaning |
|---|---|---|
| `[0]` | `cmd[0x16]` (8 → 2) | move kind 0..7 (selects the row column-block: 0–2 mid, 3 far, 4–6 keeper near/mid/far, 7 keeper holding the ball) |
| `[1]` | `atan2` to `cmd+0x14` target, deg 0..360 | heading to the move target |
| `[2]` | `P+0x20` | current facing |
| `[3]` | heading to `cmd+0x20` (look-at point) | desired body direction |
| `[5]/[6]/[7]` | `FUN_14043a0a0` = wrap180 differences | `[6]` = target − facing, the turn angle |
| `[8]/[0xc]/[0xd]` | vt `+0x60` = `FUN_1406b9b20(angle,10)` | 10-way angle bucket (§2.2) |
| `[9]` | `FUN_1406b8de0(cmd[0x17])` | speed class: command 1..4 → 0..3 (walk / jog / run / dash), else −1 |
| `[10]` | `cmd[0x172]` | desired speed, km/h |
| `[0xb]` | `cmd+0x5c5` | "use the class rate" flag |
| `[0xe]` | derived | "precise arrival" flag (set for kinds 4/5 at low class or kinds 2/0x33/0x34 near the target; cleared when turn < 22.5°) |
| `[0x11]/[0x12]` | distances to target / look-at | m |
| `[0x16]` | `cmd[0]` | command id |
| `[0x4a]` | — | output: parametric rate |
| `[0x4f]` | — | output: startRate (foot phase 0/0.25/0.5/0.75) |

Speeds are converted `km/h = |v|·54·60·60/1000` (`FUN_1406ea110`, constants 54 / 60 / 1000 OBS).

---

## 2. The MOVESTEP FSM

### 2.1 States and handlers (OBS: names at 0x1425dd698, dispatch `switch(*(P+0x245c))` in `FUN_1406ba900`)

| # | Name | Handler | Rows it picks (Animetable) | Exits to |
|---|---|---|---|---|
| 0 | INIT | inline | idle: `FUN_1406b93a0` (§2.4); start: `step_*` via `DAT_1425dd9b0` | IDLE / STEP_LOOP |
| 1 | IDLE | `FUN_1406bf0c0` | `mid_jockey_passget*`, PK idles, kick-charge idles | IDLE, IDLE_PK_* |
| 2 | IDLE_TURN | `FUN_1406bfe70` | — | IDLE |
| 3 | RUN_LOOP | `FUN_1406c0170` | parametric group (§3); exits below | 4, 6, 7, 8, 9, 10, 12, 14, 16 |
| 4 | STEP_LOOP | `FUN_1406c4c10` | `step_*`, `back_3_4_run`, `backslant_3_4_run` | STEP_TO_RUN, STEP_TURN_RUN, STEP_LOOP_CHANGE |
| 5 | STEP_LOOP_CHANGE | `FUN_1406c6480` | — | STEP_LOOP |
| 6 | TURN_STOP | `FUN_1406cab20` | `step_mid/far_1_1(_relax)` | IDLE, STEP_LOOP |
| 7 | GOTO_RUN | `FUN_1406bdf10` (+ entry `FUN_1406ce820`/`FUN_1406bf7f0`) | `goto_run_0_{1..4}_{near,mid,far,gk_*}` via `DAT_1425dda50` | RUN_LOOP |
| 8 | STOP | `FUN_1406c9e20` (entry `FUN_1406d5d60`) | `run_stop_{1..4}_0_{near,mid,far,gk_*}` via `DAT_1425ddad0` | IDLE |
| 9 | ACCELERATE | `FUN_1406bb570` (entry `FUN_1406ccd00`) | speed-up clips; live: `run_2_4_000/045/090/180` (cells of `run_turn_run_2_4`), handler rows not fully enumerated | RUN_LOOP |
| 10 | DECELERATE | `FUN_1406bcd20` (entry `FUN_1406cdaf0`) | `run_turn_run_4_2*`, `run_4_2_tired*` | RUN_LOOP |
| 11 | GOTO_STEP | (no case: never dispatched) | — | — |
| 12 | RUN_TURN_RUN | `FUN_1406c14c0` (entry `FUN_1406d6780`) | `run_turn_run_{2_2,2_4,3_3,3_4,4_4,4_2}[_reverse/_tired/_relax/_hurry/_parallel/_v2]` | RUN_LOOP / SLIPDOWN |
| 13 | STEP_TO_RUN | `FUN_1406c6f80` | — | RUN_LOOP |
| 14 | RUN_TO_STEP | `FUN_1406c1070` | — | STEP_LOOP |
| 15 | STEP_TURN_RUN | `FUN_1406c7400` (entry `FUN_1406d38d0`) | `side_l_{1..3}_{1..4}_run[_normal][_hurry]`, `back_{1,2}_{2,4}_run` | STEP_LOOP |
| 16 | RUN_TURN_STEP | `FUN_1406c3470` (entry `FUN_1406d2f60`) | `run_{2,3}_3_side_{l,r}[_near]`, `run_{2,3}_3_back` | STEP_LOOP |
| 17 | IDLE_OTHER | `FUN_1406bf7f0` | goto_run / step re-entry | IDLE, STEP_LOOP, GOTO_RUN |
| 18 | RUN_TURN_RUN_SLIPDOWN | `FUN_1406c2f80` | — | — |
| 19 | STEP_TURN_STEP | `FUN_1406c8d80` | `step_*` | STEP_TURN_RUN, STEP_LOOP |
| 20/21 | IDLE_PK_PLAYER / _GK | `FUN_1406bfd10` / `FUN_1406bfb70` | PK stances | — |
| 22 | TIMINGSTEP | `FUN_1406ca6b0` | — | IDLE |
| 23 | DEMO | `FUN_1406bdd70` | — | — |

Before the switch, `FUN_1406ce550` pre-empts every state except 23 when the command is an
out-of-play reaction (`cmd[0x14]` ∈ 0x22..0x29, 0x2f..0x32): it plays a fixed `dm_oop_*` motion
(walk variant, or run variant above 13.5 km/h), duration 0.3 s, playback 1.1, startTime 0.5 s, and
enters DEMO (OBS).

### 2.2 Angle → column (OBS `FUN_1406b9b20`, tables read from the image)

The turn angle (deg, wrapped ±180) is bucketed into 10 buckets named `MOVEANGLE_L00/L45/L90/L135/
L180/R00/R45/R90/R135/R180` (strings at 0x1425dd5c0..0x1425dd650; L = positive angle):

- windows: `DAT_1425ddeb0 = {−180, −157.5, −112.5, −67.5, −22.5, 0, 22.5, 67.5, 112.5, 157.5, 180}`;
  a narrow variant `DAT_1425ddee0` replaces ±22.5 by ±10 and ±157.5 by ±170;
- window → bucket `DAT_1425ddf10 = {9, 8, 7, 6, 5, 0, 1, 2, 3, 4}`;
- **±5° hysteresis**: the current bucket's window is widened by 5° on both sides; the window just
  below it is shrunk by 5° at its top.
- bucket → Animetable column `DAT_1425dd988 = {9, 11, 13, 15, 17, 8, 6, 4, 2, 0}` (i.e. +0, +45,
  +90, +135, +180, −0, −45, −90, −135, −180); bucket → angle `DAT_1425dd660 =
  {0, 45, 90, 135, 180, 0, −45, −90, −135, −180}`; bucket → "left side" flag `DAT_1425dd950 =
  {1,1,1,1,1,0,0,0,0,0}`.

The mirror comes from the cell bit; RUN_TURN_STEP / the step loop override it with a fixed bit set
(`0x1028` / `0x814` masks on the param state, `FUN_1406d2030`).

### 2.3 INIT: standing → first clip (OBS `FUN_1406ba900` case 0)

1. `req[0x4a]` (rate) `= clamp(v_kmh/28, 0.15, 0.7)` from the current ground speed `v`.
2. If the command speed class (`P+0x245a`, vt+0x98) is 0, or move kind 7, or (turn > 90° and
   class < 2): `FUN_1406b6bb0` decides. It returns "stay" when the distance to the target is within
   the arrival radius (0.2 m; 0.1 m precise; 0.5 m in phase 2 kick-off wait), or when precise and
   the facing error `|req[7]|` exceeds the allowed angle (112.5°; or `67.5 + 45·clamp((d−0.5)/2.5)`
   when both player sub-states `+0x5d8/+0x5dc` are 5; 180° when the look-at is > 90° off).
   Stay → idle row `FUN_1406b93a0(kind)` (§2.4), column 9 (8/9 by the sign of `req[7]` for kinds
   other than 3/6), duration 0.2, dwell 0.2 s (`req[0x2e]`), state IDLE. Go → `FUN_1406ce820`
   (GOTO_RUN entry: `goto_run_0_{1..4}_*` row from `DAT_1425dda50`, or a step row).
3. Otherwise the body turn `a = |wrap180(facing − desiredBodyDir req[4])|` decides:
   - `a < 22.5°`, precise and `|req[6]| > 22.5°` → try RUN_TURN_STEP (`FUN_1406d2f60`);
   - `a ≥ 67.5°`, or precise with v ≤ 22 km/h, class ≠ 4 and (dist ≤ 1 m or v ≤ 8 km/h or
     class < 3) → **step start** (first `FUN_1406d38d0` STEP_TURN_RUN when `a > 67.5°` and
     `|req[6]| > 22.5°`). Speed band `sb = 0` if v < 8 km/h and class ≤ 2; else 2 if v ≥ 16 km/h
     or class ≥ 3; else 1. Row `= DAT_1425dd9b0[sb·8 + max(kind,2)]` (kind 4 → 5):

   | sb \ kind | 2 (mid) | 3 (far) | 4/5 keeper mid | 6 keeper far | 7 holding ball |
   |---|---|---|---|---|---|
   | 0 | `step_mid_1_1` | `step_far_1_1` → `step_far_1_1_relax` (0x530) unless the column is ±180 | `step_gk_mid_1_1` | `step_gk_far_1_1` | `step_gk_hold_ball_3_3_onehand` |
   | 1 | `step_mid_2_2` | `step_far_2_2`; at ±180 → `_twostep` on a 50 % roll | … | … | … |
   | 2 | `step_mid_3_3` | `step_far_3_3`; at ±180 → `_bend` on a 20 % roll | … | … | `step_gk_hold_ball_4_4` |

     Column from the bucket of `a` (§2.2). Request: duration **0.3 s**, playback `FUN_1406ba070`
     (§4.1), startTime `FUN_1406b97d0(motion)` (per-motion constant 0–0.6 s, switch in code),
     dwell 0, state STEP_LOOP.
   - else → run entry `FUN_1406d2030` (§3.1; vt+0 of the `+0xac0` interface) with the foot
     `P+0x245b` and a start band capped by the class: 28 km/h (band 3) if class 4 and v > 22;
     16 (band 2) if class ≥ 3 and v > 11; 11 (band 1) if class ≥ 2 and v > 5; else 5 (band 0)
     (constants 0x41e00000 / 0x41800000 / 0x41300000 / 0x40a00000).
4. Duration override: 0.4 s instead of 0.2 s when `P+0x2814` or `P+0x2816` < `round(54·20/60)` =
   18 ticks, or a kick-off phase (`P+0xac8` top byte 0x50) (OBS).

### 2.4 Idle row choice (OBS `FUN_1406b93a0`)

Random picks use the per-player generator at `P+0x32d0` (`FUN_1406e6bf0(P,n)` = n-way pick,
`FUN_1406ebf30(P,pct)` = percent test; both `func_0x140a92470(P+0x32d0, 2)`).

| Condition | Rows |
|---|---|
| set-piece phase 0x18 or no team slot | `stand_a/c/d/e` (`DAT_1425ddc58`, 4-way random) |
| replay / player id > 0x15 | `stand_a/c/d/e` (`DAT_1425ddc48`) |
| kind 2 | `mid_jocky_standby`, `mid_jockey_passget(_v2/_v3)` (`DAT_1425ddbf0`, 4-way random); when `FUN_140a638c0` holds in phase 3 it falls through to the kind-5 row |
| kind 5 | `gkmovemid_guard_0_0` (0x5b1); kind 4 → `gkmovelow_guard_0_0`; kind 7 → `gkmovehigh_idle_0_0_ball` |
| kind 3 | stamina byte (`FUN_140a58550`) > 19, or a stoppage longer than `54·3` ticks in phase 3 → `stand_a/c/d/e` + kick-off idles (`DAT_1425ddc10`, 12-way) in phase 1 or when `+0x1554 ≠ 6`, else `stand_*` (`DAT_1425ddc00`, 4-way); stamina ≤ 19 → `idle_tired_v6/v7` (2-way), `idle_tired_v4/v5` on a 25 % roll when stamina < 5 |
| kind 6 | `gkmovehigh_idle_0_0(_ver02)` |
| default (kind 0/1) | `mid_jocky_standby_noloop` / `near_delay_standby_goalline_noloop`; within 10 m of a marked opponent facing > 135° away → `near_delay_0_0_v2` / `jockey` |

---

## 3. Running: the parametric loop

### 3.1 Loop entry (`FUN_1406d2030`, OBS)

- Run speed `req[0x1d]` clamped to **[5, 28] km/h**; class `req[0x1f]` clamped 0..3.
- Rate `req[0x4a]`: `req[0xb]` set → `DAT_1425dde10[class] = {0.15, 0.30, 0.60, 1.00}`; else
  class 0 with kind 3/6 → 0; else `FUN_1406b9e80(speed)` (§3.2). Then
  `rate ≤ 0.95 · rateMax`, `rateMax = P+0x377c` (per-player, vt+0x18 of the `+0xac0` interface).
- Style `req[0x37]` = `FUN_1406ba2f0(...)` (§3.3); representative motion `req[0x30] =
  DAT_1425ddcf0[style·4 + band]`, band = 0/1/2/3 for rate < 0.225 / < 0.4 / < 0.7 / ≥ 0.7.
- **startRate** `req[0x4f]` (the shared normalised phase, AB §5.3): foot `f = param_3`:
  `0.75` if f ∈ {1, 3}, else 0; styles in mask `0x814` flip the side; if the side flag is clear the
  value becomes 0.5 (0.25 for f ∈ {1,3}). OBS. So the four foot events of AR §5.3 are the four
  entry phases.
- Duration `req[0x32]` = **0.3 s**, or **0.1667 s** when the body turn is < 45°, the target lies
  within the clip's root reach (`rootDist/100 − 0.4·P+0x60b·… < bone10.z`), and the two foot bones
  (3 and 6), in body space, lie within x ∈ (−0.1, 0.35) and (−0.35, 0.1) m (OBS constants
  0.35 / −0.1 / 0.1 / −0.35; the reach term INF(medium)).
- Dwell `req[0x1b] = 0.1 s`; state RUN_LOOP.

### 3.2 Speed → rate (OBS `FUN_1406b9e80`, table at 0x1425de010, 10 records × `{f32 vHi, f32 vLo, f32 rHi, f32 rLo}` stored high-to-low)

```
v   = (km/h · 0.277778) / G          // m/s, divided by the physique scale G (0.9166..1.0833)
for the first record (from the top) with vLo ≤ v:
    t    = min(1, (v − vLo)/(vHi − vLo))
    rate = rLo + t·(rHi − rLo)        // lowest record and kind 3/6: rate = t·0.295
rate = clamp(rate, 0, 1); v below the lowest vLo → 0
```

Knots (m/s → rate): 1.1535 → 0.15, 3.056 → 0.295, 3.85 → 0.484, 4.3 → 0.555, 5.0 → 0.66,
5.8 → 0.75, 6.5 → 0.82, 7.1 → 0.871, 7.3 → 0.887, 8.3 → 0.96, 9.16 → 1.0.
The knots coincide with the group-80 member natural speeds GF measured (run_1 1.15, run_2 3.08,
run_3 3.95, run_4 7.47, run_5 8.96 m/s; LOC §3.1), so the curve **is** the inverse of the blend's
speed: PES speed-matches by table, not by measurement. The `/G` makes bigger players use a lower
rate for the same ground speed, because their root track is scaled up by `G` (AnimMovePlugin).

### 3.3 Locomotion style → parametric group (OBS `FUN_1406ba2f0` + `DAT_14344e0e0` + `DAT_1425ddcf0`)

`FUN_1406ba2f0(P, cmdId, rateRequested, req, sideFlag, curStyle, …)`:

| Return | When | Group (SMM §6.8) | Members at band 0/1/2/3 (`DAT_1425ddcf0`) |
|---|---|---|---|
| 0x0f | not in open play phase 1, or command 0x4a, or another player owns the scripted slot | 87 | `walk_1_1_relax`, `run_2_2`, `run_3_3`, `run_4_4_relax` |
| 0x0d | in-match stamina byte < 20 (`FUN_140a58550`) | 91 | `walk_1_1_relax`, `run_2_2_tired_v1`, `run_3_3`, `run_4_4` |
| 10 | INF(medium) reading of a nested condition: commands 2, 5, 0x33, 0x34, 0x3e (mask `0x4018000000000024`) asking for a rate more than 0.15 above the current one, while the current rate is below 0.825·rateMax (no `+0x30` flag); also reachable through the dribble branch | 86 | `walk_1_1`, `run_2_2`, `near_run_3_3`, `run_4_4_v2` |
| 8 | dribble command (`req+0x58 == 5`) in phase 1: ball (predicted 20/60 s ahead) ≤ 3 m away, within 45° of facing and below half the player's height (`FUN_1406e7380/100·0.5`), unless it is moving ≥ 16 km/h within 45° of the facing | 81 | `walk_1_1`, `run_adjust_2_2`, `run_adjust_3_3`, `run_4_4_v2` |
| 0 | otherwise | 80 | `walk_1_1`, `run_2_2`, `run_3_3`, `run_4_4` |

In the RUN_LOOP handler (`FUN_1406c0170`) the style is further overridden to 6 / 7 / 0x10 (groups
82 / 84 / 83, the `js_run_*_loop_guard_*` jostle loops) when the nearest opponent's predicted
position (4 ticks ahead) is closer than `0.7 + 0.3·clamp((ability(0x28) − 50)/45, 0, 1)` m (`FUN_1406e6660(P, 0x28)`; 0.45 m for
command 0x4a), 11.25° < bearing and his facing < 67.5° off: bearing < 112.5° → 6, < 157.5° → 7
(or 0x10 for class < 2), ≥ 157.5° → 0x10 (OBS). Styles 1–5 / 9 / 11–14 (near-run, parallel,
heisou, tired v2) are set by other callers not traced here.

A style change crossfades **0.1333 s** (0.2 s when returning to style 0), transition type
`2 + (mirror changed)`, dwell 0.2 s (`req[0x1b]`), and only once `req[0x1a] ≥ req[0x1b]` (OBS).

### 3.4 Leaving the loop (OBS `FUN_1406c0170`; thresholds from the image)

Per tick, first match wins:

1. guard/jostle request flags with rate ≤ 0.6 and not precise → re-enter GOTO_RUN (`FUN_1406ce820`);
2. `FUN_1406ccd00` → ACCELERATE (speed-up clip, 0.1667 s); `FUN_1406cdaf0` → DECELERATE
   (`run_turn_run_4_2*`, `run_4_2_tired*`, 0.1667 s);
3. not precise, class > 2, (rate < 0.225 or (speed < 11 km/h and turn > 78.75°)), distance > 3 m,
   turn > 56.25° → GOTO_RUN re-plan;
4. `FUN_1406d2f60` (threshold `(rateMax − 0.6)/2`) → RUN_TURN_STEP: turn 50.6°..123.75° picks the
   side rows `run_{2,3}_3_side_{r,l}[_near]` (sign of the turn picks r/l; `_2_2` rows when
   rate < 0.3 or class < 2 and rate ≤ 0.6), ≥ 123.75° (and ≥ 135° or look-at ≥ 33.75°) picks
   `run_3_3_back` (rate ≥ 0.6) / `run_2_2_back`; column from the bucket; 0.1667 s; **refuses
   (stays in the loop, decelerating) while rate > threshold + 0.6**;
5. straight buckets (L00/R00) with rate > 0.15 → stay, re-select style/rate (§3.3);
6. otherwise a turn: rate < 0.3 → STOP (`FUN_1406d5d60`, `run_stop_*`, 0.2 s); distance ≤ 2 m →
   TURN_STOP (`FUN_1406d2590`, 0.1333 s); else RUN_TURN_RUN (`FUN_1406d6780`, `run_turn_run_*`,
   0.1667 s). The look-ahead target is clamped to ±110° from the heading, the turn allowance
   22.5° (above 19 km/h) or the 45° default.

With no AI speed request the loop drops to STOP (`FUN_1406d5d60`) or, at > 5 km/h of facing-
projected speed, keeps the class 0/1 band (OBS tail of `FUN_1406c0170`).

### 3.5 Where the Speed attribute enters: the per-player rate cap (OBS raw disassembly 0x1406f99f0..0x1406f9b4c)

Once per frame (guarded by `P+0x3778 != *(ctx+0x270)+0x1198`) the AnimePlayer writes
`rateMax = P+0x377c`:

```
u        = clamp((G − 0.9166)/0.1667, 0, 1)                    // FUN_1406eae00, 0x1425e4a18 = 0.1667
base     = 1 − 0.085·u                                         // 0x1425e4a0c = 0.085
c        = FUN_140a581c0(player)                               // condition/stamina byte
fatigue  = clamp((20 − c)/20, 0, 1)                            // 0x14259be1c = 20, 0x1425a946c = −20
s        = ability byte +0x27 (FUN_14124dad0 record) − 20·fatigue   // officials: s = 60
q        = clamp((s − 20)/79, 0, 1)                            // 0x1425cb300 = 79, 0x1425c48cc = 99
rateMax  = base − 0.1·(1 − q²)                                 // 0x1425e4a10 = 0.1
```

Two clamps use it (OBS): the loop entry caps the requested rate at `0.95·rateMax`
(`FUN_1406d2030`), and the per-tick loop update (`FUN_1406cbf70`, §3.6) clamps the target and the
applied rate at `rateMax`. The ground speed of the loop is `G · PL⁻¹(rate)` (root track scaled by
`G`, rate computed on `v/G`, playback 1.0), so the steady top speed is
`v_top = G · PL⁻¹(rateMax)` (INF(high), composition of OBS parts; values computed from the table):

| G | ability(0x27), fresh | rateMax | v_top |
|---|---|---|---|
| 1.0 | 99 | 0.9575 | 8.27 m/s (29.8 km/h) |
| 1.0 | 77 | 0.9095 | 7.61 m/s (27.4 km/h) |
| 1.0 | 40 | 0.8639 | 7.02 m/s (25.3 km/h) |
| 0.9711 / 1.0833 | 99 | 0.9722 / 0.9150 | 8.32 / 8.32 m/s |

The `0.085·u(G)` term cancels the root scale: height barely changes top speed.
INF(medium): byte `+0x27` is Speed (LOC §2.1 labelling, index caveat O4). This closes LOC's open
item O2 statically ("Speed's contribution must be measured"): Speed acts through the rate cap,
quadratically, and fatigue below condition 20 removes up to 20 points of it.

### 3.6 Where the Acceleration attribute enters: the rate ramp (OBS `FUN_1406cbf70`, `FUN_1406e66d0`, raw disassembly of `FUN_1406eab00`)

Per tick in RUN_LOOP and STEP_TO_RUN (`FUN_1406b8340` calls `FUN_1406cbf70` for states 3 / 13):

```
target = req[0xb] ? classRate[req[9]] : PL(v_req/G)               // §3.1, §3.2
target = clamp(target, 0, rateMax)
if target > 0.5 (DAT_142565600):                                  // turning slows the run
    a = |turn| < 11.25° ? 0 : clamp(|turn|/90, 0, 1)
    target = 0.5 + (1 − a)·(target − 0.5)
τ = FUN_1406eab00(target, rate, rateMax)                          // seconds
rate = τ < 1/54 ? target : rate + (1/54)/τ·(target − rate)        // FUN_1406e66d0, clamp [0, rateMax]
rate *= 0.95 when the tracked opponent is right ahead (|x| < 0.3 m, z > 0.3 m in body space)
       and closing faster than 5 km/h
```

`τ` makes the ramp **linear in time** (rate units per second), with `a = clamp((Acc' − 40)/59,
0, 1)`, `Acc'` = ability byte `+0x2b` minus `(10 − c)` when the condition byte `c` ≤ 10:

| Case | Slope (rate / s) | Constants |
|---|---|---|
| accelerating, current ≥ 0.6 | `(rateMax − 0.6)/(1.9 − 0.15·a²)` | 0.6 `0x14257a190`, 1.9 `0x1425e4a60`, 0.15 `0x1425c48a0` |
| accelerating, current < 0.6 | `1/(1.4 − 0.15·a²)` | 1.4 `0x14259d5d8` |
| decelerating | `1/(1.25 − 0.2·a²)` | 1.25 `0x1425ab128`, 0.2 `0x142598578` |
| decelerating below 0.6 in player sub-state 2 (`+0x5d0`) | `1/3` | −3 `0x14259d654` |

So a 40-Acceleration player needs 1.4 s per unit of rate below 0.6 and 1.9 s to climb from 0.6 to
his cap; a 99-Acceleration player 1.25 s and 1.75 s. This **kills LOC §4.1** ("no explicit
acceleration variable"): acceleration lives in the rate domain, not in m/s².

---

## 4. Playback speed (closed)

### 4.1 Writers (OBS)

| Clip kind | Writer | Formula |
|---|---|---|
| step start, run→step, idle | `FUN_1406ba070` | `0.95 + 0.15·t` |
| turn, accelerate, decelerate, run-turn, step-turn | vt `+0x50` of the `+0xac0` interface = `FUN_1406e9f10(p)` | `0.95 + 0.10·p² + 0.05·t` |
| parametric loop, default | `FUN_1406b68d0` init | 1.0 |
| out-of-play demo | `FUN_1406ce550` | 1.1 |
| kick (`FUN_1407302b0`, kick kind 0x10) | inline | `0.95 + k·p²`, `p = clamp((ability(0x29) − 40)/59, 0, 1)` via `FUN_1406e6660(P, 0x29)`; `k = 0.15` (`DAT_1425c48a0`) on the traced paths, INF(medium); other kicks 1.0 |

`t = clamp((1.0833 − G)/0.1667, 0, 1)` (`DAT_1425ddf80` 1.0833, `DAT_1425ddfe0` −0.1667,
`DAT_1425ddf78` 0.9166), `G = *(*(P+0xa08)+0xf0)+0xc0` = the AnimMovePlugin scale (`FUN_141af3180`).

### 4.2 The stat (OBS disassembly of `FUN_1406e9f90`)

`p = clamp((s − 40)/59, 0, 1)` where `s` = in-match ability byte `+0x2b` of the player record
returned by `FUN_14124dad0` (byte `+0x15` instead when the player is the current ball target),
minus `(10 − c)` when the condition/fatigue byte `c = FUN_140a581c0()` ≤ 10. Constants 40
(0x14259be2c), 99 (0x1425c48cc), 59 (0x1425cb2fc), 10 (0x142588210).
INF(medium): byte `+0x2b` is the Acceleration ability (LOC §2.1 index caveat O4 applies; that the
record from `FUN_14124dad0` uses the same indexing is unproven).

### 4.3 Check against AB's live numbers (INF high)

AB §6.2 player: step family 0.9698, turn family 0.9824. `0.95 + 0.15t = 0.9698` ⇒ `t = 0.132`
(G = 1.0613). Then `0.95 + 0.1p² + 0.05·0.132 = 0.9824` ⇒ `p = 0.5085` ⇒ `s = 70.0`. The squad-wide
span 0.9763..1.0717 lies inside the formula's range [0.95, 1.10]; 1.15 belongs to linesmen.
This kills AB's "per-family cadence chosen by the locomotion controller" reading: the family split
is the two formulas.

---

## 5. Blend timing and interruption

### 5.1 Crossfade per transition (OBS, the `duration` argument of each request)

| Transition | Duration (s) | Constant |
|---|---|---|
| INIT → step clip | 0.3 | `DAT_142590d98` |
| → idle | 0.2 (0.4 within 18 ticks of a touch/kick-off) | init `0x3e4ccccd`, `0x3ecccccd` |
| → RUN_LOOP (parametric) | 0.3, or 0.1667 with feet placed | `0x3e99999a`, `0x3e2aaaab` |
| loop style change | 0.1333 (0.2 back to style 0) | `DAT_1425ddf4c`, `DAT_142598578` |
| RUN_TURN_RUN / RUN_TURN_STEP / STEP_TURN_RUN / ACCEL / DECEL / STEP_TURN_STEP | 0.1667 | `DAT_1425ddf50` |
| run-turn slip / step-turn special (motion 0x3b) | 0.15 | `DAT_1425c48a0` |
| STOP | 0.2 | `DAT_142598578` |
| TURN_STOP, STEP_TURN_RUN variant | 0.1333 | `DAT_1425ddf4c` |
| RUN_LOOP → step (side/back trap rows) | 0.2, 0.25 when turning against the mirror | `DAT_142592228` |
| out-of-play demo | 0.3 | |
| kick (`FUN_14072d0b0`) | `clamp(min(cancelTime, d), 0.1, 0.4)` (§5.3) | |

### 5.2 Minimum dwell (OBS)

`req[0x1a]` and `req[0x2d]` grow by `1/54` s each tick. Transitions set the thresholds:
loop entry `req[0x1b] = 0.1`; style change 0.2; step start / RUN_LOOP → step 0.2 / 0.3
(`req[0x2e]`); stop 0.05 (`req[0x1b]` in case 0); out-of-play demo 2.0 clamped to the clip length.
The RUN_LOOP handler returns early while `req[0x1a] < req[0x1b]`, so a new locomotion clip cannot
interrupt the previous one before its dwell.

### 5.3 Actions: cancel point (OBS `FUN_14072d0b0`, `FUN_1406af8e0`)

- Runtime motion object (0x390 B, `FUN_1406b0ed0()` table): count bytes `+1` HitData, `+2`
  SubHitData, `+5` CancelData; record pointers `+0x20+8k` (HitData), `+0x48+8k` (SubHitData),
  `+0xa0+8k` (CancelData). (Corrects AR §2.2's `+0x90` HitData pointer.)
- `cancel_frame` = CancelData u32 bits 13..22; ticks = `round((2·f / rate)·(54/60))`.
- Kick crossfade: `d = FUN_1406e7ce0(P, 0.2) = (1−w)·0.2 + w·0.3`, `w = max(clamp((|Δyaw| −
  22.5)/22.5, 0, 1), recency)`, recency = 1 inside 13.5 ticks of `P+0x2816`/`P+0x2814`, linear to 0
  at 54 ticks, or 1 when bone 10 is below the height threshold `FUN_140a6d400(4, scale)`;
  kind 0x11 clamps `d` to [0.1, 0.2]; `+0.2` when `FUN_1406ec200(P,5)`; then
  `min(d, cancelTicks/54)` (clip length if the outgoing clip has no CancelData) and clamp [0.1, 0.4].
  So the incoming kick fades in no longer than the outgoing clip needs to reach its cancel frame.

---

## 6. Aligning a kick / touch to the ball

### 6.1 Hit record (OBS accessors `FUN_1406aff90`, `FUN_1406b05e0`, `FUN_1406b0680`, `FUN_1406b00d0`, `FUN_1406b0180`, `FUN_1406b03d0`)

HitData record (pointer `+0x20+8k`): u32 `+0` bits 13..20 = hit frame, bits 27..31 + 1 = contact
part (mirrored through `DAT_1425dc5a0`); `+0x24/+0x28/+0x2c` = ball position z, y, x (cm);
`+0x30/+0x38` = root x, z at the hit (cm); `+0x3c/+0x40` = root velocity x, z at the hit
(cm/frame); `+0x44` = yaw at the hit (deg; negated when mirrored). SubHitData (`+0x48+8k`) has its
own layout (`+4/+0xc/+0x14` ball, `+8/+0x10` root, bits 11..14 + 1 = frame, bit 15 = selects the
HitData index). Motions 0xe42 / 0xf7c use built-in tables at 0x14344dcf0 / 0x14344df10.
Hit ticks: `round((2·f / rate)·(54/60))`.

### 6.2 Placed kick (OBS `FUN_140733a30`, free-kick rows `fk_*` 0x4e1..0x4e7)

1. Row by situation (curl/instep/inside/infront/short-left/right), column `FUN_1406de990(row,
   wrap180(targetYaw − facing))`, mirror from the cell (flipped for the left-footed taker).
2. Motion override through `FUN_1406b3560`/`FUN_1406b3780` (playerID derivation tables).
3. `hitTick = FUN_1406b03d0(m, rate)`; `SetMotion(m, 0, mirror, playback, 0.2, 0)`.
4. **Root target at the hit**: `FUN_1406e85b0`: `d = (hitBall − hitRoot)·0.01·P+0x3b18`
   (x negated if mirrored, `FUN_1406e8b70`), `target = ball − R(yaw)·d`, y = 0.
5. **Position spread**: `AdjustTransXZ` (`FUN_141eb39a0`, object `P+0x428`) over `N = hitTick + 1`
   ticks: prediction `p̂ = pos + (v0+v1)·N/2 + (v0−v1)·k` (`FUN_141eb30c0`/`FUN_141eb3220`,
   `v0` = current velocity, `v1 = R(yaw)·hitRootVel·(30/54)·scale`), per-tick correction
   `(target − p̂)/N` (x and z).
6. **Yaw spread**: `AdjustAngle` (`FUN_141eb35a0`, `P+0x4d0`) from the current yaw to
   `yaw + hitYaw(m)` over the same N.
7. Open-play kicks (`FUN_1407302b0`) only spread height (`AdjustTransY`, `P+0x498`, to 0 over
   hitTicks) and yaw (`round(54·12/60)` = 11 ticks when the ball line is > 112.5° off); their
   horizontal alignment comes from the approach, not traced.

---

## 7. Live

A second match was loaded later in the session. Samples (OBS, read-only `pread`): `samp.py` at
30 ms for 40 + 60 + 90 + 90 s (`samp1..4.jsonl`, 228,175 records over 25 AnimePlayers: 22 players,
referee, 2 linesmen), and an 8 s run at ~1 ms (`hf_rate.json`). Much of `samp1/2` was the match
loading and a goal replay (positions frozen); the checks below use open-play samples only.
Fields: motion `+0xae0`, MOVESTEP state `+0x245c`, action kind `+0xac8`, position `+8/+0x10`,
playback `+0x3758`, request duration `+0x3b88`, `G = *(*(P+0xa08)+0xf0)+0xc0`, and the
**engine's per-tick blend rate `BAC+0x1b4`** (written by `FUN_141af5100` each tick, OBS
disassembly; `+0x3bb4` on the AnimePlayer only holds the rate of the last *new* parametric
request, so it is not the live rate).

### 7.1 Results

1. **States and clips.** Open-play kinds 2/3 spent their time in RUN_LOOP, ACCELERATE, GOTO_RUN,
   STEP_LOOP(_CHANGE), STEP_TURN_RUN, RUN_TURN_STEP, RUN_TURN_RUN, STEP_TURN_STEP, IDLE,
   DECELERATE (kind 74, a goal/restart phase, also runs the FSM). 322 distinct clips; every
   non-keeper locomotion clip is an Animetable cell or a parametric member. The only clip that is
   a `Freemove.bin` motion and not an Animetable cell is `near_run_3_3`, a member of parametric
   groups 85/86. Supports §0.1.
2. **Logic tick = 57.0 per wall-clock second**, each advancing 1/54 s of game time (median
   interval between changes of the engine rate and of position: 17.54 ms, p10–p90 16.7–18.0 ms,
   n = 1,267 / 11,297). Game time therefore runs 1.056× wall time on this machine; wall-clock
   speeds below are compared with game-time formulas × 1.056.
3. **Playback speed (§4) — confirmed exactly.** For all 22 players with step-loop clips, the
   STEP_LOOP playback equals `0.95 + 0.15·clamp((1.0833 − G)/0.1667, 0, 1)` to 4 decimals
   (G ∈ {0.9711, 0.9836, 0.9886, 1.0, 1.0103, 1.0279, 1.0464, 1.0502}). The turn/GOTO clips'
   playback inverts through `0.95 + 0.1p² + 0.05t` to integer abilities (73, 77, 80, 82, 83, 85,
   86, 88, 89, 93, 99, and one at the formula's floor, ≤ ~45).
4. **Rate cap (§3.5) — confirmed exactly.** Live `P+0x377c` for all 25 AnimePlayers inverts
   through `rateMax = 1 − 0.085·u(G) − 0.1·(1 − q²)` to integer Speed bytes: 60 (referee and both
   linesmen: the officials' fixed `s = 60` in the code), 72, 77, 80, 82, 83, 88, 89, 93, 99. Every
   player's maximum observed engine rate is below his `rateMax`.
5. **Rate ramp (§3.6) — confirmed per tick.** The engine rate moves in fixed per-tick steps:

   | Player (Acc, rateMax) | rise < 0.6: obs / `1/(54(1.4−0.15a²))` | rise ≥ 0.6: obs / `(rateMax−0.6)/(54(1.9−0.15a²))` | fall: obs / `1/(54(1.25−0.2a²))` |
   |---|---|---|---|
   | 99, 0.9433 (4 players) | 0.01476–0.01481 / 0.01481 | 0.00363 / 0.00363 | 0.01752–0.01764 / 0.01764 |
   | 89, 0.9485 | 0.01424 / 0.01428 | – | 0.01657 / 0.01665 |
   | 93, 0.9487 | – | 0.00361 / 0.00363 | 0.01683 / 0.01701 |
   | 85, 0.9316 | 0.01395 / 0.01410 | 0.00335 / 0.00339 | 0.01606 / 0.01634 |
   | 80, 0.9099 | – | 0.00311 / 0.00313 | 0.01575 / 0.01599 |
   | 73, 0.8953 | 0.01360 / 0.01369 | – | 0.01545 / 0.01560 |

   Residuals ≤ 2 %, all on the low side, consistent with the condition term `Acc − (10 − c)`
   (Acc read from the playback is the same reduced byte only when sampled at the same moment);
   the Acc-99 rows are exact.
6. **Speed ↔ rate (§3.2).** Over 264 steady 0.3–0.4 s RUN_LOOP windows (engine rate constant
   within 0.03), measured wall-clock speed / `G·PL⁻¹(rate)` = 1.054 median (IQR 1.038–1.063),
   i.e. **0.998 in game time** after item 2. By band (wall-clock m/s, measured vs predicted ×1.056):
   rate 0.25–0.40: 3.22 vs 3.23; 0.40–0.55: 4.53 vs 4.69; 0.55–0.70: 4.66 vs 4.69; 0.85–1.0: 7.30
   vs 7.36. The locomotion speed bands are the table's knots.
7. **Clips per state (kinds 2/3, samp3+4, most frequent):** RUN_LOOP `run_3_3`, `run_4_4`,
   `run_2_2`, `walk_1_1` (group 80) and `js_run_4_4_loop_guard_side(_v2)` (groups 82/84, §3.3);
   ACCELERATE `run_2_4_000/045/090/135`, `run_2_4_000_hurry`; DECELERATE `run_4_2(_045/_quick/
   _090_v2/_180_v2)`; GOTO_RUN `idle_0_3_run_090/045/135/180`, `idle_0_2_run_135/045`;
   RUN_TURN_RUN `run_3_3_090_relax`, `run_2_4_135/180`, `run_3_3_parallel_180`; RUN_TURN_STEP
   `parallel_2_2_side_v2`, `run_3_3_side`, `run_2_2_back(_090)`, `run_3_2_side_045/090`;
   STEP_TURN_RUN `side_2_4_run(_f045/_f135)`, `side_3_3_run`, `idle_0_2_run_045`; STEP_LOOP
   `side_1_1_relax`, `backslant_1_1_relax`, `backslant_3_3`, `backstep_1_1_relax`, `side_3_3`;
   STOP `mid_delayside_2_0`, `runslant_2_0`, `backslant_2_0`, `run_2_0`; TURN_STOP
   `walk_1_0_090/045`, `run_{2,3,4}_0_*`; IDLE `stand_a/c/d/e` and kick-off loops (§2.4).
   Every clip seen is an Animetable cell, except parametric members (`run_4_4_v2`,
   `run_4_4_relax`, `near_run_3_3`, `js_run_*_loop_guard_*`) and keeper clips.

---

## 8. Killed hypotheses

| Hypothesis | Killed by |
|---|---|
| AI locomotion clips come from the `Freemove.bin` state graph (AR §4.3.3, SMM §0.6, LOC §5.2) | every MOVESTEP handler reads Animetable rows; `run_2_4_000` (live-observed by AR) is not a Freemove motion; no RIP xref to the `[FMOVE_*]` strings |
| Parametric rate = speed / 8.96 (SMM §3, LOC §3.1, GF `RateForSpeed` from measured member speeds) | `FUN_1406b9e80`'s 11-knot table, and the `/G` term |
| `playbackSpeed` is a per-family cadence the controller chooses (AB §6.2) | two closed formulas (§4), fitting AB's own numbers to an integer stat |
| Crossfade durations are scattered constants (AR §5.1) | one duration per transition kind (§5.1) |
| HitData pointer at motion object `+0x90` (AR §2.2) | accessors use `+0x20+8k`, count byte `+1` |
| No Speed → km/h coefficient exists; `v_top ≈ 8.96 m/s` clip-limited for every player (LOC §3.2) | the per-frame rate cap `P+0x377c` from ability `+0x27` (§3.5); rate 1.0 (`run_5_5`) needs `rateMax ≥ 1`, i.e. `G ≤ 0.9166` and Speed 99 |
| No explicit acceleration variable; acceleration = transition tick gates (LOC §4.1–4.2) | the linear rate ramp of §3.6 from ability `+0x2b` |
| AB §5.2: the round start times 0.1 / 0.35 s are tick counts divided by a playback rate | they are `FUN_1406b97d0`'s per-motion constants for the step-loop clips (§10.3) |

---

## 9. GF counterparts

| PES finding | Replaces in GodotFootball |
|---|---|
| MOVESTEP FSM (§2, §3.4) with Animetable locomotion rows | `humanoid.cpp` `SelectAnim` movement branch and `AnimCollection::CrudeSelection` (`animcollection.cpp`) for `e_FunctionType_Movement`; the GF movement pool / autogen transitions |
| Angle buckets + ±5° hysteresis + column map (§2.2) | GF's `CompareMovementSimilarity` / outgoing-angle ranking |
| Speed → rate table / G, style → group (§3.2, §3.3) | `src/gdext/pes_locomotion.cpp` `RateForSpeed` (measured member speeds) and the fixed group 80 |
| Rate cap from Speed / fatigue / G (§3.5) | GF `Player::GetMaxVelocity` and `StretchSprintTo(…, sprintVelocity, GetMaxVelocity())` in `humanoid_utils.cpp` |
| Rate ramp from Acceleration (§3.6) | GF's velocity acceleration in `humanoid_utils.cpp` (`StretchSprintTo`, the movement velocity curves) |
| startRate from the foot (§3.1) | `pes_locomotion.cpp` phase start (always the player's own running phase) |
| Playback formulas (§4) | `src/rig/clip_pose.hpp` / `pes_animation.cpp` fixed 1.0 playback |
| Per-transition crossfades and dwells (§5) | `humanoid.cpp` interrupt logic (`e_InterruptAnim`, `GetBestCheatableAnimID` queueing) |
| Hit record + root target + AdjustTransXZ/Angle spread (§6) | `humanoid.cpp` touch cheat (`ballDistanceZ`, height scaling near :3100) and `GetBestCheatableAnimID` touch-frame matching |

---

## 10. Open items

1. Speed ↔ rate in the 0.40–0.55 band reads 3.5 % low (n = 16 windows); more open-play windows
   would settle whether that is noise or a curve detail. The ramp residuals (§7.1.5) need the
   live condition byte to close exactly.
2. Styles 1–5, 9, 11–14 writers (near-run / parallel / heisou / tired v2): other callers of the
   `req[0x37]` slot, not traced.
3. `FUN_1406b97d0` per-motion startTime switch, resolved (OBS): it covers only the step-loop
   family — 0.6 `walkside/walkslant/walkbackslant_1_1`; 0.5 `run_2_2`, `backstep_1_1_relax_v2`;
   0.35 `mid_delayback_2_2`; 0.3 `backslant_1_1_relax`, `gkmovemid_guardback_*`; 0.25 `run_3_3`,
   `runslant_*`, `backslant_3_3`, `mid_delayside_2_2`; 0.2 `side_1_1/2_2`, `back_2_2`,
   `mid_delay_2_2`, …; 0.15 `back_3_3`, `mid_delayside_1_1`, …; 0.1 `side_3_3`, `mid_delay_1_1`,
   `mid_delayslant_1_1`, `backstep_1_1_relax`, …; 0.05 `mid_delay_3_3`, `near_delay_2_2`, …;
   0 otherwise. AB §5.2's live start times 0.1000 (`mid_delayslant_1_1`, `mid_delay_1_1`,
   `backstep_1_1_relax`) and 0.3500 (`mid_delayback_2_2`) are exactly these entries, which AB had
   explained as tick values divided by a rate. Open: the foot-phase meaning of each offset.
4. Ability identity of bytes `+0x27` (rate cap), `+0x2b` (ramp, turn playback), `+0x15` in the
   `FUN_14124dad0` record, and of indices 0x28 / 0x29 / 0x34 read through `FUN_1406e6660`
   (§3.3, §4.1). Labels follow LOC's index map, which has the O4 caveat.
5. Open-play kick horizontal alignment (the approach run that brings the foot to the ball).
6. Who, if anyone, reads `Freemove.bin` / `FreemoveOption.bin`. Their runtime copies are words
   `[2]` (3,874 × 16 B) and `[3]` (105 × 12 B) of the anime-table manager passed to the loader
   `FUN_1406abe40` (OBS; word `[0]` is the Animetable object that `FUN_1406abba0` returns via
   `DAT_1436f2378`). No reader was located. Candidate: the user-control sub-controller at
   `P+0x3530` behind flag `P+0x3725` (`FUN_140703a30`), unread.
7. The meaning of the condition byte `FUN_140a581c0()` (fatigue term in §3.5, §3.6, §4.2).

---

## 11. Sources and scratch

All in the AnimSelectRE-2 scratch dir:
- Decompiles (headless, `dec.sh` + `annot.py`): `mv1.a.c` (`FUN_1406b68d0`, `b7560`, `cbc10`,
  `ba900`, `cbf70`, `ab4c0`, `b6a90`), `mv2.a.c` (vtable leaves, `b93a0`, `ba070`, `b97d0`,
  `ce550`, `b6bb0`, `c0170`), `mv3.a.c` (`+0xac0` interface slots, `b9ff0`, `ba2f0`), `mv4.a.c`
  (`b9e80`, `ce820`, `d5d60`, `d2590`, `d6780`, `d2f60`, `d38d0`, `b70d0`, `ccd00`, `cdaf0`),
  `mv5.a.c` (all MOVESTEP handlers), `al1/al2.a.c` (hit-record accessors, AdjustTransXZ),
  `cx1.a.c` (`14072d0b0`, RNG pick helpers), `acc.a.c` (`1406e66d0`, `1406eab00`), `ld.a.c`
  (`1406abe40`).
- Raw disassembly (capstone over the flat image): `FUN_1406e9f90`, `FUN_1406e7ce0`,
  `FUN_1406eae00`, the rate-cap chunk 0x1406f99f0..0x1406f9b4c, `FUN_1406eab00`, `FUN_141af5100`.
- Tables read from the image: 0x1425dd660, 0x1425dd950, 0x1425dd960, 0x1425dd988, 0x1425dd9b0,
  0x1425dda50, 0x1425ddad0, 0x1425ddbd0..0x1425ddcd0, 0x1425ddcf0, 0x1425dde10, 0x1425ddeb0,
  0x1425ddee0, 0x1425ddf10, 0x1425de010..0x1425de0ac, 0x14344e0e0; state names 0x1425dd698;
  row labels 0x1434997f0; clip names 0x14351e590.
- Live: `live.py` (AnimePlayer scan, vtable 0x1425e34d8 + secondary 0x1425e3508), `samp.py`,
  `watch.py`, `ana.py`…`ana5.py`; data `samp1..4.jsonl`, `hf_rate.json`.

No player or team names. Small tables are quoted with their VAs as evidence; a port must read
them from the exe at runtime (the `LoadJudgeConstants` pattern), not copy them.
