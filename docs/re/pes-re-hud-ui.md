
---

## 3. The AFP instance-name table (OBS) — the element names an implementer can look up

A dense ASCII name table lives at `0x142be4000–0x142bec700` (`.data`). Every entry is a NUL-terminated
C string; the **screen names** also appear as pointer tables at `0x1435416xx` (each screen's
constructor owns a contiguous list). Two naming conventions:

- `foo_show`, `foo_off`, `foo_hide`, `foo_shortHide`, `foo_on` — **AFP frame labels** the code jumps to
  with `vtable[+0x80](movie, label, 1)` (a `gotoAndPlay`). OBS: the call pattern
  `mov rcx,[rbx+0x10]; mov rax,[rcx]; lea rdx,"<label>"; mov r8b,1; call [rax+0x80]` recurs in every
  widget update function.
- `/window/mainGroup/...`, `/score/score_contents/...` — **display-path strings** used to reach a
  nested child by path (`/window/mainGroup/time_texts_motion/time_texts/time/numbers_0`).

### 3.1 The scorebug — `game2d_score` (OBS)

AFP scene `game2d_score` (created from `common/menu/general/game2d.bin`); controller class
`Time@Screen@match2D`, main update `0x1420f0eb0`, helper `0x1420f10d0`, sub-setters
`0x1420f1210` (cards), `0x1420f12d0` (score digits), `0x1420f1750` (clock digits — already in
`pes-re-tactics-clock.md` §3.1), `0x1420f0d80` (team colours).

| Instance | Kind | Contents |
|---|---|---|
| `scoreTime_time_show` / `_off` | frame labels | the clock block appears/disappears |
| `scoreTime_losstime_show` / `_shortHide` / `_off` | frame labels | added-time block |
| `scoreTime_aggregate_show` / `_off` | frame labels | two-leg aggregate score |
| `time_disit_1/2/3` | frame labels | clock digit-count variant |
| `lossTime_disit_1/2/3` | frame labels | stoppage digit-count variant |
| `teamScore_home`, `teamScore_away` | text fields | the score, when the digit sprites are not used |
| `disit_1`, `disit_2` | frame labels | 1-digit vs 2-digit score variant |
| `aggregate_numbers_home`, `aggregate_numbers_away` | sprite digits | aggregate score |
| `teamName_home`, `teamName_away` (+ `numbers_0/1`) | text fields | team names |
| `cardNum_home0..4`, `cardNum_away0..4` | frame labels | cards shown (index = count) |
| `firstTeamColor_home/second`, `firstTeamColor_away/second` | colour widgets | kit colours |
| `emblemCompe` | image handle | competition crest on the scorebug |
| `title_we`, `title_pes` | frame labels | the scorebug's competition title variant |
| `show_kickOff`, `show` | frame labels | kickoff animation vs plain show |
| `MatchPlate-texFont-digit0..9` (+`-medium`) | font/sprite names | digit sprites used by every number |
| `/score/score_contents/time_texts/time_min_00..02`, `time_sec_00/01` | display paths | the clock's digit objects |
| `/window/mainGroup/losstime/losstime_progress/losstime_min_0..2`, `losstime_sec_0/1` | display paths | the stoppage counter |
| `/window/mainGroup/time_texts_motion/time_texts/time/numbers_0..4` | display paths | an animated clock variant |

### 3.2 Player name tags — `game2d_namePlateLeft` / `game2d_namePlateRight` (OBS)

`NamePlate@Screen@match2D`. Instances: `main/gaugeUpper`, `main/gaugeLower`, `playerName`,
`uniformNumber`, `yellowCard_on/off`, `teamCategory_national`, `teamCategory_club`, `emblem`.
Hide function `0x1420e7f70`; name/number update `0x1420e84c0`; related `0x1420e81c0` (show + yellow
card), `0x1420e7f70` (hide). Online variants: `game2d_namePlateLeft/Right` also have
`ModelCursorName`, `ModelCursorNameOnline`, `CoopNamePlate`.

### 3.3 Radar (OBS, class `Radar@Screen@match2D`)

- Constructor `0x1420e3000`; dot release `0x1420e2c70`; per-frame `0x1420e2910`; marker cache
  `0x1420e1c00`; init `0x1420e2ae0`.
- **11 player dots** created with AFP name-ids `0x89..0x93` (`getChildById`, `0x1420d6080`) and
  attached to the stage with `0x141f9e7b0`; released with `0x141f9e3e0`.
- **12 + 2 markers** with ids `0x5f..0x6a`, `0x6b`, `0x6c` (ids 0x5f..0x6a in `0x1420e2910`);
  **5 markers** with ids `0xb5`, `0xb7`, `0xb9`, `0xbb`, `0xb5` in `0x1420e1c00`.
- Position source: the object at `[this+0x18]`; floats `+0xcf4` (x), `+0xcf8` (y), `+0xcfc`
  (a multiplier). `setPosition(mc, x = [o+0xcf4]*S, y = [o+0xcfc]*[o+0xcf8]*S, z = 1.0)`.
- Scales seen: **0.75** (`0x14259bdd4`) for the dots, **2.515 / 0.268 / 0.1675** (`0x142bdfc50`,
  `0x142bdfc54`, `0x142bdfc58`) for the marker ring.
- Screen anchor: `[this+0x10] = screenOffset(idx) − [o+0xcfc]*46.0` (`0x1425cb478` = 46.0;
  the 7-case offset table at `0x1420be3d0` returns −16.0, −43.0, 22.0, −30.0 from
  `0x1425c7fa0`, `0x1425ccc40`, `0x1425c48c0`, `0x14259be88`).
  `[this+0x14] = 12.0f` (`0x41400000`).

### 3.4 Cards / injury banner — `game2d_card` (OBS)

`ModelYellowCard`/`info_card`/`info_injured`: `status_none`, `status_penalty_yellow`,
`status_penalty_yellowToRed`, `status_penalty_red`, `playerName_str`, `playerNameNumber_str`,
`emblem`, `emblem_0`, `emblem_1`, `time_home`, `time_away`, `text_title`,
`stats_home_team`, `stats_away_team`, `stats_home_number`, `stats_away_number`,
`stats_inplay_info`. Handlers: `0x1420e4440`, `0x1420e49b0`, `0x1420e4d90`, `0x1420e5180`,
`0x1420e55e0`.

### 3.5 Substitution banners (OBS)

`game2d_memberChange` (in-match): `playerName_in`, `playerName_out`, `team_str`, `emblem`,
`text_title`, `info_card`, `icon_out`, `icon_in`, `arrowRight_on/off`, `arrowLeft_on/off`,
`selectInPlayer`, `outPlayer`, `p_inPlayer`, `d_inPlayer`, `p_outPlayer`, `d_outPlayer`,
`complete`, `playerSelect`, `setGauge`, `playerImage`, `abilityNumber`, `position`,
`iconCondition` (`0x1420e4e0x`–`0x1420e5x0x`).
`game2d_memberChangeDemo` (the demo replay of the change): `anime_change_one/two/single/double`,
`change_1/2`, `emblem_0/1`, `emblem_double_0/1`, `emblem_single`, `substitutions_str_0/1`,
`playerNameIn_0..1`, `playerNameOut_0..1`, `playerNameIn_double_0/1`, `playerNameOut_double_0/1`,
`playerNameIn_single`, `playerNameOut_single`, plus `SE_HUD_MID_IN`, `SE_HUD_MID_OUT`.
Quick-substitution prompt: `game2d_quickMemberChangeLeft/Right` (`scene_quickMemberChange`,
`quickMemberChange_title`, `quickMemberChangeNum_item`, `quickMemberChangeWait_on/off`,
`quickMemberChangeNum_on/off/value`), handlers `0x1420ee600–0x1420eeb00`.

### 3.6 Pitch plan / simulation — `gameSimulationMain`, `gameSimulationPitch`, `gameSimulationStats`, `gameSimulationList`, `gameSimulationListBl` (OBS)

`main/contents/locator_pitch|list_home|list_away|stats|listBl`; `teamHeadlineText_home/away`,
`teamHeadlineTextManager_home/away`; `mode_BL`, `mode_ML`, `mode_myClub`; `emblem_home/away`;
`contents_hide/show`. Pitch (`gameSimulationPitch`): `/point_player_home_00..10`,
`/point_player_away_00..10`, `/point_ball`, `uniNumber_home_00..10`, `uniNumber_away_00..10`,
`/switchPlayAreas/playArea_0..134`, `offside_LineHome`, `offside_LineAway`, `offsideLine`,
`control_Line`, `control_Area`, `controllLine`, `controllArea`, `playerArrow`,
`switchDot_0/1/2`. Stats (`gameSimulationStats`): `d_timeline_0..4`, `d_stats_0..3`,
`/stats/d_stats_0/statsGauge_home`, `title_timeline`, `title_stats`, `title_possession`,
`possession_home`, `possession_away`, `timeline_time`, `timeline_action`,
`timeline_playerName`, `timeline_icon`, `timeline_emblem`, `value_home`, `value_away`,
`setPossessionGraph`. Handlers `0x1420fe910` (possession), `0x1420ff640`, `0x1420fef60`.

### 3.7 Goal sequence (OBS)

- `game2d_player` / `game2d_goal`: `subInfo_on/off`, `timeInfo_on/off`, `subInfo_assist`,
  `subInfo_numGoals`, `subInfo_none`, `time_goalShoot_0/1`, `minutesNum_str`,
  `goal_info_min_num_str`, `situation_str_0/1` (×6 pairs), `todaysGoal_value`,
  `todaysGoals_str`, `playerNameNumber_str`, `scorerShooter_0/1`, `playerName_str`,
  `goal_info_scorer_str`, `subInfo_assist_str`, `assist_str`, `assist_info_str`, `emblem_0/1`.
  Handler `0x1420f2f80`.
- `game2d_scoreDemo` (+`_0/_1/_2/_3`, `_kickoff`): `score_home/away`, `score_home_0/1`,
  `score_home_00/01`, `score_away_00/01`, `score_left_str`, `score_right_str`, `center_none`,
  `center_score`, `emblem_home_0/1`, `emblem_away_0/1`, `emblem_home/away/left/right`,
  `teamName_home_0/1`, `teamName_away_0/1`, `teamName_home/away`, `teamName_left_str`,
  `teamName_right_str`, `competitionName_0/1`, `scorePlayer_home_str_0..5`,
  `scorePlayer_away_str_0..5`, `homeScore_stop`, `homeScore_scroll_4/5/6`,
  `awayScore_scroll_*`, `SE_HUD_BIG_IN`, `SE_HUD_BIG_OUT`. Handlers `0x1420ea620`, `0x1420eaa50`.
- `game2d_demoTitle`: `demoTitle_str` (handler `0x1420f1df0`).

### 3.8 Penalty shoot-out — `game2d_pkScore` (OBS)

Per-competition layouts (`licence/game2d<Pes|Ucl|England|France|Italy|Spain|Brazil|Euro|Acl>Pk.bin`):
`lamp_left_0..4`, `lamp_right_0..4`, `lamp_home_0..4`, `lamp_away_0..4`, `lamp_off/on/show`,
`lamp_color_success`, `lamp_color_miss`, `emblem_left`, `emblem_right`, `emblem_home`, `emblem_away`, `teamName_left`, `teamName_right`, `teamName_home`, `teamName_away`,
`teamName_left_str_tex`, `teamName_right_str_tex`, `teamName_left_str`, `teamName_right_str`.
`game2dPkOrder` (`game2dPkOrderWindow`, `kickerSelect`, `playerName`, `value_skill`,
`value_abilityNumber`, `value_condition`, `value_favorFoot`, `favorFoot_left/right`,
`skill_on/off`, `abilityNumber`, `iconCondition`, `checkbox`, `headline`, `header_value`,
`LoadCallback…`). Handlers `0x1420eaa50`, `0x1420eae70`.

### 3.9 TACTICS banners (OBS)

`tacticsChange_show/off`, `attackDefenceLevelChange_show/off`, `tactics_on/off/show/hide`,
`attackDefenceLevel_0..4` (with `_0_1`, `_1_2`, `_2_3`, `_3_4`, `_1_0`, `_2_1`, `_3_2`, `_4_3`
transitions), `d_tactics_up/down/left/right`, `tacticsBalloonType_name`,
`tacticsBalloonType_name_player`, `tactics_name`, `tactics_player`, `tacticsBalloon_off/show/on/hide`.
Handlers `0x1420fbf20` (attack/defence), `0x1420fc8b0` (control line).

### 3.10 Other screens (OBS)

`game2d_message` (`message_str`), `game2d_gameSpeed` (`speed_100/200`, `speedVisible_on/off`,
`text_R3_explaining`), `game2d_setPlayDisplay` (`numActions_on/off/value`, `noSkip_on/off`,
`indirect_on/off`, `goalDistance_on/off`, `text_indirectFreekick`, `description_0..7`,
`description_item`, `description_value`, `goalDistance_item`, `goalDistance_value`),
`game2d_inplayChat` (`downArrow_on`, `bottom_chat_str`, `right_chat_str`, `top_chat_str`,
`left_chat_str`), `game2d_pause` (`text_title`, `show`, `info_home`, `info_away`, `emblem`,
`pause_home`, `pause_away`, `pause_enable`, `pause_default`), `game2d_playerStats`
(`d_playerRanking_0..2`, `d_playerCompare_0/1`, `d_userRanking_0..2`, `d_userCompare_0/1`,
`avatar`, `playerName`, `userName`, `text_value`, `text_title`, `playerImage`, `skin_other`,
`info_*`, `p_*`, and the full team-colour paths
`/main/window/pageGroup/p_playerRanking/d_playerRanking_0/teamColor/firstTeamColor`,
`…/secondTeamColor` etc.), `game2dTutorialCaption01/02`, `game2dReplay` (below),
`game2dCompeWinner` (`teamNameVisibility_on`, `compeNameWinner`, `compeNameWinner_str`,
`competition_str`, `teamName`, `teamName_str`, `teamName_str_tex`).

### 3.11 Replay control bar — `game2dReplay.bin` (OBS)

`text_save`, `name_str`, `text_timeCurrent`, `text_timeStart`, `text_timeEnd`, `text_x10`,
`text_fastFoward`, `text_fastFoward_x10`, `text_playLast`, `btnSave`, `pad_inactive`,
`nameBarVisible_on/off`, `setValue`. Handler `0x1420eb0c0`–`0x1420eb4bb`.

### 3.12 World-space 2D quads — `Asset/model/game2d/d2_*` (OBS, VFS)

17 models (`d2_card`, `d2_cursor_a/b/c`, `d2_headset`, `d2_icon`, `d2_manual_cursor`,
`d2_manual_pass`, `d2_offside`, `d2_offside_line`, `d2_pk`, `d2_pos_guide`, `d2_power_gauge`,
`d2_power_gauge2`, `d2_shadow`, `d2_stamina_gauge`, `d2_target`, `d2_training_guide`), each with a
`.fmdl` + `_alp_nomip.ftex` texture. These are camera-facing quads placed in world space (power gauge,
stamina gauge, target marker, offside line, PK helper, card icon, shadow, coaching target).

---

## 4. Triggers — when each element appears and disappears (OBS unless noted)

### 4.1 The one gate that controls most of the HUD

`GetHalfKind()` = `FUN_1420BD500` → `mov eax,[0x1437F965C]; ret` (the cached half/phase from
`*(gMatchContext+0x270)+0x1168`, same global the tactics-clock report uses). The **replay/demo
flag** is the byte at **`0x1437F968A`**: when non-zero, `GetHalfKind()`'s result is forced to 0 in
every HUD update (`cmovne eax, esi` where `esi=0`) and the whole clock/score block is skipped.

Phase enum (from the half-length tables, `pes-re-tactics-clock.md` §2.1): 1 = 1ST_HALF, 2 = HALFTIME,
3 = 2ND_HALF, 4 = EX_TIME, 5 = EX_1ST, 6 = EX_INTERVAL, 7 = EX_2ND, 8 = END.

| Gate | Where it appears |
|---|---|
| `half ≤ 6 \|\| half == 8` and not replay | scorebug visible; team colours refreshed (`0x1420f0d80`); score digits (`0x1420f12d0`); clock `scoreTime_time_show` (`0x1420f1750`) |
| `half == 8` only (end) | `scoreTime_losstime_off` is played *alone* (`0x1420f0eb0`) |
| `half == 7` | clock `scoreTime_time_show` or `show` instead of `show_kickOff` (`0x1420f0eb0`) |
| `half ≤ 6` | `show_kickOff` when the toggle byte `[obj+0xB8]` was non-zero, else `show` |

The latch byte `[scoreData+0xB8]` is a one-shot: when it is non-zero the widget plays the *kickOff*
variant and the byte is cleared, so the next show is plain.

### 4.2 Per-element trigger summary

| Element | Appears | Disappears |
|---|---|---|
| Clock (score) | every live tick while the gate above holds; digits chosen from the state built by `FUN_1420EFEC0` → `time_disit_1/2/3` | `scoreTime_time_hide` while `half > 6` or replay |
| Added time | `lossTime` present: `scoreTime_losstime_show`; `lossTime_disit_1/2/3` from the magnitude | `scoreTime_losstime_shortHide` (compact) / `_off` |
| Score | `disit_1`/`disit_2` from the score, digits via `MatchPlate-texFont-digitN` | hidden with the whole block |
| Aggregate score (cup) | `scoreTime_aggregate_show` / `_off` | as above |
| Cards | `cardNum_home0..4` / `cardNum_away0..4`, label index = clamped card count | count 0 → `…0` frame |
| Team colours | refreshed while the gate holds (`0x1420f0d80`, 2 sides × 2 colours) | — |
| Name tag | per-player; `yellowCard_on` when `0x1420e8292` fires, else `yellowCard_off` | `0x1420e7f70` (hide) clears name/number and plays `teamCategory_*` |
| Substitution banner | `game2d_memberChange` (interactive) or `game2d_memberChangeDemo` (replay animation) | `SE_HUD_MID_IN/OUT` sound, then off |
| TACTICS banner | `tacticsChange_show`, `attackDefenceLevelChange_show` (`0x1420fbf20`) | `tacticsChange_off`, `attackDefenceLevelChange_off` |
| Balloon | `tacticsBalloon_show/on` → `_hide`/`_off` | as named |
| Goal banner | `game2d_player`/`game2d_goal` (`subInfo_*`, `timeInfo_*`) | `subInfo_none`, `timeInfo_off` |
| Goal demo score | `game2d_scoreDemo` with `SE_HUD_BIG_IN` / `SE_HUD_BIG_OUT` | out label |
| PK shoot-out score | `game2d_pkScore` (`lamp_color_success`/`lamp_color_miss`) | — |
| Possession/stats | `game2dStats`/`gameSimulationStats`; `setPossessionGraph(float@obj+0x940)`, `possession_home/away` text | `contents_hide` |
| Replay bar | `game2dReplay` (`game2dReplayLong`, `btnSave`, `text_fastFoward_x10`) | — |
| Card/injury banner | `status_none` / `status_penalty_yellow` / `_yellowToRed` / `_red` | — |
| Pause | `game2d_pause` (`pause_home/away/enable/default`) | — |
| Big screen | `tv_condition.xml` → the stadium's tv template (§5) | — |

### 4.3 The `aandacht` banner

**Open (tried).** No `aandacht`/`letop` string exists in the exe or in `eng/string/all.str`
(case-insensitive sweep). The nearest items are `Attention` (a MyClub news headline type) and
`Now receiving treatment` (0x2BCB7) / `The injured player is unable to continue.` (0x2BC7F) in the
in-match string block. **INF (medium):** the substitution/attention notice in PES 2021 is the
`game2d_quickMemberChange*` + `game2d_memberChange` pair of §3.5, not a separately named banner.

---

## 5. Layout and scaling rules

### 5.1 What is code and what is data

**OBS.** Two layers:
1. **Positions of the static HUD chrome (scorebug, banners, cards) = data**, inside the AFP container
   `common/menu/general/game2d.bin`. The engine never sets their x/y; it only jumps to authored frame
   labels and writes text/images into named children.
2. **Positions of the dynamic widgets (name tags, radar dots, set-play control line, stat dots) =
   code**, `setPosition(mc, x, y, z)` (`0x141F9F910`) on a MovieClip obtained by name
   (`vtable[+8]`) or by container-internal name id (`0x1420D6080`).

The container is VLE-encoded, so the chrome coordinates could not be extracted statically this
session (§9.3); what *is* extractable is the child-name tree and the code formulas, which is enough
to re-author the layout in Godot.

### 5.2 The AFP MovieClip API (OBS) — an implementer can bind any named child

| Call | Meaning |
|---|---|
| `vtable[+0x08](movie, name)` | `getChild(name)` → child MovieClip (used for `playerName`, `uniformNumber`, `text_title`, `emblem`, …) |
| `vtable[+0x80](movie, label, true)` | `gotoAndPlay(label)` (`_show/_off/_hide/_on`, `status_*`, `disit_*`, `lamp_*`, `tactics*`) |
| `0x141F9F910(movie, x, y, z)` | `setPosition` — xmm0=x, xmm1=y, xmm2=z, xmm3=1.0 (the 4th arg is the constant `1.0` at `0x14256AE1C`) |
| `0x141F9E7B0(stage, &out, &out2, nameId)` | attach a MovieClip created from a container name id |
| `0x141F9E3E0(stage, nameId)` | detach it |
| `0x1420D6080(this, nameId)` | `getChildById` |
| `0x1420FAEC0(out, name, idx, flag)` | create/reuse a widget record (name + slot + bool) |
| `0x141F8AC90(child, "setPossessionGraph", …)` | script-bind a native call by name into the AS3 object (the general "ScriptBind" mechanism) |
| `0x140E48BF0(textfield, string)` | set a text field's string |
| `0x140E493A0(textfield, int)` | set a text field's number |
| `0x141F8D7A0(child, label)` / `0x141F8DDE0` / `0x141F8DA80(child, table[idx])` | goto a label; then set a child's label from a pointer table (`MatchPlate-texFont-digit*` lives in the table at `0x143541AF0`) |
| `0x141F8DB80(obj, r, g, b)` | set a colour widget (`0x1420f0d80`) |

### 5.3 Radar geometry (OBS) — the closest thing to a written layout rule

```
dot.x      = [obj+0xCF4] * 0.75                       // 0.75 @0x14259BDD4
dot.y      = [obj+0xCFC] * [obj+0xCF8] * 0.75
dot marker.y = [obj+0xCF8] * 0.268                    // 0.142BDFC50
dot marker2.y = [obj+0xCF8] * 0.1675                  // 0.142BDFC58
marker.x   = [obj+0xCF4] * 2.515                      // 0.142BDFC54
radar.anchorX = screenOffset(sel) - [obj+0xCFC] * 46.0
radar.anchorY(const) = 12.0f
```

### 5.4 HD / SD / aspect

- **OBS.** Four HUD layout variants exist as AFP screen names in the image:
  `hud_defaultHD`, `hud_defaultSD`, `hud_gamePlanHD`, `hud_gamePlanSD` (`0x1427CBF88`–`0x1427CBFC7`),
  i.e. **two authored layouts (default, game-plan) × two resolutions (HD, SD)**.
  `hud_default`/`hud_gamePlan` also appear at `0x142BE0000`-ish.
- **INF (medium-high).** The AFP stage is a screen-space coordinate system whose origin is the
  **centre** (the anchor computations subtract from a screen value and the radar uses signed
  coordinates), scaled to the viewport by the engine; the code only repositions the dynamic widgets
  and never rescales the chrome, so the two HD/SD variants exist precisely because the AFP content is
  authored at two fixed design sizes.
- **OBS (negative).** No 4K-specific HUD variant string exists; the only 4K-specific data in the
  install is the `_4K_` texture cpks (see the stadium report §1.1). **INF (medium):** 4K reuses the
  HD layout with texture/backend scaling.

### 5.5 Safe area / aspect

**Open.** No safe-area constants were found in the HUD code region (`0x1420E0000`–`0x1421102CB`);
the only screen-related literals are the 7-case offset table of §3.3 and the 256.0 detail-level
threshold (`0x1425CA314`) used by the code-positioned widgets. See §9.4.

---

## 6. The data each element reads

| Element | Reads | Runtime source (see §7) |
|---|---|---|
| Clock | minute, second, stoppage digits | match record obfuscated triples; state built by `FUN_1420EFEC0` (see `pes-re-tactics-clock.md`) |
| Score | home/away score u32 | `MatchData`/record (`0x1420f12d0` reads `[state+0]`, side loop) |
| Aggregate score | two-leg aggregate u32 | record (`0x1420f12d0`, `disit_1`/`disit_2`) |
| Cards | card counts u32 ×2 (+1 spare slot) | `state+0x38/+0x3C/+0x40` (`0x1420f1210`) |
| Team colours | 4 × RGB bytes | `FUN_1414C3A80(teamObj, side, isFirstColour)` → `0x141F8DB80` (`0x1420f0d80`) |
| Team names | strings | team DB object (same path as the name tag, below) |
| Team crests | images | `setImage(widget+0x98, "emblem"/"emblemCompe"/"emblem_flag"/"emblem_team")` via `0x1420FB540`/`0x1420FB640` |
| Player name | string | team DB at `matchCtx+0x50 → +0x35960`, `FUN_1414C2170` (`0x1420e84c0`) |
| Shirt number | u16 | same object, sub-table `+0x29A`, count `word[+0x426] & 0x7F` (`0x1420e84c0`) |
| Radar dots | player/ball world position | `[widget+0x18]` floats (the registry/`CameraTarget`-like object), `0x1420e2910` |
| Possession | float | `[obj+0x940]` (`0x1420fe910`) |
| Match stats rows | stats values | same object + `/stats/d_stats_N/statsGauge_home` |
| Replay bar | timecode strings | `game2dReplay` text fields |
| Big screen | score + crests | the tv template quad layout (§5 of the stadium report) |
| Substitution banner | player names | team DB, `playerName_in`/`playerName_out` |
| Goal banner | scorer, assist, minute, score | `situation_str_0/1`, `playerName_str`, `minutesNum_str`, `todaysGoal_value` |

### 6.1 The team/player database object (OBS)

`0x1414B6A60()` returns the match context; `[ctx+0x50]` is the **team database**. Player records are
reached by index into `+0x35960` with a name lookup (`FUN_1414C2170`) and a number table
(`FUN_1414C10B0`, numbers at `+0x29A`, count at `+0x426` masked to 0x7f). Team colours come from
`FUN_1414C3A80(team, side, isFirst)` returning 3 bytes. **INF (high):** this object is the live
`MatchData`/team-record table the sim already builds, so the HUD is fully drivable from the engine's
own team data — nothing is hardcoded.

### 6.2 Fonts and digit sprites (OBS)

- AFP font packs (`WFNT` tag) named: `alphabetLarge`, `matchBold`, `plname`, `LPsm`, `XLPsm`,
  `LUcl`, `LUel`, `LUsc`, `numMid`, `numExt`, `numSml`, `numCard`, `numXl`, `matchBoldUcl`,
  `matchBoldUel`, `matchBoldUsc`, `numMatch`, `numMatchUcl`, `numMatchUel`, `numMatchUsc`, `match`,
  `matchUcl`, `matchUel`, `matchUsc` — VFS `common/menu/font/<name>_fnt.bin` (OBS: all these `.bin`
  paths are in the exe's path list at `~3954xxxx`).
- Digit sprites: `MatchPlate-texFont-digit0..9` and `…-digit0..9-medium`, emitted by the pointer
  table at **`0x143541AF0`** (10 entries); the card-count table at **`0x143541900`** (12 entries:
  `cardNum_home0..4`, dup, `cardNum_away0..4`, dup).
- The same glyph-source approach as the shirt numbers (`pes-re-markings.md` §2): the number ftex
  strips supply digit cells.

---

## 7. Where the values live (VFS / addresses)

| What | Path / address |
|---|---|
| Whole HUD content | `common/menu/general/game2d.bin` (702,529 B; WESYS-wrapped, one object `afp_game2d.apk` + embedded `TXP2` texture pack and `GE2D` shape records at `0x4F078`+) |
| HUD variant selector | image strings `hud_defaultHD/SD`, `hud_gamePlanHD/SD` @`0x1427CBF88` |
| HUD clock/score code | `0x1420F0EB0` (Time::update), `0x1420F10D0`, `0x1420F1210`, `0x1420F12D0`, `0x1420F1750`, `0x1420F0D80` |
| HUD name-plate code | `0x1420E7F70`, `0x1420E81C0`, `0x1420E84C0` |
| HUD radar code | `0x1420E3000` (ctor), `0x1420E2910` (update), `0x1420E2C70` (release), `0x1420E1C00` |
| HUD possession/stats | `0x1420FE910`, `0x1420FF640`, `0x1420FEF60` |
| HUD goal banner | `0x1420F2F80` |
| HUD score demo | `0x1420EA620`, `0x1420EAA50` |
| HUD tactics banner | `0x1420FBF20`, `0x1420FC8B0`, `0x1420FCB90` |
| HUD substitution | `0x1420E4E0x`–`0x1420E5x0x`, `0x1420EE600`–`0x1420EEB00` |
| AFP name-id tables | `0x142BE02F8` (radar child ids), and the screen name pointer tables at `0x1435416A8`+ |
| Digit/ card label tables | `0x143541AF0` (digits), `0x143541900` (card counts) |
| Screen-name strings | `0x142BE02F8`–`0x142BEC700` (§3) |
| Match clock/phase globals | `0x1437F965C` (half), `0x1437F968A` (replay flag) — same globals the clock report uses |
| Match context | `0x1436F3DC8` → `+0x270` (phase holder), `+0x2C0` (env object) |
| WESYS constants | `common/match/constant/constant_match.bin` — **27 objects, none HUD-related** (OBS: `animeAging{FreeMove,Dribble,Kick,Trap}.o`, `ball.o`, `camera{Inplay,Setplay}.o`, `command.o`, `cpuLevel.o`, `cursor.o`, `feintCommand.o`, `injury.o`, `inplayDemo.o`, `mlScreenShot.o`, `modeMatchup.o`, `pes15Test.o`, `pesSmart.o`, `rating.o`, `setplayGuide{Common,CornerKick,FreeKickFar,FreeKickMiddle,FreeKickNear,GoalKick}.o`, `teamEmotion.o`, `throwin.o`, `userPlayTendencyTest.o`) |
| Big-screen template | `Asset/model/bg/stNNN/tv/tv_template_stNNN_basic.json` (8.1–8.4 KB, 6 templates) + `common/bg/model/bg/tv/stNNN/tv_condition.xml` (97 B) |
| Big-screen textures | `Asset/model/bg/common/tv/common/sourceimages/{bg_basic_large_bsm,bg_basic_score_alp_bsm,bg_basic_small_bsm}.ftex` |
| Big-screen selector code | `0x1419E67D0` (parses `tv_template` type → enum 1..6; also `shadow_plane`, `audi_masking`, `audi_ambient`, `audi_lightdir`, `choreography`, `color_grading_game/demo`, `luminance`, `luminance_pos`, `turf3d`, `turf3dveg`, `envmap`, `adkindcondition`, `irv`, `adcondition`, `tv_model_intensity`, `stadium_model_intensity`, `ad`, `material_name`, `intensity`) |
| World-space quads | `Asset/model/game2d/d2_*/scenes/#Win/d2_*.fmdl` (+ `sourceimages/tga/#windx11/*_alp_nomip.ftex`) |
| Fonts | `common/menu/font/{alphabetLarge,matchBold,matchBoldUcl/Uel/Usc,numMatch,numMatchUcl/Uel/Usc,numMid,numExt,numSml,numCard,numXl,plname,LPsm,XLPsm,LUcl,LUel,LUsc,match,matchUcl/Uel/Usc}_fnt.bin` |
| HUD strings (text) | `eng/string/all.str` — in-match block around `0x2A9F4`–`0x2C500` (`Shots on Goal`, `Possession`, `ADDED MINUTES`, `Half Time`, `Results`, `Go to Penalties`, `Assist`, `Now receiving treatment`, `Quick Restart`, `Change FK Setup`, `Distance to goal`, `Indirect free kick`, …) |
| Player stats screen source | `MatchResultPlayerStats`/`MatchStatsMenuContent*` classes (menu side) + the in-match `game2d_playerStats` |

---

## 8. Status per priority item

| # | Item | Status |
|---|---|---|
| 1 | Overlay inventory (every element, region, trigger, data) | **Closed** — §3–§4 give a per-element spec for 64 classes and ~60 AFP scenes; regions are normalised-by-construction for the AFP chrome (§5.1) and code-exact for the code-positioned widgets (§3.3, §5.3). |
| 2 | Layout and scaling rules | **Closed with one open sub-item** — the code/data split, the AFP API, the radar formula, the 7-case offset table and HD/SD variants are established (§5). Safe-area/aspect handling is open (§9.4). |
| 3 | Data the overlay reads | **Closed** — §6 names the runtime source for every element (team DB at `ctx+0x50 → +0x35960`, kit colours, match record, possession float, string DB). |
| 4 | Where the values live | **Closed** — §7 lists VFS paths and image addresses, including the tv templates and the big-screen textures. |
| 5 | Anything not closed | **§9.**

---

## 9. Open items and what was tried

1. **Radar dot name-ids are container-internal.** The 11 dots are `0x89..0x93`, the 14 markers
   `0x5f..0x6a/0x6b/0x6c`, the 5 markers `0xb5/0xb7/0xb9/0xbb` (OBS). These ids are not
   `StrCode32` of any guessable name (tested `radar`, `ball`, `player`, `dot`, `point`, … — none
   matched), so they are indices into the AFP container's own interned name table. They resolve only
   after the container is decoded (§9.3). The *visible* consequence is limited: the ids are stable
   across the install, so an implementation can treat them as opaque handles.
2. **`[widget+0x18]`** (the object holding the radar/position floats) was not identified. Its floats
   are `+0xCF4/+0xCF8/+0xCFC` and it is set before the constructor reads it; the writer was not
   located. **INF (medium):** it is the same per-frame registry object the HUD's other dynamic
   widgets read, i.e. live ball/player state.
3. **AFP container decode.** `game2d.bin` starts `{u32 1, u32 8, u32 0x30, u32 0xAB811, u32 0x14}`
   then `afp_game2d.apk`, then a `TXP2` pack and 38 `GE2D` shape records (112 B each) at `0x4F078`.
   Everything after the first 0x100 bytes is VLE/varint-encoded (the file ends in `cf xx df xx ef xx`
   byte pairs), so the MovieClip transform table could not be read. Tried: section-magic census
   (`GE2D` ×38, no `SWF`/`ABC` sectons), magic hypothesis tests against the 48-byte fpk layout,
   WESYS parsing of the sibling `game2dLayout.bin` and `cmnPartsHud.bin` (both hold only a 20-byte
   `TXP2` object `80 40 80 20 80 40 80 21 80 42 80 21 ×4`), and a brute-force table scan.
   **Consequence:** the *static* chrome positions must come from either (a) decoding this container,
   or (b) authoring the layout in Godot to match the seen screenshots — the code-side evidence
   (§4–§5) is sufficient for (b) but the exact normalised numbers are not in this report.
4. **Safe area / aspect handling.** Swept `0x1420E0000`–`0x1421102CB` for width/height literals
   (1280/720/1920/1080/640/480/3840/2160 as f32 and as RIP-relative constants) — only the
   `0x1420BE3D0` offset table and the 256.0 detail threshold are HUD-related. **INF (low-medium):**
   PES 2021 does letterbox/pillar-free scaling inside the AFP stage rather than applying a code-side
   safe area, and HD/SD are two authored layouts rather than computed.
5. **`aandacht` banner** — see §4.3.
6. **RTTI is dead** (§1.3): every class in §2 was mapped by *code* (name xrefs and `.pdata`
   clusters), not by RTTI, so the class→function map above is from instruction reading only.

---

## 10. Coder recipe (Godot)

1. Build the overlay as a `CanvasLayer` with one `Control` per AFP scene of §3, each a
   `TextureRect`/`Label` mirroring the named children (`teamName_home`, `teamScore_home`, …).
2. Drive text with the same sources: `MatchData` for score/period, the team DB for names/numbers,
   `kit` RGB (÷255) for the four team-colour widgets.
3. Reproduce the clock exactly as `pes-re-tactics-clock.md` §3.1 (minute/second/`time_disit_N`,
   `lossTime_disit_N`, max 9 shown, replay flag hides all).
4. Hide/show the whole scorebug on `phase ≤ 6 || phase == 8 && !replay`.
5. Name tags and radar dots: use the §5.3 formulas with the engine's own player positions
   (Fox metres), converting metres→viewport with the camera's projection, and anchor the radar with
   the offset table + 46.0 rule.
6. Big screen: parse `tv_template_stNNN_basic.json` and draw its quads; select the template from
   `tv_condition.xml`.
7. Digit glyphs: `MatchPlate-texFont-digitN` = a 10-cell strip, same layout family as
   `pes-re-markings.md` §2.1 (horizontal `u = d/10`-ish cells).

---

## 11. Consolidated element spec (normalised coords = AFP design space, origin centre)

Region convention: **n** = normalised (0..1 across the viewport, AFP design space, origin centre),
**w** = world anchor, **c** = code formula, **d** = data-only inside `game2d.bin`.

| Element | Class | Region | Trigger | Data |
|---|---|---|---|---|
| scorebug (plate, bg, title) | `Time` | d (`/score/score_contents/…`) | `half ≤ 6 \|\| 8`, not replay | — |
| clock | `Time` | n (`/score/score_contents/time_texts/time_{min,sec}_NN`) | gate above | record minute/sec |
| added time | `Time` | n (`/window/mainGroup/losstime/losstime_progress/…`) | half 3/4 + `lossTime` | `SituationData+0x145` |
| score | `Time` | n (`teamScore_home/away`, `disit_1/2`) | gate above | `MatchData` score |
| aggregate | `Time` | n (`aggregate_numbers_home/away`) | cup two-leg | record aggregate |
| cards | `Time` | n (`cardNum_home0..4`) | card count | `state+0x38/+0x3C/+0x40` |
| team colours | `Time` | n (`/window/mainGroup/teamColor/*`) | gate above | `FUN_1414C3A80` kit RGB |
| competition crest | `Time` | n (`emblemCompe`) | gate above | team/competition DB |
| player name tag | `NamePlate` | w (follows player; `game2d_namePlateLeft/Right`) | per-player visible | team DB name + shirt number |
| shirt number on tag | `NamePlate` | n (child `uniformNumber`) | same | number table `+0x29A` |
| yellow-card flash on tag | `NamePlate` | n (`yellowCard_on/off`) | card event | card state |
| radar | `Radar` | w, anchored `screenOffset − [o+0xCFC]*46`, dots at `[o+0xCF4]*0.75`, `[o+0xCFC]*[o+0xCF8]*0.75` | radar on | `[w+0x18]` floats |
| possession bar | `PlayerStats`/`SimStats` | n (`/stats/d_stats_N/statsGauge_home`, `possession_home/away`) | stats screen on | `[obj+0x940]` |
| stats list | `SimList(BL)` | n (`uniNumber`, `playerName`, `switch_gauge/injury/card`) | stats screen | match stats |
| set-play info | `SetplayDisplay` | n (`description_0..7`, `numActions_value`, `goalDistance_value`) | set play | situation data |
| tactics banner | `SetplayTactics`/`TacticsIconPlate` | n (`tacticsChange_*`, `attackDefenceLevel_*`, `tacticsBalloon_*`) | tactic change | tactic state |
| substitution banner | `DemoMemberSwap` | n (`playerNameIn/Out_*`, `substitutions_str_*`) | substitution | team DB |
| goal banner | `GoalPerformance` | n (`subInfo_*`, `timeInfo_*`, `playerName_str`, `minutesNum_str`) | goal | event record |
| goal demo score | `DemoScoreVersus` | n (`game2d_scoreDemo`, `score_home/away_00/01`, `emblem_*`, `competitionName_*`) | goal demo | record + names |
| PK score | `PkScore` | n (`lamp_*`, `teamName_*`) | shoot-out | shoot-out state |
| card/injury banner | `ModelYellowCard` | n (`status_penalty_*`) | card/injury event | event record |
| replay bar | `Replay` | n (`game2dReplay`, `text_timeCurrent`, `btnSave`) | replay playing | replay clock |
| pause | `Pause` | n (`pause_home/away/enable`) | paused | — |
| messages/captions | `Message`/`Event`/`Footer`/`Title` | n (`message_str`, `demoTitle_str`) | events | `all.str` |
| power/stamina gauge | `ModelGauge`/`ModelStaminaGauge` | w, `d2_power_gauge.fmdl` / `d2_stamina_gauge.fmdl` quads | player action | player state |
| offside line | `ModelOffside` | w, `d2_offside_line.fmdl` | offside check | `offsideLine`/`offside_LineHome/Away` |
| big screen | `Manager` (stadium side) | w (stadium geometry) | stadium's `tv_condition.xml` | `tv_template_*_basic.json` |

---

## 12. Verification log

| Check | Result |
|---|---|
| PE section table parsed from the exe header (19 sections) | `.trace` va 0x1000, `.rdata` va 0x252F000 raw 0x252E600, `.data` va 0x339B000 raw 0x339A200, `.pdata` va 0x3AC5000 |
| VA→file-offset rule | `s_lossTime` @0x14259E108 → file 0x259D508 → reads `lossTime`; `match_time` @0x142836458 → 0x2835858 → reads `match_time`; image offset = VA − 0x140000000 verified on both |
| RTTI walk | 8,492 `.?AV…@@` strings found; the `match2D::Screen` ones have null vftable pointers and no references → name table, not live RTTI (OBS) |
| `.pdata` function list in `0x1420E0000`–`0x1421102CB` | 633 functions |
| Scorebug update `0x1420f0eb0` read end-to-end | title/losstime/aggregate/off labels, `show_kickOff` latch, emblemCompe image, then `0x1420f10d0` + `0x1420f0d80` |
| Score setter `0x1420f12d0` | `disit_1/2` + tens/units via `MatchPlate-texFont-digitN` table at `0x143541AF0`; text fallback `teamScore_home/away` |
| Card setter `0x1420f1210` | 2 slots (`state+0x38/+0x3C`), label table at `0x143541900` (`cardNum_home0..4`, `cardNum_away0..4`) |
| Colour setter `0x1420f0d80` | 2 sides × 2 colours from `FUN_1414C3A80`, `0x141F8DB80`, both divided by 255 |
| Name tag `0x1420e84c0` | name `FUN_1414C2170`, number table `+0x29A` count `word[+0x426]&0x7F`, `-1` → empty |
| Radar ctor `0x1420e3000` | 4 children from id table `0x142BE02F8` + 11 dots `0x89..0x93`; anchor `−16 − [o+0xCFC]*46` |
| Radar update `0x1420e2910` | 12 markers `0x5f..0x6a` + 2 (`0x6b`, `0x6c`), `0.75` scale, `[this+0x14]=12.0f` |
| Radar release `0x1420e2c70` | `0x141F9E3E0(stage, 0x89+i)` for 11 |
| Possession `0x1420fe910` | `setPossessionGraph` script-bind, value `[obj+0x940]`, texts `possession_home/away` |
| tv templates | st002 = 6 templates, 9 materials, 3 textures, version 1; selector `0x1419E67D0` maps type → enum 1..6 |
| `constant_match.bin` | 27 objects enumerated; none HUD-related (OBS) |
| Fonts | `WFNT` names list matches `common/menu/font/*_fnt.bin` VFS paths |
| World quads | 17 `d2_*.fmdl` in `Asset/model/game2d/` |

---

## 13. AFP container format — what was mapped this session (adds to §9.3)

`common/menu/general/game2d.bin` (OBS, byte-exact):

```
+0x00  u32 1            version
+0x04  u32 8            field (OBS value 8; meaning open — not the section count)
+0x08  u32 0x30         header size
+0x0C  u32 0xAB811      total content size = file size (0xAB841) − 0x30
+0x10  u32 0x14         archive-name length
+0x14  char[20]         "afp_game2d.apk"
+0x28  zero pad to 0x30
+0x30  "TXP2"           texture-pack section
```

The payload after `0x30` is a **tagged u16 stream** (tag `v>>12`, value `v & 0xFFF`), not plain
little-endian integers — that is why every naive u32/u16 parse produced garbage. Observed tags
include `0x000`, `0x100`, `0x200`, `0x400`, `0x500`, `0x600`, `0xB00`, `0xD00`, `0xF00`.

Sections identified by magic (OBS, census over the whole file):

| Magic | Meaning | Count / location |
|---|---|---|
| `TXP2` | texture pack | 1, at `0x30`; header `{u32 0x02040100, …}` |
| `NAMP` | name table — records of `{u16 prev, u16 next, "NAMP", u32 type, 4×u8 flags, …}` with a **prev/next u16 linked list** | 6 in `game2d.bin` (`0x502D8`, `0x5030C`, `0x5061C`, `0x50638`, `0x50654`, `0x50748`); 6 in `game2dLayout.bin` (`0x1DD8`…`0x1E70`) |
| `GE2D` | shape/gradient records (112 B each) | **38**, from `0x4F078`, stride `0x70`, preceded by an 8-byte index table at `0x4F000` |

`GE2D` record layout (OBS, byte-exact for record 0):

```
+0x00 "GE2D"
+0x04 tagged-u16 stream: 0x0100 0x0000, 0x0100 0x0001, 0x0000 0x7000, 0x0400 0x0000,
                         0x0100 0x0000, 0x0000 0x3400, 0x0000 0x5400
+0x38 six BIG-ENDIAN f32: 42.0, 0.0, 0.0, 100.0, 42.0, 100.0
+0x54 u16 0x0904, u16 0xFFFF, u16 0x0600
+0x5C bytes 66 66 66 00  (RGB #666666, alpha 0 — a fill colour!)
```

The BE-f32 values are clean 0/42/100 → **gradient control points and fill colours**, not layout
transforms. All 38 records carry the same `{42,0,0,100,42,100}` or `{0,100,100,0,0,0}` pattern.

`game2dLayout.bin` (the 4,818-byte sibling, WESYS-wrapped, one object `afp_game2dLayout.apk`,
**ASCII names `game2dLayout` and `game2dLayout_shape1` at `0x1EB8`/`0x1EC5`**) is the same format at
smaller scale, and `cmnPartsHud.bin` likewise (`afp_cmnPartsHud.apk`, `cmnBgmBar_shape1`,
`cmnOnlineMessage_shape3`).

**Names are not plain ASCII inside the container** — `game2d.bin` contains zero occurrences of
`score`, `teamName`, `losstime`, `mainGroup` or `scoreTime` (OBS: whole-file byte scan), so the
instance names of §3 are stored in the `NAMP` table in a compressed/encoded form, and the
container-internal name-ids of §3.3 (`0x89..0x93`, `0x5f..0x6c`, `0xb5..0xbb`) are indices into it.
**INF (medium-high):** decoding `NAMP` gives the id→name map; the layout transforms must live in a
section between `TXP2` and the `GE2D` index (the region `0x100–0x4F000`, which is where the
`s(stu1…` compressed runs live).

### 13.1 Consequence for the implementer

The static HUD chrome positions are **not** recoverable from this session's static evidence. Two
viable paths, in order of cost:

1. **Author the layout in Godot to match screenshots**, using §3's child names as the node names and
   §4's label language as the state machine. This needs no container decode and is the recommended
   path: PES's own HD/SD split (§5.4) proves the layout is hand-authored, not computed.
2. **Decode `NAMP` + the transform section** if exact byte-parity is ever required. The starting
   facts are in §13: tagged u16 stream, section magics, `GE2D` layout, `NAMP` layout, BE-f32
   gradient records.

### 13.2 `GE2D` record semantics (corrected, OBS)

All 38 records were decoded. The float block starts at **`+0x34`** and holds **9 big-endian f32**
values; the byte pattern across the corpus is

```
rec@0x4f078  [0,    42,  0,   0,   100, 42,  100, 0,   0   ]
rec@0x4f0e8  [100,  0,   100, 100, 0,   0,   0,   100, 0   ]
rec@0x4f3f8  [0,    100, 0,   0,   100, 100, 100, 0,   0   ]
rec@0x4f5b8  [133,  0,   133, 19,  0,   0,   0,   19,  0   ]    # 133 = 4:3
rec@0x4f938  [86,   0,   86,  34,  0,   0,   0,   34,  0   ]
rec@0x4fc48  [70,   0,   78.75, 1.25, 20, 0,   85,  5,   11.25]
rec@0x4fd98  [0,    102, 0,   90,  90,  102, 90,  90,  0   ]
rec@0x4fe08  [0,    6,   0,   0,   84,  6,   84,  0,   0   ]
rec@0x4ff58  [70,   0,   78.75, 1.25, 20, 0,   85,  5,   11.25]
```

`+0x5C` is a 4-byte RGBA fill colour (`66 66 66 00`, `ff ff ff ff`, `7f 7f 7f 00`, `00 00 00 d8`).
The `42/100/133/86/102/90` pattern and the aspect-shaped `133/19`, `86/34` rows are
**gradient/fill matrices and fill colours** (the Flash `FOCALGRADIENT`/`LINESTYLE` family), not
widget transforms: the widget transforms do **not** appear as plain floats anywhere in the container
(OBS: whole-file scan for BE and LE f32 runs in `0x100–0x4F000` found only coincidental byte
patterns inside the compressed `TXP2` data).

### 13.3 `NAMP` — the name table (OBS)

Six records, `{u16 prev, u16 next, "NAMP", u32 type, 4×u8 flag}`, prev/next linked
(`0x1DD8 → 0x1DF4 → 0x1E10 → 0x1E2C → 0x1E48 → 0x1E70` in `game2dLayout.bin`; `0x502D8 → 0x5030C
→ 0x5061C → 0x50638 → 0x50654 → 0x50748` in `game2d.bin`). Each is followed by tagged-u16
entries of the form `{0x0500, nameOffset}` `{0x09E0, hashHi}` where `nameOffset` steps by
`0x5F`-ish and `hashHi` differs by `0x20` between siblings.

**OBS:** the name strings themselves are **not** stored as ASCII inside `game2d.bin`
(zero occurrences of `score`, `teamName`, `losstime`, `mainGroup`, `scoreTime`, `playerName` in the
whole file), unlike `game2dLayout.bin` where `game2dLayout` and `game2dLayout_shape1` are plain
ASCII at `0x1EB8`/`0x1EC5`. **INF (medium-high):** `game2d.bin`'s names are length-prefixed or
compressed inside the tagged stream, and the container-internal ids of §3.3 index that table.

---

## 14. The viewport-layout transform for the name tag (OBS) — the clearest window into the HUD's coordinate system

`NamePlate::UpdateName` (`0x1420E84C0`) ends by calling **`0x140E5FA00`** on the record returned by
`0x140E5F2D0` (a pointer to the global at **`0x1436F9B38`**, i.e. the HUD/screen-layout object).
That function is a pure **normalised-rect → design-space-rect** conversion:

```
in  (rcx = layout object):
     +0x20  i32  mode        (0, 1, 2 select the branch)
     +0x24  f32  x
     +0x28  f32  y
     +0x2C  f32  h
     +0x30  f32  w
     +0x34  f32  kA    (x-scale factor)
     +0x38  f32  kB    (y-scale factor)
     +0x3C  f32  kC    (x-offset factor)
     +0x40  f32  kD    (y-offset factor)
     +0x44  f32  sY    (screen height in design units)
     +0x48  f32  sX    (screen width  in design units)
out (rdx = rect):  +0x00 x, +0x04 y, +0x08 x', +0x0C y', +0x10 left, +0x14 right, +0x18 bottom
```

Rules (OBS, instruction order):

```
normW = w   / sX        // 0x140e5f2f1
normH = h   / sY        // 0x140e5f36f
bottom = normW * kB     // +0x3a1  -> +0x0C
top    = h    - normH * kD  // +0x38b.. -> +0x04
x'     = normW * kA     // +0x37c  -> +0x08
y'     = normH * kC     // +0x3ab  -> +0x00
```

So the AFP design space is a **rect in design units whose size comes from the two floats at
`layout+0x44` (height) and `layout+0x48` (width)**, and every element's authored rect is divided by
those and multiplied by `kA/kB/kC/kD` (`+0x34/+0x38/+0x3C/+0x40`).

**Consequence for the reimplementation (INF, medium-high):**
- element rects are stored **normalised** in the design space and scaled by `(kA, kC)` and offset by
  `(kB, kD)`, i.e. **a single global scale/offset pair carries the whole HUD layout**;
- the four `k*` floats are the HD/SD switch: `hud_defaultHD` vs `hud_defaultSD`
  (§5.4) are two *layouts* of the same rects, not two coordinate systems;
- an element's on-screen region = `authoredRect / (layoutW, layoutH) × (kA, kC) + (kB, kD)`.

The `mode` field (`+0x20`) picks the branch: `0` → centre-align
(`left = x + h·0.5 − bottom·0.5`), `1` → `right = x + h − bottom`, `2` → `right = x`.
OBS: `0x140e5f316–0x140e5f340`.

**The container's own rect values are the normalised ones** (§13), so §13.2's `{0,42,0,0,100,42,100,0}`
is *not* a screen rect; the real screen rects come out of this transform, whose `k*` values live in
the runtime layout object at `0x1436F9B38`, not in the file.

---

## 15. The AFP screen object and its default layout scale (OBS)

The layout object of §14 is the **AFP screen/root object** (not a small helper):

| Fact | Evidence |
|---|---|
| Runtime singleton | global **`0x1436F9B38`** (null in the static image — it is a lazily-created singleton, OBS: value 0 at dump) |
| Created by | `0x140E5F270` → `0x1403FB410(0x13, 0, 0x2C0)` then `0x140E5DEA0`, i.e. **704 bytes** |
| Constructor | **`0x140E5E2E0`**; writes a 6-entry vtable from **`0x1427BC920`** into `[obj+0]` |
| Static template | `0x1427BC920` = `{vtable[6], "default", f32 0.975, f32 1.0, u32 0x45, …}` followed by the AFP property names `property_mode_input_type`, `forcedTransition` |
| Member offsets used | `+0x0A8`, `+0x108`, `+0x268`, `+0x290` (std::string members), `+0x20/+0x24/+0x28/+0x2C/+0x30` (rect), `+0x34..+0x40` (scale/offset factors), `+0x44/+0x48` (design width/height) |

**Default scale (OBS): `0.975` and `1.0`** in the static template. `0.975 = 39/40` (= 1560/1600
= 1170/1200 = 1872/1920 — INF): the AFP design space is authored at a slightly larger size than the
render target and uniformly scaled down by 2.5 %, i.e. **the HUD has a global uniform margin built
into the layout object**. Combined with `kA/kC` this is what makes the HD/SD split a data choice.

---

## 16. Final status

| # | Priority item | Status | Where |
|---|---|---|---|
| 1 | Overlay inventory with region / trigger / data | **Closed** | §3 (names), §4 (triggers), §6 (data), §11 (per-element spec table) |
| 2 | Layout and scaling rules | **Closed** (code/data split, AFP API, radar formula, HD/SD variants, normalised-rect transform, default 0.975 scale) — safe area open | §5, §13–§15; §9.4 |
| 3 | Data it reads | **Closed** | §6, §6.1 (team DB path), §6.2 (fonts/digits) |
| 4 | Where the values live | **Closed** | §7 (VFS paths + image addresses), §5 (big-screen templates + selector) |
| 5 | Anything not closed | **Closed with residual gaps** | §9 (radar name-ids, `[w+0x18]` identity, full AFP container decode, safe area, aandacht) plus §13.3 (names encoded in `NAMP`) |

Everything above is read from static artefacts only; the game was never running
(`pgrep -f PES2021.exe` empty before and after, OBS).
