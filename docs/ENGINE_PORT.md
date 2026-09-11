# Port GameplayFootball to Godot 4 (GDExtension)

> Status: plan only, nothing started. Written 10-09-26 from a read-only survey of
> the tree. No code was changed. Pick it up at Step 1, which is useful on its own
> even if the port never happens.

## Context

What it would take to port the whole game — including the imported PES21
resources — to a modern engine. End state: the same game, the same match
simulation, the same PES-derived content produced by the same one command,
running on Godot 4 with the bespoke "Blunted" engine layer deleted.

Measured starting position:

| Thing | Size |
|---|---|
| `src/` C++17 | 122,112 LOC, 656 files |
| — `src/onthepitch` (simulation) | 39,971 LOC, 160 files |
| — `src/menu` (91 pages + screens) | 27,425 LOC, 127 files |
| — engine layer: `scene` + `systems` + `managers` + `framework` + `types` | 18,722 LOC |
| — `src/utils/gui2` (18 widgets) | 5,460 LOC |
| `tools/pes21_import` Python | 31,536 LOC, 128 files, 47 `test_*.py` |
| C++ tests | ~95 `*_test.cpp` → 54 GTest binaries |
| `data/` runtime tree | 19 GB, almost entirely gitignored |
| Installed content | 1,892 `.anim`, 230 dirs under `data/media/players/custom` (9.1 GB), 220 `.ase`, 4,616 cutscene files |

Rough effort, single full-time engineer, Godot target: Step 1 ≈ 3 weeks, Step 2 ≈ 3
weeks, Step 3 ≈ 4 weeks (first playable match), Step 4 ≈ 3 weeks, Step 5 ≈ 8 weeks
(91 pages is raw labour), Step 6 ≈ 2 weeks. ≈ 5–6 months to parity. Unreal instead
of Godot: ×1.5–2 (see **Assumptions & contingencies**).

Scope boundary, stated once: nothing currently open in `tasks/` requires a new
engine — the P1 is a heap corruption in menu code, not a renderer limit. If the
motive is "better lighting", `WorldEnvironment`-grade output is reachable inside
the current renderer for a fraction of this. This is what a real port costs, not
an argument that it is due.

---

## Approach

### D0 — Target engine: Godot 4.4+, simulation as a GDExtension shared library

Decided, not open. Two repo-derived reasons:

1. `AGENTS.md` mandates that no PES-derived byte is ever committed and that
   importing a team is exactly `python3 import_team.py path/to/AET/ path/to/team.ted`
   with no follow-up step. Every user rebuilds ~19 GB locally. Godot loads glTF at
   runtime — `GLTFDocument.append_from_file(path, GLTFState)` then
   `generate_scene()` — so importer output is playable with no editor and no cook.
   Unreal's import path is the Interchange framework, which is editor-first; using
   it in a packaged build requires adding the `Interchange` folder to *Additional
   Asset Directories to Cook*, or the third-party `glTFRuntime` plugin. Either way
   a cook/import step lands between `import_team.py` and a playable team, which
   breaks the governing rule.
2. GDExtension is a plain C++17 shared library built by the existing CMake, so the
   carved-out simulation library and all 54 GTest binaries keep building and
   running unchanged. Unreal would move the simulation into a UBT module and
   re-host the whole test suite.

### D1 — Animations do NOT cross to the engine's animation system

`.anim` files are not clips to be played back. They are the input to a per-tick
selection cost function: `AnimCollection` + `Humanoid::SelectAnim*` +
`CalculateAnimDifficulty` score every candidate clip against outgoing velocity,
outgoing body angle, current foot, and ball-touch metadata carried in each file's
XML tail, then blend with `MovementHistory`. Godot's `AnimationTree` cannot express
that, and replacing it would be rewriting the gameplay, which is the thing being
kept.

Therefore: the simulation keeps `Animation::Load`, keeps 10 ms frames, keeps
`Animation::Apply`, and writes joint rotations into a Godot `Skeleton3D` every
physics tick. Only meshes, skins and skeletons cross to glTF — enough for the GPU
to do the skinning.

### D2 — CPU skinning is deleted, not ported

`Skinning::JointTransform` + `HumanoidBase::UpdateFullbodyModel`/`SkinInto` +
`BuildBodyLod` + partial VBO upload exist only because the bespoke renderer had no
GPU skinning path. The comment in `src/onthepitch/player/humanoid/skinning.hpp`
records the cost this replaced: 49.7 ms per frame on a 20,458-vertex PES body.
Godot skins on the GPU from `Skeleton3D`. Delete the lot (≈ 470 LOC of kernels plus
their drivers) — do not port and do not "optimise" it first.

---

### Step 1 — Carve out `gfsim`: the simulation with no scene graph

Independently valuable; the existing game keeps running throughout. Do it in the
current tree, before any Godot code exists.

**1a. Introduce the pose sink.** New file `src/sim/posesink.hpp`:

```cpp
#ifndef _HPP_SIM_POSESINK
#define _HPP_SIM_POSESINK

#include <string>

#include "base/math/quaternion.hpp"
#include "base/math/vector3.hpp"

namespace gfsim {

// Everything the simulation needs from a skeleton instance. Addressed by joint
// ID, resolved once from the name — never by hardcoded index: JointOrder's DFS
// tail shifts when fingers are present (see jointorder.hpp).
class IPoseSink {
 public:
  virtual ~IPoseSink() = default;
  virtual int JointID(const std::string& name) const = 0;  // -1 when absent
  virtual void SetPose(int joint, const blunted::Quaternion& rot,
                       const blunted::Vector3& pos) = 0;
  virtual void GetPose(int joint, blunted::Quaternion& rot,
                       blunted::Vector3& pos) const = 0;
  virtual void GetDerivedPose(int joint, blunted::Quaternion& rot,
                              blunted::Vector3& pos) const = 0;
  // Replaces Node::RecursiveUpdateSpatialData: recompute derived transforms.
  // AGENTS.md: skipping this is what made poses silently not apply outside the
  // match; the sink must never require the caller to remember it twice.
  virtual void Commit() = 0;
};

}  // namespace gfsim
#endif
```

Five methods is the whole surface. Measured: the simulation calls exactly 16
distinct `Node` methods, of which the pose ones are `SetPosition`, `GetPosition`,
`SetRotation`, `GetRotation`, `GetDerivedPosition`, `GetDerivedRotation` and
`RecursiveUpdateSpatialData`; the remaining nine (`GetObject`, `AddObject`,
`SetName`, `GetNodes`, `GetName`, `GetObjects`, `Exit`, `PrintTree`,
`SetLocalMode`) are construction/teardown and move to the host.

Add `src/sim/localposesink.{hpp,cpp}` — a struct-of-arrays implementation (parent
index, local pos/quat, derived cache, name→ID map built from
`JointOrder::Permutation`). This is what the GTest suite and any headless run use,
so no test ever needs an engine.

Change `Animation::Apply` (`src/utils/animation.hpp:83-89`) to take
`gfsim::IPoseSink&` instead of
`const std::map<const std::string, boost::intrusive_ptr<Node>>` — and note that the
current parameter is **by value**, copying 21+ entries per put per body; the new
signature takes a reference, which is a free win, not a behaviour change.

**1b. Convert the humanoid chain.** `HumanoidBase::Joint::node` → `int jointID`;
`NodeMap` → `IPoseSink*`; `TemporalHumanoidNode::actualNode` → joint ID;
`HandRig::nodes` and `FaceRig::boundGeometry` likewise. `HandPoseData` and
`FaceRigData` are already plain tables and are untouched.

**1c. Split `Ball` and `Match`.** `Ball` drops `ballNode`/`ball` and keeps position
and orientation; `BallPhysics` is already engine-free and moves verbatim. `Match`
splits: clock, teams, referee, ball, `Process()`, replay ring buffers and the
`UpdateIngameCamera` maths stay in `gfsim`; stadium/goal/sky/crowd/light/sound node
construction and cutscene playback move to a host-side `MatchPresenter` that reads
the sim each frame. `Match::GetReplaySpatials`/`CaptureReplayFrame`/
`ApplyReplayFrame` re-target from `Spatial*` to pose sinks — the buffers already
store plain pos/quat at ms stamps.

**1d. Make the boundary enforceable.** New CMake `add_library(gfsim STATIC …)`
whose `target_include_directories` deliberately exclude `src/scene`, `src/systems`,
`src/managers`, `src/framework` and the SDL/GL/AL include dirs, so a re-introduced
dependency fails to compile rather than fails in review. `src/gamedefines.hpp`
currently includes `<SDL2/SDL.h>` only for the 18 default keycodes — move that
table to `src/hid/defaultkeys.hpp`, outside `gfsim`.

Acceptance for Step 1: `gameplayfootball` still builds and plays (the Blunted host
supplies an `IPoseSink` implemented over `Node`), and `ctest` is green.

### Step 2 — Re-emit meshes as glTF from the same Python pipeline

New module `tools/pes21_import/gltf_out.py` writing binary glTF 2.0 (`.glb`):
positions/normals/tangents/UVs, `JOINTS_0`/`WEIGHTS_0` (already capped at four
influences by `kMaxSkinInfluences = 4`), one skin with `inverseBindMatrices`, and a
node hierarchy whose order is exactly `JointOrder::BodyJoints()` followed by the DFS
tail — the same list as `retarget.GF_JOINT_ORDER`, because joint numbering is part
of the file format.

The bind bake moves from runtime to export, unchanged in maths: joint rest
transforms come from `retarget.PES_BIND` (anim / T-pose rig), inverse bind matrices
from `retarget.PES_RENDER_BIND` (mesh authoring pose). That is `PES_ALIGN`'s `W`
applied exactly once, in the same place in the chain, baked into the file instead of
into `base.anim.util` at load. Consequences:

- `gen_player_object.py` stops emitting `player.object`, `base.anim.util` and
  `straight.anim.util`; it emits the skeleton into the `.glb` instead. Keep a
  `straight` **test** clip (all-identity local rotations) for the T-pose check.
- The `.weights` sidecar and the vertex-colour weight encoding
  (`jointID*10 + weight*9`, which overflows a byte past joint 24 and is the entire
  reason the sidecar exists) are deleted.
- `.ase` output stays behind a flag until Step 6, so the current game keeps working.

`fmdl_to_fullbody.py` gains `--gltf out.glb`; `stadium_to_gf.py` gains the same for
static geometry (no skin); hair and face meshes follow.

Unchanged and load-bearing — do not touch: `retarget.py`, `gani_to_anim.py`,
`install_anims.py`, `reconvert_installed.py`, `migrate_anims_tpose.py`,
`hand_poses.py` → `handposes.txt`, `face_to_anim.py` → `.faceanim`,
`canm_to_camtrack.py` → `.camtrack`, `entrance_pl.py` → `.chor`,
`goal_cutscenes.py` → `celebrations.txt`. All of those feed `gfsim`, not the engine.
Textures (`.png`) and audio (`.ogg`/`.wav`) are already standard and are consumed
directly by Godot.

Re-emission of the 19 GB tree is a full `reconvert_installed.py`-scale run plus a
re-import of every installed team; budget a machine-day and verify by count, not by
spot check.

### Step 3 — Godot host: a playable match

Layout: `godot/` (project) and `gdext/` (GDExtension sources linking `gfsim` and
`godot-cpp`).

- `GFMatch : Node3D` owns the `Match` object and calls `Match::Process()` once per
  `_physics_process`. Set `physics/common/physics_ticks_per_second = 100` to match
  the existing 10 ms tick (`main.cpp:412`, `physics_frametime_ms` default 10) and
  raise `max_physics_steps_per_frame` so a slow frame does not silently drop ticks.
  Render interpolation stays on the Godot side; the sim keeps its own clock.
- `GFSkeletonSink : gfsim::IPoseSink` writes `Skeleton3D::set_bone_pose_rotation` /
  `set_bone_pose_position`; `Commit()` is a no-op because Godot recomputes derived
  transforms itself — but `GetDerivedPose` must call
  `Skeleton3D::get_bone_global_pose`, which is what the ball-contact and
  hand-attachment code reads.
- Player instancing: `GLTFDocument.append_from_file` on the model `.glb` →
  `Skeleton3D` + `MeshInstance3D`. The `SetKit` ident-string swap
  (`humanoidbase.cpp:1466-1489`) becomes a `ShaderMaterial` texture uniform.
- Stadium and ball load the same way. The procedural pitch becomes a Godot
  `ShaderMaterial` or a baked PNG emitted by the importer — pick the PNG unless the
  time-of-day variation is visibly lost.
- Camera: `Camera3D` driven by `UpdateIngameCamera` (pure maths, moved into `gfsim`
  in Step 1c) and by `CamTrack` for cutscenes.
- Renderer features that were bespoke and are now configuration, not code: SSAO/SSIL
  and shadows via `WorldEnvironment` + `DirectionalLight3D`; fog and sky via
  `Environment`; the PES LUT grade via `Environment.adjustment_color_correction`
  fed by `data/media/textures/lut/grade.png` converted to a 3D texture (the strip's
  white point is 0.689 — do not normalise it); crowd via `MultiMeshInstance3D` fed
  by the existing `InstanceList::Placement` data. Delete all 12 GLSL shaders under
  `data/media/shaders/` and `opengl_renderer3d.cpp` (2,981 LOC).

### Step 4 — Input and audio

- The 19 `e_ButtonFunction` values become Godot `InputMap` actions with the same
  names; seed the defaults from the existing tables in `keyboard.cpp:20-29` and
  `gamepad.cpp:50-115`. Per-device calibration and remap UI become Godot settings.
  Delete `src/hid` (600 LOC) except the default-bindings table kept as data.
- Sound emitters move to the host as `AudioStreamPlayer3D`/`AudioStreamPlayer`,
  driven by events the sim already raises: ball touch and post
  (`ball.cpp:302-305,485-489`, pitch-randomised), whistles (`referee.cpp:53-74`),
  crowd ambient/reaction loops and per-team chants (`match.cpp:917-954`). Keep the
  gain and pitch constants verbatim; they are tuning, not plumbing.
- `src/utils/rigdio.*` compiles into `gfsim` **verbatim** — it has no engine, FS or
  audio dependency, and `tests/utils/rigdio_test.cpp` keeps guarding it.
  `RigdioDirector` keeps its state machine and talks to the host through a
  four-method `IAudioSink` (`Play`, `Pause`, `SetGain`, `GetDuration`). Its loudness
  normalisation decodes mp3/ogg to 16-bit PCM first
  (`rigdiodirector.cpp:106-111,262-271`) — keep that; do not substitute Godot's
  loudness handling.
- Delete `src/systems/audio` (OpenAL renderer, null renderer, sound buffers).

### Step 5 — UI: the 91 pages

Order, so the game is playable as early as possible:

1. In-match HUD and ingame pages (16): scoreboard, radar, goalbug, banners,
   player HUD, stats overlay, phase menu, replay chrome, result, game over.
2. Match-start path: TeamSelect, MatchOptions, GamePlan, LoadingMatch.
3. Settings (graphics/audio/controls/language/camera).
4. The ~53 league and career pages.

Rule for each page: the **behaviour** is ported from the existing C++ page class —
which data it shows, which transitions it allows, and when a HUD element appears
(e.g. `goalbug.cpp:50-54`'s four-second delay, `replaymenu.cpp:26-74`'s cinematic vs
controllable split, the `prematchtimeline` beat table). The **layout** is rebuilt as
Godot `Control` scenes with anchors replacing the percent coordinates; `Gui2*`
widgets are deleted, not wrapped.

Two behaviours that must survive or the port regresses in ways that are invisible
until deep in a menu tree: `Gui2Page::GoBack` is idempotent and `PagePath::Push`
happens inside `PageFactory::CreatePage` — the back stack must behave identically;
and focus-first-child on gain-of-focus (`view.hpp:60-63`), without which gamepad
navigation dead-ends.

League and career **logic** (`career_persistence`, `career_database`, `leaguecode`,
standings computation) moves into `gfsim` and is not rewritten. SQLite stays: Godot
has no built-in SQL, and `database.sqlite` plus the four save databases are already
the format the importer writes.

### Step 6 — Verification harness parity, then delete the old host

Nothing is deleted until all three instruments reproduce:

- `gfviewer` → `godot --headless --path godot/ res://tools/viewer.tscn -- <model.glb>
  --anim <clip> --shots N --out <dir>`, writing PNGs directly (no raw-RGBA +
  ffmpeg step, and no four-frame lead-in to skip). Contract unchanged: a PES body
  with all-identity bone poses must render a perfect T-pose — arms dead horizontal,
  flat palm-down gloves.
- `menu_smoke_*.config` (26 files) keep their `Properties` key vocabulary and the
  marker-grep protocol in `tests/run_menu_smoke.sh`; the driver switches from
  `UserEventManager` injection to `Input.parse_input_event`. The `menuscript`
  timeline syntax (`1200:shot=plan;1500:left;…`) is kept as-is.
- `tools/showcase.sh` captures a full headless match, via Godot's `--write-movie`
  or the same fifo→mp4 pipeline.

Then delete: `src/scene`, `src/systems`, `src/managers`, `src/framework`,
`src/types`, `src/loaders/ase*` + `asecache` + `asenormals`,
`src/utils/objectloader.*`, `src/utils/gui2`, `src/hid` (minus the defaults table),
`src/viewer`, `src/main.cpp`, `data/media/shaders/*.frag|.vert`, and the `.ase` /
`.object` / `.weights` emitters in `tools/pes21_import`. ODE is already effectively
dead — its sources are listed in `sources.cmake:102-114` but the wrapper is not in
the link set; drop it with the rest.

---

## Critical files & anchors

| File | Region | Why |
|---|---|---|
| `src/onthepitch/player/humanoid/humanoidbase.hpp` | `NodeMap` (:25), `Joint` (:28), `TemporalHumanoidNode` (:180), node members (:409-446) | The seam. Every scene dependency in the simulation funnels through here. |
| `src/utils/animation.hpp` | `Animation::Apply` (:83-89) | Signature change to `IPoseSink&`; note the by-value map copy being removed. |
| `tools/pes21_import/retarget.py` | `PES_BIND` (:75-95), `PES_RENDER_BIND` (:128-169), `_build_align` (~:340-380) | Sole definition of both skeletons. The glTF rest transforms and inverse bind matrices are built from exactly these two tables. |
| `src/onthepitch/player/humanoid/jointorder.cpp` | `BodyJoints()`, `Permutation()` | Joint numbering is part of the file format; the glTF node order must reproduce it or every converted body drives the wrong bone. |
| `tools/pes21_import/fmdl_to_fullbody.py` | ASE/`.weights`/`.object` emission | Where `--gltf` is added and where the vertex-colour weight encoding dies. |

---

## Verification

Run from `data/` unless stated. Each step's check must pass before the next starts.

**Step 1 — seam.** `cmake --build build-tests && ctest --test-dir build-tests
--output-on-failure` green (54 binaries), and the unported game still plays:
`bash tests/run_menu_smoke.sh` over `menu_smoke_quick_match.config` and
`menu_smoke_full_match.config` reaching their markers. New behaviour check: a
throwaway program links **only** `gfsim`, loads
`media/animations/<any pes_*.anim>`, applies frame 0 to a `LocalPoseSink`, and
prints `GetDerivedPose("right_hand")` — it must link with no SDL, GL, AL, or
`src/scene` object on the command line, and print a finite position within 1.2 m of
the root. That link failing is the whole point of the step.

**Step 2 — glTF re-emission.** For one known-good model (`lcg_2709`), run
`fmdl_to_fullbody.py --gltf` and assert against the existing `.ase` + `.weights`:
identical vertex count, identical triangle count, and per-vertex joint/weight sets
equal to the sidecar's (the sidecar is the ground truth; the vertex-colour path is
not). Then the rig check: load the `.glb` in the Step 6 viewer with all bone poses
identity and confirm a perfect T-pose. `python3 -m unittest` over the 47
`tools/pes21_import/test_*.py` stays green.

**Step 3 — playable match.** `godot --headless --path godot/ res://tools/match.tscn
-- --team1 16 --team2 10 --minutes 10` runs to full time without crash, and a
non-headless run of the same fixture produces a frame showing 22 connected,
correctly-scaled players on the pitch. Per `AGENTS.md`, the frame is the evidence;
a clean log is not. Compare the same fixture's scoreline distribution against ten
runs of the current binary — the simulation must not have changed.

**Step 4 — input/audio.** With a gamepad attached, all 19 actions register in a
Godot input-echo scene. `menu_smoke_rigdio.config`'s equivalent run produces the
same chant/goal-horn sequence, checked by the existing marker greps.

**Step 5 — UI.** Each ported page: reachable from the main menu, `GoBack` returns to
the exact previous page, and gamepad-only navigation reaches every focusable
control. Whole-flow proof: `tools/showcase.sh` completes a full match including
entrance, half-time, full-time and result, and the mp4 is watched.

**Step 6 — parity.** All three instruments produce output equivalent to today's:
viewer T-pose still, 26 smoke configs green, one showcase mp4 reviewed. Only then
run the deletions, then `ctest` once more.

---

## Assumptions & contingencies

- **Engine choice.** Godot 4.4+, per D0. If overridden to Unreal 5: Steps 1, 2 and
  the whole of `gfsim` are unchanged and still worth doing; Step 3 becomes a UBT
  module wrapping `gfsim` as a third-party static library, `Skeleton3D` becomes
  `USkeletalMeshComponent` + a `FAnimNode` writing bone transforms, and the runtime
  glTF path needs `glTFRuntime` or a cooked-Interchange configuration. Budget the
  extra: re-hosting 54 GTest binaries under UBT, a per-user cook step in
  `import_team.py`, and an Epic EULA review against the project's Apache-2.0 licence.
- **Determinism.** Assumed not required beyond what exists today: the sim is
  deterministic in tick units but `random()` calls in animation selection are
  unseeded per match, and the replay buffer stores render poses, not inputs. If
  deterministic replay is wanted later, that is a separate change (seeded RNG +
  input log) and does not belong in the port.
- **`Skeleton3D` write cost.** Assumed fine: 22 bodies × ~58 joints × 100 Hz is
  ~128k bone writes/second, against the 49.7 ms/frame of CPU skinning being deleted.
  If profiling says otherwise, move the writes into a `SkeletonModifier3D` and batch
  per skeleton — measure before restructuring.
- **Save compatibility.** Assumed not required: league saves, `career.save`,
  `match_history.sqlite` and `set_piece_config.sqlite` keep their schemas because
  the logic moves verbatim, but no migration is written for the settings `.config`
  files, which become Godot project/user settings. If old settings must survive,
  add a one-shot reader for the `input_*`, `audio_volume`, `locale_language`,
  `radar_mode` and `context_x/y` keys.
- **The open P1.** The full-time heap corruption in `tasks/09-09-26.md` is in menu /
  `MatchData::Event` territory, which Step 5 rewrites. Do not treat the port as its
  fix — reproduce it under ASan on the current build first, so the port is not
  carrying an unknown bug across the boundary and re-finding it in a new shape.
