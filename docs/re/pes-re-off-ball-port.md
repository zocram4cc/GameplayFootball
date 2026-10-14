# Off-ball runs port (OffBallPort), pass 2

Branch: the OffBallPort worktree, rebased onto main 143c0fd (TeamConcepts). Code: `src/sim/onthepitch/pesoffball.{hpp,cpp}`,
`TeamAIController::{ProcessOffBall, ProcessDesignatedSupporter, BuildRunWorld, ProcessRunManager}`, `Strategy::RequestOffBallTarget`,
`Match::RecordPassEvent` (+ humanoid pass site). Tests: `tests/sim/pes_offball_test.cpp` (21 synthetic + 1 install).

## Done
### Pass 1 (rebased commit)
Manager cadence (team parity frame), rank table FUN_140a40050/40000, caps from `selectorVision.o` by attack level, shared
select (sort, cap, per-tick cap 11 for Chance), Chance score 100 + s·x, lane search FUN_14059ba60 (33 lanes, S grid, blocked
mask, penalties, budget, commit head gates + leaf FUN_14066d720, fallback FUN_1406067c0), FUN_1405997f0 defender prediction,
designated supporter FUN_1405eb980 (sticky) at FUN_1405e0c60's distance. Deleted GF: SelectAttackingRunPlayer, 500 ms probe,
counter window, forwardSupportPlayer, force-field run spot, GetAttackingRunAffinity.

### Pass 2
- **Rebase** onto main: Support Range / Build Up now read from `teamSheet` (TeamSheet.supportRange, toggles[kBuildUp]); main's
  deletion of the counter block / zone pressure kept; Tiki-Taka = false (main carries no advanced instructions).
- **World model** `RunWorld` (both teams, global ids team·11+slot): positions, velocities, PES-convention facings, run
  state/time, role class (FUN_1408c51f0 map, as Main gave), registered position T+0x4c00, run target, slot point
  (GetAdaptedFormationPosition), marker (GF man-marking), ball owner, focus player FUN_1405969b0, team lines, possession
  flag/age (main's possessionChange_ms), last pass event (new `Match::RecordPassEvent`: kind 1 ground / 2 through (GF long pass)
  / 3 lofted, kick point, target).
- **Stop test** FUN_140688c00 ported as a cascade (steps 1, 3, 4, 6, 8–14, 16, 17, 19, 20) with leaves 68f600, 692810, 692a00,
  692580, 68a730, 68a8c0, 694d10, 68dba0, 68de30, 68e300, 596240 (role-class map), 68efc0 (partial), 68e900, 68ec50, 68d920,
  68b070, 692c50, 68b920 (+643f50/643a30 partial), 691ef0, 692fe0, 690b90, 690630, 690400, 690cf0, 694650, 693a20, 693730,
  68e660, 68b440, 68c050, 692ea0, 691a80, 68d0b0, 690960 (partial), 693ef0, 68d730, 68fc70, 68cde0, 68cc40, 68ac40 + 694ee0
  (decompiled here), lane intercept 69cd40, cones a4e3f0/a4fbc0, a53da0. Fallthrough is PES's STOP.
- **Chance mask** FUN_140606cd0 (decompiled here), generic-lane path: attack-slot mask T+0xb8b8 = FUN_140500200 (decompiled
  here: level → need {0,2,4,6}, all slots until enough attackers are ahead (a4f230, decompiled), else positions ≥ 7 plus the
  lowest goal-distance scores), FUN_1405938a0/593290 team-may-run (decompiled: own half needs a free forward-facing focus
  player or 4 s possession from −15 m), FUN_140593430 gates (raw constants −0.1 line, 6.5 m opponent, F4, 596240), depth F8,
  position-gated reach with Build Up Long base 21 (0x1425c84bc), |dz| ≤ 42, 79ac tail (6046b0/609b40/609e60 decoded,
  inert for GF inputs). Replaces the pass-1 `ChanceReach_m` (deleted, it also mis-handled dz ≥ 30).
- Decompiled this pass (scratch): 5a4790, 596c30, 695110, 68cb80, 643f50, 5a5ff0, 44e020, a53da0/df0, 655660, 653480, 694ee0,
  654e60, a75740, 606cd0, 6046b0, 609b40, 609e60, 5a8290, 5a5400, 5938a0, 60a750, 593430, a4f230, 5a0d20, 5a5b20, 60a100,
  592cf0, 593290, a4c930, 500200 (b8b8 writer, found by a displacement scan).

## Smoke (seed 7, 20 min)
| | pass 1 | pass 2 (final) |
|---|---|---|
| score | 2–1 | 0–1 |
| Chance runs started | 357–615 | **576–445** (PES live ≈ 93 per team per 20 min) |
| passes | 39–41 | 52–41 |
| shots (on target) | 10–19 (3–9) | 15–17 (7–3) |
| possession | 42–58 | 51–49 |
Intermediate: faithful stop cascade with the old reach mask 2439–1779 (the PES fallthrough is STOP); + b8b8 mask 674–740;
+ team-may-run 757–968; + 593430 gates 576–445. Still 5–6× PES. Last stop census (pre-593430): role-class test 596240
(472, mean 46 f), pass to him (318), possession (250), step-13 leaves (207).

## Not done (exact next step)
1. **Overrun**: remaining Chance gates not reduced: special lanes (60a750 sector sweep, 608570/87a0/9350/8ac0), 5a8290
   (large; several float operands unresolved), 5a5400, the rank > 1 angle sub-test (a4c720 args dropped → raw walk
   0x1406077xx), MA+0x68/+0x60 team ints, claim gates 5ab180/5abcc0/5ab380/5ab990 (over-cap swap), commit-tree leaves of
   FUN_140664ee0 beyond 66d720. Stop-test leaves not ported: 5a7fe0/7be0/7d10 (PES action records), 68bd90 (T+0xb824),
   5a4b90 (opponent mode T+0xa50c), 5a40a0→5a3080 (partial), 5a1cc0 + 68aeb0 (need 654e60 time-to-reach: decompiled, its two
   implicit 43a0a0 angle arguments need a raw walk at 0x140654e8x), 694320, 68b1a0/6900a0/6910c0 (AI class), 68b6e0, 693340,
   694bc0/68fa10, 691300/691d10 (sub-kinds). Fixed stand-ins (named in code): AI class 0, sub-kind 0, marks 0, concept slot
   0x30, no style/skill bits, no instructions, non-runner AI target = position + 1 s velocity.
2. **Selectors** Overlap, Counter, SecondLine, Diagonal, PostPlay and the 4 s re-run block: not started (Main's stop order).
3. **Support grid** (32 layers) and cell search FUN_1405e9f50: not started.
4. **Action array**: PressLayerPort's `pes::TeamPressState` has no action array (their reply); the `runs` map stays.
5. Full ctest gate not re-run this pass (owner's close-out order); see final message for what ran.
