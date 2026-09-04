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

| # | Item | Owner | State |
|---|---|---|---|
| 1 | Monkey test: random input must never break the game plan | me | in progress |
| 2 | The drag crash on release | me | not reproduced yet, see below |
| 3 | LINEUP: drag to change formation, enabled in the release build | me | open |
| 4 | PES look and feel for the game plan | me | open |
| 5 | Multiple roles per player, on a secondary button | me | open |
| 6 | Hints, lower-left | me | open |
| 7 | Both teams at once; jump between them in one-pad coach mode | me | open |
| 8 | Pre-match screen revamp | me | open |
| 9 | Settings screen revamp | me | open |
| 10 | Sliders: finite steps, shown in the UI | me | open |
| 11 | Hide meaningless sliders (FORMATION) | me | open |
| 12 | Formation select reshapes the preview | me | partly done, verify |
| 13 | Portraits, stats, roles, medals on the lineup cards | me | open |
| 14 | Import the missing portraits | ImportProps | delegated |
| 15 | Reimport HDG (models broken) | ImportProps | delegated |
| 16 | Wario (smbg): dragging verts in cutscenes | ImportProps | delegated |
| 17 | Pitch UVs imported upside down (st002 most recently) | ImportProps | delegated |

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
