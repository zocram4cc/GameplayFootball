# Where this stands

A snapshot, 19 August 2026, taken after the HDG showcase. The checklist is the
state of every numbered item; the sections after it record what the showcase
proved and — at more length, because it is the live problem — what is still wrong
with imported player bodies and why.

Formats and the import pipeline are documented in
[PES21_IMPORT.md](PES21_IMPORT.md). How the recording harnesses work, and the
traps they exist to avoid, is in [HARNESSES.md](HARNESSES.md). What the
repository deliberately does not ship is in [ASSETS.md](ASSETS.md).

## Checklist

Done:

| # | | |
|---|---|---|
| 39 | Replays not firing after a goal celebration | fixed |
| 40 | Foul cutscene cameras re-examined | fixed |
| 41 | Substitutions firing before kickoff | fixed |
| 42 | Goalkeepers dive and use their hands | fixed |
| 43 | Close-up replay for fouls and red cards | fixed |
| 44 | Cutscene for offside and disallowed goals | fixed |
| 45 | Sole tactical control in coach mode | fixed |
| 46 | Fall back to fullbody.ase when the PES body is absent | fixed |
| 47 | Heap abort in TTF_CloseFont at shutdown | fixed |
| 48 | The E rendering black-on-black in TV overlays | fixed |
| 49 | Choppy animation on hardware | fixed |
| 50 | Stadium / time / weather / cutscene selectors | added |
| 51 | Stadiums referencing PES materials we never imported | audited |
| 52 | Pre-match rigging, camerawork and walkout | fixed |
| 53 | Current-player, philosophy and attack/defence HUD | added |
| 55 | Every stadium in stadiums/ converted | done |
| 56 | One substitution notification, under the scoreboard | fixed |
| 57 | Frame graded through PES's own colour LUT | done |
| 58 | Namek showcase with two non-/a/ teams | recorded |
| 60 | Foul cutscenes broken by the entrance changes | fixed |
| 61 | Badge aspect ratio, UI hidden on replays | fixed |
| 62 | Auxiliary cast: touchline furniture, crowd, flags | done |
| 63 | Instanced geometry for crowd and turf | done |
| 64 | Tunnel, arch and entrance cast for the walkout | done |
| 65 | PES ad boards and crowd noise | imported |
| 66 | What hazes st031's wide beat | found |
| 67 | Ad boards rendering mirrored | fixed |
| 68 | The black outline around everything | decided |
| 69 | Replay wipe from 4cc_20_swipe.cpk | imported, plays |
| 70 | Celebration loops chained after the intro | done |
| 71 | Goal camera at PES's authored distance | fixed |
| 72 | The rest of PES's cutscene camerawork | imported |
| 73 | Cutscene anchoring by where a track looks | done |
| 74 | Corner flag cloth stood up on its pole | fixed (you verified) |
| 77 | Foul cutscene sliding a player a thousand yards | fixed (you verified) |
| 78 | Teams reset to kickoff shape at half time, ends swapped | fixed (you verified) |
| 79 | Compression without giving up readable formats | 31 GB → 11 GB |
| 81 | HDG imported fully, tactics from the .ted | done |

Open:

| # | | where it stands |
|---|---|---|
| 38 | Perfect fullbody models every time | **in progress** — two causes fixed, a third understood and not yet fixed; see below |
| 54 | Match the VGL26 Day 7 reference | pending |
| 59 | PES's own pitch model and 3D turf | in progress |
| 76 | Floppy surfaces on one mechanism | in progress — corner flag done; banner and pennant are authored flat and need choreography to attach them to their bearers' hands |
| 80 | Corner flag cloth samples the wrong half of its texture | **blocked** — the gate is correct in isolation, but applying it needs `stadium_staff._write_figure` to emit per-corner TVERTs the way `adboard_uvs.py` does |
| 82 | Standalone model viewer, no match logic | **in progress** — four bugs fixed (see below); the model is in the scene and enabled and still is not rasterised |

Closed since: **81** (all 23 /hdg/ players bound to the body the .ted assigns —
15 Helldiver, 7 Helldiver Headless, 1 Alexus), **83** (kit alpha *is* honoured:
`simple.frag` discards below 0.12 — but no pack ships a transparent kit, so
compositing is not the answer), **84** (not needed — the binding gate covers it),
**85** (substitutes get their model), **86** (a prop is no longer bound as a body),
and the shadeless shader.

### What #82 still needs

`gfviewer` loads a model, reports it, frames it from its own bounds, presents N
turntable frames through the recording path and exits cleanly. Four real bugs went
with that: the vertex buffer was read by striding the whole buffer by the element
count rather than taking the position block (hdg_2402 measured a 1.49 m median edge
on a 1.6 m body), teardown aborted with "Observer(s) still present", nothing called
`SetCapping` so the near/far planes were never set, and the loaded node was never
added to the scene.

It still does not show the model, and the search is narrowed rather than open:

- the geometry is in the scene and enabled (`1 geometry object(s), 1 enabled`), so
  it is not the loader
- a 120-degree frustum changes nothing, so it is not aiming or culling
- forcing `sky_horizon`/`sky_zenith` to pure red leaves the frame neutral, so the
  uniform fill is not `postprocess.frag`'s sky gradient

Which leaves the compositing stage. A second host has to bring up more of the
renderer than registering the graphics system's three phases.

### #80, on measurement rather than sight

The per-corner TVERT work is done: figures write 3 TVERTs per face with `TFACE`
indexing them, verified lossless (0 of 96 UVs change on the real flag mesh). But
the defect cannot occur as described. The cloth samples V 0.266 to 0.991;
`cf_common_bsm` carries the flag art over V 0.25 to 1.0 with the grey band only
below that; and `aseloader.cpp` reads per-face UVs from `TFACE` without welding by
position. `cloth.match_mesh_uvs` is also an identity here, because the flag's two
sheets share their positions *and* the UVs on them. Corroborating on screen needs a
camera that shows a corner flag, which none of the recorded footage does.

Done since this snapshot was first written: the stadium grounds (all seven
affected grounds re-converted, `14ecace`/`af1ca3b`), the `.ted` requirement for
squad model imports (`be27ff7`), and the body coverage measure (`3f01a62`).

## The HDG showcase

HBR (HDG, team 11) 4–0 LCG (team 9), 10-minute halves, st017 "planet namek",
29:43 recorded. HDG played a deliberately extreme tactic so it would not look
like anything shown before: offence depth 0.98, width 0.18, line 0.92, pressure
0.95, counter 0.95, support 0.10, centre magnet 0.90 — narrow and vertical
against LCG's expansive shape.

It reached teardown, which is the thing a recording has to prove before anything
it shows can be trusted. Four goals each drew a celebration and a replay, and
every stoppage category fired: substitution, offside, foul/warning,
foul/card_yellow, foul/card_red, timeup. No missing textures and no dropped
meshes in the log.

**Frame rate held.** The engine renders 1280×720 and the encoder decimates 2:1,
so 53,498 encoded frames means about 107,000 frames presented inside the 1800 s
window — roughly 59 fps, and the true figure is a little higher because match
loading is inside that window too. Raising the triangle budget from 20,000 to
100,000 in #38 therefore cost nothing measurable, which was the open risk: some
bodies now carry ~96k faces and there are 22 of them on the pitch.

One thing to retract: mid-investigation the replay wipe looked like it was stuck
on screen, because four sampled frames showed it at full cover. It is not. Full
cover lasts about 0.3 s per replay, and four hits out of 892 samples two seconds
apart is exactly what that probability looks like.

## Imported player bodies

Two players render on the pitch as a fan of blades and a pile of plates. All 93
installed bodies were rendered at rest, offline, with
`tools/pes21_import/ase_render.py` — and they are already like that in the file.
So it is not the skinning, and not the animation.

It is not a broken import either. **A 4cc aesthetic export is often not a body.**
The packs override PES's boots, glove and face slots and lean on the
invisible-kit trick: the character you see is those pieces drawn over PES's own
base body, with a transparent kit texture hiding the body itself. 2HUG's pack
contains only `Boots/`, `Faces/`, `Gloves/` and `Kit Textures/` — no body at all.
Read such an export as a whole body and it comes out missing everything the pack
expected PES to supply:

| model | what it actually contains | how it renders |
|---|---|---|
| `lcg_2702` | `boots`, `wings` | the fan of blades, no player under it |
| `lcg_2706` | `dummy_kit`, `head` | a headless, featureless torso — `dummy_kit` is a placeholder PES never draws |
| `lcg_2718` | `bg_bsm`, `boots` | nothing: a backdrop mesh reaching 362 m sets the bounds, so the figure frames down to a dot |
| `lcg_2712` | `dummy_kit`, `face` | a diorama with geometry near none of the 20 joints |
| `ateam_70201` | the full stock mesh set (`shirt`, `shorts`, `arms`, `neck`, `boots`, …) | whole — these were imported over the base body, which is why they look right |
| `hdg_2402` | `armor_bsm`, `cape_bsm` | whole, because the armour plate set happens to cover a whole figure |

### How a body is judged

`tools/pes21_import/body_coverage.py` (13 tests in `test_body_coverage.py`) asks
the geometry rather than the mesh names, because names are the pack author's
business: for each joint of the native rig, is there anything near it?

The threshold is measured, not chosen. Over the bodies that do render whole —
the stock body, `lcg_2709`, `ateam_70201`, `hdg_2402` — the furthest any joint
sits from its nearest vertex is 0.18 m. `lcg_2702` leaves 17 of its 20 joints
bare. Nothing measured falls in between, so 0.20 m separates them. The scenery
limit is 4.0 m for the same reason: the tallest whole import (`hdg_2421`) stands
2.20 m, and `lcg_2718`'s backdrop reaches 362 m.

Over what is installed today:

```
60 whole, 30 needing a base body under them, 3 carrying scenery
```

### What fixes it

`fmdl_to_fullbody.py` already has the `--base` flag that composites an export
over the stock skinned body — it is why the `ateam` models are whole. #83 is to
have `import_team.py` pass it whenever `body_coverage.py` says "needs base".

One question decides how well that generalises: **does the engine honour kit
alpha on the body?** PES hides the base body with a transparent kit rather than
by omitting it. If our renderer honours that alpha, then always compositing the
base body is both faithful and self-regulating — a transparent kit makes it
vanish, so the genuinely non-humanoid characters in these packs (there is a
shark and a giant hand) stay as their authors drew them instead of getting a
footballer parked inside them. If it does not, the compositing has to be gated
on coverage and the non-humanoid characters need a decision of their own.

`HumanoidBase::SetKit` swaps the diffuse texture only on meshes whose current
diffuse is `kit_template.png`, so a custom body never receives the kit at all
today; a composited base body would need its kit mesh to carry that texture name.

### Two hypotheses that were wrong

Both are recorded because they are the plausible ones, and because ruling them
out is what leaves the explanation above standing.

**Joint indices past the end of the rig.** The engine packs three influences into
a vertex colour, and `gamedefines.cpp` scales it by 255 before
`humanoidbase.cpp` decodes `jointID = floor(v / 10)`. A channel above 0.784
therefore decodes to a joint past a 20-joint rig and reads `jointTransforms`
out of bounds. Replaying that decode over 2,595,629 vertex colours across all 93
bodies: highest joint referenced is 19, highest channel 0.780. Zero.

**Vertices collapsing to the origin.** An influence is kept only if its weight
exceeds 0.01; a vertex where all three fall below would get a zeroed blend
accumulator and collapse onto the body origin, dragging its triangles into
exactly the long thin shards a fan of them makes. Zero such vertices.

### Also found

`Team::InitPlayers` gives a starter with an imported model its own body and its
own vertex-colour map (`team.cpp:100-116`), but `team.cpp:215` activates every
substitute with the shared `fullbodyNode` and `playerColorCoords`. A player who
comes off the bench therefore loses the model `playermodels.cfg` assigns him and
appears as the stock body. Filed as #85; unrelated to the exploding models.

## Stadium grounds render as sky

Namek showed flat green outside the pitch and st002 flat blue-purple. Neither is a
ground colour: `postprocess.frag` fills below the horizon with the graded fog
colour, and st017's `sky.txt` says `horizon 0.34 0.76 0.11` while st002's says
`0.055 0.055 0.169`. The flat colour was the hole where the ground should be.

The ground was imported the whole time. Namek's landscape is 9984 triangles, UVs
spanning a full 0..1, an opaque 4096x4096 texture whose mean colour is teal, well
inside the 702 m far plane and listed in the stadium's `.object`. It is wound with
its lit side underneath. Area-weighted facing, where +1 is straight up:

| mesh | faces | facing |
|---|---|---|
| `Namek` | 572 | **-0.953** |
| `Namek2` outline shell | 9804 | -0.936 |
| `Namek2` (the landscape) | 9984 | **-0.935** |
| next mesh down the list | 2346 | -0.491 |
| a stand (closed volume) | 20000 | +0.109 |

PES draws its landscape two-sided and this engine culls back faces, so a mesh
wound that way is not dim here but absent. It is the same trap that
`faces_away_from_pitch` and `stadium_staff.wants_winding_flipped` already
document, in the one place where it costs a whole landscape.

`stadium_to_gf.faces_downward` now turns such a mesh round, skipping outline
shells and sky domes, and judging by area-weighted facing so closed volumes -
which cancel to near zero - are left alone. The threshold of -0.8 sits in the gap
between -0.935 and -0.491 with nothing in it.

**7 of the 10 installed stadiums carried such meshes, 52 in all.** All are
re-converted now, via `rescene.sh` with `MAX_EXTENT=3000`, which rebuilds the
scene ASE without disturbing the props, crowd and entrance alongside it. The
default 260 m drops the landscape as oversized scenery, so it has to be raised.
Measured afterwards, none has a downward sheet left:

| stadium | geoms | facing down, before -> after |
|---|---|---|
| st002 | 152 | 6 -> 0 |
| st011 | 84 | 8 -> 0 |
| st017 | 24 | 7 -> 1 |
| st041 | 21 | 6 -> 0 |
| st043 | 86 | 11 -> 0 |
| st056 | 18 | 2 -> 0 |
| st060 | 39 | 8 -> 0 |
| st019, st031 | 5, 16 | 0 -> 0 (were already clean) |

st017's remaining one is `pes_st017_25`, the ground's own outline shell, which the
converter reverses deliberately so the engine culls the side PES culls.

Two exceptions. `st060_full` is a differently-built st060 living in its own slot,
and its `.object` names `pes_st060.ase`, so `rescene.sh` cannot target it; it has
one marginal mesh at -0.81 and is only referenced by `menu_smoke_stadfull.config`,
so it is left as it is. And st002's geom count moved 154 -> 152, the only count
that changed; its ASE is 1.13 GB either way, so that is not a size regression.

Two things worth knowing before recording a re-converted stadium. Delete the stale
`.ase.geomcache` or the engine keeps the old winding — `rescene.sh` does this. And
allow for the cold re-parse: 148 MB took longer than a 200 s window and filmed
nothing but the title screen, and st002's 1.13 GB took about 15 minutes to reach
its 454 MB cache. st017 and st002 are warm; the rest rebuild their cache on first
load, which costs that one run and nothing after it.

Verified on screen for st017, where the entrance camera now looks out on water and
cliffs instead of green nothing. The other six are verified by the facing measure
only; st002 shows a building beyond the pitch that the earlier recording did not,
but the two frames are from different points in the entrance so that is not a
matched pair.

## Controlling a match: coach mode, tactics and controllers

There is no on-screen indication of any of this, which is itself the finding. What
follows is read out of `Match::ProcessTacticalHotkeys`,
`Match::ProcessTacticalHotkeysForPad`, `Match::GetTouchlineDevice` and
`CoachMode::FromHumanGamerCounts`.

### How a side ends up coached

`CoachMode::FromHumanGamerCounts` decides per team, from the number of human
gamers on it and the `coach_mode` setting:

| human gamers on the team | `coach_mode` off | `coach_mode` on |
|---|---|---|
| one or more | plays the players | plays the players |
| none | AI | **coached by a human** |

So coach mode is not a mode you enter for the match, it is a property each side
acquires by having nobody on the sticks. Two consequences worth knowing:

- **One controller, both teams coached: yes.** Assign no human gamers to either
  side and turn `coach_mode` on, and both become `HumanCoach` -
  `CoachMode::IsManagerDuel`. You do not need one controller per side.
- **One controller assigned to a team, with coach_mode on, coaches the opponent.**
  The side you are assigned to plays football; the *other* side, having nobody on
  it, becomes coached rather than AI. That is what the rule says, and it is
  probably not what someone selecting "coach mode" expects.

In coach mode nothing is run by the CPU manager at all, on either side
(`AIManager::AIManagerRuns` returns false whenever any side is coached).

### The controls

**Gamepad** — hold **RT** (`e_ButtonFunction_Sprint`) as a touchline modifier;
without it the d-pad plays football as usual. Only the press counts, so holding a
direction does not spin through presets.

| RT + | does |
|---|---|
| D-pad up | all-out attack |
| D-pad right | attacking |
| D-pad down | all-out defence |
| D-pad left | defensive |
| Short pass | toggle frontline pressure |
| Shot | toggle deep defensive line |
| High pass | toggle hug the touchline |
| Long pass | toggle tiki-taka |

Which pad addresses which bench: a human on the sticks uses his own pad, and a
coach uses the pad matching his bench - **controller 0 runs team 0, controller 1
runs team 1**.

**Keyboard** — always addresses the coached team, and **Shift** addresses the other
bench in a manager duel (`GetCoachedTeamID(preferSecondTeam)`).

| key | does |
|---|---|
| Page Up | push the team up the pitch |
| Page Down | drop it back |
| F5 | frontline pressure |
| F6 | deep defensive line |
| F7 | aggressive defence |
| F8 | hug the touchline |
| F9 | centre shading |
| F10 | tiki-taka |
| F11 | long-ball counter |

Every change takes effect immediately (`AnnounceInstructions` calls
`UpdateTactics`) and is announced on screen, so the feedback exists once you know
the key - it is only the discovery that is missing.

### Still to check

Nothing above has been exercised with a controller in hand; it is read from the
source. A pass over UI and playability should confirm it, and answer the question
the table above raises: whether "coach mode on, one controller assigned" coaching
the *opponent* is intended, or whether picking coach mode should put the human on
the bench of the side he selected. There is also no in-game surface listing any of
these bindings.

## Loose end

`data/media/objects/helpers/*.ase` and `data/media/objects/menu/background01.ase`
are modified in the working tree by an `ase_compact` run from the #79 compression
work that was never checked on screen. They are left uncommitted rather than
pushed as unverified asset rewrites: `ase_compact` dedupes TVERTs, which moves UV
indices, and that is the same machinery #80 is blocked on.
