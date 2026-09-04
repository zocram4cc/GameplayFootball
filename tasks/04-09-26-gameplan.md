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

## The two sheets were one squad (04-09, late)

The frame that settled it: both maps drew the SAME team, the primary sheet
showing the opponent's players (`tmp/plan2/fresh.png`). It was not the team
resolution - a probe in the page's constructor read `teamID 0, dbID 13,
'/smbg/'`, the right side - it was the card views' NAMES. Every card was called
`planmap_player1_entry<i>`, and gui2 fetches a view's surface from the resource
pool by name (`Gui2WindowManager::CreateImage2D` -> `Fetch(name, ...)`), so the
two maps' cards shared one portrait, one strip and one caption each and both
showed whichever map was built last. Cards are named after their own map now
(`tmp/plan2/fixed_zoom.png`: /smbg/ on the left, /hdg/ on the right).

That also explains two things blamed on other causes: the midfield "pile" in
the middle of the diagram was two maps' cards drawn at two sets of
coordinates, and the "before/after" formation captures that showed different
squads were reading whichever map had loaded its surfaces last. The primary
sheet is named after its team now, like the opponent's.

## The in-match sheet, and a sixth use-after-free

No headless test had ever opened the game plan DURING a match, so the live path
- which edits the `TeamData` that `Team` and `TeamAIController` hold pointers
into - was untested. It opens: escape at 12 s (earlier attempts pressed it
during the pre-match phase, which is why they seemed to do nothing), twelve
`up` presses to clamp the focus at the top of the pause menu, enter
(`tmp/plan2/im_lp2_zoom.png`: GAME PLAN 1: FC Cataluña beside GAME PLAN 2:
Galacticos CF, each with its own squad).

The monkey then ran 1200 taps inside it. Nothing crashed in the release build;
under ASan it found a sixth use-after-free, and not in the game plan at all:

    READ of size 4 in Gui2Slider::GetValue()
      GameplayPage::Exit()            settings.cpp:480
    freed by
      Gui2View::Exit() ... GameplayPage::Exit()  settings.cpp:520

`Exit()` runs twice on a page the user leaves - `GoBack()` exits it and the
window manager's deferred delete pass exits it again - and the second pass read
every slider back out of freed memory to save it. Both settings pages that save
on exit now save once (`settingsSaved`).

## HDG: what re-importing fixed, and the two gates it needed

Every one of the 23 installed HDG models rendered and examined, before and
after (`tmp/hdgsheet/sheet.png`, `tmp/hdgsheet2/sheet2.png`). Five players were
a prop standing where a player should be - a corpse on the ground, a gas cloud,
a stim scatter, a throne, a floating eagle - because `is_continuous_body` passes
anything that spans hip to head without a gap, and a gas column does.

Measured over HDG's 22 exports, vertices between 0.95 m and 1.55 m within
0.25 m of the rig's axis: John Helldiver 14996, Lobby doko 16384, Mechwarrior
5756, SEAF-chan 3155 - against Mothdiver 118, Brapdiver 116, Bullet Sponge 19,
and Malicious Code / The Cuck Throne / "Not gonna sugarcoat it" at 0.
`whole_body` now requires 200 of them, so those five are composited over PES's
body and stand up as players.

And "EAT" shipped a 40-vertex effect ray spanning 11 m. The engine scales a
body by its own height, so that one ray shrank the whole character to a pile on
the grass; a mesh spanning more than 4.5 m is dropped now (the tallest
legitimate export on disk is Mothdiver's wings at 3.84 m). EAT measures
2.40 m and renders as a mech.

After both gates, 19 of 23 HDG models have a chest on the axis and every
height is between 1.75 m and 4.44 m (was 0.36 m to 10.99 m).

## Portraits

All seven imported teams now have 23/23 portraits and every file resolves
(161 entries, 0 missing). Six teams' portraits are their pack's own art, read
by `import_team.bind_portraits` (`c0e8194`).

/vn/ had none: its pack is not on disk any more, and its 23 installed models
were not even bound to its players. The models are bound now (export order, 23
for 23) and `render_portraits.py` renders a head-and-shoulders still of each
player's own model through the engine's loader (`gfviewer --portrait`), which
is what a card wants when a pack ships no art. Proven on HDG
(`tmp/pf_row.png`: a Helldiver's helmet, face on).

For /vn/ itself the result is honest but poor, and the reason is in the data:
all 23 models are the SAME mesh (68,322 vertices, identical bounds), the same
`body.png`, and each per-player `*_face.png` is the same blue star. There is no
per-player art in the installed data to find. A card now shows the team's own
model instead of a blank.

## The secondary button's answer

Pressing it toggled the role and rebuilt every card, inside the event that
asked for the toggle - so the card holding the cursor was deleted and the
visible answer was the focus falling out of the map. It re-prints the one card
now (`Gui2PlanMap::RefreshRole`): the position strip, and the MEDAL, which is
the only part that can change - the count beside the position is of his OTHER
positions, and toggling the one he is standing in cannot alter it.

Both paths verified on frames: the gamepad's secondary toggles in place, and
the keyboard's `x` opens his roles with the position he is standing in
highlighted (`tmp/plan2/m1_full.png` - FUCK LUIGI, CF lit among the ten).

## STILL TO BE DONE (mine, end of 04-09)

1. **Three lcg characters still render deformed** (`tmp/trunk_ba.png`:
   lcg_2709 banded, lcg_2704's cape spread wide, lcg_2713's sleeves fanned).
   What is left is the remaining assumption: the authoring->bind bake treats
   every mesh as authored in PES's RENDER bind (arms 45 degrees down), and a
   4cc character modelled in another pose is sheared by it - the arms rotate 45
   degrees and take any surface bound near them. The bind-pose stretch count
   and the frame disagree about the cure (the count prefers no trunk rule, the
   frame prefers the cape on the trunk); per AGENTS.md the frame governs, and
   that is how it is set. Next: measure each model's OWN authoring pose - for a
   mesh with a real bone mapping, compare the mapped bone's bind position
   against the vertex cloud; for one without, fit the arm direction from the
   geometry - and bake from that pose. `hdg_2402` proves the chain is right
   when the pose matches (`tmp/f_sheet.png`). Not blocked; the packs are
   extracted under `tmp/inputs/packs`.
2. **Two HDG players are drawn a fifth of the right size** - hdg_XXX03
   (Mothdiver, 3.88 m) and hdg_XXX09 ("Not gonna sugarcoat it", 4.44 m). Both
   are composited, so a body IS underneath; the engine scales a body by its own
   height, so a character whose prop reaches 4.4 m is shrunk to fit. Next:
   either drop meshes sitting entirely above head height, or - better, and what
   PES does - draw a player at the height his database row gives instead of
   scaling every model to 1.81 m. Not blocked.
3. **/vn/ has no per-player art at all** and its portraits are therefore 23
   copies of one render. Its 23 models are the same mesh (68,322 vertices,
   identical bounds), the same body.png, and each per-player `*_face.png` is
   the same blue star; nothing in the installed data distinguishes the players.
   Next: nothing, until somebody has the /vn/ pack - then
   `import_team.py <pack>` writes the real portraits and models and
   `render_portraits.py` leaves them alone.
### Resolved since the list was first written

* **The roles submenu over the opponent's sheet** - a submenu takes the button
  column's cell, and the opponent's sheet sits under that cell, so the ten role
  rows drew across the other team's pitch. The sheet is hidden while a submenu
  is open and comes back with the column (`tmp/plan2/submenu_ba.png`).
* **The in-match sheet's drag under ASan** - done. Four grab/move/drop cycles
  and an abandoned drag inside the sheet during a match, ASan clean
  (`tmp/live_drag2.log`, `tmp/plan2/drag_row.png`), after the monkey's 1200
  random taps in the same place found and fixed the settings crash above.

* **Wario's T-pose check** - not a defect. AGENTS.md's health check holds for
  PES-derived bodies: `fullbody_pes.ase` renders a perfect T-pose and the
  legacy `fullbody.ase` droops 30 degrees under it (`tmp/tp_pes_row.png`),
  because the legacy body is authored in GF's own pose. Wario is a chibi model
  whose arms are short and whose gloves sit palm-down; his mesh holds together
  through eight frames of a sliding celebration (`tmp/wario_sheet.png`).
* **The bench's sixth card** - not clipped; the crop was.
  `tmp/plan/bench_row.png` shows all six drawn inside the panel.
