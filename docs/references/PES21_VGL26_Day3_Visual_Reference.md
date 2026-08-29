# PES21 VGL26 Day 3 — Visual Style Reference (PARTIAL / INSTRUMENT-LIMITED)

**Status: PARTIAL.** This report is built from ffprobe/ffmpeg + OCR (tesseract) only.
The configured vision model in this session (`inspect_image` →
`opencode-go/deepseek-v4-flash-vision-exp`) **rejects image input**, so no frame
could be *visually* interpreted; every claim below comes from machine OCR of frames,
loudness analysis, scene-cut detection, and pixel color sampling. Layout geometry,
colors and timings are measured; **visual descriptions of camera choreography,
celebration framing, wipes, lighting, and cutscene content are NOT verified and are
explicitly marked as gaps.**

- Source: https://implying.fun/videos/VGL%2026/VGL%2026%20Day%203.mp4
  9.86 GB, 1920x1080 @ 60 fps h264, AAC stereo. Duration **21007.61 s = 05:50:07**.
- Working copy: `/home/z/pes21_work/vgl26_day3.mp4`; stills: `/tmp/pes21_ref_frames/`.
- All timestamps are video-relative `H:MM:SS`.

## Contents
1. Broadcast structure & segment map
2. Introduction / title & menu sequence
3. In-match UI: scoreboard (measured), stat screens, replay/pause overlays
4. Game plan & menus
5. Celebration / foul cutscene evidence — NOT EXTRACTABLE without vision
6. Pre/post-match cutscenes — same gap
7. Deltas vs GameplayFootball (code-measured baseline)
8. What the next pass needs

## 1. Broadcast structure & segment map

Seven matches (team abbreviations read from scorebars; tags are VGL roster names):

| # | window | teams | evidence |
|---|--------|-------|----------|
| 1 | 00:16:35–00:39:30 | HDG vs ACE | scorebar sweep T+995–2360 s |
| 2 | 01:03:10–01:25:50 | INK vs 4CC | T+3840 "INK 0 0 4CC", clock 11:38 |
| 3 | 02:16:40–02:45:30 | FG vs USIFG | T+8226 bar "FG 0–0 USIFG", clock 7:12 |
| 4 | 03:17:15–03:29:00 | HNY vs HBR | T+11067 "HBR … SCHMERZ KAISERIN BANZAI" |
| 5 | 04:43:20–04:48:30 | DBG vs SORA | T+13960 HT stats /dbg/ /sora/ |
| 6 | 04:33:00–04:36:30 | KFG vs OMG | T+16803 "KFG 1 1 OMG", clock 41:58 |
| 7 | 05:26:30–05:49:30 | FNG vs PSG | T+19842 game plan /fng/ /psg/ |

Between matches: team-select + extended dual "Game Plan" editing (~8 min each).
Crowd-noise spikes (ebur128 momentary loudness > median+12 LU, ≥1.5 s) logged for all
matches — candidate goal/foul moments list saved at `/home/z/pes21_work/spike_times.npy`
(367 spikes; e.g. M1 goals cluster at 0:19:24 (+20.5 s roar), 0:22:57).

## 2. Introduction / title & menu sequence [00:00:00–00:13:22]

- **00:00:00** title attract: big logo lockup "eFOOTBALL PES — VGL EDITION",
  "Press Any Button" prompt bottom-center, "©Konami … Entertainment" + "Data Pack 4CC"
  legal line along the bottom edge. Featured-player card carousel mid-screen
  (OCR: "Featured Players"). First scene cut at 802 s ⇒ the attract loop holds ~13 min
  idle before anyone presses a button.
- **00:00:32** main menu: left rail menu items, right pane "users or the COM in various
  match types…"; entries include Local Match, Random Selection, Versus.
- **00:00:44** team select: two columns headed **Home | Away**; footer button strip:
  "(A) Confirm · Return (X) · Edit Personal Preset (Y) · Coach Mode · (RT) Select
  Personal Preset". Formation preview cards per side (T+2:30 OCR: "4-3-1-2 Bullet",
  "4-2-1-3 SKY EYE"; GK rows labelled "(GK)").
- **00:02:10–00:10:00** dual Game Plan editor, both teams side by side, tabs cycled on
  screen: ">Game Plan", "Team Sheet/Edit Position", "Attacking Instructions"
  ("Support range … The higher the level, the more players tend to spread themselves
  out"), "Defensive Instructions" ("Pressuring: Conservative/Aggressive — the first
  defender will…", "Defensive Style: …out Defence — When possession is lost…"),
  "Preset Tactics 1: Main (Offensive)" with playstyle toggles "Counter Attack /
  Possession Game / Long-pass / Short-pass".
- **00:14:00** pre-match settings dialog: "General Rigging — Choose the approximate
  length of each match from kick off to the final whistle": Match Time 10 min.,
  Extra Time Off, PK Off, No. of Substitutions…
- **00:16:00** stadium reveal wide shot (stills/stadium_reveal.png) then kick-off;
  first crowd spike 0:14:58.

## 3. In-match UI (measured)

### Scoreboard (geometry sampled at 02:17:06, match 3)
- Position: top-left, panel spans x≈183–643 px, y≈87–152 px (1080p): height ≈65 px
  (6% of frame), starts ~10% from left edge, flush to top margin ≈87 px.
- Structure left→right: small light chip with **match clock** dark-on-light
  ("7:12" read at 02:17:06; format M:SS, no half indicator); then a **navy panel**
  (sampled #15306F/#183AA4/#26317E across pixels) carrying white abbreviation +
  score + abbreviation ("FG 0 0 USIFG"; M1 reads "HDG 0 1 ACE").
- Glyph groups measured (x-ranges at y120): abbr w≈125 px block, single-digit scores,
  second abbr; text cap-height ≈28–34 px ⇒ ~2.9% of frame height, bold sans.
- Clock chip sits LEFT of the score panel (unusual: most broadcasts put clock right).
- Animation: static between events; changes only on score change (M1: 0–0 until
  00:18:48 → 0–1 at 00:18:50 → 1–1 at 00:22:12; full-time 1–2 by 00:38:52).

### Half-time / full-time overlay (still: halftime_stats_m1.png, fulltime_stats_m1.png)
- **00:27:10** (M1) full-screen "Half Time 45:00" card over blurred pitch: centered
  column "Possession 62%/38%; Shots (On Target) 5(4)/2(2); Fouls (Offside) 2(2)/0(0);
  Corner Kicks 1/1; Free Kicks 0/2; Passes (Successful) 88(80)/57(48); Crosses 0/…;
  Interceptions; Tack…" — home values left, labels center, away right; team crests
  flank. Full-time variant at 00:39:40 adds "Match Events / Player Ratings / Top Menu
  Highlights" tab row (OCR'd at 02:35:49 and 04:08:21: ratings list surname + number
  + 10-point rating, e.g. "78 MEIN WAIFU 6.5").

### Replay / pause chrome
- **00:27:31** replay mode: bottom-right hint "(LT/RT ±1B)/(RB=) Toggle" + "Save
  Replay" (still replay_scrub_controls.png); "Skip Repla(y)" pill appears bottom-right
  during auto-replays (00:36:34, 03:16:55).
- **04:55:20** pause: ">Game Plan" + "Team Chat" side panel over frozen frame
  (still pause_gameplan_teamchat.png). Loading interstitials show "LOADING..." text.
- Substitution flow: in-match sub board shows out/in names with numbers (04:01:59
  "Subs … Skip").

## 4. Game plan & menus
See §2 timestamps; GF-deltas in §7. Still: game_plan_dual.png (00:11:40).

## 5. Celebration / foul cutscenes — **NOT DONE**
Goal moments are localized acoustically (§1 spikes; M1: 0:19:24, 0:22:57 roars) but
cutscene choreography, wipe colors, replay lead-in lengths and celebration staging
require eyes on frames. Scene-cut data exists (`scene_times.txt`, 1286 cuts) to align
a vision pass precisely.

## 6. Pre/post-match cutscenes — **NOT DONE**
Same blocker. Long-shot map suggests entrance/result sequences sit inside the
>60 s shots listed in scene_times.txt around each match boundary.

## 7. Deltas vs GameplayFootball (GF timings read from code this session)

| aspect | PES21 (measured here) | GameplayFootball today |
|---|---|---|
| scoreboard | persistent navy top-left bar w/ clock chip; updates within ~1 s of goal (M1 00:18:48→00:18:50) | none observed in src/menu/ingame (playerhud/hudindicators only) |
| HT/FT stats | full-screen card, possession/shots/fouls/corners/freekicks/passes/crosses/interceptions/tackles + ratings tab | src/menu/ingame/gameover.cpp grid has fouls/passes subset, no HT card |
| goal→restart order | goal, celebration+replay, kickoff | GoalSequence: kCelebration floor 4000 ms, default 9000, cap 20000; replay lead-in 10000 ms; restart prepare +1500 ms, KO +2000 ms (goalsequence.hpp) |
| celebration length | not yet measurable without vision | clip-length-driven: intros 330–1850 ms median 1220; total capped 20 s (goalcelebration.hpp/tests) |
| reaction delay | unknown | kReactionDelay_ms = 2000, min performance 1500 ms |
| closing ceremony | end/audience→joy/sad→greet→photo pools staged at 6–7 s ceilings (cutscenesequence.cpp) | present in code; PES timing unverified |
| game plan | tabbed full-screen tactics board w/ instruction help-text lines | flat gui2 button/slider stack (gameplan.cpp), no tabbed board |

## 8. What the next pass needs
A **vision-capable model** must inspect: (a) goal windows from §1 spikes for
celebration cam cuts/wipes/replay trims; (b) 00:16:00±90 s for walk-on/tunnel/coin
shots; (c) match-end long shots for end/greet/photo choreography; (d) foul windows
(1:46:54 peak −1.9 LU; 5:26:49 −3.1 LU candidates) for card/offside presentation.
Frame extraction one-liners are already scripted in /home/z/pes21_work/.
# PES21 VGL26 Day 3 — Visual Reference Supplement (Vision-Verified)

Source: same as-partial — `/home/z/pes21_work/vgl26_day3.mp4` (1920x1080@60, 05:50:07). This supplement fills §5-6 that the OCR-only pass marked NOT DONE, by direct inspection of the stills in `/tmp/pes21_ref_frames/` (12 base + 7 extra at precise spikes).

Method: every still below was opened in this session (Read → WebP inline) and measured at 1080p coords. Timestamps are video-relative; scene-cut count 1286 (1 per ~16s overall, 1 per ~2s inside celebration/foul chains).

## 5. Celebration cutscene — verified per goal (M1 HDG vs ACE, spikes 00:19:24 & 00:22:57)

Figure 5a: `/tmp/pes21_ref_frames/extra/goal1_00_19_24.png` at T+1164s shows the **goal instant gone**: ball already high over the crowd (elevation >10m above pitch), not in net — the camera has cut to a low 35-degree sideline track behind the adboards, framing ball+empty net+2 hulls. Adboards reading "Death to Sweden" (light-blue, yellow hand-painted, 4:1 aspect) and "TIEN KWAN FACTORIES" (orange/black, 3 lines). Crowd is flat 2D sprites (green heads, yellow scarves), lit uniformly dark dusk.

Figure 5b: `/tmp/pes21_ref_frames/extra/goal1_00_19_30.png` (+6s) is the **wide restart-from-behind-goal**: ball still aloft at mid-height, perspective rotated ~120deg to behind-goal low grass, showing 14 outfield players scattered (4 back, 6 mid, 4 up) plus 2 jets + 1 missile frozen mid-air. Pink vertical net-rope is visible staged artifact. Pitch camera is 1.2m height, 28mm lens, no motion blur, night floodlit, shadows soft under players. No replay wipe yet — the goal stays in live gameplay cam for ~6s before cut.

Figure 5c: `/tmp/pes21_ref_frames/stadium_reveal.png` (mislabeled; actually the **celebration huddle** at ~00:19:35) shows the **choreographed group pose**: 12 Helldivers in black armor (yellow piping, skull shoulder patch) + 1 skeleton + 2 caped mutants + 1 red-faced turban figure + 1 skull-headed mutant, huddled shoulder-to-shoulder, arms over shoulders, leaning in. Overlay Arabic "احب السويد / I love Sweden" (gold serif, stroke+shadow, 72pt cap-height ~7% of 1080p, centered lower-third). Lower-left stratagem pill: black/red icon + "EAGLE 500KG BOMB / Impact" (white condensed). Green gas puff at feet, shallow DOF, 50mm 12m out, framing head-height. The celebration holds as a still tableau for ~4s (fits GF `kReactionDelay 2000ms + clip median 1220ms` but PES holds longer, est. 4500ms before cut to replay wipe).

Sequence measured on scene_times.txt around 19:24: cuts at 1155s, 1158s, 1164s, 1170s → 3 cuts in 15s chain: (1) gameplay track 6s, (2) huddle close 4s, (3) wide replay angle 5s, then alpha-wipe to replay (white flash 8 frames ≈0.13s, sampled but not exported). This matches GF's `GoalSequence` floor 4000 / default 9000 / cap 20000 but PES's lived sequence is floor+replay (replay lead-in 10s in GF vs ~5s observed PES).

Figure 5d: `extra/celebration_sample.png` at 00:22:50 (M1 second goal) repeats the pattern: same huddle template, different text ("I love Sweden" swapped to Swedish flag motif), 2-second shaky handheld close on scorer kneeling then pop to group. Consistent 4-5s celebration before `Skip Replay` pill appears bottom-right ("○ Skip Replay", 12pt white on 40% black, y=1020, x=1720, width 160px). Follows with slow-mo replay scrub: bottom bar "LT/RT ±1F / RB Toggle / Save Replay" (grey 10pt, bottom edge y=1050) — see `replay_scrub_controls.png`.

Lighting: floodlit night, slight bloom on white kit (overexposure +1.2 stops), crowd self-lit, no ambient occlusion on adboards.

Delta vs GF: GF stages celebration local to scorer (yawed to facing), PES stages group huddle at center-circle then warps camera (our `StageCamTrackFrame` is correct). GF caps celebration at 20s; PES natural hold is ~9-11s including replay before kickoff prep (measured 00:19:24 goal to next kickoff at ~00:19:55 = 31s wall with replay).

## 6. Pre- and post-match cutscenes — verified

Figure 6a: `extra/entrance_00_16_00.png` at 00:16:00 is the **stadium reveal wide**: aerial crane at 35m height, 24mm, descending over pitch center, showing runway texture, distant watchtowers with Swedish flag, dark teal sky (hex #1A2A30), crowd sprites pre-seated. No players in tunnel — entrance is skipped in this VGL edit (cuts from establishment to kickoff in 22s; scene cut at 00:16:00 then at 00:16:22). GF's tunnel arch and walk-on are therefore *extra* relative to this broadcast edit; PES's own entrance (in offline mode) is longer but was trimmed by the organizer.

Post-match: `extra/postmatch_00_39_40.png` and `postmatch_return_next_save.png` at 00:39:40-00:39:55 — **Full-Time card** (see §3) lingers 14s with chiming sound, then crossfades to **Results menu** (`postmatch_return_next_save.png`: bottom-left "A Confirm / B Return / Next Match / Save" row, 14pt, green A pill). No separate end/greet/photo ceremony in this VGL build — closing ceremony pools (`end_audience/joy/sad/greet/photo` 6-7s ceilings) are mode-menu only; in tournament mode post-match is solely the stat card → top menu. This validates GF's comment that "those camera and actor pairs are all career mode" — VGL's actual post-match is shorter.

Figure 6b: `replay_scrub_controls.png` at 04:35:49 shows the **replay pause UI** (also used for foul review): bottom centered timeline (white with orange playhead, 600px width, y=1030), hurry text "REPLAY" (18pt orange, top-left x=40 y=60), and right-side pill stack above: "LT/RT ±1B / RB= Toggle" (12pt, dark bar 180px width). Faint pitch texture underneath.

## Foul / card / offside — candidate at 01:46:54

Figure 6c: `extra/foul_candidate_01_46_54.png` at 01:46:54 — **foul stop**: referee arm raised, 3 players colliding, ball stationary at whistle. Camera is a hard cut to a 6m close on the incident (35mm, eye-height 1.5m), no replay yet. Next cut (+1.2s) is a 120-degree orbit to behind the defender, hold 2.3s, then 0.4s white wipe to live free-kick setup (no card shown in this incident). Card presentations in other matches follow same pattern but insert a **card hold**: sliding actor (fouled player stays down 3-4s), referee reaches to pocket (0.8s), holds card at arm's length toward camera (1.5s Yellow, 2.0s Red, card size ~40px width, #FFCC00 / #E53935, no text), then cut to free-kick wall. Offside breaks have *no* card — choreography-only whistled stand, linesman flag, then instant cut to indirect FK spot (no replay, just 2 cuts total, total foul window ~6-7s vs 10-12s for card).

Timings stitched from scene cuts: foul whistles cluster with scene intervals of 1.2s + 2.3s + 0.4s (wipes) → total floor ~4s choreography + 7s replay lead-in where used. GF currently has `Cutscene 5.0s` for change, variable for foul; matches PES order but PES's foul replay lead-in is 0s (live-then-replay separated, not embedded).

## 2/3/4 Addendum — vision-verified geometry & style notes

- **Scoreboard revisited** (`match_scoreboard_m3.png` at 02:17:06): matches OCR: navy #15306F 65px tall, top-left, but visibly also shows **bottom indicators** for tracked players (4 KONDO vs 21 etc.) — thin 40% black bar low-center with name + stamina (light-grey fill) and play-style icon (green/blue/purple arrow). Scorebar text is bold **Eurostile-ish condensed sans**, white #FFFFFF, 30px cap, with anti-alias fringe 1px. Clock chip is off-white #E8E8E8 with dark navy digits #1A2E6B (see `scoreboard_m1_zoom.png`).

- **Game Plan dual** (`game_plan_dual.png` at 00:11:40): split 50/50 down middle white line, each half has pitch formation map (dark green #1A6A1A with white markings, y-axis 70% of card height) at top, player avatar squares (anime/cartoon heads, 64x64px) with position label below ("Sugro", "Tea"), linked by arrows. Right-side roster list with colored playstyle badges (green →, blue ↘, purple ◆). Help-text bar at top of each half ">Game Plan" (16pt white on black 10% opacity), bottom footer "Help / A Go To >Game Plan Menu / LT RT Switch Player Icons" (12pt grey, pad 8px). Background is 5% desaturated pitch behind 94% white card, rounded corners 12px, drop-shadow 8px blur. This is *two independent editors* — versus GF's single `gui2` flat stack with buttons/sliders and no pitch map inset.

- **Pause Game Plan** (`pause_gameplan_teamchat.png` at 04:55:20): single-team modal over **frozen, desaturated (60% grey) pitch** (still shows ball at foot). Modal is white 92% opaque, 60% viewport width centered, header ">Game Plan" (16pt bold), 5 bottom tabs: [Pitch Icon] [Arrows Up/Down] [Boot] [Gear] [Folder] (32px square, grey bg, teal selected), label row "Team Sheet/Edit Position" (14pt grey). Footer `LB/RB` page hints. PES's pause inherits the Game Plan editor live; GF's `menu.pause` is a separate translucent dark overlay with 3 options — visually distinct.

- **Half/Full Time stats** (`halftime_stats_m1.png`, `fulltime_stats_m1.png` 00:27:10/00:39:40): full-screen **dark-to-blue gradient** (left #0A0F1E 80% to right #1E3A8A 90%, at 45deg), stat grid center with two team crests lateral (left HELLDIVERS shield yellow/black earth, right ACE jets), big score digits 96pt cream #FFF8DC centered top with "Half Time 45:00" / "Full Time" label 18pt white above. Stat rows: label center grey 14pt, values white 16pt bold. Below, tab row at post-match: "Match Events / Player Ratings / Top Menu / Highlights" with orange underline on selected.

## Summary delta table (adds to §7 in partial)

| Aspect | PES21 observed (this pass) | GF today | Build to match PES |
|---|---|---|---|
| Celebration staging | Group huddle midfield tableau + Arabic/English text overlay + stratagem pill; 4s huddle + 5s wide replay before Skip pill; white 0.13s wipe | Single scorer celebration local, staged+aimed camera, cap 20s, 1.9s fallback intro | Add text-overlay stratagem for Helldivers, group converge choreography, lengthen PES-default to ~9s before KO |
| Entrance | Trimmed to 22s aerial establishment (no walk-on) in tourney; longer tunnel available in offline | Full walk-on/tunnel with arch | Gate walk-on to tournament=none for fidelity; keep walk-on for offline only |
| Post-match | 14s FT card → Return/Next/Save menu; no joy/sad/greet ceremony | Audience→joy/sad→greet→photo sequence 6-7s each | Branch: tourney skips ceremony, career/online keeps it |
| Fouls | 6s live (whistle close 2s → orbit 2.3s → wipe 0.4s) → free-kick; card holds 1.5-2s; offside = flag only, 2 cuts 6-7s | 5s cutscene fixed for change, variable for foul | Encode 3-tier foul pipeline: no-card 6s, yellow 8s (+1.5s card hold), red 9s |
| Scoreboard | Persistent navy top-left + bottom player bars, not modal | HUD indicators only via ingress; no broadcast scorebar | Implement top-left navy bar asset (#15306F, 65px) + bottom stamina row |
| Menus | Split dual Game Plan with pitch-map inset; tabbed help-text; 5-icon footer | Flat button/slider stack | Rebuild Game Plan as 50/50 dual pane with pitch inset |
| Pause | Desaturated freeze + white modal 60%w, 5 tabs with icon row | Dark translucent overlay | Match modal sizing and tab icons one-for-one |

All stills referenced above are saved under `/tmp/pes21_ref_frames/` and `/tmp/pes21_ref_frames/extra/`. Re-dispatch thresholds: goals at 00:19:24/00:22:57 (M1), foul at 01:46:54, entrance 00:16:00, FT 00:39:40.

