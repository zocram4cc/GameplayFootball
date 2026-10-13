# PES 2021 dead-ball restarts other than throw-ins — choreography (RE report, SetPieceChoreoRE)

Read-only RE. Covers kick-off, goal kick, corner, direct/indirect free kick (with the wall) and the in-match
penalty. Throw-ins are in `pes-re-throwin-choreography.md`.

Sources: `pesmem.bin` (mid-match image, base 0x140000000), GhidraMCP decompiles, capstone over raw bytes,
read-only `pread` of `/proc/<pid>/mem` of the running AI-vs-AI match (no writes, no ptrace, no input).
No player/team names; players are slot indices 0..21 (0–10 home, 11–21 away), officials 22–24.

Tags: **OBS** = read in raw bytes / disassembly / decompile at the named address, or observed live.
**INF(conf)** = inference. Frame counts are 54 Hz logic frames unless marked.

Scratch (not in the repo): `~/ghidra-projects/scratch/SetPieceChoreoRE/` — `m.py` (image helpers),
`dec/*.c` decompiles, `spwatch.py` (live restart recorder), `live.jsonl` (raw live log).

Conventions (OBS, shared with the throw-in report): `G = *(u64*)0x1436F3DC8`; `MI = *(G+0x270)`;
`F = *(float**)(G+0x2aa8)` field record (`F[0]` half-length, `F[1]` half-width); Fox space Y up, pitch length
along X; `ap` = `match::AnimePlayer`, action id `ap+0xac8`, sub-id `+0xac9`, phase `ap+0x245c`, clip `u16 ap+0xae0`,
position `ap+0x8`, yaw `ap+0x20` (deg). Restart block `MI+0x1554`: MODE, `+0x1558` side, `+0x155c` display point,
`+0x1568` official restart point, `+0x1574` stop kind, `+0x1588` foul kind, `+0x173c` kicker.

## 0. Live-capture note (OBS, affects every live recorder)

`*(G+0x270)` (MI) is published only during part of each logic frame: 2000 back-to-back reads gave 152 non-null
values, all the same pointer. A recorder that treats a null read as "no match" misses most frames. Cache the last
non-null MI while `G` is unchanged (done in `spwatch.py`; ThrowInRE told). `*(G+0x2aa8)` behaves the same way.

Field record `F = *(G+0x2aa8)` read live (OBS, 24 floats): `F[0]` 52.56 half-length, `F[1]` 34.06 half-width,
`F[2]` 3.66 goal half-width, `F[3]` 2.44 crossbar, `F[4]` 47.06 goal-area line x, `F[5]` 9.16 goal-area half-width,
`F[6]` 36.14 penalty-area line x, `F[7]` 20.12 penalty-area half-width, `F[8]` 41.56 penalty-spot x,
`F[17]` **9.15** (wall / centre-circle distance). Every restart function below reads these, never literals.

`MODE = MI+0x1554` values seen (OBS live + `FUN_1404a7d60` switch): **1 kick-off (half start), 7 kick-off after a
goal** (live: goal stop `MI+0x1574 = 1` → MODE 7), 2 throw-in, 3 goal kick, 4 corner, 5 free kick, 6 penalty.
(`pes-re-foul-choreography.md` §1.2 guessed 7 = game end; that is wrong for MODE: the live goal set MODE 7.)

---

## 1. The common pipeline: stoppage → wipe → teleport → ready → kick

### 1.1 The step programs (OBS live, Flow+0x878, read with `spwatch.py` / ThrowInRE `seqwatch.py`)

| MODE | stop kind `MI+0x1574` | program (hex step codes) |
|---|---|---|
| 4 corner | 4 | `3 b 45 11 1d 1f 23 16 12 13 14 15 e 34 19 1a` |
| 3 goal kick | 4 | `3 b 45 a 11 1d 23 16 12 13 14 15 e 34 19 1a` |
| 5 free kick | 5 (foul) | `3 5 45 11 1d 1f 23 16 12 13 17 14 15 e 34 19 1a` |
| 7 kick-off after goal | 1 | `3 4 45 1d 3 11 8 23 16 12 13 14 15 e 34 19 1a` (goal stop 4 → goal scene) |
| 1 kick-off (half) | 3 | same restart tail (seen with ri at 0x1d) |

The tail `23 16 12 13 [17] 14 15 e 34 19 1a` is the restart set-up for every kind (`FUN_1404524e0`, OBS
`pes-re-foul-choreography.md` §4.1); step 0x17 (the wall, §5.3) is pushed only for MODE 5.

Handlers (OBS `FUN_14044c880` switch): 3 `FUN_140447230` stop stamp; 0xb `FUN_140447690` out-of-play hold;
0x11 `FUN_1404488e0` wipe (fade type 4, 0.1 s); 0x23 `FUN_140448e50` (UI/net fade 0.5 s + 1.0 s, skipped
offline); 0x16 `FUN_140449d20` (substitution/UI window, 10 sub-states); 0x12 `FUN_140448760/1404485f0`;
0x13 `FUN_140447b50` — **kicker selection** at sub-state 6/7 (`FUN_1404a7d60(Flow, Flow+0x4954 kickers,
MI+0x1554, Flow+0x2ab4 wall/FK block, Flow+0x2afc)`); 0x17 `FUN_1404a9ce0` **wall**; 0x14 `FUN_14044a2a0`
**placement counter**; 0x15 `FUN_14044bb40` placement finish; 0xe fade-in 1.0 s; 0x34 `FUN_14044a3e0`;
0x19 `FUN_14044bfb0` (caption hold); 0x1a `FUN_14044d2e0` restart armed.

### 1.2 How players get to their marks: teleport under the wipe (OBS live, corner at frame 46269)

Nobody walks to a set-piece position on camera. Step 0x14 sets `Flow+0x27d8 = 1` and then increments it once per
tick; the match side mirrors it in `MI+0x11b0` and moves one group of players per value. Step 0x15 finishes at
`MI+0x11b0 == 0x16` (OBS `FUN_14044bb40`: value 0x15 → `FUN_140a3cd90(Flow+0x2790, 3)`, value 0x16 →
`FUN_140453370` unless stop kind 6). Step 0x15 itself waits 20 ticks (`Flow+0x8f4 < 0x14`) except at MODE 1.
Observed write order inside the 20-tick window of one corner (positions are exact, frame-by-frame):

| tick in 0x14 | who | what |
|---|---|---|
| +5 | kicker + attackers | provisional spots (kicker briefly at the wrong end) |
| +7 | defenders | zone template: GK (−50.73, 0); x = −F[8] (penalty-spot line) z = 2.0/0.5/5.99/−6.0; (−F[4], −7.16); (−F[6], 0); outlets (−15.9, 12), (−19.53, −11.88), (−F[17], 0) |
| +9 | attackers | final authored corner spots (§4.3) |
| +10 | kicker | final run-up start spot (§4.1) |
| +13 | defenders | final marking spots; near-post man onto the line |
| +19 | officials | referee + both assistants |
| +20 | — | step 0x15; `MI+0x116c` 2 → 3 (set-play ready) one tick later |

All of it happens between the 0.1 s wipe-out (step 0x11) and the 1.0 s fade-in (step 0xe), so the cut is never
seen. OBS (frame-by-frame): the kick-off, both free kicks and both goal kicks used the same 20-tick window and the
same order — restarting team at +5 (then +9/+10, the kicker last), the other team at +7 and +13 (goal kick: a third pass
at +16), the three officials at +19.

### 1.3 Measured corner timeline (OBS live, `live_match1.jsonl`, 54 Hz frames)

| frame | Δ | step | event |
|---|---|---|---|
| 46269 | 0 | 1 → 3 | ball out over the goal line; `MI+0x116c` 1 → 2; MODE 4, R = (−51.781, 0, −33.61) |
| 46272 | 3 | 0xb | out-of-play hold (players play `DEMO_OUTOFPLAY` reactions, e.g. `dm_miss_*` clips) |
| 46546 | 277 | 0x11 | wipe-out, 5 frames (0.1 s ✓) |
| 46551 | 282 | 0x1d, 0x16, 0x13 | UI windows (11 frames); kicker chosen in 0x13 |
| 46562 | 293 | 0x14 | teleport window, 20 frames (§1.2) |
| 46582 | 313 | 0x15 | `MI+0x116c` → 3 at 46583 |
| 46602 | 333 | 0xe | fade-in: 5 + 54 frames (46602 → 46661 ✓ `FUN_1404489d0(…,1.0)`) |
| 46661 | 392 | 0x34, 0x19 | 0x19 sub 4 = 0.25 s = 14 frames (`GetFps·0.25`, `DAT_142592228`) |
| 46679 | 410 | 0x1a | restart armed; set-play counter `MI+0x11ac` starts at 1 |
| 46869 | 600 | — | `CORNERKICK` (id 50) entered, run-up clip `fk_runup`, counter 191 |
| 46942 | 673 | — | phase 2 (strike clip `kick_long_3_0_inside_y0_045_curve_placekick`), counter 264 |
| 46961 | 692 | — | `MI+0x116c` → 1 (in play), counter 282 |

So: **stoppage → in play 692 frames (12.8 s)**, of which the hold before the wipe is 277 frames (static base for
step 0xb is `FUN_14044e460(5)` = 2.5 s = 135 frames; the step also waits on the demo controller
`FUN_14047c2d0`, INF(med) the out-of-play reaction/replay), set-up behind the wipe 36 frames, fade + caption
97 frames, armed → kick 282 frames. Two more corners went in play at counters 282 and 471 (OBS), so the delay is not
fixed (§8.1).

Step 0xb hold table `FUN_14044e460(kind)` (OBS, stack copy of 0x14259ce60/0x14259ce50): kind 0 → 8.0 s,
1 → 5.0, 2 → 3.0, 3 → 0, 4 → 7.0, 5 → 2.5; `FUN_140447690` always asks kind 5 (3.0 s for offside); +2.5 s
and a random `rand(fps·100)/100 + 10` frames when a player is down (`FUN_1404441d0`).

---

## 2. Kicker selection — `FUN_1404a7d60` (OBS decompile; called from step 0x13 sub-state 6/7)

Out: `Flow+0x4954` = {kicker, second kicker} (published as `MI+0x173c` / `MI+0x1740`). Ints are typed as floats by
Ghidra; `3.57331e-43` is 0xff = none. Team record `T = FUN_140a48a30(G, side)`: attack sign `(s8)T+0x254`,
formation base positions `T+0x46e4 + 12·slot` (x, y, z), slot role `T+0xc48+0x3f8c+4·slot` (= `T+0x4bd4`),
designated set-piece takers `T+0x8e08` read by `FUN_1408c5080(T+0x8e08, kind)` then mapped to a pitch slot with
`FUN_140a3b000`. A slot is eligible only if on the pitch (`MI+0x1748[idx] != 0`).

| MODE | rule (OBS) |
|---|---|
| 1 / 7 kick-off | over the 11 slots: `d = |base.xz − (F[0], 0)|` (base positions are in a 0…2·F[0] own-goal frame, INF med), `+15` if role ∈ {5, 8} (wide roles), `−12` if `|base.z| < 18` (`DAT_14259be0c/be18/d664`); sort ascending (`FUN_140a79640`): kicker = 1st, second = 2nd |
| 3 goal kick | the slot whose role (`T+0x4c00[slot]`) is 0 (the keeper); fallback: first on-pitch entry of `T+0x8928[]` |
| 4 corner | designated taker kind **3 or 4** = left/right, chosen by `dir · R.z > 0` → 4 else 3; fallback `FUN_140a621f0(AI, kind, side, 0xff)` |
| 5 free kick | `d` = distance from the ball to the attacked goal `(dir·F[0], 0)`: `d < 32` → designated kind **1**; `d < 60` → kind **0**; else no designated taker; second kicker = designated kind **2** (≠ kicker) when the FK type `Flow+0x2ab4 ∈ {1,2,3}`. Fallbacks in order: current `MI+0x173c` if already ready; when `|z| < F[7]` and (`d < 28` or own half) `FUN_140a62690`; in own half the nearest role-1/2 (CB/SB) player within 65 m; else nearest role < 7 player; else `FUN_140a621f0` |
| 6 penalty | designated kind **5**; fallback the ordered list from `FUN_140a40ca0` |
| 2 throw-in | `FUN_1404a93a0` (see the throw-in report) |

INF(high): kinds 0–5 are the team-sheet set-piece roles (FK long, FK short, FK 2nd, CK left, CK right, PK).
OBS live: both corners of one team in a match were taken by the same player (idx 0), the kick-off by idx 9.

---

## 3. Kick-off (MODE 1 at a half start, MODE 7 after a goal)

### 3.1 Live (OBS, match start, `live.jsonl` frames 0–168)

Program at a half start: `1 f 45 23 16 12 13 14 15 c 34 19 1a` (fade-out type 6 0.5 s, then the common tail with a
0.5 s fade-in `0xc` instead of 1.0 s). Timeline: frame 2 step 0xf (taker 9 already chosen) → 30 step 0x16 → 34
0x13 → 40 0x14 (20-frame teleport) → 60 0x15, ready (`MI+0x116c` = 3) at 61 → 62 fade-in 0xc (5 + 27 frames) →
94 0x34 → 96 0x19 (14 frames) → **112 armed** (counter 1) → 113 referee `referee_kickoff_start_0`
(`judge_kickoff`) → **140 (counter 29) the taker plays `SHORT_PASS` (id 9), clip `kick_mid_0_0_inside_y0_000_late`**
→ **168 in play** (counter 56; the clip's contact). Kick-off to ball live: 56 frames after armed, 168 frames from
the first frame of the half.

**PES21 kicks off with one player, backwards** (OBS): no `KICKOFF_KICKER_LOOP` (22) / `KICKOFF` (25) action ran.
The taker (idx 9) stood in `KICKOFF_RECEIVER_LOOP` (23) at (0.55, −0.25), 0.6 m on the opponents' side of the
centre spot, yaw 294.4° (facing his own half), and passed straight back.

Positions at the ready frame (OBS; home attacks +x, all y = 0):

| who | positions |
|---|---|
| kicking team | GK (−47.07, 0) = −F[4]; (−19.5, ±6.0) back pair; (−15.5, ±23.0) full-backs; (−9.5, ±8.5); (−3.65, ±9.47) and (−1.51, 10.04) |
| defending team | GK (47.07, 0); (19.5, ±6.0); (15.5, ±23.0); (11.0, 0); (3.66, ±9.47); (1.5, ±15.5); (1.51, 10.04) |
| referee | (−5.0, 0, −10.0) |
| assistants | (−19.5, −35.5) and (19.5, 35.5) |

- Every player near the circle sits at exactly **10.15 m = F[17] + 1.0 from the centre spot** ((3.66, 9.47),
  (1.51, 10.04), (−3.65, 9.47)), and nobody is closer than 1.5 m to the halfway line except the taker (INF(high):
  formation kick-off positions pushed out of a 10.15 m disc and back to `|x| ≥ 1.5`). The layout is mirror-symmetric
  between the teams except the taker, i.e. both sides use the same template (INF med: the
  `positionKickOff_*` files named in `pes-re-setpiece-referee.md` §2).
- Keepers stand on the goal-area line (x = ±F[4]).

### 3.2 Officials (OBS)

- Referee: `FUN_140a38a00` (referee chief, MODE 1 placement) = `(−dir·5, 0, −dir·10)` with `dir = T(kicking side)+0x254`
  (`DAT_14259222c` = 5, `DAT_142588210` = 10). Live (−5, 0, −10) ✓ with dir = +1.
- Assistants: the generic rule `FUN_140a395c0`: x = the defending second-last player's x, extrapolated
  `(int)(fps·0.3 + 0.5)` = 16 frames ahead with his velocity, at least the ball's x, inside `|x| ≤ F[0]`, never past
  halfway, and ≥ 1.25 m into his own half (`DAT_1425ab128`/`DAT_1425ee92c`); z = ±35.5 = ±(F[1] + 1.44) (OBS live).
  Live x = ±19.5 = the back pair ✓.

### 3.3 Action classes (OBS decompiles, `FoxRegistryRE/dec/sp_bodies.txt`)

- `KickoffKickerLoop` (22) Update `0x1407819f0`: phase 0 plays row 0xc2 `kickoff_loop_kicker` (col 9, rate 1.0,
  blend 0) → 1; phase 1: `MI+0x116c == 1` → MOVE.
- `KickoffKicker` (25) Update `0x140781780`: phase 0 row 0xc4 `kickoff_kicker_pass` (blend 0.2, `FUN_1406f02f0`
  6/5 = 12) → 1; phase 1: at the clip's kick event (`FUN_1406b03d0`) `FUN_1406ec260` strikes, flag `ap+0x2460 |= 0x400`
  → 2; phase 2: MOVE at clip end, or after the event frame once the facing error to the target exceeds 22.5°
  (`DAT_14259d618`). Not used by the AI kick-off observed above (INF: user / two-man path).
- `KickoffReceiver` (23) Update `0x140781fc0`: phase 0 plays a random one of rows 0x8e7–0x8e9 `kickoff_receiver[02|03]`
  (`FUN_1406e6bf0(ap,3)`, the AnimePlayer's own RNG at `ap+0x32d0`); phase 1: when `FUN_1407816b0` says the ball is
  his, he builds a pass request to the team's preset kick-off target (team table `+0x22e4c + 200·idx`, side offset
  0x58) and `ChangeAction(9 SHORT_PASS)` with flag `0x2000000` — this is the observed one-player kick-off; once in play
  he leaves after `fps/2` = 27 frames.
- `KickoffOther` (24) Update `0x140781b30` (every other player): phase 0 picks the idle:
  stamina % (`FUN_140a58550`) < 20 and a 30 % roll → `kickoff_loop_tired` (0 %) or `…_tired_mild1/2`; else, for the
  restarting side that has not scored more than the opponent (stat 5, INF: goals) and has scored ≥ 1 against, a 60 %
  roll on players with skill bits `0xa000000` / personality 7, or a 20 % roll otherwise, and more than F[17] + 2 m from
  the spot → row 0xd4 `kickoff_loop_cheer`; else a random one of 11 rows 0xc6–0xd0 (`kickoff_loop_jump/one_foot/
  shake/shake45/standstill/stretch/waist/walk_back_front/walk_front_back/walk_side/look_around`). Start offset
  `rand(5)·0.1`, rate `0.9 + rand(4)·0.05`, mirror on a 50 % roll, blend 0.2 → phase 2. Phase 2: at
  `clipEnd − ⌊fps/60 + 0.5⌋` pick again (→ 1); MOVE when in play, when displaced > 2 m from the stored spot or the
  target, or (before ready) when facing > 22.5° off the target and MODE ≠ 1.

---

## 4. Corner kick (MODE 4)

### 4.1 Ball and kicker placement (OBS live ×3, `CornerKickLoop::SetKick` 0x1407331a0)

- Ball point R lies **0.90 m from the corner flag** inside the arc, at a varying angle: (−51.781, −33.61),
  (52.252, −33.214) (|Δ| to (±F[0], ±F[1]) = 0.90 m both). OBS: R is first written as (51.781, −33.61) and moved to
  (52.252, −33.214) one frame later in the third corner. INF(med): the angle follows where the ball went out.
- Kicker start: **4.15 m from R**, outside the field, facing R: (−49.83, −37.27) yaw 331.9°; (55.94, −35.11) yaw
  297.2°. SetKick takes the run-up clip `fk_runup` (row 0x509, style-remapped), builds the stand point from its
  root offset with base yaw 315° or 45° by side (`0x1425aa1c0` / `0x14259be30`) and clamps the request against the
  stadium bounds with a 0.5 m margin (fox-registry §4.1).
- Waiting: `CornerKickLoop` (49) plays row 0x507 `fk_idle` (style remap; live clip `fk_idle`).

### 4.2 Kick timing (OBS live, 4 corners)

| corner | `CORNERKICK` (50) entered | phase 2 (strike clip) | in play |
|---|---|---|---|
| #1 | — | — | counter 282 |
| #2 | counter 191 | 264 | 282 |
| #3 | counter 191 | 264 | 282 |
| #4 | counter 380 | 453 | 471 |

Run-up `fk_runup` 73 frames, strike clip (`kick_long_3_0_inside_y0_045_curve_placekick`, from row 0x4e1
`fk_long_3_0_inside_curve` via the style remap) contact 18 frames later. The phase machine (strike window
`evt − ⌊GameSpeed·14/60 + 0.5⌋` = 13 frames, post-strike lock 9 frames) is in `pes-re-fox-registry.md` §4.
Three of four corners went in play at counter 282 and one at 471 (= 282 + 189). INF(med): the AI re-evaluates the corner
command at intervals of ~189 frames (3.5 s); the deciding code was not found (§9 item 2).

### 4.3 Attackers (OBS: live positions match `constant_positionCK.bin` exactly)

`positionCK*.o` layout (OBS, 368 B): 16-byte header `{16, 0, 0, 0}` then 22 × `{u32 enable, u32 ordinal, f32 x,
f32 z}`. Frame: attack toward +x, corner on the +z side. `positionCK.o` holds all 11 ordinals; `positionCK_N_V.o`
(N = 4/5/6 attackers in the box, V = variant 1–3) override ordinals 11−N … 10.

- Corner #2 (attack −x, corner at −z) = `positionCK_6_1` with (x, z) → (−x, −z): ordinal 5 (27.77, −1.99) → idx 1
  (−27.76, 2.01); 6 (36.33, −10.74) → (−36.34, 10.74); 7 (36.23, −4.84) → (−36.22, 4.85); 8 (40.83, −10.23) →
  (−40.83, 10.25); 9 (43.35, −9.79) → (−43.35, 9.79); 10 (38.66, −7.76) → (−38.66, 7.76). All within 0.02 m.
- Corner #3 (attack +x, corner at −z) = `positionCK_6_2` with z → −z: (27.82, 1.49), (38.68, 8.22), (38.77, 0.49),
  (42.12, 7.90), (41.84, 2.69), (41.41, −3.80) — exact.
- So the world transform is `x = dir·x_t`, `z = −dir·z_t` mirrored once more when the corner is on the attack frame's
  −z side. The variant changed between two corners of the same team (INF med: `cornerKickType` tactics +0x0f0 or a roll).
- Two more attackers sat at (dir·32.21, 7.8) and (dir·32.21, −12.6) in both corners (not mirrored with the corner
  side; not from the table, rule not found) and the rest stayed back: (dir·6.65, −7.0 / 2.98).

### 4.4 Defenders (OBS live; rule INF)

- Keeper: (dir_goal·(F[0] − 1.83), 0) = ±50.73 both corners.
- **Near-post man**: `POSTMAN_LOOP` (52) at (±(F[0] − 0.5), sign(R.z)·3.35) = 0.5 m off the line, 0.31 m inside the
  near post, clip `postman_loop` (row 0x1ba `corner_defence_idle`); `PostManLoop::SetKick` `0x1407333e0` places him
  from that clip's root offset, mirror when `x·z > 0`, yaw 180° if `z ≤ 0` else 0°.
- One more at the near goal-area corner (±47.06 / ±48.26, sign(R.z)·7.16); one at the box line (±F[6], 0).
- **One outlet stays up**: (∓9.15, 0) — in the defending team's own half on the axis, F[17] from halfway (both corners).
- Everyone else marks: the 7-frame zone template of §1.2 (x = F[8] line) is overwritten 6 frames later by
  man-marking spots 0.5–2 m goal-side of the attackers (e.g. attacker (−38.66, 7.76) / defender (−38.30, 9.43)).
  The marking rule is the AI's (`ThinkUnitCornerKickDefenceType`, tactics `cornerkickDefenceMF*`,
  `dfLineWidth_Corner`); not decompiled here.
- During the wait the defenders shuffle with `STEP_MOVE` (1-step adjustments) and idle with the kick-off idle
  family (`kickoff_loop_*`, `stand_*`, `idle_tired_*`).

### 4.5 Officials (OBS)

- Referee: (sign(R.x)·35.63, −sign(R.x)·11.16) in both corners = (±(F[6] − 0.5), ∓(F[5] + 2.0)) (INF high from the
  arithmetic; the code is protected: chief MODE-4 handler `thunk 0x14415d330` in `.impdata`, mutated/obfuscated, only
  the ±1.0 sign constants and a read of `MI+0x155c` were recoverable with `protscan.py`).
- Assistant on the corner's half: `FUN_140a39330` → x = sign(R.x)·F[0]; on the corner's touchline side it is moved to
  `±(F[0] + 0.2)` (behind the goal line) when the kicker is nearer the centre than the ball, else `±(F[0] − 1.2)`.
  Live: (−52.76, −35.5) ✓ and, on the far touchline, (52.56, 35.5) ✓. The other assistant follows the generic line,
  clamped to ±1.25 (live −1.25 ✓). Flag clip `assistant_flag_up_0_flag_ck` (row 219) seen live during the stoppage.

---

## 5. Free kick (MODE 5) and the wall

### 5.1 Live free kick (OBS, `live.jsonl` frames 7897–8911; wide FK at R = (35.113, −30.729), d = 35.3 m)

| frame | counter | event |
|---|---|---|
| 7897 | — | foul; program `3 5 45 11 1d 1f 23 16 12 13 17 14 15 e 34 19 1a` |
| 7900–8518 | — | step 5 foul hold, 618 frames (with a foul scene; 2.0 s without one, foul report §4.2) |
| 8519 | — | wipe 0x11 (5 frames) |
| 8529–8534 | — | 0x13 kicker selection; 0x17 wall (same tick as the 0x13 → 0x14 hand-over) |
| 8535–8555 | — | 0x14 teleport: attackers +5, defenders +7, attackers pushed back +9/+10, kicker +10, defenders/wall +13, officials +19 |
| 8556 | — | ready (`MI+0x116c` = 3) |
| 8652 | 1 | armed; referee `referee_stand_0_whistle` at counter 2 |
| 8815 | 164 | `FREEKICK_LONGPASS` (32), run-up `fk_runup` |
| 8890 | 239 | phase 2, strike clip `kick_long_3_0_inside_y0_045_curve_placekick` |
| 8906 | 255 | wall man → `WALL_JUMP` (45) phase 1 (random delay) |
| 8909/8910 | 257 | in play (contact); wall phase 2 |
| 8911 | — | wall clip `wall_jump_high3` starts |

Second live free kick (OBS, frames 14285–14953, R = (−19.208, 21.113), attack −x, d = 39.5 m, a = 32°): one-man wall
at (−26.94, 16.22), **9.147 m from R exactly toward the goal centre** (n = 1 because 36 ≤ d < 40 ✓); armed → 
`FREEKICK_LONGPASS` at counter 164 again, strike clip at 241, in play at 259; `WALL_JUMP` requested at 257, clip
`wall_jump_high` 1 frame after contact. This kicker's styled run-up put him 3.95 m from R, so the 4.15 m of §4.1/§5.1
is the default `fk_runup` offset, not a constant. Referee at (−13.14, 6.71) — the mirror of the first FK's point.

Placement at the ready frame (OBS; attack +x):
- Kicker (the designated taker; here the same player who took this team's corners) at (36.21, −34.73) yaw 344.6°:
  **4.15 m behind R on the run-up line, 0.67 m outside the touchline** — the same `fk_runup` root offset as the corner.
  `FreeKickLoop::SetKick` `0x1407411f0` (OBS, headless decompile): row 0x509 `fk_runup` style-remapped
  (`FUN_1406b3560/3780` kind 2), stand point = ball − rotated clip root offset (`FUN_1406e8cc0`), yaw = the clip's
  yaw at contact (`FUN_1406b1c70`), wrapped to [0, 360); a ball within 0.5 m of a corner is pulled 0.5 m in; the point is
  clamped to `|x| ≤ F[0] + 1.5`, `|z| ≤ F[1] + 1.5` (`DAT_14259bddc`).
- Attackers in the box: five in a line at **x = 38.91**, z = 10.12 / 7.36 / 5.46 / 4.61 / 3.52 (first written at
  x = 42.69, moved back 3.78 m two frames later); the defenders' line is at **x = 40.05** (five players) → the attackers
  wait 1.14 m onside.
- Short option (29.24, −23.97) ≈ 9 m from the ball; one at the box edge (31.91, 4.66); rest defence (11.86, 6.44),
  (2.0, 9.68), (2.0, −6.07).
- Defenders: keeper (52.26, −1.0) (0.3 m off the line, 1 m to the ball side); line of five at x = 40.05; one at
  (36.1, −21.63) covering the short option; one at the edge (35.84, 4.22); outlets (19.1, −10.49) and **(9.15, 1.51)**.
- Wall: **one man at (39.64, −22.78)**, exactly 9.15 m from R toward the goal centre (Δ = (4.53, 7.95)), clip `wall_idle2`.
- Referee (13.14, −6.71) (protected handler, §9 item 1); assistant on the ball's half (30.11, 35.5) = **R.x − 5.0** (§5.4), the
  other (−1.25, −35.5).

### 5.2 Wall size — `FUN_1404a9920(…, goal, d, base = Flow+0x2788)` (OBS decompile; constants raw-checked)

Inputs: `d` = |R − (dir·F[0], 0)|, `a` = |angle of the ball off the goal's centre line| in degrees (`FUN_14043a0a0` on
`FUN_1404a53e0/140439e50`; INF high), `ax = |R.x|`, `zz = |R.z|`, attack sign `dir`.
```
if training flag (G+0x3b0)                              -> 7
if ax > F[0] - 7.5 and sign(R.x) == sign(goalX)         -> n = 1           (within 7.5 m of the goal line)
elif d <= 28 and ax > F[6] and zz >= F[7]               -> n = 2 + (ax <= F[6] + 5)       (wide of the box)
elif dir*R.x > F[6] - 3 and zz < 10                     -> n = 6           (straight in front of the box)
elif d <= 25:  n = 4 + int(2*t), t = 1 (a <= 20), (40 - a)/20 (20 < a < 40), 0 (a >= 40)
elif d <= 29:  n = 3 + int(2*t), t = 1 (a <= 30), (45 - a)/15, 0 (a >= 45)
elif d <  32:  n = 2 + int(2*t), t = 1 (a <= 30), (40 - a)/10, 0 (a >= 40)
elif d <  36:  n = 1 + int(t),   t = 1 (a <= 40), (50 - a)/10, (a >= 50 -> n = 1)
else:          n = 1 if d < 40 else 0
n += base (Flow+0x2788, INF: a per-team wall adjustment)
if F[6] < dir*R.x < F[0] - 2 and zz < F[7] + 6: n = max(n, 2)        (inside the box: indirect FK)
n = clamp(n, 0, 7)
```
Live check (OBS): d = 35.3, a = 60.4° → n = 1 ✓ (one-man wall seen).

### 5.3 Wall placement and members — step 0x17 `FUN_1404a9ce0(Flow, wall = Flow+0x2ab4)` (OBS decompile + raw)

Wall block: `+6` count, `+8…+0x20` member slots (0xff = none), `+0x24` wall centre vec3, `+0x34` wall yaw, `+0x38` d.
- **Aim point**: one man → the goal centre `(dir·F[0], 0)`; two or more → `(dir·F[0], ±(F[2] + 1.3))` = 1.3 m outside the
  **near** post on the ball's side (`DAT_14259bdd8` = 1.3, sign flipped when `R.z < 0`) (raw 0x1404a9e62–0x1404a9ed2).
- **Centre** = R − **9.15 m** (`DAT_1425a945c`) along the ball → aim-point bearing (two `FUN_140a92c80` table-trig
  calls, raw 0x1404a9f81–0x1404a9fa2). Yaw `+0x34` = 90° − bearing(ball → goal centre) (+180° on two quadrants).
  Live: 9.15 m exactly ✓.
- **Members**: the defending side's on-pitch outfield slots (role ≠ 0) are walked **from slot 10 down to slot 1** and taken
  in that order until `n` are found, skipping a player for whom `FUN_140a41cc0(T+0xc48, AI, slot)` is true (INF med: he is
  needed for marking) while enough candidates remain (not online, not training). So the wall is built from the
  forwards/midfielders first. Live: the defending slot-10 player stayed up as the outlet at (9.15, 1.51) and slot 9
  formed the wall ✓.
- **Order inside the wall**: when `d < 1024`, `|R.z| < 15` and a kicker's preferred foot (`player+0x34`) matches the
  ball's side, members are sorted by the byte `player+0x31` (INF med: height) with `FUN_140a79640(…, 1)`.
- How the members are spread along the line (spacing) is done when the positions are written; not decoded (§9 item 5).

### 5.4 Officials (OBS)

- Assistant: `FUN_140a394d0` (MODE 5) = the generic offside-line x (§3.2); if it is within 5 m of R.x on R's half it is
  replaced by `R.x − sign·5` (`DAT_14259222c`), or `R.x + sign·10` if that would cross halfway. Live 30.11 = 35.113 − 5 ✓.
- Referee: chief MODE-5 handler `thunk 0x144161790` (protected). One live sample (13.14, −6.71).
- Referee gesture: direct offence → `judge_whistle_0_0` / `judge_whistle_2_2`; indirect (`MI+0x1588` ∈ {1, 3}) →
  `judge_fk_indirect_start/loop/rotate/down` (foul report §4.4). Live: `referee_stand_0_whistle` at counter 2.

### 5.5 Kicker and second-kicker actions (OBS decompiles)

- `FreeKickLoop` (30) Update `0x14073e150`: style-remapped `fk_idle` (row 0x507) and `fk_runup` (0x509) are prepared
  every tick; phase 0 plays the idle (mirror by `FUN_1406e6660(ap, 0x34)`), stores the spot, → phase 1, or → phase 2 when a
  second kicker exists (`MI+0x1740 < 22`); phase 1 = the aim/turn loop (body yaw wrapped to ±180°, re-aim clips chosen by
  a switch over the current clip id 0x4d1–0x4db with per-clip blend constants; not fully decoded); in play → MOVE.
  Live: the kicker stood still in `fk_idle` for the whole wait (counter 1 → 164).
- `FreeKick` (31–33 / 37–39) Update `0x14073dfc0`: before phase 0 runs, when a second kicker exists (`MI+0x1740 < 22`) it
  reads the other kicker's player record (`FUN_140a48910`: byte +1 == '#', u16 +0x1a / +0x1c) and returns early while
  `+0x1a + ⌊fps·N/60 + 0.5⌋ < +0x1c` with N = 60 (40 when the own clip is 0x4dd); the exact meaning of the two u16 is open
  (INF med: a hold for the partner's dummy run). Phase 0 `FUN_140743380` then plays the run-up (row 0x509, or 0x50c
  after the 0x4dd clip) → phase 1 `FUN_1407423e0` (strike window, same pattern as the corner) → phase 2
  `FUN_1407421e0` (follow-through).
- `FreeKick2ndLoop` (34) SetKick `0x1407410a0` (angle buckets 22.5/67.5/292.5/337.5°), `FreeKick2ndCede` (35),
  `FreeKick2ndPassToKicker` (36) share Update `0x14073dfc0`.

### 5.6 Wall actions (OBS decompiles `0x140740810`, `0x1407400d0`)

- `WallLoop` (44): stores the spot (`ap+0x32f0`), mirror on a 50 % roll, phase 2 → shared body.
- Shared body (WallJump 45 / WallNotJump 46 / WallRunOut 47 / WallNotRunOut 48 and the loop):
  phase 0: delay = `rand(fps/10 = 5) + 1` frames → phase 1 (count down) → phase 2: the class's row (vtable slot 28),
  rate `0.9 + 0.1·rand(4)`, blend 1/6 s (`DAT_1425ddf50`) → phase 3: during the jump's air window the body yaws ±20°
  toward the ball; a ball touching him within 0.6 m plays a block clip by ball height (> 0.43 m clip 0xc99, 0.11–0.22 m
  0xf81, −0.11–0.11 m 0xf7f); after the kick he leaves (→ MOVE) once the last ball event is ≥ 0.4·fps = 21 frames old
  (kick events, kinds 3/5) or ≥ 0.2·fps = 10 frames (others); a loop re-arms itself (`WALL_LOOP` → phase 4 countdown
  `rand(fps/4) + 0.03·rand(10)`).
- Live: `WALL_JUMP` was requested **2 frames before the kick's contact**, the jump clip (`wall_jump_high3`) started
  1 frame after contact (4-frame random delay). The jump decision itself (`ThinkUnitFreekickWallJump/NotJump/Press`) was
  not decompiled (§9 item 5).

---

## 6. Goal kick (MODE 3)

### 6.1 Live (OBS, frames 9661–10228)

Program: `3 b 45 1e 10 23 16 12 13 14 15 d 34 19 1a` — the wipe is step 0x10 (fade type 2, 0.1 s) and the fade-in
0xd (0.5 s), shorter than the corner's 0x11/0xe. (ThrowInRE's log has `3 b 45 a 11 1d 23 …` for another goal kick, so the
builder varies.) Timeline: out at 9661 → 0xb hold 9662–9802 (**140 frames ≈ 2.5 s + 5**, the static base) → wipe 9803
→ 0x13 9812 → 0x14 9818–9838 → ready 9839 → fade-in 9858–9890 (5 + 27) → 0x19 9892 → **armed 9908** → whistle at
counter 2 → `KP_GOALKICK` (63, long) at **counter 246**, clip `gkgoalkick_instep_0_0` → **in play at counter 320**
(contact 74 frames into the clip). Stoppage → in play 567 frames (10.5 s).

Placement at ready (OBS; kicking keeper defends −x):
- Ball R = (−47.76, 0, 2.6): 4.8 m off the goal line, inside the goal area, on the side the ball went out (INF).
  Second goal kick (OBS): R = (−47.76, 0, −2.6) → in both samples the ball sits at **(±(F[0] − 4.8), ±2.6)**; keeper at
  (−51.64, −5.12), the same offset (−3.88, −2.52) from R as the first (not mirrored).
- Keeper (−51.64, 0.08) yaw 90°, 4.63 m from R: `GoalKickStandby::SetKick` `0x140774ae0` (OBS) uses the root offset of clip
  **0x6b1** (`gkgoalkick_instep_0_0`, mirror by `FUN_1406e6660(ap,0x34)`), base yaw 0.
- Kicking team builds from the back: centre-backs split to the box corners (−44.87, 12.37) / (−44.66, −14.03); full-backs
  high and wide (−19.12, −33.08) / (−19.31, 31.81); midfield (−12.49, ±12.81/−9.04), (−18.44, −4.91), (−2.32, 8.85),
  (−0.07, −4.67).
- Opponents: a front three outside the box (−35.06, −10.97), (−34.83, 1.05), (−28.08, 13.09); midfield
  (−17.17, 0.12), (−11.2, ±9.3); their back four near halfway (6.6, −4.55 / 5.9), (6.42, −14.9), (5.0, 13.83); their
  keeper written at (41.56, 0) = the penalty-spot line, then walking to 40.42.
- Assistants: (−44.56, −35.5) (the kicking side's second-last player) and (10.0, 35.5). Referee (4.0, 6.66) (protected
  handler `thunk 0x144161f30`). Linesman gesture row 215 `goalkick` (`assistant_3_0_090_flag_gk`, seen live).

### 6.2 Keeper actions (OBS)

- `GoalKickStandby` (62) Update `0x1407748f0`: phase 0 plays row 0x83/0x84 `goalkick_idle_l/_r` (by
  `FUN_1406e6660(ap,0x34)`; clip `gkgoalkick_idle_0_0`), counter 4 → phase 1: while the request kind (`req+0x774`) < 2,
  `FUN_1407755d0` plays a coaching gesture from `DAT_1425e8f58[kind·2 + mirror]` (rows 0x7f–0x82
  `goalkick_coaching_lower/up_l/r` → `gkcoaching_*`) → phase 2 `FUN_1407754c0`: at clip end back to the idle → phase 1.
  In play → MOVE.
- `GoalKickLongPass` (63) / `GoalKickShortPass` (64) share Update `0x140774890`, CanEnter `0x140774c50`.
- Kick distance constants: `goalKick.o` (`pes-re-setpiece-referee.md` §1.6).

---

## 7. Penalty kick in a match (MODE 6) — static only (no penalty happened in the captured matches)

- Kicker: designated kind 5 (§2). `PenaltyKickLoop::SetKick` `0x140782930` (OBS): row **0x8eb `pk_runup`**, style-remapped
  with kind 4 (`pk_runup_cristiano/ronaldinho/neymar…`, rows 0x8ec–0x8f1), stand point = ball − the clip's rotated root
  offset at contact (`FUN_1406e8cc0`, mirror by `FUN_1406e6660(ap,0x34)`), yaw = the clip's contact yaw wrapped to
  [0, 360) — i.e. **the run-up length and angle are the clip's, not a constant**. Ball on the spot (x = ±F[8] = ±41.56).
- `PenaltyKickLoop` (53) Update `0x140782770`: phase 0 plays row **0x8ea `pk_idle`** (style remap kind 4), rate 1.0,
  blend 0 → 1; phase 1 waits; once `MI+0x116c == 1` it hands over to MOVE.
- `PenaltyKick` (54) Update `0x1407826f0`: phase 0 `FUN_140783700` replays `pk_runup` (blend 0.2) → 1;
  phase 1 `FUN_140782e00`: strike window `curFrame ≥ evt − ⌊GameSpeed·5/60 + 0.5⌋` (**5 frames** at 54); strike row
  by power `p = ap+0x2bd0` and target height `h` (INF med, `FUN_140a577c0`):
  `ap+0x2d12 == 2` → 0x8f4 `pk_mid_3_0_infront_y0` (INF: chip); h ≥ 0.8: (p ≥ 0.3 or h ≥ 1.2) → p > 0.8 0x8f7 `pk_max`,
  0.55 < p ≤ 0.8 0x8f2 `pk_long_instep`, p ≤ 0.55 0x8f3 `pk_long_inside`; else 0x8f5 `pk_mid_inside`;
  h < 0.8: p > 0.85 0x8f7, 0.65 < p ≤ 0.85 0x8f2, 0.45 < p ≤ 0.65 0x8f3, else 0x8f5. Then `FUN_1406ecbb0` requests the
  kick (on failure and clip end → MOVE) → phase 2 `FUN_140782b10` (follow-through; 10/30/5 m constants). Slot 10
  `0x1407835c0` = the 9-frame post-strike lock shared with the corner.
- Shot speed/target constants: `penaltykick.o` (`pes-re-setpiece-referee.md` §1.6).
- Positions: `constant_positionPK.bin` (OBS decode, same 368-B layout as positionCK, attack +x): ordinal 0 = the kicking
  keeper (−32.22, −0.38); **ordinal 11 = the defending keeper (51.75, −0.02), 0.81 m off the line**; the remaining ordinals
  stand at x ≈ 30–36 (just outside the box, F[6] = 36.14) spread z −13.2 … 12.9, and `positionPK_N` (N = 2…5) moves N
  kicking-team players back toward halfway (PK_2: (3.29, 6.34), (2.56, −2.72), (19.24, 4.46), (15.71, −2.75); PK_5:
  six players between x = 4.8 and 29.2). INF(med): N = players held back, chosen by tactics; in-match use of these
  tables (vs. the shoot-out) not proven.
- Referee (OBS `FUN_140a38a70`): `(s·(F[6] + 4), 0, −s·(F[5] + 5))` with `s = sign(R.x)` = (±40.14, ∓14.16): inside the
  box, 1.4 m nearer halfway than the spot, 14.16 m to the side.
- During the foul stop for a penalty the displaced players get demo state 1 and a PK scene always plays
  (`pes-re-foul-choreography.md` §1.3, §2.1).

---

## 8. Summary tables

### 8.1 Timings (54 Hz frames; OBS live unless marked)

| restart | hold before wipe | wipe | teleport (0x14) | fade-in | armed → kick action | kick action → in play | total stop → in play |
|---|---|---|---|---|---|---|---|
| kick-off (half start) | — (0xf fade-out 27) | — | 20 | 0xc: 5 + 27 | **28** (whistle at +1) | 28 (`kick_mid_0_0_inside_y0_000_late`) | 168 |
| corner | 138–273 (base 2.5 s) | 0x11: 5 | 20 | 0xe: 5 + 54 | **190 / 190 / 379** | 91 (73 run-up + 18) | 557 / 692 / 718 |
| free kick | step 5: 2.0 s no scene / scene length (618 seen) | 0x11: 5 | 20 | 0xe: 5 + 54 | **163 / 163** | 93 / 95 (75–77 run-up + 18) | 1012 |
| goal kick | 140 (2.5 s) | 0x10: 5 | 20 | 0xd: 5 + 27 | **245 / 83** | 74 / 74 | 567 / 441 |
| penalty | foul step + PK scene | — | — | — | not observed | strike window 5 frames before the clip event (static) | — |

After the fade-in: step 0x34 (2 frames) and 0x19 (`0.25·fps` = 14 frames; plus 2.0 s when a caption is shown) precede
"armed" (step 0x1a; set-play counter `MI+0x11ac` = 1). The referee whistle clip always starts at counter 2.
INF(med): the AI kick command is not a fixed delay per kind — corners entered at 191, 191, 380, FKs at 164, 164,
goal kicks at 246 and 84 — consistent with a decision re-evaluated at intervals (corner gap 189 frames); the deciding
code (set-play `waitTimer*`, `ballplayerSetplay.o.waitTimerMin`) was not decompiled (§9).

### 8.2 Officials during restarts

| restart | referee | assistants |
|---|---|---|
| kick-off | `(−dir·5, −dir·10)` (OBS code + live) | second-last-defender line (live ±19.5), z = ±35.5 |
| corner | (±35.63, ∓11.16) = (±(F[6] − 0.5), ∓(F[5] + 2)) (live ×3; code protected) | corner half: x = ±F[0], on the corner's touchline ±(F[0] + 0.2) or ±(F[0] − 1.2) (OBS code + live) |
| free kick | live (13.14, −6.71) and (−13.14, 6.71) (code protected) | R.x − sign·5 when the line is within 5 m (OBS code + live) |
| goal kick | live (4.0, 6.66) and (4.0, −11.66) (code protected) | generic line (live −44.56 / 10.0) |
| penalty | (±40.14, ∓14.16) (OBS code) | not decoded |

All officials are teleported in the last tick of the 0x14 window (live), and z = ±35.5 for both assistants in every
capture. The referee's in-play diagonal resumes through `FUN_140a37910` once in play.

---

## 9. Open items (with what was tried)

1. **CLOSED (pes-re-protection.md 3.3: all six modes decoded, throw-in included; ported as `pes::ChiefRefereeSpot`).** **Referee placement for goal kick / corner / free kick**: the chief's MODE 3/4/5 handlers are `.impdata` thunks
   (0x144161f30, 0x14415d330, 0x144161790) outside the dump; the on-disk code is mutation-obfuscated (push/lea/not
   trampolines). `protscan.py` recovered only ±1.0 sign constants and a `MI+0x155c` read. Live samples are given in §8.2.
   Next: emulate the thunk (unicorn) with a fake MI/F, or capture more restarts.
2. **The AI kick-command timing** (counter 164/191/246/84/380): the set-play AI (`ThinkUnit*` / `ballplayerSetplay.o`
   `waitTimerMin`) was not decompiled. Next: watch the AI command struct for the kicker during the wait, or find writers of
   the kick request consumed by `CornerKick::CanEnter` (thunk 0x143fb8d40, protected).
3. **Defensive marking at corners/FKs** (who marks whom, the zone-template constants x = F[8], z = 2.0/0.5/5.99/−6.0,
   the line at 40.05 in the FK): seen live only; `ThinkUnitCornerKickDefenceType` and the FK line code not decompiled.
4. **Attackers outside the CK table** ((dir·32.21, 7.8 / −12.6) and the rest defence) and the corner-variant choice
   (`cornerKickType` vs a roll).
5. **Wall spacing and the jump decision** (`ThinkUnitFreekickWallJump/NotJump/Press`): only the request timing is
   observed (2 frames before contact, ×2). Multi-man walls were not observed (both live FKs had d ≥ 35 m).
6. **Penalty in a match**: no live sample; `positionPK` in-match use unproven.
7. **Where the 20-tick placement writer lives** (the match-side consumer of `MI+0x11b0`): handshake and order are
   observed; the writer was not located (tried: `FUN_14044bb40` → `FUN_140453370`, not decompiled).
8. **Kick-off two-player path** (`KickoffKicker`/`KickoffKickerLoop`) is decompiled but never ran in the AI kick-off;
   when it is used (user control, INF) is open.
9. **A goalkeeper took corners and free kicks** in the second capture (the same AnimePlayer that took the goal kicks and
   stood in goal at the kick-off); while he was up, one team-mate's AnimePlayer sat at (0, 0, 0). INF(med): the team's
   designated takers (`T+0x8e08`) in this modded install point at the keeper; the (0,0) player is an unfilled
   `positionCK` ordinal. Not resolved.

---

## 10. Ledger entries

| ID | Claim | Conf. | Evidence |
|---|---|---|---|
| EV-SPC-0001 | Set-piece placement is a teleport inside the 20-tick step 0x14, under the wipe, in group order attackers → defenders → kicker → officials | Confirmed | live frame-by-frame, corner/FK/kick-off; `FUN_14044a2a0`, `FUN_14044bb40` |
| EV-SPC-0002 | Wall = R − 9.15 m toward the goal centre (1 man) or 1.3 m outside the near post (≥ 2) | Confirmed (1-man, live ×2) / High (≥ 2, code) | raw 0x1404a9e62–0x1404a9fa2; live |
| EV-SPC-0003 | Wall size from d and angle, `FUN_1404a9920` table | High (code) / Confirmed for n = 1 (live ×2) | decompile + constants |
| EV-SPC-0004 | Wall members taken from the defending slot 10 downward, skipping busy markers | High | `FUN_1404a9ce0`; live |
| EV-SPC-0005 | Corner attackers = `positionCK_N_V` authored positions, mirrored by attack direction and corner side | Confirmed | live ×2 within 0.02 m |
| EV-SPC-0006 | AI kick-off is one player passing backwards from `KICKOFF_RECEIVER_LOOP` (SHORT_PASS) | Confirmed (1 live) | live; `FUN_140781fc0` |
| EV-SPC-0007 | Kick-off layout: players at ≥ 10.15 m from the spot, ≥ 1.5 m from halfway, keepers on the goal-area line | Confirmed (1 live) | live |
| EV-SPC-0008 | Referee kick-off (−dir·5, −dir·10); PK (±(F[6]+4), ∓(F[5]+5)) | Confirmed (kick-off live) / High (PK code) | `FUN_140a38a00`, `FUN_140a38a70` |
| EV-SPC-0009 | Assistant corner/FK rules (`FUN_140a39330`, `FUN_140a394d0`) | Confirmed | code + live |
| EV-SPC-0010 | Corner ball 0.90 m from the flag; kicker 4.15 m back on the `fk_runup` line | Confirmed (live ×3) | live |
| EV-SPC-0011 | Out-of-play hold 2.5 s base (`FUN_14044e460(5)`) | Confirmed | live 136–140 frames; code |
| EV-SPC-0012 | Kicker selection rules per MODE (`FUN_1404a7d60` + `FUN_1404a8620/8ad0/90d0`) | High | decompile |
| EV-SPC-0013 | MODE 7 = kick-off after a goal | Confirmed | live (goal stop → MODE 7); `FUN_1404a7d60` case 1/7 |
| EV-SPC-0014 | Wall jump requested ~2 frames before contact, random 1–5-frame start delay | Confirmed (live ×2) / High (delay, code) | live; `FUN_1407400d0` |
