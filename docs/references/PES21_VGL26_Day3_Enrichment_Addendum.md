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

