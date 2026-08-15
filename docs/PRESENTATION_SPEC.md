# VGL 26 Day 2 — Match Presentation Flow Spec

Source: `https://implying.fun/videos/VGL%2026/VGL%2026%20Day%202.mp4` (~16GB, ~5h, HTTP range-seekable).
All frame PNGs referenced below live in this directory:
`/tmp/claude-1000/-home-z-Code-GameplayFootball/378dca8a-d4f4-458e-b9b8-a35063a0e26c/scratchpad/vgl2/`
Timestamps are seconds into the source file unless noted "game clock" (the in-match clock, which runs much faster than real time — see note below).

## 0. What this stream actually is (context that matters for reading this spec)

This is **not a raw broadcast of a commercial football game** — it is a fan-run "/vg/ Football League" (VGL) built on what is very clearly a **PES/eFootball-generation engine** (confirmed by menu chrome: "Team Sheet/Edit Position", "Game Plan", "Advanced Instructions", the Full-Time "Attacking Areas" screen, the Highlights/Individual Match Records/Rematch/Select Team/Top Menu bar — these are stock PES/eFootball UI, not custom-built). The league has re-skinned almost everything:
- Player models replaced with anime/meme avatar cutouts and mascots (jersey numbers/positions are legible, character models are not real footballers).
- Pitches/stadiums replaced with joke venues (an industrial warehouse yard, a pool-hall/bar lounge).
- A scrolling meme sponsor-ad ticker, floating "damage number" pop-ups (donation/bit-war gag), and custom loading-screen jokes are layered on top by the organizers.

I separate **(A) reusable engine-level presentation structure** (loading screen → walkout → formation graphic → kickoff → replay-wipe → goal/foul cutscenes → half/full-time stat screens) from **(B) VGL-specific joke skin** (meme ticker, avatar models, "Now Rigging" banner, damage-number pop-ups). Only (A) is likely relevant to replicate; (B) is noted for completeness but flagged as skin, not structure.

**Timing caveat:** the in-match game clock runs far faster than real video time (e.g. kickoff at video t≈3301s shows game-clock 0:00, but by video t≈3400 the clock already reads 17:20 — roughly 8–9 in-game "minutes" per real second). All shot-length estimates below are in **real video seconds**, since that's what a broadcast-presentation clone needs.

---

## 1. Pre-match sequence, shot by shot

Captured in full from the /tf2g/ vs /trick/ match, video t≈3160–3305. Frames: `sheet_1.png`…`sheet_6.png` (clip1/clip2, 0.5s-spaced contact sheets), `confirm_menu_hd.png`, `loading_tips_hd.png`, `stadium_reveal_hd.png`, `formation_graphic_hd.png`.

| # | Video t (approx) | Duration | Shot |
|---|---|---|---|
| 1 | ≤3160.0–3168.5 | ≥8.5s (menu-gated, not timed) | **Kick-off confirmation menu.** Two starting-XI columns under team crests and a "V", captain marked "(C)", GK marked "(GK)". Bottom action bar: `Strip / Stadium / Kick Off / >Game Plan / General Rigging`. Full frame: `confirm_menu_hd.png`. |
| 2 | 3169.0–3169.5 | ~0.5–1s (tail only captured) | **Title/logo loading card**: "VG LEAGUE 26 / 2026" wordmark centered over the static plaza background. |
| 3 | 3170.0–3184.5 | ~14.5s+ (load-time dependent, not fixed) | **"Game Tips" loading screen.** Dark header bar "Game Tips" bold centered; sub-label "Quote of the Day" (rotates to other categories, e.g. "Daily Reminder" was also observed) top-left of a translucent dark-grey text panel; 5–7 lines of body copy below. Static background: rain-slicked stone plaza, string lights, ivy-covered building facade with arched awnings either side, faint centered watermark medallion ("EST. 2013 / RETURN TO THE FUTURE / /vg/ Football League"). Small controller/spinner icon bottom-left. Full frame: `loading_tips_hd.png`. |
| 4 | ~3185.0 | 1 frame (<0.2s) | **Black flash** — hard cut, no cross-fade, between loading screen and stadium. |
| 5 | 3185.5–3189.5 | ~4.5s | **Stadium establishing shot**: static, low, pitch-level camera on a corner flag (team-crest flag, orange/black/purple team colours) with red corner-arc/touchline paint, industrial brick warehouse building behind, chain-link perimeter fence with barbed-wire top strand, ad-hoarding barrier, string lamp, blue daytime sky. Full frame: `stadium_reveal_hd.png`. |
| 6 | 3190.0–3199.5 | ~9.5s | **VGL-specific montage/whip-pan** (flagged as skin, not structure): a large avatar face fills frame, blended via motion-blur/mirror-flip streaks with overlapping semi-transparent player photo cutouts and joke placards. Functions as a hype/sponsor-shoutout reel, not a standard team-reveal graphic. |
| 7 | 3200.0–3204.5 | ~4.5s | Second **stadium establishing shot** from a different angle (loading-dock doors, stone wall, tiered spectator bleachers with crowd sprites visible, ad-hoarding ticker). |
| 8 | ~3205–3230 | ~25s | **Team 1 walkout / lineup shot.** Cut to players standing in a line on the pitch, feet on the centre-circle logo. Camera pans slowly along the row (lateral dolly/pan, not a fixed wide shot), holding on individual players for a couple of seconds each; VGL layers joke name-tag/"critical hit" pop-ups over some players (skin, ignore). |
| 9 | ~3230–3260 | ~30s | **Team 2 walkout / lineup shot**, same treatment: pan along their line-up in front of their own team banner, closing in on individual players. |
| 10 | ~3261.5–3270 | ~8.5s | **Team 1 "TV-style" formation graphic** (see §1.1 below for exact layout). Frame: `formation_graphic_hd.png`. |
| 11 | ~3271–3275.5 | ~4.5s | Cut to a **wide static pitch shot** (very high aerial angle — this is the same camera used for live play, see §5) with tiny distant players, no overlay. |
| 12 | ~3275.5–3283 | ~7.5s | **Team 2 formation graphic**, identical template, own team's crest/tag/colours. |
| 13 | ~3283–3286 | ~3s | Graphic **cross-fades out** (dissolve, not a hard cut) back to the wide pitch shot. |
| 14 | ~3286–3299 | ~13s | Wide pitch hold, then a few **individual player close-ups** (side-profile, pitch-level), then a **referee close-up**: mascot referee performing a whistle-to-mouth gesture. |
| 15 | ~3301+ | — | **Kickoff.** Scoreboard bug appears first as a full-width bottom bar with team crests + "0 0", then contracts within a couple of seconds into the small persistent top-left scoreboard used throughout play (see §4). |

### 1.1 Formation graphic — exact layout (frame: `formation_graphic_hd.png`)

- A dark-navy-to-blue gradient panel, **semi-transparent** (the stadium/pitch behind it is dimly visible at the very edges of frame — this is an alpha-blended overlay composited directly on the live 3-D scene, not an opaque full-screen graphic), thin glowing light-blue border along the top edge.
- **Header bar**: team tag (e.g. `/tf2g/`) centered, bold white, on a brighter blue gradient strip; a thin white horizontal rule separates header from body.
- **Body — pitch schematic**, portrait orientation, goal at the bottom:
  - Each starting player is a light-blue **jersey-silhouette icon** containing the squad number in bold white, arranged in actual formation shape (back line, mid line(s), forward line) with faint connecting lines suggesting the tactical shape.
  - Player nickname in small white caps directly under each jersey icon.
  - GK sits inside a small goal-box outline at the very bottom.
  - A striker/lone-forward sits inside a semicircular "forward zone" arc near the top.
- **Substitutes list**, left-hand column: header "Substitutes" in bold cyan/teal, then a plain numbered list (e.g. 12–23) of squad number + nickname, left-aligned, evenly spaced, white text, no icons.
- No animation-in captured beyond the cross-fade at the end; graphic likely fades/slides in symmetrically to how it fades out (not confirmed — flag as uncertain).

---

## 2. Transitions — the "replay wipe"

**Confirmed**: the user's hypothesis is correct — there is a dedicated stinger/wipe used specifically to cut **into and out of instant replays** (both goals and fouls used the identical asset). It is not a simple alpha-wipe shape; it's a two-stage effect. Sampled at 0.2s resolution across a live example (goal replay in/out, video t≈3398–3403): frames `wipesheet_1.png`, `wipesheet_2.png`; second example (foul replay) in `foulseq.png`.

**Stage A — Team badge "power-up" bumper** (~0.6–2s):
- The circular **VGL medallion logo** ("EST. 2013" arched top text, hexagonal "VG" monogram center, "/vg/ Football League" arched bottom text, ring of small stars) assembles/glows in from a blurred whip-pan of the preceding shot — the incoming frame appears to whip-pan/motion-blur with a brief **horizontal mirror-flip** artifact on any HUD text caught mid-transition (numbers appeared mirror-flipped for 1–2 frames, e.g. "780" instead of "087" — likely a shader/UV artifact of the wipe geometry rather than an intentional design element, but reproduce carefully).
- Background behind the medallion is a radial **deep purple/violet glow**, medallion rendered in matching purple/silver/white.
- Medallion holds roughly static for under a second.

**Stage B — Lightning/energy wipe** (~0.6–0.8s):
- A bright **orange/gold glowing orb** blooms in from one side of frame.
- A jagged **purple/violet lightning-bolt** streak (like a crack of electricity) tears diagonally across the frame (bottom-left toward upper-right in the sampled instance), acting as the wipe's leading edge.
- The destination shot (the replay footage) is revealed progressively behind/through the lightning streak — i.e. the streak's path *is* the wipe boundary, not a separate mask shape.
- Once the streak clears frame, the destination shot is fully visible with no residual overlay; total observed time from "medallion first fully legible" to "destination shot clean" ≈ **1.4–1.8s**.
- The same bumper (medallion → lightning wipe) is used again, in the same form, to cut **back out of the replay to live play**.
- Branding: yes — the team/league badge rides the wipe explicitly (this is the league medallion, not a per-team crest, in both observed instances — used as a generic "instant replay" stinger rather than a scorer's team badge).

Overall recommendation for a clone: budget ~2.5–3s total for entry (whip-blur → badge assemble → lightning wipe → replay begins) and a faster ~1.5–2s for the return wipe (badge → lightning → live play), matching what was observed.

---

## 3. Goal / foul / substitution cutscenes

### 3.1 Goal (video t≈3378–3396, frames `goalsheet_1.png`…`goalsheet_4.png`)

1. **t≈3378.0–3380.0** (~2s): live play, high aerial camera, ball moves into the box.
2. **Hard cut, no wipe** to a **low static replay camera** planted just behind/beside the goal frame at ground level (crowd stand visible behind the net) — held ~2.5s. This is a different, dedicated camera position from both the live top-down cam and the fence-view replay cam used later; no slow-motion was conclusively distinguishable from stills.
3. **"Today's Goals" banner** fades in top-left, directly under the scoreboard: label "Today's Goals", running tally (e.g. "1"), scorer's name, and a small square player photo/portrait thumbnail. Banner persists through the following celebration shot.
4. **Celebration shot** (~5s): cut to players clustered around a large flat prop object (a rounded-rectangle "shield/crest" shape — team colour) that they hug/climb on; camera holds/slowly pushes in. (VGL replaces the usual player-pile-on with this crest prop — likely a specific celebration animation target rather than universal engine behaviour; flag as possibly skin-specific.)
5. **"Spotlight" shot** (~2–3s): cut to a single player, chest-up, backed by a **radial glow vignette** (observed in warm red) — reads as an individual "goal hero" callout distinct from the group celebration.
6. Brief return to **live gameplay** (~3–4s) at normal aerial camera, then — for this particular goal — the engine additionally cut via the **replay wipe** (§2) into a **proper instant replay**: first a wide **fence-view** angle (camera positioned outside the pitch perimeter, chain-link fence prominent in foreground, watermark logo top-right, small "Replay Control" label bottom-left confirming this is a scrubbable replay UI, not just a cutscene), then a closer **goalmouth** angle (behind-the-net, showing keeper/defenders reacting). Replay wipe out, back to live play at the restart (centre circle).

Not confirmed with certainty: whether the low ground-level "goal frame" shot (step 2) is always shown, or whether the fuller fence+goalmouth replay (step 6) only fires when a stream operator/production chooses to show it — the two goals sampled didn't behave identically in this respect (say so explicitly: **uncertain**).

### 3.2 Foul (video t≈3660–3684, frames in `foulseq.png`)

1. Live play, a player goes down (small red mark appears at the point of contact).
2. **Replay wipe in** (medallion → lightning, §2).
3. **Replay**, ground-level fence-view camera: referee (mascot) jogs to the incident and performs a **thumbs-up gesture** next to the fallen player — read as an "advantage/play-on" signal, not a card.
4. Camera **pushes into an extreme close-up** on the referee's head/face and holds for several seconds (~5–6s) — a dramatic beat with no additional UI.
5. **Replay wipe out** (medallion → lightning, §2), back to live play, "Possession 51%/49%" stat pop-in briefly visible on return (see §4).

**No yellow/red card graphic was captured** in the samples reviewed. I looked specifically for one (distinctive card-shaped icon expected next to the referee) across several additional scan windows and did not find a carded incident in the portions of the video sampled — this is a genuine gap, not a confirmed "no cards exist" finding. If a card cutscene exists, expect it to reuse the same replay-wipe bracket with a card icon added to the referee close-up; this should be verified against more footage before treating its absence as fact.

### 3.3 Substitution

**No dedicated walk-off/walk-on cutscene was found.** Substitutions appear to be handled entirely through the same **"Team Sheet/Edit Position"** overlay used pre-match (§1), invoked as an in-play pause menu — confirmed because this exact screen was captured mid-match with the live pitch visibly blurred/dimmed behind it (frame in `goalscan_4200.png`) and a live "Subs 3/3" counter per team. The screen: two side-by-side panels (one per team) titled `>Game Plan` or `Team Sheet/Edit Position`, pitch-schematic formation with jersey icons, editable via `Help / Confirm / Return / LT / RT: Switch Player Icons / RS: Variant Formation` control hints. No on-pitch "IN/OUT" arrow graphic, no player walking to the touchline/bench animation, and no broadcast-style "substitution board" was observed. Treat this as reasonably solid (checked across two different matches/screens) but note I did not specifically watch a sub being applied frame-by-frame at the moment of confirmation, so any brief flash-graphic at the instant of swap could have been missed.

### 3.4 Half-time / Full-time stat screens (frames: `halftime_hd.png`, `coarse_2400.png`, `coarse_2500.png`)

Identical template for both, titled "Half Time" or "Full Time":
- Big score (e.g. "0" / "2") either side of a centered league medallion logo and clock (Full Time also shows "90:00").
- Team crest + tag under each score, plus (Half Time only) a "Man of the match"-style mascot banner directly under the winning/leading side's crest (small ribbon graphic with a nickname).
- Center **stat table**, two columns either side of the stat label: `Possession | Shots (On Target) | Fouls (Offside) | Corner Kicks | Free Kicks | Passes (Successful) | Crosses | Interceptions | Tackles | Saves`.
- Full Time additionally offers a second page: "Attacking Areas" heat-strip diagram (pitch divided into thirds, percentage in each third per team) — toggled via `LB Toggle Display`.
- Full Time bottom action bar: `Highlights / Individual Match Records / Rematch / Select Team / Top Menu` (stock PES/eFootball post-match menu).
- Background in both cases is the current match's 3-D stadium/venue (blurred/dimmed), confirming these are in-engine overlays, not separate menu scenes.

---

## 4. HUD details

Frame: `hud_detail.png`, `tactic_banner_hd.png`.

- **Scoreboard** (top-left, persistent): small league-crest icon + running match clock, then a blue bar with team tag / score / team tag (e.g. "TF2  0  0  TRI"), small team-colour swatch chips flanking the score.
- **Sponsor ticker**: a thin horizontally-scrolling strip of small ad panels directly under the scoreboard, running the full width of the screen — VGL-skin joke sponsors, but the *mechanism* (scrolling ad ticker under the scoreboard) reads as a real broadcast convention worth keeping.
- **Periodic stat pop-in**: every so often during live play, a small blue box replaces/extends the scoreboard for a few seconds showing a single rotating stat pair, e.g. "93% / Passes Completed (%) / 93%" or "51% / Possession / 49%", then disappears back to the plain scoreboard. This is a standard PES/eFootball feature (not VGL-specific).
- **Radar/minimap**: **none observed, at any point sampled.** This is consistent with the camera choice (see §5) — the live-play camera is zoomed out enough to show most of the pitch at once, which likely makes a minimap redundant in this camera mode. Not verified whether a minimap exists in other camera modes (e.g. a closer, ball-following broadcast cam) since that mode was never used in the footage sampled.
- **Tactics/instruction indicator**: a dark semi-transparent rounded-rectangle banner appears bottom-left or bottom-right, near the touchline, showing the **name of the team's currently-active tactical instruction** in bold (e.g. "Anchoring", "False Winger", "False Full Backs", "Centring Targets") with the associated player's nickname in smaller gold/orange text beneath. These fire briefly (a few seconds) at moments tied to phase-of-play (e.g. right after kickoff, presumably when the relevant attack/defence instruction becomes "active"), then fade. This is the one place team-tactical info surfaces in-HUD during live play; there is no separate persistent tactics icon/arrow on the pitch itself.
- **Player nameplates**: two layers —
  1. A small floating white nickname label directly above the head of the ball-carrier / nearby players in 3-D space (in-world, billboard text).
  2. A **persistent HUD plate**, one bottom-left (team A) and one bottom-right (team B), each showing: jersey number, player nickname, a horizontal green condition/stamina bar, a controller icon (present specifically for the human-controlled player), and a small circular team badge.
- **Referee**: rendered as a normal on-pitch model (mascot-skinned in this league), visible in live play near the box/touchline; only becomes a "camera subject" during foul replays.
- **Watermark**: a small league-medallion + ">Fun" wordmark logo sits fixed top-right of frame throughout — read as the stream production's bug, not part of the game engine itself.

---

## 5. Camera angle in normal play

Frames: `hud_detail.png`, `chk_3580.png`, and the wide shots throughout `foulseq.png`/`fscansheet.png`.

- **Height/tilt**: very high and steep — I'd estimate roughly **75–85° downward tilt from horizontal** (close to top-down but not perfectly vertical; the far end of the pitch recedes to a vanishing point near the top of frame, so it is *not* a pure orthographic top-down).
- **Distance/zoom**: extremely wide — roughly **60–70% of the pitch length** is visible in frame at once (from about one penalty box to just past the halfway line), with the far penalty box visible in the distance at reduced scale. Zoom level did not appear to change between open play, near-goal action, or corners in any of the samples — no dynamic zoom-in/out was detected.
- **Tracking**: the camera does reposition to keep the ball's general area roughly centred/ahead as play moves the length of the pitch (compare `hud_detail.png`, ball near centre circle, vs `stats_3530.png`, ball near the box — the goal and box are framed close to the camera in the latter), but there is **no tight ball-lock, no lead/lag pan on fast breaks, and no dynamic FOV/zoom push** the way a typical broadcast camera behaves. It reads as a fixed high "tactical/radar" camera that slides along the pitch's long axis rather than a broadcast camera that frames and re-frames the ball dynamically.
- **Lateral framing**: camera stays centred on the pitch width; no side-to-side swing observed.
- This aerial camera is used for **100% of live open play** in every match sampled — the lower, more traditional broadcast-style angles (ground-level, behind-goal, fence-view) are reserved exclusively for **replays** (goals/fouls) and the **pre-match walkout**, never for ongoing open play.

---

## 6. Stadiums

Two distinct venues were directly observed (one per match sampled); there may be more elsewhere in the ~5h stream, not checked.

1. **"Industrial yard" pitch** — /tf2g/ vs /trick/ match. Frame: `stadium_reveal_hd.png`.
   - Open-air, daytime, clear blue sky.
   - Perimeter: chain-link fence topped with barbed wire, small tiered spectator bleachers with visible crowd sprites, wooden ad-hoarding barrier along the touchline, telephone poles, a couple of tall thin cypress-like trees.
   - Backdrop building: large brick/wood industrial structure with loading-dock garage doors, a hanging tire, security camera prop, corrugated roof sections — reads as an old warehouse/factory yard, not a stadium bowl.
   - Pitch surface: **wood-plank flooring texture** (not grass), boundary/box/arc lines painted in **red** rather than white.
   - Time of day: daytime throughout the match (no lighting change observed).

2. **"Bar/pool-hall lounge"** — /vrg/ vs /skg/ match. Frames: `coarse_1200.png`, `coarse_2100.png`, `coarse_2400.png`, `coarse_2500.png`.
   - Reads as an indoor lounge/bar, not an outdoor stadium: post/full-time menu backgrounds show wood-beamed ceiling, a wall covered in club/team badge patches, oversized billiard/pool balls flanking the frame, warm indoor lighting.
   - During live play the pitch itself is rendered as **two solid-colour halves** (one team's colour each, e.g. red vs dark blue) rather than a textured surface, with a large "Bawdy Bar Lounge VKR"-style logo painted across the centre circle.
   - No crowd/stands visible during live play (the colour-block pitch fills frame edge-to-edge); crowd/stand imagery only appears in the half-time/full-time menu backdrops.

Both venues share the same UI chrome (scoreboard, ticker, stat screens), confirming venue is a skin layer independent of the presentation logic.

---

## 7. Key uncertainties flagged for follow-up

- Card (yellow/red) foul cutscene not located in the samples reviewed — needs a wider search of the ~5h video.
- Exact hold time of the confirm/kickoff menu and the "Game Tips" loading screen are load-dependent, not fixed durations — don't hard-code the observed numbers as authoritative.
- Whether the formation graphic animates in (vs. just cutting in) was not observed — only the cross-fade *out* was captured.
- Whether the fuller fence-view + goalmouth replay always follows every goal, or is a production choice, is unconfirmed (one goal showed it, the general pattern is inferred from two data points).
- Substitution confirmed to use the tactics/pause menu, but the exact instant-of-swap moment (any flash/graphic) was not directly observed.
