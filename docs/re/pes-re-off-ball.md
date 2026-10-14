# PES 2021 off-ball attacking movement: run arbitration, run timing, support positioning (OffBallRE)

Read-only RE of the PESLive image (`~/ghidra-projects/scratch/Unpacker/`, `dec.sh` + `annot.py`, disk `pdis.py`). Scratch:
the OffBallRE scratch dir (`b1a..b5a.c` decompiles, `scan.py`, `callers.py`, `regsites.py`, `statesites.py`,
`ripv.py`, `o_*.o` constant blobs). Tags: **OBS** = decompile/raw bytes/data read; **INF(x)** = inference with confidence.
"fps" = `FUN_141e532d0()` = `[0x14351ae54]` = **54.0** (OBS; it is the game speed, **not** an RNG; every "rng" in
`pes-re-attack-decisions.md` §3 that was decoded from `141e532d0`/`141e532e0` is this constant).

## 0. Corrections to earlier reports (read first)

| earlier claim | correction | evidence |
|---|---|---|
| team-plans §3.1: vtable slot 4 is "the gate: can this player start this run now" | the slot at **+0x18** is the **stop test**: true → the runner is released. The "cooldown" `entry[8]−entry[4] < fps·0.2+0.5 → return false` is a **minimum run time** (≈11 f) before the run may be stopped | manager tick `FUN_140525530` calls `vt+0x18` only on slots already in that state and calls `FUN_140a40820` (release) when it returns true (OBS b2a.c:121–132) |
| team-plans §3.1 table slots 5/6 = "scorer/executor" | for `ActionSelectorScore` subclasses: **+0x10 select (shared `0x140602060`), +0x18 stop, +0x20 candidate mask, +0x28 per-candidate score, +0x30 target builder** | RTTI vtables (`rtti.py cls ActionSelector`), call sites in `0x140602060` |
| attack-decisions §3.7 "Score `0x14061a640`" is a run | `ActionSelectorScore` is the **base class** (sorted-score selector) of Chance/Counter/SecondLine/LineBreak/PullAway/Diagonal/PostPlay. `0x14061a640` is **CenteringGet's stop test**, `0x14061ccc0` its select | RTTI bases + vtable `0x1425cde88` |
| attack-decisions §3.6 "`0x14061e810` = PullAway gate" | it is **GoalGet's stop test** (vtable `0x1425c6318`: select `0x14061f110`, stop `0x14061e810`); PullAway's stop is `0x14061e1e0` | RTTI |
| attack-decisions §3.8 / team-plans "Overlap executor `0x1406013d0`" | Overlap is a 4-slot selector: `0x140601580` = select (start), `0x1406013d0` = **stop test** | RTTI vtable `0x1425c6148` |
| attack-decisions §3.1 "Chance: no max-score, first-listed candidate" | Chance goes through the shared sort: score = **100 + s·x** of the runner (most advanced eligible mate first); special kind 0xe uses `FUN_1406065b0` | `0x140606060` tail `0x140606575–0x140606584` (OBS disasm) |
| attack-decisions §3.2 "accept iff score+awareness < \|Δx\|" | inverted: that branch **rejects** (`goto LAB_140607834` = loop continue). The 15/21/24/26/37 m value is the **maximum longitudinal distance to the ball** for a Chance candidate; awareness widens it | runs.c:618–625 |
| attack-decisions §3.10 "rng-frame cooldowns", "PullAway double frame-gate rng/2, rng·2" | all are fps multiples (54); see §2 | `141e532d0` returns `[0x14351ae54]` = 54.0 |

## 1. Run selection: the arbitration (OBS)

### 1.1 Objects and order
`match::ai::ActionSelectorManager` lives at team-AI object `+0xf78` (ctor `FUN_1404f07a0` → `FUN_140524ae0`); its
working base `M = team-AI+0xf80`. Selectors are embedded and **registered in this order** (`FUN_140525160`, table
`M+0x2aa0` = 126 × {action id, ptr}, count `M+0x17ac`):

| # | selector | object | action id (`T+0xa2a0` value) | select | stop (+0x18) | mask (+0x20) | score (+0x28) | target (+0x30) |
|---|---|---|---|---|---|---|---|---|
| 1 | Overlap | `M+0x1818` | 0x27 OVERLAP_SB | `0x140601580` | `0x1406013d0` | — | — | — |
| 2 | ChanceSpaceRun | `M+0x1820` | 0x1e SPACE_RUN_CHANCE | `0x140602060` | `0x140607a60` | `0x140606cd0` | `0x140606060` | `0x140602600` |
| 3 | CounterSpaceRun | `M+0x1ae8` | 0x1f SPACE_RUN_COUNTER | shared | `0x14060b410` | `0x14060ab00` | `0x14060a8d0` | none |
| 4 | SecondLineSpaceRun | `M+0x1d78` | 0x1d SPACE_RUN_SECOND_LINE | shared | `0x1406118e0` | `0x14060ebf0` | `0x14060d210` | `0x14060c390` |
| 5 | LineBreak | `M+0x2028` | 0x20 LINE_BREAK | shared | `0x140612830` | `0x140421f40` (returns 0) | `0x1406127a0` | none |
| 6 | DiagonalRun | `M+0x22b8` | 0x21 DIAGONAL_RUN | shared | `0x140615090` | `0x140614290` | `0x140613910` | `0x140612cf0` |
| 7 | CenteringGet | `M+0x2558` | 0x26 CENTERING_GET | `0x14061ccc0` | `0x14061a640` | — | — | — |
| 8 | PullAway | `M+0x2568` | 0x2f PULL_AWAY | shared | `0x14061e1e0` | `0x140421f40` (returns 0) | `0x14061dee0` | none |
| 9 | OverlapCB | `M+0x27f8` | 0x28 OVERLAP_CB | `ret` (never starts) | `0x14061e590` | — | — | — |
| 10 | GoalGet | `M+0x2808` | 0x2a GOAL_GET | `0x14061f110` | `0x14061e810` | — | — | — |
| 11 | PostPlay | `M+0x2810` | 0x2b POST_PLAY | shared | `0x140620900` | `0x140620590` | `0x14061f940` | none |

Action-state names: string table `0x1434A4D50` (OBS dump; e.g. 1 BASE_POSITION, 0x10 MOVE_ON_PASS, 0x22 PASS_SUPPORT,
0x24 PASS_AND_GO_RUN, 0x25 ONE_TWO_PASS_RUN, 0x2c TEAMMATE_MOVING, 0x36 MARK).
LineBreak and PullAway have a null mask (`0x140421f40` = `xor eax,eax; ret`) → **the shared select never picks a new
LineBreak/PullAway runner**; those states are entered elsewhere and only stopped here (INF(high); writers not found —
`statesites.py` finds no immediate-0x20/0x2f state write).

### 1.2 Cadence (OBS)
Team tick `FUN_1404f6c30` calls the manager tick `FUN_140525530(M, T, team, ctx, reg)` only when
`(MI+0x1198 & 1) == team` (raw `0x1404f78a5`; b2a.c:730) → **each team selects runs every other frame (27 Hz)**,
the two teams alternate. Manager tick, in order:
1. Reference player `ref` = carrier `FUN_140a627e0(ctx,0)`, else `T+0x78`, else `MA+0x2483c`. When `ref` or the attack
   level `T+0xb8a4` changes, rebuild the per-run **caps** and **priorities** (`FUN_140600e40`, §1.4).
2. Per-slot timers (11 each): `A[s]` (`M+0x17b4`) = `int(fps·2+0.5)` = **108 f** when the slot newly enters a run state
   (`FUN_1405a4830` → states 0x1d–0x21, 0x24, 0x25, 0x2c; 0x27 only for flag; 0x2f only when the AI class is 6/7);
   while not running it counts down; set to 0 when the slot is the pass receiver `FUN_1405969b0`. `B[s]` (`M+0x17e0`)
   = `int(fps·0.3+0.5)` = **16 f** on entering PULL_AWAY, counting down otherwise. Context `ctx+0x38` = A, `+0x40` = B.
   A is read by the SecondLine mask (`param_2[7][slot] != 0 → skip`, b4a.c:1204): **2 s re-run block** after a run.
3. **Stop pass**: for every registered selector, for every own slot whose state equals the selector id and whose
   concept slot `T+0xb748` is 0x30 (none) or whose `T+0xb774` ≠ 0x22: release (`FUN_140a40820`: state 0, record cleared)
   if the player is pad-controlled (`player+0xc4`, `FUN_14050f560`) or the stop test returns true.
4. **Select pass**: for every registered selector in table order: `ctx+0x30` = priority row `M+0x204+0x2c·id`,
   `ctx+0x24` = cap `M+8+4·id`, call `vt+0x10(sel, T, ctx)`.

### 1.3 Shared select `0x140602060` (OBS b1a.c:5–252)
```
id = sel+0x11c ; n = #own slots with T+0xa2a0[s] == id
sel+0x280 = 0
if n < cap(ctx+0x24) or id == 0x1e (Chance):
    reset the 11 per-slot sub-records sel+0x120+0x20·s (FUN_140a85aa0)
    mask = vt+0x20(sel, ctx, T)                    # candidate bits
    for s in 0..10: entry[s] = {global id, 0}
        if mask bit s: target[s] = vt+0x30(...)    # 12-byte target, y = -100 means "none" → score 0
                       score[s]  = vt+0x28(sel, ctx, id, T)
    if any score != 0:
        qsort(entries, 11, desc by score)          # cmp 0x140602040
        walk from the top while score > 0:
            Chance: sub-kind 0xd → take; else take iff FUN_140601d80(ctx,id) or !FUN_1405ac450(...)
                    (not the partner of an active one-two/pass-and-go in a run state); at most one sub-kind 0xe per tick
            Counter/LineBreak: same FUN_140601d80 / FUN_1405ac450 test; others: take
            tie: among later entries with the same score and a free slot (state 0), the one with the larger
                 priority ctx+0x30[s] wins (strictly larger)
            take: FUN_140a429a0(T, s, id, subrecord) → T+0xa2a0[s] = id, timer stamp; link byte sel+0xe5[s]
                  → T record; target → T+0x26c+0x40·s; T+0x4c+4s = sel+0xf0+4s
            n += 1; stop when n >= cap, or n >= sel+0xe4 (per-tick cap), or 10 taken
    Chance only: if (n_at_tick_start − cap) > 0, release that many Chance runners whose bit is set in sel+0x280,
                 walking the list T+0xc060 (8-byte entries, team·11 + k)
```
- **Per-tick cap** `sel+0xe4` (ctor `FUN_140524d00(sel, cap, id)`): Chance **11**, all other score selectors **1**
  (OBS `FUN_140524a30`, `140524d00(.,1,0x1f/0x20/0x2f/0x2b)`, `140524e00`, `140612c60`).
- **Cross-selector exclusion = action priority ranks** `FUN_140a40050` (OBS switch); a selector may take a slot only if
  `rank(new) < rank(current)` (`FUN_140a40000`, used by every mask): rank 0 {0x23, 0x24, 0x25, 0x2c, 0x2d, 0x2e, 0x3a,
  0x3b}; rank 2 {0x1d–0x21, 0x27, 0x28, 0x2a, 0x2b, 0x33, 0x34, 0x35, 0x3c}; rank 3 {0x10, 0x26, 0x2f, 0x36, 0x37,
  0x69–0x6b}; rank 4 {0x22, 0x32, 0x3d, 0x6d}; rank 5 everything else (0 NOTHING, 1 BASE_POSITION…). BASE_POSITION with
  record byte `+0x29c` and PASS_SUPPORT with `+0x290` are promoted to rank 2 (`FUN_140a40110`).
  **Consequences:** a player holds one run at a time; a run never pre-empts another run (2 ≮ 2); concept moves
  (pass-and-go, one-two, TEAMMATE_MOVING, rank 0) are never displaced by a run; runs may start from BASE_POSITION,
  PASS_SUPPORT, MOVE_ON_PASS, CENTERING_GET, PULL_AWAY, MARK. Because the select pass runs selectors in registration
  order and each take is immediate, **Overlap > Chance > Counter > SecondLine > LineBreak > Diagonal > … for a player
  eligible for several runs in the same tick** (first-come). The Chance mask also accepts slots in OVERLAP_SB (runs.c:517).
- **Priority row** (`FUN_140601110`): per action id × slot, starts −1; for each set skill bit (41, `player+0x48`, table
  `0x1436f2110`) and playing-style bit (21, `+0x50`, function table `0x1436f2260`) take the max of the table values (0 in a
  table disables the run for that player); still-negative → 1.0. **Both tables are all-null in the shipped image and in
  the live process** (OBS read of 41+21 qwords, `pes_full.bin` and live `/proc/pid/mem`) → every priority is 1.0, the
  tie-break is inert and **skills/styles do not weight run choice through this path**.

### 1.4 How many may run at once (OBS: `FUN_140600e40`, `FUN_140a4f520`, data `selectorVision.o`)
`cap(id)` = byte from registry object 0xc2 = `team/selectorVision.json` (`constant_team.bin`, VFS winner 1513 B; live
struct = file + 8 with the lists inlined, 5 × i32 each), indexed by `L = T+0xb8a4` (attack level 0..4), `L += 1` (max 4)
when the ref player has instruction 8 (`FUN_140a64030(ref, 8)`, INF DEFENSIVE) or, for Chance only, `FUN_140a63680(ref)`:

| key (struct off) | run | L=0 | 1 | 2 | 3 | 4 | gate (difficulty capability `FUN_140a604d0`) |
|---|---|---|---|---|---|---|---|
| `offerNum_diagonalRun` (+0x08) | 0x21 | 1 | 1 | 2 | 2 | 3 | row 0x11 |
| `offerNum_lineBreak` (+0x1c) | 0x20 | 0 | 1 | 2 | 2 | 3 | row 0x10 |
| `offerNum_pullAway` (+0x30) | 0x2f | 0 | 1 | 1 | 2 | 2 | row 0x10 |
| `offerNum_spaceRunChance` (+0x44) | 0x1e | 1 | 2 | 3 | 4 | 5 | row 0xe |
| `offerNum_spaceRunCounter` (+0x58) | 0x1f | 2 | 3 | 4 | 4 | 4 | row 0xf (5 if `T+0xb6ea`) |
| `offerNum_spaceRunSecondLine` (+0x6c) | 0x1d | 0 | 1 | 1 | 1 | 1 | row 0xe |
| Overlap 0x27 (code) | | 0 | 1 | 1 or 2 | 1 or 2 | 1 or 2 | L=0 → 0; else 1 if no instruction 4 (BOTH_SB_UP) and `FUN_140a50250(ctx,team,1)` < 2, else `1 + (L>1)` |
| PostPlay 0x2b (code) | | 1 | 1 | 1 | 1 | 1 | — |

Instruction 6 (TIKI_TAKA, `FUN_140a63950(.,6,0xff)`): SecondLine and PullAway cap 0, Chance cap 1. Key names: strings
`0x142b538c8..0x142b53948` (OBS, alphabetical rule matches the six reader offsets).
**Live (OBS, both managers found by vtable scan, cap array read at vtable+8, L = 2 for both teams):** team 0 =
{SecondLine 1, Chance 3, Counter 4, LineBreak 2, Diagonal 2, PullAway 1, Overlap 1, PostPlay 1} = the L = 2 column
exactly; team 1 = {SecondLine 0, Chance 1, Counter 4, LineBreak 2, Diagonal 2, PullAway 0, Overlap 1, PostPlay 1} =
the L = 2 column with the TIKI_TAKA override (team 1 has instruction 6 active, tactics §6 live). Both priority rows
all 1.0. `+0x1f8` = 10 (count field). Note the live base: the cap array sits at the scanned vtable + 8, i.e. the
manager-tick argument `M` is the vtable address in the live object (all `M+…` offsets above are relative to it).

### 1.5 Threshold to start a run (OBS structure; sub-tests per run in attack-decisions §3)
There is **no global score threshold**: a slot starts a run iff (a) its bit is set in the run's candidate mask (the
geometric/eligibility cascade), (b) its score is `> 0` after the sort, (c) the cap is not reached, (d) its current state
ranks lower (`FUN_140a40000`), (e) for Chance/Counter/LineBreak the one-two test above. Scores only order candidates:
- Chance `0x140606060`: `score = 100 + s·x_runner` for every sub-kind (`100.0` @0x14253ee30, OBS disasm
  `0x140606575–0x140606584`), sub-kind 0xe = `FUN_1406065b0(...) + s·x` (`100 − 3/−5` adjustments, 45/−55/10/8/15/7/5/4/6
  in its body); `0` when the target builder found no target (`y = −100`, `0xc2c80000`). → **the most advanced eligible
  mate runs first**. The Chance mask's longitudinal reach (attack-decisions §3.2, polarity corrected in §0): candidate
  only if `|x_runner − x_ball| ≤ base(15 / 21 no-plan) + 5·ramp(|z_runner − z_ball|; 12 → 30) (floor 24 when a runner needs
  service, 26 if `FUN_1405a5b20`, clamp 22..1000 if `FUN_14060a100`, cap 37, floor 35 if `FUN_1405aaf00`) + 2·bonus`,
  `bonus = 18·clamp((b − 75)/20)`, `b = sub+0x24 (+10 with skill bit 13)`. INF(med) on the label of `sub+0x24`: by the
  stamina-drain rule `stat = sub + 8 + id` it is stat index 0x1C (labelled Lofted Pass in matchlogic §1.3), not
  Offensive Awareness — flagged, not resolved.
- Counter `0x14060a8d0`: `MA` line-rank value of the runner (`FUN_140a71500` table `+4+8·rank`), `+8` when the slot's
  position class `T+0x4c00 < 5`, `×1.3` if `FUN_1405c0f00(ctx,id,0x1f)`, `×1.2` when the runner is on the Attacking-Area
  side (`|z|` vs `F[7] + (F[5]−F[7])·0.5`, Centre/Wide by `FUN_140a62180(ctx,team,2)`); fixed 200 for the slots already
  pinned in `sel+0x288` (sub-kind 7) / `+0x28c` (sub-kind 0x13).
- SecondLine `0x14060d210` (rating + 14.0, attack-decisions §3.4), Diagonal `0x140613910` (depth margin; 100 if
  already pinned), PostPlay `0x14061f940` (lanes 0/1/2), LineBreak/PullAway constant 100 / angle score (never reached
  through select, §1.1).
- The `_good` constants of `spaceRun.o` (registry 0xc3, 60 keys, VFS values e.g. `chanceScore` 102 / `_good` 88,
  `checkBallDist_chance` 27.5 / 33.0, `checkLastLineDist_chance` 24 / 30, `checkOffsideLineDistX` 12, `checkZ` 41.5 /
  44) are selected per player by `FUN_1405c0f00(ctx, id, runId)` (OBS: Chance target builder reads `+0x1c`/`+0x20` =
  `chanceScore`/`chanceScore_good` and passes it to `FUN_14059ba60` as the target-search score limit). Readers of 0xc3:
  `0x1405aca50, 0x140602600, 0x140605570, 0x1406071df, 0x14060acfa, 0x140612660, 0x140664ee0, 0x1409a28d0…0x1409aacc0`
  (`regsites.py`); their per-key use is not reduced (open O3).

## 2. Timing: when a run starts and how long it lasts

### 2.1 Start (OBS)
- Selection happens only on the team's parity frame (27 Hz, §1.2) and only in open play for most masks (`MI+0x116c == 1`
  in Chance/Diagonal/PostPlay/stop tests). Every mask/stop test reads the **carrier state**, not a "head-up" flag:
  - the run is cancelled when the runner becomes the carrier (`FUN_140a627e0 == id` and `!FUN_140a6e340`) or is in
    PASS_GET (5) with `FUN_140a51320(ctx) > 1`;
  - Chance mask needs `T+0xa508 != 0` and `T+0x255` (in possession) and a valid receiver/carrier
    (`FUN_1405969b0` = carrier, else MI pass target, else `T+0x78`), and uses the carrier's speed/facing
    (`FUN_140a76240`, `+0x554`) and the last ball event (`*piVar14 == 3 && piVar14[7] == 5` = a set-play restart
    → 16 m² gate).
  - the support search (§4) waits **3 s** after the possession change (`MI.frame − T+0xb8c0 ≥ int(fps·3+0.5) = 162`).
- There is no explicit "about to pass" trigger in the run selectors; the pass itself ends most runs (§2.2) and the
  carrier's pass choice reads the runs (§3.2).

### 2.2 Duration: the stop tests (OBS; frames at 54 fps; `t` = frames in the current state from the slot timer block
`T+0xa2cc+0x34·s`, ring of 4 × {state, start, now})
| run | minimum before any stop | stop rule (true → released to NOTHING) |
|---|---|---|
| Chance `0x140607a60` | `t < int(fps·0.2+0.5)` = **11 f** → keep | pad-controlled; became carrier; PASS_GET with `FUN_140a51320 > 1`; AI class `≠ 0x13` and common test `FUN_140688c00(…,0x1e)` |
| Counter `0x14060b410` | 11 f | as Chance, then common test, else stop when `s·x > F[8]` (41.56, penalty spot) |
| SecondLine `0x1406118e0` | `t < fps` = **54 f** | as Chance, then common test |
| LineBreak `0x140612830` | 54 f | carrier; PASS_GET; predicted-position test `FUN_1405a0ec0`; after 54 f the common test or the ball-line test `FUN_140a4c720(ball.x, x, s, −1.8)` …; reached target (< 1 m) |
| Diagonal `0x140615090` | 11 f | not open play / not in possession / designated player → stop; then `FUN_140695800` |
| PullAway `0x14061e1e0` | — | pad; `s·x > F[8]`; else stop when `t > 4·fps` = **216 f (4 s)** |
| PostPlay `0x140620900` | — | not open play; `t > int(fps·3.5+0.5)` = **189 f**; no ref/receiver; lane 1: ball > 20 m away, ball past `F[6]+3`, ball `|z| < F[7]+2`; lane 2: ball > 40 m; lane 0: ball-line test −2.5 (`FUN_140a4c720(runner.x, MA+0x2482c, team+0x254, −2.5 @0x1425c74d0)`, OBS 0x140620a7f; **not the offside line** — the constant is applied to the ball x, verified OffsideAwarenessPort 2026-10-14), keeper-distance table > 20.25 m² or > 2.25, ball > 20 m; ≥ 16 m² from ball and not the event target: after `fps+0.5` f stop if the angle to the receiver > 110° |
| GoalGet `0x14061e810`, CenteringGet `0x14061a640`, Overlap `0x1406013d0`, OverlapCB `0x14061e590` | — | see attack-decisions §3.6–3.8 (bodies there are these stop tests) |

**Common stop test `FUN_140688c00(ctx, id, run, arg)` (head OBS b3a.c:438–530):** stop when not open play
(`MI+0x116c ≠ 1` and `MI+0x11d4 ≠ 2`); team lost the possession flag `T+0x255`; the runner is the carrier or in
PASS_GET; slot record kind ≠ 7 and a loose-ball/opponent-control test (`FUN_140a62690`, `FUN_140a53f90`, player `+1`
not 'A'/'B'); **target reached** (AI class 1 and `|pos − AI target (+0x12b0/+0x12b8)| < 1 m` and `FUN_1405951b0(.,1.5)`,
not for PullAway); `s·x > F[4] + 2.5` = **49.56 m** (near the goal line); **the last ball event is a pass to him**: a
non-through pass → stop now; a through pass (type 2, or type 3 sub 3/4) → stop `fps/3` = **18 f** after the kick.
The rest of the cascade (b3a.c:534–1251; `FUN_1405a7fe0/7be0` lane-free → keep, the 0.15·fps random-free clip
window, `FUN_14068f600`, `FUN_14068bd90`, distance ramps 4→10 m, 40 m, 6.25, 70°, 130°/169 m², 289 m², 45°/80°/24 m,
27→15 m angle ramp `95 + 35·k`, `FUN_14068dba0/de30/e300`, 43) is listed but not reduced (open O4).

**No offside input in any run stop test (OBS decompiles, OffsideAwarenessPort 2026-10-14).** `FUN_140688c00`,
`FUN_140607a60` (Chance), `FUN_140612830` (LineBreak), `FUN_140620900` (PostPlay) and the Chance target search
`FUN_14059ba60` were all decompiled against the flag words (`MA+0x253c4+0x10·a+4·c`) and the referee's offside line
(`MA+0x44+0x14·t`): none reads either. The only longitudinal stop input is the **ball line**
(`FUN_140a4c720`, −1.8 m for LineBreak and −2.5 m for PostPlay's lane 0). ⇒ **a run is never held, delayed or checked
because of offside; PES runs the player offside and the referee's flag stops the play.** Details in
`pes-re-offside.md` §4.2.

### 2.3 Re-run block (OBS)
`A[s]` = 108 manager ticks after a slot leaves a run state; ticks are every other frame → **216 frames ≈ 4 s**; read by
the SecondLine mask (a slot with `A ≠ 0` is not a SecondLine candidate). `B[s]` = 16 ticks (32 f) after PULL_AWAY,
handed to the masks as `ctx+0x40` (reader not reduced). INF(high): no other per-run cool-down exists — the "≈ 11-frame
cooldowns" of team-plans §3.1 are the minimum run times above.

## 3. Coordination

### 3.1 Runner vs runner (OBS)
1. **One action per player, rank-ordered** (§1.3): a runner cannot be taken by another run; runs cannot displace
   concept moves.
2. **Per-run caps** (§1.4) and **one new runner per run type per tick** (except Chance, up to the cap per tick).
3. **Fixed selector order** within a tick (Overlap → Chance → Counter → SecondLine → LineBreak → Diagonal →
   CenteringGet → PullAway → OverlapCB → GoalGet → PostPlay).
4. **Lane exclusivity is inside the masks/targets, not in the manager**: the Chance mask keeps the special slots
   `sel+0x2a0..0x2c4` (one id per special sub-kind: 0x2a4 `FUN_140608570`, 0x2a8 `FUN_1406087a0`, 0x2b8
   `FUN_140609350`, 700/0x2c0/0x2c4 `FUN_140608ac0` kinds 0x14/0x15/other) — **one runner per special Chance lane**;
   the generic Chance lane is skipped while any special lane is taken (runs.c:530); Diagonal, Chance and SecondLine
   gates call `FUN_1405a7fe0/7be0` (free-lane tests) and the corridor battery (`FUN_140608570/87a0/9350/8ac0`,
   `FUN_140615180/5380/7480/134c0/12fc0/13620/12d70`); Diagonal mask refuses slots whose state is `0x2f` without the
   `0x21` transition. INF(med): runners into the same lane are prevented by these corridor tests reading the other
   runners' targets `T+0x26c+0x40·s` — not traced to the read.
5. Excess Chance runners are released in the `T+0xc060` order when the cap drops (§1.3).
6. The **designated supporter** (§4.2) is forced back to BASE_POSITION with rank 2 by the team tick
   (`FUN_1404f6c30` b2a.c:657–699, flag `support+0x7fd7[s]`), so it can never be a runner.

### 3.2 Runs and the carrier's pass choice (OBS)
- The receiver enumeration (`0x1421174f0`, carrier-ai §4.3) does **not** read the run states: a runner is a normal
  receiver; record B ("into space") uses his velocity (`8 + 5·clamp01(v·cosΔ/20)` m), which is how a run becomes a
  through-pass target.
- Long-pass validity `0x1421240b0` (OBS b7a.c:225–234): if the receiver is in SPACE_RUN_CHANCE/COUNTER (`player+0x5d0`
  ∈ {0x1e, 0x1f}) **with sub-kind 7** (`T+0x28c+0x40·s == 7`), the opponent-proximity (`> 400` m²) and
  pressure (`FUN_140a4e250`) vetoes are skipped → **runners on a sub-kind-7 run are long-ball targets even when marked**.
- One-two unit `0x142128c10`: counts mates already in ONE_TWO_PASS_RUN (0x25); allowed count 0..2 from team caps
  6/5/4 (`FUN_140a602e0`) +1 when attack level > 2 (or match state 2/3); refuses if the carrier's pass target is already
  in 0x25.
- The pass ends the run: common stop rule "last ball event = pass to him" (§2.2) releases a Chance/Counter/SecondLine/
  LineBreak runner immediately on a short pass, 18 f after a through pass.

## 4. Support positioning when not running

### 4.1 Pipeline (OBS `FUN_1405db800`, called from `0x1405f4ce8` and `0x14067b8ee`)
Per team tick, `param_4` = the 11 slot targets (`T+0x4b4c`-type array, attack-signed): `FUN_1405e6e80`,
`FUN_1405d8b70`, `FUN_1405d1810` (base adjustments), then in open play with possession `FUN_1405eb0c0`,
`FUN_1405d0bf0`, `FUN_1405dabe0` (else `FUN_1405e1410`), `FUN_1405db000`, `FUN_1405d4b00`, `FUN_1405e6e80` again,
then the three support steps:
1. **Designated supporter** `FUN_1405eb980` (only when `T+0xc12b`): among on-pitch mates except the passer and
   `FUN_140a62690`, positions predicted `int(fps·0.35+0.5)` = **19 f** ahead, cost = `0.7·|Δangle(carrier facing (or
   T+0xc13c when T+0xc140 > 0.2), bearing carrier→mate)| + distance + 15 (if distance < 10 m)`; min cost → `+0x7b4`.
2. **Supporter target** `FUN_1405ebca0`: if `T+0xc134` is the passer: target = passer predicted point moved by the
   **support distance `d` = `FUN_1405e0c60`** along the bearing passer → supporter (tactics §4.1 formula: `5 + 3·s + 7·deep
   + …`, floors 9/6/4, 8 out of play); zone = 10 × 10 m around it; else the supporter keeps his AI target `+0x12b0`.
3. **Free support cells** `FUN_1405e9f50` for every BASE_POSITION (`player+0x5d0 == 1`) outfield mate (§4.3).

### 4.2 The support grid (OBS; names O5 of tactics.md)
`MA+0x26020 + 0x62700·k`: **32 layers × 70 × 45 cells** (`FUN_140a716c0(grid, cx<70, cz<45, layer)`); built for the
**team in possession only** (`T0+0x255` selects k) by `0x140458b71…`, one step per frame, `step = MI.frame % 11`
(full refresh every 11 frames ≈ 0.2 s); all positions predicted `int(fps·0.1+0.5)` = **5 f** ahead.
Writers are only `0x14045906a/0x1404596e1/0x1404599cf/0x140459c82` (`FUN_140a72a70`), cleared by `FUN_140a72150`
at four sites (OBS census) — layers 11, 23, 25, 26 are never written.
| layer | name (this report) | content (step) |
|---|---|---|
| 0..10 | **mate presence** (one per slot) | step 0: `5·w⁴`, `w` = 1 within 5 m of the mate, `(d − R)/(5 − R)` to `R` = 20 m (25 m for the ball holder), 0 beyond |
| 0x21 | sum of 0..11 | team-mate crowding |
| 12..22 | **mate screen** (one per slot) | steps 6–10 (2+2+2+2+3 slots): cells on the ray from the ball holder through the mate and beyond (100 m): `5·w`, `w` = 1 within `2 m` (+3 for the holder), linear to 0 at +4 m |
| 0x22 | sum of 12..23 | screened cells (a pass there runs through a team-mate) |
| 24 | **opponent presence** | step 0: summed `5·w⁴` kernels (5 → 20 m) of the 11 opponents |
| 0x23 | 24 + 25 + 26 | = layer 24 |
| 27..31 | **interception risk** | steps 1–5 (opponents 0–2, 3–4, 5–6, 7–8, 9–10): for a pass holder → cell, each opponent projected on the lane: `+10·g²`, `g` = 1 inside `r_in = max(0, 0.5·clamp(rem/5) + 0.5·clamp((L−8)/10))·c`, linear to 0 at `r_out = 2c + r_in + 2`, `c = clamp((along − 1)/5)`; `rem` = projection→cell, `L` = holder→cell, `along` = holder→projection |
| 0x24 | 27..31 | interception risk sum |

### 4.3 Support-cell cost `FUN_1405e05c0` + `FUN_1405e0180` (OBS raw disasm `e05c0r.s`; lower is better)
For candidate mate `c`, passer `p`, cell `C`; `t3 = ramp(s·x_c; s·MA[0x40+0x14·side]+5 → 0, s·MA[0x44+0x14·opp]−5 → 1)`
(own line → opponent line), `s = clamp((SupportRange−1)/9)`, flags `f_k = FUN_1405e0010(k)` = the ball holder's
in-match block ints `block+0xd4+4k` (k = 0,1,2; 0 when the ball is above 1.5 m; meaning open):
```
crowd = L21(C) − L_c(C) − L_p(C)          # other mates near the cell
V     = L_p(C)                            # closeness to the passer
if full:  f0 → V·(11 + 5(1−t3)) ; elif f1 → V·(1 + 2(1−t3)) ; elif ballHeight > 1 → crowd·0.5, V·0.5 ;
          elif c ≠ designated → V·(2 + 2(1−t3))
V = BuildUp==Long ? V·(2 + 3s) (and crowd·5) : V·(0.5 + 1.5s)
if f2 and |c − p| ≥ 30 m and not support+0x6c5[c]: crowd += 1000·crowd
cost = crowd + V
TIKI_TAKA: if no other mate within 8 m (passer 5 m) of C: cost ·= 1 − 0.7·t3
cost += L22(C) − L_{12+c}(C)              # screened by another mate
cost += FUN_1405e0180: opp = L23(C); if (role 1, c ≠ special, |angle| < 100(+20), |p−c| < 30(+2)) opp = 11·L23 + 10·L24;
        if (!f2 or |p−c| < 30 or 0x6c5[c]): opp += L24, then ·(1 − 0.2·t) (f0 = 0) or ·(1 − 0.5·t') (±15 m ramp)
        else opp += 1000·L23
```
**Search `FUN_1405e9f50` (OBS):** runs ≥ 3 s after the possession change, open play, ball not loose. Each slot is
re-searched once every 5 calls (two slots per call: `counter[s]` 0..4), plus every call for the designated supporter;
otherwise it keeps the stored cell (`T+0x48f4` in open play). Admissible distance to the passer `30 (+10 Long Pass) (+25/+40
for f2 = 1/2, else +15/+30 for f1 = 1/2) (+2 if flagged)`; behind-the-passer limits (> 130° and > 20 m → no; > 110° and
ball above 1.5 m and > 15 m → no; > 20 m behind → no). Cells: the slot's zone rectangle (`support+0x4ec+0x14·s`, centre
+ half-sizes; 10 × 10 m for the supporter), within 12 m of the current target when the mate is settled (inside the zone, no
mate within 8 m (5 m Tiki-Taka), > 10 f since `+0x7e0`/`+0x7e4`). A cell replaces the current one only if
`cost + 2 ≤ cost_now` and the second metric `FUN_1405df5a0 ≤ metric_now + 3` (hysteresis; Tiki-Taka: `2 + 3·t3`, 0).
Output: `param_4[s] = s·(cell.x, 0, cell.z)` → the slot target.
**How players offer passing angles (INF high from the cost terms):** cells close to the passer are penalised in
proportion to Support Range (wider support with higher SR), cells crowded by other mates are penalised, cells screened by a
team-mate from the passer (layers 12–22) and cells near opponents or inside an opponent's interception lane (24, 27–31)
are penalised; the search is local (zone rectangle, 12 m) with hysteresis, so mates drift to the nearest free lane.

### 4.4 Support Range
`FUN_1405e0c60` is the supporter distance (formula unchanged from tactics §4.1, re-read here, OBS b6a.c:706–896:
`5 + fwd·deep·4(Long) + 3s + 7·deep + back·ramp + 3·ramp(|z|)·ramp(x)`, `min 5` when the holder's state value > 1, `+5
floor 9` (f0), `−5` (f1), `−10 floor 4` (arg 7 = designated supporter call), else floor 6; 8 out of play). Support Range
also scales the passer-proximity weight of every cell (§4.3).

## 5. Live check (read-only `/proc/pid/mem`, AI-vs-AI match in open play)
Tools: `offball_sample.py` (20 Hz; per team `T+0x254/0x255`, `+0xb8a4`, `a2a0[11]`, `b748[11]`, records `T+0x26c..`,
timer blocks `T+0xa2cc`; 22 players pos/vel/`+0x5d0`; MA ball; managers by vtable scan), `offball_an.py`. Capture
`live1`: 18,000 samples, frames 10,478 → 47,284 (≈ 11.4 min of play), 17,881 frame-consistent, attack level 2 both teams.
Run starts are dated exactly from the slot timer block (`start` frame of the current entry).

| check | prediction | observed |
|---|---|---|
| caps (`selectorVision.o`, L = 2, TIKI_TAKA on team 1) | team 0 {1,3,4,2,2,1,1,1}, team 1 {0,1,4,2,2,0,1,1} | identical (§1.4) |
| run starts only on the team's parity frame | 100 % | **198/201** (98.5 %; 3 Chance starts off-parity — INF: another writer of 0x1e, open O6) |
| max simultaneous runners ≤ cap | SecondLine 1, Chance 3/1, Counter 4, Diagonal 2, PostPlay 1 | team 0: 1, 2, **4**, 1, 1; team 1: Chance **2 > 1** (the Chance select may take one past the cap; the excess is released the next tick, §1.3), Diagonal 1, PostPlay 1 |
| minimum run time | Chance/Counter/Diagonal 11 f, SecondLine 54 f | SecondLine min **55 f** (n = 8; 0 % < 54), Diagonal min **11 f** (n = 9), Chance p10 12 f (4 % < 11: ended by carrier/PASS_GET/possession paths that bypass the minimum), Counter 21 % < 11 (same bypass; Counter is cancelled on PASS_GET with > 1 pass in flight) |
| PostPlay ≤ 189 f, PullAway ≤ 216 f | | PostPlay max 46 f (n = 9); no PULL_AWAY in this capture |
| runs end to NOTHING | | 179/187 observed ends go to state 0 (then re-assigned); the rest: 3 CENTERING_GET → BASE_POSITION, 1 Chance → BASE_POSITION, 1 Counter → PASS_SUPPORT_THROWIN, and after a possession loss MARK / PRESS / PASS_COURSE_CUT (1 each); 11/93 Chance ends with possession lost |
| pass ends the run | | runner in PASS_GET within 30 f of the run end: Chance 18/93, PostPlay 5/9, Diagonal 2/9, others ≤ 2 |
| Chance ordering = most advanced eligible first | | rank of the starter by attack-signed x among 10 outfield mates: 2 (36), 1 (20), 4 (18), 0 (11) — not the most advanced overall; consistent with the mask's reach/permission filters but **the score order itself is not testable from positions alone** (open O7). Diagonal and PostPlay starters were always the most advanced mate (9/9, 11/11) |
| run start vs carrier state | | nearest own player to the ball at a Chance start: PASS_GET 64, DRIBBLE 20, other 17 → **runs start mostly while a pass is travelling to a team-mate**, second while he dribbles |

Totals: team 0 {Chance 53, Counter 26, SecondLine 8, Diagonal 5, CenteringGet 7, GoalGet 3, PostPlay 1}; team 1
{Chance 48, Diagonal 4, CenteringGet 21, GoalGet 15, PostPlay 10}; no LineBreak/PullAway/Overlap starts (LineBreak and
PullAway cannot start from the shared select, §1.1). Not checked live: support-cell positions and the supporter distance
(addresses of the support object `+0x4ec/+0x7b4/+0x48f4` were not located in the live heap; open O8).

## 6. GF code each finding replaces
| finding | GF code (src/sim) |
|---|---|
| §1 manager: 11 selectors in fixed order, every other frame, rank table, per-run caps from `selectorVision.o` by attack level, one new runner per type per tick | `TeamAIController::Process` attacking-run block (`teamAIcontroller.cpp` ~340–400: `% 500 ms` probe, `GetAttackingRunThreshold`, `GetCounterWindow_ms`) and `SelectAttackingRunPlayer` (single runner, nearest to `possession + 26 m`, `PlayingStyles::GetAttackingRunAffinity`) → delete; PES has up to 13 runners by type and no style weighting (§1.3 priorities all 1.0) |
| §1.5 scores (Chance = 100 + forward x; Counter rank; SecondLine rating + 14) | the `cost = distance·(1.5 − affinity)` in `SelectAttackingRunPlayer` |
| §2 stop tests, 11/54-frame minimums, 4 s PullAway, 3.5 s PostPlay, pass-to-runner end, 4 s re-run block | `ApplyAttackingRun` duration `GetAttackingRunDuration_ms` / `endApplyAttackingRun_ms` window |
| §3.1 lane exclusivity per special Chance lane, rank-protected concept moves, designated supporter never runs | none in GF (one `attackingRunPlayer`, one `forwardSupportPlayer`) |
| §3.2 runners are ordinary receivers; sub-kind-7 runners skip the long-pass pressure veto; one-two count cap | `elizacontroller.cpp` pass target selection (`_AddPass`, `AI_GetPassRatings` in `AIsupport/AIfunctions.cpp`) |
| §4.1 designated supporter = min(0.7·angle + distance + 15 if < 10 m), placed at support distance along the carrier→supporter bearing | `forwardSupportPlayer = AI_GetClosestPlayer(...)` every 1500 ms (`teamAIcontroller.cpp` ~403) and the forward-support branch in `ElizaController::GetSupportPosition_ForceField` (~976) |
| §4.2–4.3 grid layers + cell cost + zone-limited search with hysteresis | `ElizaController::GetSupportPosition` / `GetSupportPosition_ForceField` (force field, `support_distance` web scale `AITactics::GetSupportWebScale`), `AI_GetForceFieldMovement`, `AI_CalculateFreeSpace`, `MentalImage` (`AIsupport/mentalimage.*`); the `attackBias` blend in `strategies/offtheball/default_off.cpp`/`default_mid.cpp` |
| §4.4 Support Range = +0–3 m supporter distance and ×(0.5…5) passer-proximity weight | the `support_distance` tactic float |

## 7. Killed hypotheses
- "ActionSelector slot 4 decides whether a run may start" — it is the stop test (§0).
- "Chance picks the first listed candidate, no score" — sorted by `100 + s·x` (§1.5).
- "Run cool-downs of ≈ 11 frames" — minimum run time; the real re-run block is 108 ticks (§2.3).
- "Skills/playing styles weight the run choice" (team-plans §3.3 hypothesis) — the priority tables are empty, rows all 1.0
  live; no style-bit test in any mask/score decoded here.
- "Run selection runs every frame for both teams" — alternate frames per team (live 198/201).
- "The carrier's receiver enumeration excludes or prefers runners" — it does not read `T+0xa2a0` (§3.2).
- "`0x14061e810` is PullAway's gate", "`0x14061a640` is a Score run", "`0x1406013d0` executes the overlap" (§0).
- "The cap array is at manager-tick argument + 8 relative to the vtable + 8" — live read at vtable + 0x10 gave shifted
  values; the correct live address is vtable + 8.

## 8. Open list (exact next step each)
- **O1** Chance target builder sub-functions (`FUN_14059ba60`, `FUN_1406067c0`, `FUN_14059ed10`, `FUN_140673ad0`,
  `FUN_1406718f0/725f0`, `FUN_140603830/4490/3d00`): where each Chance sub-kind runs to. `dec.sh` them.
- **O2** Corridor battery read of other runners' targets (§3.1 item 4): data-xref `T+0x26c+0x40·s` reads inside
  `FUN_140608570/87a0/9350/8ac0`, `FUN_1405a7fe0/7be0`.
- **O3** per-key use of `spaceRun.o` (0xc3) in its 12 reader functions; `FUN_1405c0f00(ctx,id,run)` (`_good` selector).
- **O4** rest of the common stop test `FUN_140688c00` (b3a.c:534–1251).
- **O5** meaning of the ball-holder block ints `+0xd4/+0xd8/+0xdc` (support flags f0–f2); log them per carrier live.
- **O6** the 3 off-parity Chance starts: scan for other `T+0xa2a0 = 0x1e` writers (`statesites.py` found none with an
  immediate; try register-valued `FUN_140a429a0` callers).
- **O7** Chance ordering live: log the mask bits (`sel+0x280..0x298`, `sel+0x2a0..0x2c4`) and scores `sel+8..` of the
  live Chance selector (`vtable 0x1425c61b0` object at manager + 0x1818/0x1820) at 20 Hz.
- **O8** live support: find the support object (`FUN_1405db800` `param_1`; callers `0x1405f4ce8`, `0x14067b8ee`) and log
  `+0x7b4` supporter, `+0x40c` passer, zone rects `+0x4ec`, stored cells `T+0x48f4`; compare the supporter's distance
  with `FUN_1405e0c60`.
- **O9** who starts LINE_BREAK and PULL_AWAY (null masks), CENTERING_GET count `FUN_140642d00` and GOAL_GET select
  `FUN_14061f110` (decompiled, `b7a.c`, not reduced).

## 9. Ledger
| ID | claim | confidence | evidence |
|---|---|---|---|
| EV-OFF-0001 | ActionSelectorManager order/ids (§1.1) | Confirmed | `FUN_140525160` raw, RTTI vtables |
| EV-OFF-0002 | stop-test role of vtable +0x18 | Confirmed | `FUN_140525530` b2a.c:121–132 |
| EV-OFF-0003 | run selection every other frame per team | Confirmed | raw `0x1404f78a5`; live 198/201 |
| EV-OFF-0004 | rank table / pre-emption rule | High (code) | `FUN_140a40050`, `FUN_140a40000`, `FUN_140a40110` |
| EV-OFF-0005 | caps = `selectorVision.o` by attack level, TIKI_TAKA override | Confirmed | `FUN_140a4f520`, VFS blob, live caps both teams |
| EV-OFF-0006 | per-slot priorities inert (tables null) | Confirmed | image + live reads of `0x1436f2110/0x1436f2260`, live rows 1.0 |
| EV-OFF-0007 | Chance score 100 + s·x | High | disasm `0x140606575–0x140606584` |
| EV-OFF-0008 | minimum run times 11/54 f, PullAway 216 f, PostPlay 189 f | High (code), Confirmed for SecondLine/Diagonal minima | stop-test decompiles; live §5 |
| EV-OFF-0009 | support grid 32 × 70 × 45, layer meanings, %11 build | High | `0x140458b71…`, writer census |
| EV-OFF-0010 | support cell cost and search | High (code) | `e05c0r.s`, `FUN_1405e0180`, `FUN_1405e9f50` |
| EV-OFF-0011 | designated supporter cost and placement | High (code) | `FUN_1405eb980`, `FUN_1405ebca0` |
| EV-OFF-0012 | fps getter `141e532d0` = 54.0, not RNG | Confirmed | `[0x14351ae54]` |
