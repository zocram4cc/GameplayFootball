# Reference: VG League 26, Day 7 — /dbg/ vs /hbr/ at Planet Namek

What PES 2021 actually does, from kickoff back through the whole pre-match
sequence, used as the target for the cutscene camerawork, the stadium imports and
the in-play HUD.

Nothing from the broadcast is committed here. The source is

    https://implying.fun/videos/VGL 26/VGL 26 Day 7.mp4    (15.4 GB, byte ranges OK)

and the window below starts at **2:47:35**. Pull it with

    ffmpeg -ss 02:47:35 -i "<url>" -t 165 -c copy clip.mp4

Times below are seconds into that clip, so `s030` is 2:48:05 in the broadcast.
Frames at 1 fps are enough to read the cut structure; the camera moves need
denser sampling.

## Shot list, kickoff at s148

| s | shot |
|---|---|
| 001–017 | pre-match selector screen: both squads' talents listed either side, the two team badges over `v`, and a bottom row of buttons — **Strip / Stadium / Kick Off / Game Plan / General Rigging / Camera S…**. The players' models stand either side, idling. |
| 018–026 | load screen: "Game Tips", the VG LEAGUE 26 crest, a `Manager Tips` line |
| 027–029 | corner flag, camera low and near the flag, slow pan across the vista |
| 030 | cut to a **high wide** behind the goal, looking down and across the whole pitch |
| 031–034 | the same aerial drifting, with the **versus banner** over it: both badges, `/dbg/` and `/hbr/`, captioned `/dbg/ - Planet Namek` |
| 035 | banner gone, aerial continues over the centre circle and the three badge mats |
| 036–044 | **the tunnel**: interior corridor, both squads lined up, camera in among them at chest height moving forward with them |
| 045–048 | out into the light — camera behind the players as they walk onto the pitch |
| 049–054 | stadium detail pans: the Namek pod with the `AIR ERUSEA` board, then low along the ad boards past the blue trees |
| 055–107 | **the long lineup dolly**: a slow lateral track at roughly 1.5 m, 1–3 m out, past the standing players and the pitch-side characters |
| 105–110 | the **team photo** — squad in two rows facing camera |
| 110–118 | crossfade to a low wide of the pitch, then the `/dbg/` **lineup panel** |
| 119–122 | **pitch-level wide** from behind a goal, looking down the length; camera then tilts up to the horizon and the moon |
| 123–134 | the `/hbr/` lineup panel, over the same stadium slowly moving behind it |
| 135–137 | corner flag against the green sky, both moons visible |
| 138–140 | goal mouth **through the net**, keeper in frame, shallow depth of field |
| 141–145 | pitch-side character close-up, then an extreme close-up past a player's head with the ad boards behind |
| 146–147 | a player standing over the ball, ad boards behind |
| **148** | **kickoff** — crossfade to the match camera |

Two things to take from this beyond the shot order:

* the walkout is a real tunnel-to-pitch traversal — the actors move, and the
  camera moves *with* them, rather than the actors standing while a camera
  orbits;
* the stadium gets shown off deliberately, in five separate short shots, before
  any football happens.

## Planet Namek, as it should look

From the aerial (s030) and the pitch-level wide (s119):

* **sky** — a strong gradient, green overhead into yellow at the horizon, white
  cumulus clouds. One large grey moon high in the frame from the pitch-level
  view; a second is visible in the corner-flag shots.
* **pitch** — cyan/blue turf, white lines, a **checkerboard mow pattern** from
  above that reads as lengthwise stripes at ground level.
* **turf decal** — a large purple `VG Football League / EST 2013` crest painted
  near the centre.
* **badge mats** — three flat mats laid on the pitch at the centre before kickoff
  (both team badges and the league badge).
* **entrance arch** — a pink/magenta arch on the pitch at the tunnel mouth.
* **ad boards** — an unbroken ring of boards on a low kerb at the pitch edge,
  each panel different: `AIR ERUSEA`, `VG LEAGUE 26`, `GOOD LUCK`,
  `7000 SKELETONS`, `the ASCII helmet spawn`, and more.
* **terrain** — Namek: turquoise ground, flat-topped **mesas whose tops are
  pink/salmon and whose sides are teal**, receding in layers to the horizon,
  with water between them.
* **Namek pods** — white/grey domes with green circular windows, in a cluster of
  three or four beside the pitch and scattered over the hills; one is horned.
* **Capsule Corp** — a large white sphere with a black band, reading `CAPSULE`,
  on top of a flat-topped mesa in the middle distance.
* **trees** — thin poles topped with round dark-blue foliage balls, all around.
* **no stands** — the crowd is characters standing at the pitch side.

A converted stadium that looks "incomplete" should be checked against that list
in order; the boards, the Capsule Corp sphere, the pods, the trees and the turf
decal are all separate meshes and separate textures, and any of them can go
missing on import without the converter complaining.

## The in-play HUD

Bottom corners, mirrored, with the user's team on the left:

* **team badge**, circular, at the outer end, overlapping the plate
* **name plate** — a dark navy rounded translucent plate
* **`32 THE CHAMP!`** — shirt number then name on the left, name then number on
  the right, white bold condensed. The away side is drawn dimmer than the user's.
* **stamina bar** — a thin bright green line along the *top* edge of the plate,
  with a small dark tick at its right end marking the current value
* **attack/defence level** — a small vertical black box with a white horizontal
  band; the band's height is the level, centred meaning balanced
* **philosophy dial** — a circle split into two tones (cyan and pink) by
  proportion, with `<` and `>` chevrons either side for cycling. Greyed out for a
  side the user is not controlling.

Above each plate, a rounded navy **pop-up with a downward tail** announces a
tactical change, one line for the instruction (`Centring Targets`,
`Gegenpress`, `Defensive`) and, when it names someone, a smaller grey second
line under it.

Top-left carries a compact `⏱ 0:27  DBG 0 0 HBR` clock and score; at kickoff
that is replaced for a few seconds by the wide centred scoreboard with both
badges and `/dbg/ 0 0 /hbr/`.
