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

