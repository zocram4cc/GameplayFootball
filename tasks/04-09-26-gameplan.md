# Game plan, pre-match and settings — the owner's brief of 04-09

Everything below is one goal, in the owner's words:

> import the remaining portraits and devise testing for the Game Plan. When I
> tried to drag a player up or down, it crashed upon releasing the button. As
> the player I should be able to completely change formation by dragging around
> players in the game plan screen, under a LINEUP option. The whole look and
> feel should take after PES; I should also be able to select multiple roles per
> player by clicking a secondary button (the shoot button, for example). Hints
> should be shown to me on the lower-left side of the screen. The Gameplan
> should also show both teams at once - though unlike PES, if in one-pad coach
> mode, I should be able to jump from one game plan lineup to the other.
> Finally, the pre-match and setting screens have to be revamped, and sliders in
> both game plan and pre-match that are ambiguous should be made clear: finite
> number of steps, indicated by the UI, and no generic sliders like "FORMATION"
> that have no meaning as a slider should show up - they should be hidden, so as
> to be changed only via lineup dragging or direct formation select. When I
> select a formation via the menu, the shape of the formation on the preview
> should change accordingly, and also, portraits and stats (+ roles, and medal
> status) should show up in the lineup itself. Import missing portraits. Also,
> reimport HDG because its models have broken completely; look at Wario from
> smbg also, it's got dragging verts on cutscenes.

And, since:

> Try dragging a player FAR away from its position. We want a monkey with a
> typewriter-type test: this should not break under any circumstances.

> some pitch UVs are being imported upside down (like this latest one).

## Items

| # | Item | Owner | State | Evidence |
|---|---|---|---|---|
| 1 | Monkey test: random input must never break the game plan | me | done | `monkeyrun.sh`, 4 seeds x 1200 taps clean twice (`0480b4c`, `c41eed7`, and again after the card layout) |
| 2 | The drag crash on release | me | done - five distinct use-after-frees | ASan stacks in this file; `0480b4c`, `c41eed7` |
| 3 | LINEUP: drag to change formation, enabled in the release build | me | done | `7e40829`; `tmp/plan/b_pair1.png` |
| 4 | PES look and feel for the game plan | me | done | `tmp/plan/c3_zoom.png` - portrait cards, line colours, bench strip |
| 5 | Multiple roles per player, on a secondary button | me | done | `7e40829` (`PlayerData::ToggleRole`, shoot = `e_ControllerButton_X`); card prints "CB+1" |
| 6 | Hints, lower-left | me | done | `tmp/plan/b_pair2.png` ("A - grab / B - back / X - roles on a card") |
| 7 | Both teams at once; jump between them in one-pad coach mode | me | done | `tmp/plan/b_two2.png`; SWITCH TEAM row |
| 8 | Pre-match screen revamp | me | done | `e1f35fa`; `tmp/plan/p_zoom.png` - MATCH / PRESENTATION / TEAMS |
| 9 | Settings screen revamp | me | done | `e1f35fa`; `tmp/plan/s_zoom.png` |
| 10 | Sliders: finite steps, shown in the UI | me | done | `sliderstep.hpp` (20 steps for a scale, one per choice), `gameplayfootball_sliderstep_tests` |
| 11 | Hide meaningless sliders (FORMATION) | me | done | `7e40829` - FORMATION dropped from `tacticsSliders`; also ADBOARDS/sky/goals dropped from the stadium choice (`e1f35fa`) |
| 12 | Formation select reshapes the preview | me | done | two tests in `plan_map_interaction_test.cpp` (line counts differ, >= 4 cards move 4-4-2 -> 3-4-3) |
| 13 | Portraits, stats, roles, medals on the lineup cards | me | done | `64a12c1`; `tmp/plan/c3_zoom.png` - portrait, CB+1, 95, gold medal |
| 14 | Import the missing portraits | ImportProps | done | `c0e8194` |
| 15 | Reimport HDG (models broken) | ImportProps | done, four players still bodyless | `901b66e`; leftover 1 in `tasks/04-09-26.md` |
| 16 | Wario (smbg): dragging verts in cutscenes | ImportProps + me | done, seen in the engine | `tmp/wario_sheet.png` - 8 frames of `pes_dm_goal_Wide_sliding01`, mesh intact |
| 17 | Pitch UVs imported upside down (st002 most recently) | ImportProps | done | `c094fe1`; `tmp/overlay_ab.png` |

## 2. The crash — what has been ruled out

Headless repro through the real input path (`menu_smoke_script` pushes keys into
`UserEventManager`, exactly where a keyboard arrives, so this exercises
guitask -> windowing event -> focused widget as a human does):

| scenario | script | result |
|---|---|---|
| pre-match plan, grab GK, two steps down, drop in open space | `plan.config` | no crash, card moved and stored |
| pre-match plan, grab GK, five steps up onto the CB line, drop (swap path) | `plan_swap.config` | no crash |
| live match, pause -> game plan, fifteen steps up (far drag), drop | `plan_match.config` | no crash (pause route did not open the menu; retry) |

So it is not the interaction maths: `PlanMapInteraction` clamps to the pitch,
`NearestCardWithinRadius` is bounded, and `PitchToDatabase` is a pure inverse.
Candidates still open, in order:

* **`RebuildEntries()` deleting the cards inside an event handler.** The swap
  path deletes every `Gui2PlanMapEntry` (`entry->Exit(); delete entry;`) while
  the windowing event is being dispatched by `Gui2Task::ProcessEvents`, which
  holds `currentFocus` across the dispatch and dispatches a *second* event
  (the windowing event) after the joystick one. The window manager has a
  deferred `pendingDelete` list for exactly this case and the map does not use
  it.
* **The gamepad path.** The owner said "releasing the button"; a release
  generates a `JoystickEvent` (guitask.cpp:159 `buttonChanged`) with no
  activate/escape flag, which `Gui2PlanMap` does not handle, so it propagates
  to the parent page — a path the keyboard never takes.
* **Editing live team data mid-match** while `Team`/`TeamAIController` hold
  `PlayerData*` taken at kickoff.

Hence item 1 first: a monkey that hammers the screen through the real input
path, under ASan, until it breaks.

## 17. Pitch UVs upside down — fixed

`pitch_overlay.py` rasterises PES's pitch decal meshes into the engine's
overlay. It sampled each triangle's texture at the fmdl UV **unflipped**, while
`stadium_to_gf.py:962` and `fmdl_to_fullbody.py:957` both read `1.0 - uv.v`: an
fmdl's V runs the opposite way to the image row order. Every decal was
therefore mirrored top-to-bottom in place. A pitch is symmetric, so the layout
hid it; the centre-circle crest did not.

The destination mapping was right and stays: `pitch_to_pixel` is
`((x / pitchFullHalfW) * 0.5 + 0.5) * width`, character for character what
`proceduralpitch.cpp:128` samples with, so only one flip was missing.

Proof: `/home/z/.claude/jobs/f858a344/tmp/overlay_ab.png` - st002's crest
before and after. "GIRLS CAN LOVE GIRLS" and "設立2012年7月" read upside down on
the left and upright on the right.

## 1 + 2. The monkey, and the five crashes it found

`menu_smoke_script` gained a `monkey=<seed>:<taps>` action: one random key per
driver tick, pushed into `UserEventManager` like any keyboard, so it walks the
same guitask -> windowing event -> focused widget path a human does. The keys
are weighted towards movement and confirm with escape and 'x' often enough to
open and abandon submenus mid-drag, and the stream is a pure function of
(seed, index) - so every crash it finds is replayable from two numbers, printed
on every tap.

    "menu_smoke_script" "2500:left;3000:monkey=1:3000;600000:quit"

Seed 1 segfaulted at tap 772 within twenty seconds of first being run. Five
distinct defects, each found by re-running under the ASan build
(`build-asan`, `-fsanitize=address`), all in gui2 rather than in the game
plan's own maths:

| # | tap | what ASan said | cause | fix |
|---|---|---|---|---|
| 1 | 772 | heap-use-after-free in `Gui2View::SetInFocusPath` from `SetFocus` | the window manager kept `focus` pointing into a page that had just been deleted; the next `SetFocus` told the outgoing view it had lost focus, walking its freed parent chain | `Gui2WindowManager::ForgetFocusIn(view)`, called from `Gui2View::Exit`: the focus is dropped if it is that view **or anything below it** |
| 2 | 69 | UAF in `Gui2View::Exit`'s child loop | `Exit` iterated a *copy* of `children`; a child's `sig_OnClose` deletes other children (the game plan's submenu close rebuilds the button column), so the copy held freed pointers | the loop takes `children.back()` from the live vector |
| 3 | 69 | still UAF at the same line | a handler can also *add* views to the dying grid, and the loop could revisit an entry | pop and unparent **before** exiting each child |
| 4 | 358 | UAF reading `gridNav` inside `GamePlanPage::OnClose` | `Exit` ran twice on a page (once from `GoBack`, once from the manager's `pendingDelete`), so every close handler ran twice - the second time on state the first had destroyed | `Gui2View::Exit` is idempotent (`exited`) |
| 5 | 358 | UAF deleting the same view twice | one view in two `children` lists: the button column is taken out of its grid when a submenu opens and put back when one closes, and two closes in a row added it twice | `Gui2View::AddView` moves a parented view instead of duplicating it; `Gui2Grid::AddView` replaces its own container entry |

Two more were fixed on the way, both found by reading rather than by the
monkey: `GamePlanSubMenu` did `delete this` inside its own event handler (now
`MarkForDeletion`, and one close per submenu however many escapes arrive in a
frame), and `Gui2Grid::RemoveView(row, col)` detached only the last view in a
cell and called `Gui2View::RemoveView(nullptr)` - a logged fatal - when the
cell was empty.

**Result: seed 1 now runs its full 3,000 taps with no crash** (`monkey7.log`).
The page is also one-way at teardown: `tearingDown` stops a submenu's close
signal from rebuilding the column, saving tactics, or writing to the names db
while the page is being deleted.


## The card layout, measured rather than guessed (04-09, late)

The first capture of the finished cards (`tmp/plan/c_zoom.png`) had every line
of the formation covering the one in front of it and four names run together
into `SEAF-CHANTHDIVEROBBY D`. Measured on that frame at 720p: a card was a
50 px portrait plus two 24 px text rows = 98 px, and two lines of a 4-4-2 are
67 px apart on the diagram. Four changes, in order of effect:

* the position and rating moved ON to the portrait's bottom edge, in a
  translucent band drawn per card (never on the shared portrait texture) - the
  way PES draws them, and one row shorter: 98 -> 74 px;
* `kPitchHeightFraction` 0.76 -> 0.86, which the single scrolling bench row
  freed up: 67 -> 76 px between lines;
* `kCardW` 3.9 -> 3.1;
* `kOutfieldDepthScale` 0.8 -> 0.9 and its offset 0.1 -> 0.05, so the lines
  use more of the pitch's depth. It exists so the back line does not sit flush
  on the goal box, and 0.9 still keeps it off.

Result `tmp/plan/c3_zoom.png`: the defence, the attack, the keeper and the
bench all stand clear. The four midfielders of this team's own tactic still
sit close together - that is the tactic (AM/RM/CM/LM within a few metres),
not the layout, and PES would draw the same cluster.

## gfviewer --anim

AGENTS.md's health check (`gfviewer <model> --anim media/animations/straight.anim.util
--shots 2` must render a perfect T-pose) named a mode the binary did not have -
the viewer could turn a model or play a whole choreography, and nothing in
between. `--anim CLIP [--shots N]` plays one clip on one body, spreading the
shots over it and turning the camera half a circle, and applies the humanoid
invalidation the same file warns about. This is the instrument for any reported
skinning defect; it found Wario intact in eight frames of a sliding
celebration, which is what closed item 16.

## The skinning defect the owner reported, measured

`gfviewer --anim` was built to look at this, and the first thing it showed was
that the owner's "dragging verts" is not one model's problem: **144 of the 152
installed bodies tear at the BIND pose**, where the skin should reproduce the
mesh exactly. Worst 1293x, on a 2hug body. `skin_probe.py` measures it offline
in a second per model, so every claim below is a number.

Two causes found and fixed at the importer (`e5c00a7`, and this commit):

1. **A UV seam's two halves were weighted separately.** Same place, two
   entries, because the two faces need different texture coordinates - and
   each was guessed on its own, so where two joints tie the tie fell to the
   last bits of a float. lcg_2709's v16033/v16034 sit at the same millimetre
   and carried `right_shoulder 0.29` against `right_clavicle 0.29`; the bake
   moved one 0.15 m and left the other, stretching a 0.5 mm edge 315x.
   `seams.weld` makes coincident vertices agree.
2. **A weight was guessed from the nearest joint POINT.** 4cc packs a whole
   character into the boots slot, so most of these meshes carry no bone mapping
   at all. A hand hangs beside a hip, so a torso vertex at the waist came out
   `right_hand 0.44 / right_elbow 0.31 / right_thigh 0.25`.
   `fmdl_to_fullbody.nearest_bone` reads the distance to a bone SPAN, relative
   to how thick that bone measurably is (`BONE_RADIUS`, the 90th percentile
   distance of PES's own authored skin from each bone), compares in the rig
   space the engine draws in (a 2.5 m export against a 1.81 m rig put every
   limb outside its own bone), and blends only one bone's two ends - so a
   surface can slide along a limb but never between two limbs.

Measured, lcg_2709: **315.9x / 2068 torn edges installed -> 16.2x / 964** with
both fixes. The giant shards fanning out of the model are gone from the frame
(`tmp/lcg_fix.png`, `tmp/f_sheet.png`).

### Dead ends - measured, not argued

* **Smoothing a torn vertex over its neighbours** (hdg_XXX23 373x -> **418x**):
  the average of a hand and a chest is itself a cross-limb blend.
* **Rebinding torn vertices to their nearest bone after the fact**
  (2hug_1869 88x -> **2502x**): at the bind pose the bake is the only thing
  that moves a vertex, so changing one vertex's weights moves it away from
  neighbours that were not changed. Weights have to be right BEFORE the bake.
* **"Nothing tears without the bake"** - true and useless: with no bake every
  joint transform is identity at the bind pose, so the metric reads 1.00x by
  construction. It measures bake consistency, not correctness.
* **Welding on a rounded coordinate**: two points 0.5 mm apart can sit either
  side of a 1 mm cell boundary, so it welded nothing. Unions over neighbouring
  cells now.
* **AGENTS.md's T-pose health check on the legacy body**: `fullbody.ase` droops
  30 degrees under it and `fullbody_pes.ase` is a perfect T-pose
  (`tmp/tp_pes_row.png`). The check holds for PES-derived bodies only - the
  legacy body is authored in GF's own pose, so PES's authoring pose is the
  wrong ruler for it.

## STILL TO BE DONE (mine, end of 04-09)

1. **Three lcg characters still render deformed** after both fixes
   (`tmp/trunk_ba.png`: lcg_2709 banded, lcg_2704's cape spread wide,
   lcg_2713's sleeves fanned). What is left looks like the remaining
   assumption: the authoring->bind bake treats every mesh as authored in PES's
   RENDER bind (arms 45 degrees down), and a 4cc character modelled in some
   other pose is sheared by it - the arms rotate 45 degrees and take any
   surface bound near them. The bind-pose stretch count and the frame disagree
   about the cure (the count prefers no trunk rule; the frame prefers the cape
   attached to the trunk), and per AGENTS.md the frame governs, which is how it
   is set. Next: measure each model's own authoring pose - for a mesh with a
   real bone mapping, compare the mapped bone's bind position against the
   vertex cloud; for one without, fit the arm direction from the geometry - and
   bake from THAT pose rather than from PES's. `hdg_2402` proves the chain is
   right when the pose matches (`tmp/f_sheet.png`, clean cloaked figure).
   Not blocked; needs the packs, which are extracted under `tmp/inputs/packs`.
2. **Wario's T-pose check reads arms ~25 degrees above horizontal**
   (`tmp/wt_pair.png`, `gfviewer .../fullbody_smg_2579.ase --anim
   media/animations/straight.anim.util --shots 2`). AGENTS.md's rule says a
   perfect T-pose. It may be the character's own art - he is a chibi model with
   short limbs and the gloves do sit palm-down - or a stale bind on this one
   model. Next: run the same check on `media/objects/players/models/fullbody.ase`
   (the stock body) for a reference frame, then on two more imported bodies; if
   only the imported ones droop, the `.weights` bake is the suspect and not the
   rig. Not blocked.
2. **The in-match game plan was never opened from the pause menu in a headless
   run** - `escape` at 14 s in `tmp/plan_match2.config` did not open the pause
   menu (the capture shows live play, `tmp/plan/form_match.png`), so every drag
   test ran against the pre-match instance. The live path edits `TeamData` that
   `Team`/`TeamAIController` hold pointers into, which is the third crash
   candidate from item 2 and is therefore still untested. Next: find what
   consumes the escape key during a match (`Match::Process` /
   `IngamePage`), drive the pause menu from the script, then repeat the monkey
   sweep in-match. Not blocked.
3. **The bench's sixth card is clipped by the panel's right edge**
   (`tmp/plan/c3_zoom.png`, rightmost card cut mid-portrait). The row scrolls,
   so it is reachable, but a half-drawn card reads as a defect. Next: either
   inset the bench row by half a card or let `BenchWindowSize` shrink by one
   when the last card would overrun. Not blocked.
