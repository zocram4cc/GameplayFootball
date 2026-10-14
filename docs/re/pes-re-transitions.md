# PES 2021: transitions (team) and receiving the ball (player) — TransitionsRE

Image `pes_full.bin` (Unpacker), decompiles via `dec.sh` + `annot.py` (hypotheses only), raw via a linear capstone
disassembler over the image (`lin.py`). Scratch: the TransitionsRE scratch dir (`t1a.c`…`t12a.c` decompiles, `lin.py`,
`immscan.py`, `findhist2.py`/`dumpring.py` history-ring reader, `cvals.py` live constant reader with
`trap_fields.txt`/`passget_fields.txt`, `consts_live.txt`, `samp.py` gated live sampler).
Tags: **OBS** = decompile/raw at the VA or a live read; **INF(conf)** = inference.
Notation as `pes-re-defensive-shape.md`: `G = *(0x1436F3DC8)`, `MI = *(G+0x270)`, `T` = team record
`FUN_140a48a30(G, side)`, `P(i) = *(G+(i+0xbf)·0x28)` (pos `+0x55c`, heading `+0x554`, AI action `+0x5d0`),
`hu(i) = *(G+(i+0xa5)·0x28)` (anime: `+1` id, `+0x1a` clip frame, `+0x1c` contact frame), `ai(i) = *(G+(i+0xf3)·0x28)`,
`MA` = analysis object (`ctx+0x18`), `F = *(G+0x2aa8)` (FieldInfo: `F[0]` 52.56 half-length, `F[6]` 36.14 box line,
`F[7]` box half-width), `fps` = `FUN_141e532d0()` (54), `dir = (s8)T+0x254`, own frame = `dir·x`.
`H` = match history (`ctx+0xe0`; `FUN_140a3e150(H, ring, k)` = k-th newest record of ring `ring`, 8 × 800 B per ring,
stride 0x1908; empty sentinel record at 0x1436f3a40 has `[0] = 0x37`).

## 0. Summary

1. **There is no team-wide "possession phase" switch.** Both shapes (out of / in possession) are rebuilt on alternate
   frames for every team (DefensiveShape2RE §14.0); who holds the ball only selects the band set. Transitions are
   expressed as (a) a global *frames-since-possession-change* counter (INF(high) `MI+0x11a0`, §1) that gates a family of
   short-lived **team concepts** (`match::ai::SubConcept*`, §2–§3), and (b) the ordinary per-frame defensive passes
   (MATCH_UP / mark / cover) which simply re-run on the new carrier.
2. **`FUN_140a74ec0` is a kick-off timer, not a possession-change timer** (OBS, §1.2; live-checked). It returns
   `MI+0x119c` (in-play frames since the newest restart) only while the newest history ring-5 record has kind 1
   (a kick-off: half start **and** the mid-half kick-off seen live at frame 25286), else 0xffffffff, read unsigned.
   Every earlier "after a possession change" reading of it (DefensiveShape §6/§14.2/§14.3/§14.4: the 20 m
   in-possession line hold-back fading 5→8 s, the 4 s cover-drop gate, the "< 4 s + `MI+0x14c8`" press rule, the
   pass-7 "+20 within 7 s / +5 within 3 s" radii) is a **kick-off** rule: it applies in the first 3–8 s after a
   kick-off and never after a corner / goal kick / other restart (the value is then 4.29e9 frames).
3. **Counter-press = `SubConceptTransitionChase`** (Defensive Style 0 = Frontline Pressure teams; §2): within 3 s of
   losing the ball, if the new carrier is wide (|z| ≥ F[7] − 2) in our attacking third (own-frame x ≥ F[6] − 10),
   not kicking, and our MATCH_UP man is within 10 m of him, a **second man** is sent to a point 8 m from the carrier
   along a 160°-rotated heading (the touchline trap). One extra man, ends when the second man's state leaves
   {BASE_POSITION, COVER, MARK, PASS_COURSE_CUT}. Fore-check (`SubConceptForeCheckPress`) is a 2–5 s window of the
   same counter. Both are off when instruction 0xd is armed. **Live (team with Defensive Style 0):** TransitionChase 1
   episode, ForeCheckPress 2 (one lasting exactly 5.04 s), ForeCheckLineDown 23 episodes while the opponents had the
   ball; the All-out Defence team ran **RetreatBlockCreate** 18 times, 8 of them on the frame the ball was lost (§2.3).
4. **Counter-attack = `SubConceptSideCounter` / `ReverseSideCounter` / `CounterAfterGkCatch`** (`FastCounter` has a
   stub start and is never started by the manager) (Attacking Style 0 = Counter Attack teams; §3): SideCounter fires
   within 3 s of winning the ball (and ≥ 6 s after a half-start kick-off), ball in our half between our box line and
   12 m short of halfway, the man on the ball facing within 55° of goal, fewer than 3 opponents goal-side → one runner
   (best-scored wide/forward man near an attack channel, not marked within 6.5 m) gets `SPACE_RUN_CHANCE` (0x1e) and
   three supporters `PASS_SUPPORT` (0x22) at ball + 15 m ahead / + 12 m at ±22°. CounterAfterGkCatch: when our keeper
   holds the ball, the nearest outfielder to a free area (< 6 opponents) runs there as his outlet (live: seen 0.06 s
   after a turnover, lasting 4.1 s).
5. **Receiving**: the reaction-table entry 0 (`FUN_140933ad0`) is **not** the receive (it is the body-contact
   stagger/fall reaction, §4.1). The receive is entry 3 `FUN_1409f8220` (TRAP 4 / anime 7, or a data-driven touch
   DRIBBLE 3 / anime 0x5d). The first-touch request (touch kind, target point 5 m out, direction) is built by
   `FUN_1409f8a50` from the carrier brain's pre-decided next action; the clip is then chosen by the anime layer's Trap
   action from `trap_<kind>_<dir>_y<height>_…` clips (§4). Approaching the ball is the `PASS_GET` (5) movement with
   seven route objects (§5).
6. **RNG: none.** No decompiled body in this report calls the match RNG primitives of `pes-re-rng.md`
   (`0x140a92440/0x140a92470/0x140a924e0/0x140a92590`) — OBS for the listed functions, INF(med) for callees not
   decompiled. Transitions and first touches are deterministic given state; GF's `random()` counter roll is not PES.

## 1. Possession and kick-off timers

### 1.1 `MI+0x11a0` — frames since the possessing team changed (High: readers OBS + live)
Readers (OBS): ForeCheckPress trigger `FUN_1405747f0` (`MI+0x11a0 > fps·5 + 0.5` → no press, team-plans §4.1);
TransitionChase start `FUN_140575510` (`≤ 3·fps`); SideCounter start `FUN_140540780` (`≤ ⌊3·fps + 0.5⌋`);
ForeCheckPress continue `FUN_140574710` (`≤ ⌊5·fps + 0.5⌋`, unconditional while `≤ 2·fps`); press timer `FUN_140535650` (`> 9`).
**Live (OBS, `live1`, frames 18360–42278, 20 Hz, `chk_a.py`/`chk_r0.py`):** 28 resets to 0–3; of 22 changes of the
ball's team lasting ≥ 1 s (MA carrier `+0x2601c`, else holder `+0x2483c`), 18 reset the counter within 60 frames
(lag −6…+53 frames: the reset comes when the new team *controls* the ball, e.g. at the keeper's catch event 40 frames
after he reached it); the 4 without a reset are a keeper parry that went out, the following corner (same team kept
possession) and two loose-ball spells. A brief 14-frame touch by the other team does not reset it. It **counts every
frame, including stoppages** (1021 → 1766 across a corner stoppage). Writer not found by displacement scan
(INF(med): filled by a copy into MI).

### 1.2 `FUN_140a74ec0(H, MI)` — kick-off timer (OBS raw 0x140a74ec0–0x140a74efd)
```
if MI+0x116c != 1: return 0xffffffff
r = FUN_140a3e150(H, 5, 0)                       // newest ring-5 record
if r[0] == 0x37 (empty) or r[+0x18] != 1: return 0xffffffff
return MI+0x119c
```
- **Ring 5 = restart records** (OBS): `FUN_140a3dc40` maps message kind → ring via the table at 0x142642e60
  (msg 7 → ring 0 ball events, msg 30 → ring 4 fouls/cards, **msg 0x25 → ring 5**). Record layout (OBS readers
  `FUN_140a533a0`, `FUN_14062c400`, `FUN_140509890`, `FUN_141475ca0`): `+0x04/+0x08` frame, `+0x14` team,
  `+0x18` kind = restart MODE (1 kick-off at half start, 3 goal kick, 4 corner, 5 free kick, 6 penalty, 7 kick-off
  after a goal), `+0x1c` point, `+0x20` player ref. Live (OBS `dumpring.py`): first half — frame 167 kind 1,
  4489 kind 4 (team 0), 5854 kind 3 (team 1); second half — 18246 kind 1, 23005 / 23672 kind 4 (team 1),
  **25286 kind 1 (team 0) after a stoppage with MODE 1** (INF(med): kick-off after a goal). Throw-ins (MODE 2) and the
  goal kick at 26180 are not recorded at the time of the stop; records are written at the restart (`+0x04` = restart frame).
- `MI+0x119c` = **in-play frames since the newest ring-5 restart** (OBS live: resets to 1 at 23006, 23673, 25288 —
  the frame after each record; 113 at 18360 after the 18246 kick-off; frozen while stopped). Consistent readers:
  `FUN_14062c400` (corner receiver ≤ 5 s), `FUN_140509890` (corner by team ≤ 3 s), `FUN_140535a10` (≥ 3 s).
- The caller `FUN_1405fb6f0` zero-extends the return (`mov ebx,eax; cvtsi2ss xmm0,rbx`, raw 0x1405fb859/0x1405fb89a),
  so 0xffffffff becomes 4.29e9 frames (OBS).
- **`MI+0x14c8 + 4·side` is the kick-off type** (setpiece-choreography §11.9: 1 = two-man) — so DefensiveShape's
  "< 4 s after a possession change with `MI+0x14c8[opp] == 1`" (`FUN_140633940` 0x140633ef3, `FUN_140623750`) reads:
  **in the first 4 s after a half-start kick-off taken two-man by the opponents**, press / tighten marks.

| consumer | rule, corrected reading |
|---|---|
| `FUN_1405fb6f0` (in-possession back line, phase 1) | ramp lower end `bp.adjustBackOF − F[6] + 20·h`, `h = 1 (t ≤ 5 s), (t − 8s)/(5s − 8s), 0 (t ≥ 8 s)` → **the line stays 20 m deeper for 5 s after a kick-off, fading out by 8 s** (constants 5.0 0x14259222c, 8.0 0x14259bdf4, 20.0 0x14259be1c) |
| `FUN_1405fbcb0` → `FUN_1405f4f50` cover drop | runs when `t > 4·fps` → always except the first 4 s after a kick-off |
| `FUN_140532f70` mark pass 7 | +20 m radius for forward-line men within 7 s, +5 m within 3 s **of a kick-off** |
| `FUN_140633940` / `FUN_140623750` | aggressive press / mark distance forced to the deep value within 4 s of a two-man kick-off by the opponents (`MI+0x14c8` was 0/0 = one-man for every kick-off live) |
| `FUN_140540780` SideCounter | needs `t ≥ 6·fps` (no counter concept in the first 6 s after a kick-off) |

## 2. Losing the ball: counter-press and fore-check concepts (OBS code)

Concepts are `match::ai::SubConcept*` objects (vtable slots: 0 dtor, 1 team-sheet gate, 2 start, 3 continue/stop,
4 reset, 5 execute, 6 extra). Slot-1 gates (TacticsRE, OBS): **Defensive Style option `k4 == 0` (Frontline Pressure)
→ ForeCheckPress, ForeCheckLineDown, ForeCheckLineUp, TransitionChase; `k4 == 1` (All-out Defence) → RetreatBlockCreate,
RetreatPressBack, RetreatBlockForwardPass, RetreatCrossBlockCreate.**
**Manager (TacticsRE, OBS):** `FUN_140579840(brain+0x62b0, ctx, T)` walks concept ids 0..0x2f every team tick: skip if
the concept object is absent or the `team/subConcept.json` (registry 0xc4) entry for the id is < 1; slot 1 false →
stop; not running → slot 2 start test → `FUN_1406424a0` + running flag `T+0xb6e8[id] = 1`; running → slot 3 end test
every `⌊fps·0.1 + 0.5⌋` = 6 frames, slot 5 update every tick. **Concepts run in parallel** (one flag per id); only for
a human team does `FUN_14057a1f0` stop the others. **Stub-start concepts never run (OBS, TacticsRE):** the concept
start `FUN_1406424a0` has one caller (the selector, 0x140579ab4) and the only byte-1 write to `T+0xb6e8+id` is the
selector's (0x140579ac4), so ids 0, 5 FastCounter, 0xf, 0x1a, 0x20, 0x26, 0x27, 0x28, 0x2a, 0x2b, 0x2e are dead code.
A running concept is also stopped by `FUN_14057a160(ctx, id)` when any slot with `T+0xb748[s] == id` is in action
state 0x25 ONE_TWO_PASS_RUN or 0x2c TEAMMATE_MOVING.

### 2.1 `SubConceptTransitionChase` (vtable 0x1425c4580)
**Start, slot 2 `FUN_140575510`** (all must hold; `c` = carrier `MA+0x2601c`, else the opponents' `T'+0x78`):
1. in play; `MI+0x11a0 ≤ 3·fps` (≤ 3 s since the ball changed hands);
2. plan `(0xd, 0xff)` not armed (`FUN_140a63950`; TacticsRE: instruction id 0xd);
3. `P(c)+0x5d0` ∉ {7 MOVE_KEEP, 0x12 LONG_PASS, 0x13 CENTERING, 0x14 EARLY_CROSS, 0x16 SHORT_PASS, 0x18 THROUGH_PASS,
   0x19 SHOOT} and `ai(c)+0x20` ∉ bitmask 0x35c0000 (bits 18–20, 22, 24, 25) — the carrier is not kicking;
4. `dir·c.x ≥ F[6] − 10` (26.1 m: carrier in **our attacking third**) and `|c.z| ≥ F[7] − 2` (**wide**);
5. chaser `u = FUN_140575390` = our MATCH_UP man (`FUN_140a62960`), else our player nearest to `c` (`FUN_140a62850`);
   `hu(u)+1` ∉ {0xf DIVE, 0x10 STAGGER, 0x11 FALL_DOWN, 0x57 DEMO_INJURY}; `|u − c| ≤ 10 m` (d² ≤ 100);
6. angle tests on the bearing u→c vs. the opponents' attack heading `T'+0x250` (≤ 100°) and vs. 0°/180° by the
   sign of `c.z` (≥ 90°) — INF(med): u is goal-side and outside of the carrier (pushing him to the touchline);
7. second man `s = FUN_140575930` (OBS): walk our players in the carrier's distance list `MA+0x22e4c + c·200`
   (nearest first), stop at the first one farther than **20 m** (400 = d², 0x1425a2670); skip `u`; keep only men in
   BASE_POSITION / MARK / PASS_COURSE_CUT and not falling; pick the **widest** (largest |z|) whose |z| exceeds
   `|u.z| − 15` (0x14259be0c). None → no start.
Then record `+0x18 = s`, `+0x1c = u`, `+0x20 = c`, `+0x24 = MI+0x1198`, and execute.

**Execute, slot 5 `FUN_140575b60`** (OBS): target `p = c.pos + 8 m` (0x14259bdf4) along heading
`T'+0x250 + 160°·σ` (σ = ±1 by the side of `c.z` vs. `dir`, 0x14259be6c), clamped to `|x| ≤ F[0]`, `|z| ≤ F[1]`;
`T+0x26c` point of `s` = p (`FUN_140a42a40`), **`T+0xb748[s] = 0x24`** (concept order kind; mark pass 0 excludes
these men), `T+0xb774[s] = 1`.
**Continue, slot 3 `FUN_140575410`**: keeps running (returns 0) while in play, the carrier is still `c` (or loose),
`FUN_140575390` still gives `u`, and `P(s)+0x5d0` ∈ {1 BASE_POSITION, 0x32 COVER, 0x36 MARK, 0x3d PASS_COURSE_CUT}.
Reset, slot 4 `FUN_140579680`: ids ← 0xff.

**Counter-press in one line:** the MATCH_UP man engages as always (DefensiveShape §5.2/§14.4); TransitionChase adds
**one** extra man only for a wide carrier in our attacking third within 3 s of the turnover; there is no
team-wide "everyone presses for N seconds" mode in this concept. The press timer `T+0xa53c` (`FUN_140535650`) is a
**late-game losing-side press** (score comparison of `G+0x2ad0` records, match half `MI+0x1168` ≥ 4 or second half
with minute counter `FUN_140a74d70 > 19`, team level > 2), not a transition timer (OBS structure; INF(med) meaning).

### 2.2 `SubConceptForeCheckPress` (vtable 0x1425c44c0)
Start `FUN_1405747f0` = team-plans §4.1 (in play, `MI+0x11a0 ≤ 5·fps`, push/corridor tests, press-count test) **except
[P4]: raw 0x1405748d0–0x1405748de returns false when `FUN_140a63950(ctx, side, 0xd, 0xff)` is true** — the fore-check
needs instruction 0xd *not* armed (team-plans §4.1 has the branch inverted). **Continue `FUN_140574710` (OBS):** stop
when out of play, when `MI+0x11a0 > ⌊5·fps + 0.5⌋`, or when the ball is back below own-frame x −10 (0x14259be84);
while `MI+0x11a0 ≤ ⌊2·fps + 0.5⌋` it always continues, after that only while `FUN_140574200` passes. **So the
fore-check is a 2–5 s post-turnover window** for Frontline-Pressure teams. Effect (OBS reader scan of the running
flag `T+0xb709`): read by the DELAY/PRESS writer `FUN_140536fc0` and the PASS_COURSE_CUT lane builder `FUN_1406350c0`
(their branches not reduced). Live: 2 starts, 0.02 s and 0.15 s after losing the ball; the first ran 272 frames
(5.04 s) and stopped on the 5 s limit. **Interaction with instruction 0xd (TacticsRE live, its label: gegenpress):**
0xd arms on 8 of 9 in-play losses (never on losses during stoppages), 0–3 frames after the loss, for 0.7–5.7 s
(team 0: 23991–24105, 31904–32043, 35257–35297; team 1: 23729–23865, 28475–28586, 31350–31673, 34995–35043,
35765–36007). The veto sits only in the start tests: ForeCheckPress started at 35256, **one frame before** 0xd armed at
35257, and kept running through the 0xd episode to its 5 s limit (OBS both captures). The 39783/39790 starts are
outside TacticsRE's capture (23050–36477); INF(med) a loss that did not arm 0xd. So in practice a Frontline team's
fore-check / TransitionChase either wins the race on the loss frame or starts after the 0xd episode ends.

### 2.3 Regroup: `SubConceptRetreatBlockCreate` (id 0x25) and `SubConceptForeCheckLineDown` (id 0x22) (OBS code + live)
Running flags are `T+0xb6e8 + id` (one byte per concept). Their only effect found: the shape pass `FUN_140687fb0`
(slot targets) — **when neither 0x25 (`T+0xb70d`) nor 0x22 (`T+0xb70a`) runs**, the three line bands are pushed up so
the back line sits at `S+0x7d58 − 4·dir` (−15 with `ctx+0x138`), no closer than 6 m behind the ball-or-receiver point
`FUN_14063fdc0`, capped at own-frame −5, all bands shifted by the same amount; **while either runs, that push-up is
suppressed and the team keeps its deeper block** (other readers: 0x25 gates RetreatBlockForwardPass `FUN_140576350`;
0x22 gates ForeCheckLineUp `FUN_1405752a0`/`FUN_140575300`). `S+0x7d58`/`+0x7d5c` (= `T+0x89a0`/`T+0x89a4`) come
from `FUN_140687820(ctx, S, line 0/1)` — INF(med): the measured position of the back/midfield line.
- **RetreatBlockCreate** (Defensive Style 1 teams): start `FUN_140575ea0` = in play and its own end test false; end
  `FUN_140575db0`: stop unless (|dir·T+0x89a0 − T+0x875c| ≥ 5 m or |dir·T+0x89a4 − T+0x8770| ≥ 5 m) and
  (dir·T+0x89a0 ≥ 8 − F[6] or dir·T+0x89a4 ≥ 18 − F[6]) → **"regroup while the back or midfield line is ≥ 5 m away from
  its band and still in front of our box"** (team-plans §4.1 reported this test as a slot-3 gate).
- **ForeCheckLineDown** (Defensive Style 0): start `FUN_140574ea0` = in play, no keeper holding (`FUN_140a53f90 < 1`),
  ball own-frame x in [−F[0]/2.5, 27] (`FUN_140574a30`: −21.0…27 m), |`T+0x89a0` − `T+0x875c`| ≥ 3 m (`FUN_140574ab0`),
  and one of: `T+0xb8a3` set; `FUN_140574bb0` (opponent carrier/holder with no cover inside an 80°, 4–7 m test,
  `FUN_140a4fea0`); `FUN_140574940` (the holder in anime 8 LONG_PASS within 70° of our goal). Ends when any of these
  lapse or the line is within 3 m of its band.
- **Live (OBS `runflags.py`, frames 26095–42278, every frame, `live1` for ball team):** 0x25 (All-out Defence team)
  18 starts: 8 at `MI+0x11a0 = 1` (the frame the possession changed), the rest 1.2–37 s into the opponents'
  possession (INF(med): when their attack pulled the lines ≥ 5 m off the bands); durations 0.02–15.9 s (median ≈ 2 s), ending
  at regain or when the lines were back. 0x22 (Frontline team) 23 starts, none at a turnover (1.8–37.7 s into the
  opponents' possession), 0.1–3.4 s each (median 1.0 s).

**Transition in one paragraph (losing the ball):** the MATCH_UP / mark / cover passes re-run on the new carrier the
same frame; a Frontline-Pressure team (no instruction 0xd) may add a 2–5 s fore-check and, for a wide carrier in its
attacking third, one TransitionChase trapper; an All-out-Defence team instead flags RetreatBlockCreate, which keeps its
bands deep until the lines are back within 5 m of them.

## 3. Winning the ball: counter-attack concepts (OBS code)

Slot-1 gate (TacticsRE): **Attacking Style option `k0 == 0` (Counter Attack) → CounterAfterGkCatch, SideCounter,
FastCounter, ReverseSideCounter; `k0 == 1` (Possession Game) → PossessionMfJoinDefenceLine.** Concept orders are
written into `T+0xb748[slot]` (kind: 3 = SideCounter, 5 = FastCounter, 0x24 = TransitionChase), `T+0xb774[slot]`
(the AI action), `T+0xb7a0`, `T+0xb7cc` (pass target point), `T+0xb7d8` (the slot the carrier should look for),
and a pass plan for the carrier in `T+0x6f4` (`FUN_140a85160`).

### 3.1 `SubConceptSideCounter` start, slot 2 `FUN_140540780(concept, {ctx, side}, T)`
1. `T+0xa508 ≠ 0` (INF: concepts enabled for this team); `MI+0x11a0 ≤ ⌊3·fps + 0.5⌋`;
   `FUN_140a74ec0 ≥ ⌊6·fps + 0.5⌋` (not in the first 6 s after a half-start kick-off);
2. ball holder `h` = carrier `MA+0x2601c` or holder `MA+0x2483c`; `FUN_140a53f90` false; our keeper's anime ≠ 0x41
   KP_PUNTKICK;
3. `|P(h)+0x554 − attackHeading(dir)| ≤ 55°` (0x14259be38): **the man on the ball faces forward**;
   `FUN_140a4e720(ctx, h, heading, 150)` false (INF(med): no opponent blocking the 150-unit forward cone);
4. **ball in our half**: `−F[6] ≤ dir·ball.x ≤ −12` (0x14259d664) and `|ball.z| ≤ F[7]`;
5. opponents (non-GK, on pitch) counted goal-side by `FUN_140a4c720`; abort if one is on the ball's side of the pitch
   and level with the ball within 4 m (0x1425656c0); **need fewer than 3 opponents goal-side**;
6. runner `r = FUN_14053e9a0(…, 0)` (OBS, scoring structure): two attack channels at the opponents' offside line
   `MA+0x44+0x14·opp`, `z = ±(F[7] + 0.5·(F[1] − F[7]))`; flags which channel already has an opponent within
   17–20 m (0x1425c7224, 0x1425a946c). Candidates in the depth-ordered list `FUN_140a71500`: position category
   `T+0x4c00 > 4` (INF: wide/forward roles), already in or allowed into `SPACE_RUN_COUNTER` 0x1f (priority gate),
   not vetoed by `FUN_1405951b0(−6)`, **their nearest marker more than 6.5 m away** (42.25 = d²), on the ball's side
   of the pitch, within `11 m` (20 m with `FUN_1406405c0`) of the closest channel distance. Score = distance to
   the channel point, −10 on the ball's side, ×0.7 / ×1.3 by `FUN_1405c0f00` / `FUN_1405c1580` (INF: run-style
   bits), ×1.5 (`FUN_1405951b0(−14)`), ×1.2 for a channel already covered, then **Attacking Area** (sheet option 2,
   `FUN_140a62180(ctx, side, 2)`): 0 (wide) favours men with |z| or slot-target |z| beyond the channel / 17 m (×0.7),
   1 (centre) the opposite (×1.2). Lowest score wins (ascending sort `FUN_140a79640`).
Then: `r` gets **`SPACE_RUN_CHANCE` (0x1e)** to point `FUN_14053f510` (`T+0xb748 = 3`, `b774 = 0x1e`); three
supporters `FUN_140541010` (OBS) get **`PASS_SUPPORT` (0x22)**: for each point — #0 ball + 15 m along the attack
heading (0x14259be0c), #1/#2 ball + 12 m (0x14259be04) along the heading turned ±22° (0x1425c48c0) — the nearest own
on-pitch player (not the holder, runner or an earlier pick; current state priority > 2, i.e. not in a run/press/
MATCH_UP state; position category > 2), with #0 required ≥ 8 m ahead of the ball, #1 on the higher-z side, #2 on the
lower-z side; −5 m (0x14259be7c) bonus for the man already holding that role. `T+0xb7d8 = r` (carrier's target).

### 3.2 `SubConceptFastCounter` (vtable 0x1425c3e00; start slot 2 is the stub `0x141863350`, so the manager never
starts it; its running flag has no other writer — **dead code in AI play**, OBS via TacticsRE) — continue `FUN_140543890`, execute `FUN_140544b80`
Record: `+0x18` stage, `+0x1c` A, `+0x20` B, `+0x24` C, `+0x28` D, `+0x2c` stage start frame, `+0x8` start frame.
- Stage 0 → 1 when A has the ball (`FUN_1405969b0`): **B `SPACE_RUN_COUNTER` (0x1f)** (`b748 = 5, b774 = 0x1f`),
  C `PASS_SUPPORT` (0x22) at `FUN_1405431c0`; carrier pass plan A → C. (Before that, B is picked by
  `FUN_140542d70` (OBS): own on-pitch men from the depth list, not A/C, state overridable, not falling, **≥ 6 m
  upfield of the ball**, `|Δz| ≤ 24 m` to C, not caught by `FUN_140a4c9a0(·, 1.0)` but inside
  `FUN_140a4c9a0(·, −20)` (−15 after the first three) (INF: onside-line window), skipping men within 18° of C's
  direction when C is ahead (< 32°); score = list distance − 18·ramp(|z|; F[7] + 2 → 0, F[5] − 3 → 1) (wide bonus up
  to 18 m); lowest wins.)
- Stage 1 → 2 when C has it: **A also runs `SPACE_RUN_COUNTER`**; C's target is the 45°-offset point 8 m from A;
  stage 2: C (or the holder) passes to the runner `FUN_140543700` if `FUN_140543e00`, else to B's position.
- Stops (slot 3 returns 1) when: out of play; **> 8 s since start** (`fps·8`); stage 1 older than 4 s; the ball holder
  is not one of A/C/D; event-0 count > 2 while C's ball-distance rank ≥ ⌊0.5·fps + 0.5⌋ or a gap > 22 m (484 = d²);
  **or the ball reaches own-frame x > F[6] − 8 (28.1 m)** (end of the counter at the box).

### 3.3 `SubConceptCounterAfterGkCatch` (vtable 0x1425c3d80) — start `FUN_14053f710`, continue `FUN_14053f600`
Start (OBS): in play; our keeper `k = FUN_140a62690(ctx, side)` holds the ball (`FUN_140a53f90(ctx, &k) > 0`) and his
anime is not 0x41 KP_PUNTKICK / 0x42 KP_THROWPASS / 0x44 KP_DROP_BALL; a free area is searched from the seed point
(`dir·F[0]/−3`, `5 − F[1]`) (`FUN_14053f9f0`, 0x14259d654 = −3) and must hold **< 6 opponents** (`FUN_14053f3d0`);
runner `FUN_14053fd80` (OBS: the own on-pitch player nearest the area point within 30 m (900 = d²), position category
∉ mask 0x283 = {0, 1, 7, 9}, not falling) gets `BASE_POSITION` (1) with that point as target (`b748 = 2`, `b774 = 1`), the keeper
`b748 = 2`, `b774 = 0`, and `T+0xb7d8 = runner` (the keeper's distribution target). Continue: while the keeper holds
the ball in KP_PUNTKICK/THROWPASS, or holds it and the area has ≤ 7 opponents; after the release while
`FUN_140a53f90` holds and **≤ 10 s** since start.

### 3.4 Concept ids (`T+0xb748` order kind = selector id = running-flag index `T+0xb6e8+id`; OBS: the selector's
concept array `FUN_140579e10` maps id → `brain+0x62b0+off` and the constructor `FUN_1404efab0` stores each class's vtable there)
1 ChangeTriangle (near), 2 CounterAfterGkCatch, 3 SideCounter, 4 ReverseSideCounter, 5 FastCounter (near),
6 LongPassFlick, 7 LongPassLayOffAndToSide, 8 LongPassLayOffAndBreakthroughToDefenceLine, 9 LongPassLayOffAndShoot,
0xa EarlyCross, 0xb OneTwoPassFromSideToCenter, 0xc PassAndGoTriangle, 0xd ForwardPassAndLayOff, 0xe OneTwoPassCutIn,
0xf SupportBackOneTwo, 0x10 PenetrateInsideOneTwo, 0x11 ToCenterDecoySide, 0x12 SpaceRunRelaySquarePass,
0x13 DisorderByOneTwo, 0x15 CreateSpaceOneTwo, 0x16 SideChangeToWeakSide, 0x17 SpaceRunToCenterForCreateSideSpace,
0x18 SideOverlapAndSupport, 0x19 SideOverlapAndSpaceRun, 0x1a SideLongitudinalOneTwo, 0x1b SideInnerlap,
0x1c SideRunMakeSpace, 0x1d SpaceRunToCreateSpaceForOneTouchPass, 0x1e MoveDownMakeSpaceSide, 0x1f SideCrossRunning,
0x20 DiagonalPostPlay, 0x24 TransitionChase; **0x30 = no order** (written as the clear value). Writers: `b748.txt`.
Defensive ids (selector array, OBS): 0x21 ForeCheckPress, 0x22 ForeCheckLineDown, 0x23 ForeCheckLineUp, 0x24 TransitionChase,
0x25 RetreatBlockCreate, 0x26 RetreatPressBack, 0x27 RetreatBlockForwardPass, 0x28 RetreatCrossBlockCreate,
0x29 NetDefenceInducementIntoTheCenter, 0x2a NetDefenceCentralSurroundingPress, 0x2b PassCut,
0x2c SealOffInducementToTheSides, 0x2d SealOffSideSurroundingPress, 0x2e DoublePress; 0 PossessionMfJoinDefenceLine,
0x14 no object. Transition concepts: 2–4 (5 is dead), 0x21, 0x24, 0x25 (0x22 runs during the opponents' possession,
not at the turnover); the rest are attacking patterns or block shapes (attack-decisions / TacticsRE scope).

### 3.5 Not decoded this pass
`ReverseSideCounter` (`FUN_140577a10`/`FUN_140577800`), `PossessionMfJoinDefenceLine` (`FUN_14053d730`),
`FUN_14053f510` (runner target), `FUN_14053f9f0` (GK-catch area search), `FUN_140543700`/`FUN_140543e00` (FastCounter
final pass). Decompiles are in `t7a.c`/`t13a.c`.

## 4. Receiving the ball: first touch

### 4.1 Reaction-table entries involved (OBS; corrects DefenderDecisionsRE §1)
- **Entry 0 `FUN_140933ad0` is the body-contact reaction**, not the receive: it calls `FUN_140844cb0(anime)` which
  returns AI action 0x58 STAGGER / 0x59 FALL_DOWN with a sub-kind (contact with an opponent within 1.5 m: speeds,
  stat 0x28 via `0.3 + 0.2·clamp((S−40)/59)` / `0.2 + 0.1·…` thresholds, 45°/60° geometry) or anime 0x15 DODGE
  (writes the dodged player's position into `cmd+0x8b0`). KILLED: "entry 0 = ball receive".
- **Entry 3 `FUN_1409f8220` = receive.** Refuse if: `P+0x5d0 == 5 PASS_GET` with `ai+0x1457 == 3` or `ai+0x1458 == 3`;
  in a kick clip before its contact frame (unless the order byte is 0x3a or `cmd+0x19b9`); receive trigger
  `FUN_1409fa400` false (§4.2); anime is 0x5d. Then:
  - **data-driven touch** (`DRIBBLE 3`, anime 0x5d): only when `FUN_1409fbb40` (order kind 0x4a or `ai+0x14 == 5`)
    and `FUN_1409fbba0` (ball ≤ 0.217 m high 10 frames ahead and the player's 0.694/fps-s prediction within
    `38000/(fps·3600)` = 0.195 m of the ball next frame); refused if the planned direction `cmd+0xe48` differs from
    `FUN_140439f90` by ≥ 157.5° (0x1425cca3c);
  - else **TRAP**: `FUN_1409f8640` builds the request (§4.3) and asks the anime layer `FUN_1406b4580(anime, 7, …)`;
    on success `cmd+0x874 = 4 TRAP`, `+0x878 = 7`, target `cmd+0x8b0`, `cmd+0xe6c = 7`. If the anime refuses with
    reason code ∈ {3, 7, 8} (`(r − 3) & ~5 == 0`, `r ≠ 4`) while the request sub-kind `R[1]` is 4 and flag `+0x35` is
    clear, it retries once with `R[1] = 0`.
- Entry 11 `FUN_140a08be0` (PASS_GET + JOSTLE): the shoulder-to-shoulder duel for an incoming pass when the anime
  allows JOSTLE (0x48) (`FUN_140a08d00`).

### 4.2 Who may touch now — `FUN_1409fa400` → `FUN_1409fa520` → `FUN_1409fa720` (OBS)
- Only players with a ball-distance entry `MA+0x24710[…] ≤ 100` (sorted list `MA+0x247be` rank), and if I am the
  carrier only from anime 0x44 KP_DROP_BALL (when `MA+0x251fa[side]` is clear), 0x10, 0x15.
- Anime layer can start TRAP (`FUN_1406b4c10(anime, 7)`), `FUN_140a53f90` false, not `MA+0x2600c` (INF: the kicker).
- Out of play: only at listed restarts (MODE 3 goal kick after event 1..3/0x10, special mode 0x18).
- **Re-touch lockout** (frames since the newest ring-0 ball event `+0x10` must be ≥ `fps·L`), `trap.json` (registry
  0x3e) live: `L = offence 0.05 s` (my team's ball, I'm not the carrier) / `defenceKeep 0.26 s` (someone else's
  controlled ball) / loose ball: `defence 0.16 + 0.1·(1 − s²)` with `s = clamp((stat+0x1e − 40)/59)` (INF: Ball
  Control); after event 5 (shot) or 3 with the ball > 0.75 m: `0.1·(1 − s²) + shoot 0.5` (≤ 80 km/h) or
  `shootSpeedBall 0.75`; **×3 after my own pass**, ×2 shortly after my own touch.
- Shot rule: after a shot by my team, no trap if the ball is within 30° (+½ the goal-mouth angle) of the goal and
  ≥ 35 km/h (lets shots through).
- Teammate/opponent conflict loop (22 players): skip when another player's kick/trap clip contacts the ball within
  `param_4 − 2` frames, an opponent is 0.2·fps from his own contact and closer, etc. (not fully reduced).

### 4.3 The first-touch request — `FUN_1409f8a50(this, nextCmd = cmd+0x874, cmd, R)` (OBS)
`R[0]` = **touch kind** (passed to the anime as the trap type; clip names `trap_<kind>_<dir>_y<height>_<variant>`,
strings 0x1425f2150…), `R[1]` sub-kind, `R+8` target point (y = −1 → "no target"), `R+0x14` ball at the trap contact
frame `R[9] = FUN_1406b3d60(anime, 7)`, `R[8]` touch heading, flags `+0x34` (forced kind 4), `+0x36` (no direction),
`+0x37`, `+0x38`, `+0x39` (stop dead), `+0x3a` (open-body touch).
Default: `R[0] = 0`, target = own position + 1 m along the body heading.
1. **Touch into a pre-decided kick** (`nextCmd` anime, `cmd[1]` or `ai+0x14` is a kick class `FUN_140a6e340`):
   target = the kick's target point `cmd+0xe8c`, **`R[0] = 3`** (one-touch / touch-and-kick).
2. Otherwise `FUN_1409fa0b0` (OBS) may fix the kind: pad-player cases; `P+0x5d0 == 1` → keep; **ball event < 0.5 s
   old (`fps·50/100`) and ball faster than 8.33 m/s (69.44 = v², 30 km/h) from the other team** (an interception) →
   kind = the planned trap type `cmd+0x5c` if the carrier brain already holds a pass/shot whose target is within 45°
   of the body's predicted heading, else 0 (or `FUN_140a75be0` by the order byte).
3. Body facing / direction (normal receive, own team's pass):
   - planned direction known (`cmd+0x5d4 ≥ 0`, the carrier brain's chosen next direction): target = own position +
     **5 m** (0x14259222c) along it, kind from `cmd+0x5c`, flags `+0x38`/`+0x3a`;
   - kind 4 forced by `FUN_1409fa330` (pad users only, `cmd[0] == 0x1b`);
   - "**let it run**": order kind 0xb → kind 0, target = 5 m along the order's heading, `+0x36`;
   - a DELAY/PRESS defender receiving (`P+0x5d0 ∈ {0x33, 0x34}`) → kind 0 forced (just control it).
4. **Running trap**: kind 2 (target 5 m along the 0.694/fps-s velocity prediction) only when the player is in anime
   MOVE (2), `cmd[0] == 2`, and his speed exceeds `passget.json neutralTrapAutoFront` = **100 (km/h) live → never
   for AI** (OBS live value; INF(high) dead in practice).
5. Touch-line guard: if the ball is coming within 25° of the touch heading and the body faces > 135° away, and the
   point 1.5 m ahead is within 1 m of a line → kind 0.
6. `FUN_1409fbce0` (trap.json `use` = 1): for a ball not passed by my own team (`checkMyBallPass` 0), within
   `busyTime` 0.5 s of the event and faster than 3.75 m/s: the allowed turn between body heading and touch direction
   is `runTrapStopTurnAngle` 90° when running with the ball (facing within 45° of the ball path and fast) else
   `stopTrapStopTurnAngle` −1° → **a stationary interceptor's touch is forced to kind 0 (plain trap, no turning touch
   kind)** — INF(med) on what kind 0 looks like (the clip choice is the anime layer's);
   within the limit kind 4 → 3.
7. If the target equals the own position and kind 0 without `+0x36` → `+0x39` "stop dead".

**Turn vs. shield vs. lay-off:** there is no separate chooser in the receive. "Lay-off / first-time" = kind 3 with the
carrier brain's pre-decided kick (`BallPlayer` think units run before the ball arrives, carrier-ai §2); "turn" = a
5 m directional touch along the brain's planned direction when the angle rules allow it; "shield/stop" = kind 0.
INF(high).

### 4.4 Trap constants (live `trap.json`, registry 0x3e, deserializer `FUN_141e681c0`; full dump `consts_live.txt`)
`busyTime` 0.5, `checkMyBallPass` 0, `runTrapStopTurnAngle` 90, `stopTrapStopTurnAngle` −1, `use` 1; lockouts
`defence` 0.16, `defenceKeep` 0.26, `offence` 0.05, `shoot` 0.5, `shootSpeedBall` 0.75; anime-layer touch push
`offsetPos_<in>_<out>` (Idle/Run/Dash) 0.5…2.25 m, `speed_0/15/28` 0.5/1.75/2.0, `ballControlRate` 1.0,
`ballControlWeekFootDownLimit` 20, `checkEnemyFutureFrame` 0.33, `checkSectorDist` 2.0 (consumers not traced).
Live winner is the gameplay mod's `constant_player.bin` (DefenderDecisionsRE §7 note applies).

## 5. Stepping to meet the ball — `ActionPassGet` (AI action 5; OBS structure)

`match::player::ActionPassGet` (vtable 0x1426301e0) owns a route manager at `+0x70` (ctor `FUN_14091e390`) with seven
route objects: `+0x00` BallFollow, `+0x610` MoveStraightTrapStop, `+0xc68` MovePositionAdjust, `+0x11f8` NextPlay,
`+0x1828` BallAvoid, `+0x1e00` TargetEnemy, `+0x24c0` ThrowIn (`match::player::PassGetRoute*`). Per frame
`FUN_140945ec0` tries them in order (first slot-2 `true` wins): ThrowIn → NextPlay (`passget.nextPlayEnable` 1) →
BallAvoid → MovePositionAdjust (`positionAdjustEnable` 1, `positionAdjustSide` 0.3, angle sub-max 35°) →
MoveStraightTrapStop / TargetEnemy → BallFollow. The receiving point is the ball's predicted position at the
arrival frame `FUN_140a4d340` (0 when the ball is slower than 5000/(fps·3600) per frame), clamped into the pitch
0.163 m inside the lines (`FUN_14094e3a0`). Route result codes `+0x6c`: 0 normal, 1, 3, 4, 5 (next play, target id
`+0x68`), 7, 8, 9 (reset). `passget.json` (registry 0x35) live: `circleDist` 9, `ballArrivalFastSec` 1.0,
`ballSpeedMin/Max` 4/42, `secMin/Max` 0/0.35, `paraMin/Max` 20/120, `ballHeight` 1.5, `targetDist` 3,
`openBodyMaxValue` 45, `passGetThroughNoTrap` 1, `neutralTrapAutoFront` 100. The route bodies are not reduced.

## 6. GF code each finding replaces

| finding | GF code |
|---|---|
| §1.2 kick-off-only line hold-back / cover gate / mark radii | nothing in GF (no kick-off phase); DefensiveShape ports that used these as "possession change" ramps must key them to the newest kick-off instead |
| §1.1 + §2 TransitionChase (one extra man, ≤ 3 s, wide carrier in our attacking third, widest man within 20 m), ForeCheck (2–5 s window, not with instruction 0xd), RetreatBlockCreate / ForeCheckLineDown (suppress the band push-up while the lines are ≥ 5 m / 3 m off) | `TeamAIController::Process` turnover block (teamAIcontroller.cpp ~292–318: `ShouldStartZonePressureOnLoss` → `ApplyZonePressure`, 250 ms `ApplyTeamPressure` refresh, `zonePressureEndTime_ms`), `aitactics.hpp` `ShouldStartZonePressureOnLoss`, `TeamPhilosophy::PressesOnPossessionLoss`; the regroup part has no GF counterpart (GF's formation update has no "stay deep until reformed" state) |
| §3 counter concepts (≤ 3 s, ball in own half, holder facing ≤ 55°, < 3 opponents goal-side → runner + 3 supporters; FastCounter A→C→B; GK-catch outlet) — deterministic, no RNG | the counter block (teamAIcontroller.cpp ~319–344: `ShouldLaunchCounter` with `random()`, `GetCounterWindow_ms` 2–5 s, `ApplyAttackingRun`) and the 500 ms tactics-run trigger (~348–394, `SelectAttackingRunPlayer`) |
| §4 receive = reaction entry 3, request kind 0/3/(2)/4, 5 m directional touch along the carrier brain's pre-decided direction, stationary interception = kind 0, trap.json lockouts | `ElizaController::_TrapCommand` (elizacontroller.cpp ~557) and the humanoid trap path (`humanoid.cpp` `GetTrapVector` ~471, `GameplayTuning::GetTrapTouchableDistance`/`GetTrapAcceptGateScale` ~512–551) |
| §5 PassGet routes | Eliza's receiver lead/go-to-ball movement (`GameplayTuning::GetReceiverLeadTime_sec`, elizacontroller.cpp ~1571) |

## 7. Live check — status

### 7.1 Existing captures (OBS)
- **Concept episodes** (`conc2.py` over DefensiveShape2RE's `live2.bin`: 1975 untorn in-play records ≈ 99 s at
  20 Hz, 7 changes of the carrier's team): order changes seen — team 1 (Attacking Style option 0 = Counter Attack)
  **id 2 CounterAfterGkCatch on slots 0 (keeper) and 6, 0.06 s after the change of possession, lasting 4.1 s**
  (keeper `b774 = 0`, runner `b774 = 1`, as §3.3); team 0 ids 8 (×3, `SPACE_RUN_CHANCE` + `PASS_SUPPORT` pairs),
  9, 0x12, 0x15 at 2.6–21.7 s after a change (attacking patterns, not transitions). No 3/4/5/0x24 episode in this
  short window (both need the ≤ 3 s window plus strict geometry). `T+0xb748` idles at 0x30.
- **First touches** (`traps.py` over DefenderDecisionsRE's `tk1.jsonl`, 1,144 open-play logic frames): 4 TRAP starts
  (`hu+1 = 7`), all with `P+0x5d0 = 4 TRAP` (the entry-3 command, §4.1), all receiving a team-mate's ball, clip
  contact frame 18 in every case; touch direction −106°, −100°, −49°, +39° off the body heading at clip start,
  ball 2.2–3.3 m/s after the touch — i.e. **directional, turning touches** for own-team receives (§4.3 step 3).
  No interception in the window.

### 7.2 Second-half capture (OBS, `samp.py` → `live1`, 16,812 records at 20 Hz, frames 18360–42278; `runflags.py`
every frame 26095–42278)
Team sheets live: team 0 options `1,0,1,0,0,0,1` (Possession Game, Frontline Pressure), team 1 `0,1,1,1,1,0,0`
(Counter Attack, All-out Defence); `T+0xa508` = 1 / 0.
- (a) `MI+0x11a0`: §1.1 — resets with the possessing team (18 of 22 ≥ 1 s changes), counts through stoppages.
- (b) `MI+0x119c`: §1.2 — resets at each ring-5 record, frozen while stopped; ring 5 gained a kind-1 record for a
  mid-half kick-off.
- (c) concepts (`chk_c.py`, `runflags.py`): **TransitionChase once** (team 0, frame 39790): lost the ball 0.28 s
  earlier, carrier at own-frame x 35.6 (≥ 26.1) and |z| 18.7 (≥ F[7] − 2), order 0x24 to slot 7; it stopped after
  6 frames because the MATCH_UP man changed (slot 4 → 5 → 2), as the continue test requires. ForeCheckPress,
  ForeCheckLineDown, RetreatBlockCreate: §2.2–§2.3. No counter-attack concept ran: team 1 (Counter Attack) has
  `T+0xa508 = 0`, which SideCounter requires, and its keeper did not catch a ball; team 0 orders were attacking
  patterns only (6, 7, 8, 9, 0x13). Team 1 orders 1, 0xd, 0x13, 0x1d.
- (d) first touches (`traps2.py`): 95 TRAP starts (`hu+1 = 7`). 84 from a team-mate's ball: touch direction
  median 58° off the body heading at clip start, 35 % within 30°, ball 3.2 m/s after the touch — directional,
  turning touches dominate (§4.3 step 3). 10 after an opponent's ball, but only one by a stationary player (0.2 m/s,
  ball 18.5 m/s), 61 frames after the event, i.e. outside the 0.5 s `busyTime` window of §4.3 step 6: **the
  interception rule remains unchecked live.** The 20 Hz sampling cannot resolve the 0.05–0.26 s lockouts.

## 8. Killed hypotheses
- "`FUN_140a74ec0` = frames since the last possession event" (DefensiveShape §1/§6/§14): it is the kick-off timer
  (§1.2).
- "ForeCheckPress needs instruction 0xd armed" (team-plans §4.1 [P4]): the raw branch returns false when it is armed.
- "Ring 5 kind 1 = half-start kick-off only": a mid-half kick-off (MODE 1 stop) also wrote kind 1.
- "Reaction entry 0 `FUN_140933ad0` = ball receive": it is the body-contact stagger/fall/dodge reaction.
- "`T+0xa53c` is a transition press timer": it is a late-game losing-side press flag (INF(med)).
- "`ctx+0x138` is a transition mode" (DefensiveShape §6 note): it is a byte passed through `FUN_1405ebfa0`
  (`ctx+0x48` of the shape owner, cleared when `T+0x218 == 0`), INF(med) a training/demo mode.

## 9. Open list
1. Live still open: a SideCounter episode (needs a Counter-Attack team with `T+0xa508 ≠ 0`; also decode what sets
   `T+0xa508`), the stationary-interception touch rule (needs a per-frame capture like `tk1` around interceptions),
   and the 0.05–0.26 s re-touch lockouts (per-frame capture).
2. `team/subConcept.json` (registry 0xc4) per-id enable values (live read).
3. ReverseSideCounter, PossessionMfJoinDefenceLine bodies; `FUN_14053f510`, `FUN_14053f9f0`; the run-style bit
   helpers `FUN_1405c0f00`/`FUN_1405c1580`.
4. PassGet route bodies (§5) and the anime-layer Trap clip choice (`FUN_1406b4580` with 7; sim-motion-mapping has the
   TRAP row selection).
5. `MI+0x11a0` writer.
   (INF(med): MI is filled through the reflection layer; no direct store found by displacement scans of 0x11a0/0x38).
6. PASS_GET (5) trigger: no team-level writer of state 5 (`FUN_140a42940`/`FUN_140a429a0` call sites give only
   MOVE_ON_PASS 0x10); find the player-level selector that creates `ActionPassGet` for the pass target `MA+0x24838`.
7. Re-run live: `sudo python3 samp.py live1 900 20 <H>` (H = history object, found by `findhist2.py` per match load).

## 10. Ledger entries
| ID | Claim | Conf. | Evidence |
|---|---|---|---|
| EV-TRN-0001 | `FUN_140a74ec0` = in-play frames since a kick-off (ring-5 kind 1), else 0xffffffff read unsigned | Confirmed (raw + live ring-5 / `MI+0x119c`) | 0x140a74ec0, 0x1405fb859/0x1405fb89a; ring map 0x142642e60; `dumpring.py`, `live1` |
| EV-TRN-0002 | Ring 5 = restart records (`+0x14` team, `+0x18` MODE) | High (code + live 3 records) | `FUN_140a3dc40`, `FUN_14062c400`, `FUN_140509890`, `FUN_141475ca0` |
| EV-TRN-0003 | `MI+0x11a0` = frames since the possessing team changed (counts through stoppages) | High (readers + live 18/22 changes, 28 resets) | `FUN_140575510`, `FUN_140540780`, `FUN_1405747f0`, `FUN_140574710`; `chk_a.py` |
| EV-TRN-0004 | TransitionChase rule (§2.1) incl. second-man pick | Confirmed (code) | 0x140575510, 0x140575930, 0x140575b60, 0x140575410 |
| EV-TRN-0005 | ForeCheck 2–5 s window, ball below −10 stops it | Confirmed (code) | 0x1405747f0, 0x140574710 |
| EV-TRN-0006 | SideCounter trigger, runner and supporter picks (§3.1) | High (code; scoring helpers partly INF) | 0x140540780, 0x14053e9a0, 0x140541010 |
| EV-TRN-0007 | CounterAfterGkCatch outlet (§3.3) | High (code + 1 live episode) | 0x14053f710, 0x14053fd80, 0x14053f600; `conc2.py` |
| EV-TRN-0008 | Concept ids in `T+0xb748` (§3.4) | High (immediate writers + vtables) | `b748.txt` |
| EV-TRN-0009 | Receive = reaction entry 3; entry 0 = contact stagger/fall | Confirmed (code) / live TRAP = action 4 (n = 4) | 0x1409f8220, 0x140933ad0, 0x140844cb0; `traps.py` |
| EV-TRN-0010 | First-touch request rules (§4.3) and trap.json lockouts/turn limits (§4.2, §4.4) | High (code + live constants) | 0x1409f8a50, 0x1409fa0b0, 0x1409fbce0, 0x1409fa720; `consts_live.txt` |
| EV-TRN-0011 | No RNG in the transition concepts or the receive | High (no RNG primitive calls in the decompiled bodies) | `t*a.c` |
| EV-TRN-0012 | ForeCheckPress and TransitionChase both need instruction 0xd *not* armed | Confirmed (raw) | 0x1405748d0, 0x1405755df |
| EV-TRN-0013 | RetreatBlockCreate (0x25) / ForeCheckLineDown (0x22) gate rules and the band push-up they suppress | High (code) / live episode counts | 0x140575ea0, 0x140575db0, 0x140574ea0, 0x140574b00, 0x140687fb0; `runflags.py` |
| EV-TRN-0014 | Live transition episodes: 1 TransitionChase (conditions met), 2 ForeCheckPress (5.04 s), 18 RetreatBlockCreate, 23 ForeCheckLineDown, 0 counters | Confirmed (live, ~7 min of second half) | `runflags.txt`, `chk_c.txt` |
