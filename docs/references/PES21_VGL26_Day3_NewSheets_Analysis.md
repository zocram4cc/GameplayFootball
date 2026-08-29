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

