# PES 2021: individual defender decisions (DefenderDecisionsRE)

Read-only RE. Image `pes_full.bin` (Unpacker), decompiles via `dec.sh` + `annot.py` (hypotheses only), raw via `pdis.py`.
Scratch: the DefenderDecisionsRE scratch dir (`d1..dN.c` decompiles, `dscan.py` displacement-store scanner, `ripref.py`
rip-relative xref scanner, `argrdx.py` call-site edx extractor, `getters.txt` every constant-registry getter call,
`regnames.txt` registry index → `.json`, `names.txt` AI/anime id names). Tags: **OBS** = decompile/raw at the VA (or
live read); **INF(conf)**.

Notation: `G = *(0x1436f3dc8)`; `MI = *(G+0x270)`; `pl(i) = FUN_140a48a10(G,i)` (player: pos `+0x55c/+0x564`,
heading deg `+0x554`, vel `+0x574/+0x57c`, current AI action `+0x5d0`); `hu(i) = FUN_140a48910(G,i)` (anime state:
`+1` anime action id, `+0x1a` clip frame, `+0x1c` clip contact/end frame); `ai(i) = FUN_140a489f0(G,i)` (published
AI record, the one the Judge reads); `gs = FUN_141e532d0()` (fps, 54). `pc` = the per-player think context passed to
every reaction candidate (`pc+4` player id, `pc+0x28` team order record, `pc+0x30` constant registry, `pc+0x38`
match context, `pc+0x40` anime context). `cmd` = the command record the candidate fills (`cmd+0x874` AI action id,
`cmd+0x878` anime action id, `cmd+0x8b0..` target point, `cmd+0xfc4` tackle variant). AI action names = table
0x1434a4d50; anime names = table 0x1434a54d8 (`names.txt`).

## 0. Summary

1. **The engaging defender does not have a think-unit list like the carrier.** Team AI (DefensiveShapeRE's area)
   puts him in `PRESS 0x34` / `DELAY 0x33` / `SAND 0x35` / `MARK 0x36` (movement). The ball-winning act is chosen
   by a separate **reaction chooser `FUN_140a06dd0`** (reads `reaction.json`, registry 0x39) that walks a fixed,
   ordered table of predicates (`ScriptBind` table @0x14263ec60) and adopts the **first one that returns true**
   (§1). Tackle is entry 1, sliding entry 2, block entry 8. No score competition.
2. **`ai(B)+0x134` is "this challenge was made by a pad-controlled (human) player"**, not a CPU foul intent (§4).
   Writer `FUN_140a596f0` (`mov [rcx+0x134],dl`) from the per-player publisher `FUN_14092d580`. In AI-vs-AI it is
   never 1, so the Judge's DOGSO +30 (when not reckless) and the "professional foul ≥ yellow" floor never fire
   for CPU tacklers.

3. **Tackle (standing) trigger** for a CPU defender (§2): a geometric/timing gate, no RNG, no score. Outside
   PRESS/DELAY/SAND/FREE_RUN/WAIT_TIMER the ball 0.25 s ahead must be ≤ 3.0 m away and ≤ 22.5° off his predicted
   heading; the solver's contact point must be reachable (≤ 3.0 m); the engagement angle to the carrier must be
   ≤ 112.5° and the predicted gap ≤ 2.0 m; from behind (> 135° carrier-relative) the tackle is refused unless the
   approach is within 11.25° / 45° / 0° tolerance (by "aggressive level" 0/1/2 = playing style ENFORCER + team
   defensive mode 3); a non-PRESS/SAND defender who is the last line (≤ 10 m from his line) or has no cover does not
   tackle once gs/9 frames have passed since the last ball event.
4. **Slide trigger** (§3): CPU team only, open play only, **odd logic frames only**, difficulty capability **0x08**
   (on at tier ≥ 3, i.e. COM level ≥ 300), ball ≤ 0.5 m high 10 frames ahead, contact point closer than
   `4.0 + 0.8·L` m (L = slide aggression 0..3), no teammate already sliding, the defender must be the first of his
   team to reach the point, no opponent body in the slide path. L = ENFORCER + (team-sheet flag 6 off) + 2·(team
   mode 3) + (marking plan flag 0xe) + (`T+0xa53c == 1`) + **(stamina < 30 %)** + (`T+0xb8a2`), capped 3; inside
   the own box L must be 3.
5. **Block** (§5): owner within 1.3 m (3.0 m while PRESSing), widened to 7 m once a pass/shot clip passes 70 %
   (80 % long pass) of its contact frame for a DELAY/PRESS defender; loose-ball block only within 0.9 s (2.0 s for a
   ball > 70 km/h) — `block.json` normal / speedBall.
6. **Jockey/contain is not a separate decision.** It is the team-level `DELAY 0x33` movement state
   (DefensiveShapeRE, `FUN_14062af10`); the individual choice is only "does any reaction predicate fire this frame".
   No shirt-pull / push decision exists for CPU players (§6).
7. **No RNG** in any predicate read here (tackle, slide, block, reaction walk). OBS: no call to the RNG getters of
   `pes-re-rng.md` in `FUN_140a06dd0`, `FUN_140970e40`, `FUN_14096fda0`, `FUN_140971450`, `FUN_140971ba0`,
   `FUN_140a08ea0` decompiles. [INF(med) for callees not decompiled: `FUN_1405bebd0`, `FUN_140972870`.]

## 1. The reaction chooser `FUN_140a06dd0` (OBS)

Call chain (OBS): per-player AI tick `FUN_140928fc0` → `FUN_1409257b0` → command build `FUN_1409f4db0`
(0x1409265d6). `FUN_1409f4db0` first builds the movement command (`FUN_1409307c0`, the PRESS/DELAY/MARK
locomotion), then calls `FUN_140a06dd0(this+0x8ac, pc, cmd)` (0x1409f5177). If the chooser returns 1 and the
current action is not in the "busy" classes (`FUN_140a6e090` on `+0x44e8`, `FUN_140a6e340`/`FUN_140a6e1c0` on
`+0x2b08`), the reaction command replaces the movement command.

**Cadence:** the context `pc` is rebuilt with the current frame `MI+0x1198` at `pc+0` every tick
(`FUN_140928fc0` @0x1409290ca) — INF(high): the chooser runs **every logic frame (54 Hz)**; the slide predicate
self-limits to odd frames (27 Hz).

Gate (OBS): returns 0 when `MI+0x1775` ∈ {0x0b, 0x17}. Mode `m = FUN_140a06fd0(pc)`:
`m = 2` (from `FUN_141e534f0(id)`) → nothing; player not on pitch (`MI+0x1748[id] == 0`) → `m = 3`; CPU team
(`T+0x255 == 0`), no order `T+0x4c00[slot]`, and (`MA+0x251fc[side]` set or newest event type 6) → `m = 1`;
otherwise `m = 0`.

Table walk (OBS, `ScriptBind` table of `{fn, u8 flagA, u8 flagB}` @0x14263ec60): entry skipped if `flagA` and
`reaction.json+8` (`thinkWaitTimer`, live 0) and `cmd+0x874 == 0x11 WAIT_TIMER`; skipped if `flagB` and `bVar12`
(bVar12 = not [kick-off-like stop: `MI+0x116c == 2` with player action 5, or `MI+0x1775 == 0x18`], or ball mode
`BallInfo+0x2920 == 4` in replay/special). First predicate returning 1 wins: `cmd+0x880 = 1`, `cmd+0x87c = 0x18`
(INF: command priority); a BLOCK (anime 0xe) with `cmd+0xfd5` clears `+0x880`.

| # (m=0 list, 17) | fn | issues `cmd+0x874` AI / `+0x878` anime | meaning |
|---|---|---|---|
| 0 | `0x140933ad0` | variable (`edi`/`r15d`) | ball receive (trap/header family; not decoded) |
| 1 | `0x14096f870` | 0x31 TACKLE / 0xd TACKLE | standing tackle (§2), flagB |
| 2 | `0x140971b60` | 0x30 SLIDING / 0xb or 0xc SLIDING_KEEP | slide tackle (§3), flagA+flagB |
| 3 | `0x1409f8220` | 3 DRIBBLE / 0x5d | INF: keep/receive variant, flagB |
| 4 | `0x140a070e0` | 0x29 BALL_DODGE / 0x15 DODGE | jump out of the ball's path |
| 5 | `0x140a07dd0` | 0x0b GOAL_NET_DODGE / 0x15 | |
| 6 | `0x140a08340` | 0x0c POST_DODGE / 0x15 | |
| 7 | `0x140a089e0` | 0x0e HUMAN_DODGE / 0x15 | |
| 8 | `0x140a08ea0` | (AI unchanged) / 0xe BLOCK | pass/shot block (§5), flagB |
| 9 | `0x140a0b5b0` | 0x5c OVER_TAKEN / 0x14 | |
| 10 | `0x140979920` | 0x15 CLEAR / 8 LONG_PASS | first-time clearance |
| 11 | `0x140a08be0` | 5 PASS_GET / 0x48 JOSTLE | **jostle for an incoming pass** |
| 12 | `0x140a0b890` | 0x6c / 0x28 QUICK_RESTART_READY | restart |
| 13–16 | `0x1409bd910`, `0x1409bda60`, `0x140a0ba50`, `0x140a0bc00` | 0x69/0x6b wall, 0x6e/0x1d seamless throw-in, 0x76–0x78 | set pieces |

m = 1 list (4): 0, 5, 3 (flagA+B), stub `0x141863350` (returns 0). m = 3 list (4): 4, 5, 6, 7 (dodges only).
Command ids: the issuing code writes the immediates (OBS `dscan.py 0x874 0x878`, `w874.txt`).
**Interception of a pass lane is not in this table**: it is movement (`PASS_COURSE_CUT 0x3d`, team level) followed
by entry 0 (receive) when the ball arrives (INF(med); entry 0 not decoded).

## 2. Standing tackle `FUN_14096f870` → `FUN_140970e40` (OBS decompile; constants raw)

`FUN_14096f870` (entry 1): refuse if the analysis tables say the player is far (`MA+0x247be[id] > 2` with distance
`MA+0x24710[...] > 25.0` — INF(med) "nearest-ball rank / distance²"); target `t = FUN_140a50e00(ctx,id)` (the
opponent to challenge: the ball owner when he is an opponent, else a predicted one; uses 12/60 s =
`⌊gs·12/60+0.5⌋` frames of look-ahead); then `FUN_140970e40`. On success: `cmd+0x874 = 0x31`, `+0x878 = 0xd`,
contact point from `FUN_1405bebd0(ctx,id,clip,&p,1,&f)` into `cmd+0x8b0`, `cmd+0xfc4 = 5` if `f` else 0 (tackle
variant; ActionTackle slot 4 `0x140971ab0` writes 2 when the team order kind is 3), `cmd+0xfc8 = 1` (reaction
tackle), `cmd+0xfc9 = 1` when the order kind is 0x36..0x39 (MARK/DELAY_MARK/BLOCK/CONTACT order).

`FUN_140970e40` (all must hold):
1. anime layer can start TACKLE: `FUN_1406b4c10(anime,0xd)` (INF: includes the BW re-tackle cooldown of
   `pes-re-duels-fouls.md` §1.2, which lives in the anime Tackle action).
2. own anime not in the `FUN_140a6e5e0` class and not 0x15 DODGE.
3. if the ball has no current controller event, and the newest event is by `FUN_140a62fe0(ctx,id)`'s player and
   younger than `gs` frames → refuse (INF: no tackle in the first 1 s after that player's touch).
4. **Pad-controlled players only** (`FUN_140479470(G+0x180,id)`, §4) and action ∉ {DELAY, PRESS, SAND}: the
   `tackle.json` user switches (`+0x84 freeMoveTackleMode` = 2, `+0x6c autoSideStepEnable` = 1, `+0x88
   manualSideStepEnable` = 0) and a 20°/15° direction check apply. Dead for CPU.
5. if action ∉ {2 FREE_RUN, 0x11 WAIT_TIMER, 0x33 DELAY, 0x34 PRESS, 0x35 SAND} (mask 0x38000000020004): ball
   predicted `gs>>2` frames ahead (0.25 s) must be within **22.5°** (0x14259d618) of the heading predicted
   `0.6944/gs` s ahead and within **3.0 m** (dist² ≤ 9.0, 0x14259d60c).
6. solver point `p.y` (second float) ≤ **3.0** (0x14258c264) — INF(med): reach distance in m.
7. `FUN_14096fda0` true and `FUN_140971450` false (below).
8. pad-controlled and PRESSing and `FUN_140a8ff10(pos, target, heading, 1.2, 2.0)` → refuse (human only).

`FUN_14096fda0` (geometry; OBS up to the final angle test, return path INF):
- aggressive level `A` = playing style **ENFORCER** (in-match block `+0x48` bit 12, carrier-ai §6.1) + 1 if
  `T+0xa298 == 3` (INF: team defensive mode); `A = 0` if `FUN_140a3b520`/`FUN_1414dab90` team-sheet flag.
- positions predicted `0.05·gs` frames ahead (0x1425a8a28) for both players.
- carrier's clip progress `s = clamp((cur − end)/(0 − end))·90` → `sf = FUN_140a92c80(s,1)` (1.0 if none).
- events: newest event 8 → fail; newest 1–3 or 0x10 by a teammate of the defender → fail.
- **behind test:** carrier's analysis bearing `MA+0x247d4[id]+180` vs defender heading: if > **135°**
  (0x14259be60) the tackle continues only if `|∠(carrier→defender) − carrier heading|` > tolerance
  **11.25° (A=1) / 45° (A=0) / 0° (A=2)** or the angle to the carrier is > **25°** — i.e. CPU defenders without
  ENFORCER do not tackle from up to 45° directly behind (INF(med) on the branch sense; constants OBS 0x1425cc680,
  0x14259be30, 0x14259be20).
- **engage window:** `∠(heading, defender→carrier) ≤ 112.5°` (0x1425cb30c) and next-frame ball distance from the
  carrier ≤ **2.0 m**; lateral offset of the carrier-defender line < **0.8 m** with ball closer to the defender →
  needs `∠ < 90°` and ball speed ≥ **5.0** (FUN_14042d470 units INF km/h) else fail.
- loose ball: fail if the defender's point is farther than the ball path within 1.0 (`FUN_140591a10`).
- **safety (only when action ∉ {PRESS, SAND})** `FUN_140a4fea0` (cover check, 10 m, 60°) and `|line.x − x| < 10`:
  with no cover and the newest event older than `gs/9` frames, fail when `sf < tackle.json +0x78
  check_SaftyRate_No_Cover` (live 2.5) or, on the last line, `sf < +0x74 check_SaftyRate_Last_Line` (live 2.6).
  OBS: `FUN_140a92c80(deg, amp)` = `amp·sin(deg)` (wrap ±360/180, 512-entry quarter-wave table, 90.0, 512.0), so
  `sf = sin(90°·progress) ∈ [0,1]` < 2.5/2.6 → **these vetoes always fire with the live constants**: a covering-less
  or last-line defender who is not in PRESS/SAND never standing-tackles once `gs/9` frames passed since the last
  ball event (INF(med) only on the cover/line predicate meaning of `FUN_140a4fea0`).
- carrier in a ball-touch clip (`FUN_140a6e340`): wait until the touch is `≤ (end − x)` frames away, `x =
  0.075·gs` (A=0) / `0.1·gs` (A=1) / `0.125·gs` (A=2) — **timing window: tackle into the touch**.
- teammates within 100 m: fail if one is already in TACKLE with clip in its first half-to-contact, or in a ball
  action and 1.5× closer (no double tackle).
- anime feasibility `FUN_1406b4580(anime, 0xd, …)`; human: final angle limit 90° (65° above 8 speed units).

`FUN_140971450` (veto, returns 1 = refuse): carrier speed `v·gs·3.6` km/h; if ≥ **3 km/h** (0x14258c264) or the
carrier is turning: refuse when `|∠| < 50°` and defender−carrier distance² gap > `(r + 1.75)²` with `r =
clamp((Δheading − 110)/50)` and the last event older than `trap.json +0x1b8 · gs` (when someone else owns);
else (carrier slow) refuse if the carrier is > **1.0 m** from the contact point. Also refuse when the carrier is
behind the defender (`Δheading > 160°`, direction within 20°, analysis angle table `MA+0x228d4+0x58·id+0x16a8`
within 20° and `FUN_14043b8b0` < 4.0); pass-receive check `FUN_1409f9600` (trap clip 7) when the ball is owned.

## 3. Slide tackle `FUN_140971b60` → `FUN_140971ba0` (OBS)

Gates in order: open play `MI+0x116c == 1`; own anime not `FUN_140a6e4b0` class; **`MI+0x1198` odd**; not
(`MA+0x248f2[id] > 1` and `MA+0x247be[id] > 1`); no newest-4 event of type **0xb** by the target within
`10·gs+0.5` frames (INF: no slide within 10 s of a whistle/foul on him); **capability `FUN_140a604d0(T caps, 8)`**
(carrier-ai §7 row 0x08: 0,0,0,1,1,1,1 → tiers 3–6); **CPU team** (`T+0x255 == 0`); pad slot of the player
(human) not in states 1/2; own anime not in `0x140a6e0d0`/`0x140a6e3c0`/`0x140a6e5e0` classes.
Contact point `p` = `FUN_1405bebd0` with the SLIDING clip (`FUN_14096f930(pc,0xb)`); `d = |p − pos|`,
`φ = bearing(pos→p)`.

Aggression `L` (0..3): ENFORCER (+1) · `FUN_140a62180(ctx,side,6) == 0` (team-sheet kind 6 via `FUN_1408c5030`,
INF: an instruction) (+1) · `T+0xa298 == 3` (+2) · plan flag `FUN_140a63950(ctx,side,0xe,markTarget)` (+1) ·
`T+0xa53c == 1` (+1) · **stamina % `FUN_140a58550(ai) < 30`** (+1; OBS: `cur·100/max`, FNV-checked) ·
`T+0xb8a2` (+1); cap 3; 0 by the team-sheet flag. Inside the own penalty area (`FUN_140a4c7a0`): refuse if
`L < 3`, else `L = 2` if `T+0xa298 == 3` else 0. Pad-controlled: `L = 2` (human).

Then refuse when: own anime `FUN_140a6e300` class or ball height 10 frames ahead > **0.5 m**; `d ≥ 4.0 + 0.8·L`
(0x1425656c0, 0x142572348); `|Δ(predicted heading, φ+90)| < 11 + 11·clamp((3−L)/3)` (0x14259bdfc; INF: slide
must be lateral enough); `∠(φ, heading) ≥ 90°`; if the player is > 1.0 from `ai+0x12b0` and predicted-heading
error > 90°; `∠(direction to the carrier/target, heading) ≥ 112.5°`; any teammate (distance-sorted table
`MA+0x22e4c+200·id`, ≤ 100 m) in SLIDING or SLIDING_KEEP; for each on-pitch opponent ≤ 100 m: (`L < 3` and within
**2.25 m** of `p` and ≤ 60° off) or his body intersects the slide capsule (`FUN_140a8ff10`, width
`1.5 − 0.2·L`, length `0.6·d`, radius `0.5·d + 0.5`); `FUN_140a539d0(ctx,id,1.0,0)`; `FUN_140a53690` with the
slide clip; another player of the team reaches `p` first (`FUN_140a4ee40`); when the ball is loose and the
opponent's team leader `T'+0x78` is the nearest: he must be ≤ **2.0 m** (4.0 = d²) from the defender. Final:
`FUN_140973000` (loose ball) or `FUN_140972870` (owner is an opponent) and anime feasibility (clip 0xb, or 0xc
SLIDING_KEEP when `FUN_140973460` says the ball can be kept).

Reach and timing during the slide are the anime layer's (`pes-re-duels-fouls.md` §1.2: BW facing gate 67.5°,
Aggression reach `1.5 + 3·clamp((Agg−60)/35)` m).

## 4. `ai(B)+0x134` (OBS writer, OBS live)

`FUN_14092d580` (per-player publisher, called by `FUN_14092f320`, copies AI state into `ai(i)`): when the anime
action `hu(i)+1 ∈ {0xb SLIDING, 0xc SLIDING_KEEP, 0xd TACKLE}`, it scans the **pad table** `*(G+0x180)` (24 ×
0x74 B; `+0x20` controlled player id, `+0xd` disabled; same table as `FUN_140479470`) for the player:
found and enabled → `0x134 = 1` iff `ai+0x2c` (requested anime, from `+0xad3c`) equals the running anime; not
found → `0x134 = 0`; not in a tackle/slide → `0x134 = 0` (raw @0x14092da30..0x14092db63, `FUN_140a596f0` =
`mov [rcx+0x134],dl`). Only writer: `dscan.py 0x134` (514 stores, the only `ai`-range ones are this and the
reset `FUN_140a58930`; the copier `FUN_140431800` mirrors it).
**Meaning: "the user pressed for this challenge".** The Judge (duels-fouls §2.3/§2.5) therefore treats a
*human* tackle on a DOGSO chance as +30 and enforces the "professional foul ≥ yellow" floor only for humans.
Live (OBS, OPEN_PLAY, COM vs COM): all 24 pad slots have `+0x20 = 255` (slots 0/1 bound to sides 0/1, no player)
→ `0x134` is 0 for every player (`pads.py`).
Related (OBS same function): `ai+0x1474/+0x1478/+0x12a0` = frame/target/approach angle when the defender's
contact candidate `pl+0x610` is an opponent within `2·(0.23 − r) < pl+0x63c` (`FUN_140a6dd30`); `+0x148c` set when
he was the ball's last player and the ball moved > 2.22 m (event of this frame); `+0x14a0` = the opponent he is
"forcing" (within 160° and 1.5 m `FUN_140a4ff30`).

## 5. Block `FUN_140a08ea0` (OBS partial) — `block.json` (registry 0x27)

Pre-gate `FUN_140a0b350`. Opponent owner `o = FUN_140a4d970`: block radius **3.0 m** if the defender is in PRESS
(`pl+0x5d0 == 0x34`) or pad-controlled, else **1.3 m**; owner in LONG_PASS (8) / SHOOT (10, radius ×1.3): when the
kick clip passes **0.7** (0.8 long pass; ×0.5 when the ball is > 0.5 m high at contact) of its contact frame:
DELAY/PRESS/PASS_GET defenders get radius **7.0 m**; others only for a shot within 33.75° whose path capsule
(1.5 m, 7 m) covers them. FEINT sub-class 0xc0000 ×1.2. Refuse beyond the radius, flag bit 9, busy classes,
short pass early in its clip (`< 0.8·contact`), dribble-touch facing > 90° / 67.5°. Loose ball: kicked ball
speed `·3.6` > **70 km/h** (55 for GOAL_COVER after event 5) → window `gs·speedBall` (live **2.0 s**), else
`gs·normal` (live **0.9 s**), measured against `FUN_140a51320` frames. Fields (deserializer `0x141e77800`, live):
`exec` 1, `checkArea` 0, `checkDist` 0.5, `checkDistShoot` 0.9, `enable` + per-kick bytes
centering/longPass/shoot/shortPass/shortPassForecast = 1, `normal` 0.9, `speedBall` 2.0.

## 6. Not present / killed hypotheses

- **Shirt pull / push:** AI ids `PUSH 126`, `PULL 128` exist (table tail) but no reaction entry and no
  `cmd+0x874` immediate write of 0x7e/0x80 in `.trace` (OBS `w874.txt`). INF(med): human/demo only.
- **CONTACT 0x39 / JOSTLE 0xf as a defender choice:** no `cmd+0x874` immediate writer; jostle (anime 0x48) is
  issued only by entry 11 (pass reception duel) and by the anime/collision layer. KILLED: "the AI chooses
  shoulder challenges".
- **`ai+0x134` = CPU professional-foul intent:** KILLED (§4).
- **A think-unit list mirroring the carrier:** KILLED — the defender uses one ordered predicate table, no
  units, no answer latency, no difficulty reaction delay (the team-level reaction delay `FUN_1405027a0`,
  duels-fouls §1.1, applies to state changes).
- **Tackle choice uses Defensive Awareness / Tackling stats:** not in the chooser. Stats enter only via the
  anime layer (BW cooldown and angle, Aggression slide reach; duels-fouls §1.2) and via playing style ENFORCER
  (block `+0x48` bit 12) here.

## 7. Constants (live `tackle.json`, registry 0x3c; deserializer `0x141e66610`)

Live winner = `common/match/constant/constant_player.bin` from the `4cc_06_gameplay.cpk` download (a gameplay
mod; the stock file is in `Data/dt18`). Values below are the live object (`consts.py`, read-only); struct offset
= deserializer store offset. pressTackleEnableAngle Idle 125 / Lv1 147.5 / Lv2 167.75, Move 99 / 122 / 142
(+0x40..+0x54, read by the anime layer, not decoded here); `check_SaftyRate_Delay` 2.5 (+0x70),
`check_SaftyRate_Last_Line` 2.6 (+0x74), `check_SaftyRate_No_Cover` 2.5 (+0x78), `failAfterStopTime` 4.0,
`forecastHitRate` 4.5, `freeMoveTackleMode` 2 (+0x84), `manualSideStepEnable` 0 (+0x88), `reactionStartFrame` 0,
`saftyRate_FarDist` 8.0, `saftyRate_NearDist` 3.0, `tackleThinkDisableToTrap` 0.95, `autoSideStepEnable` 1 (+0x6c).
`sliding.json` (0x3b): anime fields only (`animeAccelLimitSpeed` 4.0, `animeAdjustLimitAngle` 1.0,
`animeEnterSpeed` 13.0, playback 2.2/0.6) — the slide decision constants are immediates. `pes15Test.json`
(0xf): `enable` 1, `pressCheckStepMoveDistance` 2.0 (live; code default 5.0) used by ActionPress slot 22.

## 8. Difficulty / COM level

- Slide: team capability 0x08 → **no AI slide tackles below level 300**.
- Standing tackle, block, dodges: no capability read in the predicates (OBS). Difficulty reaches them only
  through the team-level reaction delay and pressing (other reports).

## 9. GF replacement map

| finding | replaces in GF |
|---|---|
| §1 ordered reaction table, first-true wins, every frame | `elizacontroller.cpp` defending branch's ad-hoc tackle/intercept selection |
| §2 standing-tackle gates (3 m/22.5° outside press, 112.5°/2 m engage, behind tolerance by ENFORCER, last-line/no-cover veto, touch timing) | `elizacontroller.cpp` tackle trigger distance/angle; `AIsupport/` tackle helpers |
| §3 slide gates (odd frames, level ≥ 300, `4 + 0.8L` m, stamina < 30 % aggression, no double slide, own-box rule) | GF sliding decision in `elizacontroller.cpp` |
| §4 `ai+0x134` = human challenge | `pesjudge.cpp` intent input: always 0 for AI-vs-AI |
| §5 block radii/windows | GF has no block decision (new) |
| §6 no shirt pull, no AI jostle choice | remove any GF CPU "shoulder/jostle" decision; jostle comes from collision |

## 10. Live check (read-only, `live_tk.py` / `ana.py`, `pads.py`, `consts.py`)

- Sample `tk1.jsonl`: frames 16231–18051 contiguous (every logic frame), 1,144 in open play, 677 stopped, then
  half-time; 22 players × anime id, clip frames, `pl+0x5d0`, pos/heading/vel, `ai+0x134/+0x1474/+0x1478/+0x12a0`.
- OBS: `ai+0x134 == 0` in all 25,168 player-frames; pad slots all unbound (`+0x20 = 255`) → §4 confirmed.
- OBS: **no TACKLE/SLIDING/SLIDING_KEEP start in the 1,144 open-play frames** (21 s of play). The tackle-vs-jockey
  distance rule is therefore **not live-confirmed**. Only 4 defender-frames within 5 m of a ball owner were seen
  (one in DELAY 0x33 at 3–4 m, anime STEP_MOVE).
- Second sample (`tk2.jsonl`, 900 s) got 1 frame: the match sits at the half-time screen (`MI+0x1198` frozen at
  18051, `pes_state.py` = MATCH_FROZEN). It cannot be resumed without input, which is not allowed.
- Note from DefensiveShape2RE: the engaging defender is usually in **MATCH_UP 0x3a** (writer `FUN_140536210`, the
  first defender whose mark target is the carrier). 0x3a is **not** in the tackle mask {2, 0x11, 0x33, 0x34, 0x35}
  (§2 step 5) nor in {PRESS, SAND} (safety veto), so a MATCH_UP defender tackles only with the ball ≤ 3 m and
  ≤ 22.5° off his heading 0.25 s ahead, and is subject to the last-line/no-cover veto. INF(high) from the masks.

## 11. Open

1. **Live tackle/slide distances.** Next: when the match resumes, `sudo python3 live_tk.py tkN.jsonl 900` then
   `python3 ana.py tkN.jsonl`; check every tackle start has `pl+0x5d0` ∈ {0x33,0x34,0x35} or ball ≤ 3 m/22.5°,
   slides start only on odd `f`, never for a team below level 300, and `|p − pos| < 4 + 0.8L`.
2. **`FUN_14096fda0` final return path** (decompile shows `void`; the true/false split after 0x1409706c2 and the
   behind-tolerance branch sense). Next: `pdis.py 0x14096fda0 900`, follow the `al` set before 0x1409706c2.
3. **`FUN_1405bebd0`** (contact-point solver; meaning of `p.y ≤ 3.0`) and **`FUN_140972870` / `FUN_140973000`**
   (slide vs carrier / loose ball). Next: `dec.sh` those three.
4. ~~`sf` range~~ — closed (§2: `amp·sin`, ≤ 1).
5. **Team terms in L/A:** `T+0xa298` (== 3), `T+0xa53c`, `T+0xb8a2`, team-sheet kind 6 (`FUN_1408c5030`), flag
   `FUN_140a3b520`/`FUN_1414dab90`. Next: dscan writers of `0xa298`/`0xb8a2`; ask DefensiveShape2RE (team level).
6. **Entry 0 `FUN_140933ad0`** (receive/intercept command) and entry 3 `FUN_1409f8220`: decompile to close
   "interception" (§1). Pass-lane positioning itself is team-level (`PASS_COURSE_CUT`).
7. **Stock vs mod constants:** live `tackle.json` comes from `4cc_06_gameplay.cpk`; compare with the stock
   `constant_player.bin` in `Data/dt18` (`cpkls.py` extracts cpk members; the file is a `{nameOff, dataOff, size}`
   directory, struct packing differs from the live layout — map through the deserializer offsets).

## 12. Live check + solver/slide-helper decode + stock-vs-mod constants (DefenderLiveRE)

### 12.1 Live sampler, second half open play (OBS)
`sudo python3 live_tk.py tkN.jsonl 900` in the DefenderLiveRE scratch dir, then `ana.py` / `ana2.py` / `ana3.py`
(same reader). Sample: **13,247 logic frames, every frame contiguous** (frame-gap histogram: 1 for all 13,245
consecutive pairs), 8,233 in open play (`ph == 1`, ≈ 152 s), 4,019 `ph == 2`, 994 `ph == 3`, 1 `ph == 0`; frame
counter 29033 → restarts at 0 at the end. Combined with the earlier sample: **9,377 open-play frames (≈ 173 s)**.
OBS: `ai+0x134 == 0` in all 181,126 player-frames (pad slots unbound) — §4 holds again.

Challenge/block anime starts in open play (`hu+1 ∈ {0xb,0xc,0xd,0xe}`, contiguous detection):
**1 standing tackle, 1 slide, 10 blocks, 0 SLIDING_KEEP (0xc)**.

**Standing tackle** f=32012, CPU, `pl+0x5d0`: WAIT_TIMER 0x11 → 0x31 TACKLE, anime 0xd clip cur 0 / end 11:

| gate (decoded) | measured | limit | agree |
|---|---|---|---|
| action ∉ {2,0x11,0x33,0x34,0x35} → strict branch | WAIT_TIMER 0x11 → branch taken | — | ✓ |
| ball ≤ 0.25 s (13 f) ahead ≤ 3.0 m | 2.31 m (f+1: 1.76 m) | 3.0 | ✓ |
| off predicted heading ≤ 22.5° | 3.0° (f+1: 14.6°) | 22.5 | ✓ |
| engage ∠(heading, def→carrier) ≤ 112.5° | 42.2° | 112.5 | ✓ |
| predicted gap ≤ 2.0 m | 1.17 m | 2.0 | ✓ |
| from-behind (>135°) tolerance | carrier-facing ∠ 114.2° | ≤ 135 | ✓ not taken |
| into the carrier's touch | carrier in a 14-f dribble-touch clip (cur 10/end 25 = 15 f to contact) | — | ✓ |
| never as last man / no-cover veto, A level, BW cooldown, double-tackle, `p.y ≤ 3.0` | — | — | not in the sample (needs MA/team/registry state) |

**Slide** f=32896, CPU, PRESS 0x34 (replaced a 0xe BLOCK anime that had run 32894–32895), anime 0xb clip cur 0 /
end 22:

| gate (decoded) | measured | limit | agree |
|---|---|---|---|
| open play | ph == 1 | — | ✓ |
| ball height ≤ 0.5 m (10 f ahead) | 0.11 m (f+10: 0.20 m) | 0.5 | ✓ |
| \|p − pos\| < 4.0 + 0.8·L | ball 1.42 m (L ≥ 0) | ≥ 4.0 | ✓ |
| no teammate already sliding | none of the 10 teammates in 0xb/0xc | — | ✓ |
| slide only on odd logic frames | anime starts f=32896 (**even**) | odd | consistent-with-lag: the command issued on odd 32895 lands in the anime one frame later (INF(high)); the block anime it replaced ran 32894–32895 |
| capability 0x08 (level ≥ 300), L terms, own-box rule | — | — | not in the sample |

**Blocks (10):** blocker action, ball distance, nearest-opponent ("owner") distance, ball height/speed:

| f | action | ball d | owner d | owner clip | ball h | ball speed | verdict vs decoded radii (1.3 m / 3.0 m PRESS) |
|---|---|---|---|---|---|---|---|
| 29400 | PRESS | 1.92 | 1.52 | touch 2/14 | 0.11 | 4 km/h | ✓ ≤ 3.0 |
| 32723 | PRESS | 3.34 | 3.27 | touch 2/14 | 0.11 | 3 km/h | ✗ 3.27 > 3.0 — open |
| 32894 | PRESS | 1.51 | 1.70 | touch 2/14 | 0.11 | 3 km/h | ✓ ≤ 3.0 |
| 32910 | MARK | 6.81 | 10.61 | PASS_GET | 0.75 | 97 km/h | ✓ loose ball ≥ 70 km/h → 2.0 s speedBall window |
| 36011 | WAIT_TIMER | 5.60 | 8.21 | PASS_GET | 0.73 | 82 km/h | ✓ loose ball ≥ 70 km/h → 2.0 s window |
| 36428 | PRESS | 2.63 | 1.48 | touch 5/25 | 0.11 | 5 km/h | ✓ ≤ 3.0 |
| 38879 | WAIT_TIMER | 4.77 | 1.18 | touch 0/14 | 0.32 | 43 km/h | ✓ ≤ 1.3 |
| 39427 | PRESS | 2.59 | 2.26 | touch 2/14 | 0.11 | 42 km/h | ✓ ≤ 3.0 |
| 40269 | PRESS | 3.14 | 2.67 | touch 5/25 | 0.11 | 2 km/h | ✓ ≤ 3.0 |
| 40435 | DELAY | 2.27 | 1.74 | touch 2/14 | 0.34 | 51 km/h | ✗ 1.74 > 1.3 — open |

**Agreement: tackles 1/1, slides 1/1, blocks 8/10** (6 plain-radius, 2 loose-ball speedBall; f=32723 and f=40435
sit 0.27 m / 0.44 m outside the decoded owner radius with the owner in a 14-frame touch clip 12 frames from
contact — INF(med) a solver/predicted-point measurement, open). No tackle-as-last-man violation is observable
here; the standing tackle was by a WAIT_TIMER defender, i.e. the strict branch of §2 step 5.

### 12.2 `FUN_14096fda0` (tackle geometry) — full body, return path, behind-tolerance direction (OBS raw)
`pdis.py 0x14096fda0` stops at the **first `ret` (0x1409706f5)** and hides the tail; the real function runs
0x14096fda0–0x140970e32 (0x1409706f6 onwards, verified with a no-break disassembly). The previous
"void return / split after 0x1409706c2" was that truncation.

- **Return path:** true path `mov al,1` @0x140970e2c → `jmp 0x14097067b`; **every veto jumps to 0x140970679
  `xor al,al`** → the same epilogue (`0x14097068..` register restores → stack-cookie check → `ret` @0x1409706f5
  is inside the *epilogue* of the longer body). So the function returns bool exactly as the caller
  `FUN_140970e40` (`test al,al` @0x14097139a) expects.
- **Touch-timing gate** (0x1409709d5): `FUN_140a6e340(carrier anime)`; with `end ≠ 0` refuse when
  `(end − cur) < (FUN_1406b3d60(anime, 0xd, dist-class) − x)` where `x = 0.075·gs` (A=0, 0x14259da90) /
  `0.1·gs` (A=1, 0x142588200) / `0.125·gs` (A=2, 0x14259bdcc), dist-class = 2 when the carrier is > 1.5 m
  (0x14259bddc) from the contact point else 0/1. (Report §2 confirmed.)
- **No double tackle** (0x140970ad1..0x140970c4d): the 11 teammates via `MA+0x22e4c + 200·def_id`, skip if the
  entry distance > 100.0 (0x14253ee30): a teammate in TACKLE (anime 0xd) with `end/2 ≤ carrier clip cur` →
  refuse; a teammate in a ball-action class (`FUN_140a6e5e0`) whose distance to the point is 1.5× (0x14259bddc)
  farther → refuse.
- **Safety vetoes, raw** (0x140970869..0x1409709d5): applied only when `action − 0x34 > 1`, i.e. action ∉
  {PRESS 0x34, SAND 0x35}; cover check `FUN_140a4fea0(ctx, id, &pos, 10.0, 60.0)` (0x142588210 / 0x14259a764);
  with no cover and `FUN_140a4f1e0 == 2` and frames-since-event > `gs/9` (0x38e38e39/2 division trick) and
  `tackle.json +0x78` (live 2.5) > sf → refuse; and `|MA + 20·id + 0x40 − defender x| < 10` with no cover and an
  old event and `tackle.json +0x74` (live 2.6) > sf → refuse. (Matches §2.)
- **Output struct** (0x140970c53..): {point, target id, variant, `+0xa0 = 1`} with the variant forced to 5 when
  the defender's action is FREE_RUN 2, or WAIT_TIMER 0x11 and pad-controlled; `+0xa1 = 1` when the team order kind
  ∈ 0x36..0x39.
- **Anime feasibility** `FUN_1406b4580(anime, 0xd, …)` (0x140970d51): false → refuse.
- **Human-only final angle** (0x140970d80..0x140970e26): pad-controlled and action ≠ PRESS: predicted heading
  (0.6944/gs s ahead, 0x14259bdd0) vs the ball bearing must be ≤ `[rsp+0x48]` = **90°** (0x14259be5c), or **65°**
  (0x14259be48) once the speed (`FUN_14042d470`) ≥ 8.0 (0x14259bdf4).
- **From-behind branch — direction now OBS** (0x140970734..0x140970863): guard `FUN_140a6e3f0(carrier anime)`
  (else the branch is skipped); `∠ = FUN_140481b00(carrier heading, bearing(carrier→defender))`; **if 135 < ∠**
  (0x14259be60) the tackle is refused unless either (a) `|bearing(carrier→defender) − carrier heading| ≥ tol`
  or (b) the same difference (re-wrapped) is > **25.0** (0x14259be20). `tol = 45°` (A=0, 0x14259be30),
  **11.25°** (A=1, 0x1425cc680), **0°** (A=2, register zeroed). So the branch **refuses** when the defender is
  directly behind AND within the tolerance — the report's INF(med) sense is confirmed, and ENFORCER+team-mode-3
  (A=2) means "never tackle from behind".
- The 135° test at 0x140970294–0x140970349 is a **different** gate: `|defender heading − wrap360(carrier analysis
  bearing MA+0x247d4[id] + 180)| ≤ 135` (fail above) — not the from-behind tolerance.

### 12.3 `FUN_1405bebd0` (contact-point solver) — meaning of `p.y ≤ 3.0` (OBS)
`FUN_1405bebd0(ctx, id, clip, float *p, char f_in, char *p_real)`:
- resolves the target: mode `m = FUN_140a627e0(ctx,0) < 0x16` → `(10 < id ? id : id)` team-side test, else
  `FUN_140a50e00(ctx,id)` (the opponent to challenge / ball owner); 0xff = invalid → the table fallback.
- with a valid target the point starts as `FUN_140a6cf70(AC, clip+1)` — the clip's **contact point**, a world
  position from the global anime-context table; if the target is in a touch-clip class
  (`FUN_140a6e150`/`FUN_140a6e5e0`) and `end ≠ 0`, with `iVar12 = end − cur` frames to contact and
  `iVar12 ≥ 1 && clip ≥ iVar12`: a 22.5° (0x14259d618) ball-velocity-bearing test; if it fails, the
  `tackle.json +0x80` ratio check (`(cur/end ≤ forecastHitRate, live 4.5) && !bVar11`) or `f_in == 0` keeps the
  table point with `*p_real = 0`, otherwise the point becomes `FUN_140a6cf70(AC, iVar12+1)` — the contact point of
  the **specific frame** — and `*p_real = 1`. `FUN_140a92d40(p − ball)` emits the bearing helper. Anime 5 has a
  35° (0x14259a760) variant of the bearing test.
- **`p.y ≤ 3.0` measures the contact point's HEIGHT**: the gate is in the caller `FUN_140970e40` @0x14097136f
  (`comiss xmm0, [0x14258c264]` on `[rbp-0x55]`, the **second float** of the solver's out point at `rbp-0x59`)
  → refuse when the ball-contact point is more than 3.0 m above the ground. The earlier INF(med) "reach distance"
  reading is dead. (The record layout is (x, height, z): ball/player heights are the middle float.)

### 12.4 `FUN_140972870` / `FUN_140973000` (slide follow-ups) (OBS decompile + constants)
**`FUN_140972870(pc, p, target, L)`** — slide against a carrier:
- veto when `*(G+0x2aa8)[team] / 3.0` (0x14258c264) `< (float)(int)aggr_byte · p.x` (the team-sheet flag term);
- reaction delay: `FUN_1408caf60(&stat, player, 0x15)` + the stat at +0x15 → refuse while
  `MI.frame − event.frame < gs·0.3 (0x142590d98) + gs·ratio·(scale 1.0, 0x14256ae1c)` — the per-player reaction
  time in the slide predicate;
- carrier in a touch clip: refuse when `end·0.7 (0x14257a194) < cur` and the angle ≥ 45° (0x14259be30);
  refuse on `FUN_140a6e3c0` / anime 0x57;
- an angle test whose limit is **L-dependent: 90° (L=0, 0x14259be5c) / 105° (L=1, 0x1425c48d0) / 120° (L=2,
  0x14256ae30) / 135° (L≥3, 0x14259be60)** — operands INF;
- the slide capsule: `1.7 − 0.2·L` (0x14259da9c, 0x142598578) reach, then an inner threshold
  **1.4 (L=0) / 1.2 (L=1) / 1.1 (L=2) / 0.9 (L=3)** (0x14259d5d8 / 0x14259d5d4 / 0x1425a3590 / 0x14258c260),
  a 135° bearing test with the perpendicular-distance check (`FUN_140581ce0`/`FUN_140591a10`), and the
  "team-mate reaches the point" test `FUN_140a4f160(ctx, …, p, d, 60.0, 10 + 10·L)` (0x142588210) plus the
  z-order comparison.

**`FUN_140973000(pc, p, L)`** — slide on a loose ball:
- refuse unless `FUN_1415a1780(table value at `(byte[`FUN_140a714a0`+0xb2+id]+team)·8) > 18` (0x14259be18);
- `FUN_140a4ee40(ctx, side, p, 0)` (who reaches the point) and the distance to it ≤ 1.5 (0x14259bddc);
- `p[1] ≤ 0.6` (0x14257a190), target = `FUN_140a50e00`;
- refuse when `(float)(int)aggr_byte · p.x > 0` (slide direction) or the target anime is in `FUN_140a6e5e0` (L < 2)
  or `L < 1`;
- ball speed `·3.6 (0x14259ce38) ≥ 35 km/h` (0x14259a760) or the `1.5 + 0.5·clamp(L/3)` cap (0x142565600);
  then a 60° (0x14259a764) bearing test, the anime class checks, a distance-difference test ≤ `L·0.5 + 1`
  (0x14256ae1c), and for L < 3 a 120° (0x14256ae30) angle limit.

### 12.5 Stock vs mod constants (OBS) — §11.7 closed
Method: the two `constant_player.bin` files (stock `dt18_all.cpk` member, live gameplay cpk member) and the two
`constant_match.bin` files are both `WESYS` containers — stock tag `\xff\x10Q`, mod tag `\x00\x10\x01`, zlib
deflate, inflated `u32 count | table | names | payloads | "v4.0"` — decoded with the org's
`tools/pes21_import/wesys_constant.py`. The mod's decoded objects are byte-identical to the live registry
objects currently loaded in the process (checked field-by-field), so the live values in §5/§7 are the mod's.
Values below are **stock → mod(live)**.

**tackle.json (0x3c, 176 B):**
`check_SaftyRate_Delay` 0.4 → 2.5 · `check_SaftyRate_Last_Line` 0.5 → 2.6 · `check_SaftyRate_No_Cover` 0.4 → 2.5
· `failAfterStopTime` 0.5 → 4.0 · `forecastHitRate` 0.7 → 4.5 · `saftyRate_FarDist` 1.5 → 8.0 ·
`saftyRate_NearDist` 0.2 → 3.0 · `tackleThinkDisableToTrap` 1.75 → 0.95 · `accelBackLimitSpeed` 1 → 3 ·
`accelLimitSpeedIdle` 4 → 6 · `adjustLimitAngleIdle` 22.5 → 62.5 · `adjustLimitAngleMove` 11.25 → 42.5 ·
`pressTackleEnableAngle` Idle 135 → 125, IdleAggressiveLv1 157.5 → 147.5, IdleAggressiveLv2 168.75 → 167.75,
Move 105 → 99, MoveAggressiveLv1 135 → 122, MoveAggressiveLv2 157.5 → 142 · `playbackSpeedHighest` 1.2 → 1.84 ·
`playbackSpeedLowest` 0.9 → 0.55 · `shoulderInterruptSlideLimit` 0.1 → 0.5 · `shoulderSlideLimit` 0.75 → 0.45 ·
plus three boolean/dword fields 0→1, 0→1, 1→0 (candidates `autoSideStepEnable`, `stepMoveAutoTackleFarEnable`,
`reactionStartFrame` — INF(med), the payload packs the byte fields).
Unchanged: `accelLimitSpeedMove` 0, the four `autoShoulder*` bytes 1, `ballVisibleCheck` 1, `enterSpeed` 14,
`farCutFrame` 0, `holdEnable` 1, `midCutFrame` 2, `nearCutFrame` 4, `shoulderCutFrame` 0, `shoulderOnly` 0,
`freeMoveTackleMode` 2, `manualSideStepEnable` 0.

**sliding.json (0x3b, 32 B):** all five anime fields changed — `animeAccelLimitSpeed` 2 → 4,
`animeAdjustLimitAngle` 0.75 → 1, `animeEnterSpeed` 14 → 13, `playbackSpeedHighest` 1.2 → 2.2,
`playbackSpeedLowest` 0.9 → 0.6.

**block.json (0x27, 80 B):** `checkDist` 1.0 → 0.5 · `checkDistShoot` 3.0 → 0.9 · `normal` 0.2 → 0.9 ·
`speedBall` 1.0 → 2.0 · one of the per-kick byte flags 0 → 1 (the packed group `01 01 01 00` → `01 01 01 01`;
which of centering/longPass/shoot/shortPass is INF). Unchanged: `exec` 1, `checkArea` 0, `enable` 1, the other
per-kick flags.

**reaction.json (0x39, 16 B):** `thinkWaitTimer` 1 → 0 — with 0 the `flagA` skip on `cmd+0x874 == 0x11
WAIT_TIMER` in the chooser (§1) never fires for a WAIT_TIMER player.

**pes15Test.json (0xf, constant_match.bin, 128 B):** exactly one value changed, 0.3 → 0.3051 (field unnamed);
`enable` 1 and `pressCheckStepMoveDistance` 2.0 are **stock values too** (the "code default 5.0" is the code's
fallback when the key is absent, not a stock value).

Mod objects in `constant_player.bin` that also differ but are outside this report's scope: `avoid`, `centering`,
`contact`, `dribble`, `press`, `shoot`, `ballplayer*` (and in `constant_match.bin`: `ball`, `cpuLevel`, `cursor`,
`pesSmart`, `animeAging*`, `setplayGuideGoalKick`).

