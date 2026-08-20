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
| 82 | Standalone model viewer, no match logic | pending — `gfviewer` builds but dies reading geometry the engine loads on worker threads |
| 83 | Composite slot-override exports over the base body | new, from the showcase |
| 84 | Drop scenery meshes that swallow a player's bounds | new, from the showcase |
| 85 | Give substitutes their imported model | new, from the showcase |

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

**7 of the 10 installed stadiums carry such meshes, 52 in all**: st043 11, st011
8, st060 8, st017 7, st002 6, st041 6, st060_full 4, st056 2. st019 and st031 are
clean. Only st017 has been re-converted so far; the rest still need it, with the
flags they were first built with — st017 needs `--max-extent 3000`, since the
default 260 m drops the landscape as oversized scenery.

Two things worth knowing before recording a re-converted stadium: delete the
stale `.ase.geomcache` or the engine keeps the old winding, and then warm it,
because a cold re-parse of a 148 MB stadium ASE takes longer than a 200 s capture
window — the first attempt filmed nothing but the title screen.

## Loose end

`data/media/objects/helpers/*.ase` and `data/media/objects/menu/background01.ase`
are modified in the working tree by an `ase_compact` run from the #79 compression
work that was never checked on screen. They are left uncommitted rather than
pushed as unverified asset rewrites: `ase_compact` dedupes TVERTs, which moves UV
indices, and that is the same machinery #80 is blocked on.
