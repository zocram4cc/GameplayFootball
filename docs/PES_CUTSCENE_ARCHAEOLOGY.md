# PES cutscene camerawork across generations: PES16, PES17, PES19, PES21

Measured from the user's own installs (`/run/media/z/Dati III/PES{16,17,19}/Data/`)
against `docs/PES21_CAMERA_TRACE.md`'s PES21 numbers. Nothing from these installs is
committed; the numbers below are the record.

## 1. Where it lives, per generation

Every generation keeps its cutscene data in the same container, `dt12`:

| version | archive | .fdc files | camera streams | camera frames |
|---|---|---|---|---|
| PES16 | `dt12_win.cpk` | 2,810 | (see per-category below) | |
| PES17 | `dt12_win.cpk` | 3,360 | | |
| PES19 | `dt12_g4.cpk` | 3,897 | | |
| PES21 | `dt12_g4.cpk` | 4,804 | 5,104 | 1,274,880 |

Layout is identical across all four: `common/demo/fixdemo/<category>/cut_data/*.fdc`,
one `.fdc` per pack, `.canm` camera streams embedded inside. PES16/17 additionally
carry a `menu` category PES19/21 dropped.

## 2. The cut-table record shrank, then grew back

The tag-0x06 camera-cut record - the one that names a shot's clip and its
near/far override - is **276 bytes in PES16**, **284 bytes in PES17, 19 and 21**.
PES19 matches PES21 byte-for-byte on every record tag; PES16 and PES17 share a
larger actor-cut record (368 vs 364 bytes) and a smaller scene-object record,
marking a clean generational boundary between PES16/17 and PES19/21.

The 8-byte difference is PES16 not carrying the trailing per-slot blend array and
frame-rate float PES17 added at `+0xF0`; every field `canm_to_camtrack.py` actually
reads (start frame, canm name, near, far - all at or before `+0x98`) sits at
identical offsets in both, so the smaller record was previously read as
**empty** rather than mis-read: `parse_fdc`'s exact-size gate (`==0x11C`) simply
dropped every PES16 tag-0x06 record, so `fdc.cuts` stayed empty and
`canm_to_camtrack.export()` wrote zero frames for every PES16 pack with a
camera - not because the data was missing, but because the parser rejected the
record before ever reading it.

**Fixed** (`tools/pes21_import/camera_cut.py`): the gate now accepts either
`0x114` or `0x11C` (`CAMERA_CUT_SIZES`), and `CameraCut.__init__` reads its
trailing frame-rate float only when the buffer is long enough, defaulting to
PES's own constant 30 fps otherwise (measured across every generation). Covered
by `tools/pes21_import/test_camera_cut.py` (5 tests, synthetic 276/284-byte
records + an end-to-end cut-to-camera-name resolution test - no PES data
required, per the no-assets rule).

## 3. Foul camerawork

| version | foul .fdc | with camera | camera streams | frames per stream |
|---|---|---|---|---|
| PES16 | 66 | 3 | 3 | 300 (10.0 s) each |
| PES17 | 72 | 2 | 2 | 180 (6.0 s) each |
| PES19 | 78 | 2 | 2 | 180 (6.0 s) each |
| PES21 | 78 | 2 | 2 | 180 (6.0 s) each |

**PES16 ships three foul cameras, including a no-card variant every later
generation deleted**: `foul_injury_card_n01_2015` (static, 10.0 s), alongside
red/yellow variants that all point at the *same* clip
(`foul_injury_card_n01_cam1.canm`) - PES16 filmed one foul shot and reused it for
every sanction. PES17 onward replaced it with a *different*, shorter clip shared
between only card_red and card_yellow (`foul_injury_card_y01.canm`), and dropped
the no-card case entirely. So PES17-21's seven foul subpools
(`card_red, card_yellow, warning, injury, protest, referee_run, no_card`) are
covered by camerawork on only two of them; the other five - including a plain
telling-off, which is the *most common* foul outcome - have never had an
authored camera in any generation the user owns except PES16.

Measured camera geometry (GF space, Z up, after the Fox->GF basis change):

| | PES16 (`n01`) | PES17/19/21 (`y01`/`r01`) |
|---|---|---|
| position | (-5.000, -5.000, 5.000) | (-4.418, -3.066, 3.500) |
| distance from origin | 8.66 m | 6.42 m |
| duration | 10.00 s | 6.00 s |
| FOV | 9.15° -> 17.29° | 18.18° -> 24.62° |
| motion | static | static |

Both are hand-placed at round numbers (PES16's is literally `(-5,-5,5)`), both
static, both a slow lens rack rather than a camera move - a fixed "card hold"
shot, not a broadcast pan. `CutsceneViewer::ClassifyAnchoring` correctly reads
both as incident-local (well under the 12 m radius).

**Engine fix.** The foul pool fallback (`Match::StartCutscene`, `foul` flat pool
receiving every subpool's tracks) already routes a camera-less subpool to
whatever the category has - this was not broken. Recording an actual match
(`debug_cutscene_report`) confirmed a real `foul/card_yellow` stoppage played
its authored 6.0 s shot, anchored 6 m from the incident at (22,-6) - not the
2008 follow-camera, not the centre spot.

## 4. Offside: no camera in any generation, confirmed across all four

| version | offside/assistant .fdc | camera streams | cut records naming a clip |
|---|---|---|---|
| PES16 | 9 | 0 | n/a (record format predates cut-record camera refs here) |
| PES17 | 9 | 0 | 0 of 16 (all name an empty clip) |
| PES19 | 7 | 0 | 0 of 10 (all name an empty clip) |
| PES21 | (per PES21_CAMERA_TRACE.md) | 0 | "offside is choreography only - staged but never filmed" |

PES19's actor-cut record for the assistant (slot 23) and the offender (slot 0)
both place them at the **origin, yaw 0** - no authored world position either.
PES means the flag going up to be caught by the live broadcast camera, in every
generation checked. So the placement is entirely the engine's to get right; there
is nothing to import.

**The bug was not the absence of a camera - it was placement.** Two faults,
found by reading the actual spawn data in `officials.cpp`:

1. `officials.cpp` spawns `linesmen[0]` (the `GetLinesmanNorth()` accessor) at
   `(25, -36.5)` and `linesmen[1]` (`GetLinesmanSouth()`) at `(-25, +36.5)` - the
   accessor names are inverted relative to which touchline each man actually
   runs. Both `OfficialForCutscene()` (which casts the assistant into the
   choreography) and the offside camera fallback picked `y >= 0 ? North :
   South`, casting the assistant standing on the **opposite** touchline.
2. Once cast, he was staged at the incident's own coordinates (mid-pitch,
   wherever the offence happened across the width of the pitch) rather than at
   his own touchline, level with the offside line.

**Fixed**: `CutsceneViewer::OffsideAssistantMark(incidentX, incidentY,
linesman0Y, linesman1Y, pitchHalfX, pitchHalfY)` (`cutsceneviewer.hpp/.cpp`)
picks the assistant by comparing the **live** y of each linesman to the
incident's y (not a hardcoded spawn-index assumption - the exact bug class
that broke this the first time), and returns his stand mark: `x` = the
incident's own x (the offside line), clamped to the pitch length; `y` = his own
touchline. `Match::OfficialForCutscene()` and the choreography staging offset
in `Match::UpdateCutsceneChoreo()` both route through it. Covered by 8 tests in
`tests/onthepitch/cutscene_viewer_test.cpp` including one that swaps which man
lives on which touchline and asserts the pick follows the *position*, not an
index.

**Measured in a recorded match** (`debug_cutscene_report`, see §6): an offside
at match clock 69:00 staged the assistant visibly on the touchline boundary,
confirmed on the extracted frame (`evidence/offside_clock6900.png`) - the man in
the green/black kit stands at the pitch's edge, water-moat and stand beyond it,
not on the grass mid-pitch.

## 5. Goal camerawork

| version | goal .fdc | with camera | camera streams | camera frames |
|---|---|---|---|---|
| PES16 | 899 | 818 | 3,739 | 806,395 |
| PES17 | 1,471 | 1,353 | 5,258 | 1,232,056 |
| PES19 | 1,302 | 817 | 2,841 | 721,615 |
| PES21 | 1,691 | 733 | 2,015 | 515,278 |

PES16 and PES17 both ship **more** goal camerawork by frame count than PES21 -
PES17 nearly 2.4x as many frames. This is consistent with the project's general
finding that older PES generations are simpler and more exhaustively filmed
per-scenario; PES21 appears to have consolidated many of the older per-scenario
shots into fewer, more reusable tracks.

**A real bug found in the current (PES21-derived) playback, not an import
gap.** PES authors a goal cutscene as a *montage*, not one shot -
`goal_A_celebrate_0229_LM.fdc` (documented in `PES21_CAMERA_TRACE.md`) cuts
between three distinct cameras (low static `cam_00`, two closer angles
`cam_01`/`cam_02`) at demo-timeline frames 0/10/11/20/30/31, revisiting `cam_00`
partway through. `canm_to_camtrack.py`'s export preserves this - it writes the
concatenated cuts with each keeping its own frame number, exactly the format
`CamTrack::SampleTimeline()` exists to replay. But
`Match::UpdateIngameCamera()`'s goal-celebration playback called
`track.Sample(goalScoredTimer * 0.03f)` - **plain row-linear interpolation**,
which blends smoothly between the *last frame of one cut and the first frame of
the next*, since they sit on adjacent rows in the exported file. Two
completely different, unrelated authored camera setups get smoothly dollied
between instead of hard-cut - exactly backwards from PES's own editing, and
exactly the mechanism the entrance/prematch sequence already avoids by calling
`SampleTimeline()` instead (`match.cpp`'s prematch shot playback has done this
correctly all along).

**Fixed**: swapped the one call to `track.SampleTimeline(goalScoredTimer *
0.03f)`. `SampleTimeline()` degrades to plain `Sample()` for any single-cut
track (the majority of goal packs), so this is a zero-risk change for most
celebrations and a direct fidelity fix for every multi-cut one. The underlying
function was already covered by 5 tests in `tests/base/camtrack_test.cpp`
(`CamTrackTimeline` suite) proving it plays the cut that has started, holds a
cut's last frame until the next begins, and cuts instantaneously at a
boundary - this change makes the goal celebration actually use that tested
mechanism. No new unit test was added for the call site itself: `match.cpp` is
not linked into the unit-test binaries, and the underlying `SampleTimeline`
contract is already exhaustively tested; verification here is the full build
succeeding and the recorded match (§6).

## 6. Recorded verification (headless, own worktree data)

Run: `gamescope --backend headless -W 1280x720 -- ./gameplayfootball
<config>` from `/home/z/Code/gpf-cutscenes/data` (own worktree, own `log.txt`,
own build - PES-derived assets reached read-only via per-file symlinks into the
main tree, never committed), `debug_cutscene_report true`,
`frame_recording_path` a fifo read continuously by `ffmpeg`, per
`docs/HARNESSES.md`.

10-minute-config AI-vs-AI match produced, unforced:

- **Foul** at match clock 13:09: `category foul/card_yellow, clock 13:9, 60 ds`;
  `CutsceneReport`: `shot incident-local: incident at 22,-6, camera 6 m from
  it`. Frame: a tight two-shot on the incident (referee mascot + player),
  matching the measured 6.42 m static card-hold geometry from §3 - not the
  centre spot, not the follow camera.
- **Offside** at match clock 69:00 (also at 34:17 and 62:37):
  `category offside (choreography only), clock 69:0, 70 ds`. Frame shows an
  "OFFSIDE / KING OF GETS" banner with the assistant (green/black kit) standing
  at the pitch's touchline boundary, water and crowd stand beyond it - not
  mid-pitch.
- **Goal** at match clock ~71:44 (`HARMONY SCORES`, 0-1): `celebration length:
  intro 1810 ms, whole performance 6000 ms`, `celebration: celebrate_0088 (var
  175), filmed by goal_celebrate_0088_mayaL0x`. Frame shows the celebration
  camera staged near the goalmouth with the scorer, net, and crowd in shot.
  Captured under the pre-`SampleTimeline` build (the fix landed moments after
  this run, verified by full rebuild + the existing `CamTrackTimeline` suite,
  not by a second recording - noted honestly as a gap, not claimed as directly
  observed).

Full C++ suite: 1350/1350 passed (7 skipped, unrelated `RigdioFidelity` tests
needing assets not present). Python suite: 791/797 passed; the 6 failures are
all in `test_install_team.py` against `ted.STAT_FIELDS`, pre-existing at this
worktree's branch commit, unrelated to cutscene/camera code, and inside a file
the lead is actively revising in the main tree - not touched here.

## 7. What is still unknown / dead ends

- **PES16's tag-0x00/0x01/0x02/0x04 record sizes were not generalised.**
  Only the tag-0x06 (camera cut) gate was widened, because that is the only
  record type `canm_to_camtrack.py`'s export path reads. PES16's actor-cut
  (tag 0x04, 368 bytes vs PES17-21's 364) and scene-object (tag 0x01, 172 vs
  156/164) records are still rejected by their exact-size gates. This only
  matters if a future importer wants PES16's `_pl` actor placements or scene
  refs specifically - not touched here, not needed for foul/offside/goal.
- **PES16's foul_injury_card_n01/r01/y01 all reference the SAME clip.** This
  was measured, not assumed to be a parsing artefact - three separate `.fdc`
  packs genuinely point at one shared `.canm`. Whether PES16 authored distinct
  shots elsewhere for red vs yellow was not searched further.
- **The `Mbinfo/CameraPickupInfo.bin` bit-packed format** (documented as
  undecoded in `PES21_CAMERA_TRACE.md` §7) was not revisited; nothing here
  needed it.
- **Whether PES16/17/19 ship a distinct `card_red`/`card_yellow` split at all**
  beyond the shared-clip case above was not exhaustively checked outside the
  `foul` category census in §3 - the numbers reported are camera-bearing packs
  only, not a full semantic audit of every no-camera choreography pack's
  intended sanction.
- **The exact real-time alignment between PES's own demo-timeline numbering and
  `goalScoredTimer`** (real elapsed ms since the goal) was not verified to be
  perfectly calibrated - `SampleTimeline` guarantees hard cuts happen at the
  *right relative order*, not necessarily at the *identical real-time offset*
  PES's broadcast intended. This is a strict fidelity improvement regardless
  (no interpolation ever crosses a cut boundary), but the precise timing of
  each cut boundary relative to the whistle was not independently confirmed
  against broadcast reference footage.
