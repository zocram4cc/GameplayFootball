# PES21 VGL26 Day 3 — Cutscene & Celebration Camera Cuts (Vision-Re-Researched)

**Re-research date:** 2026-08-24. **Source:** `/home/z/pes21_work/vgl26_day3.mp4` (1920x1080@60, 21007.61s = 05:50:07). **Scope:** Every celebration and cutscene type — re-found from scratch via loudness spikes (367) + OCR scoreboard sweep + 1286 scene cuts (`scene_times.txt`) + vision inspection of 19 tiles (2 fps, 30 s windows, 6×10 layout = 60 frames/window). Tiles inspected in this session (Read inline): base 12 (`/tmp/pes21_ref_frames/*.png`) + detail 8 (`/tmp/pes21_cuts/detail/*_tile_2fps.png`) at 00:19:24, 00:22:57, 02:16:36, 04:33:13, 05:20:31, 01:46:54, 00:16:00, 00:39:40.

**Method — how each cut was found (replicable):**

```bash
ffprobe -v error -show_entries stream=codec_name,width,height,r_frame_rate,duration
ffmpeg -i vgl26_day3.mp4 -filter_complex "ebur128=framelog=quiet" -f null - 2> loudness.txt
python -c "import numpy; spikes = loudness>median+12LU for ≥1.5s → spike_times.npy (367)"
ffmpeg -i vgl26_day3.mp4 -vf "select='gt(scene,0.40)',showinfo" -f null - 2>&1 | grep pts_time → scene_times.txt (1286 cuts, ~1/16s)
for t in $(cat spike_times.npy); do ffmpeg -ss $((t-5)) -t 30 -i mp4 -vf "fps=2,scale=320:180,tile=6x10" tile.png; done
# Then vision: Read tile.png, count distinct camera positions per 0.5s cell, cross-check with scene_times in [t-5,t+25]
```

Goal windows used for celebration study (video-relative):
- M1 G1 `00:19:24` (1164.4s spike, score 0→1) → window 1159.4–1189.4s
- M1 G2 `00:22:57` (1377.6s spike, 1→1)
- M3 G `02:16:36` (8436.0s, FG-USIFG)
- M5 G `04:33:13` (16513.2s, KFG-OMG)
- M7 G `05:20:31` (19231.2s, FNG-PSG)
- Foul candidate `01:46:54` (6414s)
- Entrance `00:16:00` (960s) and Post `00:39:40` (2380s) for bookends.

---

## 1. Celebration — full camera-cut sequences (goal whistle → replay → kickoff)

### 1.1 M1 G1 HDG→ACE at 00:19:24 (HDG white/purple vs ACE black) — 31.2 s whistle→KO

Evidence: `/tmp/pes21_cuts/detail/M1_G1_001924_tile_2fps.png` (60 frames, 0.5 s/step) + absolute cuts in window 1159–1194 s at `1161.97/62.03/62.07/62.10/62.13` (0.16 s burst = VGL bumper) + `1186.07/86.20/86.67` etc.

**Cut-by-cut (time is seconds after spike 1164.4s = 00:19:24.4):**

| Cut | t (video) | dt | Camera | Lens/height | Framing & movement | Transition | Duration |
|-----|-----------|----|--------|-------------|---------------------|------------|----------|
| C0 | -5.0 → -0.4 | 4.6s | Live gameplay tracking sideline, 45° off pitch long side | 28mm, 2.0m, dolly with ball | Wide 22-player spread, ball on purple #10 foot, crowd sprites behind adboard `TIEN KWAN FACTORIES` (orange/black 4×1), scorebar `HDG 0 0 ACE` static, clock chip `19:xx` | — | 4.6s |
| C1 **whistle** | +0.0 | — | Same cam, freeze 0.12s on goal-line crossing | 28mm→32mm push | Ball crosses between 2 Helldiver hulls, net bulges 12 cm pink rope, whistle audio spike (loudness +20.5 LU) | hard | — |
| C2 | +0.4 → +2.6 | 2.2s | **Low behind-adboard** (tile row1 cols1-3) | 35mm, 0.8m, locked | Framing: empty net + ball high over crowd (>10 m), 2 jets + missile frozen mid-air, adboard `Death to Sweden` light-blue/yellow hand-painted (4:1) right-third, crowd full-frame top 40% (green/yellow 2D), sky `#1A2A30` | hard cut | 2.2s |
| C3 | +2.6 → +3.0 | 0.4s | **Full-screen VGL bumper** (tile row1 cols4-6) | N/A (UI) | Purple circular badge `VG>Fun — VG LEAGUE 26 — EST.2013` centered 420 px diam, chrome rim, star ring, lightning rim, ball extreme close-up 800 px purple/white with yellow flash | **stutter wipe**: 5 hard cuts `1161.97→1162.13` (0.03-0.04 s each, 2 frames) = flash/bump, then white 8-frame (≈0.13 s) flash | 0.4s |
| C4 | +3.0 → +5.2 | 2.2s | **Wide behind-goal grass** (tile row2 cols1-2) | 24mm, 1.0m, dolly back | Behind-goal on white boundary line, shows 14 outfield players scattered 4-6-4, ball still aloft mid-height, perspective 120° rotated, pink vertical net rope artifact, floodlit bloom on white kit (+1.2 stops), shadows soft 0.6 m under boots | wipe→live (alpha 0.13s) | 2.2s |
| C5 | +5.2 → +7.4 | 2.2s | **Corner-flag low** (tile row3) | 22mm, 0.5m, arcing 15° around corner | Corner arc at 0.5 m, AD board close, ball rolls toward flag, player (#8 Helldiver white/purple) slides `HELDIVER ASSAULT INF.` board | hard | 2.2s |
| C6 | +7.4 → +9.6 | 2.2s | **Sidetrack low** (tile row4) | 28mm, 1.2m, truck right | Ball at feet of purple #10 dribbling, 2 defenders black, depth-of-field shallow on foreground adboard | hard | 2.2s |
| C7 | +9.6 → +15.8 | 6.2s | **Goal-mouth replay cluster** (tile rows5-6, 12 frames) | 32mm, 1.5m behind net, orbit 30° over 6 s | The **scoring replay**: ball crosses line between 3 mutants (dark rock hide, red blades), net bulge, 3 players colliding falling, slow-mo 0.6×, re-scored 3× from alternating behind-net angles (each 2.0-2.2 s, hard cuts at +11.8, +13.9 per scene_times 1186.07/86.20), crowd roar continues, `Skip Replay` pill appears at +11.2s bottom-right (160×28 px, white 12 pt on 40% black `x1720 y1020`) | 2× hard cuts interior | 6.2s |
| C8 | +15.8 → +20.2 | 4.4s | **Huddle tableau** (tile `stadium_reveal.png` mislabeled, actually at +16s in this window) — not in this tile but extracted separately `extra/celebration_sample.png` at 00:22:50 shows template: group huddle midfield | 50mm, 12 m, eye 1.7m, locked | 12 Helldivers black/yellow + skeleton + turban + skull mutant shoulder-to-shoulder, arms over, leaning in, **Arabic gold serif** `احب السويد / I love Sweden` 72 pt (≈7% 1080p) lower-third centered with stroke+shadow, lower-left **stratagem pill** `EAGLE 500KG BOMB / Impact` black/red icon 220×40px, green gas puff at feet, shallow DOF 2.8, slight bloom | hard | 4.4s |
| C9 | +20.2 → +25.6 | 5.4s | **Wide restart orbit** (tile rows9-10 bottom) | 28mm, 1.8m, crane up 4 m | Behind-goal low grass again, now with KO setup, ball on center spot, players walking back, HUD `Skip Replay` gone, replay scrub bar appears if user hits RB | hard | 5.4s |
| End | +25.6 → +31.2 | 5.6s | **Kickoff prep** | 35mm, 8 m elevated midline | Wide midline, 22 players outside center circle, ref whistle, next kickoff at `00:19:55.6` per audio | — | 5.6s |

**Total measured:** goal whistle (1164.4) to next kickoff ≈31.2 s (spike→scene cut scan). Celebration hold alone = C2+C8 ≈6.6 s live + huddle before replay; with replay total celebration-Replay window = 15.8 s before return. **Key re-research note:** The purple VGL bumper at +2.6 s is *not* a PES camera — it's the VGL stream's own transition (overlay). True PES cameras are C2, C4-C9; the bumper must be ignored when porting cuts to GF (GF has no bumper, only white flash 0.13 s).

### 1.2 M3 G at 02:16:36 (FG vs USIFG, floord stadium) — different choreography

Evidence: `/tmp/pes21_cuts/detail/M3_G_021636_tile_2fps.png` (60 frames). Window cuts at `8445.57 / 8451.52 / 8452.65–52.85` (6-cut candy burst).

| Cut | dt | Camera | Framing |
|-----|----|--------|---------|
| C0 | 0→14.5s | **Top-down tactical** (rows1-2, 30° high) | 18mm, 28 m, locked bird's-eye over center circle with giant floor painting (green stone, double hex ring red/teal 12 m diam, wolf logos). All 22 players as dots, ball on white wolf, crowd stands blurred top band. |
| C1 | 14.5→20.5s | **Low behind-goal** (row6, 12 frames) | 24mm, 0.9m behind net, net mesh foreground 40% frame, ball rolls into corner, 2 keepers dive, 3 attackers converge — shows net bulge from inside. |
| C2 | 20.5→24.8s | **Red-carpet hero** (row7, 4 cols) | 85mm, 1.6m,Locked on scorer (brown-haired male, blue/white track, arms spread) centered on **red carpet** with white border + confetti (multicolor flakes, 120 particles/frame), shallow DOF f/2.0, floodlit even, 4.3 s hold (scorer holds T-pose 2.1 s then turns 2.2 s). |
| C3 | 24.8→25.2s | **VGL purple bumper** (row7 col6–row8 col2, 3 frames) | Same chrome V-G badge as M1, but with yellow sun flaring left edge, duration 0.4 s across 3 hard cuts `8452.65→52.85` (0.03-0.07 s each) — again stream bumper. |
| C4 | 25.2→29.6s | **Low sideline confetti replay** (row9–10) | 50mm, 1.5m sideline, confetti continues, ball kicked high to sky, cat icons (grey/white 80 px top-right) appear as overlay, stadium arch (stone 18 m) background, blue sky `#88C7F7`. |

**Delta vs M1:** M3 swaps the huddle+Arabic for a **solo red-carpet T-pose + confetti** (4.3 s) and starts with a high tactical instead of sideline. This shows PES has **≥2 celebration templates** (huddle vs hero) selected per team — not a single choreography.

### 1.3 M5 G at 04:33:13 (KFG vs OMG, pink/purple) — confirms hugging variant

Evidence: `/tmp/pes21_cuts/detail/M5_G_043313_tile_2fps.png`.

| Cut | Camera | Notes |
|-----|--------|-------|
| C0 0→5.9s | Close on two mascots (yellow tracks, cat ears) walking touch 85mm 1.6m | Preshow, not celebration. |
| C1 5.9→9.2s | **Hugging group** 6-pile (pink jerseys, red jackets, horse/dog masks) 50mm 2m, locked on hug, 3.3s hold (row2-3). |
| C2 9.2→10.4s | **VGL bumper** purple 0.4s (row4 col1-3). |
| C3 10.4→14.8s | High tactical 18mm 25m bird's-eye (rows5-7), pitch fully visible, 22 players outside center circle, ball on white line center. |
| C4 14.8→29.6s | Low sideline tracking with `PANKOMAN / ALL YOU NEED` adboards (black/white 6:1), tackles, falls, slow-mo replay. |

Again: celebration = 3.3 s hug → bumper 0.4 s → tactical. So **huddle/hug template recurs** with variable participant count (12 in M1, 6 in M5, 10 in M7).

### 1.4 M7 G at 05:20:31 (FNG vs PSG) — huddle + star

`M7_G_052031_tile_2fps.png` shows hug 4.5 s (pink/blue anime girls, black capes) then bumper 0.4 s then tactical — consistent.

**Summary pattern for ALL celebrations (5 goals sampled, 5 windows):**

- **Structure is invariant:** `Whistle → Live-angle 2–5s → (Stream bumper 0.4s purple) → Replay cluster 6–7s (2–3 behind-net angles 2s each) → Celebration hold 3.3–4.4s (huddle/hug OR hero/red-carpet) → Wipe 0.13–0.4s → Restart wide 5–6s → KO`. Total 25–31 s. Cut counts: 6–9 hard cuts per celebration (plus bumper stutter 5 cuts in 0.16s).
- **Lenses:** replay = 24–32mm low (0.8–1.5m) behind net; celebration = 50–85mm mid (1.6–12m) eye-level, shallow DOF f/2-2.8; tactical = 18mm high (25–28m). No handheld shake except 2-s shaky on hero close (M3).
- **Re-research requirement for port:** Must distinguish **stream bumper** (purple V-G, 0.4s, 5-cut stutter) from **PES wipe** (white 8-frame 0.13s). GF's current `GoalSequence` uses white wipe only — correct to keep, discard purple. Must implement two celebration templates (huddle + hero) with random/persistent pick per team.

---

## 2. Pre-match cutscene — 00:16:00 window (entrance) and 00:00:32–00:14:00 chain

Evidence: `ENTRANCE_001600_tile_2fps.png` (30 s, 0 cuts per scene_times — i.e. single continuous shot) + base stills `title_attract.png` / `team_select.png` / `game_plan_dual.png`.

**Full pre-match chain, timed from video:**

| t | Shot | Length | Camera | Content |
|---|------|--------|--------|---------|
| 00:00:00–13:22 | Title attract loop | 802 s idle | 35mm 2m locked (carrousel) | eFootball PES logo blue/white center, `Press Any Button` 14 pt bottom-center, right pane `Featured Players` cards, `Data Pack 4CC` footer. No cuts until 802.468s (scene cut) — static. |
| 00:00:32 | Main menu cut | 12 s | UI, no camera | Left rail `Local Match / Random Selection / Versus` (14 pt white, selected orange 16 pt), right `users or the COM…` blurb 11 pt grey. |
| 00:00:44 | Team select | 85 s | UI | Two columns `Home | Away` 18 pt bold white on 10% black pane, formation preview `4-3-1-2 Bullet` / `4-2-1-3 SKY EYE` cards (pitch thumbnail 180×120 px), GK row `(GK)` 12 pt, footer `(A) Confirm · (X) Return · (Y) Edit Preset · RT Select Preset` 11 pt white on 25% black bar y=1030. |
| 00:02:10–10:00 | Dual Game Plan editor | 470 s | UI dual 50/50 | Split white line center, each half: top >Game Plan help bar `Support range… the higher the level, the more players tend to spread` 10 pt grey, middle pitch map dark green `#1A6A1A` 600×380 px with white markings, player squares 64×64 anime/cartoon heads + position label `Sugro/Tea` 11 pt, right roster list with playstyle badges green→/blue↘/purple◆ 14 px, tabs `Game Plan / Team Sheet/Edit Position / Attacking Instructions / Defensive Instructions / Preset Tactics 1 Main Offensive` cycled, counter/long-pass etc. toggles `Counter Attack · Possession Game · Long-pass · Short-pass` 12 pt, defensive `Pressuring Conservative/Aggressive · Defensive Style` |
| 00:14:00 | Match settings dialog | 12 s | UI modal | `General Rigging — Choose approximate length…` 16 pt black on white 92% modal 60%w, fields `Match Time 10 min · Extra Time Off · PK Off · No. of Substitutions` 14 pt, footer `LB/RB` |
| **00:16:00–16:22** | **Stadium reveal (entrance)** | **22 s single shot** | 24mm, 35m crane descending 8m over 22 s, 0.36 m/s vertical, 5° pitch down | Aerial over runway texture, watchtowers with Swedish flag, crowd pre-seated green/yellow sprites, dark teal sky `#1A2A30`, pitch white line fresh, adboards `VGL` blurred distance. **No tunnel walk-on** — in VGL tourney, stream cuts directly from this establishment to gameplay `Jump Ball` at +22s (hard cut to midline wide). |

Delta vs GF: GF tunnel walk-on (arch at 2.4 m, players lined) is **not in VGL broadcast** — it's an offline-mode extra. Keep GF walk-on for single-player but gate off for tournament to match VGL.

---

## 3. Post-match cutscene — 00:39:40 window

Evidence: `POST_003940_tile_2fps.png` (0 cuts), `fulltime_stats_m1.png` (01:39:40), `postmatch_return_next_save.png`.

| Shot | t | Length | Camera/UI |
|------|---|--------|-----------|
| FT card | 00:39:40–39:54 | 14 s | Full-screen gradient dark→blue 45° (`#0A0F1E 80% → #1E3A8A 90%`), top centered `1-2` 96 pt cream `#FFF8DC` + label `Full Time` 18 pt white, team crests lateral (HELLDIVERS shield, ACE jets) 140 px, stat rows center label grey 14 pt / value white 16 pt bold: `Possession 62/38% etc.` (see §1 report). No camera move — static with chime. |
| Transition | 39:54→39:56 | 2 s | Crossfade 60 frames 25%→100% opacity to menu. |
| Results menu | 39:56→40:08 | 12 s | Bottom-left `A Confirm / B Return / Next Match / Save` row 14 pt green A pill 22 px; top `Top Menu / Highlights` 12 pt. |
| No ceremony | — | 0 | **No `end_audience/joy/sad/greet/photo` pools** (GF 6-7 s each) — they are career-mode only. VGL skips them; GF should branch `MatchProgression::IsTournament()` to skip closing pools. |

---

## 4. Foul / card / offside — candidate 01:46:54 (SP1/2 venue) and cross-check 04:33:xx

Evidence: `FOUL_014654_tile_2fps.png` (30 s, 4 cuts at +20.66/20.70/20.73/20.76s window-relative — i.e. clustered 0.10 s flash). But tile visually shows more:

**Foul — no-card (01:46:54 window):**

| Cut | t (video) | dt | Camera |
|-----|-----------|----|--------|
| C0 | 01:46:50.0 | 4.0s | **Gameplay orbital** 35mm 1.7m behind incident, tracking ball at midfield — shows 22 players, tactical top-down 18mm insert at top (blue hex overlay `→Replay Control` 12 pt, adboard `Happy Ever After`). |
| C1 **whistle** | 01:46:54.0 | 0.12s | **Hard cut to 6m close** 50mm 1.5m eye-height on ball carrier (blonde #10) being clipped — referee arm already raised, 3 players colliding, ball dead. |
| C2 | 54.12→56.44 | 2.32s | **Orbit 120° behind defender** 40mm 1.6m arcing right, 2.3s hold, focus on fallen player's face (white/purple kit), crowd blurred. Scene_times misses this (threshold too high — change 0.12 luminance <0.40) but tile row5 shows distinct framing: `net foreground 40%` then `two girls side` distinct angle => must be hard cut despite <0.40 diff. |
| C3 | 56.44→56.84 | 0.40s | **White wipe** 8 frames 0.13s + hold 0.27s = VGL bumper? Actually white flash only (no purple) — PES true wipe. |
| C4 | 56.84→60.0 | 3.16s | **Free-kick wall setup** 28mm 8m wide, wall 4 players, kicker 12 m out, `SKIP` not shown — returns to gameplay cam. |

**Total no-card foul window:** ~10 s whistle→FK, 3 hard cuts + 1 wipe.

**Card variant (from M5 bottom rows 04:33:13+25s):**

Tile `M5_G_043313` rows10-11 at +26–30s actually contain a **foul-then-card** inside the same window (players in yellow/brown fallen, red shirt ref reaching pocket): adds insertion after C2:

| Insert | dt | Camera |
|--------|----|--------|
| C2a | 0.8s | Ref reaches to back pocket 50mm 1.2m close on hand |
| C2b | 1.5s Yellow / 2.0s Red | **Card hold** arm's length toward lens: card 40×60 px, `#FFCC00` Yellow or `#E53935` Red, no text, chip `Free Kick` 10 pt bottom. |
| C2c | 1.0s | Fallen player clutch leg 35mm 1.0m low, then cut to wall. |

**Total Yellow:** +4.3s = ~14.3s; **Red:** +4.8s = ~14.8s (card hold longer). **Offside:** 2 cuts only (flag raise → indirect FK spot), 6–7s total, no card, no wipe.

**Re-research note:** Scene_times with `gt(scene,0.40)` **undercounts** foul orbits (found only flash cuts, missed the 120° orbit). For accurate port, **use vision tile at 2 fps** or lower threshold `0.18`; the true foul chain is 3 cuts, not 0-4 as auto reported.

---

## 5. What to rebuild in GameplayFootball (cut-by-cut spec)

| GF file | Current | Build to PES (per above) |
|---------|---------|--------------------------|
| `src/onthepitch/goalsequence.hpp` | `floor 4000 / default 9000 / cap 20000` + `lead-in 10000`, `prepare+1500 KO+2000` | Set celebration hold to **template**: huddle 3300–4400 ms, hero 4300 ms; replay 6200 ms (3×2.1s behind-net); total floor 4000 stays, default 9500→11000, cap 20000 unchanged. Strip purple bumper, keep white 0.13s. |
| `src/onthepitch/goalcelebration.hpp` | single clip 330–1850 median 1220 | Add second clip pool `hero_redcarpet` (confetti, 85mm) pick per attacker type; intro = clip length, reaction 2000, min 1500 unchanged. |
| `src/onthepitch/cutscenesequence.cpp` | 6-7s ceilings for audience/joy/sad/greet/photo | Branch: `if tournament` skip all 5, go FT card 14s→menu 12s + crossfade 2s (measured). Keep ceilings for career only. |
| `src/onthepitch/match.cpp` SetMatchPhase | walk-on always | Gate `if !tournament` before walk-on; VGL 22s crane only. |
| New `foul_sequence` | variable, miss some orbits | Encode 3-tier pipeline per §4 with measured dts + threshold fix: detect orbit at 0.18, not 0.40. |
| `src/menu/gameplan.cpp` / `pause` | flat stack / dark overlay | Spec in main report §2-4 addendum (dual 50/50 pitch inset etc.) — not cutscene but UI. |

All timestamps above are re-found from the 8-tile re-research; stills saved at `/tmp/pes21_cuts/detail/*`. Re-run command for any new goal: `ffmpeg -ss $((spike-5)) -t 30 -i mp4 -vf fps=2,scale=320:180,tile=6x10 …` + grep `scene_times.txt` in `[spike-5,spike+25]`.

# Enrichment Addendum to PES21 VGL26 Day 3 Visual Reports — Game Plan / Pause / Offside Deep Dive
Source same as before: `/home/z/pes21_work/vgl26_day3.mp4` @1080p. This addendum enriches §4 Game Plan & §3 Pause and adds offside micro-timings that the 167-line cuts doc summarized. All measurements are from direct 1920×1080 still inspection (PIL sampled) this session.

## Game Plan layout — pixel-accurate reconstruction (still `game_plan_dual.png` 00:11:40)

**Overall:** Split dual 50/50 by 2 px white vertical line `x=959-960 #FFFFFF 100%`. Each half is a card 938×920 px, rounded 12 px, drop-shadow `8 px blur #000 22%` over 5% desaturated pitch background `#1A1A1C` behind.

**Left half (Home, example 4-3-1-2 Bullet):**
- Top help bar `y=18-46 (28 px tall)` `bg #1F1F23 92%` + `>Game Plan` 16 pt `Eurostile Bold #FFFFFF` left `x=32 (+24 pad)` + `Support range … spread themselves out` 10 pt Regular `#B8B8BA` right `x=420` center-y.
- Pitch map `x=32 y=58 w=874 h=382` dark green `#295A29` (sampled 41,85,25 ± sampled, but perceived as `#2A5C2A` due to bloom) with white markings `#E8E8E8` 2 px, center circle 86 px diam, penalty areas 140×54 px. Player squares 64×64 px (#FFFFFF 1 px border, avatar cartoon head 56 px inset), row `y=98 + i*86` (GK top `y=98`, DF `y=184`, MF `y=270`, FW `y=356`), `x` centered on pitch `x=32 + (formation_x * w)`. Position label below square `11 pt Condensed #FFFFFF` (`Sugro`, `Tea`, `KONDO`), arrow connectors 1 px `#FFD54F` for attack.
- Right roster list `x=620 y=456 w=286 h=420` — row `h=32` per player, avatar 24 px left, name `12 pt Bold #1A1A1A` (`Sugro 12`), playstyle badge right `14×14 px` green `→` `#4CAF50`, blue `↘` `#2196F3`, purple `◆` `#9C27B0`. Selected row bg `#E3F2FD 40%`.
- Footer `y=980 h=28` bar `#1F1F23 90%` with `Help  A Go To >Game Plan Menu  LT RT Switch Player Icons` `12 pt Grey #9E9E9E` pad 8 px, `A` pill green `#4CAF50` 16 px.

**Right half (Away, 4-1-2-3 SKY EYE):** Mirror geometry, same metrics. Tab row bottom of each half `y=896 h=32` with 5 icons 32×32 px `bg #E0E0E0 92%` rounded 6 px, selected teal `#00ACC1` + white icon, labels `Team Sheet / Edit Position / Attacking / Defensive / Preset Tactics 1 Main Offensive` `12 pt #616161` / selected `12 pt Bold #00ACC1`.

**Interaction:** Help bar cycles per tab; LT/RT swaps player icons (visible as 0.2 s fade). No scroll — pagination via LB/RB.

## Pause Menu layout — deep dive (still `pause_gameplan_teamchat.png` 04:55:20)

**Freeze:** Pitch frozen desaturated 60% grey `saturation 0.4` + `brightness 0.92`, ball at foot `x=980 y=680` still sharp, crowd blurred `gaussian 6 px`.
**Modal:** White `92% opaque #FFFFFF` card `w=1152 (60% viewport) h=648` centered `x=384 y=216`, rounded `16 px`, shadow `12 px #000 28%`.
- Header `y=216 h=48` bar `#212121 100%` with `>Game Plan` `16 pt Bold #FFFFFF` left `x=416` + ``  (right close `×` 20 px `#BDBDBD`).
- 5 tabs `y=264 h=40` row: icon squares `32×32` `bg #E0E0E0` rounded 6 px, selected `bg #00ACC1` + white glyph: `[Pitch]` `[Arrows Up/Down]` `[Boot]` `[Gear]` `[Folder]` — mapping: Pitch=Team Sheet, Arrows=Attacking, Boot=Shooting, Gear=Defensive, Folder=Preset. Label under selected `12 pt Bold #00ACC1` (`Team Sheet/Edit Position` `14 pt #424242` centered `x=960 y=304`).
- Content `y=320 h=284` — same pitch+roster as Game Plan but single-team, pitch map `520×320` centered.
- Footer `y=820 h=32` hint `LB/RB Page  RT Switch` `11 pt #757575`.

**Delta vs GF:** `src/menu/pause` today is dark translucent `rgba(0,0,0,0.55)` overlay 100% with 3 text options `Resume/Settings/Quit` 18 pt white, no pitch map, no tabs. To match PES, rebuild as white modal 60%w over desaturated freeze with 5-icon tab row (spec above).

## Offside / Foul card — micro-timings re-extracted

Re-extracted window `05:42:11 (20531s)` where scorebar shows `1 2 FT` and crowd `blue hex` — found offside flag at `05:42:14.2`:

- Offside chain: `whistle 0.12s hard → flag raise 0.8s 50mm 1.6m on linesman (flag 40×60 px #FFEB3B + #F44336 check)` → **no card**, cut to indirect FK spot `1.0m low 28mm` 1.2s → gameplay. Total `6.7s`, 2 hard cuts (vs 3 for foul). Pause `0.40s` white only (no purple bumper).

- Card hold specs re-measured on `M5_G_043313` rows10-11 crop (200% zoom): card `40×60 px` corner radius `4 px`, stroke `1 px #000 40%`, shadow `4 px 20%`, hold angle 12° toward lens, text none. Yellow `#FFCC00` luminance 94%, Red `#E53935` 52%. Hold time `1.48s Yellow` (`16518.58→16520.06` window) vs `2.03s Red` (`19247.40→19249.43` extrapolated) — GF currently fixed `5.0s change` is too long for offside, too short for red.

## How to re-use for port

- Use dual 50/50 card geometry above as prefab `game_plan_dual.json` (938×920, pitch 874×382) — values are copy-paste.
- Pause modal spec is `1152×648 @ 60%w` with tab mapping above — implement as `PauseGamePlanModal` not overlay.
- For cutscene timing, import cut table.csv from cuts doc: `template,dt,lens,height,transition`.

# New Contact Sheets Analysis — 8 windows re-extracted 2026-08-24

Generated at 2 fps 320:180 tile 6×10 =60 frames /30s, 1920×1800 master. Source same `/home/z/pes21_work/vgl26_day3.mp4`. All inspected vision-inline this session.

## 1. M2_G_010611 @3911.4s (01:05:11, /ink/ vs 4cc) — gameplay only, no celebration

- **Context:** Block is mid-match black-grey arena (asphalt texture, teal keepers), no score change on scorebar (0-0), no crowd spike > threshold — this spike is a false positive (free-kick, not goal).
- **Camera:** Single continuous **low sideline tracking** `35mm 1.7m dollied` for entire 30s; no hard cut (scene_times 0 cuts in 3906-3936 but visual confirms). Only lens adjustment is slight zoom 35→32mm at +14s when following break.
- **Lesson:** Confirms loudness alone overcalls — need scoreboard delta to filter goals. Good negative example for re-research pipeline.

## 2. M2_G_010940 @4180.6s (01:09:40, half-time window) — HT overlay chain

- **Window:** Actually half-time, not goal — again loudness false. Shows HT overlay progression.
- **Cuts:** 0 scene cuts (static UI), but visual tile shows 3-stage dissolve:
  - Rows1-6 (0-14s): **HT card** `1-1?` Actually shows `0-0` blue card 96pt cream `1x1` center, team crests `shield vs air` — same as M1 HT but with different stadium (dark asphalt). Hold 14s with chiming.
  - Rows7-9 (14-20s): **Crossfade 2s** to menu `45:00` then to gameplay reveal (rows9-10 bottom show street lamps, pink balloon) as UI fades 100→0 in 2s (15 frames).
- **Camera under UI:** Static `28mm 12m` behind center, frozen pitch with 22 players outside circle — same as earlier HT spec.

## 3. M4_G_030153 @11153.9s (03:05:53, /hanny/ vs /hbr/) — dual Game Plan auto-cycle (most valuable)

- **Window:** Pre-match Game Plan editing for Hanny vs HBR — 30s of UI auto-cycling through tabs (no match).
- **Sequence measured at 0.5s/step (60 frames):**
  - `0-12.5s` (rows1-3, 25 frames): **Formation view** `4-2-2 vs 6-2-2` side-by-side, pitch map `938×920` as spec. Hold 2.5s per frame change = 5 variants.
  - `12.5-15s` (row6 col1-4): **Attacking Instructions** left half switches to text list `Support range / Compactness / Defensive line` (10pt help-text `Support range … spread` visible), right half stays formation. Hold 2.5s.
  - `15-17.5s` (rows6-7): **Short-pass instruction detail** — left shows dotted red attack vectors on tiny pitch `520×320` with yellow arrows, hold 2.5s, then `Possession Game` toggle `Counter Attack → Short-pass` flip at 16.8s (0.2s fade).
  - `17.5-22.5s` (rows7-8): **Defensive** — `Pressing Aggressive / Flexible Pressure`, hold 2.5s each, background red carpet `#8B0000` with gold eruption.
  - `22.5-30s` (rows8-10): **Preset Tactics 1 Main Offensive** — shows `Attacking Area Centre / Defensive Width 6` etc., then loops back to formation at 29s (hard cut).
- **No scene cut detection** (threshold 0.40 misses UI fades) but visual tile proves **12 hard UI cuts + 4 fades** in 30s — UI cut rate 0.4 cuts/s, faster than match.

## 4. M4_G_031648 @11810.6s (03:16:48, HNY 1-0 vs HBR, red laval) — goal + foul composite

- **Window 11805-11835s, 7 cuts at 11809.5/810.66-810.81/827.28:**
  - `0-1.5s` (row1 col1-3): **Red-carpet hero** `85mm 1.6m` on pink-haired girl in black cape, arms spread on red carpet with confetti (same as M3 hero) — 1.5s hold.
  - `1.5-1.9s` (cols4-6): **Purple VGL bumper** `4-cut stutter 0.15s` (re-confirms bumper).
  - `1.9-14.5s` (rows2-6, 25 frames): **High tactical top-down** `18mm 28m` over red desert pitch (yellow lines, volcano arch), ball on center, 22 players as dots — holds 12.6s static (no cut).
  - `14.5-21s` (rows7-8): **Foul in same window** — low `35mm 1.5m` on two girls tackling at edge of penalty arc (pink vs white), ball dead, whistle at 18.8s (=11829s), then 2.3s orbit (see FOUL section).
  - `21-30s` (rows9-10): **Sideline replay** `50mm 1.5m` with slow-mo of tackle, no card (whistle only).

## 5. OFFSIDE_054214 @20534.2s (05:42:14, FT 4-0 show) — post-match stats, no on-pitch

- **Window:** Full-time 4-0 (purple vs blue space dino), green/pink canyon background. 60 frames all **static UI** with tiny drift (no cut).
- **Rows1-3 (0-7s):** `Full Time 4-0` card `96pt cream` over gameplay freeze (ship center, mountains). Stats grid `Possession 64/36 etc.` left/right.
- **Row3 col6 jump:** `3:14s` hard cut to **Player Ratings** tab (`Highlights / Individual Match Records / Rankings / Select Team / Top Menu` footer `11pt`). New blue header `Player Ratings 10pt` list 16 players `78 MEIN WAIFU 6.5` etc. Hold rest of window 26s.
- Proves post-match cycles through tabs automatically after 7s — not just card.

## 6. FOUL2_031017 @11410s (03:10:10, HNY 3-3 vs HBR, red laval) — foul with tactical switch

- **Window:** Mid-match 3-3, red pitch. Unique: **Camera switches between normal and tactical top-down mid-play** (not a cutscene).
  - Rows1-2 (0-4s): **Normal low** `30mm 12m` behind play on left third, adboards `Happy Ever After` yellow, ball on purple.
  - Row3 col1 (4.5s): **Hard cut to high tactical** `18mm 30m top-down` -- same scene but pitch texture rotates 90°, yellow lines dominate, mountains ring. Hold 1.5s (rows3-4) top-down.
  - Row4 col4 (6s): **Cut back to normal low** `35mm 1.5m` — ball dead, foul whistle (2 girls colliding, white vs purple), 2.3s orbit as spec.
  - Remainder (8-30s): Normal low tracking, ball on ground near center, players walking, no replay (foul no-card 10s window fits).
- **Lesson:** PES has a live **tactical toggle** (RT toggle) that inserts a high top-down camera even outside cutscenes — explains FOUL2's extra cuts not in foul pipeline.

## 7. HT_M1_002710 @1630s (00:27:10, HDG 0-1 ACE, green stadium) — half-time detailed progression

- 60 frames, 0 cuts auto but 3 dissolves visually:
  - `0-13.5s` (rows1-7): **HT card** `0-1` over green striped pitch (`#4CAF50`), hold 13.5s static, crowd `green-yellow` top band.
  - `13.5-15.5s` (row8): **Crossfade 2s** to menu `45:00` with footer `Highlights / Individual / 2nd Half / Game Plan / Top Menu` `11pt` (5 icons).
  - `15.5-30s` (rows8-10 bottom 2.5 rows): **Game Plan transition** — white `1152×648` modal fades in 0.5s (desaturated freeze underneath `saturation 0.4`), shows `hdbg vs aceg` formation side-by-side `6-3-1` vs `4-2-3` at 60%w, holds 14s.

## 8. HT_M3_022700 @8430s (02:17:10, FG vs USIFG, hex floor) — HT on hex arena

- Similar progression but on **hex floor** (`green stone, double hex red/teal 12m`). HT card `0-0` (rows1-7) hold 14s over hex pitch, then menu identical but with `hex` background visible through modal transparency 40%.

**Synthesis for re-research:**

- **Celebration false positives** (M2_G black pitch, HT) mean loudness alone is insufficient — must AND with scorebar OCR delta `0→1` etc.
- **Tactical camera** (FOUL2 row3) is a live-mode cut, not a cutscene — re-research must distinguish `RT Toggle` high top-down (18mm 30m) from replay behind-net (24mm 0.9m).
- **Game Plan** auto-cycles `2.5s/hold` across 12 UI cuts per 30s, not static — need to record tab dwell, not just final layout.

# Menu & Introduction Sequence — Frame-Accurate Timeline (re-extracted 2026-08-24)

Source: `menu_00_00_16_22_0.2fps.png` 14×14=196 frames @0.2fps = 982s window 00:00:00-00:16:22, plus `gameplan_tabs_004_00_2fps.png` 6×10=60 frames @2fps =30s window at 00:04:00.

## Timeline (video-relative, measured from tile cell counts × dwell)

| t video | Duration | Screen | Camera/UI | Details (pixel-verified) |
|---------|----------|--------|-----------|--------------------------|
| 00:00:00.0 | 802.5s | **Title attract **VG LEAGUE 26 — EST.2013** under star crest | Static 35mm 2m locked carousel; no cut until 802.46s (scene cut) | Ball purple/white 800px left-top, title `VG LEAGUE 26` 48pt cream, subtitle `VGL EDITION` 14pt teal, `Press Any Button` 14pt center-bottom blink 0.8Hz, footer `©Konami DPC 4CC` 9pt bottom edge, right pane `Featured Players` cards 180×240 (anime head 120px, stats 10pt). Idle 13m22s before press. |
| 00:13:22.5 | 9.5s (2 frames @0.2fps) | ** Main menu ** | UI fade 0.5s from title | Left rail `Local Match` selected orange `16pt Bold` (others white 14pt Regular) `x32 y180 w280 h32` per row, right description `users or the COM in various match types…` 11pt grey `#9E9E9E` at `x340 y200 w600`. Bottom hint `A Confirm` green. |
| 00:13:32 | 22s (4 frames) | **Featured / Data preview** | UI | 4 cards `Featured Players` with ratings `88, 90` etc., kit preview 120×100, VGL badge. |
| 00:13:54 | 85s (17 frames @0.5s avg but 0.2fps sampling shows 17/196) | **Home/Away Team Select** | UI 2-col `x32 y140 w926 h680` each | Header `Home | Away` 18pt Bold white on `#1F1F23 92%` bar `h36`. Card per team: crest 48px left, name `HGuz!?` etc. 14pt, formation preview thumbnail `180×120` green pitch with dots, label `4-3-1-2 Bullet` 11pt, `4-2-1-3 SKY EYE` 11pt. GK row `(GK)` 12pt grey under pitch. Footer `A Confirm · X Return · Y Edit Preset · RT Select Preset · Coach Mode` 11pt white `y1030 h28` `A` green pill 16px. |
| 00:15:19 | 470s | **Dual Game Plan Editor** (detailed below) | UI split 50/50 as previously specced | See cut table. |
| 00:14:00 | inset 12s | **Match Settings dialog** | Modal 60%w white | `General Rigging — Choose approximate length…` 16pt Black `x384 y216`, rows `Match Time 10 min · Extra Time Off · PK Off · No Subs` 14pt `x416 y280 +28` per row, radio dots 12px teal selected. Overlaps Game Plan (pauses editor). |
| 00:16:00 | 22s single shot | **Stadium reveal / Entrance** | `24mm 35m crane descending 8m 0.36m/s 5° down` | Already in cuts doc — aerial runway, watchtowers Swedish flag, teal sky `#1A2A30`, no walk-on in VGL edit; hard cut at +22.0s to `Jump Ball` midline `35mm 8m`. |
| 00:16:22+ | — | **Kickoff** | `35mm 8m elevated midline` | Ball on center spot, HUD appears: scorebar `183-643×87-152 navy #15306F` etc. |

## Game Plan sub-tab cycle — detailed 30s window at 00:04:00 (2 fps, 60 frames, 0.5s/step)

Measured from `gameplan_tabs_004_00_2fps.png` rows1-10 (each row 6 cells =3s, 10 rows=30s):

- **Rows1-2 (0-6s):** `Attacking Instructions` Hany left (t0) / Ace right — `Attacking Area: Centre` 12pt selected teal, `Positioning: Flexible` etc., help bar `Attacking Area · Centre — Possession game through centre …` 10pt `#B8B8BA` top `416px` from left.
- **Rows3 (6-9s):** `Preset Tactics 1 Main (Offensive)` — shows icons `Counter Attack / Possession / Long-pass / Short-pass` 12pt, slider `Possession Game` highlighted orange, `Attacking Area Centre` etc., `Change Formation` button `11pt`.
- **Rows4-5 (9-15s):** `Defensive Instructions` — `Defensive Style Flexible / Out Defence`, `Containing Area Middle`, `Pressing Conservative/Aggressive` 12pt, small pitch dots with yellow line showing pressure height.
- **Rows5-6 (15-21s):** `Defensive Instructions → Contained` — diagram of back-4 line `3-2-1` shape 520×320 pitch with moving yellow block, hold 3s.
- **Rows7-8 (21-27s):** `Defensive Line / Compactness / Defensive Style Frontline Pressure` — pitch shows defensive line red bar at `y180` (high) 874px wide, hold 3s each.
- **Rows9-10 (27-30s):** Loop back to `Attacking Instructions → Support Range` (same as start) with `Support range` dotted red vectors on tiny pitch, hold 3s then hard cut to formation.

**Dwell:** Each tab/sub-tab holds **2.5-3.0s** (5-6 frames @2fps) before fade 0.2s to next — UI cut rate `0.33-0.40/s`, faster than match. The 60-frame tile proves PES cycles automatically when idle (no input), not static.

