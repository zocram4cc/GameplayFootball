# PES contact port (ContactPort) — pass 2

Sources: `docs/re/pes-re-contact.md` §1–4, `pes-re-contact-2.md`, plus my own static decodes (Ghidra MCP, `dec.sh`, raw exe reads; listed per item). Code: `src/sim/onthepitch/pescontact.{hpp,cpp}` (pure layer), `Match::ProcessPesContacts` / `PesActionOf` / `TripFromContact` (`match.cpp`), playback rate in `Humanoid::Process`. Test: `tests/sim/pes_contact_test.cpp` (36 synthetic + 1 install). Mutations killed: veto 7 `&&`→`||`, hold-up `>`→`<`, veto 5 → `false`, push-away along-test → `true`.

## Commits
- `e23e177` pass 1 (level table, 4 vetoes, gauge).
- `dd4f31c` reverts my `docs/re/pes-re-contact.md` edit (port status lives only here).
- pass 2 commit (this report) — see final message.

## Pipeline now (per player per tick, after GF's pushes, `FUN_140844cb0` order)
1. **PES action** per player (`PesActionOf`): Movement → LOCO, or DRIBBLE when he owns the ball (PES DRIBBLE is not locomotion — pass 1 had them as LOCO, see "possession" below); BallControl with a feint → DRIBBLE (feint_through clips); Trap → TRAP; kicks/header → KICK; Sliding → SLIDE; Interfere → TACKLE / BLOCK / **JOSTLE** by the defender reaction that issued it (new `Player::SetPesChallenge`, set in `ElizaController::_ReactionCommands`); Trip type 1 → STAGGER, 2/3 → FALL; Catch/Deflect → KP handling. Action start refills the gauge (`0x1406eb85b`).
2. **Contact record** (`FUN_1408c31c0`): deepest part-on-part AABB overlap with an opponent, mapped onto PES's 19-bone enum (pelvis, thighs, shins, feet, trunk→chest, head, upper/forearms). Partner kept 0.5 s after the last touch (`FUN_1408440f0`, gs/2).
3. **JOSTLE** (new GF action state): challenger's contact time = `human+0x2f`, reset when the partner staggers/falls (`FUN_140781440`); **hold-up** `T = 120+120·clamp((PC_c+60−PC_j−20)/80)` frames@60 (imm `lea ecx,[rbx+0x3c]` @0x14084331f, floats 0x14259be1c/0x14253ee30/0x142596e6c/0x14256ae30) then `dS×0.005, vS=−3, dF/vF×0.5, forced×1.5` (0x1425b8708, 0x14259d654, 0x142565600); **shoulder-through** at the jostle's opening guard_back clip when `PC_j > PC_c + 5` (imm `add ecx,5` @0x140780c55; the opening clip is group 0x8eb from `FUN_140783700`, decoded) → carrier's sticky `ap+0xb10=2` → STAGGER cause 2 while he is in the jostle. Carrier touched by a JOSTLE challenger is in the jostle with him.
4. **Push-away** (decoded `FUN_1407a5cc0` @0x1407a65e0..0x1407a6640, clips `js_run_*_push_away` m0x1119–0x111d = the `FUN_140a6e170` set): dribbler, opponent foot < 0.5 m of his bone 7, ball 0.3 m ahead and < 0.5 m across, headings < 90°, opponent > 8 km/h, ball direction < 67.5°, opponent along < −0.1 m. Feeds the ×1.5 window and direct cause 1. **Correction to the report:** the "kick-window ×1.5" set `FUN_140a6e170` is the push-away jostle clips, not kicks.
5. **Level** `FUN_140842d20` with every modifier: keeper handling, **frontal standing tackle** (set `FUN_140a6e360` = the `tacklefoot_*` clips, decoded from the motion name table; GF TACKLE challenges are that set) `dS 0, dF 0.05` within 78.75°; **committed ×1.25** with the full predicate decoded at 0x1408430d6..0x140843232 (opponent down & self tackling; dodge either way; jostle in a tackle mode; kick before contact); push-away ×1.5 between 8/60–16/60 s; hold-up; receiving clash (both feet > 0.25 m, inside the hold window, PC gain 3; lenient +25/+22 before the clip's contact frame or in a TRAP, else +7/−6 — decoded at 0x1408435fe..0x140843651, the report had it reversed); running-carrier cap at 2.
6. **Forced falls** `FUN_140844540`: slide rule now gated by the slider's **bit 15** (decoded `FUN_140789d00`: slide phase 4, first 6/60 s) and by the slide-part = slide leg or body, above the knee or leg-on-leg, approach < 112.5°; from-behind (byte switch, off); displacement part 0 > +0x24 (+0x2c tackling), part 3 > 0.5.
7. **All nine vetoes** (0x14348cab0): V0 arms / partner arm unless keeper handling / leg-on-leg with idle legs / trailing foot (−0.25·scale) / **keeper spared** (decoded `FUN_140a638c0` = "is the team's role-0 player", via `FUN_140a62610`); V1 keeper throw/punt, own kick or tackle before contact vs a non-playing opponent (DRIBBLE/TRAP excluded), dodge > 45°; V2 fallen / getting up within the 0.3 s rise-up blend; V3; V4 kick/trap/keeper before contact; V5 dodge; V6 opponent running away (opponent not in the `FUN_140a6e050` play set = dribble/trap/kick/slide/tackle/block, decoded from the property table mask 0x34000); V7 both LOCO, or LOCO vs an opponent past his last hit frame; V8 slow touch incl. the past-contact-by-10/60 s branch.
8. **Direct triggers** (§1.6): cause 4 (tackle foot at bone 10, **0.2 m with Gamesmanship** — skill 0x15 = engine bit 21 MALICIA per carrier-ai §6.1), cause 5 (studs on a running carrier), causes 3/6 (feint-through cut, opponent in line), cause 7 (sub-kind-1 tackle at contact ±4 frames, imm @0x140845852), cause 1 (push-away foot at bone 7). Nearest opponent that is not his keeper.
9. **Measures**: displacement from the tracked parts over GF's rigid body (part 0 = root offset less this tick's push, parts 1–3 its 2D length; slide-leg 0.2 m); closing speed = both velocities less their push this tick, projected on the pair line (`FUN_140843a20`). **Correction:** the report's "0.6944/gs s ahead" is not a look-ahead — `FUN_140a6c6d0`'s third argument is a 2.5 km/h speed floor below which a state-5 player's velocity is replaced; there is no prediction.
10. **Playback** (§4.3): STAGGER `0.95+0.15·clamp((Bal−40)/59)²` (0x1425ab124/0x1425c48a0), FALL 1.0 until the clip's lowest body frame, then rise-up `0.95(1−b)+1.1b`, `b=clamp((Bal−40)/60)` (0x1425a3590). GF's frame counter now advances by an accumulated `playbackRate` (new `Anim` fields), clamped to the last frame.

## Bugs found and fixed (the pass-1 possession / fouls question)
Evidence (instrumented runs, seed 7, 20 min):
- **Possession 85–15 was a bug.** Pass 1 classed a dribbling carrier as locomotion, so veto 7 (both running) cancelled every carrier contact, and every body contact was "body part" so a slider within 0.72 m forced a fall through the "above the knee" rule. Counted: 2 800 accepted trips in 20 min, 848 of them slide forced falls through plain body proximity. After the fixes possession is 47–53 / 53–47.
- **Fouls doubled — also bugs, not PES:** (a) the slide rule ignored PES's 6/60 s sliding flag; (b) `TripFromContact` notified the referee even when GF refused the trip (`TripMe` silently ignores sliders and special-state clips), so a slider touching another slider produced a TripNotice every tick. `TripMe` now returns whether it took the trip, and only accepted trips reach the Judge. Accepted reactions fell from ≈3 000 to ≈80 per 20 min (PES live: 44 in 13 min, report §6).

## Deleted GF behaviour
- Pass 1: the trip-sensitivity sum, the tackle-AABB trip types and frame windows, `GetDecayingPositionOffsetLength`.
- Pass 2: the per-pair `ResolvePesContact` / `CheckTackleLegContact` / `UpdateBalanceGauges` (replaced by one per-player pass), the body/body part assumption, `pesContactReacted`.

## Ponytail approximations (each `ponytail:` in code)
- GF's body is rigid: all tracked parts share the root offset (parts 1–2's `FUN_140843e30` test folds into part 3).
- Hit window = lead-in to the clip's touch frame (GF clips have no PES hit/hold window data).
- Slider's bit 15 window opens at the slide clip's touch frame (GF slides have no phases); slide leg = the slider's foot furthest along his heading (GF slides carry no foot tag).
- Trunk stands for both bone 7 and bone 8.
- Contact flags +0x62a/+0x62d decoded as per-part-group deep-overlap (depth ≥ 0.23 m, `FUN_1408c31c0`); GF's AABB contact has no part groups, read unset.
- Mutual jostle (carrier touched by a JOSTLE challenger counts as jostling) — INF from the writer/reader pair of `ap+0xb10`.
- Team-mate contacts are not run: PES requires 0.66 m capsule overlap for team-mates, which GF's geometry never reaches.

## Not ported (what I tried)
- **Forced-fall second-hit branches** of `FUN_140844540` (report §8 item 4, open in the RE) — allowed out.
- **DODGE as a source**: the vetoes/committed rules consume DODGE, but nothing in GF enters it; DODGE comes from defender reaction entries 4–7 (BALL_DODGE `0x140a070e0` …), owned by the defender-decisions port, whose report lists them unported.
- **Tackle sub-kind** (`human+0x44` ← Tackle action `+0x68`, publisher `FUN_140791dd0` decoded): its writer was not found — byte scan of `0x14078c000–0x140793000` for `mov byte [r+0x68]` forms found none. GF tackles count as the general sub-kind (mode 1 rule), so cause 7 and the sub-kind 2/4 level branches never fire.
- **V4 tail** (leg classes with zero separation, head-height test after the contact frame) and **cause 5's** two 0.8/0.7 m distances and 0.35 m foot-height test: their operands are body-dimension slots (`FUN_1406e6a40` indices 4/5/7/8) I did not decode.
- **Stagger early exit** (`min(5, 6−⌊6f²⌋)` frames after the clip's cancel frame) and the FALL rise-up gate (ball within 10 m): GF trip clips carry no cancel frame and play the rise-up inside the same clip.
- **Blend-in time** [0.1, 0.4] s: GF's anim smoothing has no time-bounded blend to set.

## Smoke (teams 1 v 2, 20 min, worktree root)
| | before (6585420), seed 7 | pass 1 (e23e177), seed 7 | pass 2 (73e6377), seed 7 | pass 2, seed 11 |
|---|---|---|---|---|
| score | 1–0 | 1–1 | 1–1 | 0–0 |
| possession | 41–59 | 85–15 | 53–47 | 53–47 |
| shots | 3–1 | 5–5 | 8–8 | 7–3 |
| "tackles" | 13–19 | 31–31 | 37–42 | 23–23 |
| "slides" | 51–27 | 44–65 | 1–0 | 0–2 |
| fouls | 6–10 | 13–18 | 0–0 | 0–0 |
| yellow | 2–1 | 2–5 | 0–0 | 0–0 |
| accepted contact reactions | — | ≈2 800 | ≈80 | — |

Read the challenge columns with care: `Referee::TripNotice` counts them, so "slides" means trips where the other player was sliding (type 3 = a PES FALL against a slider). Under PES's rules a slide's leg on the shin mostly gives a STAGGER (type 1, counted under "tackles"). It only gives a FALL inside the slider's 6/60 s bit-15 window. That is why "slides" collapsed and "tackles" rose. Fouls 0–0 on both seeds: the Judge now only sees the ≈80 accepted reactions (PES live: 44 knock-downs in 13 min, report §6). I did not measure a PES live foul rate to compare against, so whether 0 fouls in 20 min is PES-like is **open**. The seed-11 baseline was not run because the baseline worktree had been removed.

---

# Pass 3 (rebased onto main 143c0fd)

Commits: `cf0134f` pass 1 · `cd49128` revert of my docs/re edit · `7e31a47` pass 2 · `82ec796` shin flags · `bdc643c` Judge feed.
Rebase conflicts: `tests/sim/CMakeLists.txt` (kept both main's kick-error/team-concepts tests and pes_contact_test), `player.hpp` includes (kept both `pesrandom.hpp` and `pescontact.hpp`). Gate: 511 tests, 2 standing skips, `SameSeedPlaysTheSameMatch` OK.

## 1. Fouls (why pass 2 got 0, and the fix)
Root cause: the Judge was fed only through `Referee::TripNotice`, which kept GF's own gates: the fouled team's possession > 1.1, the fouler in an Interfere/Sliding clip, the ball within 2 m, and type 3 needed for slides. It also took "victim fell" from the trip type and always used the geometric approach angle. Under PES rules most reactions are staggers from opponents who are just running, and none of those got past GF's gates.
PES (pes-re-duels-fouls §2.2–2.3, plus my decode of `FUN_1405b6d40`):
- the gate `FUN_14050d6a0` admits the contact when the players are on different teams and (B played the ball, or B targeted A, or A is staggering or down);
- the points come from B's own action class: slide = reckless 35; tackle 25; anything else 0. If A fell, anything else (jostle or a plain contact) still gets the `else` branch's +10. The live "type 0 contact" foul, 48 points, is this branch;
- P_angle's θ is `ai(B)+0x12a0` **only when B targeted A**, otherwise 0.
Ported in `Referee::ContactEvent` + `pes::JudgeAction::Other`, with `victimFell` taken from the victim's real state. TripNotice and its gates are deleted.
`ponytail:` I did not find who posts event 0xf. I searched `FUN_1408c48c0`, Contact::Start `FUN_14072f3b0` and the dispatcher `FUN_1404514f0`, and scanned `.text` for `mov [rsp+x],0xf` before calls. The event is raised here on every accepted STAGGER/FALL; the gate admits those through its "A staggering/down" clause. Feeding it on every touch-begin instead gave 28–41 fouls per 20 min, far above PES live.

| fouls per 20 min (teams 1 v 2) | seed 7 | seed 11 |
|---|---|---|
| main 143c0fd (GF trip system) | 13–20 | 12–4 |
| pass 2 (7e31a47) | 0–0 | 0–0 |
| pass 3, touch-begin feed (rejected) | 12–16 | 21–20 |
| pass 3, reaction feed, θ always | 8–8 | 6–4 |
| **pass 3 final (bdc643c)** | **2–3** | **1–6** |
| PES live reference (duels-fouls §6.1) | 2 fouls in a 20-min capture (88 pts sliding, 48 pts contact) | |

Final smoke, other numbers. Seed 7: 0–3, possession 65–35, shots 7–11, "slides" 5–7, yellows 0–0. Seed 11: 0–0, possession 45–55, shots 7–1, "slides" 4–12.

## 2. DODGE — still has no source (decoded, not ported)
Decoded predicates for entries 4–7: `0x140a070e0` BALL_DODGE, `0x140a07dd0` GOAL_NET_DODGE, `0x140a08340` POST_DODGE, `0x140a089e0` HUMAN_DODGE.
- **Correction to the defender-decisions port:** the feature gate `FUN_140ca1a50` is `mov al,1; ret`, so it is always on. HUMAN_DODGE is the dodge of an *opponent* within 3 m (dist² ≤ 9.0 @0x14259d60c), not a pad-only entry, and all four are live for the CPU.
- What blocks the port: every one of them commits only when `FUN_1406b4580(anime, 0x15 DODGE, …)` finds a dodge clip that solves the geometry (`FUN_1406b4c10(anime,0x15)` gates first). That is the Dodge action's own clip search over PES dodge motions, and GF has no dodge clips.
- Driving DODGE from the geometry alone would skip the clip solver and fire on every opponent within 3 m, so I left it out rather than fake it.
- Next step: import PES's dodge clips (anim importer), then port these four predicates plus the Dodge selector.

## 3. Tackle sub-kind writer — found
`human+0x44` ← Tackle+0x68 (publisher `FUN_140791dd0`). Writer: `FUN_14078b550` stores `uVar6` at `(Tackle+0x28)+0x40`. It is called from `FUN_140791870(Tackle+0x28, …)`, which is called from slot 13 `FUN_1407902c0`; the record is passed by pointer, which is why the `[reg+0x68]` byte scans missed it.
- The sub-kind is the first kind 1..8 whose bit is set in the readiness mask `FUN_14078e5a0` (bit k ⇒ kind k) **and** for which `FUN_14078a640` solves one of the kind's clips (tables `FUN_14078fe90` @0x1425e9d88 / 9c58 / 9cb0 / 9be0 / 9a40 / 9a50 / 9b50 / 9db8).
- The default is 7 when nothing is ready.
- Not ported: like DODGE, the result depends on PES's tackle-clip solver. GF tackles stay the general sub-kind, so cause 7 and the sub-kind 2/3/4 branches still never fire.
- The kind tables' ids are not motion-table indices; reading them through 0x14351e590 gives kick clips, so the per-kind clip families are still unnamed.

## Also in pass 3
- Veto 0's contact flags are decoded: record `+0x628+part` for parts 1..6 are the per-leg-part contact flags (the injury roll reads the same bytes, stamina-drain §6). `+0x62a`/`+0x62d` = both shins in the contact. Ported as `selfBothShinsTouching`, computed from GF's shin AABBs.
