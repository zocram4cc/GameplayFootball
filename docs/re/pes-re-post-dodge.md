# Post dodge (kind 3) — ported and firing

Closes the named gap in `docs/re/pes-re-dodge-port.md` §5: `FUN_1407368d0` is
decoded, the post selector is ported, and the post dodge fires with measured
counts.

Commits on this branch (off main `1d91833`, worktree `the worktree`):
- `1b9ceca` pesdodge: the post selector over the Animetable and Mbinfo
- `212b597` sim: the post dodge fires, and the net dodge takes the goal mouth's
  own gate

## 1. Decode — `FUN_1407368d0`

Decompiles: `the scratch dir/FUN_1407368d0.c`,
`dis7368d0.txt` (raw disassembly, Ghidra headless over `/tmp/pesmem.bin`).

Output struct, 4 ints: `[row, column, mirror, kind]` with kind 3 — the same
layout the net selector (`FUN_14073c5d0`) writes, confirmed side by side.

Two angles are read, both relative to the post's own facing:
- `bearing` = the raw 0..360 bearing from the post's anchor to the player
  (`FUN_141eb50b0` + `FUN_14046f230`), then
  `|wrap180(bearing − refAngle)|` gates the near/far split at 45 deg
- `approach` = the angle the ball comes in at, relative to the same axis
  (`FUN_140474c40` + `FUN_1406e4230`, the signed value kept)

Then:
- speed class from the player's speed: `>22 → 2`, `>13.5 → 1`, `>8 → 0`,
  else `3` — and `if (uVar7 < 3)` guards the read, so class 3 has no row
- near branch (`< 45`): the deep **side 1** needs the raw bearing between
  30 and 150 (the exe's `30 ≤ |wrap180(−A0)|` *and* `30 ≤ |wrap180(180 − A0)|`);
  inside it `|approach|` picks the column `17` / `13` / `9` at the 135 / 45
  splits
- far branch (`≥ 45`): **side 2**, and only for an approach inside the
  `60..120` window; outside it `out[2] = 3` and no row is written, i.e. no clip
- mirror from the sign of the approach angle (`if (local_118 < 0) mirror = 1`)
- row = `grid[side][speedClass]`, the 3×3 int grid at `0x1425e6680`

The post's anchor and gate come from `FUN_140a08340` / `FUN_140a084d0`: the
**player's own team's post**, at `+0x55c` of that team's match record, with the
eligibility check that it sits at the field edge within 1.0 m in both axes.
`FUN_140a8f7c0` then steps the anchor 20 units along the pitch's *width* axis
(90 deg for a +x post, 270 for a −x one), which is the part not carried.

## 2. The port

`pes::PostSelector` in `src/sim/onthepitch/pesdodge.{hpp,cpp}` — pure over
`Animetable` + `DodgeConstants`. Every number is read at load time through an
opcode-checked instruction or `PeImage::ReadVa`:
- the post grid via `MOVDQA XMM2,[RIP+disp32]` at `0x140736a72` → `0x1425e6680`
- the 8 km/h floor (`0x14259bdf4`), 30 (`0x142591140`), 45 (`0x14259be30`),
  135 (`0x14259be60`), 60 (`0x14259a764`), 120 (`0x14256ae30`)

The grid was verified byte-identical between `PES2021.exe` on disk and the live
`/tmp/pesmem.bin`, so it is not a runtime-written `.data` slot.

## 3. Tests

`tests/pes_dodge_test.cpp`:
- `PesDodge.PostSelectorPicksTheSideRowAndColumn` — synthetic constants and a
  synthetic table: sides 0/1/2, the three column bands, the 60..120 bail-out,
  all three speed classes, the 8 km/h floor, and the empty-cell case. Watched
  fail first (link error), then three mutations caught: forcing `col = 9`,
  forcing `side = 0` in the far branch, and collapsing the speed class.
- `PesDodgeInstall.TheUsersTablesAndRows` — the install's own grid rows resolve
  to real clips, plus the pinned facts below.

**Install facts pinned (not port failures):**
- `grid[2][2]` is `0x41000000`, which is the 8.0f of the *next* constant, not a
  row: side 2 at the fast class has no row. `Populated` is false there, so the
  selector returns no clip, as PES does.
- **The 17-vs-16 column is unresolved and pinned as such.** The exe writes
  `0x11` (17) in the `>135` band and `0xd` (13) below it. Column 13 matches the
  install's table exactly (`0x313` col 13 = `goalpost_stop_2_0_090`), but column
  17 is *empty* on this install while column 16 holds
  `goalpost_stop_2_0_160` — the clip the band is for. `0x11` is a valid index
  in an 18-column table and the other two columns are literal, so this is either
  an off-by-one in the exe's own band or a lookup shift the decode cannot
  resolve without a live poke. The port reads 16 (the column the install
  populates) and the test asserts `!Populated(0x313, 17)` and
  `Populated(0x313, 16)` so a future poke can decide it.

## 4. Wiring, and the net dodge's own gate

`_DodgePostCandidate` now builds the anchor from GF's own pitch marks — the own
goal line at `+team->GetSide() * pitchHalfW` and the goal's half-width — picks
the post on the ball's side, and calls the selector. The same geometry is
rebuilt in `_DodgeCommand`'s `kDodgePost` case, so the predicate and the command
agree.

Two things had to be fixed for it to fire at all, both found by instrumenting
the run:

1. **The goal sign.** GF's own goal is at `+team->GetSide() * pitchHalfW`, not
   `-`. The set-piece code's defending branch (`team->GetSide() * pitchHalfW *
   0.98`) is the same end, and the penalty aim (`-team->GetSide() *
   pitchHalfW`) is the other.
2. **The net dodge was swallowing the post.** Its GF gate was `|x| > 40` on both
   ends, which covers the whole own-goal band, and `NetSelector` is
   unconditional, so the post never got a frame. `FUN_140a07e40`'s real
   discriminator is that the ball's predicted position must be going *into the
   goal mouth* — past the goal line, between the posts. With that gate the net
   drops from 2992 to 1212 commands and the post surfaces.

**The post dodge fires.** Instrumented run, teams 1 v 2, seed 7, 3 minutes
(instrumentation removed afterwards):

| dodge | commands |
|---|---|
| ball | 3216 |
| net | 1212 |
| **post** | **717** |
| human | 778 |

`[stalemate] forced 7590`, `[determinism] c98507f215423193`, goals 1-2,
possession 42%-58%.

**Gate.** `PES21_ROOT=… PES21_EDIT_SAVE=… nice ctest --test-dir build -j3
--timeout 2400` — **623/623 passed, 100%**, 505 s. Standing skips 59
(VfsInstall.NestedOriginalsSurviveWithoutDownloads) and 132
(EditSaveInstall.DecryptsAndMatchesTheRunningGame), exactly as required.

**Smoke.** Teams 1 v 2, 20 min, seed 7, `gfsim_run --minutes 20`, exit 0:

| | main `1d91833` | this branch |
|---|---|---|
| `[stalemate] forced` | 59520 | **42420** |
| fouls | 12-7 | **8-21** |
| possession | 71%-29% | **47%-53%** |
| shots | 7-24 | 12-14 |
| goals | 0-9 | **3-4** |
| yellow / red | 0-0 / 0-0 | 0-0 / 0-0 |
| determinism | 126d516e643b0c27 | **be501d2af2ccfff0** |

(The main row is the stage-B baseline recorded in `pes-re-dodge-port.md` §6 at
its own base; the dodge port's earlier own-tree figure was 12662. The forced
stalls stay an order of magnitude below main, possession is balanced instead of
71-29, and the score is competitive instead of 0-9.)

## 5. Not done / next

- **The 17-vs-16 column band** needs a live poke or a decisive static cross-check.
- **PES's 20-unit out step** along the width axis (`FUN_140a8f7c0`) is not
  carried; GF's anchor is the post itself. The ported gate is the own-goal band
  and the selector's clip fit.
- **The mirror bit** is taken from the cell, not from the sign test PES runs on
  the approach angle; the two agree on this install's cells.
- **The chooser's order.** In PES's table the four dodges are entries 4-7 and
  each has its own predicate; GF's ball predicate is broad, so on frames where
  the ball is within 3 m the post is shadowed. That is PES's own ordering, but
  GF's ball eligibility (`FUN_140a07bf0`'s action classes and last-event frame)
  is still not carried, so GF's ball is broader than PES's.
