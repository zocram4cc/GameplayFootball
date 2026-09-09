# VGL 26 Day 12 — the presentation reference, frame by frame

Source: `VGL 26 Day 12.mp4` (implying.fun, 12.6 GB, byte ranges OK). Pulled
as four clips with `ffmpeg -ss <t> -i <url> -t <n> -c copy` into
`tmp/vgl12/`; frames read at 1–2 fps and then full-size. Every claim below
names the timestamp it was read from. The "Now Rigging" ticker is the stream's
own overlay and is ignored throughout.

| what | broadcast time | clip |
|---|---|---|
| pre-match sheet, Stadium and General Rigging submenus | 5:22:36 | `prematch.mp4` 0–60 s |
| Strip screen, Game Plan (both teams) | 5:23:37 | `prematch.mp4` 60–90 s |
| goal sequence, live to restart | 5:09:59 | `goal.mp4` |
| end-of-match ceremony | 5:12:09 | `end.mp4` |
| Full Time stats card, then the post-match menu | 5:15:08 / 5:15:13 | `ht.mp4` |

This document is the reference for `src/menu/startmatch/*`, `src/menu/gameplan.*`,
`src/menu/ingame/{phasemenu,gameover,statsoverlay,goalbug}.*` and the goal /
closing sequences in `src/onthepitch/match.cpp`. Where our build already does
what the frames show, it says so; where it does not, the gap is named and the
plan is given. PES is the reference; the engine changes.

---

## 1. The pre-match sheet (5:22:36)

One card, centred, ~48% of the width, over the stadium backdrop. Not a grid of
sliders.

```
 ┌────────────────────────────────────────────┐
 │  [manager]  [crest]     V     [crest]  [manager] │   header band
 │            /xivg/             /hgg2d/            │
 ├────────────────────────────────────────────┤
 │  GK NAME (GK)           GK NAME (GK)            │
 │  name                        name               │   eleven rows, home
 │  name                        name               │   left-aligned, away
 │  ...                          ...               │   right-aligned
 │  name (C)                    name (C)           │
 └────────────────────────────────────────────┘
 [ Strip ] [ Stadium ] [ Kick Off ] [ >Game Plan ] [ General Rigging ]
```

Read off `pm_settings.png`:

- **Header band**: manager portrait (generic bust) at each edge, crest inboard
  of it, the team tag under the crest, a lone `V` between. Darker than the body.
- **Body**: the two starting elevens, one column per team, GK first and marked
  `(GK)`, captain marked `(C)`. Names are drawn in the **player's own name
  colour** from the .ted (`0x11 c RRGGBBAA` markup — we already decode it,
  `tools/pes21_import/ted.py`), so some rows are blue, orange, red.
- **Footer**: five equal buttons across the full card width. `Kick Off` is the
  one focused by default, drawn darker. Order: Strip, Stadium, Kick Off,
  >Game Plan, General Rigging. Left/right moves between them; Enter opens.
- Nothing else is on this screen. Difficulty, time, weather, kits, condition
  are all behind `General Rigging` and `Strip`, not on the front.

### 1.1 Stadium submenu (`pm_stadium.png`)

Title bar "Stadium", a large preview image of the ground (16:9, ~44% of width)
with a vertical icon column on its right (help, sun, cloud/sun, rain, snow -
the weather set, the current one lit), the index `1/13` under the preview, then
a **five-row list** with an icon per row and the value right-aligned with
`<  >` chevrons on the focused row:

| row | icon | value read |
|---|---|---|
| Stadium | pitch glyph | `/1999/ - Inside The Suitcase` |
| Time | clock | `Night` |
| Season/Weather | cloud | `Summer/Fine` |
| Length of grass | grass | `Normal` |
| Pitch Conditions | pitch | `Normal` |

### 1.2 General Rigging submenu (`pm_rigging.png`)

Title bar "General Rigging", a two-paragraph description of the FOCUSED row
at the top (it changes with focus), then a **nine-row list**, same
icon / label / right-aligned value / chevrons pattern:

| row | value read | our key |
|---|---|---|
| Match Level | `Legend` | `match_difficulty` |
| Match Time | `10 min.` | `match_duration_minutes` |
| Extra Time | `On` | (absent - always on) |
| PK | `On` | (absent - always on) |
| No. of Substitutions | `3 (+1 in Extra Time)` | (absent - 3 hard-coded) |
| Condition : Home | `? Random` | `match_condition_home` (**done**, see §6) |
| Condition : Away | `? Random` | `match_condition_away` (**done**) |
| Injuries | `On` | (absent) |
| Ball Type | `/1999/ - Green Oranges` | (absent) |

Condition shows a `?` glyph before `Random`; a fixed arrow shows the arrow.

### 1.3 Strip submenu (`gp_strip.png`)

Title "Strip", `Home` / `Away` column headers, the two kits previewed large in
the body (the frame caught the competition backdrop where the kit renders go),
`1/5` and `1/8` counters under them, and a two-row list at the bottom: `Home`
and `Away`, each with a colour swatch of the chosen kit and chevrons. Our
`kitSlider[2]` is the same data; the presentation is the swatch row + preview.

### 1.4 Where ours is

`MatchOptionsPage` (`src/menu/startmatch/matchoptions.cpp`) draws every setting
as a slider on one two-column sheet. Navigable now (column wrap, 66cbad9), and
it now carries the two Condition rows - but it is not this screen.

**Plan** (S/M):
1. Front card = header band + two elevens + five-button bar. The elevens come
   from `TeamData` formation entries; name colour from the player's `.ted`
   colour (already imported, `ted.py` name markup).
2. `Stadium`, `General Rigging`, `Strip` become **sub-pages** with the
   icon/label/value/chevron list widget (one new `Gui2SettingsList`, reused by
   all three). Values cycle with left/right; description text at the top comes
   from a per-row locale key.
3. Keep every existing config key; only the presentation moves.

---

## 2. Game Plan (5:23:37) — `gp_plan.png`, `gp_tactics.png`

**Split screen, both teams at once**, home left / away right, each half:

```
 [crest]  /xivg/                 (header, team tag, big)
 >Game Plan                      (breadcrumb)
 ┌── pitch ───────┐ ┌ list ──────────────┐
 │ cards on grass │ │ ▶ NAME     ▬▬▬▬     │  eleven rows: role glyph,
 │  (portrait +   │ │ ▶ NAME     ▬▬▬▬     │  name, condition/stamina bar
 │   name below)  │ │ ...                 │
 │ 4-3-1-2        │ └────────────────────┘
 └────────────────┘
 [pitch] [▼▲] [boot] [gear] [folder]      icon bar, five tabs
        Team Sheet/Edit Position           caption under the bar
```

- **Cards on the pitch**: portrait with the name beneath, a small role glyph
  on the portrait's corner (the up-triangle / arrow), `C` badge on the
  captain. Formation string bottom-left of the pitch (`4-3-1-2`).
- **List** beside the pitch: one row per starter - a coloured role glyph
  (orange forwards, green midfield, blue defence, purple GK), the name, and a
  short green bar (the player's condition). The glyph in the list and on the
  card is **the same indicator, toggled by LB/RB** between the condition
  arrow and the registered position (owner). In `gp_plan.png` the list shows
  the arrows; several are orange/green ups.
- **Tabs**: five icons - Team Sheet/Edit Position, Tactics (▼▲), Boots
  (individual instructions), gear (Support Settings), folder (Data
  Management / Save). The focused tab has `^ v` chevrons above and below it
  and its caption underneath (`Preset Tactics 1: Main (Offensive)` on the
  tactics tab).
- **Tactics tab** replaces the list with the seven-row tactics readout, each a
  cyan left rule, bold value and grey label: Counter Attack / Attacking Style,
  Short-pass / Build Up, Centre / Attacking Area, Flexible / Positioning,
  All-out Defence / Defensive Style, Wide / Containment Area, Aggressive /
  Pressuring.
- **Save** dialog (`gp_save.png`): a small centred card listing the presets and
  `Cancel` / `Overwrite`.
- The screen is **one and the same** pre-match and in-match (owner): the
  in-match pause menu opens exactly this.

### 2.1 Where ours is

`GamePlanPage` draws one team's map and list with a nav column of buttons and a
small opponent map at the bottom right. It is not split-screen, it has buttons
rather than icon tabs, and pre-match/in-match are two entry points into one
page already (`GoGamePlan(teamID)`), so the "not duplicated" requirement is
met at the code level but not visually.

**Done in this pass**: the LB/RB indicator toggle (Q/E on keyboard,
`GamePlanPage::ProcessKeyboardEvent`) flips every card between the
**condition arrow** (coloured `^ - v`, `PlanMapCard::ArrowGlyph/ArrowRgb`) and
the **registered position**; both maps follow the one mode
(`PlanMapCard::Indicator`). Evidence: `tmp/menu/a23.png`.

**Plan** (M):
1. Two `Gui2PlanMap`s side by side, each with its own list; the focused half
   gets the nav. The opponent's half is read-only (PES lets the streamer edit
   both; ours can too since the same page serves both teams).
2. Icon tab bar replaces the button column; captions under the bar.
3. Tactics tab: the seven-row readout already exists as data
   (`TeamTactics`/philosophy); it needs the rule+value+label layout.
4. Role glyph colours by line: orange F, green M, blue D, purple GK
   (`PlanMapCard::LineColour` already colours by line; align the palette).

---

## 3. Full Time stats card and the post-match menu (5:15:08, 5:15:13)

**Two screens, not one** (owner). Ours draws both on the same page.

### 3.1 The stats card (`ht_screen.png`)

Wide translucent navy card (~78% width), title `Full Time`, the competition
crest centred under it, the **score as two very large digits** at the card's
left and right thirds, the crests large beneath the digits, the team tags in
cyan under the crests. The stats table runs down the centre between them:

```
 53%      Possession        47%
 9 (8)    Shots (On Target) 8 (7)
 0 (0)    Fouls (Offside)   0 (0)
 0        Corner Kicks      2
 0        Free Kicks        0
 133 (113) Passes (Successful) 126 (110)
 1        Crosses           2
 9        Interceptions     10
 1        Tackles           0
 3        Saves             2
```

Ten rows, no bars, no heatmap. Label centred, values inward. Prompt at the
bottom: `A Confirm`. Nothing else on screen. This is legible because the
text is large (row height ≈ 4% of screen) and there is nothing competing
with it.

### 3.2 The post-match menu (`ht_menu.png`)

A different card: header band with crests, `Full Time`, the score and
`90:00`, team tags; then `Player Ratings` with `LB`/`RB` page tabs, and two
columns of eleven rows: number, name (in the player's name colour), rating
`x.x`, a star badge on the man of the match. Under the card, **five big
icon buttons** with captions: Highlights, Individual Match Records, Rematch,
Select Team, Top Menu (the focused one lighter). Prompts: `Toggle Display`,
`A Confirm`.

### 3.3 Where ours is

`GameOverPage` = the half-time card (`Gui2StatsOverlay`, 13 rows incl. xG and
the heatmap) + a `MATCH HISTORY / CONTINUE` bar on one page.

**Plan** (S):
1. `GameOverPage` shows the stats card alone with `Confirm` (already the shared
   `Gui2StatsOverlay`; drop the heatmap and xG rows from THIS card into a
   second tab or the ratings page, and raise the row height to ≈ PES's).
2. Confirm → new `ResultPage`: header band, Player Ratings (we have per-player
   match ratings? if not, average of the match's `PlayerRating`-style events -
   name the source before building), five icon buttons: Highlights (replay
   reel), Match Records (the stats card again), Rematch, Select Team, Top Menu.
3. Half time uses the same card with title `Half Time` and `Begin Second Half`
   / `Game Plan` in place of the five buttons.

---

## 4. The goal sequence (5:09:59) — `goal_a.png`, `goal_b.png` (0.5 s/cell)

Read across both grids. Times are from the clip start; the goal goes in at
≈ 8 s.

| t (s) | shot | length | notes |
|---|---|---|---|
| 0–8 | live wide camera, build-up and finish | – | ordinary HUD |
| 8–10 | **scorer from behind**, low, arms up, camera tracking his run | ~2 s | HUD stays: scoreboard **and** the player plates |
| 10–12 | **front medium**, low, ad boards behind, he turns to camera | ~2 s | |
| 12–15.5 | **two-shot** from behind as the first teammate arrives | ~3.5 s | a **scorer-name ribbon** rises bottom-left (yellow, his name only) |
| 15.5–17.5 | **mob close**, three heads | ~2 s | |
| 17.5–20 | **mob wide**, four players, low angle | ~2.5 s | |
| 20–24 | **crowd/stand shot** – in this room-stadium a dark ceiling with an arc | ~4 s | this is PES's crowd reaction shot; it is *meant* to be the stands |
| 24–27 | second stand shot (the "books" = this ground's stands) | ~3 s | |
| 27–28.5 | **VGL replay sting** (medallion) | ~1.5 s | streamer-side wipe |
| 28.5–34 | replay angle 1: wide behind the goal, build-up | ~5.5 s | `Replay` caption, no HUD |
| 34–39 | replay angle 2: from the side, finish | ~5 s | |
| 39–41 | sting out, live wide, players walking back | – | |
| 41–47 | **second replay burst** (a different goal later in the clip shows the same pattern: 3 angles incl. one from inside the net) | | |
| 47+ | restart | | |

Then at ≈ 66 s a **pause-menu card** appears (`Pause Menu`, score `5 90:00 4`,
team names, the five-icon bar): this is the **streamer opening the post-goal
menu by hand** - it is not automatic (owner).

**What matters for us:**

1. **Shot lengths are 2–4 s, and there are six to seven of them before the
   replay.** Ours are 15/12/15 s - three long holds. PES's celebration is
   *many short cuts*, and the whole thing (goal → restart) is ≈ 40 s here, not
   60–80: the 60–80 s figure from the Adriano reference included the
   streamer's replay control. Our montage should become **six beats**:
   behind-tracking 2 s → front medium 2 s → two-shot 3.5 s → mob close 2 s →
   mob wide 2.5 s → crowd 4 s (+ a second crowd 3 s), then the replay.
2. **The HUD stays up during the celebration**: scoreboard AND both player
   plates are visible on every celebration frame (`goal_a.png` rows 3–5). Only
   the replay is clean. Our `ApplyHudVisibility` hides the plates over a
   celebration - that should be revised to scoreboard + plates.
3. **The scorer-name ribbon** (yellow, bottom-left, name only, rising in)
   appears on the two-shot, ≈ 4 s after the goal. It is not the score bug
   from the Adriano reference; PES 2021 uses this ribbon. Our `Gui2GoalBug`
   should draw this instead of (or before) the score/scorer bugs, in the
   team's colour.
4. **Crowd shots exist even when the stadium has no crowd** - PES cuts to the
   stand regardless. We import stands for every ground; the shot family is the
   `end/audience_*` pool we already have for the closing sequence and should be
   reused here (two cuts, 4 s + 3 s).
5. **Replay is two or three angles, each ≈ 5 s, ending at the goal**, with a
   slow-motion tail on the last. Ours now does two angles (04530f9); add the
   third (inside-the-net / low behind) when the shot allows.
6. **Nothing is frozen**: in every celebration cell the scorer and the
   arrivals are mid-motion. Our fix for the past-end actor (7a1e173) is the
   right direction; the beats above are short enough that clips will not run
   out inside them.

---

## 5. The end-of-match ceremony (5:12:09) — `end_a.png` (1 s/cell)

| t (s) | what |
|---|---|
| 0–5 | final whistle on the live camera; players stop |
| 5–20 | **result lower-third stays on for every shot**: a dark bar with both teams' scorers listed (name + minute) and the score `5 4` between the crests |
| 6–11 | **losing side** close-ups: one player, head down, slow push; then a second, a teammate consoling |
| 11–15 | **winning side**: two players hugging from behind (low), then a wider three-shot |
| 15–17 | wide of both squads leaving, the flag bearers in frame |
| 17–19 | **fade to black**, prompt `Skip / Next` |
| 19–21 | **"Match Highlights" prompt card** (small, centred) - the streamer triggers the highlights reel here; it is not automatic |
| 21+ | the Full Time stats card (§3.1) fades in over the stadium; `A Confirm` |
| later | the post-match menu (§3.2) |

**Where ours is**: the closing queue (`CutsceneSequence::ClosingStages`) plays
crowd → winners → losers → walk → photo; there is no result lower-third, no
fade to black, no Highlights prompt, and the victory anthem now plays over it
(04530f9).

**Plan** (M):
1. `Gui2ResultBar`: scorers + minutes per side, score between crests; shown
   from the whistle through the whole closing queue (`hudLevel` gets a
   `ResultBar` state).
2. Fade to black at the end of the queue, then a `Match Highlights` prompt card
   with `Skip / Next`; on `Next`, play the goal replays back to back (we have
   the buffer and the angles); on `Skip`, straight to the stats card.
3. Order the queue as PES has it here: losers first, then winners, then the
   wide - `ClosingStages` currently leads with the crowd.

---

## 6. Condition arrows and position familiarity (owner's addendum)

Both are PES's live stat modifiers (Arrays 2 and 3 of the pipeline documented
in `docs/21Research.md` - not committed). **Implemented in this pass** in
`src/data/formstate.{hpp,cpp}` with `tests/onthepitch/form_state_test.cpp`.

### 6.1 Condition arrows (Array 2)

Five arrows, three stat tiers, fifteen multipliers - the engine's own table.
Tier 1 = the two awareness stats, the five GK stats and kicking power; tier 3 =
physical contact, balance, stamina; tier 2 = everything else including speed,
acceleration and jump.

| arrow | colour | tier 1 | tier 2 | tier 3 |
|---|---|---|---|---|
| Terrible | purple | 0.84 | 0.88 | 0.88 |
| Poor | blue | 0.92 | 0.94 | 0.94 |
| Normal | green | 1.00 | 1.00 | 1.00 |
| Great | orange | 1.06 | 1.05 | 1.03 |
| Top | red | 1.12 | 1.09 | 1.06 |

- **Per side**: `match_condition_home` / `match_condition_away` =
  `random | purple | blue | green | orange | red`, the two new rows on the
  pre-match sheet (`MatchOptionsPage`, "Condition: Home/Away"). `random`
  deals each player his own arrow from his Form; a colour deals that arrow to
  the whole side.
- **The draw** (`FormState::Draw`): PES's exact assignment is an open question
  in the research, so the Form attribute shapes the bands the obvious way - a
  steady player (form 1.0) is Normal three matches in four and never at the
  ends; a volatile one reaches purple or red one match in five. Reproducible
  from `match_condition_seed` (written by the sheet at Kick Off) so the pre-
  match card and the Team at kick-off show the same colour
  (`FormState::ArrowForPlayer`).
- **Ceiling**: `FormState::Apply` clamps to 1.0. A player already at 1.00 on a
  red arrow stays at 1.00 and nothing downstream ever sees a value above the
  profile range (owner). Pinned by `FormStateApply.NeverExceedsTheCeiling`.
- Where it enters play: `Player::GetStat` applies arrow × familiarity to the
  base attribute, clamps, then the situational factors (difficulty, fatigue,
  injury, clutch) scale it, and the result is clamped again.
- Shown on the plan card as a coloured `^ - v` (`PlanMapCard::ArrowGlyph`).
  Log line at team creation: `condition arrows (random): purple 4, blue 3,
  green 12, orange 1, red 3`.

### 6.2 Position familiarity (Array 3)

Thirteen slots, left and right kept apart (GK CB LB RB DMF CMF LMF RMF AMF LWF
RWF SS CF), rating A/B/C at each. Tiers differ from the arrows': tier 0
(technical, hardest hit) includes **speed and jump**; tier 1 includes
**acceleration** and the awareness/GK/kicking-power set; tier 2 physique.

| rating | tier 0 | tier 1 | tier 2 |
|---|---|---|---|
| C unfamiliar | 0.80 | 0.82 | 0.84 |
| B partial | 0.92 | 0.94 | 0.96 |
| A natural | 1.00 | 1.00 | 1.00 |

- Read at the **deployed formation slot**, not the player's position on the
  pitch (`Player::GetDeployedSlot`: role + left/right off the formation entry
  in the team's own frame). A CF at CB is penalised for as long as he is
  assigned there, wherever he runs.
- Carried as `<position_familiarity>` in profile_xml: thirteen letters in slot
  order. Absent → inferred from the registered roles
  (`FormState::InferRatings`: A at the roles' slots, B on their mirrors and
  neighbours, C elsewhere).
- **Importer gap**: the EDIT-record bit offsets of PES's thirteen playable-
  position ratings are not in the research text on disk, so `ted.py` does not
  decode them yet and no guess was made. Until they are sourced, imported
  players get the inference above; a hand-written `<position_familiarity>`
  wins when present.

---

## 7. Status

| item | state |
|---|---|
| Condition arrows, per side, pre-match rows, plan-card indicator + LB/RB toggle | **done** |
| Position familiarity in the stat pipeline, profile key, inference | **done** (importer decode of PES's bytes pending offsets) |
| Ceiling clamp on modified stats | **done**, tested |
| Pre-match front card + three sub-pages | plan §1.4 |
| Split-screen Game Plan with icon tabs | plan §2.1 (toggle done) |
| Two-screen full time (stats card → result page) | plan §3.3 |
| Six-beat celebration, HUD kept, scorer ribbon, crowd cuts, third replay angle | plan §4 |
| Closing ceremony result bar, fade, Highlights prompt, losers-first order | plan §5 |
