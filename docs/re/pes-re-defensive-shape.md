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
