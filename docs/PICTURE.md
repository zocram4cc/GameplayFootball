# The picture

How a frame gets from the lit scene to the screen, what each stage does to it, and
which of the things that look wrong are defects rather than somebody's art.

Everything here is measured against one still from the VGL26 Day 7 broadcast
(`ref.png` in the job's scratch directory, the reference for task #54). Its
luminance ladder is the yardstick used throughout:

| | median | p90 | p98 | spread (sd) |
|---|---|---|---|---|
| reference broadcast | 0.537 | 0.827 | 0.918 | 0.132 |

**Measure the ladder, not the median.** A level error and a contrast error look
identical in the middle of the histogram. The colour grade defect below moved the
median by 0.007 while destroying the top 40% of the range - reading medians alone,
it looked like nothing was wrong, and a whole round of exposure work went into
chasing a problem that was not there.

## 1. The chain, in order

`data/media/shaders/postprocess.frag`, top to bottom:

1. **Exposure** — a gain on the lit frame, from a 4x4 tap grid.
2. **Fog** — `skyFogColor` mixed in by linearised depth.
3. **`EngineGrade`** — the engine's own brightness, saturation and contrast
   (`ContrastSaturationBrightness` then `AlternateContrast`, bias 0.3).
4. **Sky fill** — where the depth was never written, a view-direction gradient
   between `skyHorizonColor` and `skyZenithColor`.
5. **`LutGrade`** — PES's colour grading table.
6. **Vignette**, then clamp.

Two things follow from that order and both have bitten:

- The exposure runs *first*, so it can only move what was actually lit. The sky
  fill at step 4 is painted afterwards and is beyond its reach.
- Steps 3 and 5 both lift midtones substantially, so the frame as lit reads far
  darker than the frame as shown.

### Exposure

`graphics_exposure_key` (0.45), `graphics_exposure_min_gain` (0.55),
`graphics_exposure_max_gain` (1.6). PES's own concept is `gameKeyValue 0.18` with
`gameMinExposure`/`gameMaxExposure` per atmosphere; this is the same idea with one
key for every ground.

**It is measured on the engine side, not in the shader, and that is the whole point.**
A fragment shader remembers nothing, so a gain computed inside it is rebuilt from
scratch every frame — and sixteen taps at fixed screen positions see entirely
different things as a camera moves. Measured off a recorded match, the picture's mean
brightness moved 0.0114 from frame to frame through the opening cutscene, 39% of
frames moving more than 0.01, with single-frame jumps of −0.073 and +0.038. It read
as a flicker on every cut and pan.

The renderer now reads a centre window of the frame it just presented (a quarter of
the width and height, every third frame — the adaptation is slow, and a readback costs
a sync) and `AutoExposure::Adapt` walks the gain toward what that asks for over a
half-life: `graphics_exposure_half_life`, 1.2 s by default, frame-rate independent,
the same both ways. The shader is one multiply. On the same six-second window the mean
frame-to-frame change fell to 0.0011 and the frames moving more than 0.01 from 39% to
1%.

Centre-weighted metering is also what a camera does, and the readback measures the
*final graded* frame, which is the space the key is calibrated in.

Historical note, because the reasoning is worth keeping: the in-shader version
measured through `EngineGrade` and `LutGrade` and skipped cleared-depth taps, for
these reasons:

- **Measure where the frame is judged.** It used to sample the raw lit frame and
  compare that against a key read off graded pictures. Every scene therefore
  measured too dark, the gain sat pinned at its 1.6 ceiling, and the pass became a
  flat brightening: across nine grounds all nine got brighter, including the four
  already past the reference, and the spread between them *widened* from 0.31 to
  0.35. The taps now go through `EngineGrade` and `LutGrade` first.
- **Skip empty background.** Where the depth was never written the accumulation
  buffer holds the clear colour. Counting those taps read a frame as far darker
  than it is shown and asked for light the picture did not need — st011 is most of
  a bowl under a wide sky and was still being lifted at a median of 0.55.

Instrumenting the shader to write `displayed`, `ratio` and the tap count into three
patches on the bottom edge of the frame is how that was pinned down; the readback
is `pow(value, 2.2)` to survive the sRGB framebuffer, and note that `texCoord`
comes from `gl_FragCoord`, so the patches are at the **bottom** of the image, not
the top. Reading them from the top gave plausible-but-wrong numbers for a while.

## 1a. The entrance cast blinking

The same recording showed the squads blinking in and out of the centre circle every
frame or two. `HideUnstagedPlayers` called `HumanoidBase::Hide()` on every player who
was not part of the staging, once a frame — and `Hide()` only parks the model at
(1000, 1000, −1000), which `UpdateFullbodyNodes` puts straight back because it follows
the humanoid node. Two different schedules, so whichever ran last decided whether that
player was on screen. The old comment on the function said as much ("this has to run
... on every frame") without noticing that the race was the bug.

Being parked is a state now (`SetBenched`), and `EntranceCast::ShouldBench` gives one
answer per player per frame which is applied either way — saying "not parked" out loud
is what lets it clear when the entrance ends. Counting player-coloured pixels per
frame, the frame-to-frame change fell from a median of 6.5% to 0.6%, and pairs jumping
more than 20% from 17% to 1%.

### The colour grade, and how its table is chosen

PES grades every frame through a 33-cubed lookup table picked by time of day and
weather. `tools/pes21_import/lut_strip.py` unrolls the set into one PNG
(`media/textures/lut/grade.png`, 1089 x 132: 33 blue slices across, one band per
condition down) and `postprocess.frag` samples it with the blue axis interpolated
by hand and red and green by the texture's own filtering.

**Eleven of the sixteen tables PES ships cannot be a display transfer at all.**
Their grey response:

| input | day / cloudy / evening (`_s_*_game`) | night | `lut_h_day_demo` |
|---|---|---|---|
| 0.00 | 0.021 | 0.014 | 0.021 |
| 0.25 | 0.277 | 0.227 | 0.277 |
| 0.50 | 0.616 | 0.606 | 0.623 |
| 0.75 | 0.666 | 0.978 | 0.773 |
| 1.00 | **0.689** | 1.000 | **1.000** |

Half the input range lands inside a 0.07 band. Applied as a display transfer that
costs the frame its whole top end: on st011, p98 0.659 graded against 0.918
ungraded and 0.918 in the reference, with the spread down to 0.087. The picture
reads milky and flat, and *the median barely moves* (Planet Namek 0.424 -> 0.431),
which is why keying the exposure never touched it.

The decode was never at fault — the ftex read matches PES bit for bit. The
selection was: the importer took `lut_s_day_game` on the strength of its name. It
now measures instead (`grey_response`, `spans_display_range`, `plan_bands`) and
takes the first table per band that actually reaches white:

```
day:     lut_h_day_demo     grey 0.5 -> 0.623, white -> 1.000
cloudy:  lut_h_day_demo     borrowed from day: nothing it ships reaches white
evening: lut_h_day_demo     borrowed from day: nothing it ships reaches white
night:   lut_h_night_demo   grey 0.5 -> 0.589, white -> 1.000
```

Only `lut_h_day_demo` and the four night tables qualify. Cloudy and evening have
nothing usable of their own and borrow the day table, which the importer prints as
it writes.

`graphics_lut_strength` defaults to 1. Over all nine converted grounds, as distance
from the reference's whole ladder, it is a wash — 3.44 graded against 3.49
ungraded, closer on five of nine — so PES's own colour is the tie-breaker:

| ground | ungraded (med / p98 / sd / off) | graded | |
|---|---|---|---|
| st002 | 0.18 / 0.81 / 0.19 / 0.92 | 0.16 / 0.84 / 0.24 / **0.82** | graded closer |
| st011 | 0.51 / 0.98 / 0.22 / 0.21 | 0.57 / 0.96 / 0.22 / **0.20** | graded closer |
| st017 | 0.39 / 0.90 / 0.19 / **0.40** | 0.43 / 0.86 / 0.20 / 0.41 | ungraded closer |
| st019 | 0.55 / 0.91 / 0.25 / **0.15** | 0.60 / 0.95 / 0.25 / 0.27 | ungraded closer |
| st031 | 0.53 / 0.93 / 0.20 / **0.21** | 0.59 / 0.84 / 0.21 / 0.37 | ungraded closer |
| st041 | 0.45 / 0.98 / 0.23 / 0.28 | 0.57 / 0.95 / 0.22 / **0.18** | graded closer |
| st043 | 0.25 / 0.99 / 0.23 / 0.74 | 0.27 / 0.92 / 0.25 / **0.59** | graded closer |
| st056 | 0.42 / 0.98 / 0.28 / 0.36 | 0.49 / 0.93 / 0.28 / **0.29** | graded closer |
| st060 | 0.57 / 0.96 / 0.26 / **0.21** | 0.64 / 0.95 / 0.26 / 0.32 | ungraded closer |
| | **3.49** | **3.44** | |

It helps most where a ground is dark and costs a little where one is already
bright. Side by side the old table is unmistakably milky; this one holds Namek's
rocks pink and st011's grass green.

### Fog

`clamp(fragDepth * 0.01 * (1 - fogScale) - 0.16 * fogScale, 0, 0.25) * fogStrength`,
mixed toward `skyFogColor`. `graphics_fog_strength` defaults to **0**, and a
converted ground scales it by its own atmosphere's `influenceOfFog` through
`lighting.txt`. Every readable 4cc atmosphere asks for none; on Planet Namek the
engine's quarter-of-the-horizon wash turned its pink rock formations flat green.

### The sun

Each 4cc stadium download ships `light/#Win/.../*.fox2.xml` — a place, a date and a
time, which fixes the sun to the degree. `stadium_lighting.py` does the astronomy at
import time and writes `lighting.txt` beside the stadium; `SceneLighting::Parse`
reads a vector.

**Six of the nine converted grounds have no sidecar**, because their packs came out
of cpk extractions, which keep PES's binary atmosphere (`.atsh`, `.rpd`, `.pcsp`)
rather than readable XML. The fallback used to be `random(-1.7, 1.7)` on two axes
over a height multiplier of 1.3, which puts the sun near the zenith more often than
not, drives `noonBias` to 1, and lights the ground with the full cool noon sun from
overhead. That is what washed those six to white — medians of 0.55-0.57 against the
reference's 0.434 at the time, while the three grounds that do carry a
`lighting.txt` sat at 0.26-0.41 and kept their own colour.

`SceneLighting::DefaultSun` now gives a fixed mid-afternoon sun instead: 44 degrees
up, across the ground rather than down it, lowered toward 6 degrees as the
time-of-day selector moves to night, never below the horizon. The same shadows fall
every kickoff, which is what a broadcast looks like.

## 2. The black outline around everything

Three separate things read as black outlines and only the first is an effect.

### The engine's edge pass (upstream, still active)

`ambient.frag`'s `GetEdge` is a Sobel over depth *and* normals — nine taps, a
depth-ratio test at 35x and a normal-dot test at 0.5 — written to `modifier.r`.
`postprocess.frag` uses it for exactly one thing:

```glsl
  // edge blur
  if (modifier.r > 0.0) {
    ... average of the four neighbours ...
    base = base * (1.0 - modifier.r) + smoothPixel * modifier.r;
    //base = base * (1.0 - modifier.r) + vec3(0, 0, 0) * modifier.r * 0.5 + smoothPixel * modifier.r * 0.5; // cartooney effect
  }
```

It is cheap anti-aliasing: this path has no MSAA. But the average is taken *across*
the silhouette, so on an object against a bright sky it pulls the object's dark
pixels onto the sky side and every object gets a one-pixel dark fringe. The
original author's explicitly black version is still there, commented out — so the
outline was a deliberate style once and then disabled, and what survives is a
weaker accident of the AA.

**Settled (#68): the blur only averages neighbours on the same surface.**
`edgeBlurDepthTolerance` (`graphics_edge_blur_tolerance`, 0.02) is the depth test as
a fraction of the fragment's own depth, and it spans every behaviour worth having -
0 accepts nothing and turns the blur off, a small value blurs along an edge but
never across it, a huge value is the old flat average. Measured on st060 over twelve
frames, counting dark one-pixel troughs (a pixel darker than both its neighbours by
0.06, which is what a fringe is):

| variant | dark one-pixel troughs |
|---|---|
| across, the old flat average | 0.771% of the frame |
| **along the edge** | **0.609%** |
| off entirely | 0.791% |

Turning it off is the *worst* of the three, because raw aliasing makes its own
one-pixel troughs; the depth-aware blur removes the fringe and keeps the
anti-aliasing. The overall ladder does not move (median 0.608 either way, p98 0.961,
spread 0.186 -> 0.179), so this touches the edges and nothing else.

### PES's own cel-shade shells (art, not an effect)

An outline in PES is the mesh again, pushed out along its normals a few centimetres
— about 4 cm on Planet Namek — and drawn with its *front* faces culled, so only the
far side survives and it reads as a line around the silhouette. The engine culls
back faces, so `stadium_to_gf.is_outline_pass` spots the pass by its texture
(`outline.png`, matched on whole words: `outlined_turf` is artwork) and writes those
meshes with reversed winding. Left as they were, the shells simply covered the
meshes they were meant to outline, which is why every imported ground first looked
flat and grey.

Only two of the nine grounds use it: **st002 (61 meshes)** and **st017 (12)** —
Planet Namek's rocks. It is the pack authors' art and under the project's
favour-PES rule it stays.

### Not outlines at all

st019, st011 and st060 render their stands near-white (st019 p90 0.91) while the
atlas behind them is dark navy metal (`sourceimagesAtlas_77045.png`, mean 0.2-0.4).
Only the darkest texels survive, and they read as harsh linework. This is the
over-lighting on those grounds, and it is what makes the other two look bad: a thin
dark line on a blown-white surface. See #66.

## 3. Geometry that lands in the wrong place

### PES's pitch is smaller than this engine's

PES: 105 x 68 m, half 52.5 x 34. This engine (`gametypes.hpp`): 110 x 72, half
55 x 36. Anything authored around PES's pitch therefore lands 2.5 m too far in at
each goal and 2 m too far in at each touchline.

It surfaced on the advertising ring. Its boards stand **4.17 m behind PES's goal
line and 0.28 m outside its touchline** — a real setback. Dropped on the longer
pitch that becomes 1.67 m behind the goal line, and the engine's own goal net is
**2.55 m deep** (`goals.ase` reaches x ±57.55), so the hoardings ran straight
through the netting.

`stadium_to_gf.py --pitch-scale` carries the geometry over by the ratio of the two
pitches (x 1.0476, y 1.0588, height untouched), which keeps the author's placement
*relative to the pitch* instead of nudging boards by hand:

| | before | after |
|---|---|---|
| goal-end plane | x ±56.67 | x ±59.37 |
| clearance behind the net | −0.88 m (through it) | +1.82 m |
| setback behind the goal line | 1.67 m | 4.37 m |

### Flat things lying face-down

The engine derives a mesh's lighting from the winding that was written
(`ase_util.write_mesh_normals` computes normals geometrically), and PES's
centre-circle banner is wound the other way: its four flag faces average **−0.99 in
z**. PES does not mind, because it draws them from its own material; here the
printed side was lit as though it faced away from the sky and the banner came out a
black disc on the grass, competition emblem and all.

`stadium_staff.wants_winding_flipped` turns over a mesh that is flat, lying on the
ground and facing away from it — judged by area, so a wall keeps the orientation
its author gave it and a solid is never touched.

## 4. Textures that are not what their name says

### `sys_zero_bsm` is not a zero

It is a slot PES fills at run time, and **every model ships a different picture
under that one filename**:

| model | its `sys_zero_bsm` |
|---|---|
| `doh_fb_home` (walkout flag bearer) | the flag of the United States |
| `tunnelarch_uefa_euro` | the FC Barcelona crest |
| `mob_prop_teamflag_home01` (stand flags) | the FC Barcelona crest |

Imported verbatim, every converted ground's crowd waved Barcelona. And because the
importer keys textures by bare filename, one `sys_zero_bsm.png` per stadium was
shared between all four models, so whichever converted last silently decided what
the whole ground carried.

`stadium_crowd.is_placeholder_texture` spots PES's slots (`sys_zero`, `dummy`) and
the material is pointed at the engine's own neutral cloth instead — repo art, named
`teamflag_home.png` or `teamflag_away.png` for the side it belongs to.
`Match::PaintTeamFlags` paints the playing team's badge over it as the crowd loads,
which is what PES does with the slot too; a team with no badge keeps the plain cloth
rather than flying somebody else's crest. Measured on st041: 18 flags painted.

### The ftex header, and everything it sheared

`ftex.parse` reads **width before height**. Read the other way round it is
invisible on a square texture — most of PES's are — and shears every other one.
That one bug corrupted the pitch art, the ad boards, the crowd palette, the nets,
the pitch detail maps and PES's own colour-grading strip.

It has a long tail: the 4cc hoarding set was extracted at 512 x 1024 the day
*before* the fix and never redone, so nine sheared boards sat in the randomiser's
pool for weeks, and the blue-on-white smear with red edges is exactly what a
striped flag looks like at a distance. Re-extracted, they are 1024 x 512 and clean.
**When a reader is fixed, re-run everything it ever produced** — the file dates are
the only record of what came out of the broken version.

### The advertising pool holds 4cc art only

496 faces became 160: the 336 named `<set>__<name>.png` were PES's own sponsor sets
(`acl_pes`, `lg_denmark`, `ligue1`, `eredivisie`, ...) and are not wanted. What
remains is the 16 tracked community boards plus 144 from the 4cc pack's own
`bill/4cc` set. The ring's board faces carry `ad_placeholder.jpg` so
`Match::RandomizeAdboards` swaps a panel from the pool onto each, the same
mechanism PES uses through `bill_anime.json`.

## 5. Capturing and measuring

**The recorder used to write alpha 0.** `postprocess.frag` ended with
`stdout = vec4(fragColor, 0)`. Nothing composites a presented frame so it never
showed on screen, but the frame recorder hands that buffer straight out as rgba,
ffmpeg carries the zero into the PNG, and every captured still was invisible in any
viewer that honours alpha — while looking perfectly fine in anything that drops it.
It now writes alpha 1, the capture scripts force `-pix_fmt rgb24`, and `opaque.sh`
flattens a set that was taken before the fix.

That is the same mistake as reading medians: **check the artifact, not a
transformed copy of it.** `PIL.convert('L')` and `convert('RGB')` both discard
alpha, so every check ran clean over images that could not be seen.

Recipe, and the traps in it:

- `gamescope --backend headless -W 1280 -H 720` with the engine's own
  `frame_recording_path` fifo into ffmpeg. Do not grab the window: under the
  headless backend the X pixmap goes stale and a grab shows a frame from seconds
  ago.
- Every capture script starts with `pkill -9 -f gamescope`, so **nothing else that
  spawns gamescope may run at the same time**. Doing that during a shutdown-abort
  loop killed five of its eight runs, and the loop correctly refused to call them
  clean.
- `pkill -f` patterns match the harness's own command line. Kill by PID.
- Output paths must be absolute: the scripts `cd` into `data/`, so a relative
  `--out` puts the stills somewhere other than where the final count looks.
- xkbcommon prints a screenful of `Could not resolve keysym XF86...` at every
  gamescope start. It is keyboard-layout noise and the error filter excludes it;
  left in, a healthy run reads like a broken one.

## 6. The replay wipe

The 4cc mod ships its own replay transition in `4cc_20_swipe.cpk` (PES19's download
folder), and it is not a shader or a mask - it is a movie:

```
movie/fade/ACLwipe_hd.usm      1280 x 720, 145 frames at 60 fps (2.42 s)
movie/fade/WEPESwipe_hd.usm    the same, the default slot
movie/fade/settings.json
```

CRI USM containers, and ffmpeg reads them straight - the mod re-encoded them, so
there is no key to find. The picture is the /vg/ Football League badge, a purple and
silver ring crest, spinning in on black, holding, then spinning out. The first frame
and the last third are pure black; about 59% of the run carries picture. Being
opaque rather than an alpha mask, it is played **over** the cut: the wipe covers the
screen, the camera changes underneath it, the wipe leaves.

`settings.json` carries both the mapping and the timing:

```json
{ "id": -1, "file": "WEPESwipe_hd.usm", "fade": 0, "fadestart": 6 }
{ "id":  7, "file": "ACLwipe_hd.usm",   "fade": 0, "fadestart": 8 }
```

`id` is the competition slot (7 is the ACL one, −1 the default) and `fadestart` is
the frame on which the picture underneath is switched. Both slots hold the same
crest in this mod.

`tools/pes21_import/import_wipe.py` merges the two streams into RGBA PNGs, drops the
transparent tail and writes the timing beside them. Two notes on doing that with
ffmpeg: the file must be opened **twice**, because a filtergraph addresses its inputs
by file index rather than by stream (`[0:v:0][1:v:1]alphamerge`), and the frames are
scaled to 640 wide by default — 92 frames of 1280x720 RGBA is 320 MB of texture for a
second and a half of transition, and the crest is large and moving while it plays.

`ReplayWipe` (`src/onthepitch/replaywipe.hpp`) is the timing alone: which frame is on
screen this far in, and whether the cut is due yet. It is asked as "is the cut due"
rather than "is this the cut frame" so a dropped frame cannot lose the switch, and a
build with no wipe imported parses nothing, draws nothing and cuts immediately. The
replay page plays it in on the way to the replay and out on the way back, holding
itself open until the cut is covered so the return to live play happens behind the
crest rather than in the clear.

### PES's blur masks

While looking for what hazed st031: PES splits one piece of geometry into several
material passes, and one of them exists only to keep the mesh out of the motion blur
— technique `fox3DDF_Blin_Fuzzblock`, material `"<name> antiblur"`. Nothing to look
at, and imported anyway it is the same mesh written twice in the same place. st031
carried 22,323 wasted vertices that way, st060 29,704, and st011, st043 and st056
their own share; sixteen masks dropped across the five.

It is **not** the cel-shade outline, and the difference is worth keeping straight: an
outline carries a texture named `outline.png` and sits pushed out along its normals,
while these sit at exactly zero offset with the same base texture. Measured on
st031's pairs: mean offset 0.0000 m over 11,019 vertices.

That was not the haze, though. st031 measures p98 0.694 with the grade on **and**
off, so nothing in the chain is capping it — the ground is pale marble and a
light-blue tiled pitch, and it has no bright values in that framing to lose. Its
textures do carry contrast (`face.png` sd 0.415) but the statue's UVs land on the
atlas's pale stone. Comparing a marble temple against a grass stadium's ladder is
the part that was misleading.

## 7. Still open

- **#66** st031's wide beat is the one ground still flat: p98 0.66 and sd 0.10
  against the reference's 0.92 and 0.13, while its own close beat is fine at 0.96
  and 0.29. So it is that framing, not the ground's lighting — suspect the large
  translucent sculpture filling the upper half, or a scene mesh imported as a sky
  shell that should not be one.
- **#67** the hoardings render mirrored ("CANAL+" backwards). The 234 imported
  faces are clean and correctly oriented and the boards are not part of any
  converted stadium's scene, so it is the ring's own UVs or winding.
- **#68** decide the edge pass, above.
- **#54** the grade still costs already-bright grounds a little (st019 0.15 ->
  0.27, st031 0.21 -> 0.37). A per-ground band chosen from the pack's own
  atmosphere would fix that properly instead of one key for every ground.
