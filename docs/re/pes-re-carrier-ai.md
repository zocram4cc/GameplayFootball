# PES 2021 ball-carrier AI (`match::ai::bp::BallPlayer`) — what the carrier decides and why

Agent: CarrierAIRE. Read-only RE of `pesmem.bin` (mid-match image, base 0x140000000) with
capstone (raw bytes) + GhidraMCP decompiles (hypothesis only, every formula below re-read
from the disassembly unless tagged otherwise). Scratch: `~/ghidra-projects/scratch/CarrierAIRE/`
(decompiles `*.c`). Tags: **OBS** (observed, address given), **INF** (inferred, confidence given).
Angles are **degrees** throughout (OBS: the wrap helpers `0x140481b00`/`0x14043a020`/`0x1404583b0`
wrap with 360.0 @0x14259858c and fold at 180.0 @0x142598580). Positions are Fox/PES metres,
pitch frame of `pes-re-matchlogic.md` §6 (x = length axis ±52.56, z = width axis ±34.06).

## 0. Corrections to earlier reports (read first)

| earlier claim | correction | evidence |
|---|---|---|
| player-ai §2.3 "default (normal): units 2, 3, 0x10 … plus 4" | default appends **2, 3, 0x0A (PassOneTwo)**, then 4 unless image-unit state = 4. Kind 0x10 (Dribble) is **never** in the list: it is the always-computed fallback answer | OBS `0x1421112c0` decompile + disasm |
| player-ai §2.4 "deferred branch: `gameSpeed*0.5 + DAT_142588200 (≈0.5)`" | the literal is `(int)(gameSpeed * 0.1 + 0.5)` = **5 frames**; 0.1 @0x142588200, 0.5 @0x142565600 | OBS `0x142110fab..0x142110fc7` (`mulss [0x142588200]; addss [0x142565600]; cvttss2si; cmp ebx,eax; jbe`) |
| team-plans §5.1/§5.2 "`player+0x55c/0x560/0x564` = base values (position anchors), `+0x574..` = scale values, score = (frames×scale+base)×mult" | `+0x55c/+0x560/+0x564` is the player's **position (x,y,z)** and `+0x574/+0x578/+0x57c` his **velocity**; `frames×scale+base` is **linear position prediction** `pos + vel·t`. It is not an ability term. | OBS `0x140a4c820`: `|*(+0x564)| ≤ pitch[7]` (PA half-width 20.12) and `pitch[6] ≤ |*(+0x55c)| ≤ pitch[0]` (PA line 36.14 … half length 52.56) → "player inside the opponent penalty area" |
| team-plans §5.5: `0x14063e960` tests "`player+0x48` = u64 skill bitmask" | the address it reads is the **0x128-B in-match block +0x40 (u64 skill mask, engine bit order)** of matchlogic §1.3: `0x140a3af90` returns `roster + idx*0x128 + 0xA8`, `0x141b59e20` adds 0x30, the getter adds 0x48 → `roster + idx*0x128 + 0x120` = block(+0xE0) + 0x40. Team-plans' "+0x48" is relative to the `0x141b59e20` sub-object, not the block. The same mask is read a second way through the live-stat copy (`0x140a489f0(i)+0x70` = block bytes, `+0x40` u32) in the receiver scorer (§4.3). | OBS `0x14063e960`, `0x140a3af90`, `0x141b59e20`, `0x1421174f0` |
| player-ai §2.4 "adopt = first kind whose gates pass" | adoption is a **priority walk with veto/override rules** (§3), not a score max. Units never emit a comparable float score; each unit emits a 28-byte *answer* or nothing. | OBS `0x14210f110` |

## 1. Object map and the 25 think-unit kinds (OBS)

Kind → embedded object → RTTI class → `evaluate` (vtable slot 2). Resolved from the BallPlayer
constructor `0x14210d320` (it stores each vtable at the object offset) and the kind switch
`0x1421105b0` (jump table @0x14211069c).

| kind | obj | class (`match::ai::bp::`) | evaluate | in outfield list? |
|---|---|---|---|---|
| 0 | +0x3678 | ThinkUnitShoot | 0x142121920 | always (3rd) |
| 1 | +0x36C0 | ThinkUnitClear | 0x142109eb0 | always (4th) |
| 2 | +0x3700 | ThinkUnitPassGoalFront | 0x14212aba0 | plan-dependent |
| 3 | +0x3740 | ThinkUnitPassBackSpace | 0x142129110 | plan-dependent |
| 4 | +0x3780 | ThinkUnitPassForward | 0x14212a700 | plan-dependent |
| 5 | +0x37C8 | ThinkUnitPassSideChange | 0x14212b8b0 | plan 7 only |
| 6 | +0x3808 | ThinkUnitPassSafety | 0x14212b520 | conditional tail |
| 7 | +0x3848 | ThinkUnitPassCross | 0x142129590 | plan 1–3 / 5 / tail |
| 8 | +0x3888 | ThinkUnitPassEarlyCross | 0x14212a2d0 | plan 5 |
| 9 | +0x38C8 | ThinkUnitPassLong | 0x14212ad40 | plan 6 / tail |
| 10 | +0x3918 | ThinkUnitPassOneTwo | 0x14212aeb0 | default plan, plan 1–4 |
| 11 | +0x3958 | ThinkUnitPassDirectHighBall | 0x142129be0 | always (7th) |
| 12 | +0x3998 | ThinkUnitPassSpecial | 0x14212b9c0 | always (8th) |
| 13 | +0x39E0 | ThinkUnitPassTarget | 0x14212b9e0 | plan 9 |
| 14 | +0x3A20 | ThinkUnitPassRespondRequest | 0x14212b230 | always (6th) |
| 15 | +0x3A60 | ThinkUnitPassRandomTest | 0x14212afd0 | never (debug) |
| 16 | +0x33A0 | ThinkUnitDribble | 0x14212fe70 | **never — fallback answer** |
| 17 | +0x3AC8 | ThinkUnitFeint | 0x14212d910 | always (5th) |
| 18 | +0x3D00 | ThinkUnitGKClear | 0x142132b40 | GK list |
| 19 | +0x3B30 | ThinkUnitGKDribble | 0x142132e20 | GK fallback |
| 20 | +0x3C70 | ThinkUnitGKPassShort | 0x1421333f0 | GK list |
| 21 | +0x3CB8 | ThinkUnitGKPassLong | 0x142133120 | GK list |
| 22 | +0x3D40 | ThinkUnitGKCoaching | 0x142133950 | always (2nd) |
| 23 | +0x3D88 | ThinkUnitOnePattern | 0x142133c80 | always (1st) |
| 24 | +0x3DD0 | ThinkUnitSetPlay | 0x142135c50 | set-piece list |

Image units (the *perception* layer the think units read; OBS ctor + MainEval):
`+0xEC` "image A" updated by `0x142119390` (holds the receiver tables §4), `+0x32B0` by
`0x1421197b0`, `+0x32C0` "plan" by `0x14211d710` (the plan id at `+0xA0`, plan target player at
`+0xCC`, plan angle at `+0xC0`), and a 72-byte context built by `0x14211ed10`.

**Unit context** (`ctx`, the `local_88` struct, OBS `0x14211ed10`): `ctx[0]` = &BallPlayer+0x1C
(u32 player index 0..21; `+0x8` team 0/1; `+0x14` float attack sign ±1), `ctx[1]` = match
state, `ctx[2]`, `ctx[3]` = team data (`0x140a48a30`), `ctx[4]` = &BallPlayer+0x78 (carrier
kinematics: `+0x38` facing angle, `+0x40..+0x48` position, `+0x44` pressure 0..1 (§4.3),
`+0x6C` "ball at feet" byte, `+0x6D`, `+0x6E`), `ctx[5]` = image A, `ctx[6]` = +0x32B0,
`ctx[7]` = plan (+0x32C0).

**Answer record** (0x1C B per kind in the table at BallPlayer+0x3E20+0x84; OBS
`0x14211f050`, `0x14210e790`): `{u32 type, u32 kind, u32 a, u32 b, u8 f10, u8 f11, u8 f12,
u8 f13, u8 f14, …, u32 frame}`. `type = 0` means "no answer". Translation to an AI action id
(`0x14210e790`, table 0x1434A4D50 names):

| type | meaning | action |
|---|---|---|
| 1 | shoot | 25 SHOOT (`a` = shot sub-kind); 104 PENALTY_KICK in PK mode |
| 2, 7 | pass to player `a` | `b`: 0 → 22 SHORT_PASS, 1 → 18 LONG_PASS, 2 → 19 CENTERING, 3 → 24 THROUGH_PASS; flags `f11→|1`, `f12→|2`, `f13→|8`, `f14→|0x10` (`0x14210ee00`); throw-in/goal-kick restarts remap to 98..103 |
| 3 | dribble/keep, sub-kind `a`, direction `b` (int degrees) | `0x14210e880`: a=0 → 9 MOVE_STOP_FRONT, 1 → 10 MOVE_STOP_GOAL, 2 → 0 NOTHING (stand on the ball), 3/4 → 3 DRIBBLE mode 3, 5 → 3 DRIBBLE mode 4, 6/7 → 27 FEINT kind 0xE/0xF. Target point = carrier pos + 10.0 (0x142588210) along `b` |
| 4 | feint | 26 KICK_FEINT (in-play) / 27 FEINT, else re-mapped to dribble sub-kinds 1/4/5 (`0x14210eca0`) |
| 5 | clear, angle `a` | 21 CLEAR |
| 6 / 8 / 9 / 11 | keeper answers | 87 KP_PICKUP_BALL / 102 / 90 DIVE / 84 KP_COACHING |

So **keep/hold/shield is not a unit**: it is the Dribble unit's answer with sub-kind 0/1/2.

## 2. When the carrier thinks (OBS) — cadence, slicing, commitment

MainEval `0x142110d30`, per logic frame (1/54 s), only while `BallPlayer+0x10` is set:

1. **Busy reset.** `lVar13 = 0x140a48910(idx)` is the player's action state; if
   `state+8 bit0` and the current action id `state+1` has property bits `0x30000` in the
   per-action table @0x1426484F0 (`0x140a6e320`, ids ≤ 0x5D), `BallPlayer+0x4224 = frame`
   (`matchInfo+0x1198`). I.e. the think timer restarts every frame the carrier is inside a
   committed action.
2. If the current action (or the `0x140a489f0` object's `+0x14` action) has that property, the
   unit pipeline is skipped and `0x14210dd60` continues the running action.
3. Otherwise the answer is reset to "none" (0x19) and the **Dribble unit (kind 16) — or
   GKDribble (19) when `0x140a638c0` says the carrier is the keeper — is evaluated every frame**
   as the default answer (`0x1421102d0`).
4. **Re-think gate:** `frame − BallPlayer+0x4224 > (int)(GetGameSpeed() × 0.1 + 0.5)` = **> 5
   frames** (0.1 @0x142588200, 0.5 @0x142565600, gameSpeed 54) and Intention bit 3 or 4.
   Then: bit 3 → rebuild the active list (`0x1421111b0`), run the list (`0x142110370`), and if
   `BallPlayer+0x18 & 0x10`, adopt (`0x14210f110`) and copy the winner's answer.
5. The answer is converted to an action (`0x14210e790`) and posted (`+0x417C` = 1 → the tick
   entry sets `+0x425A` "decided this frame").

**Intention** (`+0x14`, `0x142117370`): byte0 = mode, byte1 = step, u32 `+4` = bit flags. It is
reset to mode 1 whenever the previous flags had bit 4 or were 0 (or no answer is held), and
mode 0/1 sets flags `0x1F`. Modes 2/3 (flags 3→0x1C, resp. 1→6→0x18 over successive frames —
an even/odd time-slicing of the list in `0x142110370`) are reachable only if something writes
mode ≠ 1; no writer was found in the `bp` code range (scan of all `mov byte/word [r+0x14]`
in 0x14210d000–0x142136000). **INF (high): in AI-vs-AI the flags are 0x1F every frame →
every think evaluates every listed unit.**

**Unit lifecycle** (`+0x38` of each unit; OBS `0x14211eec0`, `0x14211f010`, `0x14211efd0`,
`0x142110370`): 0 = disarmed (slot 1 "reset" is called), 1 = armed (slot 2 `evaluate` runs on
the next pass over the list, then state 2), 2 = holding its answer, stamped with the frame of
evaluation (`unit+0x28` ← ctx4+4) and the carrier position (`unit+0x2C..0x34`). A held answer
is **not re-evaluated**; it is published only when "ripe", and publishing re-arms the unit
(state 1). The per-kind gate `0x142111610` (§3.1) arms/disarms units every think.

**Reaction delay per difficulty** (`0x142110ac0` + `0x142110700`) — this is the "think frames"
table of matchlogic §3.7. An answer of type 2 or 5 (pass, clear) is ripe only once
`now − evalFrame ≥ int54 × T1[tier] / 60` frames, type 3 (dribble/keep) once
`≥ int54 × T2[tier] / 60`, where tier = `*(int*)(BallPlayer+0x60) / 100` clamped 0..6,
T1 @0x142BEF098 = {60, 40, 30, 20, 10, 0, 0}, T2 @0x142BEF0B8 = {40, 30, 20, 10, 5, 0, 0},
60.0 @0x14259a764; result truncated to int. Overrides that make it ripe at once:
`*(u32*)(BallPlayer+0x8C) > int54` (INF med: frames the carrier has had the ball, i.e. after
1 s of possession every answer is immediate); for type 3 also "no answer adopted yet"
(`+0x4204` empty) or the ball (`+0xB8`/`+0xC0`) within 3.0 (0x14258c264) of a touch/goal line
(`|x| ≥ pitch[0]−3` or `|z| ≥ pitch[1]−3`). Kinds in mask `0x1C40800` (11 DirectHighBall,
18 GKClear, 22 GKCoaching, 23 OnePattern, 24 SetPlay) and shots (type 1) have no delay.
Human-controlled carriers (`+0x78 ≠ 0`) have 0. Delay in frames at 54 fps:

| tier | 0 | 1 | 2 | 3 | 4 | 5 | 6 |
|---|---|---|---|---|---|---|---|
| pass / clear | 54 | 36 | 27 | 18 | 9 | 0 | 0 |
| dribble / keep | 36 | 27 | 18 | 9 | 4 | 0 | 0 |

**Decision rate (answer to task item 6).** A think (list evaluation + adoption) can run every
frame once `frame − BallPlayer+0x4224 > 5`, i.e. from the 6th frame (0.11 s) after the
carrier leaves a committed action; the default Dribble answer is recomputed every frame
regardless. A pass/clear found by a unit becomes executable after the tier delay above
(1.0 s at tier 0 … 0.17 s at tier 4, immediate at tiers 5–6 and after 1 s of possession).
`BallPlayer+0x60` is the AI **level**: tier = level / 100 (0..6), and level % 100 is an
interpolation weight used by the capability table (§7). OBS live (20:4x, OPEN_PLAY): all 22
BallPlayers had level = 500 (tier 5) → zero reaction delay in that match. The previous
report's `gameSpeed*0.5` reading is wrong (§0).

## 3. Adoption: how the answers compete (OBS `0x14210f110`, `0x142111610`)

There is **no score comparison between units**. Every listed unit publishes either nothing
or one answer; the adopter walks the active list in insertion order and returns the first
kind that survives its veto rules, with a few hard-coded *overrides* that promote a later
kind. If nothing survives, the Dribble/keep default (kind 16) stands.

Outfield list order (OBS `0x1421112c0`, live-confirmed on 2,454 think snapshots):
`23 OnePattern, 22 GKCoaching, 0 Shoot, 1 Clear, 17 Feint, 14 RespondRequest,
11 DirectHighBall, 12 Special`, then by plan (`+0x32C0+0xA0`, §4.4):

| plan | appended | live sample |
|---|---|---|
| 0 Basic (default) | nothing if `BallPlayer+0xE7` or `close`; else 2 GoalFront, 3 BackSpace, 10 OneTwo, then 4 Forward unless image state (`+0x32B0+8`) = 4 | `…,2,3,10,4` |
| 1 Breakthrough / 2 MiddleShot / 3 CurveShot | nothing if `central && close`; else 2, stop if `plan+0xD9`; 3 if `!(central && mid) && !close`; 7 Cross if zone sector (`imageA+0xB44`) = 3; then 4, 10 | `…,3,2,4,10` (plan 4 shape) |
| 4 PassThrough | 3, 2, then 4, 10 unless (`plan+0xD9` and `0x14211cb00(plan,10)`) | `…,3,2,4,10` |
| 5 Cross | 3 if `ctx4+0x40·sign < pitch[6] − 3`, 7 Cross, 8 EarlyCross | — |
| 6 Long | 9 PassLong | `…,9,7,6` |
| 7 SideChange | 5 | — |
| 8 ShotPassFromGoal | nothing | — |
| 9 BackDFLine | 13 PassTarget | — |

Tail: 9 PassLong if `team+4 == 3 && ctx4+0x6C` (not already listed); 7 Cross and 6 Safety if
`plan+0xD8` or `plan+0xC4 == 3`. Keeper (formation index 0): `14, 20, 21, 18`; human-side
`+0x78 == 1`: `14, 24`. Dribble (16) is never listed (§1).

### 3.1 Per-kind arming gate (`0x142111610`)

Evaluated for every listed kind before its unit runs; failing disarms the unit (state 0).
- Team capability (§7) for three kinds: 17 Feint needs cap 0x1E, 3 BackSpace cap 0x1C,
  6 Safety cap 0x1D (team-level struct from `0x140a40bd0`).
- In open play (`matchInfo+0x116C == 1`) with the ball not yet owned (`+0xE4 == 0`): for
  `int(gameSpeed×0.2+0.5) = 11` frames (`×0.4` → 22 if the event was a kick,
  `0x140a6ce10`) after a ball event addressed to the carrier (`0x140a3e190`,
  `0x140a60320`), every shoot/clear/pass/dribble-class kind except 13 and 22 is disarmed.
- Shoot/Clear and pass kinds then need the "touch window" `BallPlayer+0xB4 ≤
  int(gameSpeed×0.5+0.5) = 27` frames (`int(gameSpeed+0.5) = 54` when owned; for kinds 0/1
  only when `+0xE6` is clear or the last event is not a sub-state-3/4 pass) and the ball
  within **5 m** of the carrier (`dist² ≤ 25.0` @0x14259be20). OBS live `+0xB4` = 18 on
  carriers in possession.
- Pass kinds (`0x14063c9c0`: 2–13, 15, 20, 21) while owning (`+0xE4`): disarmed if the
  carrier's body direction (`player+0x554`) is > 60° from the running action's direction
  (`state+0x124`) during a turn action (`0x140a6e6b0`); if the running action ends within
  `int(gameSpeed×0.15+0.5) = 8` frames; and if team byte `+0x255 == 0` and team `+0x78` names
  another player (INF: designated restart taker). A nearest-opponent test (team table
  `+0x2470C`: opponent index and a value > 9.0 @0x14259d60c; opponent within 120° and faster
  than 10 in `0x140477130` units, heading within 60° after `0.6944/gameSpeed` of motion)
  *skips* the next veto; that veto disarms pass kinds when the ball is higher than
  `byte[0x140a489f0(i)+0x12A7] / 200` and the last ball event is not the carrier's own.
  INF (med) on both meanings.
- Any kind is (re)armed once `BallPlayer+0x8C > int54` (INF: 1 s of possession).

### 3.2 The adopter (`0x14210f110`) — rule list in walk order

For each listed kind `k` whose answer type `t ≠ 0`:
1. Outside set pieces (`matchInfo+0x116C != 3`): kind 22 returns at once.
2. **First-time gate.** Unless the ball is owned (`+0xE4`) or capability 0x14 is on
   (tier ≥ 2): accept only if `[ballY ≥ height/100 (stat 0x31) or (t ≠ 1 or skill 11
   ONE_TOUCH_SHOOT) and t ≠ 5] and [t ≠ 2 or skill 12 ONE_TOUCH_PASS]`. I.e. at tiers 0–1 an
   AI player plays a first-time pass only with One-touch Pass, a first-time shot below head
   height only with First-time Shot, and never clears first-time unless the ball is above
   his head. (OBS raw `0x14210f2d3..0x14210f34d`.)
3. If `BallPlayer+0xC8` and the held answer is Dribble and `k` is Dribble: keep / skip.
4. Restart vetoes: matchInfo `+0x1775 ∈ {3, 9, 10}` skips t = 2, 5; mode 2 accepts only t = 2.
5. **Cut-back beats the long shot**: a shot (t = 1) outside the close band (`!close`, §4.1)
   is replaced by kind 3 PassBackSpace if 3 holds an answer (Shoot is always listed before 3).
6. **Clear (t = 5) is replaced by a pass when one exists**: the first of **3 BackSpace,
   4 Forward, 9 Long, 6 Safety** listed after `k` that holds an answer is returned, unless a
   "forced clear" test passes (it counts opponents around the ball from the pressure table,
   §4.2, with 90°/135° sectors, 1.5 m and 2.0 m distances and team floats `+0x2482C..`; not
   fully resolved). A Safety back-pass to the own keeper is accepted only when owned or the
   keeper is within 90° of the carrier's body direction.
7. Pass kinds: kind 6 Safety is dropped in the close shooting band, and in image state 4
   unless the back-pass direction is within 100° of the dribble direction (`+0x3380`); kind 4
   Forward is dropped if the receiver is the player who passed to the carrier less than
   `2·int54` frames ago (no instant return).
8. A walk reaching kind 16 would apply further dribble-vs-pass overrides (`0x14210fe80..`),
   but 16 is never listed, so they are dead in open play (OBS list builder + live lists).
9. **Cross override**: carrier with skill **14 PINPOINT_CROSS**, answer t ∈ {2, 3} from a
   kind ∉ {7, 8, 23} (or kind 24) → return 7 if Cross is listed later and holds an answer.
10. Final open-play check: accept `k` if the ball owner is unknown or on the carrier's team,
    or `t = 3`; else continue the walk. Set pieces: accept if the carrier is not the team's
    designated kicker (`team+0x7C`), or t ∈ {1, 5}, or a pass t ∈ {2, 7} passes
    `0x14063e3a0`.

If the walk ends, the result is 0x19 ("none") and the per-frame Dribble/keep answer stays.


## 4. Perception layer (image units) — the inputs every unit reads

### 4.1 Shoot zone (`imageA+0x9CC`, built by `0x140698d10`; OBS)

Inputs: ball position `P = ctx4+0x40`, attack sign `s`, pitch table (matchlogic §6).
Init (`0x140699610`, immediates): `R0 = 11.0` (+0x180), `R1 = 12.0` (+0x184), near/far
central range `Rn = 18.0` (+0x188) / `Rf = 24.0` (+0x18C), near/far side range 16.0 / 22.0
(+0x190/+0x194), central half-angle 15.0° (+0x198), side angle 40.0° (+0x19C).

```
Rn += 3.0 * clamp01((Finishing[0x1A] - 70) / 29)                      # 70 @..be4c, 29 @..9460
if skill 39 MIDDLE_SHOT | 33 DROP_SHOT | 8 KNUCKLE_SHOT:  Rf += 3.0
if |P.z| > goalHalfWidth(3.66):      # outside the posts: measure to the near post
    d   = |P - (s*halfLen, 0, sign(P.z)*3.66)|   (+0x170)
    phi = angle(P -> post) vs attack direction (+0x174)
    sector = 0 if |phi|<=15 ; 1/2 (by sign) if |phi|<=40 ; 3 otherwise   (+0x178)
else: d = halfLen - s*P.x ; phi = 0 ; sector = 0
if s*P.x >= 0:   band = (sector==0) ? (d<=Rn ? 0 : (d>Rf)+1) : (d<=16 ? 0 : (d>22)+1)
else:            sector = band = 3                                       (+0x17C)
bit0 (+0x16C) = P inside the 180° half-disc of radius 11.0 in front of the goal centre
```
Predicates used everywhere: `inRange` = d ≥ 0 && (bit0 || (band<3 && sector<3))
(`0x1406997e0`); `central` = inRange && sector 0 (`0x140699710`); `mid` = inRange && band<2
(`0x1406997a0`); `close` = inRange && band 0 (`0x140699820`); `box` = bit0 (`0x1406996f0`);
`headerRange` = inRange && d ≤ 12 (`0x140699750`).
OBS live: `Rn` read 19.03 and 20.90 for two carriers, `Rf` 27.0 for a long-shot-skilled one.

### 4.2 Pressure table (`imageA+0x2124`, `0x14069a500`; OBS)

One 0x170-byte entry per team-mate (entries at +0xCC, index `*(+0x2124)` = carrier):
position predicted `T = max(int(gameSpeed×0.2+0.5)=11, ctx4+0x3C)` frames ahead (carrier:
current); three radii `r1 = 2 + 2f`, `r2 = 1.3·r1 + r1` (1.3 @0x14259bdd8), `r3 = 2·r1 + r2`,
where, with `x` = the mate's attack-signed predicted x, `lo = min(−halfLen/3, lineA)`,
`hi = max(halfLen/3, lineB)` (team-table lines `+0x40`/`+0x38 + team·0x14`):
`f = 1` if `x ≤ lo`, `(x − hi)/(lo − hi)` between, `0` if `x ≥ hi` — i.e. r1 = 4 m in the own
third, 2 m in the attacking third. Opponents are predicted `T` frames ahead (for the
carrier's own entry `T·clamp01((d−2)/3)`), and counted inside r1/r2/r3 at +0x30/+0x34/+0x38;
**pressure class**
(+0x10): 0 if any inside r1, 1 if inside r2, 2 if inside r3, 3 free; the carrier's own class
drops to 0 with ≥ 2 inside r2 and to 1 with > 2 inside r3. Per-opponent distances at
`+0x4C + k·0x1C` (k = opponent slot), angles at `+0x50 + k·0x1C`.

**Danger flag** `imageA+0x31C1` (`0x140699960`, plus `0x142119390`): set when the image's
evaluation says no safe option, and forced to 1 when the ball is in the own third
(`s·P.x < −2/3·halfLen`) with an opponent inside r1 and **no team-mate of openness class ≥ 3**
(`0x142118cb0(table,3,cumulative)==0`). This flag alone triggers the Clear unit (§5.3).
`imageA+0x31C0` = "goal-mouth reachable": ball predicted 18 frames ahead within 30 m
(900 = 30² @0x1425c65f4) of the opponent goal and either a clear lane (`0x14063dd20`) or
inside `imageA+0xB54`² with facing within 60° (90° when not owned).

### 4.3 Pass-receiver tables and openness (`imageA+0xB6C`, `0x142117c30` → `0x1421174f0` →
`0x142118700`; OBS raw + **live-validated**)

**Enumeration.** All 11 team-mates (slot order) get a status code at `table+0x3C+4i`:
1 not on the pitch, 2 the carrier, 4 predicted (18 frames) position off the pitch,
5 set-piece taker, 6 offside, 7/8/9/10
busy action classes (`0x140a6e450`, `0x140a6e110`, action 0x57, action ids 0x0F–0x11),
**0x0B = valid**. Each valid mate gets two records (stride 0xF8):
- **A** (`table+0x68+i·0xF8`, "to feet"): target = mate position predicted
  `int(gameSpeed×0.2+0.5) = 11` frames ahead;
- **B** (`table+0xB10+i·0xF8`, "into space"): from the mate's current position, direction =
  team attack angle (`team+0x250`) + `σ·45°·clamp01(d/30)`, σ = side of the carrier→mate
  bearing relative to the attack direction, d = the mate's distance to his nearest opponent
  (team table `+0x22E4C + idx·200`); length `8.0 + 5.0·clamp01(mateSpeed·cosΔ/20)` m
  (Δ = mate velocity vs that direction). If the target passes the penalty-area line
  (`pitch[6]`), it is re-placed at half the length returned by `0x140a4ddc0` (INF: room left
  before the keeper/goal line).

**Status 6 (offside) — OBS decompile 2026-10-14** (`FUN_142117c30`, `LAB_14211818f`).
```
T16  = int(fps * 0.30 @0x142590d98)                     -- 16 frames, stored at table+0x30
line = 0.0                                             -- a running maximum, so halfway is a floor
for k in 0..4 while entry valid:
        line = max(line, s * (entry[k].x + entry[k].vx * T16))   -- FUN_140a714e0(MA, s), opponents
line = max(line, s * (ball.x + ball.vx * T16))                  -- table+0x34
status = 6  if  line < s * (mate.x)                             -- his position now
           or  line < s * (mate.x + mate.vx * T16)              -- or 16 frames ahead
```
⇒ a team-mate is an offside receiver when his **current or near-future** position is beyond the
deeper of the five last defenders (predicted 16 frames ahead) and the ball (predicted the same way).
This is the carrier's own line, **not** the referee's `MA+0x44`: it has no second-last-defender rule
and no goal-line clamp. It does **not** read the offside flag bits
(`MA+0x253c4+0x10·a+4·c`): an offside attacker is excluded because of geometry, not because a flag is set.

**Record fields** (OBS writer `0x1421174f0`): `[0]` openness class, `[1]` direction class,
`[2]` turn class, `[3..5]` target, `+0xE0` pass angle (deg, `atan2(dx,dz)`), `+0xE4` angle to
the attack direction, `+0xE8` angle to the carrier's facing (`ctx4+0x38`), `+0xEC` distance
**ball → target** (2-D), `+0xF0` difficulty, `+0xF4` openness, `+0x18..` opponents sorted by
distance to the target.

```
dirClass  = fwdAngle <= 60 ? 0 (forward) : (fwdAngle <= 110 ? 2 (square) : 1 (back))
turnClass = turnAngle <= 60 ? 3 : (turnAngle <= 110 ? 4 : 5)
# difficulty (+0xF0)
if not ctx4+0x6D:                       # ball not settled: first-time pass of a moving ball
    s  = clamp01((|ballVel|*3.6 - 10)/50)          # 3.6 @0x14259ce38, 10, 50
    dd = clamp01((dist - 10)/20)
    a  = angle(passDir, incoming ball dir): 1 if <=90, (150-a)/60 to 150, else 0
    mb = (a + dd) * s*s ;  if skill 12 ONE_TOUCH_PASS: mb *= 0.5
run  = (motion state == 4) ? clamp01((carrierSpeed - 16)/12) : 0   # 16 @be10, 28 @a2004
turn = clamp01((turnAngle - 60)/70)                                # 60, 130
high = clamp01((ballY - 0.10869)/0.89131)                          # ball radius .. 1 m
diff = 2.0*mb + 1.5*run + 1.5*turn + high
diff *= 0.7 + 0.3*g(LowPass)    g = 1 if LP <= 80, (99-LP)/19 to 99, 0 above   (unless team flag 6)
# openness (+0xF4) = min over opponents o (in front: |bearing - passAngle| <= 90°)
o' = o.pos + o.vel * int(F * clamp01((|o - ball| - 1)/29))       # F = table+0x38 frames
if body-block test 0x140a53df0(o, corridor 5.0 wide from 9 m behind the ball, length dist+8): margin 0
t  = proj of o (current) on [ball,target]; if 0<t<=1 and |ball-o'|>1.5 and perp(o', lane) < 1.0
     (1.5 if o is sliding, action 0x33/0x34): margin 0
s  = proj of o' on the lane extended to `dist`;  P = ball + lane*min(s,1)
along = |P-ball| ; perp = |o'-P| ; need along >= 0.2
T  = min(0.3*max(along-1,0)^2, 0.6*along)
if s > 1: T *= k^2, k = 0 if bearing(P->o') vs pass <= 45°, (b-45)/45 up to 90°, 1 beyond
if diff > 1: T *= diff + 1
margin = 100 * max(perp - 1, 0) / T          ; start value 150
openClass = margin <10:0, <40:1, <60:2, <100:3, <130:4, else 5   (5 also when no opponent)
```
`F` (`table+0x38`) = `int(gameSpeed×0.2+0.5) = 11` when the carrier owns the ball, otherwise
the number of frames until the ball is closest to the carrier along its trajectory (loop over
`0x140a6cf70`, ≤ 250), minus one.
**Live validation (OBS, 2,454 snapshots, 23,642 valid records):** class-from-openness
thresholds 99.9 % match, direction/turn classes 99.9 %, `+0xEC` = |target − ball| 99.3 %;
the openness formula re-computed from the logged 22 player positions/velocities fits
673 / 831 snapshots to < 2 % mean relative error with `F` as the only free parameter, and the
fitted `F` peaks at 10–11 frames (the owned-ball value 11). The difficulty multiplier is a
per-player constant in [0.76, 1.0] exactly as `0.7 + 0.3·g(LP)` predicts.

**Team histograms**: `table+0..+0x14` count of mates per openness class, `+0x18..` per
direction/turn class; `0x142118cb0(table, k, cumulative)` reads them ("how many mates are at
least class k open").

### 4.4 Plan choice — the carrier's intention (`+0x32C0`, `0x14211e2d0`; OBS)

Ten image units, plan id = index: 0 Basic, 1 Breakthrough, 2 MiddleShot, 3 CurveShot,
4 PassThrough, 5 Cross, 6 Long, 7 SideChange, 8 ShotPassFromGoal, 9 BackDFLine (ctor
`0x142119ee0`). Every frame all are updated; the running plan ends when its slot 3 says so;
when none runs, the first plan whose slot 2 ("start") returns true in the priority order
**9, 8, 2, 3, 1, 4, 5, 6** (table @0x142bf02c0) becomes the plan, else Basic. The plan sets
the desired direction `+0xC0` (`0x14211e650` → `0x14211c380`: goal centre for 2, near post
zone for 3, cross zones `imageA+0x278/+0x294` for 5/6, the plan target player for 8, …) and
the plan target `+0xCC`. Several plans are **rolled**: `roll = RNG(seed = int(ctx4+0x18 ×
K)).next(100)` (`0x140a928a0`/`0x140a924e0`; K = 1100 Breakthrough, 1200 MiddleShot,
1300 CurveShot, 1500 Cross, 1600 Long), and the plan starts if `roll ≤ P`. Because the seed
is a deterministic function of `ctx4+0x18` (INF: a carrier clock), the roll is reproducible.
`X` below is the carrier's attack-signed x (`BallPlayer+0x40`):

| plan | start conditions (abridged, OBS) | P (percent) |
|---|---|---|
| 1 Breakthrough `0x14211a800` | image state `+0x32B0` > 2; `mid` band or X beyond the PA line (36.14); not (`central` and not `close`); team `+0xB8A2` = 0; facing within 60° of the goal | base 70 if `BallPlayer+0x68 == 2` or skill **37 DOUBLE_TOUCH**; 40 if `+0x68 == 1`; 20 if COM **1 DARTING_RUN / 4 INCISIVE_RUN / 2 MAZING_RUN**; + 15·clamp01((x−70)/20) for **Dribbling 0x17, Speed 0x27, Phys. Contact 0x28** (each only if > 69); cap 70 |
| 2 MiddleShot `0x14211b460` | state < 3; `mid` band (or X beyond the PA line when sector = 3); facing within 60° of goal | 70 if skill **39 MIDDLE_SHOT, 8 KNUCKLE, 33 DROP_SHOT, 34 RISING_SHOT** or COM **5 LONG_RANGE**, else 0; +40 if state = 2 or team flag; + 15·clamp01((x−70)/29) for **0x2A (INF Kicking Power)** and **0x1A (INF Finishing)**; clamp 5..80 |
| 3 CurveShot `0x14211af20` | state > 2; zone angle ≤ 0; carrier on the side that matches `BallPlayer+0x64` (INF foot side) | 30 if COM **4 INCISIVE_RUN** + 30 if skill **7 LONG_RANGE_DRIVE** (= "Long-range curler"); + 10·clamp01((x−70)/29) for 0x2A, 0x1A; clamp 5..70; then requires skill 7 or COM 4 unless team flag |
| 4 PassThrough `0x14211b7b0` | plan target valid; not `mid`; X ≤ PA line − 3; ball outside zone `imageA+0x1D0` | — |
| 5 Cross `0x14211ab10` | ball inside the cross zone of its side | 50 if style **7 CROSSER**, COM **3 EARLY_CROSS** or skill **14 PINPOINT_CROSS** (or team instruction `BallPlayer+0x70` = 1 → 50, = 2 → 70); + 20·clamp01((x−70)/29) for 0x2A and 0x1A; clamp 40..80; plus timing gates (270/540 frames) |
| 6 Long `0x14211b1d0` | not already vetoed (`0x14211a570`), team mode ≠ 3, not designated | base by carrier pressure class: 0–1 → 30, 2 → 60, 3 → 80; + 50 COM **6 LONG_PASS**, + 50 skill **13 WEIGHTED_PASS**, + 50 skill **17 LOW_LOB**; + 20/40 for `BallPlayer+0x70` = 1/2; + 10·clamp01((LoP 0x1C − 70)/29). If `BallPlayer+0x58` ≠ 0: only 10+10 for skills 13/17 + the stat term |
| 7 SideChange `0x14211b870` | target valid; X ≤ PA line − 5; `|ball z| > halfWidth/3`; target on the far side | — |
| 8 ShotPassFromGoal `0x14211a030` | (not decoded) | — |
| 9 BackDFLine `0x14211a7f0` | plan target valid | — |

The plan id then selects which pass units are listed (§3) and gives Dribble its preferred
direction and weight (§5.4). Live (2,454 snapshots): plans 0 (80 %), 4 (17 %), 6, 1 seen.

## 5. The think units — literal decision functions

Notation: `own` = ball owned (`ctx4+0x6C`), `rec_A[i]` / `rec_B[i]` = §4.3 records,
`openCls`, `dirCls`, `turnCls`, `dist`, `diff` their fields, `press` = carrier's pressure
entry (§4.2), `zone` = §4.1, `plan` = §4.4. "→ pass(i, b)" = answer type 2 to player i with
pass kind b (0 short, 1 long/lofted, 2 centering, 3 through; §1).

### 5.1 Shoot — kind 0, `0x142121920` (OBS)

No-shot exits, in order: restart states 3/9/10 with the team-table pressure value > 400;
`0x140a4c930` (INF: ball out of play / own-goal side); carrier action 0x12; outside
`inRange` with no usable opponent keeper (keeper index from `0x140a62690`, keeper busy and
not flagged). First-time (`!own`): plan ≠ 8 and not `+0x6E`: incoming-ball vs facing angle
over 120° → no; ball-to-goal-target distance² over 400 (20 m) → no; over 225 (15 m): body
angle over 70° and `0x140a4dcc0` over 7.0 → no; a volley/header situation (`+0x6E`) checks
the ball's arrival direction within 60°. `unit+0x41` = "ball above head":
`ballY > 0.8 × height/100`. Plan check: plan target dist < 30 with `plan+0xD9` and
`imageA+0xB3C ≥ 16` → no.

Then the **shot decision** (sub-kind `a`; aim point from `0x14211f090`):
```
if zone.d < 20 and zone.angle < 40 and keeper present & settled:      a = 1   # just shoot
elif box (11 m half-disc) or (aboveHead and headerRange(12 m)) or plan == 8:   a = 1
else:
   if !own and !+0x6E and angle(ball->aim, facing) > 100: no shot
   if plan == 2 (MiddleShot) and not +0x6F and (central or close or press.n1 > 1):
       allowed = (stat[0x26] < 3) ? facing + (foot ? -20 : +20) : facing      # weak-foot side
       tol = 50 + 30*clamp((Rf - d)/(Rf - Rn))  [stat 0x26 < 3]  or  100 + 60*clamp(...)
       if d <= Rf and ball-goal dist² > 0.81 and |aim - allowed| < tol
          and visibleGoal(imageA+0x898, 2) > 0.1:  a = 1
   elif central:  a = central_eval(0x14211fe20)
   elif plan != 3: a = wide_eval(0x142120e60)         # not decoded
if cap 0x16 (tier >= 2) and finesse_ok (0x14211fbf0):  a = 3     # curled / finesse
if cap 0x15 (tier >= 3) and chip_ok (0x142120bd0):     a = 2     # chip
answer = {type 1, a, aim.xyz}  if a != 0
```
`central_eval` (`0x14211fe20`) rejects when an opponent sits within 6.25 of the shot line
(pressure entry), the keeper is too close (dist² < 625 in the long branch, < 49 without
COM 5 LONG_RANGE in the `mid` branch), the ball is behind the PA line (`s·x < 36.14`) and the
shooter has none of skills 8, 33, 34 nor COM 5, the lane test `0x14211faf0` fails (corridor
from the ball 1/3 or 2/3 of the way to the aim), or the aim is more than `60 − zone.angle`
(resp. `90 − zone.angle − 20·clamp(...)`) degrees off the body direction. It returns
**4** (knuckle, ActionShoot flag 4) with skill **8 KNUCKLE_SHOT**, owned, aim within 15° of
the facing and the frame random byte (`*(ctx+0x360)+4`, INF 0..99) below 10 — reset to 1
when the shooter also has **33 DROP_SHOT** or **34 RISING_SHOT** under the same angle test;
**5** (ActionShoot flag 8; INF volley/half-volley) in the close band with ball height over
0.7 m, distance under 8 m and random below 65; else **1** (OBS raw `0x142120a99..0x142120b75`).

`finesse_ok`: plan 2/3 or skill **7 LONG_RANGE_DRIVE**, owned, `zone` bits 11|12, aim within
90° of facing and outside the post-to-post sector, and the curl side agrees with the
stronger foot (`stat 0x34`) unless `stat 0x26 == 3` (INF: two-footed).
`chip_ok`: aim within 60°, band < 2, d ≥ 10, keeper ≥ 8 m from the ball and the keeper's
team-table value over 81 and over (d/2)² (INF: keeper far off his line), else a body test
`0x140a53da0`.

ActionShoot translation (`0x140979170`, OBS): a = 2 → kick style 2 with power
`0.2·clamp01((d−10)/25) + 0.55`; a = 3 → style 1, power `0.3·clamp01((d−10)/25) + 0.6`;
a = 4/5 → normal kick with flags 4/8; a = 1 → normal. INF (high): 2 = chip, 3 = finesse.
Header shots are not a separate sub-kind: `aboveHead` + `headerRange` gives a = 1 and the
action layer picks the header animation.

**Live:** 23 shot answers adopted (kind 0, type 1, a = 1) in 2,454 think snapshots at tier 5.

(INF med on the header remark above: no header sub-kind exists in `0x142121920`; whether the
action layer chooses a header animation from ball height was not traced.)

### 5.2 Pass units

**PassForward — kind 4, `0x14212a700` (OBS raw).** Gate: (own or `press.cls < 2`) and not
`close`. Attempts in order:
1. **short forward** `0x142125c80` → pass(i, 0), `f10` = one-touch flag when
   `0x142128c10 && 0x142124950`;
2. else, if the carrier's team-table value (`0x140a714c0` side table, INF "free space") exceeds 40.0: **through**
   `0x142127ff0(ctx, 0)` → pass(i, 3) with `f12` = 1;
3. else, image state ∈ {1, 2}: **lofted forward** — among valid mates with `0x142124cc0`
   ok, `15 ≤ dist ≤ 25`, `turnCls == 3`, `dirCls ≠ 1` (not backward), same touchline side
   as the carrier when `plan+0xC4 == 3` and the mate is wider than `pitch[5]` (9.16),
   **ground lane closed (`openCls == 0`)**, opponent-slot-0 record valid,
   `(10 + 5·clamp01((dist−15)/10))² < pressure[i].dist(opp 0)` and `pressure[i].cls > 2`:
   the most advanced (max `s·x`) → pass(i, 1).
   INF (med): `+0x4C` of a pressure entry is a *linear* distance (`0x140a8f860`, sqrt) to
   opponent slot 0 (INF: the keeper), compared with a *squared* 100–225 — it can never pass
   on a 105 m pitch, so this branch is effectively dead (raw `0x14212aaa2: mulss xmm6,xmm6;
   comiss [rax+0x4c]`). Live: all 61 PassForward answers were kind b = 0 (short); b = 1/3
   came only from PassLong (31× b = 1), DirectHighBall (64× b = 3) and SetPlay.

`0x142125c80` (short forward), per valid mate; returns a mate flagged by `0x14063e890` at
once, otherwise the **most advanced** survivor:
```
lp = stat LowPass[0x1B] (+20 if skill 12)
skip if |passAngle - plan.+0xC0| > 60 and (opponent-team flag and value > 36, or (!own and dist > 15))
if danger(+0x31C1) or diff <= 1.0:
   need = state 0: 2 (4 if fwdAngle > 60) | state 3/4: 2 | else 3, 4 if fwdAngle > 120,
          2 if fwdAngle <= 30 and not 0x140a4c8e0
   need = 4 if the target is within 30 m of the opponent goal centre
   require openCls >= need
   lane test 0x14063dab0(width clamp(0.25*dist, 1.5, 6.0), 220, 27 frames) — run in image
        states 0/1, or when the line value 0x140587e70 < 1225 (35²), or |line.x - mate.x| < 10
   free = 0x14063e010(i) && 0x14063e270(i)   (INF: mate's own shoot-zone tests)
   if !free: nearest opponent to the mate must be farther than r, r = 8 if 0x14063e140(i),
        else 5 in state 0 (or state 1 with the line value < 1600), else 1.5
   |ball.x - target.x| <= 35
   opponents within 7 m of the mate (3 m if free), 0x140a4e250: < 3 (< 2 when the mate is
        in his own half)
   dist < 15: reject if the mate faces > 135° away and is moving (motion > 2), or > 80° away
        and (speed >= 20, motion 4, or action 0x1D-0x1F)
   reject if the ball is already travelling to that mate
   turnAngle < 80 (dist < 15: 104 if !own and lp > 70; 140 if own and press.cls > 1);
        no turn limit for skill-12 first-time passes shorter than 10 m
```

**PassSafety — kind 6, `0x14212b520` (OBS skeleton).** Outside the danger state:
`k = (press.n1 ≠ 0 ? 2 : press.n2 > 1 ? 1 : 0) + (|carrier facing − plan angle| > 135°)`;
a free lane from the ball along the attack angle + 180° (`0x14063de20`, 80 m, 3.0) → no
answer. Then `i = 0x142126b70(ctx, 1, 1)` (short safe receiver); if `k ≥ 1`, try
`j = 0x142128490` (safe through) → pass(j, 3, `f12`), else pass(i, 0) with the one-touch flag.
`0x142126b70` filters mates by openness class against the pressure count, distance windows
5/10/15/25/30 m, turn limits 45°–135° and a lane test of width `clamp(dist/5, 4, 12)`, and
breaks ties by the **smaller turn angle when the carrier is pressed** (nearest opponent
≤ 9 m), otherwise by the **more advanced** mate (partial decode).

**PassLong — kind 9, `0x14212ad40` (OBS).** Needs own (or the danger flag with the ball
below 1.5 m). Target: in plans 6/7 the plan target if `0x1421240b0` validates it; else
`0x142128a20`: among mates valid for a long ball, the **most advanced**; a candidate within
10 m (x) of the current best replaces it when **taller by more than 5 cm** (stat 0x31) —
the target-man rule. Pass kind **short (0) if `openCls ≥ 4` (≥ 3 when `0x14063a3a0`), else
long (1)** with `f14`.

**PassSideChange — kind 5, `0x14212b8b0` (OBS).** Plan 7 and own: target = plan target;
needs `dist ≥ 20` and |pass angle − carrier facing| ≤ 60° → pass(t, 1).

**PassTarget — kind 13, `0x14212b9e0` (OBS).** Plan 9: if the target isn't the carrier:
when not owned or the target's record is not valid → type 3 sub-kind 4 (dribble toward the
target), else pass(t, 0).

**PassGoalFront — kind 2, `0x14212aba0` (OBS gate).** Not `central`; a free lane straight
ahead (`0x14063de20` along the attack angle, 20 m, 80°) means **no** pass; else
`0x1421265c0` (goal-front receiver; skill 12 adds 20 to its LowPass use) → pass(i, 0).

**PassOneTwo — kind 10, `0x14212aeb0` (OBS).** If an opponent closes within 6 m in
`int(gameSpeed×0.3+0.5) = 16` frames (`0x140a4e720`) or the carrier faces within 90° of the
attack direction, and `0x142128c10` (one-touch ok): `0x142126a80` → pass(i, 0) with the
wall-pass flag `f10`.

**PassDirectHighBall — kind 11, `0x142129be0` (partial).** First-time pass while receiving
(`0x140a6e5e0` on the carrier's action) with the ball not above `height/100 + 0.1`; uses the
receiver records, keeper distance > 25 m checks and the team pass-request table. Live: the
most frequent adopted pass (284 of 2,454 snapshots), always with `own = 0`.

**Cross 7, EarlyCross 8, Special 12, RespondRequest 14, OnePattern 23, BackSpace 3**:
evaluators located (§1) but not decoded (open list). Live: Cross answered 16 times.

### 5.3 Clear — kind 1, `0x142109eb0` (OBS)
`if imageA+0x31C1 (danger): answer {type 5, a = 0x142108630(ctx, carrier)}` (clear angle),
then usually overridden by any listed pass answer (§3.2 rule 6). Live: 44 adopted.

### 5.4 Dribble and keep — kind 16, `0x14212fe70` (OBS core)

Runs every frame as the default answer. Pre-check `0x142131810` "let it run": not owned,
team flag 5 (`0x140a60360`) off, the last event a pass by this team, the carrier's
attack-signed x between 0 and `halfLen − 20`, ball speed 40–60 (`0x14042d470`, INF km/h)
within 20° of the attack direction with ≥ 15 m of room (`0x140a4ddc0`), and a team-mate
level or ahead within 8 m laterally and < 5 m ahead → answer type 3 sub-kind 5
(DRIBBLE mode 4).

**Direction choice `0x14212ec20`** (writes the dribble angle `+0x2A0`):
```
W = 0.1                       # preference weight, by image state (+0x32B0+8):
    state 1: 0.3 (0.5 if own); states 2..4: 1.0; state > 4: 0; plans 1/2/3: 1.0
desired = plan.+0xC0, bent once by 2/3 of the angle to an escape tangent point when the
          tracked opponent's predicted path crosses the 5 m ray at 30..150° (0x140a900e0)
u = clamp01((W - 0.5) * 2)
best = +inf
for k in 0..71:                                   # 72 directions, 5° apart
    cost = 0
    for j in 1..15:                               # r_j = j * 2/3 m  -> 10 m look-ahead
        p    = ball + r_j * dir(5k)
        edge = 100 if min(halfLen - |p.x|, halfWid - |p.z|) <= 0.5 else 0
        pref = 0.6*W*(1 - r_j*cos(5k - desired)/10)        (0 when W == 0)
        cost += max(edge, threat(p) + pref) * ((10 - (r_j - 2/3))/10) ** (3 + 3u)
    if cost < best: best = cost; angle = 5k

threat(p) = sum over listed opponents o of
        pow(max(R_o - |p - o*|, 0)/R_o, 1 + w_o) * a_o * (2 if o is the keeper)
  o*  = point of o's predicted path (now -> frames*clamp01(dist/10)) closest to p; if that
        point is within 0.667 m (0.444 m² @0x142bf0a88) of the ball, it is pushed 2/3 m away
  R_o = 10/f, f = 1 (image state 0), 3 − 2·clamp(zone position) (states 1–2), 2.5 (state 3),
        3 (state ≥ 4); 1.0 if o is busy (0x140a6e3c0 or flag bit 2); 5.0 if o is the keeper
        (the opponent list is distance-sorted and stops past R_o + 10 m from the carrier)
  w_o = 1 if d ≤ 1, (5 − d)/4 if 1 < d < 5, 0 beyond   (d = carrier→o distance)
  a_o = 1 when not owned; when owned, with γ = angle between p's direction and the bearing
        to o: 1 if γ ≤ 90°, falling linearly to 0 at L = 100 + 60·g, where g = 1 if the
        carrier faces within 80° of o, linear to 0 at 135°
```
Then the sub-kind (`0x14212f1c0` and the tail of `0x14212fe70`): 3 or 4 (DRIBBLE mode 3),
5 (DRIBBLE mode 4), 6 (FEINT 0xE) along `+0x2A0`; **2 = stand on the ball (keep)** when no
direction was kept; the **shield** sub-kinds 0/1 (MOVE_STOP_FRONT / MOVE_STOP_GOAL) come
from other branches of the same function (`0x142130ee0` and the plan-target checks) — open.
Live: adopted Dribble answers were sub-kind 4 (801) and 5 (86); no 0/1/2 seen at tier 5.

So **when does the carrier hold/shield?** OBS structure, INF semantics: (a) when every
listed unit stays silent (no ripe pass/shot/clear answer, §2–3), the Dribble answer is
executed; (b) Dribble itself chooses "keep" (sub-kind 2) only when no direction beats the
edge/threat costs — boxed in near a line or surrounded; (c) at low tiers the reaction delay
(§2) keeps a found pass unpublished for up to 1 s, so the carrier dribbles/holds meanwhile.

### 5.5 Feint — kind 17, `0x14212d910` (OBS skeleton)
Two phases. Idle: `0x14212c0f0` (eligibility) and `0x14212cb30` pick a feint; skill and COM
reads at `0x14212c2c0` (skill 3 SOMBRERO), `0x14212c670` (skill 0x15 = 21 MALICIA,
INF), `0x14212cc20` (skill 4 QUICK_TOUCH, COM 2 MAZING_RUN). An answer of type 4 arms
phase 2, which emits a dribble answer (type 3, sub-kind 4, stored direction) while the
carrier's action is 0x1B FEINT. Needs team capability 0x1E (tier ≥ 2).

## 6. Attributes, skills and styles in the carrier brain (answer to task item 5)

### 6.1 Engine enums (OBS — resolves matchlogic §1.3's "engine bit order")

The script-binding string table at **0x1434A10D0** (u64 pointers) lists the in-match masks
in bit order; three independent checks confirm it is the bit order: GK spot check of
matchlogic §1.3 (skill bits 30 = PK_STOPPER = GK Penalty Saver, 35 = HIGH_PUNT_KICK = GK High
Punt; style bit 19 = OFFENSIVE_GK) and team-plans' Super-sub test `>> 0x1a` (26 = SUPER_SUB).

- **Skills, block+0x40 (u64), bit = index**: 0 SCISSORS_SKILLS, 1 FLIP_FLAP_SKILLS,
  2 ROULETTE_SKILLS, 3 SOMBRERO, 4 QUICK_TOUCH, 5 PIVOT_FOOT_TOUCH, 6 HEADER,
  7 LONG_RANGE_DRIVE, 8 KNUCKLE_SHOT, 9 ACROBATIC_SHOOT, 10 HEEL_TRICK, 11 ONE_TOUCH_SHOOT,
  12 ONE_TOUCH_PASS, 13 WEIGHTED_PASS, 14 PINPOINT_CROSS, 15 OUTSIDE_LONG, 16 RABONA,
  17 LOW_LOB, 18 LOW_PANT_KICK, 19 LONG_THROW, 20 GK_LONG_THROW, 21 MALICIA, 22 MAN_MARK,
  23 CHASING, 24 ACROBATIC_CLEAR, 25 CAPTAINCY, 26 SUPER_SUB, 27 COMBATIVE_SPIRIT,
  28 NO_LOOK_PASS, 29 PK_KICKER, 30 PK_STOPPER, 31 CONTROL_LOOP_SHOT, 32 SOLE_CONTROL,
  33 DROP_SHOT, 34 RISING_SHOT, 35 HIGH_PUNT_KICK, 36 INTERCEPT, 37 DOUBLE_TOUCH,
  38 EDGE_TURN, 39 MIDDLE_SHOT, 40 THROUGH_PASS.
  Player.bin names (matchlogic §1.2) map by meaning, INF high: 7 = Long-range curler
  ("Long Range Drive" 0x52), 11 = First-time Shot, 12 = One-touch Pass, 13 = Weighted Pass,
  14 = Pinpoint Crossing, 17 = Low Lofted Pass, 21 = Gamesmanship, 33 = Dipping Shot,
  34 = Rising Shots, 37 = Double Touch, 39 = Long Range Shooting, 40 = Through Passing.
- **Playing styles, block+0x48 (u32), bit = index**: 0 GOAL_POACHER, 1 DUMMY_RUNNER,
  2 FOX_IN_THE_BOX, 3 POST_PLAYER, 4 CHANCE_MAKER, 5 WING_STRIKER, 6 INSIDE_PLAYER,
  7 CROSSER, 8 CLASSIC_NO_10, 9 FREE_ROAMING, 10 BOX_TO_BOX, 11 ANCHOR_MAN, 12 ENFORCER,
  13 PLAY_MAKER, 14 BUILD_UP, 15 OVERLAP, 16 OFFENSIVE_SB, 17 DEFENSIVE_SB, 18 INNERLAP,
  19 OFFENSIVE_GK, 20 CLASSICAL_GK.
- **COM playing styles, block+0x4C (u32; getter `0x14063e830`), bit = index**:
  0 TRICK_STAR, 1 DARTING_RUN, 2 MAZING_RUN, 3 EARLY_CROSS, 4 INCISIVE_RUN, 5 LONG_RANGE,
  6 LONG_PASS (INF: = Trickster, Speeding Bullet, Mazing Run, Early Cross, Incisive Run,
  Long Ranger, Long Ball Expert).

### 6.2 Bindings used by the carrier (every read below is OBS at the listed address)

Stat indices are the in-match block byte (matchlogic §1.3; labels there are INF high except
where noted). "Live stat" = read through `0x140a489f0(i)+0x70` (fatigue-adjusted copy).

| unit / function | input | weight / use |
|---|---|---|
| receiver difficulty `0x1421174f0` | stat **0x1B Low Pass** | `diff × (0.7 + 0.3·g)`, g = 1 ≤ 80, (99−x)/19, 0 at 99 (lower diff → more lanes open) |
| receiver difficulty | skill **12 ONE_TOUCH_PASS** | moving-ball term ×0.5 |
| short forward `0x142125c80`, goal-front `0x1421265c0`, through `0x142127ff0` | stat 0x1B (+20 with skill 12) | turn-limit raise 80 → 104 when > 70 (first-time) |
| shoot zone `0x140698d10` | stat **0x1A Finishing** | near range `18 + 3·clamp01((x−70)/29)` |
| shoot zone | skills **39, 33, 8** | far range 24 → 27 |
| adoption `0x14210f110` | stat **0x31 height** | ball-above-head test (`/100`) |
| adoption (tiers 0–1, first time) | skills **11**, **12** | first-time shot / pass allowed |
| adoption | skill **14 PINPOINT_CROSS** | Cross override |
| Shoot `0x142121920` | stat 0x31 | "above head" `0.8·h/100` |
| central shot `0x14211fe20` | skill **8** (knuckle 10 %), **33/34** (cancel knuckle; allow long shot), COM **5** (long shot) | sub-kind 4 / long-shot permission |
| long-shot branch, finesse | stat **0x26** (`< 3` → one-footed, INF weak-foot usage) and **0x34** (stronger foot, INF) | ±20° comfortable aim, curl side |
| finesse `0x14211fbf0` | skill **7** | allows sub-kind 3 outside plans 2/3 |
| Breakthrough plan | skill **37**, COM **1/2/4**, stats **0x17 Dribbling, 0x27 Speed, 0x28 Physical Contact** | P table §4.4 |
| MiddleShot plan | skills **39, 8, 33, 34**, COM **5**, stats **0x2A, 0x1A** | P table §4.4 |
| CurveShot plan | skill **7**, COM **4**, stats 0x2A, 0x1A | P table §4.4 |
| Cross plan | style **7 CROSSER**, COM **3**, skill **14**, stats 0x2A, 0x1A | P table §4.4 (INF: 0x2A/0x1A for crossing looks odd — labels need a check) |
| Long plan | COM **6**, skills **13, 17**, stat **0x1C Lofted Pass** | P table §4.4 |
| long target `0x142128a20` | stat **0x31 height** | taller by > 5 cm wins within 10 m |
| Feint | skills 3, 4, 21; COM 0/2 | eligibility (not decoded) |
| other reads in the range (not decoded) | skill 0x25 at `0x1421314a0`, 0x28 (40 THROUGH_PASS) at `0x14211bcbe`, 0x13 at `0x142137460`, 7/8 + stats 0x1A ≥ 85 / 0x21 ≥ 80 at `0x14213647d` (set-play kicker) | — |

Playing-style bits (block+0x48) are read in the carrier code only once (Cross plan, bit 7).
The style influence is otherwise outside the carrier: off-ball selectors (team-plans §3).

## 7. Difficulty → AI capability table (OBS `0x140a603c0` / `0x140a604d0`, table 0x142645A40)

34 rows × 32 B: byte 0 = interpolate flag, floats `v[0..6]` per tier at +4. Value for level L
(`BallPlayer+0x60`, `team` struct for team caps): `t = L/100` (clamped 0..6), `r = L%100`;
if the flag is set, r ≠ 0 and t < 6: `v[t] + (v[t+1]−v[t])·r/100`, else `v[t]`.
`0x140a604d0` = "|value| > 1.19e-7" (capability on). Rows (tiers 0..6):

| cap | v | consumer (OBS call site) |
|---|---|---|
| 0x00, 0x01 | 4, 3.5, 3, 2, 1.5, 1, 0.5 (interp) | `0x1404fdeea` (not carrier) |
| 0x02 | 60, 40, 20, 12, 12, 0, 0 | `0x1406284c5` |
| 0x03 | 1 × 7 | `0x140534a20` |
| 0x04 | 0, 1 × 6 | `0x1406295a0` |
| 0x05, 0x07, 0x08, 0x0F, 0x10, 0x15, 0x18, 0x1F | 0, 0, 0, 1, 1, 1, 1 | 0x15 = carrier **chip shot** (§5.1); others off-ball/defence |
| 0x06, 0x09, 0x0A, 0x0E, 0x14, 0x16, 0x19, 0x1C, 0x1E, 0x21 | 0, 0, 1, 1, 1, 1, 1 | 0x14 = carrier **first-time freedom** (§3.2), 0x16 = **finesse shot**, 0x1C = **PassBackSpace**, 0x1E = **Feint** (team caps) |
| 0x0B | 4, 3, 2, 1.5, 1, 0, 0 (interp) | `0x140623750` |
| 0x0C | 2, 1.5, 1, 0.5, 0, 0, 0 (interp) | `0x14096a51d` |
| 0x0D | 30, 50, 85, 90, 95, 100, 100 (interp) | — |
| 0x11, 0x1A | 0, 1 × 6 | — |
| 0x12 | 60, 40, 30, 20, 10, 0, 0 (interp) | (same numbers as T1, §2) |
| 0x13 | 40, 30, 10, 10, 5, 0, 0 (interp) | — |
| 0x17 | 0, 0, 0, 0, 1, 1, 1 | — |
| 0x1B, 0x1D | 1 × 7 | 0x1D = **PassSafety** (always on) |
| 0x20 | 10, 10, 5, 3, 3, 0, 0 (interp) | — |

Carrier difficulty effects therefore are: the reaction delay T1/T2 (§2, tiers 0–4), the
first-time gate (tiers 0–1), finesse shots and BackSpace and feints from tier 2, chip shots
from tier 3.

## 8. Live validation (task item 7; read-only `/proc/<pid>/mem`)

Tools: `CarrierAIRE/live_bp.py` (finds the 22 `bp::BallPlayer` objects by vtable 0x142bef090
with ThinkUnitShoot's vtable at +0x3678; dumps state), `CarrierAIRE/sampler.py` (90 s,
2 ms polling, one record per carrier per logic frame: list, answers, states, plan, zone,
pressure, receiver table A, 22 player positions/velocities), `validate_open.py`.
Sample `s1.jsonl`: 2,454 think snapshots, OPEN_PLAY, both teams AI, level 500 everywhere.

| check | result |
|---|---|
| Intention flags | 0x1F on every active carrier (mode 1) — §2 INF confirmed |
| active-list composition | every observed list is produced by `0x1421112c0` for the logged plan id (e.g. Basic `23,22,0,1,17,14,11,12,2,3,10,4[,7,6]`, plan 4 `…,3,2,4,10`, plan 6 `…,9,7,6`, keeper `14,20,21,18`) |
| answer encoding | adopted payloads: Dribble type 3 sub-kinds 4/5; passes type 2 with `a` = receiver index 0..21; Shoot type 1 a = 1; Clear type 5 — matches `0x14210e790` |
| receiver classes | openness→class thresholds 23,617 / 23,642 records; direction and turn classes 99.9 %; `+0xEC` = ‖target − ball‖ 99.3 % |
| openness formula | recomputed from positions: 673 / 831 snapshots within 2 % mean error with the ball-travel budget as the only free parameter; best budget peaks at 10–11 frames (= 11 for an owned ball) |
| difficulty multiplier | per-player constant in [0.76, 1.00], consistent with `0.7 + 0.3·g(LowPass)` |
| shoot ranges | `Rn` 19.03 / 20.90 (Finishing ≈ 71 / ≈ 89), `Rf` 27.0 for a long-shot-skilled carrier |
| kind frequencies (adopted) | 16 Dribble 890, 6 Safety 329, 11 DirectHighBall 284, 24 SetPlay 279, 10 OneTwo 143, 4 Forward 89, 1 Clear 44, 9 Long 31, 0 Shoot 23, 7 Cross 16; 326 snapshots with no answer |

Not validated live: the reaction delays (tier 5 → 0 in this match), the plan roll
probabilities, the dribble direction cost (the per-direction costs are not stored), shot
sub-kinds 2–5 (none occurred). The match ended mid-session (registry pointers went to 0), so
the player-stat read-back of `g(LowPass)` per player was not completed.

## 9. Open list

1. **Undecoded units**: Cross `0x142129590`, EarlyCross `0x14212a2d0`, BackSpace
   `0x142129110`, Special `0x14212bb00`, RespondRequest `0x14212b230`, OnePattern
   `0x142133ed0`, GKCoaching `0x142133950`, the wide-shot evaluator `0x142120e60`, the aim
   point `0x14211f090`, the shot-lane test `0x14211faf0`, the through choosers
   `0x142127ff0` / `0x142128490`, `0x142126b70` in full, the long validity `0x1421240b0`,
   `0x142124cc0`, `0x142124950`/`0x142128c10` (one-touch flag), `0x142108630` (clear angle).
2. **Dribble sub-kind choice** `0x14212f1c0` (481 lines) and the shield branch
   `0x142130ee0`: when MOVE_STOP_FRONT/GOAL (shield) is answered.
3. **Plan targets**: slot 7 of each image unit (`+0xCC`) and `0x14211c380` (desired angle);
   ShotPassFromGoal start `0x14211a030`; end conditions (slot 3).
4. **Unknown fields**: `BallPlayer+0x58/+0x64/+0x68/+0x70` (INF team instructions / foot),
   `+0x8C` (INF possession frames), `+0xB4`, `ctx4+0x18` (plan-roll seed),
   `ctx4+0x6D/+0x6E/+0x6F`, image state `+0x32B0+8` (0..4, `0x1421197b0`), the
   frame-random byte `*(ctx+0x360)+4`, team floats `+0x2470C`, `+0x228D4`, `+0x2482C`.
5. **Stat labels** 0x1A/0x2A/0x26/0x34 are INF; the Cross plan's use of 0x2A/0x1A should be
   re-checked against a player whose Lofted Pass/Curl differ from Finishing/Kicking Power.
6. Body-block test `0x140a53df0` (explains the 158 openness snapshots that did not fit).
7. Live validation at tiers 0–4 (needs a match at a lower difficulty) for the reaction delay
   and first-time gate.

## 10. What is implementable now (OBS unless noted)

- Think cadence: default dribble/keep every frame; full think when `frame − busyStamp > 5`;
  reaction delay tables T1/T2 by tier; capability table rows 0x14/0x15/0x16/0x1C/0x1D/0x1E.
- Unit list builder per plan, the arming gate's 5 m / 27–54-frame / 11-frame windows, and the
  adoption walk with rules 2, 5, 6 (structure), 9, 10.
- Shoot zone (all constants), the "just shoot" rule (`d < 20`, angle < 40), box/header rule,
  knuckle/volley sub-kinds, finesse and chip gates (structure), ActionShoot power formulas.
- Receiver enumeration (status codes, offside line, A/B targets), the difficulty term and the
  openness/interception formula with its class thresholds (live-validated), the short-forward
  filter cascade, PassForward's three tiers, PassLong target-man rule, SideChange, Target,
  GoalFront and OneTwo gates.
- Pressure table radii/classes, danger flag → Clear, clear-replaced-by-pass rule.
- Plan selection order and the rolled start probabilities for Breakthrough, MiddleShot,
  CurveShot, Cross and Long with every skill/COM/style/stat term.
- Dribble direction: the full 72×15 cost search with edge, threat and preference terms.
- Engine skill/style/COM bit enums (0x1434A10D0) — needed by every attribute-driven branch.


