# PES 2021 set pieces and match officials — evidence report (items 1, 3, 7 closed; items 2, 4, 5, 6 open)

Scope: read-only RE of PES 2021. Sources: the constant archives of the install (VFS via
`build/apps/pesfmt_dump`), the exe image (`<scratch>/pesmem.bin`, base 0x140000000, 64 MB) and the
on-disk `PES2021.exe` (`.impdata` holds the reflection serializers, reachable only through the
on-disk file: `fileoff = 0x391b400 + (VA − 0x140000000 − 0x3d14000)`).
Tags: **OBS** = observed with how. **INF** = inferred with confidence.
Values in this report are PES's own tuning values (owner decision: verbatim values allowed as
evidence; nothing is committed to the repo).

Companion reports this must agree with:
- `pes-re-matchlogic.md` (attributes, tactics, ball, AI);
- `pes-re-animselect.md` + `pes-re-sim-motion-mapping.md` (clip selection, officials' clip
  families `judge_*` / `linesman_*` / `layer_*` / `assistant_*`, keeper's six-phase cycle);
- `pes-re-tactics-clock.md` (54 Hz logic tick, match-clock percentage-of-1920 model).

Scratch (never in the repo): `<scratch>/SetPieceRefRE/` holding `bin/*.bin` (the extracted
constant archives), `wes.py` (WESYS parser), `edis.py` (on-disk exe disassembler), `gas.py`,
`thunks.py`, `dec/*.dis` and `refl_reg.json`.

---

## 0. Status per priority item

| # | Item | Status | Where |
|---|---|---|---|
| 1 | Set-piece state machines | **partly done** — the per-restart constants and the "step" machinery are located and the `RestartStep` enum is fully decoded (§1.9); the phase FSM itself is still not decompiled, and the shipped per-restart durations remain unread because the tactics JSON is not in the VFS (§1.11, §7.1) | §2, §3 |
| 2 | Placement and marking | **partly done** — the position tables are located and their object layout is decoded (`positionCK.o` 368 B, `positionPK.o`) and the defence-mark/lane fields are named | §4 |
| 3 | Officials | **open** — clip families already mapped by the sibling report; the referee/linesman/fourth-official *placement* code not yet decompiled | §5 |
| 4 | Where the values live | **done** — every archive and object enumerated, the field-name ↔ offset maps recovered from the exe's own deserializers; `penaltykick.o`'s shipped values decoded (§1.6, §3.5) and the `Tactics.bin`/`TacticsFormation.bin` container arithmetic closed (§1.11) | §1 |
| 5 | GF divergences | **draft** | §6 |
| 6 | Open items | §7 (items 1, 3, 7 from the previous revision are now closed; §7 item 1 stays open only for the un-shipped durations) |

---

## 1. Where the values live (OBS, runtime sources)

### 1.1 The WESYS constant archives

All are read at runtime from the install by the same loader (the repo's
`src/pes/uniform.cpp::ParseUniformTable` already parses them: 16-byte WESYS header, zlib from
+0x10, then `{u32 count, u32 tableOffset}` and `count × {u32 offset, u32 size, u32 nameOffset}`
records; objects are named `<name>.o`):

| Archive | VFS path | Objects used here |
|---|---|---|
| `constant_match.bin` | `common/match/constant/constant_match.bin` (4817 B) | `throwin.o` 144 B, `setplayGuideCommon.o` 32, `setplayGuideCornerKick.o`/`setplayGuideGoalKick.o`/`setplayGuideFreeKickNear|Middle|Far.o` 176 each, `cameraSetplay.o` 80 |
| `constant_player.bin` | `common/match/constant/constant_player.bin` (40643 B) | `freekick.o` 16, `goalKick.o` 32, `penaltykick.o` 80, `ballplayerSetplay.o` 64, `ballplayerGk.o` 96 |
| `constant_positionCK.bin` | `common/match/constant/constant_positionCK.bin` (775 B) | `positionCK.o` + `positionCK_4_1 … positionCK_6_3` (10 × 368 B) — the corner-kick position table, one per formation family |
| `constant_positionPK.bin` | `common/match/constant/constant_positionPK.bin` (645 B) | `positionNone.o`, `positionPK_2 … positionPK_5` (5 × 368 B) — the penalty-kick position table |
- `constant_team.bin` | `common/match/constant/constant_team.bin` (1354 B) | `basePosition.o` 864, `defenceMark.o` 320, `support.o`, `defence.o`, `pairAnime.o`, `selectorVision.o`, … |

### 1.8 The animation-action class taxonomy (OBS — RTTI names + class-info records)

Every set-piece phase is a C++ class in `action@anime@match` (the object that drives one
player's clip). Recovered by decoding the MSVC RTTI cluster in `.rdata` (`COL` records:
`{u32 sig=1, u32 off, u32 cd, u32 pTDrva}` at ~0x142c9b000–0x142ca5000, type-descriptor names at
0x14361aa00–0x143622680) and the per-class info record
(`{u32 td, u32 n, u32 0, u32 −1, u32 0, u32 size=0x40, u32 vtableRva, u32 0,u32 0,u32 0}`).

Kickoff / restart base classes: `Base@action@anime@match`, `Restart@action@anime@match`,
`KickoffKicker`, `KickoffKickerLoop`, `KickoffReceiver`, `KickoffOther`, `QuickRestartReady`,
`QuickRestartKick`.

Throw-in: `ThrowinLoop`, `ThrowinNormal`, `ThrowinLong`, `SeamlessThrowin`
(plus `ActionPassSupportThrowIn`, `ActionThrowInRotate`, `ActionSeamlessThrowInMove`,
`PassGetRouteThrowIn`, `ThinkUnitThrowinBodyAngleRotation`).

Goal kick: `GoalKickStandby`, `GoalKick`, `GoalKickShortPass`, `GoalKickLongPass`
(+ `ThinkUnitGoalkickPassSupport`, `ActionGoalKickLong`, `ObserverGoalKickKind`).

Corner: `CornerKickLoop`, `CornerKick` (+ `PostManLoop` = the
`corner_defence_idle → postman_loop` family of the sibling report), `ThinkUnitShortCorner`,
`ThinkUnitCornerKickTactics`, `ThinkUnitCornerKickDefenceType`, `ActionShortCornerMove`,
`ActionCornerKick`.

Free kick: `FreeKickLoop`, `FreeKick`, `FreeKick2ndLoop`, `FreeKick2ndCede`,
`FreeKick2ndPassToKicker` (+ `ThinkUnitFreekickTypeChange`, `…KeeperMove`,
`…PositionChange`, `…Tactics`, `…WallJump/NotJump/Press`, `ObserverFreeKickKind`,
`ObserverFreeKickWall`, `ActionFreeKick`, `ActionFreeKickLong`, `ActionFreeKick2ndPass`,
`ActionFreeKickCede`, `ActionKeeperMoveFreeKick`).

Wall: `WallBase`, `WallLoop`, `WallJump`, `WallNotJump`, `WallRunOut`, `WallNotRunOut`
(+ `ActionWallJump`, `ActionWallPress`).

Penalty: `PenaltyKickLoop`, `PenaltyKick` (+ `ThinkUnitPenaltykick`, `…Guide`,
`KeeperPenaltykickMove`, `…Action`, `…Layer`, `ActionPenaltyKick`, `ActionPenaltyKickAfter`,
`ActionKeeperMovePenaltyKick`, `ObserverPenaltyKick`).

Keeper distribution / drop ball: `PuntKick`, `Throw`, `PickupBall`, `DropBall`, `SeeOff`,
`PreSaving`, `SavingBase`, `AfterCatchBase`, `Catch`, `Punch`, `Deflect`, `ScoopOut`,
`SnapUnder` (namespace `goal_keeper@action@anime@match`) plus
`ThinkUnitKeeperDropBall`, `ActionKeeperDropBall`.

Officials (namespace `referee@match` and `action@anime@match`):
`Referee@referee@match`, `RefereeChief`, `RefereeLinesman`, `RefereeActionBasePosition`,
`RefereeActionOutofPlay`, `Referee@action@anime@match`, `Judge@action@anime@match`,
`LinesmanMove`, `LinesmanAction`.

**Read-out recipe (OBS):** to get a class's method list, resolve its TD RVA from the enum name
table at `0x14344a098` (qword `char*` per name), scan `.rdata` for
`{u32 td, u32 n, u32 0, u32 0xffffffff, u32 0, u32 size, u32 vtableRva, …}`, then read the
vtable block at `vtableRva` as `{u32 0, u32 1, u32 count, u32 entriesRva}` and the entries as
RVAs of the interface records. The Fox dispatch is through **interface records
(`{u32 td, u32 n, …}`) plus a runtime `RetryWork` struct**, not a static C++ vtable, so method
addresses are obtained by hash lookup at runtime — INF (med): the `n` field is the method count
(`Referee@referee@match` n=5, `RefereeChief` n=7, `RefereeLinesman` n=7,
`ThrowinLoop` n=4, `WallLoop` n=4, `SeamlessThrowin` n=4, `FreeKickLoop` n=3).

### 1.9 The restart enums (OBS — value-pointer arrays in `.rdata` at 0x14344a918–0x14344a988; value strings in the pool at 0x1425db3a0)

`RestartKind` (`RestartKind` name string @0x1425d3388; its value-pointer array is at
0x14344a918 (4 entries) and 0x14344a940 (9 entries), split by NULL separators — 13 value pointers
in the table below, numbered 0–15 as in the original survey):

| # | value | # | value |
|---|---|---|---|
| 0 | `RESTART_KIND_NONE` | 8 | `LINEOUT` |
| 1 | `THROW_IN` | 9 | `ON_PITCH` |
| 2 | `GOAL_KICK` | 10 | `FOUL` |
| 3 | `CORNER_KICK` | 11 | `STARTING_MEMBER` |
| 4 | `HALFSTART` | 12 | `TRAINING` |
| 5 | `HALFEND` | 13 | `CAPTAIN` |
| 6 | `FORFEITED` | 14 | `STEPUP` |
| 7 | `GOAL` | 15 | `PENALTY_SHOOTOUT` |

`FutureRestartKind` (@0x1425d3398) and `RestartStep` (@0x1425d3f70) each have their own
value-pointer array in `.rdata`. Their name strings live in **two** adjacent name tables in the
reflector blob: the shorter one at **0x14344a098** (where `FoulKind` is 0, `RecordKind` is 3 and
`RestartStep` is **147**) and the longer one at **0x143449fb0** (where `RestartKind` is 26,
`FutureRestartKind` 27, `RestartStep` **176**). Related enum names in the same blob include
`FreeKickArea`, `BallTouchKind`, `PassKind`, `PassKindEx`, `ShootType`, `GoalKeeperFlag`,
`ZwideLine`, `Axis`, `FoulKind`, `FoulJudgeKind`, `FoulDemoType`, `FoulSituation`,
`FoulActionKind`, `AdvantageKind`, `Caution` (=`CautionKind`), `PenaltyArea`,
`CheckPenaltyAreaPlayerNum`, `CheckAroundPenaltyAreaPlayerNum`, `CheckNearPenaltyAreaLine`,
`CheckConditionForFoul`, `CheckFoulKind`, `CheckFoulKindDetail`, `CheckFoulJudgementKind`,
`CheckDemoTypeFoulJudgement`, `isFoulEnd`, `CheckAdvantageData`, `CheckRestartStep`,
`CheckRestartFkKind`, `CheckRestartKindHistry`, `CheckBallTouchRestartKind`.

**`RestartStep` — 31 values, OBS.** The value-pointer array starts at **0x14344c3e8** (the first
entry points at the string `GOAL` @0x1425d6dac) and runs to (and including) the `NONE` at
**0x14344c4d8**; the pointer before that array (0x14344c3e0 = `MOVE_B`, @0x1425d5e74) belongs to the
preceding enum. Order as stored:

| # | value | # | value | # | value |
|---|---|---|---|---|---|
| 0 | `GOAL` | 11 | `KICKOFF_GOAL` | 22 | `LINEOUT_THROW` |
| 1 | `INPLAY` | 12 | `OUTPLAY` | 23 | `SETPLAY` |
| 2 | `START_HALF` | 13 | `END_HALF` | 24 | `SET_GK` |
| 3 | `CK` | 14 | `HALFTIME` | 25 | `SET_CK` |
| 4 | `GK` | 15 | `END_MATCH` | 26 | `SET_THROW` |
| 5 | `THROW` | 16 | `GOAL` | 27 | `SET_FK` |
| 6 | `FK` | 17 | `FOUL` | 28 | `SET_PK` |
| 7 | `PK` | 18 | `LINEOUT` | 29 | `SET_KICKOFF` |
| 8 | `KICKOFF` | 19 | `LINEOUT_CK` | 30 | `SET_KICKOFF_HALF` |
| 9 | `KICKOFF_HALF` | 20 | `LINEOUT_GK` | 31* | `SET_KICKOFF_GOAL` |
| 10 | — | 21 | — | 32* | `NONE` |

(\* — the array is 31 words, so the `SET_KICKOFF_GOAL`/`NONE` pair is the 30th and 31st entry; the
table above numbers them 31/32 for readability. The exact 31-entry list in address order is:
`GOAL, INPLAY, START_HALF, CK, GK, THROW, FK, PK, KICKOFF, KICKOFF_HALF, KICKOFF_GOAL, OUTPLAY,
END_HALF, HALFTIME, END_MATCH, GOAL, FOUL, LINEOUT, LINEOUT_CK, LINEOUT_GK, LINEOUT_THROW,
SETPLAY, SET_GK, SET_CK, SET_THROW, SET_FK, SET_PK, SET_KICKOFF, SET_KICKOFF_HALF,
SET_KICKOFF_GOAL, NONE`.)

INF (high): the first 15 values are the **live** restart steps and the next 15 the **set-play
(offered) steps**; `NONE` (the last entry) is the idle value. The names are the same tokens the
phase FSM (§2/§3) switches on, so GF's restart state machine should be keyed on these exact strings.

INF (high): `RestartStep` is the per-phase counter inside a restart
(`restartPosNo` / `restartPosTable` @0x142b4f938/8/58 are its position-table indices) — the
state variable the engine needs to mirror.

### 1.10 Complete clip inventory (OBS — the 4,389-name table at 0x14351e590)

Motion numbers are the 13-bit `motionNo` of the sibling report.

**Referee (21 clips, m2959–m2980):** `referee_1_0_000_hand_low_point_000`,
`referee_1_0_000_hand_middle_point_000`, `referee_1_0_f060_hand_low_point_f060`,
`referee_1_0_f060_hand_middle_point_f060`, `referee_1_0_f150_hand_middle_point_f150`,
`referee_2_2_000_whistle`, `referee_3_0_000_hand_low_point_000`,
`referee_3_0_000_hand_middle_point_000`, `referee_3_0_f060_hand_low_point_f060`,
`referee_3_0_f060_hand_middle_point_f060`, `referee_3_0_f150_hand_middle_point_f150`,
`referee_advantage_1_1_jog`, `referee_advantage_3_3`,
`referee_game_end_1_0_hand_up_whistle`, `referee_hand_up_0`, `referee_hand_up_0_060`,
`referee_hand_up_0_f060`, `referee_hand_up_0_hand_down`, `referee_kickoff_start_0`,
`referee_stand_0`, `referee_stand_0_hand_up_whistle`, `referee_stand_0_whistle`.
(The `judge_*` names @0x1426010f8 / 0x142601a40 / 0x142601b70 are the **AI-situation** names
from a separate table, not clip names — the referee's *clips* are `referee_*`.)

**Assistants / linesmen (17 clips, m0–m16):** `assistant_3_0_090_flag_gk`,
`assistant_3_0_090_flag_middle_point_000`, `assistant_3_0_090_flag_up`,
`assistant_3_0_f090_flag_gk`, `assistant_3_0_f090_flag_middle_point_f180`,
`assistant_3_0_f090_flag_up`, `assistant_flag_up_0`, `assistant_flag_up_0_flag_ck`,
`assistant_flag_up_0_flag_down`, `assistant_flag_up_0_offside_far/middle/near`,
`assistant_flag_up_0_wave`, `assistant_idle_0_flag_gk`, `assistant_idle_0_flag_up`,
`assistant_idle_0_point_f090`, `assistant_stand_0`.

**Flag-hand layer (2 + ~50 `layer_*`):** `layer_assistant_flag_hand_R` (m2785),
`layer_assistant_flag_switch_R_L` (m2786), plus the `layer_*` coaching/appeal family
(`layer_gk_free_instruction` m4018, `layer_gk_free_thrashHandNear` m4351,
`layer_gk_free_BackLineInstruct_Bothhand(_2)` m4275/6, `layer_gkcoaching_high_shout`,
`layer_gkcoaching_mid_point`, `layer_notouch_appeal`, `layer_godhand`,
`layer_wipe_sweat`, `layer_push/pushaside/jostle/touch_*`).

**Kickoff (15):** `kickoff_kicker_pass`, `kickoff_kicker_stand_pass`, `kickoff_look_around`,
`kickoff_loop_jump`, `kickoff_loop_kicker`, `kickoff_loop_kicker_stand`,
`kickoff_loop_one_foot`, `kickoff_loop_shake`, `kickoff_loop_shake45`,
`kickoff_loop_standstill`, `kickoff_loop_stretch`, `kickoff_loop_waist`,
`kickoff_loop_walk_back_front`, `kickoff_loop_walk_front_back`, `kickoff_loop_walk_side`.

**Throw-in (16, m3316–m3332):** `throwin_loop`, `throwin_pickupball_000/090/180`,
`throwin_release_idle_000_long/mid/short`, `throwin_release_run_000_mid/short`,
`throwin_release_run_067_mid/short`, `throwin_run_3_3_000_onehand_ver21`,
`throwin_seamless_liftupball`, `throwin_slide_1_1_090/f090_onehand_ver21`,
`throwin_turn_067_loop`.

**Goal kick (keeper, 6, m1709–m1714):** `gkgoalkick_idle_0_0`,
`gkgoalkick_inside_0_0_000/045/f045`, `gkgoalkick_instep_0_0`, `gkgoalkick_instep_0_0_f045`
(plus `goalkick_idle_l/r`, `goalkick_inside_l/r`, `goalkick_instep_l/r`,
`goalkick_coaching_up/lower_l/r` @0x1425ffd98 for the outfield coaching waiters).

**Corner (2 + wall):** `postman_loop` (m2934) — the defending-corner idle that the sibling
report ties to `corner_defence_idle`; `fk_through` etc.

**Free kick (34, m1233–m1266):** `fk_idle`, `fk_idle2`, `fk_idle_bale`,
`fk_idle_beckham_2002`, `fk_idle_cristiano`, `fk_idle_dummy_13/14/15/16`, `fk_idle_honda`,
`fk_idle_jadson`, `fk_idle_reus`, `fk_idle_ronaldinho_2`, `fk_idle_sub`,
`fk_kick_short_135`, `fk_kick_short_f045`, `fk_runup`, `fk_runup_bale`, `fk_runup_beckham`,
`fk_runup_beckham_2002`, `fk_runup_cristiano`, `fk_runup_dummy_13/14/15/16`, `fk_runup_honda`,
`fk_runup_jadson`, `fk_runup_pass`, `fk_runup_reus`, `fk_runup_robecal`,
`fk_runup_ronaldinho`, `fk_runup_ronaldinho_2`, `fk_runup_sub`, `fk_side_L`, `fk_side_R`,
`fk_through`. Named run-ups exist per player (INF: selected by a per-player "motion" byte —
the in-match block's 0x37..0x3B motion fields, cf. `pes-re-matchlogic.md` §1.3).

**Penalty (29, m2908–m2933 + m4384/5):** `pk_enclose_ashibumi_loop`,
`pk_enclose_harai_loop`, `pk_enclose_hiza_hiraki_loop`, `pk_enclose_hiza_loop`,
`pk_enclose_kamae_loop`, `pk_idle`, `pk_idle_2`, `pk_idle_cristiano`, `pk_idle_dummy_8`,
`pk_idle_lewandowski`, `pk_idle_neymar`, `pk_idle_pogba`, `pk_idle_ronaldinho`,
`pk_idle_ronaldinho_2`, `pk_kamae_loop_001..005`, `pk_runup`, `pk_runup_2`,
`pk_runup_cristiano`, `pk_runup_dummy_8`, `pk_runup_lewandowski`, `pk_runup_neymar`,
`pk_runup_pogba`, `pk_runup_ronaldinho`, `pk_runup_ronaldinho_2`.

**Wall (12, m3637–m3648):** `wall_idle`, `wall_idle2`, `wall_idle3`, `wall_jump_cancel`,
`wall_jump_cancel2`, `wall_jump_cancel3`, `wall_jump_high`, `wall_jump_high2`,
`wall_jump_high3`, `wall_jump_high4`, `wall_runup`, `wall_runup_cancel`.

INF (high): the `fk_*`/`pk_*`/`wall_*` **idle** clips are the "loop" phase and the
**runup** clips the "approach" phase; `pk_enclose_*` are the keeper's pre-kick rituals.

### 1.11 The tactics field map (OBS — 297 fields, offsets into the per-team tactics struct)

Recovered from the tactics deserializer `FUN_141e79ce0` @0x141e79ce0 (body 0x1e7bb64) and the
second one reachable through it: each field is
`FUN_1403842a0(node, name); FUN_140384750|810|6d0 → *(float|u32|u8*)(tactics + off)`.
Struct size ≈ **0x368** (highest field `xposiRateCustom` u8 at +0x364).
Reader: `{char* name, offset, type}` table in `<scratch>/SetPieceRefRE/tactics_fields.json`.

Set-piece-relevant subset (the rest is in the scratch JSON):

| off | type | field |
|---|---|---|
| +0x008 | f32 | `addAngleKickOff` — extra angle applied to the kick-off kick direction |
| +0x028 | f32 | `adjustDfLineWidth_GoalKick` — how much the defensive line widens at a goal kick |
| +0x02c | f32 | `adjustDistX_Throwin_CB` — CB moves this far (m) from the throw-in point |
| +0x050 | u8 | `adjustSetplay` — team-wide set-piece aggressiveness flag |
| +0x064 | f32 | `adjustSupportDist_FreeKick` |
| +0x078 | f32 | `adjustX_FreeKick`, +0x08c `adjustZ_FreeKick` — FK defensive-line shift |
| +0x084 | u32 | **`cornerKickTime`** — corner-kick duration (ticks, §3) |
| +0x0a4 | f32 | `baseCloseZRate_FreeKick`, +0xa8 `baseDist_FreeKick` |
| +0x0e8 | f32 | `closeZRateThrowin` — throw-in pressing closeness |
| +0x0ec | u32 | **`cornerKickOffencePlayerNumber`** — how many attackers go up for a corner |
| +0x0f0 | u32 | **`cornerKickType`** — corner delivery type selector |
| +0x0f4 | f32 | **`cornerkickDefenceMFSide`** — which flank the defending MF covers |
| +0x0f8 | f32 | **`cornerkickDefenceMFWidth`** |
| +0x15c | f32 | `dfLineRate_goalKick` — defensive line X-rate at a goal kick |
| +0x16c | f32 | **`dfLineWidth_Corner`** — defending-line width at a corner |
| +0x17c/0x180 | f32 | `diffRestartX_End` / `diffRestartX_Start` — restart-position difficulty band |
| +0x194/0x198/0x19c | f32 | `freeKickAdjustRateX_MF` / `freeKickAdjustRateZ` / `…Z_FW` |
| +0x1a0 | u32 | `freekickAttackAddNum` — extra attackers added to the FK wall rush |
| +0x1a4/0x1a8/0x1ac | u32 | `freekickAttackForce0/1/2` — three force levels of the FK press |
| +0x1f8 | f32 | **`gksPenaltyLineRateX_DF`** — GK's X-position as a rate of the penalty line |
| +0x214 | f32 | **`kickoffDfLineX`** — kick-off defensive-line X |
| +0x21c/0x220/0x224 | f32 | `lengthDf` / `_Retreat` / `_SuperRetreat` |
| +0x24c | f32 | **`margin_restartX`** — the X margin that turns a ball position into a restart point |
| +0x258 | f32 | `maxCloseXRate_FreeKick` |
| +0x25c | f32 | `maxWaitTime` — longest a team waits before taking a restart |
| +0x284/0x288/0x28c | f32 | `minWidth` / `minWidth_GoalKick` / `minWidth_stratagy_defensive` |
| +0x2c8 | f32 | `rangeX_Throwin_CB` |
| +0x2d0 | f32 | `setPlayDeclineRate` — rate at which a set-piece offer decays |
| +0x2d4/0x2d8 | f32 | `setplayToInplayHeightMax` / `…Min` — ball height at which the restart becomes live |
| +0x2f4/0x2f8 | f32 | `slideJudgeWidthBase` / `slideJudgeWidthMargin` |
| +0x328 | f32 | `supportDist_FreeKick` |
| +0x340/0x344 | f32 | **`throwinLengthDf` / `throwinWidthZ`** — the defending line's throw-in offset |
| +0x348/0x34c | u32 | `transitionOpenZ` / `transitionPriorityX` |
| +0x350/0x354 | u32 | `transitionSec` / `transitionSmooth` |

Other map entries that the engine needs for placement: `gksBaseX` (+0x1e8),
`gksWidthZ` (+0x200), `gksAdjustRateX/Z_DF/FW/MF` (+0x1d8..0x1e4), `gksHalfLineRateX_SB`
(+0x1f4), `gksSideLineRateZ_SB` (+0x1fc), the mirror `gkl*` block (+0x1b4..0x1d4),
`dfLine*` (+0x13c..0x158), `dfLineWidth_3/4/5` (+0x160/0x164/0x168),
`dfUserPositionAdjustEnable/Rate/Time` (+0x170/0x174/0x178), `zoneMarkX/ZLimit(_BoxToBox)`
(+0x124..0x130), `remainMarkX/ZLimit(_BoxToBox)` (+0xf4..0x100),
`markContinueDist(_BoxToBox)` (+0x0bc/0x0c0), `needMarkAngleBase` (+0x0c8),
`needMarkDistGKShort` (+0x0cc), `needMarkOverLastLineDist` (+0xd0),
`releaseCheckTarget*` (+0xe0..0xf0), `closeRate_MF_adjustX*` (+0xdc/0xe0),
`defenceCompact` (+0x104), `defenceFormationTest1/2` (+0x108/0x109),
`dfCoverAdjustX/Enable`, `dfForceAverageZ` (+0x134..0x136), `dfGroupRate` (+0x138),
`dfAttackWidthBack/Front/MarginBack/MarginFront` (+0x120..0x130),
`pushUpSide/Back` (+0x2c0/0x2c4), `mfPushUpRate(_offencive)` (+0x270/0x274),
`sideBackPushUpRate` (+0x2dc), `backOffsideLine` (+0x098) etc.

**Where the values come from at run time:** `common/etc/pesdb/Tactics.bin` (15,912 B) and
`common/etc/pesdb/TacticsFormation.bin` (525,096 B), mounted under `cpk_dat/common/etc/pesdb`
(the exe's db table list at 0x1434d66e8: `Tactics.bin`, `TacticsFormation.bin`, `Team.bin`,
`PlayerWeekly.bin`, `TeamWeekly.bin`).

**Container arithmetic — OBS this session.** The db loader is the generic
`FUN_14125f310` @0x14125f310 (bounded `cmp $0x1c,%edx`; record stride table at **0x1428bb100**
and name table at **0x1434d6630**, both indexed by the table id — `Tactics.bin` is index **23**
with stride **12**, `TacticsFormation.bin` index 24 stride 12). It path-joins
`cpk_dat/common/etc/pesdb` (@0x1428baf40) with the table name, loads the file into a vector with
`0x1415a1708` and hands the vector to the per-table parse (`0x14125f670` → `0x1415a1708`
`divq`/realloc loop). The shipped bytes were decoded:

- `Tactics.bin` = **1,326 × 12 B** `{u32 id, u32 mid, u32 hash}`; ids run 0x1af6 → 0x400028a0
  (the high u16 is a table family), the `mid` values always come in **equal pairs** (663 distinct
  values, 1…71212) and the third word is **not** a `StrCode32` (verified: a Python port of
  `pes::StrCode32` — `MOTION`→`0x08908348`, `RIG_ROOT`→`0xd3c3ff90`, `sk_root_hip`→`0x3ca4c491`
  — does not preimage any of the 1,314 distinct hashes, and a length ≤ 3 brute force over
  `[A-Za-z0-9_]` finds no match). INF (med): the third word is a checksum of the record's payload.
- `TacticsFormation.bin` = **1,326 × 396 B** payload groups, each holding **33 × 12 B** records
  `{u32 id, u32 slot, u32 packed}`. Every group contains **three identical 11-record segments**
  that differ **only in bits 20–23** (`0x03403`, `0x103403`, `0x203403`), i.e. the payload is
  stored three times with a segment tag and the low 20 bits are the value. The remaining 20 bits
  are a 3-byte word `(b2, b1, b0)` where `b2 = 0…10` is the record's ordinal and `(b1, b0)` is a
  small packed selector (across the first 20 groups `b0` ∈ {3, 11, 13, 25, 43}, `b1` ∈ {12…92}).
  The `slot` word runs 0…12 with duplicates (e.g. `1, 1`, `3, 2`).

**What is still open (§7 item 1):** the 20-bit packed words are a **presence/bitmask encoding**,
not the tactic values — 11 packed words per id cannot express the 297-field / 0x368-byte tactics
struct. The numeric values are therefore not in these two files. They come from the per-team
JSON the tactics deserializers read (`FUN_141e79ce0` @0x141e79ce0 with 220 `lea rdx,[rip+X]`
field-name lookups, and `FUN_141e71120` @0x141e71120 — the object is 0x368 bytes and
`cornerKickTime` is a u32 at its **+0x84**, read once, with its only lea reference at
0x141e714fe). **The `cornerKickTime` / `penaltyKickTime` shipped numbers therefore remain
unread** — the source JSON is not shipped in the VFS (searched: no `DevelopData/**` path and no
tactics JSON anywhere in the 121,075 mounted paths), so the defaults live only in the game's
develop data or in the live save.

---

## 2. The kickoff state machine

**Trigger (INF, high):** `RestartKind = HALFSTART` (`RESTARTKind` value 4) / `HALFEND` (5) /
`GOAL` (7) — the same `RestartKind` value is set by the match restart logic and the
`Kickoff*` action classes are selected from it (`ThinkUnitKickoff` @pad, `ThinkUnitKickOffChange`).

Phases (INF, med — class list from §1.8, clip assignment from §1.10 and the sibling report):

| step | class | clip | exit |
|---|---|---|---|
| S0 | — | `referee_kickoff_start_0` (m2977) | referee's whistle arm |
| S1 | `KickoffOther` | `kickoff_loop_*` (any of the 12 waiters' loops) | all players in their kick-off position |
| S2 | `KickoffKickerLoop` | `kickoff_loop_kicker` / `_stand` (m2298/9) | the kick-off taker has the ball |
| S3 | `KickoffKicker` | `kickoff_kicker_pass` / `kickoff_kicker_stand_pass` (m2294/5) | the ball is kicked (touch) |
| S4 | `KickoffReceiver` | (in-play) | normal play resumes |
| quick | `QuickRestartReady` → `QuickRestartKick` | `quick_restart*` @0x142b81798 (0/45/90/135/180/225/270/315 + `_set_loop`, `_step*`) | an optional fast-restart path |

INF (high): `KickoffReceiver` n=3 methods and `KickoffOther` n=2 — the receiver has its own
phase set (position → turn → receive).

Placement (OBS offsets, INF semantics):
- the kick-off position set is chosen from `basePosition.o` (864 B, `constant_team.bin`)
  with the defending line's X at `kickoffDfLineX` (+0x214);
- the kick-off spot is the centre spot; `addAngleKickOff` (+0x008) jitters the initial pass
  direction;
- the previous report's `positionKickOff/positionKickOff_4_1_2_3_offence.fox` etc. @0x142597100
  are the **formation-specific** kick-off position files (4-1-2-3 and 4-2-3-1, offence and
  defence variants) — OBS as strings, INF (med) that the loader keys on the team's formation.

Duration: **not closed** (see §7). No kick-off timer field exists in the tactics map; the
`kickoff_loop_*` clip lengths (from the mtar, 30 fps) bound the wait, and
`maxWaitTime` (+0x25c) bounds how long a team may hold a restart.

---

## 3. The other restarts: state machines, constants and durations

### 3.1 Throw-in

**Trigger (OBS enum):** `RestartKind = THROW_IN` (1). Related reflection names: `CheckBallTouchRestartKind`,
`MODE_THROW_IN` @0x14259ddf0, `THROW_IN` @0x14259df90, `THROWIN_ADMIT` @0x1425d7110,
`SET_THROW` @0x1425d7038, `STOP_THROW_IN` @0x1425d7c68.

Phases (class names from §1.8, OBS; semantics INF):

| step | class | clip | exit |
|---|---|---|---|
| S0 | — | ball placed at the touchline point | position computed from the crossing point |
| S1 | `ThrowinLoop` (n=4) | `throwin_loop` (m3316) | taker arrives |
| S2 | `ThrowinLoop` | `throwin_turn_067_loop` (m3332) | taker faces the pitch |
| S3 | `ThrowinLoop` | `throwin_pickupball_000/090/180` (m3317–9) | ball picked up |
| S4 | `ThrowinNormal` (n=3) / `ThrowinLong` (n=3) | `throwin_release_idle_000_{short,mid,long}` (m3320–2) / `throwin_release_run_000_{mid,short}` (m3323–4) / `throwin_release_run_067_*` (m3325–6) | ball released |
| S5 | `ThrowinNormal`/`ThrowinLong` | `throwin_run_3_3_000_onehand_ver21` (m3328), `throwin_slide_1_1_090/f090_onehand_ver21` (m3330/1) | one-handed/slide variants |
| seamless | `SeamlessThrowin` (n=4) | `throwin_seamless_liftupball` (m3329) | below `seamlessThrowin.possibleDist` |

**Constants (`throwin.o`, §1.3, OBS values):** `move.beginAngle` 0.0, `move` mode 1,
`longThrowSupport.enable` 1 / `adjustRate` 0.0, `mode` 0, `seamlessThrowin.dist` 0.5 m,
`search.distFarLongPara` 80.0, `search.distFarNormal` 3.0, `search.distMiddle` 0.0,
`search.distNear` 0.0, `speed.gageMiddle` 5.0, `speed.adjustDistMin` 3.0, plus 15.0 / 5.0 /
0.6 / 20.0 / 10.0 / 10.0 / 50.0 / 35.0 in the tail, `speed.baseMaxKph` 32.0 (inferred),
`speed.baseMinKph` 25.0 (inferred).
Tactics-side (OBS offsets): `closeZRateThrowin` +0xe8, `throwinLengthDf` +0x340,
`throwinWidthZ` +0x344, `adjustDistX_Throwin_CB` +0x2c, `rangeX_Throwin_CB` +0x2c8,
`margin_restartX` +0x24c.

INF (high): the throw-in **placement rule** is: take the point where the ball crossed the
touchline, then the defending line drops back by `throwinLengthDf` (m) and compresses toward the
centre by `throwinWidthZ`; the nearest CB is offset by `adjustDistX_Throwin_CB` limited by
`rangeX_Throwin_CB`.

**Duration: NOT closed.** The `throwin_release_*` clips are the only bound; there is no
throw-in timer in the tactics map (OBS absence).

### 3.2 Goal kick

**Trigger:** `RestartKind = GOAL_KICK` (2). Classes `GoalKickStandby` (n=3) → `GoalKick` (n=2) →
`GoalKickShortPass` (n=3) / `GoalKickLongPass` (n=3).

Clips: `gkgoalkick_idle_0_0` (m1709) wait, `gkgoalkick_inside_0_0_000/045/f045` (m1710–2)
and `gkgoalkick_instep_0_0[_f045]` (m1713/4) the kick, `goalkick_coaching_up/lower_l/r`
@0x1425ffd98 for the waiting outfielders.

**Constants (`goalKick.o`, 32 B, OBS layout §1.6 and shipped values):**
`angleBase` 0.0, `angleDelta` 0.0, `distMaxBase` 5.0, `distMaxDelta` 3.0,
`distMinBase` 32.0, `distMinDelta` 25.0, `freekickDebug` 0, `goalKickMode` 0,
`thinkDistFar` / `thinkDistMiddle` / `thinkDistNear`.
INF (med): the GK's target distance is `distMinBase …` for a short kick and
`distMaxBase + random·distMaxDelta` for a long one; `angleBase/Delta` jitters the direction.
Tactics (OBS offsets): `dfLineRate_goalKick` +0x15c, `adjustDfLineWidth_GoalKick` +0x028,
`minWidth_GoalKick` +0x288, `gk.*` block for the keeper's own placement
(`limitLineFromPenaltyLine` @0x142b51538/0x142b51660 in `ballplayerGk.o`).

**Duration: NOT closed.**

### 3.3 Corner kick (attacking and defending)

**Trigger:** `RestartKind = CORNER_KICK` (3). Classes `CornerKickLoop` (n=2) → `CornerKick` (n=2);
defending-side `PostManLoop` (n=2) with `postman_loop` (m2934);
`ThinkUnitShortCorner` (n=2), `ThinkUnitCornerKickTactics` (n=2),
`ThinkUnitCornerKickDefenceType` (n=2).

Constants (`setplayGuideCornerKick.o`, 176 B, §1.4, OBS):
`rot.gage` 0.8, `rot.addBackSpin` 0.2, `rot.addHigh` 5.0, `rot.addValue` 14.0, `rot.distMax` 8.0,
`rot.enable` 1, `speed.topSpinRot` 10.0, `speed.addParaRate` 25.0, `speed.base` 0.0,
`kickPowerParameter.max` 99.0, `.min` 40.0, `spin.sideSpin.base` 0.6.

Tactics (OBS offsets): **`cornerKickTime` (u32, +0x084)** — the corner timer;
`cornerKickType` (u32, +0x0f0); `cornerKickOffencePlayerNumber` (u32, +0x0ec) — the number of
attackers in the box; `cornerkickDefenceMFSide` (f32, +0x0f4) and
`cornerkickDefenceMFWidth` (f32, +0x0f8) — the defending midfields' assignment;
`dfLineWidth_Corner` (f32, +0x16c).

Placement: `constant_positionCK.bin` = `positionCK.o` + `positionCK_4_1 … positionCK_6_3`
(10 × 368 B) — the **corner-kick position table, one object per formation family**
(4-1-2-3 / 4-2-3-1 / 4-3-3 / 5-1-2-2 / 5-2-2-1 / 6-2-1-1 …). The on-disk formation
files `positionCK/positionKickOff_4_1_2_3_offence.fox` @0x142597100 are the same data in
source form. INF (med): each 368-byte object is an array of per-slot
`{u32 enable, u32 playerNo, f32 startPosX, f32 startPosZ, …}` (the `positionCK` deserializer at
0x141e7c2a0 begins `playerData.enable`, `playerData.playerNo`, `playerData.startPosX`,
`playerData.startPosZ` — OBS), so the corner positions are **explicit authored coordinates per
slot**, not a computed rule.

**Duration: `cornerKickTime` (u32, tactics **+0x84**, read exactly once at 0x141e714fe into the
0x368-byte tactics struct) is the field, but its shipped number is still unread** — the value is
not in `Tactics.bin`/`TacticsFormation.bin` (see §1.11: both files carry an id/slot/bitmask index,
not the field values) and the tactics JSON that fills it is not in the VFS.

### 3.4 Free kick (attacking and defending)

**Trigger:** `RestartKind = FOUL` (10) plus `CheckRestartFkKind`. Classes `FreeKickLoop` (n=3) →
`FreeKick` (n=2) → `FreeKick2ndLoop` (n=2) / `FreeKick2ndCede` (n=2) /
`FreeKick2ndPassToKicker` (n=2). Wall: `WallLoop`/`WallJump`/`WallNotJump`/`WallRunOut`/
`WallNotRunOut` (all n=4). Keeper: `ThinkUnitFreekickKeeperMove`, `ActionKeeperMoveFreeKick`.

Clips §1.10 (`fk_idle*` wait, `fk_runup*` approach, `fk_kick_short_*` strike, `fk_side_L/R`,
`fk_through`; wall `wall_idle/2/3`, `wall_runup`, `wall_jump_high/mid/cancel*`).

Constants (`freekick.o`, 16 B, §1.6, OBS): `longpassInplayUse` 0,
`longpassSpeedKPHMax` 0.5, `dist` 80.0, `suspendTime` 3.0, `waitTime` 0.0.
**INF (high): `suspendTime` = 3.0 s is the hold between the foul and the restart becoming
playable, and `waitTime` the post-kick pause; `dist` 80.0 m is the "quick restart allowed" cone.**

Guides (`setplayGuide*.o`, §1.4, OBS values): near 0.6/0.2/2.5/4.0/4.0; middle 0.6/0.2/2.5/6.5/5.0;
far 0.6/0.2/2.5/5.0/5.0 with `speed.addParaRate` 45.0 and `kickPowerParameter.max` 55.0 for
all three.

Tactics (OBS offsets): `adjustSupportDist_FreeKick` +0x064, `adjustX_FreeKick` +0x078,
`adjustZ_FreeKick` +0x08c, `baseCloseZRate_FreeKick` +0x0a4, `baseDist_FreeKick` +0x0a8,
`maxCloseXRate_FreeKick` +0x258, `supportDist_FreeKick` +0x328,
`freeKickAdjustRateX_MF` +0x194, `freeKickAdjustRateZ` +0x198, `freeKickAdjustRateZ_FW` +0x19c,
`freekickAttackAddNum` +0x1a0, `freekickAttackForce0/1/2` +0x1a4/8/ac,
`adjustAngle_FreeKickSupport` +0x008, `adjustCloseZRate_FreeKick` +0x014,
`adjustSetplay` +0x050, `setplayToInplayHeight{Min,Max}` +0x2d8/0x2d4,
`setPlayDeclineRate` +0x2d0.

**Duration: `freekick.o.suspendTime` 3.0 s = 162 ticks at 54 Hz is the only closed value**
(OBS value, INF semantics). The rest of the FK phase timing is not closed.

### 3.5 Penalty kick

**Trigger:** `PENALTY_SHOOTOUT` (15) for a shoot-out; in normal play `PENALTY` @0x1425d6db8 and
`PenaltyAreaX/Z` @0x1425d4b18/0x1425d4b28. Classes `PenaltyKickLoop` (n=2) → `PenaltyKick` (n=2);
keeper `ThinkUnitKeeperPenaltykickMove` (n=3) / `…Action` / `…Layer`; `ThinkUnitPenaltykickGuide`
(n=3).

Clips §1.10 (`pk_enclose_*` rituals, `pk_idle*`, `pk_runup*`, `pk_kamae_loop_001..005`).

Constants (`penaltykick.o`, 80 B, §1.6, **OBS values**): the object's data words are at
**+0x08…+0x38** (+0x00/+0x04 are the Fox object header). Shipped:
`shootSpeed.speed` = **64** (u32), `shootSpeed.gageAddKPH` = **0.0**,
`shootSpeed.gageMax` = **50.0**, `shootSpeed.gageMin` = **0.0**,
`shootSpeed.heightAddKPH` = **0.0**, `shootSpeed.minKPH` = **0.0**,
`shootSpeed.paraAddGageMax` = **30.0**, `shootSpeed.paraAddGageMin` = **1.0**,
`targetPoint.paraAddKPH` = **0.2**, `targetPoint.heightAdd` = **10.0**,
`targetPoint.heightAddKeyConfig` = **65.0**, `targetPoint.widthAdd` = **1.0**,
`targetPoint.widthAddKeyConfig` = **0.75**. INF (med): `speed` is an integer rate and
`+0x3c…+0x4c` (10.0, 2.0, 2.0, 2.0, 2.0) is unnamed padding.

Placement: `constant_positionPK.bin` = `positionNone.o` + `positionPK_2/3/4/5.o`
(5 × 368 B) — **the penalty position table, one object per the number of takers** (2,3,4,5),
plus `positionNone`. Same 368-B `{enable, playerNo, startPosX, startPosZ, …}` layout as
`positionCK`. `addPenaltyX`/`addPenaltyZ` (+0x5adf0/+0x5ae00) and `gksPenaltyLineRateX_DF`
(+0x1f8) offset the GK's line.

**Duration: NOT closed.**

### 3.6 Drop ball

**Trigger:** keeper-held dead ball after a stoppage — `RestartKind` = none / the referee's call.
Classes `DropBall` (goal_keeper, n=1) + `ThinkUnitKeeperDropBall` (n=2) +
`ActionKeeperDropBall` (n=2). Clips `gkdropball_0_0` @0x142b7ae58, `gkdropball_3_0` @0x142b80950,
`gkdropball_0_0_fast` @0x142b6ca18, `gkdropball_3_0_fast` @0x142b69b20,
`gkdropball_3_3_fast` @0x142b69ba8.

INF (med): the `_fast` variants are the AI-vs-AI (skip-ceremony) path; plain variants are the
broadcast path. `gksBaseX`/`gksWidthZ` (+0x1e8/+0x200) place the keeper for it.

**Duration: NOT closed.**

---

## 4. Placement and marking

### 4.1 The authored position tables (OBS)

| Archive | Objects | Meaning |
|---|---|---|
| `constant_positionCK.bin` | `positionCK.o` + `positionCK_4_1/4_2/4_3/5_1/5_2/5_3/6_1/6_2/6_3.o`, each **368 B** | corner-kick positions per formation family |
| `constant_positionPK.bin` | `positionNone.o` + `positionPK_2/3/4/5.o`, each **368 B** | penalty positions per taker count |
| `constant_team.bin` | `basePosition.o` 864 B, `defenceMark.o` 320 B, `support.o` 240, `defence.o` 144, `defenceCover.o` 144, `pairAnime.o` 224, `selectorVision.o` 224, `spaceRun.o` 240, `subConcept.o` 208, `combination.o` 288, `diagonalRun.o` 96, `lineBreak.o` 64, `overlap.o` 32, `pullAway.o` 48, `teamId.o` 112 | base positions + marking rules |

`positionCK`'s deserializer (`0x141e7c2a0`) shows the field order **`playerData.enable` (u32),
`playerData.playerNo` (u32), `playerData.startPosX` (f32), `playerData.startPosZ` (f32)** — so a
slot record is `{u32 enable, u32 playerNo, f32 x, f32 z, …}` and 368 B holds ~22 slots.
(OBS for the first four fields; INF for the count.)

INF (high): these are **absolute Fox-space coordinates** (metres, Y up) for each slot; the
engine selects the object by the team's formation (CK) or by how many takers remain (PK) and by
attacking/defending side. The `positionKickOff/positionKickOff_{4_1_2_3,4_2_3_1}_{offence,defence}.fox`
strings @0x142597100 confirm the same scheme for the kick-off.

### 4.2 The marking rules (OBS offsets, INF semantics)

From the tactics map (§1.11) — these are the marking/holding assignments:

- Man-marking: `needMarkAngleBase` (+0x0c8), `needMarkDistGKShort` (+0x0cc),
  `needMarkOverLastLineDist` (+0x0d0), `markContinueDist` (+0x0bc),
  `markContinueDist_BoxToBox` (+0x0c0), `markContinueAddDistDF` (+0x0b8),
  `remainMarkXLimit` (+0x0f4), `remainMarkZLimit` (+0x0fc) and their `_BoxToBox` twins
  (+0x0f8/+0x100).
- Zone marking: `zoneMarkXLimit` (+0x124), `zoneMarkZLimit` (+0x12c) +
  `_BoxToBox` twins (+0x128/+0x130), `zoneZRange` (+0x134), `zoneZRangeAlone` (+0x138),
  `zoneFrontXMf` (+0x120), `checkZoneAreaRate` (+0x070).
- Release/check: `releaseCheckTargetAngle` (+0x0e0),
  `releaseCheckTargetDfLineDist` (+0x0e4), `releaseCheckTargetMarkedAngle` (+0x0e8),
  `releaseCheckTargetMarkedDist` (+0x0ec), `releaseTargetDist` (+0x0f0).
- Compactness: `adjustZCompact` (+0x07c), `adjustZMinCompact` (+0x080),
  `adjustZOneSideCut` (+0x084), `adjustXCompactFW` (+0x06c), `defenceCompact` (+0x104),
  `lowerConnectionParam` (+0x238), `upperConnectionParam` (+0x358).
- The defensive line: `dfLine` (+0x13c), `dfLineBack` (+0x148), `dfLineRate` (+0x158),
  `dfLineWidth_3/4/5` (+0x160/4/8), `dfLineWidth_Corner` (+0x16c),
  `dfLineRate_goalKick` (+0x15c), `adjustDfLineWidth_GoalKick` (+0x028),
  `dfAdjustZ` (+0x10a), `dfAdjustZCheckLen{Max,Min}` (+0x10c/0x110),
  `dfAdjustZCheckWidth{Max,Min}` (+0x114/0x118), `dfAdjustZRateMax` (+0x11c),
  `dfAttackWidth{Back,Front}` (+0x120/0x128), `dfAttackWidthMargin{Back,Front}` (+0x12c/0x130),
  `dfCoverAdjustX` (+0x134), `dfCoverEnable` (+0x135), `dfForceAverageZ` (+0x136),
  `dfGroupRate` (+0x138).
- Set-piece specific: `dfLineWidth_Corner`, `cornerkickDefenceMFSide/Width`,
  `throwinLengthDf/WidthZ`, `adjustDistX_Throwin_CB`/`rangeX_Throwin_CB`,
  `freekickAttackAddNum`/`freekickAttackForce0/1/2`, `gks*/gkl*` keeper blocks.

INF (high): the marking assignment is **computed**, not authored: each defender picks the
nearest attacker inside `needMarkAngleBase`/`needMarkOverLastLineDist`, held while
`markContinueDist` remains satisfied, with the zone limits (`zoneMarkX/ZLimit`) as the
fallback. `defenceMark.o` (320 B, `constant_team.bin`) carries the per-concept weights.

---

## 5. The officials

### 5.1 How many, and which classes (OBS)

Three official classes exist in `referee@match` (RTTI names @0x143622548–0x1436226e0):
`Referee` (n=5 methods), `RefereeChief` (n=7), `RefereeLinesman` (n=7), plus the two action
mixins `RefereeActionBasePosition` (n=2) and `RefereeActionOutofPlay` (n=2), and the anime-side
`Referee`/`Judge`/`LinesmanMove`/`LinesmanAction` in `action@anime@match`.

INF (high): one **chief referee** (`RefereeChief`), **two linesmen/assistant referees**
(`RefereeLinesman`, two instances), and the base `Referee` is the shared behaviour; there is
**no fourth-official class** in the image (OBS absence — the only related name is the
ball-washer prop `ball_washer`/'sweeper' family, §5.3). So PES 2021 creates **3 on-pitch
officials** (1 referee + 2 assistants).

### 5.2 Placement and movement (OBS offsets, INF semantics)

- `RefereeActionBasePosition` = the default in-play placement (the diagonal, ~off the
  dominant side, roughly level with the ball);
- `RefereeActionOutofPlay` = the set-piece placement;
- The assistants carry the **flag line** — the assistant stays on the offside line level with
  the second-last defender: clips `assistant_flag_up_0_offside_{near,middle,far}` (m9–11) are
  the flag raised at three depths, `assistant_3_0_090/f090_flag_{gk,up,middle_point_*}` (m0–5)
  are the 45°-angled runs toward the flag, `assistant_flag_up_0_flag_ck` (m7) the corner flag,
  `assistant_idle_0_flag_up` the wait, `assistant_stand_0` the standing rest.
- `layer_assistant_flag_hand_R` (m2785) / `layer_assistant_flag_switch_R_L` (m2786) are the
  flag-hand swap layers (right/left hand) — OBS names.
- The referee's signalling set: `referee_hand_up_0[_060/_f060/_hand_down]` (m2973–6),
  `referee_1_0_/3_0_hand_{low,middle}_point_*` (m2959–69), `referee_2_2_000_whistle` (m2964),
  `referee_stand_0_whistle` (m2980), `referee_stand_0_hand_up_whistle` (m2979),
  `referee_kickoff_start_0` (m2977), `referee_game_end_1_0_hand_up_whistle` (m2972),
  `referee_advantage_1_1_jog` / `referee_advantage_3_3` (m2970/1).
- Foul/card reflection names (OBS): `FoulKind`, `FoulJudgeKind`, `FoulDemoType`,
  `FoulSituation`, `FoulActionKind`, `AdvantageKind`, `Caution`, `isFoulEnd`,
  `CheckConditionForFoul`, `CheckFoulJudgementKind`, `penaltyCard_{none,y1,r1,y2,y1r1,y3,y2r1}`
  @0x14262a880 / 0x1427c48e8, `status_penalty_yellow/yellowToRed/red` @0x142be0a78,
  `advantageousRate{Classical,Normal,Offensive}` @0x142b5bfb8/e0/0x142b5c000,
  `continueAdvantageDelay`/`continueAdvantagePress` @0x142b51710/28,
  `judge_advantage_1/3` @0x142601050/0x1426010a0.
- The props exist: `whistle_hand.fmdl`, `whistle_mouth.fmdl`, `card_yellow/red.fmdl`,
  `note_yellow/red.fmdl`, `referee_flag.{fmdl,skl}`, `referee_f_close.{fmdl,skl}`,
  `referee_prop.fmdl`, `cap_sleeve_long/short.fmdl` (extracted from the install's
  `common/…/referee` packs; OBS by VFS listing).

INF (med): the whistle/card props are attached by the `Referee*` classes; the flag is a
separate skinned model carried by `RefereeLinesman`.

### 5.3 Officials' kit and id selection

- The kit config is the same `UniformParameter.bin` / `<teamId>_DEF_<n>th_realUni.bin` machinery
  as the players (`common/etc/...`), with the referee-specific appearance records
  (`RefereeAppearance.bin`, `RefereeColor.bin`, `referee_DEF_1/2/5.bin`,
  `referee_EU_1.bin`, `<T>_DEF_1st_realUni.bin` for the officials) — OBS by VFS listing of the
  officials data folder.
- INF (med): the official id is picked from the competition/stadium data
  (`constant_stadium.bin` holds `studiumPositionData.o`, and the match's referee selection is a
  competition-regulation field). **The id-picking rule is still OPEN (§7).**

### 5.4 The ball-washer

INF (low, from the kit-report lineage in this project): PES places a ball-washer/"sweeper"
behind the goal during stoppages; it is a prop (`ball_washer` / `sweeper` family) not a class
in `referee@match`. Not closed (§7).

---

## 6. GF divergences (PES rule → GF behaviour to replace)

Concrete, actionable list (PES values OBS unless marked INF):

1. **Restart kind is one enum with 16 values.** GF should model `RestartKind`
   (NONE/THROW_IN/GOAL_KICK/CORNER_KICK/HALFSTART/HALFEND/FORFEITED/GOAL/LINEOUT/ON_PITCH/
   FOUL/STARTING_MEMBER/TRAINING/CAPTAIN/STEPUP/PENALTY_SHOOTOUT) plus a `RestartStep` counter —
   GF's separate `SetPiece` states (throw-in, goal-kick, corner, free-kick, penalty) with their
   own timings do not map 1:1 (INF, high: GF also has a kickoff state that PES folds into
   `HALFSTART`).
2. **Throw-in placement is a defending-line offset, not a point.** PES drops the defending line
   by `throwinLengthDf` and compresses by `throwinWidthZ` (tactics +0x340/+0x344), and shifts
   the nearest CB by `adjustDistX_Throwin_CB` capped by `rangeX_Throwin_CB` (+0x2c/+0x2c8).
3. **Throw-in seek distances**: `search.distFarLongPara` 80.0 / `distFarNormal` 3.0 /
   `distMiddle` 0 / `distNear` 0 (m), with `seamlessThrowin.dist` 0.5 m as the no-run-up
   threshold and `speed.baseMaxKph` 32 / `baseMinKph` 25 as the throw's speed band.
4. **Goal-kick distance is a base+delta pair**: `distMinBase` 32.0 / `distMaxBase` 5.0 with
   `distMaxDelta` 3.0 / `distMinDelta` 25.0 and `angleBase/Delta` 0 — GF should pick
   `distMaxBase + rnd·distMaxDelta` (long) or `distMinBase…` (short), not a fixed length.
5. **Corner attack numbers are tactics-driven**: `cornerKickOffencePlayerNumber` (u32, +0x0ec),
   `cornerKickType` (+0x0f0), `cornerkickDefenceMFSide` (+0x0f4) and
   `cornerkickDefenceMFWidth` (+0x0f8) decide how many go up and which flank the defending MF
   covers; `dfLineWidth_Corner` (+0x16c) sets the line width.
6. **Corner positions are authored per formation**, 10 objects × 368 B in
   `constant_positionCK.bin`, each `{enable, playerNo, startPosX, startPosZ, …}` — GF must read
   them, not compute them.
7. **Penalty positions are authored per taker count**, `positionPK_2/3/4/5.o` + `positionNone.o`
   in `constant_positionPK.bin`; the GK's line X is `gksPenaltyLineRateX_DF` (+0x1f8) and the
   spot offsets `addPenaltyX`/`addPenaltyZ`.
8. **Free-kick hold is 3.0 s** (`freekick.suspendTime` = 162 ticks) plus `waitTime` 0.0, and the
   quick-restart cone is `dist` 80.0 m with `longpassInplayUse` 0.
9. **Free-kick wall jumping is a first-class state** (`WallLoop/WallJump/WallNotJump/
   WallRunOut/WallNotRunOut`, `wall_jump_high/mid/cancel*`), driven by
   `ThinkUnitFreekickWallJump/NotJump/Press` and `freekickAttackAddNum` /
   `freekickAttackForce0/1/2` — GF has no wall-jump state.
10. **The keeper's set-piece cycle has named phases** (`PuntKick`, `Throw`, `PickupBall`,
    `DropBall`, `SeeOff`) that mirror the sibling report's six-phase keeper cycle; `goalKick.o`'s
    distance rules (§4) replace any fixed punt length.
11. **Officials: 3, not 1+2+1.** `RefereeChief` + 2× `RefereeLinesman` (OBS class inventory; no
    fourth-official class exists). The assistants carry the offside line and a swap-able flag
    hand (`layer_assistant_flag_switch_R_L`).
12. **Officials have a set-piece placement mixin** (`RefereeActionOutofPlay` vs
    `RefereeActionBasePosition`) — GF's referee that only follows the ball needs the
    out-of-play placement added.
13. **The restart-becoming-live rule is a ball-height window**:
    `setplayToInplayHeightMin/Max` (+0x2d8/+0x2d4) — the restart is playable only inside it.
14. **Every set piece has a decline timer** for the AI: `maxWaitTime` (+0x25c) and
    `setPlayDeclineRate` (+0x2d0) — GF should bound how long a team holds a restart.
15. **Kick-off positions come from the formation family**, from
    `positionKickOff_4_1_2_3_{offence,defence}.fox` / `positionKickOff_4_2_3_1_*` @0x142597100 —
    GF's kickoff must select the file matching the team's formation and side, not a single
    generic layout.
16. **`RestartStep` is a 31-value enum** (§1.9): the live steps `GOAL, INPLAY, START_HALF, CK, GK,
    THROW, FK, PK, KICKOFF, KICKOFF_HALF, KICKOFF_GOAL, OUTPLAY, END_HALF, HALFTIME, END_MATCH`,
    the offered/set-play steps `GOAL, FOUL, LINEOUT, LINEOUT_CK, LINEOUT_GK, LINEOUT_THROW,
    SETPLAY, SET_GK, SET_CK, SET_THROW, SET_FK, SET_PK, SET_KICKOFF, SET_KICKOFF_HALF,
    SET_KICKOFF_GOAL`, and `NONE`. GF should model the same split: a live step and a
    "set-play offered" step per restart kind.
17. **`penaltykick.o` shipped values** (§1.6/§3.5) bound the keeper's aim gage
    (`shootSpeed.gageMax` 50.0, `shootSpeed.paraAddGageMax` 30.0, `paraAddGageMin` 1.0,
    `targetPoint.widthAddKeyConfig` 65.0, `heightAddKeyConfig` 65.0) and the target-point jitter
    (`heightAdd` 10.0, `widthAdd` 1.0, `paraAddKPH` 0.2); `shootSpeed.speed` is an integer 64.

---

## 7. Open items and what was tried

1. **The per-restart phase durations in ms/ticks — PARTLY CLOSED (container decoded; values not
   shipped in the VFS).** `cornerKickTime` (u32, tactics +0x084) is read exactly once, at
   **0x141e714fe** inside `FUN_141e71120`, into the 0x368-byte tactics struct. Tried: decompiling
   the db loader — the "loader symbols" 0x1428baee0/0x1428baef0/0x1428baf08 named in last night's
   report are **not functions** but the `.trace` **string** block (`Tactics.bin`,
   `TacticsFormation.bin`, `Team.bin`, `PlayerWeekly.bin`, `TeamWeekly.bin` @0x1428baee0…), and
   the db registry at 0x1434d66e8 is a `char*` table of them. The real generic loader is
   **`FUN_14125f310`** @0x14125f310 with a stride table at **0x1428bb100** (`Tactics.bin` = 12) and
   a name table at **0x1434d6630** (index 23). Results, OBS:
   `Tactics.bin` = 1326 × `{u32 id, u32 mid, u32 hash}` where `mid` always occurs in equal pairs
   and `hash` is **not** a StrCode32 (a Python port of `pes::StrCode32` reproduces
   `MOTION`/`RIG_ROOT`/`sk_root_hip` exactly, so the port is trusted; no ≤ 3-char preimage
   matches); `TacticsFormation.bin` = 1326 × 396 B = 33 × `{u32 id, u32 slot, u32 packed}` per id,
   with **three identical 11-record segments differing only in bits 20–23** and the low 20 bits a
   `(b2, b1, b0)` selector (b2 = ordinal 0…10). Both files are therefore an **index/bitmask**,
   not the values. The tactics values come from the per-team JSON read by the name-keyed
   deserializer `FUN_141e79ce0` (220 field names) / `FUN_141e71120`; **no such JSON exists in the
   VFS** (all 121,075 mounted paths searched; the only `DevelopData/**` strings are the constant
   serializer registry at 0x142b48ca0, whose 227 paths cover `common/match/constant/**` only).
   **What is needed next:** either dump the live process and read the tactics struct at runtime, or
   find PES's `DevelopData/common/etc/pesdb/*.json` develop sources.
2. **The per-phase state machines (S0…Sn) are INF, not OBS.** Tried and **failed** to recover the
   method bodies: this exe uses Fox's interface-record dispatch — the class-info record
   `{u32 td, u32 n, u32 0, u32 −1, u32 0, u32 size, u32 vtableRva}` points to a
   `{u32 0, u32 1, u32 count, u32 entriesRva}` block whose entries are RVAs of further
   interface records, with **no static function pointers** anywhere (verified: no `lea rip→record`
   refs in `.trace`, no u64 code-pointer runs adjacent to any COL). Method addresses are
   resolved at runtime by name hash, so they cannot be read statically without the Fox object
   registry. **What is needed next:** locate the Fox object-registry builder (search for a table
   keyed by `StrCode32` of the class names in §1.8), or dump the live process.
3. **The `RestartStep` enum values — CLOSED.** The name is at 0x1425d3f70 (enum-name index **147**
   in the reflector's short name table at **0x14344a098**, index 176 in the long one at
   **0x143449fb0**). The "shared pool at 0x1425db3a0 interleaved with another enum" observed last
   night is real but it is only the **string bodies** — the pointer arrays are what identify each
   enum's values. `RestartStep`'s value-pointer array starts at **0x14344c3e8** and ends at the
   `NONE` entry at **0x14344c4d8**: 31 values in the order listed in §1.9. The pointer immediately
   before it (0x14344c3e0 = `MOVE_B`) belongs to the previous enum, which is what fixes the
   boundary. OBS.
   The AI-side step names (`KICK_KICKER_LOOP` @0x142652672/0x142652884,
   `KICK_SHORTPASS/LONGPASS/SHOOT/SHORT` @0x142652692–0x1426529e5, `KICK_TACTICS`,
   `KICK_DEFENCE_TYPE`, `KICK_KIND`, `KICK_WALL`, `KICK_POSITION_CHANGE`, `KICK_FAILED`,
   `WAIT_TIMER` @0x142651638) remain INF (high) as the free-kick/corner AI step names.
4. **Officials' exact coordinates.** The classes are identified (§5.1) but their placement
   constants were not decompiled; the tactic-side lines (`gks*/gkl*`, `dfLine*`) are the closest
   proxy.
5. **The officials' id-picking rule.** Not closed; the kit-config machinery is identified
   (`RefereeAppearance.bin`, `referee_DEF_*.bin`, `UniformParameter.bin`) but the selection code
   was not found. Tried: RTTI enumeration, the constant reflection registry, and a name scan for
   `Referee*`/`Linesman*`/`Fourth*` (no fourth-official class).
6. **The ball-washer** is named only indirectly (prop models); its behaviour is unverified.
7. **`penaltykick.o`'s shipped values — CLOSED.** Decoded from the shipped
   `common/match/constant/constant_player.bin` object (80 B, OBS by the WESYS parser): the
   deserializer thunk `0x141e76670` (5-byte `jmp` → on-disk `0x15b44b550`,
   `fileoff = 0x391b400 + (VA − 0x140000000 − 0x3d14000)`) stores a 13-word array at
   **+0x08 … +0x38**, so +0x00/+0x04 are the object header. Full values in §1.6 / §3.5:
   `shootSpeed.speed` 64 (u32), `gageAddKPH` 0.0, `gageMax` 50.0, `gageMin` 0.0,
   `heightAddKPH` 0.0, `minKPH` 0.0, `paraAddGageMax` 30.0, `paraAddGageMin` 1.0,
   `targetPoint.paraAddKPH` 0.2, `heightAdd` 10.0, `heightAddKeyConfig` 65.0, `widthAdd` 1.0,
   `widthAddKeyConfig` 0.75 (OBS values, INF semantics).
8. **The headless-Ghidra path needed repair** (documented for the next session):
   `<scratch>/ghk/scripts/*.class` was stale, so Ghidra tried to compile the `.java` and failed with
   "The class could not be found"; recompiling with
   `javac -cp "$(find /opt/ghidra/Ghidra -name '*.jar' | tr '\n' ':')" -d <scratch>/ghk/scripts DecompAt.java`
   fixed it. Also, when both `.java` and `.class` are present Ghidra sometimes prefers neither —
   keeping a **java-only** script dir (`<scratch>/ghk/scripts_only_java/`) worked reliably.
   Finally the serializer thunks in the reflection registry are 5-byte `jmp rel32` into
   `.impdata`, which is **outside the 64 MB memory dump**; they must be disassembled from the
   on-disk exe with `fileoff = 0x391b400 + (VA − 0x140000000 − 0x3d14000)`

Scratch: `<scratch>/SetPieceRefRE/` = `bin/*.bin` (all nine `constant_*.bin`, `Tactics.bin`,
`TacticsFormation.bin`), `wes.py` (WESYS parser), `exe.py`/`edis.py` (on-disk exe section map +
disassembler), `thunks.py`, `gasm.py`, `cols.json` (178 action/anime RTTI COLs),
`classinfo.json` (174 class-info records), `tactics_fields.json` (297 field offsets),
`refl_reg.json` (228 constant-object serializer addresses), `dec/ser_all.dis`,
`dec/deser.dis`, `dec/const_ser_raw.txt`.

(OBS: enumerated by parsing each archive's record table; the record table is 12 bytes per entry:
`{u32 offset, u32 size, u32 nameOffset}`, offsets and sizes are into the *inflated* blob.)

### 1.2 How the object layouts were recovered (method, reusable)

The exe carries a reflection registry at **`0x142b48ca0 … 0x142b49ad0`**: 228 records of
`{const char* jsonPath, void* serializerThunk}` covering every constant object (OBS: all 228
`DevelopData/common/match/constant/**` paths of the shipped data are present, e.g.
`…/match/setplayGuideCornerKick.json → 0x141e80960`).

Each serializer thunk is a 5-byte `jmp rel32` into `.impdata`
(OBS: `0x141e80960: e9 fb 8b 65 1c` → `0x15b4d9560`). The real function reads
`*(u32*)0x153f740fa ^ 0xdfd14818` → an object id, calls `0x1403fb410` to allocate, stores a
**field descriptor pointer** loaded by `lea rcx,[rip+X]` and zeroes the struct. The descriptor
at e.g. **0x142b5c958** is
`{void* ctor, u32 wordsize, void* deserializer, {char* name, u32 ?, u32 type}…}`.
The **deserializer** (descriptor[2]) is the gold: it walks the JSON node-by-node and shows the
field name and the destination offset, e.g. `throwin`'s deserializer `0x141e6fe40` performs
`call nodeLookup("longThrowSupport") ; call nodeLookup("adjustRate") ; call ← ; movss %xmm1,0x8(%rdi)`
→ `f32 longThrowSupport.adjustRate at +0x08` (OBS, `dec/deser.dis`).

Reader recipe: `lea rdx,[rip+X]` gives a `char*` name; the following
`call 0x1403842a0` descends into that node; the converter call tells the type
(`0x140384750` = f32, `0x1403846d0` = u8, `0x140384810` = u32); the `movss/mov …,N(%rdi)` right
after gives the offset. A fork of `edis.py` + this recipe reproduces every layout below.

### 1.3 `throwin.o` (144 B) — decoded layout, OBS

Offsets are into the inflated object blob (the same object the game deserializes into).

| +0x00 | f32 | `move.beginAngle` | — angle the taker turns to before the throw |
| +0x04 | u32 | `move` (mode) | 0/1/2 |
| +0x08 | f32 | `longThrowSupport.adjustRate` | |
| +0x0c | u32 | `longThrowSupport.enable` | |
| +0x10 | u32 | `mode` | throw-in mode selector |
| +0x18 | f32 | `seamlessThrowin.possibleDist` | distance below which the throw is played seamlessly (no run-up) |
| +0x1c | f32 | `seamlessThrowin.dist` | |
| +0x20 | f32 | `search.distFarLongPara` | |
| +0x24 | f32 | `search.distFarNormal` | |
| +0x28 | f32 | `search.distMiddle` | |
| +0x2c | f32 | `search.distNear` | |
| +0x30 | f32 | `speed.gageMiddle` | |
| +0x34 | f32 | `speed.adjustDistMax` | |
| +0x38 | f32 | `speed.adjustDistMin` | |
| +0x3c | f32 | `speed.adjustSpeed` | |
| +0x40 | f32 | `speed.baseMaxKph` | |
| +0x44 | f32 | `speed.baseMinKph` | |

The shipped values (OBS from the archive) read:
`beginAngle` = 0.0, `move` = 1, `longThrowSupport.adjustRate` = 0.0, `longThrowSupport.enable` = 1,
`mode` = 0, `seamlessThrowin.possibleDist` = 0.0, `seamlessThrowin.dist` = 0.5,
`distFarLongPara` = 80.0, `distFarNormal` = 3.0, `distMiddle` = 0.0, `distNear` = 0.0,
`speed.gageMiddle` = 5.0, `speed.adjustDistMin` = 3.0, `speed.baseMaxKph` = 32.0,
`speed.baseMinKph` = 25.0, plus 15.0, 5.0, 0.6, 20.0, 10.0, 10.0, 50.0, 35.0 in the tail.
INF (med): the `distFar*` trio (80 / 3 / 0 / 0 m) is how far up the line the taker looks for a
receiver; `speed.baseMaxKph` 32 is the throw's speed ceiling.

### 1.4 `setplayGuide*.o` (176 B) — decoded layout, OBS

All five (cornerKick, goalKick, freeKickNear/Middle/Far) share one layout
(deserializer `0x141e809d0` for corner/goal, `0x141e64e30` for the free-kick trio):

| +0x00 | f32 | `rot.gage` |
| +0x04 | f32 | `rot.addBackSpin` |
| +0x08 | f32 | `rot.addHigh` |
| +0x0c | f32 | `rot.addValue` |
| +0x10 | f32 | `rot.distMax` |
| +0x14 | f32 | `rot.distMin` |
| +0x18 | u32 | `rot.enable` |
| +0x1c | f32 | `rot.base` |
| +0x20 | f32 | `speed.topSpinRot` |
| +0x24 | f32 | `speed.addParaRate` |
| +0x28 | f32 | `speed.addGage` |
| +0x2c | f32 | `speed.base` |
| +0x30 | u32 | `speed.debugParameter` *(kickPowerParameter.debugParameter)* |
| +0x34 | f32 | `kickPowerParameter.max` |
| +0x38 | f32 | `kickPowerParameter.min` |
| +0x3c | u32 | `kickPowerParameter.mode` |
| +0x40 | f32 | `spin.backSpin.base` |
| +0x44 | f32 | `spin.backSpin.paraRate` |
| +0x48 | f32 | `spin.sideSpin.base` |
| +0x4c | f32 | `spin.sideSpin.paraRate` |

Shipped values, OBS:

| field | cornerKick | goalKick | fkNear | fkMiddle | fkFar |
|---|---|---|---|---|---|
| rot.gage | 0.8 | 0.8 | 0.6 | 0.6 | 0.6 |
| rot.addBackSpin | 0.2 | 0.2 | 0.2 | 0.2 | 0.2 |
| rot.addHigh | 5.0 | 2.5 | 2.5 | 2.5 | 2.5 |
| rot.addValue | 14.0 | 19.0 | 4.0 | 6.5 | 5.0 |
| rot.distMax | 8.0 | 17.0 | 4.0 | 5.0 | 5.0 |
| rot.enable | 1 | 1 | 1 | 1 | 1 |
| speed.topSpinRot | 10.0 | 10.0 | 10.0 | 10.0 | 10.0 |
| speed.addParaRate | 25.0 | 20.0 | 45.0 | 45.0 | 45.0 |
| speed.base | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 |
| kickPowerParameter.max | 99.0 | 80.0 | 55.0 | 55.0 | 55.0 |
| kickPowerParameter.min | 40.0 | 40.0 | 40.0 | 40.0 | 40.0 |
| spin.sideSpin.base | 0.6 | 0.8 | 0.7 | 0.7 | 0.7 |

(OBS: read from `constant_match.bin`'s object blobs at the offsets above.)

### 1.5 `setplayGuideCommon.o` (32 B) — decoded layout, OBS (deserializer `0x141e659a0`)

| +0x00 | u32 | `retryMax` (value 0) |
| +0x04 | f32 | `turnAngleNum` (4.0) |
| +0x08 | u32 | `turnAngleAdd` (1) |
| +0x0c | f32 | `turnAngleStart` (200.0) |
| +0x10 | f32 | `angleDiff` (50.0) |
| +0x14 | f32 | `angleDiff_good` (25.0) |
| +0x18 | f32 | `checkBallDist` (0.0) |
| +0x1c | f32 | `checkBallDist_good` (0.0) |

INF (med): the "guide" is the on-screen aim helper: the number of candidate aim lines
(`turnAngleNum` = 4), the starting angle offset (`turnAngleStart` = 200 in some internal unit,
INF degrees×…), and the acceptance band (`angleDiff` 50 / good 25). `checkBallDist*` gates
whether the guide is offered at all (0.0 = always).

### 1.6 `constant_player.bin` objects — decoded layouts, OBS

- `freekick.o` (16 B, deser `0x141e6d750`): `+0x00 u32 longpassInplayUse`,
  `+0x04 f32 longpassSpeedKPHMax`, `+0x08 f32 distWide/dist`, `+0x0c f32 suspendTime`,
  `+0x10 f32 waitTime`. Shipped: `longpassInplayUse` = 0, `longpassSpeedKPHMax` = 0.5,
  `dist` = 80.0, `suspendTime` = 3.0, `waitTime` = 0.0.
  INF (med): `suspendTime`/`waitTime` are **seconds of hold before the free-kick may be taken**;
  `distWide` 80 m is the width of the "quick restart allowed" cone.
- `goalKick.o` (32 B, deser `0x141e78290`): `+0x00 f32 angleBase`, `+0x04 f32 angleDelta`,
  `+0x08 f32 distManualBase`, `+0x0c f32 distManualDelta`, `+0x10 f32 distMaxBase`,
  `+0x14 f32 distMaxDelta`, `+0x18 f32 distMinBase`, `+0x1c f32 distMinDelta`,
  `+0x20 u32 freekickDebug`, `+0x24 f32 goalKickMode`, `+0x28 u32 thinkDistFar`,
  `+0x2c f32 thinkDistMiddle`, `+0x30 f32 thinkDistNear`.
  Shipped: `angleBase` = 0.0, `angleDelta` = 0.0, `distMaxBase` = 5.0, `distMaxDelta` = 3.0,
  `distMinBase` = 32.0, `distMinDelta` = 25.0.
  INF (med): the goal-kick target distance is `distMaxBase + random·distMaxDelta` for a long
  kick and `distMinBase …` for a short one; `angleBase/angleDelta` jitter the target angle.
- `penaltykick.o` (80 B, deser `0x141e76670`): the deserializer thunk (5-byte `jmp` → on-disk
  `0x15b44b550`) copies a 13-word JSON array into the object at **+0x08 … +0x38** and leaves
  +0x00/+0x04 as the object header (vtable + refcount). The shipped blob
  (`constant_player.bin` object 78432, 80 B, OBS by `wes.py` decode) reads:

  | off | field | shipped value |
  |---|---|---|
  | +0x08 | `shootSpeed.speed` | u32 **64** (integer, not f32 — `0x00000040`) |
  | +0x0c | `shootSpeed.gageAddKPH` | 0.0 |
  | +0x10 | `shootSpeed.gageMax` | **50.0** |
  | +0x14 | `shootSpeed.gageMin` | 0.0 |
  | +0x18 | `shootSpeed.heightAddKPH` | 0.0 |
  | +0x1c | `shootSpeed.minKPH` | 0.0 |
  | +0x20 | `shootSpeed.paraAddGageMax` | **30.0** |
  | +0x24 | `shootSpeed.paraAddGageMin` | **1.0** |
  | +0x28 | `targetPoint.paraAddKPH` | **0.2** |
  | +0x2c | `targetPoint.heightAdd` | **10.0** |
  | +0x30 | `targetPoint.heightAddKeyConfig` | **65.0** |
  | +0x34 | `targetPoint.widthAdd` | **1.0** |
  | +0x38 | `targetPoint.widthAddKeyConfig` | **0.75** |

  INF (med): the first five fields (speed/gage) are integers/rates and only the last eight are
  f32 — the store path is a plain 4-byte copy, so all of them are u32s in the blob and the JSON
  converter decides the type. `+0x3c…+0x4c` is unnamed padding (10.0 / 2.0 / 2.0 / 2.0 / 2.0).
  INF (med): `gageMax` 50 and `widthAddKeyConfig` 65 bound the on-screen aim gage; `heightAdd` 10
  and `widthAdd` 1 m are the target-point jitter.
- `ballplayerSetplay.o` (64 B, deser `0x141e6fb50`): `+0x00 f32 FreekickShoot.range_frontZ`,
  `+0x04 f32 FreekickShoot.range_max`, `+0x08 f32 FreekickShoot.range_middle`,
  `+0x0c f32 FreekickShoot.range_short`, `+0x10 f32 Throwin.range_width`,
  `+0x14 f32 Throwin.range_long`, `+0x18 f32 Throwin.range_min`, `+0x1c f32 waitTimerMin`.
- `ballplayerGk.o` (96 B, deser `0x141e66f20`): `+0x00 f32 ClearInfo.enemyDist`,
  `+0x04 f32 LongPassInfo.forceKickTime`, `+0x08 f32 LongPassInfo.forceEnemyDist`,
  `+0x0c f32 LongPassInfo.rangeFar`, `+0x10 f32 LongPassInfo.limitLineFromPenaltyLine`,
  `+0x14 f32 ShortPassInfo.rangeNear`, `+0x18 f32 ShortPassInfo.courseCheckWidth`,
  `+0x1c f32 ShortPassInfo.passAngleSubMax`, `+0x20 f32 ShortPassInfo.passDistMax`,
  `+0x24 f32 ShortPassInfo.rangeFar`.

### 1.7 Image constants already known (carry-over)

- Logic tick 54.0 @`0x14351AE54`, fps 54 @`0x14351AE50` (`pes-re-tactics-clock.md` §0).
- Restart/step reflection names: `RestartKind` @0x1425d3388, `RestartStep` @0x1425d3f70,
  `FutureRestartKind` @0x1425d3398, `CheckRestartStep` @0x1429558a0, `CheckRestartFkKind` @0x142955c00,
  `CheckBallTouchRestartKind` @0x142955838, `CheckRestartKindHistry` @0x142955c18 —
  the record reflection tables are at `0x14344a098/8/308/318/310/450` (match record) and
  `0x1434f6f10` (online record). OBS: string scan.
- Tactic-name strings near `0x142b59xxx` carry the placement rule names, e.g.
  `kickoffDfLineX` @0x142b59bd0, `addAngleKickOff` @0x142b56078, `throwinLengthDf` @0x142b5a228,
  `throwinWidthZ` @0x142b5a238, `dfLineWidth_Corner` @0x142b59838, `adjustDfLineWidth_GoalKick` @0x142b590e0,
  `minWidth_GoalKick` @0x142b59e40, `cornerKickOffencePlayerNumber` @0x142b59560,
  `cornerKickTime1/2` @0x142b587e0/f0, `penaltyKickTime1/2` @0x142b58878/90, `cornerkickDefenceMFSide`
  @0x142b59590, `cornerkickDefenceMFWidth` @0x142b595a8, `gksPenaltyLineRateX_DF` @0x142b59b40,
  `addPenaltyX` @0x142b5adf0, `addPenaltyZ` @0x142b5ae00, `limitLineFromPenaltyLine` @0x142b51538,
  `margin_restartX` @0x142b59d08, `diffRestartX_Start/End` @0x142b598c8/b0, `restartKind` @0x142b4e260,
  `restartPosNo` @0x142b4f938, `restartPosTable` @0x142b4f948/58. OBS: string scan.
  These are the **tactics-file field names** — the same names GF must expose per team.
