# PES 2021 open-play team defence: line, block, pressing, marking, state transitions (DefensiveShapeRE)

Image: `pes_full.bin` (Unpacker), decompiles via `dec.sh` + `annot.py`, raw via `pdis.py`. Scratch: the defshape scratch dir
(`ds1.c`… decompiles, `calls.py` call-xref, `argr8.py` state-immediate extractor, `sample.py` live sampler, `live1.*`).
Tags: **OBS** = decompile/raw at a stated VA or live read; **INF(conf)** = inference.
Notation: `T` = team record `FUN_140a48a30(G, side)`, `slot` 0..10, `idx = side·11 + slot`, `dir = (s8)T+0x254`,
`MI = *(G+0x270)`, `MA` = analysis object (`*(ctx+0x18)`, offside report: `MA+0x38+0x14·t` = {front x, mid x, back x,
offside line, id}), ball `MA+0x2482c/+0x24834` (x/z). `fps` = `FUN_141e532e0()`.

## 0. Live-reading notes (OBS)

- Registry entries `G + k·0x28` are non-null only part of each tick (≈5 % of 200 k reads non-null); poll until non-null,
  then cache — the values are stable for the match. Layout: RTTI vtable at `+0x10` of a record, data pointer at vtable+0x18
  (so `TeamAIInfoRef` vt at `G+0xa0·0x28+0x10` → data `G+0xa1·0x28` = `T0`; `FieldInfoRef` → `G+0x2aa8`).
  Named via `rtti_all.json`: 0x1 `BallInfoRef`, 0xf `MatchInfoRef`, 0x11 `MatchEnvRef`, 0xa0/0xa1 `TeamAIInfoRef`,
  0xa2/0xa3 `OrderInfoRef`, 0xbe.. `PlayerMoveRef`, 0xd8.. `DemoPlayerInfoRef`, 0xf2.. `PlayerInfoRef`, 0x108
  `AnalyzeInfoRef` (data slot stayed null in 20 k polls), 0x10e `BallAttachRef`, 0x10f `SituationInfoRef`, 0x110 `FieldInfoRef`.
- **Ball (INF high):** `*(G+0x40)` (BallInfoRef data) `+0x88` = live ball xyz: continuous over 60 polls at 20 Hz (max step
  < 2.5 m / 0.05 s), within 3 m of a player in > 50 % of polls, y = 0.11 on the ground. `pes_state.py`'s cached signature hit
  (stuck near (0.5, 0.8, 0)) is **not** the match ball. `+0x0c/+0x50` hold event points (jump, hold ≥ 1 s).
- `T1 − T0 = 0x182a0` (records are contiguous): any read of `T` longer than 0x182a0 overlaps the other team.
- perf execute breakpoints were unavailable (debug registers busy: `perf_event_open` → ENOSPC), so `MA` was not resolved live.

## 1. Where open-play defence runs (OBS)

- Team tick `FUN_1404f6c30(brain, ctx, T)` (`brain+0x6d48` = ctx, `+0x6d50` = T, `+8` = side) → **`FUN_140535c60(brain+0x4200,
  &{ctx, ?, side}, T)`** at 0x1404f7955, then `FUN_14057ba50` (OFFSIDETRAP, 0x1404f7998). Every defensive action-state write
  except set pieces is reached from `FUN_140535c60` (call graph of all 63 `FUN_140a42940` sites, `calls.py` + `argr8.py`).
- **Cadence (OBS 0x140535c60):** `q = MI+0x1198 & 3` (frame counter mod 4); stopped with `MI+0x11d4 == 0` → `q = 2`.
  CPU team only (`T+0x255 == 0`; the human team runs just the mark pass every 4th frame and resets `T+0xa50c..0xa538` to 5):

  | when | call | writes |
  |---|---|---|
  | q = 0 | `FUN_1405379c0` | `T+0xa50c` (ball) / `T+0xa510+4·slot` (players) = `FUN_140a510f0(ctx, side, pos, prev)` — a zone index per point (INF: pitch zone) |
  | q = 0 | `FUN_1406369e0` | `T+0x6ec = 3` |
  | q = 0 | `FUN_1405360d0` | `T+0x6f0` = `FUN_1406293f0(ctx, presser)` for presser `FUN_140a62960` (in play only) |
  | q = 0 | `FUN_140536170` | `T+0xa53c` = `FUN_140535650` (+ start frame `T+0xa540`) |
  | q = 0 | `FUN_140535a10` | `T+0xa56c` = designated player: on-pitch slot passing `FUN_140535350`, nearest to ball (sticky: current one gets −5 m), kept ≤ 1 s (`fps` frames) after losing eligibility; only after `MI+0x119c ≥ 3·fps` frames of play |
  | q = 1 | `FUN_1406377f0` | `GOAL_COVER` |
  | q = 1 | `FUN_1406315b0` | `COVER` (§4) |
  | every frame | `FUN_140534a20` | `MARK` (§3) |
  | every frame | `FUN_140635ab0` | `PASS_COURSE_CUT` |
  | every frame | `FUN_140536210`, `FUN_140634470`, `FUN_140536690`, `FUN_140536fc0` | `MATCH_UP`; `PRESS`/`SAND`; `NOTHING`/`PRESS+k`; `PRESS`/`DELAY` |
  | every frame | loop at 0x140535c60+… | slot whose `T+0xb748+4·slot == 0x26` and state ∉ {DELAY, PRESS} → `DELAY` if `FUN_14062af10(ctx, idx)` else `PRESS` |
  | every frame | `FUN_140628400(…, 4, q)` | mark spot `T+0x26c+0x40·slot` (§3.4) |
  | every frame | `FUN_140525b50` | FK wall (set-piece report §11.8) |

## 2. Action-state transitions: the priority gate (OBS, closes "when does a defender switch")

State value `v` names = string table entry `v + 0x13` at 0x1434a4cb8 (`NOTHING` = 0, `BASE_POSITION` = 1, …). The writer
`FUN_140a42940(T, slot, v)` stores `T+0xa2a0+4·slot = v`, then calls `FUN_140a40200(T+0xa2cc+0x34·slot, v, T+0x21c)`
and tail-jumps to `FUN_140a85aa0(T+0x28c+0x40·slot, v)` (both not reduced). `FUN_140a40820(T, slot)` = state 0 +
`FUN_140a40200(…, 0, …)` + zero `T+0x28c..0x2ab+0x40·slot`.

**Gate `FUN_140a40000(cur, new, idx, T)` = `prio(new) < prio(cur)`** with `prio = FUN_140a40110` → table `FUN_140a40050`
(jump table 0x140a40094/0x140a400a8, raw-decoded):

| prio | states (defence-relevant in bold) |
|---|---|
| 0 | **MATCH_UP 0x3a**, **OFFSIDETRAP 0x3b**, PASS_SUPPORT_THROWIN, PASS_AND_GO_RUN, ONE_TWO_PASS_RUN, TEAMMATE_MOVING, SHORT_CORNER_MOVE, GK_COACHING_RUN |
| 2 | **DELAY 0x33, PRESS 0x34, SAND 0x35, GOAL_COVER 0x3c**, SPACE_RUN_*, LINE_BREAK, DIAGONAL_RUN, OVERLAP_SB/CB, GOAL_GET, POST_PLAY |
| 3 | **MARK 0x36, DELAY_MARK 0x37**, MOVE_ON_PASS, CENTERING_GET, PULL_AWAY, WALL_JUMP/NOT_JUMP/PRESS |
| 4 | **COVER 0x32, PASS_COURSE_CUT 0x3d**, PASS_SUPPORT, POST_MAN |
| 5 | NOTHING 0, **BASE_POSITION 1**, every kick/keeper/fall state, anything < 0x10 or > 0x6d |

`FUN_140a40110` returns the table value directly when it is < 3; for prio ≥ 3, re-requesting the slot's *current* state
returns 2 when `cur == BASE_POSITION` and flag `T+0x29c+0x40·slot` is set, or `cur == PASS_SUPPORT` and `T+0x290+0x40·slot`
is set. Consequences (OBS by the table):
- BASE_POSITION/NOTHING → anything defensive. MARK (3) can be overridden by MATCH_UP/PRESS/DELAY/SAND/GOAL_COVER, never by
  COVER or PASS_COURSE_CUT (4). MATCH_UP/OFFSIDETRAP (0) are never overridden by the gate; they end only by explicit reset.
- **COVER on a marker is queued, not switched (OBS 0x1406315b0):** if the cover candidate is in `MARK`, the cover pass
  writes the secondary state `T+0x278+0x40·slot = COVER` (`FUN_140a42b80`) and the point to `T+0x28c` block
  (`FUN_140a42b50`) instead of calling the writer; when the mark is later dropped with `T+0x278 == COVER`, the mark pass
  promotes it: `FUN_140a42940(COVER)` + point `T+0x26c` (`FUN_140a42a40`) + byte (`FUN_140a42a30`) (0x140534a20 lines 195–201).
- Every 4 frames (q = 1) the cover pass first clears all `COVER` states (`FUN_140a40820`) and secondary `COVER` slots,
  then re-assigns ≤ 2 covers — so COVER is re-decided at 13.5 Hz (54 fps / 4).

## 3. Marking in open play — `FUN_140534a20` (OBS)

Runs every frame for the CPU team when in play (or stopped and `MI+0x11b0` stage 1..21, setpiece report §11.5), mode ≠
kick-off, and `FUN_140a604d0(level, 3)` (INF: the team's marking level enables it).
1. **Opponent order list:** for each opponent slot `j`: key = `−dir·(x + vx·fps/6)` with `x = P+0x55c`, `vx = P+0x574`
   (INF: per-frame velocity → 1/6 s look-ahead) — most advanced opponent (towards our goal) first; throw-in supporters
   (state 0x23) get `10000 − x·dir`; MODE 4 (corner, stopped) uses the slot index. Sorted descending
   (`FUN_141e53540`, comparator `0x140533c70` = `b.key − a.key`). All `T+0x558[slot]` (mark target) cleared to 0xff first.
2. **Assignment `FUN_1405346e0`** = seven passes in fixed order, each filling `brain+0x4264+0x84[opp] = def slot` and
   `+0xb0[def] = opp`, weight `+0x1d34[def] = 1.0`:
   1. `FUN_140531810` **first defender on the carrier** (§5.2).
   2. `FUN_140531260`, `FUN_1405325e0` (sets a flag passed to 4–7), `FUN_14052f4f0`, `FUN_140530320`, `FUN_14052f720`,
      `FUN_140532f70` — decompiled (`ds3.c`), not reduced.
3. **Hand-off hysteresis (OBS 0x1405346e0 post-loop):** a changed target for defender `s` is committed only after the
   same new target has been pending for more than `⌊w·60⌋` frames (pending target `+0x1ea8+4s`, start frame `+0x1ed4+4s`),
   with `b = FUN_140a62140(ctx, idx, r)` (byte; mean of two `FUN_140a61f40` reads) and
   `w = 1 (b ≤ 10), (50 − b)/40 (10 < b < 50)`; `b > 50` (or not in play) → immediate. `r` = player of team-sheet entry
   `T+0x8e08+0x1438` (`FUN_140a3b320`). Constants 10/50/60/−40 at 0x142588210/0x14259be34/0x14259a764/0x1425c6604.
   **Max hand-off delay = 60 frames ≈ 1.1 s.** INF(med): `b` is a 0..99 rating (team-sheet captain/organiser) — a better
   organiser hands markers over faster.
4. **State:** `FUN_141b92460(2)` is a stub returning **5** (OBS 0x141b92460), so `3 < 5` always holds: every defender with
   a target `T+0x558[slot] < 0x16` gets `MARK` iff `prio(MARK)=3 < prio(cur)` (i.e. from BASE_POSITION, NOTHING,
   COVER, PASS_COURSE_CUT …). Defender without a target in `MARK` or `MATCH_UP`: promote queued COVER (above) or reset
   (unless `GOAL_COVER`).
5. Not in play/disabled: `FUN_14052f460` clears targets and resets every `MARK`.

### 3.4 The marker's spot — `FUN_140628400` (OBS) and its refresh rate
Per slot with a target: the spot `brain+…+0x584+12·slot` is recomputed by `FUN_140623750` (1900-line body, not reduced)
every `N` frames (`MI+0x1198 % N == 0`), else kept, and copied to `T+0x26c+0x40·slot` when the state is MARK/COVER/NOTHING.
`N = k + FUN_140a603c0(level, 2)` with **k from the defender's in-match byte `+0x15` (Defensive Awareness, matchlogic §1.3 INF
high): ≥ 96 → 1, 91–95 → 2, 81–90 → 4, 71–80 → 6, ≤ 70 → 8**; k = 1 also when in-match flag bit 22 of `+0x40` is set
(INF(med): the "Man Marking" skill); k capped at 2 when plan flags `(0xc,·)` or `(0xb,·)` are armed (`FUN_140a63950`).
(Literals are instruction immediates 0x60/0x5b/0x51/0x46 in 0x140628400.)

## 4. Cover — `FUN_1406315b0` (OBS), every 4th frame (q = 1)

Gate: `FUN_140ca1a50(0x13)` (`mov al,1; ret` per team-plans §1 — always true) and in play; skipped when the situation is
`{3, ·, …, [7] = 4}` and `FUN_140a72e70(FieldInfo, ball)` bit 10 (INF: own penalty area at a GK restart). CPU team only.
1. Clear every `COVER` state and every queued `COVER` (`T+0x278+0x40·slot`).
2. **Who is covered:** the own slot in `MATCH_UP` (0x3a) other than `T+0x78`; if none, the highest on-pitch slot ≠ `T+0x78`.
3. Up to **two** cover men `aiStack_64[0..1]` and their points from `FUN_14062ef30` → `FUN_14062d350` → `FUN_140632210`
   (gate) → `FUN_140631de0` → `FUN_14062c5a0`×2 → `FUN_14062f8c0` → `FUN_140631cb0`/`FUN_140631b60` (not reduced). Point is
   stored `×dir` (own frame → world). Candidate accepted iff its state is already COVER/… (`(s − 0x32) & ~4 == 0`, i.e. COVER or
   MARK) or the gate allows COVER; MARK → queued (§2), else `FUN_140a42940(COVER)` + point `T+0x26c` + `FUN_140a42a30` byte.
4. The pass caches its result in `brain+0x4208+0x14..0x4b`.
- **Drop-back on cover (OBS `FUN_1405f4f50`, run only when frames since the last possession event `FUN_140a74ec0` > 4·fps):**
  the deepest COVER man (own-frame x of his body or his cover point, whichever is higher) pulls his line's front
  (`T+0x87d0+8·slot` = his line 0/1) down to `max(coverX, front − k)`, `k = 6.0` (0x14259bdec) for the back line,
  `|front₁ − front₀| / coverLimitXRate` for the midfield line, floored at `−F[4]` (−47.06); a back-line drop moves the
  back line's rear and the midfield front by the same amount.

## 5. Pressing

### 5.1 One man engages, ≤ 2 cover — live (OBS, 6428 defending frames, `live1.jsonl`)
| state of the defending team | per frame | distance to ball (p10 / p50 / p90, m) |
|---|---|---|
| `MATCH_UP` | exactly 1 in 94 % of frames, never 2 | 4.5 / 9.3 / 15.7 |
| `PRESS` | 0 or 1 (8 % of frames) | 1.6 / 9.1 / 15.8 |
| `COVER` | 0 (32 %), 1 (21 %), 2 (47 %), never 3 | 11.3 / 14.7 / 29.7 |
| `MARK` | median 3, up to 8 | 12.9 / 27.7 / 41.3 |
| `PASS_COURSE_CUT` | 0–3 (mostly 1) | 13.6 / 26.5 / 29.7 |
| `DELAY`, `SAND` | never seen | — |

INF(high): the "presser" is the `MATCH_UP` man (the first defender of mark pass 1); `PRESS` is a short extra burst.

### 5.2 First defender on the carrier — mark pass 1 `FUN_140531810` (OBS)
- Carrier `c = FUN_140a627e0(ctx, 0)` (else the opponents' `T+0x78`), team chaser `u = T(own)+0x78`. Skipped for the human team,
  `u` invalid, or situation `{3, …, [7] ∈ {4,5}}`.
- **Keep the chaser on the carrier** if `|u − c|_xz < 8.0 + (2.0 if u already marks c)` (0x14259bdf4, 0x142565604) and `u`
  is not in a fall/action class (`FUN_140529d30`) — plus the body-state checks on `u` (`+0x20` ∈ 0x12..0x16, 0x18..0x19;
  `FUN_140a6e150/5e0/340` with in-match flag bits 22/9) or, when the ball is higher than 1.5 m (`*(*(G+0x40)+4)`, BallInfo y;
  0x14259bddc) and `FUN_140a6ce00` is false, `u` within 5.0 m (0x14259222c) of the ball. These set the "eligible" flag; the
  8(+2) m distance test then decides.
- **Team-sheet man-marking** (`FUN_1408c50a0(T+0x8e08, slot, 3)`, the human/plan "man marking" assignment): each own slot
  with a sheet target on the pitch gets it, except FK wall men (MODE 5, `FUN_140a3c9b0`) and when `FUN_14052c440` (in play)
  or `FUN_140a3c8f0` (stopped) vetoes.
- **Press-lane pick** (in play, ball state `FUN_140a55d40(ball, side, 3)`, no team-press plan `(0xd)`, `FUN_140529430` < 3
  = attack level (capped to 2 at a stopped FK), `FUN_140a53f90` false):
  - Reference bearing = `FUN_140a4db20(dir)` (straight back), replaced by ball→own-goal bearing when the ball is within
    **40 m** (1600 = 0x14259ce40) of the own goal centre.
  - Previous presser (`brain+0x5db0` = pass object `+0x1b4c`) is kept if `FUN_140a53d30` (distance ≤ **13 m**, cone
    **160°** full) passes and `FUN_140479470` allows.
  - `w = clamp((dir·ballX − (dir·L + 10)) / 15, 0, 1)`, `L = MA+0x44+0x14·side` (own offside line) — 10/25 at
    0x142588210/0x14259be20. Search radius **10 + 5w m**, full cone **70 + 130w °** (0x14259be4c, 0x1425a9bf0).
    (`FUN_140a4fbc0`, a 3–15 m ring count, is called first; its return value is discarded in the decompile.)
  - Candidates in list order `MA+0x2470c+8·(side·11+k)` (INF: by arrival time to the ball); angle `a` between the
    reference bearing and ball→candidate bearing gives a bonus `2.5·{1 (a ≤ 20°), (65 − a)/45, 0 (a ≥ 65°)}` m
    (20/65/−45/2.5 at 0x14259be1c/0x14259be48/0x1425aa1c8/0x14259bde4) taken off the candidate's distance. The first
    candidate inside the cone wins; it replaces the previous presser only if `|prev − ball| ≥ (d − bonus) + 1.5`
    (0x14259bddc). Winner → `brain+0x5db0`, paired with the carrier (weight 1.0).
- The engaged man's state is `MATCH_UP` (writer `FUN_140536210`, not reduced; prio 0 so nothing overrides it).

### 5.3 When the team presses vs drops — what is closed
- **Line-up / block choice is the defensive-line formula (§6)**: the line height is a ramp of ball depth; the press
  geometry widens with ball distance from the own line (§5.2). The fore-check gate, count test and `ActionPress` target
  of team-plans §4.1/§4.2/§4.5 stand (not re-derived).
- **Retreat flag `T+0xc12a` = `FUN_140533d10`** (written every frame by `FUN_140535c60`; structure OBS, condition
  semantics INF(low)): CPU team, in play (or MODE ∉ 2..4), not in situation `{3, …, [7] ∈ {4,5}}`, and team level
  `FUN_140a60340(FUN_140a40bd0(T)) > 1`. Reads ball `MA+0x2482c/+0x24834`, the outlet index list `FUN_140a71500(MA, dir)`,
  carrier / opponents' `T+0x78`, their velocity `+0x574/+0x57c` projected `fps·0.1+0.5` frames, the ball predictor
  (`FUN_140a57720/57800`, ball speed `> 3.0`), plan `(0xc,·)`, and returns 1 through distance/angle tests with literals
  1.0 / 15 / 13 / 2.5 / 1.3 (−0.2 when already set) / 10 / 135° / 160° / 38 / 5 / 85° / 70° / −30° / 25 / 0.5556
  (0x14256ae1c/0x14259be0c/0x1425c5948/0x14259bde4/0x14259bdd8/0x142588210/0x14259be60/0x14259be6c/0x1425c65ec/
  0x14259222c/0x14259be58/0x14259be4c/0x14259be88/0x14259be20/0x1425c65e4) or an arrival time > `FUN_1404f6110()`.
  Not reduced to a rule; consumers not traced (open item 7).

## 6. The defensive line — `FUN_1405f9eb0` (OBS; live-checked)

**Where:** team tick → `FUN_14051d9e0` → `FUN_1405ec600` → … → line builder **`FUN_1405fbcb0(ctx, S, ·)`** with
`S = T+0xc48`: `ctx+0x10c == 0` (out of possession) → `FUN_1405f9eb0` + `FUN_1405fa990`; else (in possession)
`FUN_1405fb6f0` + `FUN_1405fb2d0` (block `+0x3c`). Then `S+0x7c3c = S+0x7b14`, `FUN_1405f4f50` (§4) when > 4·fps frames
since the last possession event, then `FUN_1405efbd0`. Output: three line bands `S+0x7b10+0x14·line+0x3c·phase` =
`{rear x, front x, …}` (live: back line rear = front − 3.0, forward band 5 m deep), **own attack frame** (x toward the
opponents' goal, origin centre spot, metres). World x = `dir·x`. Live address of the defending back-line front:
`T+0x875c`; pre-adjust value `T+0x8880` (`S+0x7c38`).

Inputs: `bx = ctx+0x100` (ball x, own frame), `F` = FieldInfo (`F[0]` 52.56, `F[4]` 47.06, `F[6]` 36.14, `F[8]` 41.56,
`F[0x11]` 9.15), `bp` = live `basePosition.o` (vtable 0x142b5a328, read at runtime this session), `L` = attack level
`T+0xb8a4` (0..4), `v2/v3` = team-sheet bytes `T+0x8e08+4+5·plan+{2,3}` (`FUN_1408c52e0(sheet, k, 3)`, plan = `*(sheet)`;
INF(med): Defensive Line / Compactness 1–10), style = `FUN_140a62180(ctx, side, 4)` (INF(med): Defensive Style 0/1).

```
// RECONSTRUCTION (OBS 0x1405f9eb0), own frame
a  = style==0 ? bp.adjustDefenceLine_ForeCheck(-9) - 10*clamp((v2-1)/9)            // 9 = 0x14259d60c, -10 = 0x14259be84
    : style==1 ? 5*trig(90*clamp((v2-10)/-9)) + bp.adjustDefenceLine_Retreat(-11)  // 0x14259be5c, FUN_140a92c80
    : 0
hi = 0.6*F[0]  + bp.dfLineCenterAdjustMax(-8)  + a                                // 0.6 = 0x14257a190
lo = -0.2*F[0] + bp.dfLineCenterAdjustMin(-20) + a                                // -0.2 = 0x1425c8210
if inPlay: hi,lo -= ramp(L;2→0,3→1) * bp.adjustDefenceLine_strategy_defensive(1)
if FUN_140a40bc0(T)==3: hi,lo += bp.adjustDefenceLine_strategy_levelup(-14)
w    = clamp((bx-lo)/(hi-lo), 0, 1)
deep = 1 - F[6]  (+5 if plan (0xd,·) armed)                                       // 5 = 0x14259222c
x    = deep - w*(deep + 5)            -> S+0x7c38                                  // deep .. -5
if !ctx[0x138]:                                                                   // normal (not transition mode)
   MODE 3 (goal kick): x -= 5*ramp(L;0→1,2→0)
   else if x > -F[6]: x += (L==2 ? -5 : 0) - 3.75*ramp(L;1→1,2→0); x = max(x, -F[6])  // -5 0x14259be7c, 3.75 0x1425cd064
else: ctx+0x140 -> x = dir*ctx[0x13c]+15 ; else ±(ctx[0x134]-0.5)-based ±15 / ±7.5 shifts
g = bp.marginTargetLineX(-10) * (bp.keepDfTargetLineX>0 ? FUN_1405f7a00() : 1)
x = min(x, bx + g)   (MODE 5 stopped: dir*R.x + g)                                 // never closer than 10 m to the ball
if T+0xb8a3==0: FUN_1405ef910(ctx,&x)                                             // not reduced
x = min(x, 0); stopped throw-in/FK special cases; x = max(x, -F[4])                // never past halfway; >= -47.06
S+0x7b14 = x                                                                      // back-line front
len = lerp(style==0 ? bp.lengthDf(46) : bp.lengthDf_Retreat(42), bp.lengthDf_SuperRetreat(32), ramp(L;0→1,2→0))
len += {10, 7.5, 5, 2.5, 0}[team level FUN_140a60340 = 0,1,2,3,≥4]
fw  = x + len - (v3-1)*bp.adjustXCompactFW(0.33)                                   // forward line rear
fw  = max(fw, bx) if (T+0xa53c set ≥ 1 s, throw-in, or T+0xa56c valid); GK/FK stopped: fw ≤ -opp(T+0x8798)
S+0x7b38 = clamp(fw, 3 - F[0x11], F[8])                                            // [-6.15, 41.56]
```
- **No time smoothing in the line target:** it is recomputed every team tick from the current ball. "Step-up speed" is the
  players' own locomotion toward the band; the only timers are the 4 s gate on the cover drop (§4) and, in possession
  (`FUN_1405fb6f0`), a 20 m hold-back after a possession change that fades between 5 s and 8 s (`FUN_140a74ec0` frames vs
  `fps·5`/`fps·8`, 0x14259222c/0x14259bdf4).
- `FUN_1405efbd0` (defending, in play): shifts the back line (rear and front) up by
  `k·|midFront − backFront| / bp.adjustDiv(4)`, `k = ramp(d; bp.mfLineScore_adjustMax(36) → 1, 200 → 0)`,
  `d = FUN_1405f80c0()` (stored `S+0x7c50`; not reduced).
- `FUN_140687fb0` (shape pass, in play, `T+0xb70d == T+0xb70a == 0`): back-line front pulled `dir·4` m (15 m with `ctx+0x138`)
  behind its band, held 6 m behind `FUN_14063fdc0`'s point, capped at own-frame −5; all three bands shift together.
- **In possession (`FUN_1405fb6f0`, OBS):** line-2 rear `x = (8a − 4) + (F[6] + 7 − (8a − 4))·w`,
  `a = clamp(L/4)`, `w = ramp(bx; bp[0xc] − F[6] + 20·hold → 0, F[6] + bp.adjustFrontOF(−5) → 1)`; back-line front
  `= x − lengthOf(40)·(1 − 0.5·dfLineRate(0.09))`, clamped to `[−F[6], 2.0]`; kick-off: front = `kickoffDfLineX` (−12.0).

**Live check (OBS, `linecheck.py`, 190 one-second samples with one team clearly defending, style 0 and v2 = 4 assumed):**
pre-adjust `T+0x8880` vs the reconstruction: **median |e| 0.38 m, 53 % within 0.5 m, 67 % within 2 m**, RMS 3.7 m;
final front `T+0x875c` vs reconstruction (without §4/`efbd0`/`687fb0`/`ef910` passes): median |e| 1.56 m. Empirical
defending back-line front by ball depth (own frame, mean): bx −45 → −44, −35 → −39, −25 → −31, −15 → −24, −5 → −18,
+5 → −12.6, +15 → −8, +35 → −4.7. The outfield back four sit on the front (median offset +0.7 m, p10 −8.8, p90 +11.8).

## 7. Lateral shift and width — `FUN_140688300(ctx, bands, line)` (OBS; replaces team-plans §2.1's reading)

Called from `FUN_140681ff0` for line 0, 1, 2 after `FUN_140682320` sorts the 11 slots into lines by role class
(`ctx+0x128[slot]`: 3–6 → line 1, 7–9 → line 2, else line 0; count `bands+0x58+0x68·line`, on-pitch count `+0x59`).
Outputs the **z band** `bands+0x60+0x68·line` (left edge) and `+0x64` (right edge = left + 2·half). If
`dir·ctx[0x108] > 0` (INF: attacking toward −z camera side) the three bands are mirrored (`FUN_140640c80`).
```
// RECONSTRUCTION (OBS), n = players in the line, F = FieldInfo, F[1] = 34.06 half-width
lat   = clamp((|ctx[0x104]| - 6) / (F[7] - 5 - 6), 0, 1)          // ball |z|: 6 m .. 15.12 m; 6 = 0x14259bdec
lvl   = ramp(L; 0→1, 2→0)                                         // attack level T+0xb8a4
half0 = restart ? bp.dfLineWidth_Corner(6.0)
      : penalty MODE 6 vs us ? F[5] + 6
      : n==3 ? 15 : n==4 ? 19 : n>=5 ? 23 : 10     (bp.dfLineWidth_3/4/5; 10 = 0x142588210)
half0 += compactAdj ;  compactAdj = -bp.adjustZCompact(2.85)*clamp((v3-5.5)/4.5,-1,1)   // FUN_14063f230
if inPlay || MODE != 3: half0 -= 6*lvl  else: half0 -= bp.adjustDfLineWidth_GoalKick, (line 1, n==3: -2)
minHalf = (n-1) * 0.5 * (inPlay&&!restart ? lerp(bp.minWidth 6, bp.minWidth_stratagy_defensive 5, lvl) : bp.minWidth_GoalKick 2)
half  = clamp(half0, minHalf, 0.85*F[1])                          // 0.85 = 0x1425ab120 -> <= 28.95
// ball-side squeeze (in play / throw-in / FK, FUN_141b92460 stub = 5 > 1):
k1 = ramp(ctx[0x100]; -F[6] → 1, 10 → 0)                          // ball depth
d  = |dir*MA.lineBack(+0x40+0x14·side) - ctx[0x100]|; k2 = ramp(d; 7→1, 20→0) (or 12→1, 24→0 within 7·fps of an event)
k  = max(k2*bp.lastLineCloseMaxRate(0.34) [full if ball behind the back line], k1); MODE 5: max(k, 0.4)
r  = clamp(bp.dfLineCloseRate(0.61) + {0.4, 0.25, 0}[team level 0,1,≥2], 0, 1)
half += (r*half - half) * k * (1 - 0.6*lat')                       // squeeze toward the ball when it is close/deep
edgeL = -half - (-half + ballSideTarget)*lat*c ;  edgeR = edgeL + 2*half     // c = 0.7/0.3/0.5/0.9 by line and n
edges clamped to |z| <= F[1] - 0.5
```
(`ballSideTarget = max(min(1·or 0.7, …)·F[1], half)` with the `(0xb,·)` plan forcing 1.0; exact per-line `c` table:
line 0: n < 2 → 0.3, n = 2 → 0.5, n ≥ 3 → 0.7·lat-capped; line ≥ 1: 0.6 / 0.9 — raw 0x140688a40–0x140688ab8, INF(med) on the
branch mapping.) Line 2 (forwards) uses `(n−1)·minWidth` vs a 7 m floor (40 m cap with the sheet flag and `bp+0x109`) and
`0.7·F[1]` as the far edge; with `bp.defenceFormationTest1` (+0x108) set it uses the pitch-width bands instead.

## 8. Tactics inputs (OBS reads; values = live `basePosition.o` at 0x33ddb370 this match)

| input | where read | value | feeds |
|---|---|---|---|
| `adjustDefenceLine_ForeCheck` +0x18 / `_Retreat` +0x1c | `FUN_1405f9eb0` | −9 / −11 | line ramp offset by Defensive Style |
| `adjustDefenceLine_strategy_defensive` +0x20 / `_levelup` +0x24 | same | 1 / −14 | ramp shift at attack level > 2 / special level 3 |
| `dfLineCenterAdjustMax/Min` +0x14c/+0x150 | same | −8 / −20 | ramp ends |
| `marginTargetLineX` +0x248, `keepDfTargetLineX` +0x210 | same | −10 / 1 | line ≥ 10 m behind the ball |
| `lengthDf/_Retreat/_SuperRetreat` +0x21c/0x220/0x224, `lengthOf` +0x228 | f9eb0 / fb6f0 | 46/42/32, 40 | team length |
| `adjustXCompactFW` +0x6c | f9eb0 | 0.33 | length −0.33 m per Compactness step |
| `dfLineRate` +0x158, `dfLineRate_goalKick` +0x15c | fb6f0 | 0.09 / 0.1 | in-possession length |
| `adjustFrontOF` +0x38, `adjustBackOF` +0xc | fb6f0 | −5 / −3 | in-possession ramp |
| `adjustDiv` +0x30, `mfLineScore_adjustMax` +0x26c | efbd0 | 4 / 36 | back-line catch-up shift |
| `coverLimitXRate` +0xfc | f4f50 | 6 | midfield drop on cover |
| `dfLineWidth_3/4/5` +0x160..0x168, `_Corner` +0x16c | 688300 | 15/19/23, 6 | line half-width by count |
| `minWidth`, `_GoalKick`, `_stratagy_defensive` +0x284..0x28c | 688300 | 6/2/5 | min spacing per man |
| `adjustZCompact` +0x7c | `FUN_14063f230` | 2.85 | ±2.85 m width by Compactness |
| `dfLineCloseRate` +0x154, `lastLineCloseMaxRate` +0x218 | 688300 | 0.61 / 0.34 | ball-side squeeze |
| `defenceFormationTest1/2` +0x108/0x109 | 688300, 682770 | 0 / 1 | width mode / sheet-driven lines |
| team sheet `T+0x8e08` plan bytes k = 2, 3 | `FUN_1408c52e0` | team0 4, 6; team1 4, 8 (k0 = 9 / 4) | Defensive Line / Compactness (INF med) |
| attack level `T+0xb8a4` | all of §6–7 | 2 (both, whole sample) | −5 m line, width −6·ramp, length |
| Defensive Awareness (in-match `+0x15`) | `FUN_140628400` | — | mark-spot refresh 1–8 frames |

## 9. Live sample (OBS)
`sample.py` (read-only `/proc/pid/mem`, registry pointers polled then cached), 420 s, 8400 records at 20 Hz
(6428 defending-team records in play) + 420 one-second dumps of both `T` and 22 player objects. Files: `live1.jsonl`,
`live1.raw`, `bp_live_33ddb370.bin`. Checks run: line formula §6; state counts/distances §5.1; state transitions:
`NOTHING→MARK` 342, `MARK→NOTHING` 310, `NOTHING→COVER` 112, `MARK→MATCH_UP` 70, `MATCH_UP→MARK` 43, `COVER→MARK` 38,
`MARK→COVER` 33, `PRESS↔NOTHING` 20–25 — all consistent with the priority gate (§2): `MARK→COVER` only via the queued path,
`MATCH_UP→MARK` via the explicit reset of `T+0x7c`'s MATCH_UP in `FUN_140535c60` followed by the same-frame mark pass.

## 10. GF code each finding replaces
| finding | GF code |
|---|---|
| §6 line height (ball-depth ramp, −5 cap, ≥ 10 m behind ball, length 32–46 m) | `TeamAIController::GetAdaptedFormationPosition` depth terms (`position_defense_depth_factor`, `depth = 0.45`) and `ApplyOffsideTrap`/`offsideTrapX` (`AI_GetOffsideLine` slack) |
| §7 width by line count, compactness slider, ball-side squeeze | same function's width terms (`position_defense_width_factor`, `width = 0.95`) |
| §3 mark assignment (most-advanced-first list, seven passes, 1.1 s hand-off, DA-paced spot refresh) | `TeamAIController::CalculateManMarking`, `CalculateMarkingQuality` |
| §5.2 first defender (8/10 m keep, lane cone 10–15 m / 70–200°) + §4 two covers | `ApplyTeamPressure`/`ApplyZonePressure`, Eliza's `huntingPlayersNum = 2`/`huntDistanceThreshold` branch (elizacontroller.cpp ~520–620) |
| §2 state machine + priority gate | none in GF (Eliza has no per-player defensive state); new per-slot state array needed |

## 11. Killed hypotheses
- "`FUN_140688300` writes the ball-relative X shift of the base position" (team-plans §2.1): it writes the **lateral (z)
  band** of a line; `r13+4` is FieldInfo half-width 34.06, not the slot's base X; `dil` is the **number of players in the
  line**, not a role; `basePosition+0x160/164/168` are `dfLineWidth_3/4/5` (half-widths 15/19/23), not line depths.
- "`*(int*)(ctx[0x10]+0xb8a4)` is the match clock in ms with a 2 s start ramp": it is the attack level 0..4 (setpiece §11.6);
  the ramp is 1 at level ≤ 0, 0.5 at 1, 0 at ≥ 2.
- "`FUN_141b92460` is a 2-second timer": it is a stub that returns 5.
- "The line steps up through a smoothing filter (`FUN_14063f230`)": `FUN_14063f230` is the Compactness → ±`adjustZCompact`
  width offset; the line target has no smoothing.
- "Kick-off pair keeping `(1 − (r−10)/−40)·60`" (setpiece §11.5): the hand-off delay is `((50 − b)/40)·60` frames for
  10 < b < 50, 60 for b ≤ 10, none for b > 50.
- "`pes_state.py`'s ball is the match ball": it stays near (0.5, 0.8, 0) while play is at x ≈ 40.
- "`ct_vfs.bin` at file offset 864 + off − 8 gives basePosition values": decodes to garbage; the live object (vtable
  0x142b5a328) gives cornerKickOffencePlayerNumber 5, dfLineWidth_Corner 6.0, kickoffDfLineX −12.0 as reported.

## 12. Open list (exact next step each)
1. **Mark passes 2–7** (`FUN_140531260`, `FUN_1405325e0`, `FUN_14052f4f0`, `FUN_140530320`, `FUN_14052f720`,
   `FUN_140532f70`; decompiled in `ds3.c`): reduce each to its pairing rule; check against `T+0x558` in `live1.jsonl` (`mk`).
2. **Mark spot `FUN_140623750`** (1900 lines, `ds5.c`): reduce the goal-side/ball-side offset; check vs `T+0x26c+0x40·slot`
   in the raw dumps.
3. **MATCH_UP writer `FUN_140536210`, PRESS/SAND/DELAY writers `FUN_140634470`/`FUN_140633940`/`FUN_140536690`/
   `FUN_140536fc0`, PASS_COURSE_CUT `FUN_140635ab0`, OFFSIDETRAP `FUN_14057ba50`/`FUN_14057b820`** (all in `ds1.c`): extract
   triggers; live shows DELAY/SAND never used in 7 min.
4. **Cover point chain** `FUN_14062ef30`…`FUN_140631b60` (§4 step 3).
5. **Remaining line passes**: `FUN_1405fa990` (rear/mid bands), `FUN_1405ef910`, `FUN_1405f7a00`, `FUN_1405f80c0`,
   `FUN_14063fdc0`; then re-run `linecheck.py` on the final `T+0x875c` (target: median |e| < 0.5 m).
6. **Style / slider identity**: confirm `FUN_140a62180(ctx, side, 4)` = Defensive Style and sheet bytes k2/k3 = Defensive
   Line/Compactness by reading `T+0x8e08` for a team whose in-game settings are known (menu screenshot), read-only.
7. **Retreat flag `T+0xc12a` consumers**: `calls.py`-style displacement scan for reads of `0xc12a`.
8. **Per-slot target `T+0x4b4c`** (the zone template writer missing in setpiece §11.5): `FUN_140686190`, `FUN_140683db0`,
   `FUN_140685d70` (decompiled, `ds6.c`) write `param_3` = `T+0x4b4c` per slot from the bands — reduce and check vs `tg` in
   `live1.jsonl`.

## 13. Ledger entries
| ID | Claim | Conf. | Evidence |
|---|---|---|---|
| EV-DEF-0001 | Open-play defence dispatcher `FUN_140535c60`, cadence frame & 3 | Confirmed (code) | 0x140535c60, caller 0x1404f7955 |
| EV-DEF-0002 | State priority table (0/2/3/4/5) and gate `prio(new) < prio(cur)` | Confirmed (raw jump table) | 0x140a40050, 0x140a40000, 0x140a40110 |
| EV-DEF-0003 | COVER queued on markers via `T+0x278`, promoted when the mark drops | High (code) / consistent live | 0x1406315b0, 0x140534a20 |
| EV-DEF-0004 | Mark list most-advanced-first with 1/6 s look-ahead; hand-off delay ≤ 60 frames | High (code) | 0x140534a20, 0x1405346e0 |
| EV-DEF-0005 | Mark-spot refresh 1/2/4/6/8 frames by DA ≥ 96/91/81/71 | High (code); DA identity INF high | 0x140628400 |
| EV-DEF-0006 | First defender: 8(+2) m keep; lane cone 10–15 m, 70–200°, 40 m goal-bearing switch, 1.5 m hysteresis | High (code) | 0x140531810 |
| EV-DEF-0007 | One MATCH_UP, ≤ 2 COVER per defending frame | Confirmed (live 6428 frames) | `live1.jsonl` |
| EV-DEF-0008 | Defending line front = ball-depth ramp `deep → −5`, −5 at level 2, ≥ 10 m behind ball, ≥ −47.06, ≤ 0 | High (code + live median 0.38 m on pre-adjust) | 0x1405f9eb0, `linecheck.py` |
| EV-DEF-0009 | Line bands at `T+0xc48+0x7b10+0x14·line(+0x3c in possession)` | Confirmed (code + live) | 0x1405fbcb0, raw dumps |
| EV-DEF-0010 | Line width by count 15/19/23 (10 if < 3), compactness ±2.85, min spacing 6/5/2 | High (code) | 0x140688300, 0x14063f230, live bp |
| EV-DEF-0011 | `FUN_141b92460` returns 5 | Confirmed | 0x141b92460 |
| EV-DEF-0012 | Live basePosition values (§8) | Confirmed (live read) | object 0x33ddb370, vtable 0x142b5a328 |

## 14. Second pass (DefensiveShape2RE): closing the §12 open list

Scratch: the DefensiveShape2RE scratch dir (`l1a.c`, `l2a.c`, `m1a.c`, `r1a.c`, `ang.c` decompiles; `sample2.py` sampler,
`rd2.py` reader, `linecheck2.py`/`e2e.py`/`midcheck.py` line checks, `live2.bin` 8400 records at 20 Hz = 420 s, 1975
in-play untorn records × 2 teams; `p0xbc.bin` DefenceMark table, `bp_live2.bin` BasePosition). Live data this session:
one AI-vs-AI match, both teams at attack level `T+0xb8a4 = 2`, team 0 Defensive Style 0 / Defensive Line 4,
team 1 Defensive Style 1 / Defensive Line 8 (sheet bytes below). MA = `0x7ffffd570018`, found with the offside report's
MatchControl walk (`find_ma7.py`); perf debug-register breakpoints still fail with ENOSPC.

### 14.0 Corrections to §6 (OBS)
- **`ctx+0x10c` is the shape phase being built, not "in possession".** `FUN_1404f9dc0` calls the shape builder
  `FUN_14051d9e0` once per frame with `r8b = (T+0x255 != 0)` on odd frames (`MI+0x1198 & 1`) and `(T+0x255 == 0)` on even
  frames (raw 0x1404f9f0d–0x1404fa237), and `FUN_14051d9e0` passes that byte to `FUN_1405ec600` → `FUN_1405ebfa0`
  (`ctx+0x10c`, raw 0x1405ec663/0x1405ec6ce). So **every team rebuilds both shapes, alternating frames**: phase 0
  (out-of-possession bands, `f9eb0`/`fa990`) and phase 1 (in-possession bands `+0x3c`, `fb6f0`/`fb2d0`), each at 27 Hz.
  Who has the ball only selects which band set the players use.
- `FUN_1405fbcb0` order (raw 0x1405fbcb0–0x1405fbe09): f9eb0|fb6f0 → (phase 1, MODE 2 only: sign fix of `+0x7b38`) →
  fa990|fb2d0 → `S+0x7c3c = S+0x7b14` (**phase 0's** back-line edge, copied in both phases) → `f4f50` if
  `FUN_140a74ec0 > 4·fps` → tail-jump `FUN_1405efbd0`. On a phase-1 snapshot `S+0x7c3c` therefore already holds the
  final value of the last phase-0 build; this is why §6's "final vs pre-adjust" comparison looked noisy.
- **Band layout:** each line band `S+0x7b10+0x14·line` is `{upper x, lower x, …}` in own frame (upper = toward the
  opponents' goal): back line `0x7b10` = `min(B+3, …)` ≥ `0x7b14` = B. §6's "front" `T+0x875c` is the back line's
  **lower edge = the line height**; its "rear = front − 3" is the upper edge `B + 3`.
- **The line's ball is `MA+0x2482c/+0x24834`** (`FUN_1405f95a0`: in play `ctx+0x100 = dir·MA.x`, `ctx+0x104 = |MA.z|`),
  not BallInfo `+0x88`. The two differ by metres in flight (MA is the analysis ball). Using it took the pre-adjust check
  from 53 % to 88 % within 5 cm.
- **Defensive Style 1 branch confirmed live:** team 1 (style byte 1, Defensive Line 8) sat a constant 4.18 m deeper than the
  style-0 formula; `a = 5·sin(90°·t) + adjustDefenceLine_Retreat(−11)`, `t = 1 (v2 ≤ 1), (10 − v2)/9 (1 < v2 < 10), 0`
  (`FUN_140a92c80` = sin: sin 20° gives a = −9.29, the observed offset; cos would give −6.30).

### 14.1 The remaining line passes (OBS code; live-checked)
Own frame, metres; `B` = back-line lower edge being built; `bx` = `dir·MA.x`; `ref` = `FUN_140a4db20(dir)` = 270° (dir +1) /
90° (dir −1), the heading of "straight back to our goal"; headings are `atan2(dx, dz)` in degrees
(`FUN_14046f260`); ball velocity = BallInfo `+0x2948` (`FUN_140a57720`), m/s (OBS: |v| ≈ 14 for a pass in the live read).
Carrier `c = MA+0x2601c` (`FUN_140a627e0(ctx,0)`), holder fallback `MA+0x2483c`, then opponents' `T+0x78`.

**`FUN_1405f7a00` — factor on `marginTargetLineX` (−10) (called from f9eb0 when `keepDfTargetLineX > 0`, live 1):**
```
away = |hdg(ballVel) − ref| > 135° && |ballVel| > 30000/(fps·3600) && bx > B + 4      // 135 0x14259be60, 30000 0x1425cd078, 3600 0x142598590, 4 0x1425656c0
if no carrier: if away return 0; c = holder; if none c = opp T+0x78; if none return 0
if c is ours: return 0
if prevFinal(T+0x875c) < B:  return away ? 0.5 : 1                                      // 0.5 0x142565600
a = |facing(c) (P+0x554) − ref|;  return a ≤ 65 ? 1 : a < 180 ? (a − 180)/−115 : 0      // 65 0x14259be48, −115 0x1425cd07c
```
→ `B = min(B, bx − 10·factor)`: the 10 m cushion in front of the line shrinks to 0 as the opponent on the ball turns his
back to our goal (fully at 180°), or the ball travels away from our goal. `fps` here is `FUN_141e532d0` (float 54), so the
speed test is `> 0.154 m/s` (any moving ball; INF(med): written for a per-frame velocity, i.e. 30 km/h intended).

**`FUN_1405ef910` — step-up when deep (only if `T+0xb8a3 == 0`):** applies only when `B < 1 − F[6]` (−35.14) and
`bx > B + 4`:
```
if carrier: if |hdg(ballVel) − ref| > 100° && |ballVel| > 5.5556 m/s: B = bx − 4; return      // 100 0x14253ee30, 5.5556 0x1425cd068
            else c = holder
if c valid: a = |facing(c) − ref|;  t = 0 (a ≤ 100), (a − 100)/35 (100 < a < 135), 1 (a ≥ 135); B += (bx − 4 − B)·t
```
→ a deep line jumps to 4 m behind the ball when the ball is played away (> 20 km/h) or the man on it faces away from goal.

**`FUN_1405fa990` — midfield band and back-line upper edge (phase 0):** with `W = S+0x7b38` (forward band upper edge,
f9eb0), `lv = ramp(L; 2→1, 4→0)`, `Lown = dir·MA.line(+0x44+0x14·side)` (own offside line), bp = BasePosition:
```
r  = style==1 || plan(0xb) ? 40 : lerp(closeRate_DF_FW 52, closeRate_DF_FW_Retreat 50, ramp(bx; Lown+20→1, Lown+30→0))
if inPlay: r += ramp(L;2→0,3→1)·ramp(bx; Lown+20→0, Lown+30→1)·(50 − r);  r += (40 − r)·lv       // L ≤ 2 → r = 40
r -= ramp(bx; 15−F[6]→1, −5→0)·closeRate_MF_adjustX(14);  r = clamp(r, 52−14 = 38, 100)          // plan(0xb): no clamp
goal kick (MODE 3, MI+0x14c4 == 0): r = min(r, 30)
r += lv·bp[0x10](−5) − ramp(bx; 2−F[6]→1, 8−F[6]→0)·closeRate_MF_adjustX_stratagy_defensive(16)·lv
(sheet flag FUN_1408c5900 && defenceFormationTest2: r += 10·ramp(spread; 5→0, 12→1))
midUpper(0x7b24) = B + (W − B)·r/100
backUpper(0x7b10) = min(B + 3, B + (midUpper − 1.33 − B)/2)                         // 1.33 = bp+0xac
sq = max(0.06·ramp(|z|;10→0,22→1), mfAdjustMaxRateX(2.5)·ramp(bx; Lown+20→1, Lown+40→0))
midLower = (midUpper − 1.33) − sq·|backUpper − (midUpper − 1.33)|
gap = (plan 0xb or GK restart) ? 5 : lerp(minDistMF_DF 9.25, _stratagy_defensive 7.25, lv)
if midLower < B + gap: midUpper += (B + gap − midLower); midLower = B + gap                  // 0x7b28
forward lower edge 0x7b3c = max(W − max(spread·rate, 5 + ramp(bx; −F[6]−5→1, −15→0)·(9 + 5·lv)), −F[17] − 12)
```
(MODE 4 corner: both mid edges = −F[8].) Live check (`midcheck.py`, B from the modelled f9eb0 output): mid upper median
|e| 0.009 m (64 % < 5 cm, 84 % < 0.5 m), mid lower 0.000 m (70 %, 82 %). Since the ball is usually within 20 m of our line,
the midfield normally sits on the gap floor `B + 7.25…9.25`.

**`FUN_1405f4f50` — cover drop** (as §4, now raw-checked): deepest COVER man by `max(own x, cover point x)` (point
`T+0x26c` if active COVER, `T+0x27c` if queued); his line (`T+0x87d0+8·slot`, not 2) lower edge drops to
`max(m, edge − k)`, `k = 6` (line 0) or `|midEdge − T+0x875c|/coverLimitXRate(6)` (line 1), floored at `−F[4]`; a
line-0 drop moves `0x7b10` and the mid lower edge by the same amount.

**`FUN_1405efbd0` + `FUN_1405f80c0` — midfield catch-up shift (phase 0, in play or `MI+0x11d4 ≠ 0`):**
```
score = sum of the 3 HIGHEST per-midfielder scores (FUN_140a79640 sorts descending),   // live: desc fits 74 % within 1 pt, asc 43 %
        midfielder = on pitch with T+0x87d0+8·slot == 1; score 100 if x ≤ midLower + 2, 100·(x − midLower − 8)/−6 if < 8, else 0
k = 1 (score ≤ mfLineScore_adjustMax 36), (score − 200)/(36 − 200) (36 < score < 200), 0 (≥ 200)
B += k·|midLower − B|/adjustDiv(4);  backUpper += same                           // stored score S+0x7c50
```
→ the back line steps up by up to a quarter of the line-to-midfield gap only when the midfield has been caught ≥ ~7 m
upfield of its band (two or more midfielders still in the band → no shift).

**Live check of the final line `T+0x875c` (OBS, `e2e.py`, 3950 team-snapshots in play, everything modelled from the MA
ball, ball velocity, carrier/holder ids, positions, facings, line roles, states and the team sheet; only the previous
`T+0x875c` read back as f7a00's input): median |e| 0.000 m, 78 % < 5 cm, 91 % < 0.5 m, 98 % < 2 m, RMS 1.08 m.** Same
data, §6's old final model: median 1.14 m, 38 % < 0.5 m. Per stage (`linecheck2.py`): f9eb0 pre-adjust 88 % < 5 cm;
f9eb0 output with f7a00 + ef910 vs `S+0x7c3c` 61 % < 5 cm (the rest are phase-1 snapshots holding the shifted value, as
above); f4f50 + efbd0 given the observed mid band/score 70 % < 5 cm, 99 % < 2 m. Target (< 0.5 m median) met.
(`FUN_14063fdc0` is the ball-or-receiver point for the slot-target pass `FUN_140687fb0`, not a line pass: carrier in a
kick state with `+0x1a > fps/10` → ball predicted 10 m (30 m for state 9) along the kick; else within 6 frames of a
set-piece event the BallInfo `+0x27a0` point; else the MA ball.)

### 14.2 Mark assignment passes 0, 2–7 (OBS code at the stated VAs; live-checked against `T+0x558`)
Working arrays (brain `+0x4264` block = `M`): `M+0x00[opp]` last frame's marker (global idx), `M+0x58[k]` the
**danger list** (opponent slots still needing a marker, filled by pass 2), `M+0x84[opp]` = def slot, `M+0xb0[def]` = opp,
`M+0x108[def]` availability, `M+0x114[k]` pending hand-off, `M+0x1e98` back-line reference x, `M+0x1ea4` set-piece
target bits, `M+0x1f00[def]` steal cooldown. Opponent distances come from MA's per-player sorted lists
`MA+0x22e4c + idx·200` ({id, distance} × 11 per team, rank byte at `+0xb2+id`).
`T+0x4c00[slot]` is the slot's **position category** (matchlogic §2.4: formation position index; INF(med): 0 = GK).
Tuning values = live `constant::DefenceMark` table (RTTI vtable 0x142b567d8) at `*(*(*0x1437f4808)+0xbc·8)+8`
(`FUN_141e5d4c0(·, 0xbc)`); field names follow the alphabetical +4 rule from +8 (names at 0x142b56078…; alignment
checked by meaning: `addAngleKickOff` 50 is added to an angle in the first 15 s, `continueAdd*` only when the opponent was
marked last frame, `checkRear*` 21/22 compared with distances to the line). Dump: `p0xbc.bin`, `defencemark_fields.txt`.

| pass | VA | rule (reduced) |
|---|---|---|
| 0 init | `FUN_140531490` | `M+0x108[s] = 1` for own on-pitch slots that are not FK-wall men (`FUN_140a3c9b0`), not plan `(0xf, s)`, not `GOAL_COVER`/0x6d, position ≠ 0 (`T+0x4c00`), not falling (`FUN_140529d30`), not the goal-kick taker (MODE 2: `T+0x4b10`) / FK taker (MODE 5), `T+0xb748[s] ≠ 0x24`; human team: only position ≥ 3, or at a stopped GK restart more than 5 m (7 m if he had a target) from his slot target `T+0x4b4c`. |
| 1 | `FUN_140531810` | first defender on the carrier (§5.2) + team-sheet man-marking. |
| 2a | `FUN_140531260` | `M+0x1e98` = mean world x of back-line men (`T+0x87d0 = 0`, position ≠ 0) within `6 + 6·clamp((dir·ball − dir·Lown)/30)` m of the own offside line `MA+0x44+0x14·side`; none → `dir·T+0x875c`. |
| 2b | `FUN_1405325e0` | **danger list.** Walks the §3 order list (most advanced opponent first). Not listed: pass-1 pairs, off-pitch men, kick-animation windows, set-piece takers (`FUN_140528070`), or a `FUN_14052d350` veto. Listed directly: `FUN_14062c400` (restart receiver within 20 m of the ball, < 5 s after the event, in front of our box) and stopped-play cases (`FUN_14052a350` FK wall zone, `FUN_14052c8b0`). Open play (opponent predicted `fps·0.1+0.5` frames ahead): lateral window `W = 2·(F[7] + 4)` (+`continueAddWideLimit` 2 if he was marked last frame; +`reverseSideAddWideLimit` 12 when within `checkReverseSideXDist` 16 m of our line and ball-flag `0x1000`); angle limit `needMarkAngleBase` 92° (+`addAngleKickOff` 50 in the first 15 s unless Defensive Style 1; +`continueAddAngleLimit` 26.5); start line `FUN_14062acf0` = analysis line + {−40, −30, 0, +20, +30} m by attack level 0..4 (+`continueAddStartLine` 2). An opponent more than `checkRearOffenceDist` 21 m from our line who is upfield of the start line is dropped (attack level < 3, no plan 0xd/0xe, not the carrier); one more than `checkRearOffenceDistFar` 22 m away is dropped when the ball→opponent angle exceeds the limit (+50·`FUN_140529490` side weight); an opponent ≥ 10 m from the carrier and > 8 m off the back-line reference is dropped when the carrier→opponent bearing is > 135°. Kept if `|z − ball z| ≤ W`, or plan `(0xe, opp)`, or he stands in front of our box (`|z| < F[7]`, `x < −F[6] + 5`). |
| 4 | `FUN_14052f4f0` | plan mark bits `T+0xb8bc`: each flagged own slot takes the **nearest on-pitch opponent** from MA's list (not a set-piece taker); a flagged slot already in MARK (`P+0x5d0 == 0x36`) keeps his last target. Removed from the danger list. |
| 5 | `FUN_140530320` | only `FUN_140a533a0` (stopped corner, or stopped FK in the box zone): defenders ranked by stat bytes `+0x31 + +0x2c + +0x1d` (`FUN_14124dad0`; INF: aerial/defending ratings; −100 for slot `T+0x3ed0`); set-piece target opponents (`M+0x1ea4`) get the free defender with the smallest MA distance (+10000 for `T+0x3ecc` and position < 4, +2500 for `T+0x3ed0`/plan bits); the rest of the danger list (ranked by the same stat sum, +100 with plan 0xe; or by distance) is paired best-to-best; the MI `+0x173c` target is moved into the top 3. |
| 6 | `FUN_14052f720` | **continuity.** A danger-list opponent whose last marker `M+0x00[opp]` is free keeps him immediately in transitions (carrier in a kick window ≤ 0.2 s, carrier state 8, a restart ≤ 3 frames old, or `FUN_140529f60` deep GK restart). Otherwise a closer free defender inside radius `R = FUN_140529330` (35 m; 20 m when attack level < 3, no plan 0xd/0xe, not the special target `T+0xb8d0`) that passes `FUN_140528380` (zone limits `remainMarkXLimit` 12 / `remainMarkZLimit` 13.5, box-to-box 14 / 16) and the hand-off tests `FUN_14052eab0`/`FUN_14052a820` takes over (opponent parked in `M+0x114`); else the old marker stays. |
| 7 | `FUN_140532f70` | **nearest free defender with stealing.** For each remaining danger-list opponent: walk own players by MA distance up to `R` (+20 for a forward-line man within 7 s of a possession change; +20 if `FUN_140622490`; +5 within 3 s of a possession change; +20 for the first three when the opponent is the carrier/receiver). Candidate: available, not committed (`T+0xb88c[s]` 0, or 5 for the carrier), on pitch, not vetoed (`FUN_14052a190`), feasible (`FUN_140527e10`: position-1 men do not follow a wide opponent `|z| > F[7]` within 20 m of the ball while plan 0xb is armed). Free → pair. Taken → steal test (`FUN_140527140`, `FUN_14052a820`, `FUN_140528380`); a steal puts the displaced opponent back on the danger list at this index and sets the defender's cooldown `M+0x1f00 = fps·0.1 + 0.5` frames. |

Then the hand-off hysteresis of §3.3 commits changes. **Live check (OBS, `markcheck.py`, 3950 team-snapshots, 9736
pairs):** the marker is the nearest participating own player to his opponent in 57 %, top 2 in 78 %, top 3 in 86 %;
pair distance p10/p50/p90 2.3 / 6.9 / 16.7 m; a pair survives the next 50 ms sample in 95 % (continuity + hysteresis).
Share of opponents marked by depth (own frame, relative to the ball): ≥ 20 m upfield of the ball 2 %, 10–20 m 11 %,
0–10 m 34 %, goal-side of the ball 37 %; relative to our offside line: beyond it (offside side) 18 %, 0–20 m in front
46–49 %, 20–30 m 26 %, > 30 m 7 % — consistent with the start-line/rear-distance drops of pass 2. INF(med): the
per-pass weights are not individually checked (no per-pass instrumentation without breakpoints).

### 14.3 Mark spot `FUN_140623750` (OBS code; live structure)
Inputs: defender `d` (global idx), opponent `o = T+0x558[d]`, `oP` = o predicted `fps·0.2 + 0.5` frames ahead
(`FUN_140a6c990` velocity), own goal point `FUN_140a62780` (INF: own goal centre).
1. **Distance r** (fVar43/fVar40): `r = 6 − 4·k`, `k = ramp(dir·oP.x; 20 − F[6] (−16.14) → 1, 20 → 0)` — 2 m when the
   opponent is deep, 6 m when he is ≥ 20 m upfield; `k` is raised by the team press timer (`T+0xa53c`, ramp between 4 s and
   10 s of `T+0xa544`) and forced to 1 within 4 s of a possession change when `MI+0x14c8[opp side] == 1`. If `o` is the
   most-advanced opponent and stands within 1–3 m of our offside line: `r → lerp(r, 0.8, ramp(|L − x|; 1→0, 3→1))`
   (no plan 0xc). **+2 m when the defender's own team has option 6 (Pressuring) set** (`FUN_140a62180(ctx, team, 6)`),
   + `FUN_140a603c0(level, 0xb)` (team-level table). Overrides: 1.2 m for the plan/special target or at a GK restart
   (MODE 3, `MI+0x14c4 == 1`); for opponents in forward roles (`FUN_140a58320 > 2`) facing within 90° of our goal,
   `r ≥ 1 + 2·ramp(x; −F[6] → 1, −F[6] − 10 → 0)`; danger flag `FUN_140622490`: `r += 2·ramp(x; F[6]…F[8])·ramp(Δstat, 0→20)`
   with `Δstat` = defender − opponent stat byte `+0x27` (INF: speed) — a slower defender keeps more cushion.
2. **Goal-side point** `G = oP + r·(sin θ, cos θ)`, θ = heading from `oP` to the own goal point (`FUN_140a92c80` = sin).
3. **Line hold** (defender in a line > 0 or plan 0xd, no danger flag/plan 0xb): the segment `oP → goal` is cut with the
   line `T+0x875c − FUN_140625a70·dir` (`FUN_140a90d30`); when `G` would fall behind it and the opponent is within 8 m
   laterally of the slot target, `G` = the cut point and `T+0x613[slot] = 1` (live: 4 % of MARK snapshots).
4. **Blend and clamps:** `x` from `FUN_140625e00(rate = FUN_1406216a0, G.x, slot target T+0x4b4c.x)`, `z` from
   `FUN_1406276b0(…, slot target z, 0.8·…)`; rate stored at `T+0xb834+4·slot`; then inside the pitch
   (`|x| ≤ F[0] + 0.62`, `|z| ≤ F[1] − 0.62`) and out of a 6 m circle round our keeper in the box (`FUN_140a907c0`).
   Corner (MODE 4): ≥ 10 m from the kick spot.

**Live (OBS, `spotcheck.py`/`spotfit.py`, 7957 MARK snapshots):** the spot is not the bare goal-side point (median
|spot − G| 6.9 m) but lies between the slot target and G in proportion to the stored rate: the fitted fraction of the
way from target to G along x is 0.14 / 0.39 / 0.58 / 0.67 for rate bins [0,25) / [25,50) / [50,75) / [75,100]
(z: 0.01 / 0.09 / 0.20 / 0.39). Rate p10/p50/p90 = 2 / 80 / 100. INF(med): `x ≈ target.x + rate/100·(G.x − target.x)`
before the extra clamps; the exact `FUN_140625e00`/`FUN_1406276b0`/`FUN_1406216a0` arithmetic is open (Ghidra loses the
float returns of `FUN_140625a70`: "type propagation not settling").

### 14.4 State writers (OBS code)
- **MATCH_UP `FUN_140536210`** (every frame): the own on-pitch slot whose mark target `T+0x558` is the carrier
  (`MA+0x2601c`, else the opponents' `T+0x78`) becomes `MATCH_UP`, `T+0x7c` = his idx. No such slot → every MATCH_UP is
  reset (`FUN_140a40820`). Skipped while any own player is inside a kick/trap window, when we have the ball, out of play
  except a goal kick, and (with `T+0x6e7`) while the `MA+0x24710` distance of player `FUN_140a62690(ctx, side)` is < 6 m
  (INF(low): an own player within 6 m of the ball). In `MI+0x1775 ∈ {3, 8, 9, 10}`
  the man is instead the first entry of MA's ball-distance list `MA+0x24840`. → **the presser is pass 1's first defender.**
- **PRESS / SAND second man `FUN_140634470`** (needs team level flag `FUN_140a604d0(level, 5)` and toggle 0xf): keeps one
  extra presser (`param_1[2]`), reason code in `T+0xb88c[slot]`. Situation `FUN_140632ae0`: **5** = double-team (the own
  player nearest the carrier other than the MATCH_UP man → PRESS); **6** = every slot with `T+0xb774 == 0x35` → SAND;
  **3** = the goal-kick slot `T+0x4b10` → PRESS; other codes go to `FUN_140633940` only when "aggressive": Defensive
  Style 0 with the ball point in the opponents' half, ball in our box zone (`x < 10 − F[6]`, `|z| < F[5]`), press timer
  `T+0xa53c`, `T+0xa298 == 3`, plan 0xd or `(0xe, opp)`, attack level ≥ 3, team level 6, or < 4 s after a possession
  change with `MI+0x14c8[opp] == 1`. Ball must be low (≤ 1.5 m). Live: PRESS 0.09 per team-snapshot (both teams), codes 7 (301),
  5 (38), 4 (15); SAND never seen.
- **DELAY / PRESS `FUN_140536fc0`** (and the dispatcher loop of §1), structure only: skipped while
  `FUN_140a53f90 > 0`, out of play, or the opponents' `FUN_140a62690` man is in states 0x41/0x42; after restart events
  (types 1–3, 0x10) it needs `FUN_140a51320 ≥ 3` (INF(low): a pressure score); picks men via
  `FUN_140639830`/`FUN_140639a20` (constants 25/64/81/90/155°, 3–4 m), writes PRESS and `T+0xa548+4k`/`T+0xa55c+4k`;
  DELAY when `FUN_14062af10`. Not reduced; live DELAY never seen (420 s in §9 + 420 s this pass).
- **OFFSIDETRAP `FUN_14057ba50` / `FUN_14057b820`** (OBS, reduced; `b820` is the same trigger without the re-check): runs after the defence dispatcher for a
  CPU team **only when team flag `T+0xe4` is set** (INF(med): the Offside Trap instruction). Existing OFFSIDETRAP men are
  re-validated by `FUN_14057b750` (kept or reset). Trigger: `o` = first entry of the opponents' list
  `FUN_140a71500(MA, dir)` (INF: the most advanced attacker, with his list value `v_o`), no pending event on him
  (`FUN_140a62fe0`/`FUN_140a60300`), and he is not the ball holder `MA+0x2483c`. Then every own slot with position ≠ 0
  whose list value `v ≤ v_o + 2` gets OFFSIDETRAP with target `(o.x + 2·dir, own z)` — the back line steps to 2 m
  upfield of the attacker. Live: `T+0xe4 = 0` for both teams in all 1986 in-play records → never armed, matching
  zero OFFSIDETRAP states.
- **PASS_COURSE_CUT `FUN_140635ab0`**, structure only: needs team level flag `FUN_140a604d0(level, 7)` and toggle 0x11;
  clears stale 0x3d states, builds lanes (`FUN_140634ac0`, `FUN_1406350c0`, `FUN_140634cb0`) with radii 7/10/15 m
  (30 m under plan 0xd), lane angle ramp 60°→150°, start line `FUN_14062acf0 + 5`; prefers men already in MARK.
  Live 0.30 per team-snapshot. **Cover chain** `FUN_14062ef30`→…→`FUN_140631b60`: decompiled (`l1a.c`), not
  reduced; live COVER points (744 snapshots): 7.4 m (p10 3.0, p90 14.0) goal-side of the MATCH_UP man and 10 m from
  him, 11 m behind the ball, median 2.9 m deeper than the line height; lateral side mixed.

### 14.5 Retreat flag `T+0xc12a` — the rule and its readers (OBS code; live)
**Writer `FUN_140533d10`** (every frame from `FUN_140535c60`; own frame; `o` = the opponents' extreme player from
`FUN_140a71500(MA, dir)[side·0x16 + 2]` (INF: their most advanced attacker), `c` = carrier else opponents' `T+0x78`,
`w` = 0.2 and `R` = 13 when the flag is already set (else 0 and 15), plan `(0xc)` adds 10 to `R`):
```
0 unless: in play (or MODE ∉ 2..4), not a GK-restart situation, CPU team, team level (T+0xa294/100) ≥ 2
0 if o is not deeper than the ball by > 1 m (0 m when already set), or o is inside a kick/trap window
flag = ball height > 3 m
if carrier and he is not kicking (state classes / +0x5d0 == 7):
   flag |= |carrier(+0.1 s) − ball(+0.1 s)| > 2.5 − w                    // ball not under control
   flag |= ball.x > o.x + R  and  that gap test > 1.3 − w                 // space behind the line   (13/15 m)
   flag |= ball.x > o.x + R + 15
   flag |= carrier facing > 135° (160° with plan 0xc) from straight at our goal (FUN_14043a0a0)
   flag |= ball.x + w > o.x + 38
if carrier or ball in our box zone (x < −F[6], |z| < F[7] − 5):
   if |ball v| > 0.745 m/s and its heading is within 85° (70° plan 0xc, −30° while kicking) of our goal:
        flag |= box zone, or (o.x + 15 (25 plan 0xc) < ball.x and the receiver angle < 85° (+5 when set))
   flag |= FUN_140a53f90
if not kicking: n = fps·0.1 + 0.5 frames (or the loose ball's travel frames); flag |= FUN_1404f6110() < n
```
→ **The flag means "the opponents can play a ball in behind": there is an attacker goal-side of the ball and a big gap
(13–38 m) between him and the ball, or the carrier is not under pressure/control, or the ball is already travelling at
our goal.** INF(med) for `FUN_1404f6110` (own team's arrival frames). Live (`tally.py`): set in 14 % of in-play
team-snapshots; 33 % with an opponent carrier, 0 % with our own carrier, 13 % with a loose ball; with an opponent
carrier by ball depth (own frame): x ∈ [0, 15) 48 %, [15, 60) 37 %, [−15, 0) 0 %, [−30, −15) 0 %, < −30 23 %.

**Readers (OBS displacement scan of `0xc12a`, 17 hits, `disp_c12a.txt`):**
| reader | role | effect of flag = 1 |
|---|---|---|
| `FUN_14052d350` (mark pass 2 veto) | need-mark test | opens the "runner beyond the line" branch: an opponent behind `T+0x875c − (1 + continue)` who is not covered gets marked (with the 6 s possession guard) |
| `FUN_140622a80` (via `FUN_140625e00`, `FUN_1406276b0`) | mark-spot clamp | flag 0 and no plan 0xb: a depth value is clamped with `min(·, dir·v)`; flag 1 skips the clamp (INF(low): markers may then drop with runners) |
| `FUN_140623750` | mark spot | flag 0 and the carrier in a kick class (`FUN_140a6e320`): enables a sideways offset branch for a static opponent (INF(low)) |
| `FUN_1406276b0` | mark-spot z | flag 0 gates a branch on zone bit 12 (`FUN_140a72e70`) and the carrier's kick window (INF(low)) |
| `FUN_1406369f0` (from `FUN_140636c80`, GOAL_COVER pass `FUN_1406377f0`) | gate | returns 1 (no action) when set |
| `FUN_140682770` (slot targets) | shape | `S+0x7c40`/`S+0x7c44` (two reference x values of the shape pass) raised by 5 m |
| `FUN_140950890` (player brain `FUN_140956c30`) | back-line man vs runner | flag 0 only: when the runner's 1/6 s prediction is at or below the man's target x and runner + 2·dir is below the line height, line-0 men take `runner + 2·dir`, other lines shift by the same amount (OBS arithmetic; INF(low) meaning) |
| `FUN_140951dd0` (player brain `FUN_1409589f0`) | back-line run type | flag 1, back-line man, < 4 s guard off: run mode 3, or 4 when the two players are ≥ 3.5 m apart in x (INF(low): track-runner run) |
| `FUN_14052a820` | pass 6/7 hand-off | read in the swap radius logic (15/20/30 m) |
| `FUN_1404161e0`, `FUN_142153ed0`, `FUN_14215cf10`, `FUN_140a415a0` | copy / replay serialisation / reset | — |

### 14.6 Team-sheet identity (OBS live + code; edit-data cross-check open)
- **Sheet layout `T+0x8e08`:** `u32 plan` (0..2), then 3 × 5 slider bytes at `+4+5·plan` (`FUN_1408c52e0`), then 3 × 7
  option bytes at `+0x13+7·plan` (`FUN_1408c5030`, `k < 7`). Live this match: team 0 plan 0 sliders `9,2,4,4,2`,
  options `1,0,1,0,0,0,1`; team 1 plan 0 sliders `8,2,8,9,2`, options `0,1,1,1,1,0,0`.
- **Option 4 = Defensive Style (OBS by effect):** team 1 (option 4 = 1) follows f9eb0's style-1 branch exactly (§14.0)
  and its forward band uses `lengthDf_Retreat` 42 instead of 46; team 0 (0) uses `ForeCheck`/46. The seven options in
  code order match the game's Strategy screen order (Attacking Style, Build Up, Attacking Area, Positioning, Defensive
  Style, Containment Area, Pressuring); INF(high) for the labels, and option 6 (Pressuring) is the +2 m mark distance
  of §14.3.
- **Slider k2 = line height, k3 = compactness (OBS by effect):** the line formula reproduces both teams with k2 = 4 and 8
  (§14.1), and the forward band `W = B + len(style) + {10,7.5,5,2.5,0}[team level] − (k3 − 1)·0.33` reproduces both
  teams with k3 = 4 and 9 (team 0: 89 % < 5 cm, team 1: 83 %). INF(high): k2 = "Defensive Line", k3 = "Compactness".
- **Edit-data cross-check: not closed.** The EDIT save was decrypted read-only (Rigbot's `decrypt_edit`, 10.99 MB
  `data`); neither team's slider bytes occur byte-aligned (0- or 1-based) in a way tied to a team, and no pair of
  `Tactics.bin` records reproduces either team's two plans under any nibble/byte permutation (`Tactics.bin` stays the
  id/bitmask index of the setpiece report). The live team ids were not found in `T`, the player objects or MI.

### 14.7 Per-slot targets `T+0x4b4c` (OBS live; code structural)
`FUN_140686190` (with `FUN_140683db0`, `FUN_140685d70`) copies the 11 targets in, adjusts them against each other
(lane spacing; constants 7, 18/−11 ramp, 1.5, 3.5, 8, 10; `FUN_140687310`, `FUN_1406405c0`) and writes them back; the
band-to-slot placement itself happens before it. Live (`tgcheck.py`, defending team, 5550 slot-snapshots):
- **Back line (line 0): target x = the line height `T+0x875c`** — 82 % within 5 cm (90 % of MARK men, 98 % of COVER).
- **Midfield (line 1):** always inside the midfield band (`S+0x7b28 ≤ x ≤ S+0x7b24`, 100 %); median 6.3 m above its lower
  edge (MARK 3.8, MATCH_UP 8.7, idle 13.8).
- **Forwards (line 2):** median exactly mid-band (2.5 m from both edges of the 5 m band), 67 % inside it when idle.
Exact per-slot rule (lateral spacing, the line-1 depth choice) remains open.

### 14.8 GF code each finding replaces
| finding | GF code |
|---|---|
| §14.1 full line chain (f7a00 cushion by carrier facing, ef910 step-up, cover drop, midfield catch-up, fa990 mid band and 7.25–9.25 m gap) | `TeamAIController::GetAdaptedFormationPosition` depth terms, `ApplyOffsideTrap`/`offsideTrapX` |
| §14.0 both shapes rebuilt every other frame | GF's single per-tick formation update (`TeamAIController::Process`) |
| §14.2 seven-pass assignment, continuity, stealing with cooldown (every frame) | `TeamAIController::CalculateManMarking` (GF: every 400 ms from `team.cpp`, max 3 marked opponents), `CalculateMarkingQuality` |
| §14.3 mark distance 2–6 m by depth, goal-side point, blend with slot target by rate | `PlayerController::AddDefensiveComponent` (playercontroller.cpp, the marker's defend position) |
| §14.4 MATCH_UP = pass-1 marker; second presser codes; offside trap only with `T+0xe4` | `TeamAIController::ApplyTeamPressure`/`ApplyZonePressure`, `elizacontroller.cpp` hunt branch (~520–580); `ApplyOffsideTrap` (called unconditionally from `default_def.cpp`/`default_mid.cpp`) |
| §14.5 retreat flag (hold the line vs drop with runners) | none in GF (no hold/drop switch) |
| §14.6 Defensive Style / Defensive Line / Compactness effects | `teaminstructions.cpp` (`DeepDefensiveLine` nudges), `teamphilosophy.cpp` |
| §14.7 back-line slot target = line height | `TeamAIController::GetAdaptedFormationPosition` per-role x |

### 14.9 Killed hypotheses (this pass)
- "`ctx+0x10c` = in possession" (§6): it is the shape phase, alternating every frame for every team (raw 0x1404f9f0d).
- "`T+0x875c` is the back line's front": it is the band's lower edge (line height); `T+0x8758` (`B+3`) is the upper edge.
- "The line uses BallInfo `+0x88`": it uses `MA+0x2482c/+0x24834`.
- "`FUN_1405f80c0` sums the three lowest midfield scores": sorted descending (live 74 % within 1 point vs 43 %).
- "Defensive Style 1's line term uses cos": sin (live 4.18 m offset).
- "Mark spot = goal-side point at 2–6 m": the stored spot is blended toward the slot target by the mark rate (median 6.9 m off G).
- "The live team sheet comes from `Tactics.bin`": no record pair matches either team.

### 14.10 Open list (exact next step each)
1. `FUN_140625e00` / `FUN_1406276b0` / `FUN_1406216a0` (mark-spot blend and rate): Ghidra drops the float returns of
   `FUN_140625a70`; read them with `pdis.py` from 0x140625e00 and refit with `spotfit.py`.
2. Cover chain `FUN_14062ef30`…`FUN_140631b60` and PASS_COURSE_CUT `FUN_140635ab0`: reduce from `l1a.c`/`ds1.c`; live check
   with `live2.bin` (COVER/PCC points are in `T+0x26c`).
3. DELAY/PRESS `FUN_140536fc0`, second presser `FUN_140633940`, situation classifier `FUN_140632ae0`.
4. Edit-data identity of the sheet: find the live team ids (MI or the team-select object), then locate that team's
   tactics block in the decrypted `data` (`edit_data.dat`).
5. Line-1 slot depth and lateral spacing in `FUN_140686190`/`FUN_140683db0`/`FUN_140685d70`.
6. fa990's `ctx+0x120` spread object (forward lower edge, sheet-flag terms).

### 14.11 Ledger entries
| ID | Claim | Conf. | Evidence |
|---|---|---|---|
| EV-DEF-0013 | Both shape phases rebuilt on alternate frames for every team (`ctx+0x10c` from frame parity) | Confirmed (raw) | 0x1404f9f0d–0x1404fa237, 0x1405ec663 |
| EV-DEF-0014 | Final line `T+0x875c` = f9eb0 (MA ball, style branch sin, f7a00 cushion, ef910) → f4f50 → efbd0 (top-3 desc score) | Confirmed (code + live median 0.000 m, 91 % < 0.5 m, n 3950) | 0x1405f9eb0, 0x1405f7a00, 0x1405ef910, 0x1405f4f50, 0x1405efbd0, 0x1405f80c0; `e2e.py` |
| EV-DEF-0015 | Midfield band from fa990 (r = 40 at level ≤ 2, gap 7.25–9.25 m) | High (code + live median 0.009/0.000 m) | 0x1405fa990; `midcheck.py` |
| EV-DEF-0016 | Mark passes 0, 2–7 rules (§14.2) | High (code); aggregate live only | 0x140531490, 0x1405325e0, 0x14052f4f0, 0x140530320, 0x14052f720, 0x140532f70; `markcheck.py` |
| EV-DEF-0017 | Mark distance 6 − 4·ramp(depth) (+2 Pressuring); spot blended to slot target by rate | High (code) / Tentative (blend arithmetic) | 0x140623750; `spotfit.py` |
| EV-DEF-0018 | MATCH_UP = slot marking the carrier | Confirmed (code) | 0x140536210 |
| EV-DEF-0019 | OFFSIDETRAP needs `T+0xe4`; step to attacker x + 2 m | High (code + live flag 0, no states) | 0x14057ba50 |
| EV-DEF-0020 | Retreat flag rule and readers (§14.5) | High (code) / INF(med) semantics | 0x140533d10, `disp_c12a.txt` |
| EV-DEF-0021 | Sheet option 4 = Defensive Style, slider k2 = line height, k3 = compactness | Confirmed (effect, live both teams); labels INF(high) | `linecheck2.py`, forward-band check |
| EV-DEF-0022 | Back-line slot targets sit on the line height | Confirmed (live 82 % < 5 cm) | `tgcheck.py` |
| EV-DEF-0023 | DefenceMark constant table location and field names | High (alphabetical rule checked by meaning) | 0x1437f4808 registry id 0xbc, vtable 0x142b567d8 |


> **Correction (TransitionsRE, live):** the "after a possession change" hold-back (5–8 s) and the >4 s cover-drop are **kick-off** timers: `FUN_140a74ec0` = in-play frames since the newest kick-off (`MI+0x119c`), reset at every restart, frozen while stopped. The possession timer `MI+0x11a0` is separate.


> **Closed (CounterTrigger2RE):** the mark-spot blend is an exact lerp `spot = base + (candidate − base)·rate/100` (and the mark distance `1 + (r−1)·rate/100`), then clamped; nothing in `.text` reads `T+0xb834` in the blend. Live refit reproduces 0.14/0.39/0.58/0.67; the rate steers direction, not displacement.
