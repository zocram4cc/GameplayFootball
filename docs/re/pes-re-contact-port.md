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

---

# Pass 4 (DODGE / tackle sub-kind clips) — scope found, not started in code

Rebased onto main: clean, no new code commits.

## What the pipeline actually is in GodotFootball
- The GF sim loads only GF-native `.anim` files from `assets/gf_anims` (293 files). `AnimCollection::Load` still has a hook for imported clips under `<family>/pes/`, plus `Humanoid::PreferImportedAnims`, but nothing in this repo produces them.
- `gani_to_anim.py` and `reconvert_installed.py` live in the old `~/Code/GameplayFootball` repo, not here. GodotFootball reads ganis only on the Godot side (`pes/clips`, `rig/ClipSampler`, `gdext/pes_animation`). The sim already holds the rig (`MatchSetup::rig`).
- So "import through the pipeline" means: port `gani_to_anim.convert` into the sim (ClipSampler → GF node keyframes, Fox→GF basis `(x,−z,y)`, gameplay position scale), load the result in memory, and synthesise GF metadata (type / special vars, mirrored copy, `_PrepareAnim`). Clips would be built at match start from the user's install, so nothing is committed.
- Playback: GF has no "play this exact clip" command, but the `special` type with `specialvar1/2` is a forced-clip path that already exists.

## DODGE, decoded so far
- Dodge action vtable `0x1425df820` (RTTI `.?AVDodge@action@anime@match@@`, COL `0x142c9c840`).
- Slot 13 `FUN_14073bc70` dispatches on the dodge kind (`cmd+0x1094`, set by the four predicates):
  - 1 ball → `FUN_14073bd80`;
  - 2 goal net → `FUN_14073c5d0`;
  - 3 post → `FUN_14073c9c0`;
  - 4 → accepted without a selector;
  - else, human → `FUN_14073c520`.
- The name table at `0x14349af78…` lists the clips: `dodge_human_*` ×21 (jumplow, jumphigh, jumpslide, slide_rapid_up/down, decelerate, stop, block_back), `dodge_ball_{0,2}_0_{crouch,slide_low/mid/high}` ×8, `dodge_net_*` ×5, `dodge_post_*` ×4.
- Predicates: `FUN_140a072d0`, `FUN_140a07e40` and `FUN_140a084d0` are decoded (scratch `dodge.c` / `dodge2.c`). `FUN_140a07bf0`, `FUN_140a07570` and `FUN_140a07880` are not yet.
- The selectors go on into `FUN_140736ce0` and `FUN_1407358e0`, and the motion-group lookup goes through `FUN_1406abb30` / `FUN_1406abdd0` (group+angle → clip). That group machinery is its own RE area: anim-select §4.2 only describes it, it does not decode it.
- Correction kept from pass 3: `FUN_140ca1a50` = `mov al,1; ret`, so all four dodges are live for the CPU.

## Tackle sub-kind, decoded so far
- Writer `FUN_14078b550` (via `FUN_140791870` ← slot 13 `FUN_1407902c0`).
- It needs the readiness mask `FUN_14078e5a0` (decoded, scratch `tk5.c`), the per-kind tables of `FUN_14078fe90`, and the solver `FUN_14078a640` (not decoded).
- The table ids are not motion-table indices (read as motions they come out as kick clips), so they index the tackle selector's own group table, which is not decoded.
- Making the sub-kind real also means GF's standing tackle plays PES tackle clips, which amounts to porting the Tackle action and replacing GF's interfere clips.

## Staged plan (each stage commit + gate + smoke)
- **A.** Runtime gani→GF importer in the sim (C++ port of `convert`), the clip list carried in `MatchSetup`, and a forced-special playback path. Plus a unit test on a synthetic gani and an install test.
- **B.** DODGE: decode the three remaining predicate helpers, the four selectors and the group→clip lookup; port entries 4–7 into `ChooseReaction` (after receive, before block); the PES action becomes DODGE while the clip plays.
- **C.** Tackle: decode `FUN_14078a640` and the group table, port the readiness mask and the sub-kind pick, and play PES tackle clips.

## Stage A — done (`d78d064`, on main `212a7cf`)
- `src/sim/onthepitch/pesclips.{hpp,cpp}`:
  - `LoadPesClips(exe, vfs)` takes every motion in the exe's motion table (`pes::MotionNames`) whose name starts with a family prefix (`dodge_`, 67 clips) and pulls the gani from the install (`FindBodyClip`).
  - `PesClipAnimText` is a C++ port of `gani_to_anim.convert`: `rig::ClipSampler` at `PES_FRAME_S` and the gameplay scale, the verbatim `(x,y,z)→(x,−z,y)` map on the 20 body joints, `body` = RIG_ROOT∘mover, and a `player` line for the translation from the first frame, written at 10 ms per frame.
- `Animation::LoadText` loads the .anim text from memory. The file loader shares the same `LoadLines`.
- `AnimCollection::AddPesClips` adds each clip as `special` with specialvar1 = family (100 = dodge) and specialvar2 = motion number, authored and mirrored, through `_PrepareAnim`. GF's existing special query is the forced-clip path.
- `MatchSetup::pesClips` is required (Match throws without it) and is wired in `gfsim_run`, `pes_match_constants` and `pes_library`/`gf_match`. Nothing PES-derived is committed.
- Tests `pes_clips_test` (3):
  - a synthetic clip checks the basis map, the 10 ms resampling and the player line; the basis-map mutation fails it;
  - the install test converts every dodge clip and checks its length;
  - the install test checks that a special query (family, motion) returns exactly that clip and its mirror.
- Gate green, `SameSeedPlaysTheSameMatch` OK. Smoke seeds 7/11 are bit-identical to main (`56643e20…`, `fc6fccc6…`): the clips are loaded, but nothing selects them until stage B.

## Stage B — decode status
- **Every dodge predicate ends in the same call.** All of them (`FUN_140a072d0`, `07570`, `07880`, `07e40`, `084d0`, `089e0`) end in `FUN_1406b4580(anime, 0x15, ball, params{kind})` → Dodge slot 13 `FUN_14073bc70`. The decision is the selector's clip fit, so B = predicates + selectors + clip fit.
- **Decoded:** `FUN_140a07bf0` (ball-dodge eligibility: the player is not in play/receive classes; ball-owner rules; `5/48·fps` frames since the last event), `FUN_140a07570`, `FUN_140a07880`, and the ball selector head `FUN_14073bd80`.
- **Still to decode:**
  - the selector helpers `FUN_140736ce0` and `FUN_1407358e0`;
  - the group→clip lookup `FUN_1406abdd0` / `FUN_1406abb30` (the motion-group selector tables);
  - the net, post and human selectors `FUN_14073c5d0`, `c9c0`, `c520`.

## Stage B — decode progress (run 2; rebased onto main, nothing committed)
- **Clip choice uses the Animetable.** Dodge clip selection is a row/column lookup in `Animetable.bin` (animselect §2.3: 3045 rows × 18 angle columns, `motionNo+1` and a mirror bit; row labels at 0x1434997f0). `FUN_1406abdd0` tests whether a column is populated; `FUN_1406abb30` returns the motion.
  - GodotFootball has **no Animetable parser yet**. It is needed by B and by C, so it is the next code item: `src/pes/animetable.{hpp,cpp}`, read from `common/anime/AnimeTable/bin/Animetable.bin` via the VFS, with row labels from the exe.
- **Ball selector** (`FUN_1407358e0`, decoded; scratch `dsel3.c`):
  - a row table on the stack: 0xbe5 default, a 3×5 grid from .rdata `0x1425e6650…0x1425e6678`, plus 0x306/0x30a;
  - the grid index is a ball-height class from `FUN_1406e69f0` at two look-ahead times (`3/60` + offset and `6/60` s), and the side from the ball bearing (> 45°);
  - column 9; the mirror is decided by `FUN_140735700` and the clip's hit-point positions (`FUN_1406e8690`).
- **Look-ahead offset** `FUN_140736ce0`: `gs·12/60` frames when `FUN_140736d30` ∈ {0,1,2}.
- **Dodge gate** `FUN_14073cbc0`: the current action must allow DODGE (actions 2–10, 0x14, 0x4f through the action's own slot 0x70; TACKLE only after `ap+0xae4`; 0x49/0x4a always).
- **Human selector** `FUN_140735e10` (scratch `dsel4.c`):
  - about 600 lines: row 0xbe5 / column 0x12 by default;
  - kind from `FUN_14073a3d0`, row from `FUN_140739f90`, column/variant from `FUN_140735870`;
  - **one per-player LCG draw** (`FUN_1406e6bf0(ap,1000)`, stream 2) picks the mirror side in the default case.
- **Next to decode:** `FUN_140736d30`, `FUN_1406e69f0`, `FUN_140735700`, `FUN_1406e8690`, `FUN_14073a3d0`, `FUN_140739f90`, `FUN_140735870`; the net/post selectors `FUN_14073c5d0` / `FUN_14073c9c0`; and the Dodge action's playback (start/update slots 4/5/14) for the clip's root motion and cancel window.

## Run 3
- **Committed `c977358` `pes::Animetable`** (`src/pes/animetable.{hpp,cpp}`, test `tests/pes_animetable_test.cpp`, 4 tests):
  - general: all rows × columns, motion number + mirror;
  - every layout number is an opcode-checked exe immediate: rows `0x1406aa4df`, columns `0x1406aa53a`, motion mask `0x1406aa4e5`, mirror shift `0x1406aa50a`;
  - the hard-coded patch cell `0x1406abb3a/43/49`;
  - the column picker `FUN_1406de990`: split column `0x1406dea47`, default column `0x1406de9b1`, floats `0x14259d618/0x142598580/0x1425de6fc/0x1425a9668/0x14259858c`;
  - row labels from 0x1434997f0.
  - Mutation of the split column fails the column test.
- **Dodge helpers decoded** (scratch `dh.c`):
  - `FUN_140736d30`: speed class — 0 unless the movement is within 45° of the heading and speed ≥ 11 km/h (0x14259bdfc); 1, or 2 at ≥ 22 km/h (0x1425c48c0).
  - `FUN_1406e69f0`: the ball-height band `FUN_140a6d380(height, body scale)`, i.e. contact-2 §3's 0.335…2.585 × scale bands.
  - `FUN_140735700`: mirror side from the ball bearing. The relative angle is clamped to a per-speed-class window (class 0: `0x14259d674`..`0x14253ee30`, class 1: `0x1425aa1c8`..45, class 2: `0x1425cfa50`..22.5); the flag is "clamped by more than 0.1°".
  - `FUN_1406e8690`: a clip's hit-data position × clamp(body scale, `0x1425ddf78`, `0x1425ddf80`).
  - `FUN_14073a3d0`: the human-dodge **kind 1..8**, from the opponent's predicted position 3/60 s ahead, the contact record (part 7 = belly on both sides, overlap > `0x1425e6640` / `0x1425e6648`) and the opponent's action (0x50 → 3, 0x14 …). Partly read; it continues past the excerpt.
- **Still to decode:** the rest of `FUN_14073a3d0`, `FUN_140739f90` (kind → row), `FUN_140735870` (kind → variant), the net/post selectors, and the Dodge playback slots 4/5/14.

## Run 4 (decode only; rebased, nothing new to commit)
- **`FUN_14073a3d0`, human-dodge kind (rest):**
  - kinds 4/5: the opponent's contact direction and heading both off by more than 150° (0x14259be68), bearing < 45°, the opponent not the ball owner, distance² > 9.0; 5 when he is in FALL_DOWN before his frame `+0x22`;
  - otherwise `FUN_14073aa20` (kinds from the opponent's action classes; scratch `dh2.c`, not yet read).
- **`FUN_140739f90`, kind → Animetable row:**
  - row table **`0x14344efb0`**: `int[8 kinds][3 speed classes]`; speed class 0 below 13.5 km/h, 1, 2 above 22;
  - for the ball owner it steps down the speed classes until the row's column 9 is populated **and** the clip's hit window (from `FUN_1406b0250` / `b03d0` / `af8e0` / `e9050`) fits the ball;
  - row 0x2f8 becomes 0x2f4 or 0x2f9 (frame parity), and 0x2ff becomes 0x302 when the player's AI action is 0x11; no row = 0xbe5.
- **`FUN_140735870`, kind → column:** kinds 0–2 → 9, 3–5 → 5, 6 → 7, 7 → 8, else 11.
- **New data dependency:** the row choice reads each clip's **HitData / Animation records** (Mbinfo: hit frame, contact position; animselect §2.1–2.2). GodotFootball has no Mbinfo reader, so `src/pes/mbinfo.{hpp,cpp}` (HitData + Animation.bin, from the install) comes before the B port. Stage C's tackle solver `FUN_14078a640` needs it too.
- **Next, in order:**
  1. the `pes::Mbinfo` reader (commit);
  2. `FUN_14073aa20`, the net/post selectors and the Dodge playback slots 4/5/14;
  3. the B port: the predicates, the four selectors over `pes::Animetable` + `pes::Mbinfo`, reaction entries 4–7, and the DODGE action through the stage-A special clips; commit, gate, smoke.

## Close-out (run 5, owner's stop order)
- **Committed and not yet on main:**
  - `c977358` `pes::Animetable` (4 tests pass)
  - `0effd48` `pes::Mbinfo` (Animation / HitData / CancelData readers; 3 tests pass, including the install test)
- **Not run for these two commits:** the full gate and the 20-min smokes. Each commit's own test binary passes. They add pesfmt readers only and do not touch the sim.
- **Done overall:**
  - contact passes 1–3 (merged);
  - stage A, the clip importer (merged);
  - the Animetable and Mbinfo readers (above).
- **Not done:**
  - stage B (DODGE): decode is partial — the remaining items are listed under "Run 3" and "Run 4";
  - stage C (tackle sub-kind / PES Tackle action): not started.
- **Next step:** read `FUN_14073aa20` (decompiled in scratch `dh2.c`, not read), then the net/post selectors (`FUN_14073c5d0` / `c9c0`) and the Dodge playback slots (`0x140734a80`, `0x140734a30`, `0x14073b680`). Then port B as planned in "Run 4".
