# PES 2021 team tactics sheet → AI behaviour (TacticsRE)

Scope: every team-sheet tactics value, where it lives at runtime, which AI code reads it and how. Evidence marks:
**OBS** = seen in code (raw/decompile at the stated VA) or read live; **INF(conf)** = inference.
Scratch: the TacticsRE scratch dir (`a*.c`/`b*.c` decompiles, `args.py` call-site argument scanner, `after.py`
return-use summariser, `vcls.py` vtable-slot → RTTI class, `live*.py` read-only live readers).

## 0. Corrections to earlier reports (read first)

- **Defensive Style is NOT a byte of the 5-byte block.** `FUN_140a62180(ctx, side, k)` (defensive-shape §6 "style")
  calls `FUN_1408c5030(T+0x8e08, k, 3)`, the **7-byte binary block**, not `FUN_1408c52e0` (OBS raw 0x140a621b7).
- **Attack level is not a pure team-strategy copy** (setpiece §11.6): the strategy word `T+0x134` maps 0→0, 1→1, 3→3,
  4→4, else 2, but the value actually stored follows the manual/target word `T+0x210` under conditions (§3).

## 1. The team sheet `S = T + 0x8e08` (OBS layout from the accessor family 0x1408c4fe0–0x1408c52e0 and ctor `FUN_1408c5390`)

`plan = *(u32*)S` ∈ {0,1,2}; every accessor takes a plan argument, `3` = "current" (`*S`).

| offset | type | accessor | content |
|---|---|---|---|
| `+0x00` | u32 | — | current plan index 0..2 |
| `+0x04 + 5·plan + k` | u8, k<5 | `FUN_1408c52e0(S,k,plan)` | **numeric sliders** (defaults k0..4 = 5,2,5,5,2) |
| `+0x13 + 7·plan + k` | u8, k<7 | `FUN_1408c5030(S,k,plan)`; team wrapper `FUN_140a62180(ctx,side,k)` | **binary toggles** (defaults all 0) |
| `+0x28 + 4·(3·(11·plan+slot)+f)` | i32, slot<11, f<3 | `FUN_1408c51b0` (default 0xc) | registered role per slot per formation phase f; `FUN_1408c51f0` maps role 0..12 → class 0..9 |
| `+0x1b4` / `+0x268 + 2·(3·(10·plan+i)+f)` | u16, i<10 | `FUN_1408c5130` / `FUN_1408c5170` | outfield formation coordinates (x / y) per phase |
| `+0x31c + 12·(3·(11·plan+slot)+f)` | 12-byte rec | `FUN_1408c50e0` | per-slot per-phase record |
| `+0x844 + 4·(11·plan+slot)` | i32 | `FUN_1408c50a0` (default 0xff) | per-plan slot → player |
| `+0xef8 + 0x1c0·plan + 0x70·i` | 0x70-byte rec, i<4 | `FUN_1408c6430(S,i,plan)` (byte +0x30 read by checksum 0x142160a60) | **instruction records** (4 per plan) |
| `+0x143c + 4·k` | i32, k≤5 | `FUN_1408c5080` | 6 designated players (default 10; INF(med), O8) |
| `+0x1454 + 4·k` | i32, k<3 | `FUN_1408c5060` | designated set-piece takers (setpiece §11.6) |

## 2. The seven binary toggles: SubConcept gates (OBS, closes "which AI reads Attacking Style … Pressuring")

Every `match::ai::SubConcept*` class (46, vtables 0x1425c3d00–0x1425c4840, 7 slots each, `rtti_all.json`) has
vtable **slot 1 = its tactics gate**, a 4-instruction predicate `FUN_140a62180(ctx, side, k) == v` (OBS: each predicate's
raw `mov r8d,k; call 0x140a62180; cmp eax,1 / test eax,eax; sete al`). The value meaning follows from the concept
names (INF(high) for the 0/1 → UI label mapping; OBS for the gate itself):

| k | toggle (UI) | value → concepts enabled (gate VA) |
|---|---|---|
| 0 | Attacking Style | **0 Counter Attack** → CounterAfterGkCatch, SideCounter, FastCounter, ReverseSideCounter (`0x140543f40`); **1 Possession Game** → PossessionMfJoinDefenceLine (`0x14053d730`) |
| 1 | Build Up | **0 Long Pass** → LongPassFlick, LongPassLayOffAndToSide, LongPassLayOffAndBreakthroughToDefenceLine, LongPassLayOffAndShoot, EarlyCross (`0x14054c3d0`); **1 Short Pass** → ForwardPassAndLayOff, PassAndGoTriangle, PenetrateInsideOneTwo, OneTwoPassFromSideToCenter, OneTwoPassCutIn, SupportBackOneTwo (`0x14056fbb0`) |
| 2 | Attacking Area | **0 Wide** → SpaceRunToCenterForCreateSideSpace, SideChangeToWeakSide, SideOverlapAndSupport, SideOverlapAndSpaceRun, SideLongitudinalOneTwo, SideInnerlap (`0x14056b630`); **1 Centre** → ToCenterDecoySide, SpaceRunRelaySquarePass, DisorderByOneTwo, CreateSpaceOneTwo (`0x140558a10`) |
| 3 | Positioning | **1 Flexible** → ChangeTriangle, SideCrossRunning, SpaceRunToCreateSpaceForOneTouchPass, MoveDownMakeSpaceSide, DiagonalPostPlay (`0x14053de40`); 0 Maintain Formation → none of these |
| 4 | Defensive Style | **0 Frontline Pressure** → ForeCheckPress, ForeCheckLineDown, ForeCheckLineUp, TransitionChase (`0x140574910`); **1 All-out Defence** → RetreatBlockCreate, RetreatPressBack, RetreatBlockForwardPass, RetreatCrossBlockCreate (`0x1405767f0`) |
| 5 | Containment Area | **0 Middle** → NetDefenceInducementIntoTheCenter, NetDefenceCentralSurroundingPress, PassCut (`0x1405788d0`); **1 Wide** → SealOffInducementToTheSides, SealOffSideSurroundingPress (`0x140579650`) |
| 6 | Pressuring | **0 Aggressive** → DoublePress (`0x140579810`); 1 Conservative → no DoublePress |

Ungated in the sheet but **dead**: SideRunMakeSpace (slot 1 = `0x141863350` = `xor al,al; ret`, OBS) never passes
its gate. Concepts whose **slot 2 (start test) is that same false stub** can never be started by the selector (§2.1):
PossessionMfJoinDefenceLine, FastCounter, SideLongitudinalOneTwo, SupportBackOneTwo, DiagonalPostPlay,
RetreatPressBack, RetreatBlockForwardPass, RetreatCrossBlockCreate, NetDefenceCentralSurroundingPress, PassCut,
DoublePress (OBS slot lists, `rtti_all.json`). So on the selector path **Pressuring = Aggressive has no effect**
(its only concept, DoublePress, never starts), All-out Defence runs only RetreatBlockCreate, and Possession Game
starts no concept of its own. **Closed (OBS):** `FUN_1406424a0` (concept start) has one caller, the selector (0x140579ab4),
and the only byte write of 1 to `T+0xb6e8+id` is the selector's (0x140579ac4, `dispscan.py 0xb6e8 0xb718`): these 11
concepts are dead code in PES 2021.
Slot numbering: slot 0 = deleting dtor, slot 1 = gate (+0x08), 2 = start (+0x10), 3 = end (+0x18), 4 = on-stop
(+0x20), 5 = per-tick body (+0x28), 6 = interruptible (+0x30). (Team-plans §1 counts from 1, so its "slot 2" = slot 1.)

### 2.1 The concept selector `FUN_140579840(CM, ctx, T)` (OBS decompile + raw)

`CM` = team brain + 0x62b0 (ctor `FUN_1404efab0`, called from the brain ctor `FUN_1404f07a0`); `getConcept(CM, id)` =
`FUN_140579e10` (48 ids; 0x14 and 0x2f empty). Id → concept (OBS: `getConcept` offsets × ctor vtables; matches
TransitionsRE): 0 PossessionMfJoinDefenceLine, 1 ChangeTriangle, 2 CounterAfterGkCatch, 3 SideCounter,
4 ReverseSideCounter, 5 FastCounter, 6 LongPassFlick, 7 LongPassLayOffAndToSide, 8 LongPassLayOffAndBreakthrough…,
9 LongPassLayOffAndShoot, 0xa EarlyCross (INF: the one class the ctor builds through a sub-call), 0xb
OneTwoPassFromSideToCenter, 0xc PassAndGoTriangle, 0xd ForwardPassAndLayOff, 0xe OneTwoPassCutIn, 0xf SupportBackOneTwo,
0x10 PenetrateInsideOneTwo, 0x11 ToCenterDecoySide, 0x12 SpaceRunRelaySquarePass, 0x13 DisorderByOneTwo,
0x15 CreateSpaceOneTwo, 0x16 SideChangeToWeakSide, 0x17 SpaceRunToCenterForCreateSideSpace, 0x18 SideOverlapAndSupport,
0x19 SideOverlapAndSpaceRun, 0x1a SideLongitudinalOneTwo, 0x1b SideInnerlap, 0x1c SideRunMakeSpace,
0x1d SpaceRunToCreateSpaceForOneTouchPass, 0x1e MoveDownMakeSpaceSide, 0x1f SideCrossRunning, 0x20 DiagonalPostPlay,
0x21 ForeCheckPress, 0x22 ForeCheckLineDown, 0x23 ForeCheckLineUp, 0x24 TransitionChase, 0x25 RetreatBlockCreate,
0x26 RetreatPressBack, 0x27 RetreatBlockForwardPass, 0x28 RetreatCrossBlockCreate, 0x29 NetDefenceInducementIntoTheCenter,
0x2a NetDefenceCentralSurroundingPress, 0x2b PassCut, 0x2c SealOffInducementToTheSides, 0x2d SealOffSideSurroundingPress,
0x2e DoublePress. Per team tick:
```
// RECONSTRUCTION (OBS 0x140579840)
if regObj(0xf)+0x68 != 0: stop all 48                                   // registry index 15 flag
if !AIcap(difficulty, row 0x21) (FUN_140a604d0): return                  // difficulty table, carrier §7
if MI+0x116c == 2 and MI+0x11a8 < 2: stop all; return
own = T+0x255 (in possession, INF high)
running = first id with T+0xb6e8[id] set (only searched when own)
for id in 0..47:
  c = getConcept(id); if !c or reg(0xc4)[+8+4*id] < 1: continue         // per-concept enable table (registry 196)
  if own  and id > 0x20: stop(id); continue                             // ids <= 0x20 are attacking concepts
  if !own and id <= 0x20: stop(id); continue                            // ids > 0x20 are defending concepts
  if running not in {none, id} and !running.slot6(): continue           // one attacking concept at a time unless interruptible
  if !c.slot1(ctx) (tactics gate, §2) or anyMember(id) in state 0x25/0x2c: stop(id); continue   // FUN_14057a160: T+0xb748[s]==id and T+0xa2a0[s] ∈ {0x25,0x2c}
  if !T+0xb6e8[id]:
     if c.slot2(ctx,T): FUN_1406424a0(c,ctx); T+0xb6e8[id]=1; if own: stop all others (FUN_14057a1f0)
     else continue
  elif now - c.start > int(fps*0.1 + 0.5):                               // 0.1 = 0x142588200, 0.5 = 0x142565600
     T+0xb718[id] = byte c+0x10; if c.slot3(ctx): stop(id); continue
  c.slot5(ctx,T)                                                         // per-tick body
```
`stop(id)` = `FUN_14057a3e0`: clears `T+0xb6e8[id]`, calls slot 4, releases every player whose concept id
(`T+0xa2a0+0x14a8+4·slot`) = id (→ 0x30) and resets `T+0xb7d8..0xb824`. No RNG in the selector.

## 3. Attack level `T+0xb8a4` ("in-match mentality", 0..4) — writer and the CPU strategy decider (OBS)

**Only in-match writer:** `FUN_1404f8350(brain)` (OBS; other writes: T copy `FUN_1404161e0`, init `FUN_140a415a0` → 2).
Called every team tick from `FUN_1404f6c30` right after `T+0x134 = brain+0xbcc`.
```
// RECONSTRUCTION (OBS 0x1404f8350)
if team is pad-controlled (FUN_140a55cf0(G+0x180, side)):
   pad command 0x6c: L = clamp(L ± 1, 0, 4) (direction FUN_140a6e6b0); T+0xb8c0 = frame
else:
   want = {0:0, 1:1, 3:3, 4:4}.get(T+0x134, 2)
   t = T+0x210   // init 2; no in-match writer found (O4)
   if t != 2 and ((t < 2 and t <= want < 3) or (t > 2 and 2 <= want <= t)): want = t (extra phase/pad gates)
   if want != L and MI+0x116c != 1: want = L      // the level only changes while the ball is in play
   L = min(want, 4); if changed: T+0xb8c0 = frame
T+0xb8a2 = (L >= 4) ; T+0xb8a3 = (L <= 0)        // set right after, in FUN_1404f6c30
```
**The CPU strategy `T+0x134` = `R[3]`, `R = brain+0xbc0`, computed by `FUN_14051e970(R, ctx)`** (via `FUN_14051e910`,
which runs only when `MI+0x116c ∈ {1,3}`; `e970` returns at once when it is 1 → decisions happen in phase 3, INF(med)
a stoppage state). `R[0]` side, `R[1]` match mode, `R[5]`/`R[6]` minute limits, `R+8` LCG
`s = s·0xD2DD4D1D + 0x4513 & 0x7fffffff`, draw `int(s·2⁻³¹·n)` (`FUN_140a924e0`, slot 2). Goals = `FUN_1408c9df0(stats,5)`
(sum of period goals); `minute = clock/3600` (`FUN_140a6bd60`, INF med: clock in 1/60 s); strength = float
`G+0x2ad0 + 0xc404 + side·0x2804` (INF: team rating).

`R[1]` (once, `FUN_14051e7a0`): `T+0xa298` = 1 → **5 (always attacking)**, 2 → **6 (always defensive)**; else 1 if one
of 8 fixture records `setup+0x2308+0x38·i` matches (INF: two-leg tie), 2 if `setup+0xc31b`, 3/4 by competition flags
(`FUN_140a7f3a0/f360/f580`, INF: knockout variants), else **0 (normal match)**.

Mode 0, first half (`MI+0x1168 < 2`):
```
at minute 0: d = strength(own) - strength(opp)
   (w3, w1, w2) = d > 0.9 ? (7, 0, 3) : d < -0.9 ? (4, 4, 2) : (3, 2, 5)   // 0.9 = 0x14258c260
   (mode 5: w3 = 10 only; mode 6: w1 = 10 only)
   r = rand(w3+w1+w2):  r <= w3 → strategy 3, hold until minute 10+rand(36)
                        r <= w3+w1 → strategy 1, until minute 20+rand(26)
                        else 2
   (mode 5/6: until minute 100);  R[6] = 28 + rand(10)  (mode 5/6: rand(20))
before R[5]: strategy 0/1 and losing → 2 (mode 6: 1); strategy 3/4 and winning → 2 (mode 5: 3)
after R[5]: strategy 2
```
Mode 0, second half (`MI+0x1168` 2..3), `g` = goal difference (`FUN_14051e5b0`, aggregate-aware): before minute `R[6]`
a state machine steps the strategy toward 3 when behind (`g < 0`, `g < −1`) and toward 1 when ahead (mode 5 biased
up, mode 6 down); at/after `R[6]`: ahead → 1 (mode 6: 0, mode 5: 3), else 3 (mode 5: 4, mode 6: 2). Extra time
(`≥ 4`): `g ≤ 0` → 3 (mode 5: 4, mode 6: 2), else 1. Mode 5 never rests at 1 (→ 2), mode 6 never at 3 (→ 2). Modes 2–4
compare goals (plus first-leg goals `FUN_140a7f0f0`) and pick 1/2/3 by who leads (OBS code; mode labels INF).

Consumers of `L` (other reports): line ramp shift at L > 2 and −5 m at L = 2, width −6·ramp(L), team length
(defensive-shape §6–7), men in the box at set pieces `4 + min(L, 2)` (setpiece §11.6), concept start tests (e.g.
`FUN_14057a580`: allowed forward count `{0:2, 1–2:3, 3–4:4}[L]`, −2 with Possession Game).

## 4. Numeric sliders (5-byte block) — consumers (OBS call-site args, `args.py 0x1408c52e0`)

| k | identity | consumers | effect |
|---|---|---|---|
| 0 | **Support Range** 1..10 (live 9/4/8/1) | `FUN_1405e0c60` (support distance; callers 0x1405de2dc, 0x1405dea3a, 0x1405ebddc), `FUN_1405e05c0` (support-cell score), `FUN_1405db800` (change timestamp `+0x7e0`) | §4.1 |
| 1 | INF(low): a setting with no AI effect (live always 2, default 2) | `FUN_140642d00` only, **result discarded** (raw 0x140642d54 → next insn overwrites ebx with `[rbx+0xb8a4]`) | none |
| 2 | **Defensive Line** 1..10 | `FUN_1405f9eb0` (line, defensive-shape §6), `FUN_14062ade0`, `FUN_1404fc720` | §4.2 |
| 3 | **Compactness** 1..10 | `FUN_1405f9eb0` (`−0.33·(v−1)` m length), `FUN_14063f230` (±2.85 m width) | defensive-shape §6–7 |
| 4 | INF(low): no AI effect (live always 2) | only the UI/edit copier `FUN_141fc26e0` | none |

### 4.1 Support Range → off-ball support distance (OBS `FUN_1405e0c60`, raw constants checked)
```
// RECONSTRUCTION, metres; b = ball x own frame (dir·MA+0x2482c); F = FieldInfo
s    = clamp((SR - 1)/9, 0, 1)                                            // 9 = 0x14259d60c
deep = ramp(b; dir·MA[0x40+0x14·side]+10 → 1, dir·MA[0x44+0x14·opp]-10 → 0)
fwd  = 1 if |supporter bearing − attack dir| <= 45°, → 0 at 90°
back = 0 if |bearing − reverse dir| <= 45°, → 1 at 90°
d = 5 + 3·s + 7·deep + (BuildUp==Long ? 4 : 0)·fwd·deep
    + ramp(b; -0.5·F[1] → 1, F[6] → 0)·back
    + 3·ramp(|ball z|; F[7]-5 → 0, F[7]+5 → 1)·ramp(b; F[6]-10 → 0, F[6] → 1)
if supporter state value (FUN_140a57800()+4) > 1: d = min(d, 5)
flag A (FUN_1405e0010(·,0)): d += 5, floor 9 ; flag B (·,1): d -= 5, floor 6 ; arg7: d -= 10, floor 4 ; else floor 6
not in play: d = 8
```
(Skipped when the slot is in state 1 with a stored distance `T+0x2a0+0x40·slot`.) **Support Range adds 0–3 m** to the
supporter's distance from the ball; Build Up = Long Pass adds up to 4 m for forward-facing supporters in the own half.
`FUN_1405e05c0` (support-cell scorer) weights a grid layer value `v` by `2 + 3s` (Long Pass) or `0.5 + 1.5s` (Short
Pass), and the 0x21-layer difference by 5 with Long Pass (OBS raw 0x1405e08e5–0x1405e0996); the grid layers
(`MA+0x26020+0x62700·side`, `FUN_140a716c0`) are not identified (O5). TIKI_TAKA (id 6) in the same scorer: if no
team-mate stands within 8 m of the cell (5 m for the passer slot `+0x40c`), the score is multiplied by `1 − 0.7·t`,
`t` = the cell's own-line→opponent-line depth ramp (0.3 = 0x142590d98; raw 0x1405e0a13–0x1405e0b5e).

### 4.2 Defensive Line, other readers (OBS)
- `FUN_14062ade0(ctx, side)` → metres `lerp(a, b, DL/10)`: Defensive Style 0: `a = 20, b = F[0]` (52.56); Style 1:
  `a = −8, b = 22` (`0x14259be1c`, `0x1425c7ba4`, `0x1425c48c0`). Callers `FUN_140535424` (open-play defence
  dispatcher `FUN_140535c60` family) and `FUN_14062acf0`. INF(med): the press-start depth.
- `FUN_1404fc720(brain)` (team tick, caller 0x1404f7970): while counter `brain+0x6c48` > 0:
  `x = 7.5 − F[6]`, `x −= s·(x + 7.5)`, `s = (DL−1)/9`, three 0..1 ramps of `x`, `x ± 7.5` and `x + 15·brain+0x54` against
  `−F[0]` into per-side floats; sets `T+0xa570 = 1` (INF: a timed step-up window).

## 5. Binary toggles read outside the concepts (OBS sites from `args.py 0x140a62180`; effects read from raw at the site)

112 call sites in total; 57 are the SubConcept gates/bodies (§2). The rest, with the effect at the site:

| toggle | site (function) | effect (OBS raw) |
|---|---|---|
| Attacking Style | `FUN_14057a580` (counter-start helper) | Counter: allowed late-attacker count `{L0:2, L1–2:3, L3–4:4}`; Possession: −2. Counter window `MI+0x11a0 ≤ fps·6+0.5` (Possession 3 s, `0x14258c264`); `FUN_14057ab30`: same with 6 s / 5 s |
| Attacking Style | `FUN_1404fedc0` (team tick), `FUN_1405a3080`, `FUN_14060b590`, `FUN_140592cf0`, `FUN_1405938a0`, `FUN_1421197b0` (carrier plan ctor), `FUN_14066bde0` | branch selects (Possession → alternative constants / skip), not reduced (O6) |
| Build Up | `FUN_1405e0c60`, `FUN_1405e05c0` | §4.1 (Long: +4 m support distance, ×2–5 cell weight) |
| Build Up | `FUN_14044d110` | `+0x2af8 = (BuildUp == 1)` (Short flag copied into a 0x2af8 field) |
| Build Up | `FUN_1404fbca0`, `FUN_140509f60`, `FUN_14066bde0`, `FUN_1405e9f50`, run selectors `FUN_140606cd0`, `FUN_14060bdd0`, `FUN_14060cb50`, `FUN_14065ff20`, `FUN_140660220`, `FUN_140662a10`, `FUN_140662d60`, `FUN_14068c050`, carrier `FUN_142111db0` | Short-pass gates on run/support choosers (O6) |
| Attacking Area | `FUN_1405d8b70` (two sites, compares `|+0x704|`, `|+0x708|`), `FUN_14060a8d0` (`|+0x564|` = player z), `FUN_140680980`, `FUN_14067d6d0`, `FUN_14099d650`, `FUN_142132020` | Centre/Wide lane tests on lateral position (O6) |
| Positioning | `FUN_1405d71b0`, `FUN_1405dc350`, `FUN_1405deee0`, `FUN_140605570`, `FUN_1406612b0`, `FUN_140664ee0`, `FUN_1406725f0` | Flexible: free-position branches (O6) |
| Defensive Style | `FUN_1405f9eb0` | line offset `ForeCheck −9` vs `Retreat −11` + slider ramp; length 46 vs 42 (defensive-shape §6) |
| Defensive Style | `FUN_1405fa990` (rear/mid bands) | All-out: band constant 40 m; Frontline: ball-relative (30 m) |
| Defensive Style | `FUN_14062ade0` | §4.2 press depth `lerp(20, 52.56)` vs `lerp(−8, 22)` by DL |
| Defensive Style | `FUN_140634470` (PRESS writer) | Frontline only: extra press-permission test against `FUN_14063fdc0`'s point |
| Defensive Style | `FUN_140635ab0` (PASS_COURSE_CUT) | All-out (and not case 2): a 3.0 m term replaces the computed one |
| Defensive Style | `FUN_1405325e0` (mark pass) | All-out drops the `+[rbx+8]` addend in the pairing score |
| Defensive Style | `FUN_140528380`, `FUN_1406216a0`, `FUN_1406295a0`, `FUN_14062acf0` | not reduced (O6) |
| Containment | `FUN_140632850` | Wide: returns false when the player's `|z|` (`+0x564`) < `F[7]` (20.16, the box half-width) |
| Containment | `FUN_1406350c0` | Middle / Wide pick different ball-`|z|` vs `F[7]` branches |
| Containment | `FUN_140529490` | Middle: returns a constant (`xmm9`) instead of the computed value |
| Containment | `FUN_14062b4c0`, `FUN_140682540` | Wide: extra block (state `T+0x6ec`); `682540` also needs `T+0xb8a3 == 0` |
| Pressuring | `FUN_140623750` (mark spot) | **Conservative: +2.0 m** to the spot distance (`0x142565604`) |
| Pressuring | `FUN_140625e00` | Conservative: fixed 1.5 / 5.0 (`0x14259bddc`, `0x14259222c`) and 3.0 instead of computed values |
| Pressuring | `FUN_140971ba0` | Aggressive: count +1 (and +2 more when `T+0xa298 == 3`) |
| Pressuring | `FUN_1406295a0`, `FUN_1409677c0`, `FUN_14096a4f0` | not reduced (O6) |

## 6. Advanced and individual instructions

### 6.1 Storage (OBS)
- **Sheet:** four 0x70-byte records per plan at `S + 0xef8 + 0x1c0·plan + 0x70·i` (`FUN_1408c6430(S,i,plan)`; ctor
  `FUN_1408c5390`; setter `FUN_140a42600(T, i, id, slot, p5, plan, p7)`, enable `FUN_140a42880`, clear `FUN_140a42430`,
  backup `FUN_140a428e0`): `+0x00` id (0x11 refused); `+0x04..+0x2b` u8 per own slot (ids 8, 0xa, 0xf: the chosen
  players, several allowed); `+0x2c` opponent index 0..21 (id 0xe only, else 0xff); `+0x30` u8 **enabled**; `+0x34` p5;
  `+0x3c..+0x68` backup copy; `+0x6c` u8 p7 (`FUN_140a41bc0`). Live: team 0 plan 0 = {8 with slot 10, 4, 0xd, 0xb},
  team 1 = {2, 6, 0xd}, all enabled; plans 1–2 empty.
- **Active in match:** `MA = *(ctx+0x18)`, `MA + 0x25cf8 + 4·(4·side + i)` = id, `MA + 0x25d38 + …` = target slot
  (`0xff` = team-wide). Test `FUN_140a63950(ctx, side, id, slot)` (298 call sites); helpers `FUN_140a62360` (id 3),
  `FUN_140a62440` (id 9), `FUN_140a63360/63500` (per-player eligibility), `FUN_140a64410` (ids 0x10 + 0xf).
  Static writer not found (no displacement write to 0x25cf8/0x25d38 in the image). **Live (frozen frame 18051):
  MA = team 0 {8 @ slot 10, 4, —, —}, team 1 {2, 6, —, —}** — only the first two (attacking) records are active; the
  enabled defensive records (0xd, 0xb) are not. INF(med): instructions are switched on in match (CPU logic or pad
  "quick instruction"), not copied wholesale; the gated sampler (§7) logs every change of the 8 slots (O7).

### 6.2 Enum (INF(high)) — engine names from `.rdata` 0x14261433c…; index = position after `NONE`

`0 NONE, 1 OPEN_WIDE, 2 FALSE_9, 3 FALSE_SB, 4 BOTH_SB_UP, 5 FLOW_SIDE, 6 TIKI_TAKA, 7 TARGET_CROSS, 8 DEFENSIVE,
9 FALSE_WG, 10 KEEP_LANE, 11 CAUTION_CROSS, 12 CAUTION_THROUGH_PASS, 13 GEGEN_PRESS, 14 MARK_HARD, 15 READY_COUNTER,
16 WING_BACK, 17 DEFENCE_MAX` (sentinel: the test rejects id ≥ 0x11). Evidence: (a) the tester bound `< 0x11`; (b)
**exactly ids 8, 0xa, 0xe, 0xf are always tested with a player slot** (`mov r9d, reg`), all others with 0xff — those
four are DEFENSIVE, KEEP_LANE, MARK_HARD, READY_COUNTER = the per-player instructions (UI: Defensive, Anchoring?,
Tight Marking, Counter Target); (c) id 0xd is read by ForeCheckPress/TransitionChase/line code (Gegenpress);
(d) id 0xf excludes a player from the id-0x10 wing-back drop (`FUN_140a64410`). The order with OPEN_WIDE = 0
would make ids 8/0xa/0xe/0xf = FALSE_WG/CAUTION_CROSS/READY_COUNTER/WING_BACK — killed by (b).
UI labels (INF(med)): OPEN_WIDE Hug the Touchline, FALSE_9 False No. 9, FALSE_SB False Full-backs, BOTH_SB_UP
Attacking Full-backs, FLOW_SIDE Wing Rotation, TIKI_TAKA Tiki-Taka, TARGET_CROSS Centring Targets, CAUTION_CROSS
Swarm the Box, CAUTION_THROUGH_PASS Deep Defensive Line, GEGEN_PRESS Gegenpress, WING_BACK (back-five drop).

### 6.3 Consumers (OBS sites, `a63950s.txt`; site counts) and the effects decoded so far

| id | sites | readers (function) | effect (OBS unless marked) |
|---|---|---|---|
| 1 OPEN_WIDE | 9 | `FUN_140596010`, `1405a9820`, `1405e6e80`, `1405f3530`, `1405f5a10`, `14061ccc0`, `140662d60`, `14067bc10`, `140680980` | lateral-position tests on support/run cells (|z| vs ball z) — not reduced |
| 2 FALSE_9 | 5 | `1405d8b70`, `1405dc350` ×3, `1405e2c90` | `1405dc350`: −10 to a score when `FUN_140a63680` holds; `1405e2c90`: clears `+0x724[slot]` = 0xff |
| 3 FALSE_SB | 8 | `1405d8b70` ×2, `1405dc350` ×2, `1405e6e80`, `14060d960`, `140641690`, `140994090` | eligible players = `FUN_140a62360` (registered pos `T+0x8d28[slot]` 0x13/0x14, or flag `T+0x8c2a` with `T+0x8ce8` 0xb/0xc) |
| 4 BOTH_SB_UP | 23 | `14053d600` (PossessionMfJoinDefenceLine end test), `1405d1810` ×2, `1405d8b70` ×3, `1405dc350` ×2, `1405df1f0` (8 m), `1405e1f60`, `1405e2c90` (classes 5/8), `1405e6e80` ×5, `140640cc0`, `14067b540`, `14067ecc0`, `1406805a0`, `140681a20`, `140958d00`, `140a4f520` | full-back support/overlap permissions — not reduced |
| 5 FLOW_SIDE | 11 | `140545e50`, `1405465b0` (LongPassFlick; `fps·0.2+0.5` frames), `14055ed70` (SideRunMakeSpace start: false), `1405d1e60`, `1405d4b00` (sets `+0x19f[slot]`, `+0x8024[slot]`), `1405d8b70`, `1405dc350`, `1405de950`, `140640cc0`, `1406697b0`, `140994090` | wing-rotation swap flags — not reduced |
| 6 TIKI_TAKA | 26 | support scorers `1405d3550…1405e9f50`, `140642d00`, carrier `1421174f0` (receiver difficulty: skips the 80/99 stat window), `142124cc0` (3 m), `140556c40` (ToCenterDecoySide start: false) | §4.1 (no-team-mate-within-8 m cells ×(1−0.7t)) |
| 7 TARGET_CROSS | 14 | `1405a8290`, `1405f5a10` (18 m), `14061b110`, `14061ccc0` ×2 (level ≥ 3), `140642d00` (→ 3 forwards at L > 2 with ball x > 16), `140643630`, `140643a30`, `140645ba0`, `140646b30`, `14066f080`, `1406725f0`, carrier `142123750`, `14212a2d0` | box-entry runner count `FUN_140642d00` returns 3 instead of 2 |
| 8 DEFENSIVE (per player) | 33 | concept bodies `1405617b0`, `140567400`, `1405694c0`, `14056b350` (SideInnerlap: false), `140593430`, run selectors `14061c120`, `14061ccc0`, `140640cc0`, `140641690`, `140642810`, `140644320`, `14067ecc0`, set-piece ordinal `14051b8b0`, support `1405d1e60…1405fc860` | OBS: at most sites a set flag skips the candidate (`test al; jne skip` / `je` → `xor al,al`); INF(high): the slot is excluded from attacking runs and concepts |
| 9 FALSE_WG | 5 | `1405d8b70`, `1405dc350` ×2 (25 m), `1405e6e80`, `14067ecc0` | eligible = `FUN_140a62440` (pos 0x17 or role 0xf, class 5/8) |
| 0xa KEEP_LANE (per player) | 14 | run selectors `140596470`, `140607bc0`, `140608570`, `1406087a0`, `14060ca60`, `14060d4d0`, `140613910`, `140615770`, `140615ac0`; support `1405dc350`, `1405df1f0` (2.5 m), `1405e6e80` (±2.5 m band); `140680980`, `140695800` | OBS: run selectors return false when set; ±2.5 m support band. INF(med): "Anchoring" — no off-ball runs, holds position |
| 0xb CAUTION_CROSS | 13 | `1405027a0`, `140527e10`, `14052a820`, `14052bda0` (625 = 25², 30 m), `1405fa990` (40 m band), `1406216a0`, `140622a80`, `140623750` ×2, `140625e00`, `140628400` (mark-spot refresh index capped at 2), `140688300` (width: ball-side target forced 1.0) | box-protection variants of line/width/marking |
| 0xc CAUTION_THROUGH_PASS | 13 | `1405027a0`, `14052a820`, `140533d10` (retreat flag `T+0xc12a`), `1406216a0`, `140622a80` (+5 m, +2 m, 6 m), `140623750` ×2 (blend to 0.8), `140625e00`, `1406276b0` (5 m), `140628400`, `1406855f0`, `14096dfc0` | deeper cover/marking offsets |
| 0xd GEGEN_PRESS | 39 | `1405747f0` (**ForeCheckPress start returns false**, raw 0x1405748d0), `140575510` (**TransitionChase start returns false too**, raw 0x1405755df → 0x14057590f `xor al,al`), `1405f9eb0` (**line deep end +5 m**: `deep = 1 − F[6] + 5`), `140632ae0` (returns 7), `140635ab0` (PASS_COURSE_CUT: 30 m radii, count 5), `1405a58b0` (counter helper: false unless set), marking `14052eab0`, `1405325e0`, `140623750`, `140633940` | OBS: while 13 is armed neither ForeCheckPress nor TransitionChase can start (both need it **not** armed; agrees with TransitionsRE). Live, 13 is armed only for 0.7–5.7 s after a loss (§7.1), so these two concepts start outside that window; the counter-press itself is carried by the per-player state code (PASS_COURSE_CUT, marking sites), not by a concept |
| 0xe MARK_HARD (per player) | 40 | mark passes `140526890`, `140528380` ×3, `140529330`, `14052a820`, `14052d350`, `14052eab0` ×2, `140530320`, `1405325e0` ×3, mark spot `140623750` ×2, `140625e00`, `1406276b0` ×2, `140633350` ×2, `140633940`, `140634470`, `140634ac0`, `140635ab0`, `140578260`, `140578f80`, UI marker `14147bb60` | OBS: tested with the marker slot in every mark pass and the mark spot; INF(med): tight marking of the assigned opponent — not reduced |
| 0xf READY_COUNTER (per player) | 21 | lines `140682770` ×4 (+10 m at 0x14068346a; 1.0), `1406850e0`, zone `1405cdf30`, `1405ce150`, `1405cf310`, `1405f0180`, `14062d9a0`, `14062e240`, `140633940`, `140635ab0`, `140531490`, `1405bced0`, `14092fad0`, `1409563d0` (25 m), `1409589f0`, `140a41cc0` ×2 | OBS: +10 m on the slot's line value; excluded from the WING_BACK drop. INF(high): Counter Target stays forward and skips defensive duties |
| 0x10 WING_BACK | 3 | zone template `1405ce150`, `140684390`, `140685d70` | OBS `FUN_140a64410`: a wide player (class `T+0x4c00` 5/8, not READY_COUNTER) qualifies when fewer than 5 team-mates are class 1–2 and no same-side class-5 player is already dropping; INF(med): the wide midfielder drops into a back five |

## 7. Live (read-only `/proc/pid/mem`)

**Done (OBS, frozen frame 18051, phase 2, MODE 7):** sheet `T+0x8e08`, both teams (`live0.py`, `live2.py`):

| | plan | 5-byte (SR, k1, DL, Comp, k4) plan 0 / 1 / 2 | 7-byte (AttSt, Build, Area, Pos, DefSt, Cont, Press) plan 0 / 1 / 2 | `T+0xb8a4` | `T+0x134` | `T+0x210` |
|---|---|---|---|---|---|---|
| team 0 | 0 | 9,2,4,4,2 / 4,2,4,7,2 / 4,2,4,7,2 | 1,0,1,0,0,0,1 / 0,1,0,1,1,0,0 / 0,0,0,1,1,0,0 | 2 | 3 | 2 |
| team 1 | 0 | 8,2,8,9,2 / 1,2,1,1,2 / 1,2,1,1,2 | 0,1,1,1,1,0,0 / 1,1,1,1,0,0,0 / 1,1,1,1,0,0,0 | 2 | 3 | 2 |

k1 and k4 read 2 in all six plans (consistent with "no AI effect"). Both strategies are 3 while the level is 2 —
consistent with §3: the level follows the strategy only while `MI+0x116c == 1` (the game is stopped at phase 2).

**Live consumer check already on record (same match, DefensiveShape2RE, defensive-shape §14):** team 0 = Defensive
Style 0 / Defensive Line 4, team 1 = Style 1 / Line 8 — exactly the bytes read here at 7-byte k4 and 5-byte k2. The
style-1 team held its line a constant 4.18 m deeper than the style-0 formula, matching the Retreat branch
(`5·sin(90°·t) − 11`), so the k4 → Defensive Style and k2 → Defensive Line identities are behaviourally confirmed
(OBS live, theirs; byte read OBS, here).

### 7.1 Live run (OBS, second half resumed; `tacsample.py` 855 samples frames 19267–21701, `tacsample2.py` 9600 samples
frames 23050–36477, 20 Hz, 7298 in-play samples; analysis `tacanalyse2.py`)

Sheets (current plan 0): team 0 toggles (AttSt, Build, Area, Pos, DefSt, Cont, Press) = 1,0,1,0,0,0,1 = Possession,
Long, Centre, Maintain, Frontline, Middle, Conservative; team 1 = 0,1,1,1,1,0,0 = Counter, Short, Centre, Flexible,
All-out, Middle, Aggressive.

| check | prediction | result |
|---|---|---|
| **1. Toggle gates (§2)** | every running concept satisfies its gate; never a dead id; attack ids only in possession | **0 gate violations** in 7298 samples; dead ids seen: none; 1 sample with an attacking concept on a possession-change frame (sampling skew). Team 0 ran LongPassFlick, LongPassLayOff…Breakthrough, LongPassLayOffAndShoot (Long build-up), DisorderByOneTwo (Centre), ForeCheckPress/ForeCheckLineDown (Frontline). Team 1 ran ChangeTriangle (Flexible), ForwardPassAndLayOff (Short), DisorderByOneTwo (Centre), SpaceRunToCreateSpaceForOneTouchPass (Flexible), **RetreatBlockCreate 4500 of its defending samples** (All-out). Neither team ever ran a concept the other team's sheet allows and its own forbids. |
| **2. Defensive Style → defending concept family** | Frontline team: ForeCheck* only; All-out team: RetreatBlockCreate only | as predicted (team 0: 0x21/0x22, 451 samples; team 1: 0x25 only) |
| **3. Attack level** | `T+0xb8a4` follows the strategy only in the CPU branch | **L = 2 for both teams throughout**, while strategy `T+0x134` was 3 and team 1's changed 3 → 1 during play. Both sides have a pad slot attached (`G+0x180`, slot 0 → side 0, slot 1 → side 1; `padchk.py`), so `FUN_1404f8350` takes the pad branch and, with sheet byte `S+0x1466 = 0`, the level moves only on pad command 0x6c. OBS: in this AI-vs-AI (watch) setup the CPU strategy decider runs but does **not** drive the level. |
| **4. Instruction activation (O7)** | — | Attacking instructions are on whenever in possession (team 1 TIKI_TAKA off for one 88-sample stretch); **defensive instructions are on only out of possession**: GEGEN_PRESS (13) switches on **0–3 frames after the possession loss** (8 episodes, both teams), lasts 40–323 frames (0.7–5.7 s at 57 fps) and then switches off; team 0's CAUTION_CROSS (11) episodes 157–679 frames, never overlapping 13. INF(med): GEGEN_PRESS = a ≤ ~5 s counter-press window after a loss; the switch-over to 11 follows. |
| **5. GEGEN_PRESS vetoes the ForeCheckPress start** (`0x1405748d0`) | no ForeCheckPress start with 13 active | 2 starts seen; 1 with 13 off, 1 on the loss frame itself with 13 already set in the same sample (activation and start inside one 3-frame sample window — inconclusive) |

Commands to repeat: `sudo python3 tacsample2.py OUT.jsonl 480` (no gate; MA address hard-coded for this match — re-find
with the offside report's `find_ma7.py` in a new match), then `python3 tacanalyse2.py OUT.jsonl`.

**(Superseded by §7.1.)** Original plan: `tacsample.py` waits for `pes_state.py` = OPEN_PLAY and then records 600 s at
20 Hz. Checks it runs (`tacanalyse.py live_tac.jsonl`):
1. **Gate check (§2):** every running concept id (`T+0xb6e8`) of each team must satisfy its sheet gate for the current
   plan; predicted from the live sheet: team 0 (Possession, Long, Centre, Maintain, Frontline, Middle, Conservative) can
   only run ids 6–0xa, 0x11–0x13, 0x15 in possession and 0x21–0x24, 0x29 out of it; team 1 (Counter, Short, Centre,
   Flexible, All-out, Middle, Aggressive) only 1–4, 0xb–0xe, 0x10–0x13, 0x15, 0x1d–0x1f and 0x25, 0x29. Ids with the
   false start stub (0, 5, 0xf, 0x1a, 0x20, 0x26–0x28, 0x2a, 0x2b, 0x2e) must never appear.
2. **Attack-level check (§3):** `T+0xb8a4` must equal the `T+0x134` mapping (0,1,3,4 → same; 2 → 2) whenever it changes,
   and change only on in-play frames.
3. **Instruction activation (O7):** every change of `MA+0x25cf8/0x25d38` with possession and level.

Commands if the agent is gone (scratch dir = the TacticsRE scratch dir): `sudo python3 tacsample.py OUT.jsonl 600`
(self-gating on OPEN_PLAY), then `python3 tacanalyse.py OUT.jsonl` (needs `concept_ids.json` beside it).

## 8. GF code each finding replaces

| finding | GF code (src/sim/onthepitch) |
|---|---|
| §1 sheet: 5 sliders (SR, DL, Compactness live; k1/k4 inert) + 7 toggles per plan, 3 plans, 4 instruction records | `teamAIcontroller.cpp` `baseTeamTactics` / `teamTacticsModMultipliers` floats (`position_*_factor`, `team_pressure`, `counter_attack`, `support_distance`, `dribble_*`) and the tactics XML → replace by the byte sheet; the 0..1 slider pipeline has no PES counterpart |
| §2 toggles gate 46 named team concepts; selector §2.1 | `teamphilosophy.cpp` (Balanced/Gegenpressing/TikiTaka/ParkTheBus presets, `PressesOnPossessionLoss`, `PrefersShortPassing`, `GetPassErrorMultiplier`) → delete; philosophy = a toggle set + instructions. `TeamAIController` pressure/counter branches → concept selector |
| §3 attack level 0..4 and the CPU strategy decider (strength-weighted first-half roll, score/minute steps, mode 5/6 teams) | `matchmentality.cpp` (`Decide`: Desperation at 80′, TimeWasting at 85′, `GetDesperationShape`, corner-flag time wasting) → replace; PES has no corner-sheltering or formation override, only the level. `TeamInstructions::e_Mentality` five rungs = `T+0xb8a4` 0..4 (keep the enum, drive it from §3) |
| §4.1 Support Range → `5 + 3·(SR−1)/9 + …` m support distance | `support_distance` tactic and its users in `teamAIcontroller.cpp` / Eliza support positioning |
| §4.2, defensive-shape §6–7: Defensive Line, Compactness, Defensive Style → line/length/width | `position_defense_depth_factor`, `position_defense_width_factor`, `TeamInstructions::e_Instruction_DeepDefensiveLine` offsets |
| §6 instructions (17-id enum, per-player DEFENSIVE / KEEP_LANE / MARK_HARD / READY_COUNTER) | `teaminstructions.cpp` 7-bit mask (FrontlinePressure, DeepDefensiveLine, AggressiveDefence, HugTheTouchline, CentreShading, TikiTaka, LongBallCounter) and `Apply` offsets → replace with the 4-record/plan structure; FrontlinePressure/AggressiveDefence/CentreShading/LongBallCounter are toggles (§2), not instructions |

## 9. Killed hypotheses
- "Defensive Style is sheet byte `FUN_1408c52e0` k" — it is the 7-byte block k4 via `FUN_1408c5030` (raw 0x140a621b7).
- "Attack level `T+0xb8a4` = team strategy `T+0x134` mapped" (setpiece §11.6) as a direct copy — the copy is gated on
  play phase and on `T+0x210`; live: strategy 3, level 2.
- "SubConcept slot 1 is a shared fore-check evaluator family" (team-plans §1) — slot 1 is the per-concept tactics gate;
  the fore-check family is slot 2 (start test) / slot 3.
- "Advanced-instruction enum starts at OPEN_WIDE = 0" — the per-player slot pattern puts NONE at 0 (§6.2).
- "`0x141863350` is a return-true stub" — it is `xor al,al; ret` (false): SideRunMakeSpace is disabled and 11 concepts
  cannot start from the selector.
- "The 5-byte block k1 is Number of players in the box" — its only reader discards the value; men in the box at set
  pieces come from the attack level (`4 + min(L,2)`), and in play `FUN_140642d00` returns 2/3/4 from level,
  TARGET_CROSS and `T+0xb8a2`.
- "`FUN_140480910` writes `T+0x210`" — it writes another struct's `+0x210` (pad command buffer).
- "ForeCheckPress starts when GEGEN_PRESS is set" (team-plans §4.1, its [P4]) — inverted: raw 0x1405748d0 returns false
  when `FUN_140a63950(ctx, side, 0xd, 0xff)` is true; same for TransitionChase (0x1405755df).
- "GEGEN_PRESS hands the press to TransitionChase" (this report's first draft) — TransitionChase is vetoed by it as well.

## 10. Open list (exact next step each)
- ~~O1 live checks~~ — done (§7.1). Remaining live item: Support Range vs off-ball distance needs teams with different
  SR (this match 9 vs 8, a 0.33 m predicted difference — below what positions can show).
- ~~O2 Defensive Line / Style byte identity~~ — closed by the cross-check in §7. Compactness (k3) still has no live
  behaviour test (both teams differ, 4 vs 9: compare back-to-forward line length `S+0x7b38 − S+0x7b14` per team).
- ~~O3 concept starts outside the selector~~ — closed (§2: selector is the only starter).
- **O4 `T+0x210` writer** — no displacement write in the team code; try the T copy paths and `FUN_140a64e20`.
- **O5 support grid layers** — decompile `FUN_140a716c0` and the grid builder at `MA+0x26020` to name layers 0x21/0x22.
- **O6 non-concept toggle sites marked "not reduced"** in §5 and the "not reduced" instruction rows in §6.3.
- **O7 instruction activation** — behaviour measured (§7.1); the writer of `MA+0x25cf8` is still unknown (no
  displacement write; perf watchpoints fail with ENOSPC). Next: value-scan the image for `lea reg,[reg+0x25cf8]`
  and the 0x25d38 slot array stores, then decompile around the possession-change handler.
- **O8 `+0x143c` int[6]** (live 8,7,10,1,7,10 / 8,4,8,8,2,1; default 10; accessor `FUN_1408c5080`; readers
  `FUN_14048ddf0`, `1404a8620`, `1404a8ad0`, `1404a90d0` (k 5, kicker code), `140520930` (swaps the value on a
  substitution via `FUN_1408c5cf0`), `140521090` (k 1, 2 as fallbacks), `140a1a1a0`, `141466650`). Values go through
  `FUN_140a62ee0` → on-pitch slot: INF(med) the six designated players (captain, penalty and free-kick takers, corner
  takers). Not a tactic; next: decompile `FUN_140a62ee0` and label each k from its caller's restart MODE.
- ~~O9 `FUN_14057a160`~~ — closed (raw 0x14057a160–0x14057a1db: true iff a player assigned to the concept is in action
  state 0x25 or 0x2c).

## 11. Ledger
| ID | Claim | Conf. | Evidence |
|---|---|---|---|
| EV-TAC-0001 | Sheet `T+0x8e08`: plan u32, 5-byte sliders `+4+5p`, 7-byte toggles `+0x13+7p`, 3 plans | Confirmed (code + live) | 0x1408c52e0, 0x1408c5030, 0x1408c5390, `live0.py` |
| EV-TAC-0002 | SubConcept slot 1 = toggle gate; map of §2 | Confirmed (code) | vtables 0x1425c3d00–0x1425c4840, predicates §2 |
| EV-TAC-0003 | Concept selector rules §2.1 | High (decompile, raw spot-checked) | 0x140579840, 0x140579e10, 0x14057a3e0 |
| EV-TAC-0004 | 11 concepts have a false start stub; SideRunMakeSpace gate false | Confirmed (code) | 0x141863350 = `xor al,al; ret` |
| EV-TAC-0005 | Only in-play writer of `T+0xb8a4` = `FUN_1404f8350`; level changes only when `MI+0x116c == 1` | Confirmed (disp scan + decompile) | `disp_b8a4.txt`, 0x1404f8350 |
| EV-TAC-0006 | CPU strategy decider rules §3 (weights 7/0/3, 4/4/2, 3/2/5 at ±0.9; LCG 0xD2DD4D1D) | High (decompile) | 0x14051e970, 0x14051e7a0, 0x140a924e0 |
| EV-TAC-0007 | Support distance `5 + 3s + 7·deep + …`, floor 6 | High (raw constants) | 0x1405e0c60 |
| EV-TAC-0008 | 5-byte k1, k4 have no AI effect | High (all call sites) | `args.py 0x1408c52e0`, 0x140642d54 |
| EV-TAC-0009 | Instruction enum NONE=0 … WING_BACK=16, DEFENCE_MAX sentinel | High | slot-argument pattern, 0x140a63950 bound |
| EV-TAC-0010 | GEGEN_PRESS: ForeCheckPress start false; line deep end +5 m | Confirmed (raw) | 0x1405748d0, 0x1405fa166 |
| EV-TAC-0011 | Pressuring Conservative: mark spot +2 m | Confirmed (raw) | 0x140624996 |
| EV-TAC-0012 | Live MA active instructions = first two records only | Confirmed (one frozen frame) | `live1.py` |
| EV-TAC-0013 | Toggle gates hold live: 0 violations, no dead concept ever runs | Confirmed (live, 7298 samples) | `live_tac1/2.jsonl`, `tacanalyse2.py` |
| EV-TAC-0014 | Attack level stays 2 in watch mode (pad branch); strategy changes without effect | Confirmed (live) | `live_tac2.jsonl`, `padchk.py` |
| EV-TAC-0015 | Defensive instructions active only out of possession; GEGEN_PRESS on within 0–3 frames of a loss, 0.7–5.7 s | Confirmed (live, 8 episodes) | `live_tac2.jsonl` |
