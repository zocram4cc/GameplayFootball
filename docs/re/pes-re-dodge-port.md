# DODGE port (ContactPort stage B) — done

Commits on `dodge-port-b` (worktree <repo-worktree>, rebased onto
main `221b7cd`):
- `f38b190` pesclips: the dodge family carries the human, net and post dodges too
- `01b4f32` pesdodge: the dodge selectors over the Animetable and Mbinfo
- `dd8adee` sim: the dodge reactions drive the Dodge action's clips
- `f2031b1` gdext: pes_library carries the dodge selectors' install data

## 1. Corrections to the record (two, both material)

- **Motion-name table is `0x14351e590`, not `0x14349af78`.** `pes::MotionNames`
  (`exe.cpp`) reads `0x14351e590`; the earlier scratch extracts used the other
  table, which is why the row ids resolved to the wrong clips. With the right
  table (4,389 entries) every dodge row resolves to the correct family.
- **Stage A loads 8 clips on this install, not 67.** The 67 was a different
  install/table. `dodge_` covers only the 8 ball dodges. The human dodges are
  the `avoidjump_*` / `avoidslide_*` clips, the net dodges the `goalnet_*` and
  the stop/decelerate the `rapidstop_*` / `rapid_3_2_dodge_*`. Stage B adds
  those prefixes to `pesclips.cpp`'s family table (`e0fbb35`).

## 2. Decode — every open item closed

All four predicates, all four selectors, the kind→row and ball-grid tables,
the kind→column jump table, the speed class, the ball-height band and the
look-ahead. The post row/column (`FUN_1407368d0`) is the one item left open
(see §5). Decompiles: `<scratch>/`.

## 3. The port

`src/sim/onthepitch/pesdodge.{hpp,cpp}` (pure layer):
- `BallSelector` (FUN_1407358e0 + 73bd80), `NetSelector` (FUN_14073c5d0),
  `KindColumn` (FUN_140735870), `KindRow` (FUN_140739f90 with the column-9
  hit-window step-down), `SpeedClass` (FUN_140736d30), `BallBand`
  (FUN_140a6d380).
- Every exe table is read through the instruction that references it:
  the kind table's two `LEA R8,[RIP+..]` (0x14073a076 / 14073a26c), the ball
  grid's `MOVDQA` (0x14073593e) and the band table's `LEA RCX` (0x140a6d383),
  plus the kind-column jump table's per-case `MOV EDX,imm32`. A different
  executable fails the load instead of reading wrong numbers.

Wiring (`f3af366`):
- `DefenderReaction` gains `kDodgeBall/kDodgeNet/kDodgePost/kDodgeHuman`;
  `ChooseReaction` runs them between receive and block, in PES's table order.
- `ElizaController::_ReactionCommands` runs the four predicates (each carrying
  its own kind), and the winner issues a Special command naming the clip by
  (family, motion) — the stage-A forced-clip path — with
  `SetPesChallenge(kDodge)` so `PesActionOf` maps the clip to
  `ContactAction::kDodge` for veto 5 and the committed flag.
- `MatchSetup` / `Match` / `gfsim_run` / `pes_library` / `gf_match` /
  `pes_match_constants.hpp` carry the dodge constants, the Animetable and the
  Mbinfo.

Tests:
- `tests/pes_dodge_test.cpp` (9 tests): the selectors, the kind column, the
  speed class, the ball band, the kind-row step-down, and one install test
  (every kind resolves to a row, the net rows resolve to the goal-net family,
  the ball row resolves to the jump row).
- `tests/sim/pes_defender_decisions_test.cpp` gains the dodge-entry order
  test (the four dodges sit between receive and block; tackle/slide/receive
  still win).
- `tests/sim/pes_clips_test.cpp` updated for the widened dodge family.

## 4. Finding to carry forward

**Kinds 3-6 map to columns 5 and 7, and this install's dodge rows are not
populated at those columns.** `FUN_140735870`'s jump table is unambiguous
(3-5 → 5, 6 → 7), and the rows (`0x2fb`, `0x2fd`, `0x2f8`, `0x303`) are only
populated at columns 8/9. So the human dodge completes only for kinds 0-2
(col 9) and 7 (col 8) on this install — that is the install's own table, not
a port gap. The port returns "no clip" honestly for 3-6.

## 5. Ponytail gaps (named, in code and here)

- **Post dodge does not fire.** `FUN_1407368d0` (its row/column) was not
  decoded — it reads the post's own geometry table. The entry is wired through
  the chooser and the command; the predicate returns false until that table is
  decoded. `_DodgePostCandidate` and the report carry the gap.
- **Human dodge kind (FUN_14073a3d0)** reads PES's per-part contact record and
  the opponent's action classes, which GF does not carry per part. The ported
  part is the geometry: approaching from behind → slide_rapid_up (kind 4), in
  front → jump (kind 1), beside → slide (kind 3).
- **Net dodge's geometry table** (which goal-line positions admit it) is open;
  the selector's clip fit is the ported gate, GF's own pitch bounds the
  near-goal test.
- **Ball dodge's eligibility** (FUN_140a07bf0's play/receive action classes and
  the last-event frame) is not carried; the selector's clip fit is the ported
  gate.

## 6. Gate and smoke

**The dodge fires.** An instrumented run (since removed) counted the Special
commands the reaction path issues, over 2 minutes of play:

| dodge | kind | clip (motion) | commands |
|---|---|---|---|
| net | 2 | `goalnet_stop_slow` (2068) | 4620 |
| net, fast | 2 | `goalnet_stop_rapid` (2067) | 16 |
| human | 1 | `avoidjump_3_3` (19) | 3816 |
| post | 3 | — | 0 (selector open, see §5) |

Smoke (teams 1 v 2, 20 min, seed 7, `gfsim_run --minutes 20`), exit 0:

| | main `221b7cd` | this branch |
|---|---|---|
| `[stalemate] forced` | 59520 | **12662** |
| fouls | 12-7 | **21-16** |
| possession | 71%-29% | **47%-53%** |
| shots | 7-24 | 18-16 |
| goals | 0-9 | **5-3** |
| yellow / red | 0-0 / 0-0 | 0-0 / 0-0 |
| determinism | 126d516e643b0c27 | **5e9fa0046caf67a1** |

The dodge is live in play: the forced-stalemate stalls drop by 79%, possession
becomes balanced instead of 71-29, and the score becomes competitive instead
of 0-9. Fouls rise from 19 to 37, which is the DODGE action feeding the contact
reaction's committed flag and veto 5.

The `functiontype 1` / `physical_height` console spam is pre-existing (it
reproduces on the unmodified committed tree at the same rate) and is not from
this change.

**Godot-side gate tests.** On a fresh worktree the Godot tests failed with
`undefined symbol: _ZTIN5godot8ResourceE`: the worktree's own godot-cpp build
is a partial archive (130 of 1089 objects) because the fresh clone has no
scons cache. Copying the main repo's complete `libgodot-cpp….a` into
`build/bin/` and relinking fixes it; the tests then pass (with `PES21_ROOT`
set they run the real character pipeline, otherwise skip). This is an
environment artefact, not a change in this branch.

Gate: `PES21_ROOT=… PES21_EDIT_SAVE=… nice ctest --test-dir build -j3
--timeout 2400` — **622/622 passed, 100%**, 390 s. The two standing skips are
tests 59 (VfsInstall.NestedOriginalsSurviveWithoutDownloads) and 115
(EditSaveInstall.DecryptsAndMatchesTheRunningGame), exactly as required.

## 7. Next

Decode `FUN_1407368d0` and port the post selector; then stage C (the tackle
sub-kind and the PES Tackle action) per the plan in `pes-re-contact-port.md`.
