# `constant_team.bin` field map + the `T+0xa53c` setter (TeamConstRE, finished by TeamConstRE2)

Tags: **OBS** = raw bytes / disassembly / decompile at the VA, or a decoded file value; **INF(level)** = inference.
Static only (no live process touched). Image: `pes_full.bin` (base 0x140000000); decompiles via
`~/ghidra-projects/scratch/Unpacker/dec.sh`. Notation as the team-AI reports: `G = *(0x1436f3dc8)`, `MI = *(G+0x270)`,
`MA = *(G+0x18)`, `T` = team record, `fps = FUN_141e532e0()` (54).

Scratch: the TeamConstRE scratch dir —
`ctmap.py` (the decoder; regenerates every table below from the exe + both files and asserts 14 anchors),
`ctmap_all.tsv` (720 fields: registry, object, field, blob offset, struct offset, type, stock, mod),
`ctmap_diff.tsv` / `ctmap_diff_compact.md` (the 445 differing fields), `readers.tsv` (getter sites + field reads),
`out/a53c_writers.c`, `out/a53c_pred.c`, `out/a53c_caller.c`, `out/node.c` (decompiles),
`aoba_dt18_deepdive.txt` (the Aoba dt18 wiki page the user attached; used only as an anchor source).

## 0. Result

| item | state |
|---|---|
| 1. Parse `constant_team.bin`, map fields, stock-vs-mod diff | **CLOSED.** All 16 objects, 720 fields named and typed, struct offsets for every scalar; 445 fields differ (§5). |
| 1b. Map fields to readers | **Partly closed** ("where practical"): registry getter sites for 11 of 16 objects and 76 field-level read sites (§4); prior-report readers folded in. |
| 2. `T+0xa53c` setter | **CLOSED (static).** Setter `FUN_140536170` (store `0x1405361ed`), predicate `FUN_140535650`, scheduled every 4th frame from the team tick (§6). |

## 1. Inputs (OBS)

- **Stock:** `common/match/constant/constant_team.bin` from `Data/dt18_all.cpk` (1354 B, WESYS tag `ff 10 51`), extracted
  with the DefenderLiveRE `cpkls.py`.
- **Mod:** the same path resolved by `pesfmt_dump cat` (1513 B, tag `00 10 01`). The user confirmed the overlay:
  `download/4cc_06_gameplay.cpk` overrides `dt18`. DefenderLiveRE/SetpieceChoreographyRE showed the live registry
  objects equal this VFS copy (EV-SPC-0023), and §2.4 below re-confirms it for 32 basePosition fields + subConcept.
- Both files: 16 objects with identical names and sizes (`wesys_constant.py` table).
- **INF(high), mod lineage:** a Righetti-0.6.4-derived tune. Of the 17 Righetti values the Aoba page lists for
  `constant_team`, 14 match the mod exactly (pressRate 5, slowDown* 0, dfLineAdjustMax/Min 0/−20, maxWaitTime 0,
  adjustXFromAttackAwareness3/4 7/14, addAngleKickOff 50, futureFrame 64, defence predictionFrame 44); 3 are re-tuned
  (transitionSec 2 vs 1, adjustDistBoxToBox 5.5 vs 5, defenceMark predictionFrame 17 vs 18).

## 2. How the game loads these objects (OBS)

### 2.1 Registry and classes
Registry records `{char* jsonPath, factory}` at **0x142b49810 + 16·i**, registry index **183 + i**, in object-name order
(`DevelopData/common/match/constant/team/<name>.json`). Matches the known indices (basePosition 183 = 0xb7,
defenceMark 188 = 0xbc, selectorVision 194 = 0xc2, subConcept 196 = 0xc4). Getter: `FUN_141e5d4c0(ctx, index)`.

| reg | object | blob B | vtable | slot 1: JSON deserializer | slot 2: binary loader | fields | differ |
|---|---|---|---|---|---|---|---|
| 183 | basePosition | 864 | 0x142b5a328 | 0x141e79ce0 | 0x141e79320 | 220 | 137 |
| 184 | centeringGet | 16 | 0x142b4f678 | 0x141e62d80 | 0x141e71db0 | 1 | 0 |
| 185 | combination | 288 | 0x142b4ff98 | 0x141e642f0 | 0x141e641f0 | 57 | 6 |
| 186 | defence | 144 | 0x142b51af8 | 0x141e67460 | 0x141e67360 | 36 | 28 |
| 187 | defenceCover | 144 | 0x142b53f58 | 0x141e6bc60 | 0x141e6bb80 | 33 | 26 |
| 188 | defenceMark | 320 | 0x142b567d8 | 0x141e71120 | 0x141e70e30 | 77 | 68 |
| 189 | diagonalRun | 96 | 0x142b587a8 | 0x141e77ba0 | 0x141e77b10 | 21 | 21 |
| 190 | lineBreak | 64 | 0x142b58d58 | 0x141e78820 | 0x141e787c0 | 14 | 14 |
| 191 | overlap | 32 | 0x142b5aa68 | 0x141e7cb10 | 0x141e7cad0 | 7 | 0 |
| 192 | pairAnime | 224 | 0x142b4f0a8 | 0x141e61ab0 | 0x141e61930 | 34 | 17 |
| 193 | pullAway | 48 | 0x142b50a68 | 0x141e65ba0 | 0x141e65b30 | 15 | 10 |
| 194 | selectorVision | 224 | 0x142b539b8 | 0x141e6b290 | 0x141e6b170 | 30 | 10 |
| 195 | spaceRun | 240 | 0x142b54f28 | 0x141e6dcf0 | 0x141e6dac0 | 60 | 54 |
| 196 | subConcept | 208 | 0x142b56888 | 0x141e71cc0 | 0x141e71c40 | 50 | 21 |
| 197 | support | 240 | 0x142b58418 | 0x141e77110 | 0x141e76f90 | 58 | 33 |
| 198 | teamId | 112 | 0x142b58e50 | 0x141e78c90 | 0x141e78b70 | 7 | 0 |

Vtables were found from the factory → ctor `lea`/`mov [rax],rcx` for the plain factories and by slot-address proximity
to the factory for the five obfuscated ones (combination, diagonalRun, pullAway, support, teamId); every one is
confirmed by its slot-1 key set matching the object's blob and by the anchors in §2.4. Slot 2 of defence and pullAway
is VM-obfuscated (not read); slot 2 of basePosition, support, combination, pairAnime, diagonalRun is plain code.

### 2.2 Slot 1 = name → struct offset (the authoritative field names)
Each field: `lea rdx,[key]; call FUN_1403842a0(node, key)` (a std::map<string, json-value> lookup, `out/node.c`),
then a converter — `FUN_140384750` → f32 (`cvtsd2ss; movss [this+off]`), `FUN_140384810` → u32, `FUN_1403846d0` →
bool (`mov byte [this+off]`) — and a store to `this + off`. Nested objects chain lookups; lists use
`FUN_1403841e0(node, i)` inside a loop whose bound gives the length (OBS `cmp` immediates): support `style` 20,
subConcept 48, selectorVision `offerNum_*` 5 each, teamId `home`/`away` 3 strings, combination `combsCom` 3 ×
{area, kind} and `combsSide` 2 × 11 × {area, kind}. Keys are visited in sorted order. Struct fields start at +8
(vtable at +0). This is the dev-time JSON path; release builds use slot 2.

### 2.3 Slot 2 = blob → struct (the release loader) and the blob layout rule
Example (basePosition slot 2 → `0x15b482750`): `mov eax,[rdx+k]; mov [rcx+k+8],eax` for dwords and
`movzx eax, byte [rdx+0x3c]; mov [rcx+0x44],al` for bools. support (`0x15b455d80`): blob 0x00–0x70 → struct
0x08–0x78, blob 0x74 holds the `style` offset, resolved by `FUN_141e823e0(off, blob)` and copied as 20 u32 to struct
0x7c, then blob 0x78–0x98 → struct 0xcc–0xec. pairAnime (`0x15b3102d0`) resolves each sub-object offset the same way.
Rule (OBS on those five loaders, INF(high) for the obfuscated two, anchor-checked for all):
- keys in sorted order; f32/u32 = 4 B, 4-aligned; **consecutive bools pack into single bytes**;
- sub-objects and lists are a u32 offset from the blob start to a 16-aligned block after the scalars;
- for flat objects (no lists/sub-objects) **struct offset = blob offset + 8**.

### 2.4 Validation of the map (OBS)
- **Stock vs Aoba / MatchLogicRE "vanilla" values: 87/87** after dropping the wiki's lineBreak table (see §3) — e.g. pressRate 0.5,
  transitionSec 5, marginSide 1, lengthOf 35, lengthDf 35, dfLineRate 0.2, dfAdjustZRateMax 10, dfLineWidth_3/4/5
  16/20/24, slowDown* 1, dfLineAdjustMax/Min 10/3, maxWaitTime 30, adjustXFromAttackAwareness3/4 2.5/5; all 19
  defenceMark normal/Box-to-Box values; overlap 6/6; diagonalRun normal + dummyRunner 8/8; defence matchUp*
  DefensiveAwareness0/1/3/4 5/8/6/12; predictionFrame 40; futureFrame 60; pairAnime stopSec 0.3, moveSec 0.8,
  firstAttackEnd enable 0 / paraDiff 10, stamina 0.5/0.8/0.2; support as MatchLogicRE §2.5.
- **Mod vs DefensiveShapeRE §8 live struct offsets and values: 32/32** (e.g. `adjustDefenceLine_ForeCheck` +0x18 −9,
  `dfLineCenterAdjustMax/Min` +0x14c/+0x150 −8/−20, `defenceFormationTest1/2` +0x108/+0x109 0/1,
  `minWidth*` +0x284..+0x28c 6/2/5, `transition*`/`cornerKick*` as SetpieceRefereeRE §1.11).
- **Mod subConcept = forty-eight 1s** = TeamAILeftoversRE §2.3 live read (`[reg+8+4·id]`).
- `ctmap.py` asserts 9 stock and 5 mod anchors on every run.

## 3. Killed hypotheses

| hypothesis | killed by |
|---|---|
| "blob offset = alphabetical index × 4" (MatchLogicRE §4.1 rule, used by the first pass of this task) | basePosition bools pack where they are consecutive (`defenceFormationTest1/2`, `dfAdjustZ` at struct 0x108–0x10a; `dfCoverAdjustX`, `dfCoverEnable`, `dfForceAverageZ` at 0x134–0x136; OBS slot 1 and slot 2): 220 keys in 216 dwords, so the index rule drifts by 2 keys after 0x10a and by 4 after 0x136 — which is why it gave stock pressRate 0.24. Lists also break it (support after `style`). Holds only for flat objects without consecutive bools. |
| "the key strings have no code xrefs, so the deserializer is protected/hashed" | slot 1 `lea`s every key (e.g. `0x141e79cf3 lea rdx` → `adjustAngle_FreeKickSupport` 0x142b58fd8); the earlier xref scans were broken (numpy slicing / filter bugs). |
| "stock dt18 is not vanilla" (pressRate 0.24 vs Aoba 0.5) | artefact of the index rule; with the slot-1 map stock pressRate = 0.5 and 87/87 vanilla anchors hold. |
| Aoba wiki "lineBreak PES21" table (checkZ 70, checkBallDist 60/90, angleDiff 90/70 …) | those keys do not exist in lineBreak's slot-1 key list (14 keys, all check*/searchCount*, OBS); the names exist only in pullAway/spaceRun, with other values. The wiki row is mislabelled or from another build; not used. |
| "PES18 gameplay-editor mappings apply" | already killed for basePosition/diagonalRun by MatchLogicRE; confirmed (slot-1 offsets differ). |
| "T+0xa53c has only 3 writers / no displacement store; flips via reflection" (TeamAILeftoversRE §3, first pass of this task) | full disp32 scan finds 5 store sites (+1 address-take) incl. the semantic store `mov [rbx+0xa53c], dil` at **0x1405361ed** (§6.1). The earlier pattern scans missed REX-prefixed and `dil`/`sil` forms. |

## 4. Field → reader map

### 4.1 Readers already named by the team-AI reports (OBS there; offsets now carry names from §2)
- **basePosition (183)**: `FUN_1405f9eb0` (ForeCheck/Retreat/strategy ramps, `dfLineCenterAdjust*`, `marginTargetLineX`,
  `keepDfTargetLineX`, `lengthDf*`, `adjustXCompactFW`), `FUN_1405fb6f0` (`lengthOf`, `dfLineRate*`, `adjustFrontOF/BackOF`),
  `FUN_1405efbd0` (`adjustDiv`, `mfLineScore_adjustMax`), `FUN_1405f4f50` (`coverLimitXRate`), `FUN_140688300`
  (`dfLineWidth_*`, `minWidth*`, `dfLineCloseRate`, `lastLineCloseMaxRate`, `defenceFormationTest1`), `FUN_140682770`
  (`defenceFormationTest2`), `FUN_14063f230` (`adjustZCompact`) — DefensiveShapeRE §8; set-piece fields — SetpieceRefereeRE §1.11.
- **defenceMark (188)**: the marking passes `FUN_140527140…FUN_140533bc0` (getter `·,0xbc`) — DefensiveShapeRE marking section.
- **subConcept (196)**: selector `0x14057986f` reads `[reg+8+4·id]` (`subConcept[id]`, skip if ≤ 0) — TeamAILeftoversRE §2.3.

### 4.2 New: registry getter sites (OBS) and getter-adjacent field reads (INF(med))
Method: every `call FUN_141e5d4c0`/`FUN_141e5ca20` with `mov edx, 183..198` in the preceding instructions (OBS), then
`[rax(+copies)+disp]` reads in the next 80 instructions matched to slot-1 struct offsets (heuristic: a register reuse
can create a false hit). Full list: `readers.tsv` (11 objects, 75 getter sites, 76 field reads). Not reached this way
(cached pointer or computed index): centeringGet, combination, lineBreak, overlap, teamId.

| object | getter sites (containing functions) | field reads (site) |
|---|---|---|
| basePosition | 7 (`0x1404f9dc0`, `0x1405c0150`, `0x140952e10`, `0x1409571c3`, `0x1409589f0`, `0x14095c9d0`) | `slowDownFw` 0x140958ad1, `slowDownPassFrame` 0x140958ab8 / 0x1405c0b2a, `adjustSlideMoveSpeed` 0x140958aea, `adjustMoveEnemySide` 0x140958b00, `adjustGapDfLine` 0x140958b20, `lineControl` 0x140958b3e, `adjustGapDfLineAction` 0x140957525, `jogMfLineScore` 0x140952ea7, `forceDashDistOffence` 0x1405c0745, `isUseDashSituation` 0x14095cbb2 |
| defence | 1 (`0x140631b60`) | — |
| defenceCover | 11 (`0x14062c5a0`…`0x140631de0`) | `adjustAngleRate` 0x14062cfb0, `mfLimitRate` 0x14062d8e1, `coverAdjustDist` 0x14062f4c0, `coverDist` 0x14062f4c6, `limitDistX_MfLine` 0x14063213b |
| defenceMark | 16 (`0x140527140`…`0x14096dfc0`) | Box-to-Box block in `0x140533bc0`: `remainMarkXLimit_BoxToBox` 0x140533c19, `remainMarkZLimit_BoxToBox` 0x140533c21, `checkBackFollowRemainDist_BoxToBox` 0x140533c2a, `adjustDistBoxToBox` 0x140533c30; `checkBackFollowDist_BoxToBox` 0x14052fec3 |
| diagonalRun | 6 (`0x140613910`, `0x140614290`, `0x1409b8ba0`…) | dummy-runner block in `0x140613910`: `fanAngle_dummyRunner` 0x140613cc0, `fanFar_dummyRunner` 0x140613cc6, `distXBpMax_dummyRunner` 0x140613ccc; `moveKPHMin` 0x140614533 |
| pairAnime | 13 (`0x140776e10`…`0x14077b7d0`) | `protectAuto1..5`, `protectButton`, `protectDribbleTouchNum` (0x14077a42b–0x14077a609, 0x14077896e, 0x140776f0f) |
| pullAway | 1 (`0x14061dee0` = Aoba `GP_PullAwaySystem`) | — |
| selectorVision | 2 (`0x140600e30`, `0x140a53530`) | — |
| spaceRun | 12 (`0x1405aca50` = Aoba `GP_HP_GoodSelector2`, `0x140612660` = `HP_GoodSelector`, …) | 28 fields, e.g. `searchCount` 0x1405acac5, `checkZ` 0x1405acadf, `checkBallDist_second` 0x140612703, `chanceScore_good` 0x14060265a |
| subConcept | 5 (`0x140579840`, `0x140961860`, `0x140992ec0`, `0x1409ad830`, `0x1409b88d0`) | `subConcept_passRequest_all_off` / `_off` (+0xc8/+0xc9) at 0x140961a39/42, 0x140993046/4f, 0x1409ada8e/97, 0x1409b8943/4c |
| support | 1 (`0x140996400`) | — |

The Box-to-Box and dummy-runner read clusters agree with the Aoba descriptions of those styles (INF(med) for the
style gate itself; not traced).

## 5. Stock (`dt18`) vs mod (`4cc_06_gameplay`) — every differing field (OBS)

445 of 720 fields differ. Values are `stock→mod`; list entries with the same change are grouped. Offsets and types
for each field are in `ctmap_diff.tsv`. Units are the game's (m, deg, frames or s per field name; INF where unstated).
Notable behaviour changes (INF(med) on effect): pressRate 0.5→5, slowDown* 1→0, transitionSec 5→2, all 21 stock-disabled
subConcept ids enabled, pairAnime duel decide timings 0.8/0.8/0.3 s → 0, support distances/angles up ~50–70%.

- `basePosition.o` (137 of 220 differ): adjustBackOF -10→-3; adjustCloseRate_stratagy_defensive -4→-5; adjustCloseZRate_FreeKick 45→40; adjustDefenceLine_ForeCheck -8.1→-9; adjustDefenceLine_Retreat -7→-11; adjustDefenceLine_strategy_defensive 10→1; adjustDefenceLine_strategy_levelup -5→-14; adjustDfLineWidth_GoalKick 2→4; adjustFrontOF -10→-5; adjustFwLine -12→-6.77; adjustMoveEnemySide 1→2; adjustReturnDist 7→12; adjustSideZ 8→6.66; adjustSideZ_concept_side 13→16; adjustSpaceCoverRate 0.23→0.333; adjustWidth_strategy_defensive -1.5→-1.2; adjustXCompactFW 0.5→0.33; adjustXFromAttackAwareness3 2.5→7; adjustXFromAttackAwareness4 5→14; adjustX_FreeKick 4→9; adjustZCompact 2.5→2.85; adjustZMinCompact 0.5→0.15; adjustZOneSideCut 7→9; adjustZRate_cover 0.4→0.43; adjustZ_FreeKick 7→15; attackLevel 10→12; attackLevelAdjustX 12→14; ballSideMaxRate_stratagy_defensive 0.6→0.85; baseDist_FreeKick 23→30; baseMfTargetLineWidth 1→1.33; changeWidthRate 0.1→1; checkAdjustDist 3→4; checkBaseDist 4→6; checkReturnDist 12→14; checkReturnDistX 22→24; checkReturnDistZ 15→16; checkZRate_cover 0.5→0.65; closePenetrateZRate 50→65; closeRate_DF_FW 45→52; closeRate_DF_FW_Retreat 42→50; closeRate_MF_adjustX 10→14; closeRate_MF_adjustX_stratagy_defensive 10→16; closeSideZRate 0→1; coverLimitXRate 4→6; coverRateZ 0.4→0.67; defenceCompact 10→8; dfAdjustZCheckLenMax 20→28; dfAdjustZCheckLenMin 12.5→9; dfAdjustZCheckWidthMax 30→29; dfAdjustZCheckWidthMin 5→4; dfAdjustZRateMax 10→33; dfAttackWidthBack 2→4; dfAttackWidthFront 7.8→6.66; dfAttackWidthMarginBack 5→6.77; dfAttackWidthMarginFront 12→13.3; dfGroupRate 0.5→0.37; dfLine 10→8; dfLineAdjustMax 10→0; dfLineAdjustMin 3→-20; dfLineBack 10→3; dfLineCenterAdjustMax 8→-8; dfLineCenterAdjustMin -5→-20; dfLineCloseRate 0.55→0.61; dfLineRate 0.2→0.09; dfLineWidth_3 16→15; dfLineWidth_4 20→19; dfLineWidth_5 24→23; dfUserPositionAdjustRate 1→10; dfUserPositionAdjustTime 30→5; diffRestartX_End 58→65; diffRestartX_Start 35→40; forceDashDistDefence 9→10; forceDashDistOffence 15→14; forceJogDist 2.4→2.8; forceWalkDist 1.2→1.6; fwGroupRate 0.8→1.25; gklBaseX 12→18; gklWidthZ 36→40; gksBaseX 18→21; jogMfLineScore 50→100; kickoffDfLineX -18→-12; lastLineCloseMaxRate 0.8→0.34; lengthDf 35→46; lengthDf_Retreat 35→42; lengthDf_SuperRetreat 30→32; lengthOf 35→40; limitBesideWidth 13→16; limitBesideWidth_df 13→12; marginPredictionFrameAdjust 120→0; marginTargetLineX -6→-10; margin_restartX 6→8; matchUpContinueRate 30→40; matchUpContinueRate_FW_MF 40→55; maxCloseXRate_FreeKick 95→80; maxWaitTime 30→0; mfAdjustMaxRateX 0.2→2.5; mfGroupRate 0.4→0.04; mfLineRate 0.1→0.01; mfLineScore_adjustMax 10→36; mfPushUpRate 35→48; minDistMF_DF 9→9.25; minDistMF_DF_stratagy_defensive 6→7.25; minDistMF_FW 1.5→0.85; minWidth 9→6; minWidth_stratagy_defensive 7→5; moveStartDist 0.8→0.08; offenceWideLength 45→36; offenceWideLength_concept_side 48→39; offenceZposiAdjustDist 0→2; openSideAdjustZRate 0.15→1.29; player_count_backDistX 4→3; player_count_frontDistX 25→24; player_count_height 25→18; pressRate 0.5→5; pushUpSide 1→3; pushUpSideBack 1→3; setPlayDeclineRate 0.24→0.12; sideBackPushUpRate 0.6→4; sideCloseRate 60→44; slideDistMax 25→32.33; slideDistMin 8→0.6; slideJudgeWidthBase 10→8; slideJudgeWidthMargin 10→18; slideWidth 3→2; slowDownArrivalFrame 1→0; slowDownDefenceLine 1→0; slowDownFw 1→0; slowDownMySide 1→0; slowDownPassFrame 1→0; spaceCoverRate 0.43→0.69; speedDownRunDist 5→1; speedUpRunDist 4→1; targetLineXVariation 0.2→0.12; targetLineZVariation 0.2→0.12; transitionSec 5→2; walkMfLineScore 20→33; wideRate 0.3→0.35.
- `centeringGet.o` (1 fields): no differences.
- `combination.o` (6 of 57 differ): checkDist 15→14.5; checkWedgeAngle 60→56; checkWedgeFarX 25→24; checkWedgeNearX 9→8.2; checkWedgeOffsideX 7.5→9.5; checkZ 4→5.
- `defence.o` (28 of 36 differ): adjustDribbleWaitTimer 10→8.88; adjustSandDist 10→12; continueAdvantageDelay 5→6.5; continueAdvantagePress 3→4; courseCutCheckRange 50→24; courseCutTargetDistFar 35→28; courseCutTargetDistNear 6→3; matchUpAdjustLineCheckDistDefensiveAwareness3 6→2.2; matchUpAdjustLineCheckDistDefensiveAwareness4 12→4.55; matchUpBackFollowAngle 70→50; matchUpBallFutureFrame 20→22; matchUpContinueAddBackFollowAngle 10→7; matchUpContinueAddBallDist 1.5→3.65; matchUpContinueAddDfXLimit 2→1.65; matchUpContinueAddMfXLimit 2→1.65; matchUpContinueAddZLimit 2→1.65; matchUpContinueDistSub 4→3; matchUpDfXLimit 10→6.5; matchUpFwXLimit 4→3.5; matchUpLimitDistDefensiveAwareness0 5→3.5; matchUpLimitDistDefensiveAwareness1 8→6.25; matchUpMustContinueDist 3→2.22; matchUpStartLine 10→6; matchUpStartLineMax 26→20; matchUpZLimit 5→3.25; maxSandDist 14→8; maxWaitTime 30→34.5; predictionFrame 40→44.
- `defenceCover.o` (26 of 33 differ): adjustAngleOneSideCutRev 20→18; adjustAngleRate 50→44; adjustCorrectDist 5→4; adjustZ 2→1.67; adjustZRate 0.3→0.45; baseAngleCutLongitudinal0 165→125; baseAngleCutLongitudinal1 15→18; baseAngleCutSide0 240→152; baseAngleCutSide1 300→240; baseAngleOneSideCutRev 25→20; checkDistBaseToCover 6→5.5; checkDistTransToCover 7.5→7.25; correctAngleWidth0 60→50; correctAngleWidth1 120→110; correctDist0 10→9; correctDist1 7.5→7; coverAdjustAngle 10→9; coverAdjustDist 4→3.67; coverAngle 42→40; coverDfAngle 55→48; coverDfAngle2 18→17; coverDist 4.5→5; futureFrame 60→64; limitDistX_MfLine 8→8.5; mfLimitRate 70→90; minCoverDist 5→4.5.
- `defenceMark.o` (68 of 77 differ): addAngleKickOff 22.5→50; adjustApproachRateMax_inOffenceDir 20→22; adjustContinueDistTeamWork 5→5.1; adjustCrossDistTeamWork 3→4; adjustDistBoxToBox 3→5.5; approachRateMax 50→49; approachRateMax_buildUp 30→47; approachRateMax_side 40→39; backAngleLimitGoal 60→58; backAngleLimitSide 90→88; ballBackAngleForLineUp 67.5→60; checkBackFollowDist 0.7→0.71; checkBackFollowDist_BoxToBox 0.7→0.78; checkBackFollowRemainDist 15→17; checkBackFollowRemainDist_BoxToBox 18→19; checkBackFollowZoneDist 1→1.85; checkBackFollowZoneDist_BoxToBox 2→2.35; checkCenterPlayLastXDist 10→11; checkCenterPlayLastXDistRemain 8→9; checkFrontPlayPointDist 25→24; checkRearOffenceDist 20→21; checkRearOffenceDistFar 20→22; checkReverseSideXDist 15→16; checkTargetGoalDist 40→38; checkZoneAreaRate 0.1→0.2; checkZoneAreaRate_BoxToBox 0.3→0.35; continueAddAngleLimit 22.5→26.5; cornerKickTime 4→3; dfZZoneRange 2.5→2.6; distDash 6→8; distDashMargin 2→2.5; distDash_course 4→4.5; distJog 2→2.2; distJogMargin 2→2.2; distJog_course 2→2.2; distRun 3→3.55; gapDist 4→3.75; markContinueAddDistDF 7→8; markContinueDist 15→15.5; markContinueDist_BoxToBox 20→20.5; needMarkAngleBase 90→92; needMarkDistGKShort 25→26; needMarkOverLastLineDist 2→2.5; noCrossCheckDist 7→6; preLastDistForGap 4→4.2; predictionFrame 20→17; releaseCheckTargetAngle 29.15→40; releaseCheckTargetDfLineDist 15→18; releaseCheckTargetMarkedAngle 22.5→24.5; releaseCheckTargetMarkedDist 6→8; releaseTargetDist 5→7; remainMarkXLimit 10→12; remainMarkXLimit_BoxToBox 13→14; remainMarkZLimit 12→13.5; remainMarkZLimit_BoxToBox 18→16; reverseSideAddWideLimit 10→12; targetBallAngleForGap 135→124; targetLastDistForGap 30→32; targetLineDistForLineUp 2→2.5; targetPlayDistForLineUp 20→22; tmpCheckZoneMarkDist 4→6; zoneFrontXMf 10→12; zoneMarkXLimit 15→14; zoneMarkXLimit_BoxToBox 18→17; zoneMarkZLimit 15→14; zoneMarkZLimit_BoxToBox 18→17; zoneZRange 2.5→3; zoneZRangeAlone 5→6.
- `diagonalRun.o` (21 of 21 differ): angleBase 45→60; bpAngleSubAbsMax 80→82; deltaXEnd 1→2; deltaZMax 15→17; deltaZMin 6→3; distNear 4→3; distPlayPointMax 25→24; distXBpMax 8→8.5; distXBpMax_dummyRunner 15→11; distXOffsideLineMax 13→10; distXOffsideLineMin 0→1; fanAngle 20→16; fanAngleHalfMargin 5→3; fanAngleHalfMargin_dummyRunner 5→6; fanAngle_dummyRunner 40→42; fanFar 5→6; fanFar_dummyRunner 8→13; fanNear 2→1; fanOffset 5→6; moveKPHMin 9→8; scoreMin 60→58.
- `lineBreak.o` (14 of 14 differ): checkAreaScore 90→92; checkAreaScore_lineBreaker 70→66; checkBodyAngleEyesOff 60→82; checkBodyAngleEyesOff_lineBreaker 80→111; checkBodyAnglePullAway 60→85; checkBodyAnglePullAway_lineBreaker 90→122; checkDistEyesOff 15→19; checkDistEyesOff_lineBreaker 25→36; checkInterceptScore 70→72; checkInterceptScore_lineBreaker 40→44; checkSideAngleEyesOff 65→84; checkSideAngleEyesOff_lineBreaker 80→111; searchCount 3→4; searchCount_lineBreaker 5→6.
- `overlap.o` (7 fields): no differences.
- `pairAnime.o` (17 of 34 differ): decideTiming.moveSec 0.8→0; decideTiming.pes15TestDecideTiming.maxSec 0.8→0; decideTiming.stopSec 0.3→0; highballEnable 0→1; moveContact.firstAttackEnd.enable 0→1; moveContact.firstAttackEnd.paraDiff 10→11; moveContact.pes15TestMoveContact.defenceIdealRot.margin 5→3; moveContact.pes15TestMoveContact.defenceIdealRot.max 40→29; moveContact.pes15TestMoveContact.defenceIdealRot.min 30→9; moveContact.pes15TestMoveContact.defenceIdealRot.paraDown 15→8; moveContact.pes15TestMoveContact.dropOut.minSec 0.15→0.03; moveContact.pes15TestMoveContact.speedDiff.paraDown 5→4; moveContact.pes15TestMoveContact.speedDiff.value 5→4; stamina.effect 0.5→0.86; stamina.max 0.8→1.26; stamina.min 0.2→0.46; stopContact.keepBodyAngleSub 110→96.
- `pullAway.o` (10 of 15 differ): angleDiff 90→86; angleDiff_good 140→120; checkBallDist 22→20; checkBallDistBP 5→4.5; checkBallDistBP_good 6.5→5.5; checkBallDist_good 30→28; checkLastLineDist 7→6; checkLastLineDist_good 13→12; checkZ 27→40; eyeOff 0→1.
- `selectorVision.o` (10 of 30 differ): offerNum_diagonalRun[2] 1→2; offerNum_diagonalRun[4] 2→3; offerNum_lineBreak[2] 1→2; offerNum_lineBreak[4] 2→3; offerNum_pullAway[1] 0→1; offerNum_pullAway[3,4] 1→2; offerNum_spaceRunChance[2] 2→3; offerNum_spaceRunChance[3] 3→4; offerNum_spaceRunChance[4] 3→5.
- `spaceRun.o` (54 of 60 differ): angleDiff_chance 90→100; angleDiff_chance_good 110→125; angleDiff_second 60→70; angleDiff_second_good 90→95; chanceScore 100→102; chanceScore_good 85→88; checkAngleWidthSector 30→40; checkAngleWidthSector_good 50→69; checkAreaNum 2→3; checkAreaNum_good 4→5; checkBackDist_second 12→13; checkBackDist_second_good 17→19; checkBallDist_chance 25→27.5; checkBallDist_chance_good 28→33; checkBallDist_counter 16→19; checkBallDist_counter_good 20→25; checkBallDist_second 25→29.5; checkBallDist_second_good 30→40.5; checkBodyAngle 90→84; checkBodyAngle_good 110→105; checkCount 1→2; checkCount_good 2→3; checkFreeDist 27→30; checkFreeDist_good 32→35; checkInterceptScore 0→97; checkInterceptScore_good 10→105; checkLastLineDist_chance 19→24; checkLastLineDist_chance_good 23→30; checkMarginDist 3→6; checkMarginDist_good 5→11; checkMoveAngle 82→90; checkMoveAngle_good 100→111; checkOffsideLineDistX 15→12; checkOffsideLineDistX_counter 2→2.25; checkOffsideLineDistX_counter_good 2→2.25; checkOffsideLineDistX_good 18→12; checkPlayPointDist 40→41; checkPlayPointDistX 20→23; checkPlayPointDistX_good 24→36; checkPlayPointDist_good 40→42; checkScore 100→92; checkScore_good 90→98; checkSpaceRunCount_good 1→2; checkZ 40→41.5; checkZ_counter 13→14.5; checkZ_counter_good 18→20; checkZ_good 40→44; checkZ_second 20→24; checkZ_second_good 30→32; inOut 0→1; searchCount 4→5; searchCount_good 6→7; supportTime 3→4; supportTime_good 2→3.
- `subConcept.o` (21 of 50 differ): subConcept[1,3,4,7,8,10,11,12,13,16,18,19,20,22,23,24,25,29,30,31,47] 0→1.
- `support.o` (33 of 58 differ): angleMax 45→75; angleMaxAdjust 22.5→65; angleMaxFront 90→125; angleMaxMargin 22.5→85; angleSubAbsMax 135→170; baseDistMax 20→32; baseDistMaxAdjust 5→9; baseDistMaxMargin 5→9; baseXDistMax 15→20; baseXDistMaxAdjust 5→9; baseXDistMaxMargin 5→9; baseZDistMax 15→20; baseZDistMaxAdjust 5→9; baseZDistMaxMargin 5→9; distDash 12→20; distDashMargin 1→2; distDashQuick 8→11; distJog 2→3; distJogMargin 1→1.5; distMax 20→34; distMaxAdjust 15→24; distMaxMargin 5→9; distRequestPass 3→4; distRun 4→6; distRunMargin 1→4; distXDashQuick 10→12; supportDist 6→10; supportDistAdjust 3→6; supportDistAdjustBase_Long 3→2; supportDistSituation 5→4; supportDistSituationBack 8→5; supportDistSituationSide 12→10; waitTimeMax 1→0.01.
- `teamId.o` (7 fields): no differences.

## 6. `T+0xa53c` — what sets it and when (static)

### 6.1 Complete access set (OBS: every disp32 `0xa53c` in 0x140001000–0x142400000, de-duplicated)
Writes (5 stores, plus the address-take `lea rdx,[r14+0xa53c]` at 0x142154511 in `0x142153ed0`):
| VA | instruction | function | role |
|---|---|---|---|
| 0x140a41656 | `mov [rbx+0xa53c], sil` (0) | `FUN_140a415a0` | T init → 0 |
| 0x14041707d | `mov [rdi+0xa53c], al` | `FUN_1404161e0` | T copy (with the 0x14041706f read) |
| 0x14053618f | `mov byte [rbx+0xa53c], 0` | `FUN_140536170` | dead clear branch (below) |
| **0x1405361ed** | **`mov [rbx+0xa53c], dil`** | **`FUN_140536170`** | **the setter** |
| 0x14215d537 | `mov [r14+0xa53c], al` | `FUN_14215cf10` | restore from a stream (INF(med): save/replay state; paired `lea rdx,[r14+0xa53c]` in `0x142153ed0` = write side) |

Reads (18 incl. the copy's 0x14041706f; function starts by padding scan): `0x1405269bf` (0x140526890), `0x14052ee98` (0x14052eab0),
`0x14053598b` (`FUN_140535650`, hysteresis), `0x140535aa3` (0x140535a10), `0x1405361ca` (setter edge test),
`0x1405fa645` (`FUN_1405f9eb0`, defensive line), `0x140621f8d`, `0x140624353` (0x140623750), `0x140626001`,
`0x140629be6`, `0x14062ad24` (0x14062acf0), `0x1406327c4`, `0x14063310d`, `0x140633e2e` (0x140633940, attack level),
`0x1406347c7` (0x140634470), `0x140635fa7` (0x140635ab0), `0x140972020` (slide chain `FUN_140971ba0`: `L += 1` iff `== 1`).

### 6.2 Setter `FUN_140536170(ctx, T)` (OBS decompile `out/a53c_writers.c`, checked against raw)
```
if (FUN_141b92460(2) < 5) { T.a53c = 0; return; }   // FUN_141b92460 = `return 5` (OBS) → branch is dead
v = FUN_140535650(ctx, T) ? 1 : 0
if (v && G-team(side).a53c == 0)                     // rising edge (FUN_140a48a30(G, ctx.side) = same team's T)
    T.a540 = MI+0x1198                               // frame stamp of the rise (MI+0x1198 = frame counter, ClockLiveRE)
T.a53c = v                                           // 0x1405361ed
```

### 6.3 Predicate `FUN_140535650(ctx, T)` (OBS raw 0x140535650–0x1405359fd; names INF as marked)
```
late  = MI+0x1168 > 3  ||  (MI+0x1168 == 3 && FUN_140a74d70() >= 20)        // 0x14053574b–0x14053576d
k     = first of 8 team slots (G+0x2ad0 block +0xc400+side*0x2804, +0x2308+k*0x38) that is active, else 8
if k < 8:   proceed  iff  byte MA[+0x25ff4 + side*8 + k] <= 0               // 0x140535788–0x140535796
else:       own = goals(side), opp = goals(other)                          // FUN_1408c9df0(stats,5) = goal sum (MatchManagementRE)
            (two-leg branch adds FUN_140a7f0f0 per side when FUN_140a7f3a0 && FUN_140a7f580)
            opp <  own → return 0;  opp > own → proceed;  tie → proceed iff late
// proceed:
r     = (level(T) >= 3 && late && !FUN_140a55d40(G+0x180 ball, side, 2))   // level = FUN_140a60340(FUN_140a40bd0(T))
bx    = dir * MA+0x2482c                                                   // dir = (float)(int8)T+0x254; ball x in own attack frame
win   = bx > F[+0x18] - 5.0 ? 16*fps : (bx < 0 ? 60*fps : 32*fps)          // F = *(G+0x2aa8); 5.0 @0x14259222c
bias  = T.a53c ? -5.0 : 0                                                  // -5.0 @0x14259be7c (hysteresis)
if (dir * MA[0x3c + 0x14*side] + bias <= bx  &&  MI+0x11a0 >= 10) {         // MI+0x11a0 = possession timer (frames)
    T.a544++;  return (4*T.a544 >= win) ? 1 : r                            // a544 = consecutive-call counter
}
T.a544 = 0; return r
```
INF(med) meanings: `MI+0x1168` = match period (ClockRecordRE saw 3 in the second half); `FUN_140a74d70` = match minute
(it returns `floor(t/3600)` of the period clock `FUN_140a3c050(MI+0x1168)`, OBS; a sibling uses a 45-minute boundary,
so the minute is likely absolute, which would make `== 3 && minute ≥ 20` always true in the second half — open);
`MA[0x3c+0x14·side]` = the side's middle line x (DefensiveShapeRE: 0x38/0x3c/0x40 = front/mid/back);
`FUN_140a55d40(ball, side, 2)` = a ball-ownership state test (DefensiveShapeRE uses k = 3 for the press lane); the 8 slots +
the `MA+0x25ff4` byte = per-team active instruction/concept slots and their enables (INF(low); adjacent to the
`MA+0x25cf8/0x25d38` instruction arrays of TeamAILeftoversRE §1.1).

### 6.4 When it runs (OBS `out/a53c_caller.c`)
`FUN_1404f6c30` (team tick) → unconditional `call FUN_140535c60` at `0x1404f7955` → inside, only when
`T+0x255 == 0` (INF(med): team is CPU-driven; the same byte gates other AI in CarrierAIRE / AttackDecisionsRE) and
`phase == 0`, where `phase = MI+0x1198 & 3` (frame counter mod 4), forced to 2 (skip) when `MI+0x116c == 2 && MI+0x11d4 == 0`
(stoppage). In phase 0 the order is `FUN_1405379c0`, `FUN_1406369e0`, `FUN_1405360d0`, **`FUN_140536170`**, `FUN_140535a10`.
So the flag is re-evaluated **every 4th logic frame (~13.5 Hz)** per AI team, and `4·a544` approximates frames.

### 6.5 Plain-language rule (INF(med) on the semantic names; structure OBS)
`T+0xa53c` is a **chase/push-up flag** of a CPU team: it can turn on only if the team has no active instruction slot and
is losing (or level and `late`), or its first active slot's enable byte is ≤ 0; **and** then either the team is strong (level ≥ 3) late in the match without the
ball-state veto, or the ball has stayed ahead of the team's middle line (−5 m once on) with possession older than 10
frames for 16 s (ball beyond `F[6] − 5` ≈ 31 m into the opponent half), 32 s (ball between halfway and there) or 60 s
(ball in own half). It turns off
on the next phase-0 frame where neither holds. Live TeamAILeftoversRE: team 1 (losing, out of possession) = 1, team 0 = 0 —
consistent. Effects: slide level `L += 1`, plus 14 other read sites (defensive line, attack level, marking, cover).

## 7. Open items (each with the exact next step)

1. **Readers for centeringGet, combination, lineBreak, overlap, teamId** (no `mov edx, imm` getter call found).
   Next: scan `lea edx,[r+imm]` / table-driven calls to `FUN_141e5d4c0`/`FUN_141e5ca20` for indices 184/185/190/191/198
   and xrefs to the vtables in §2.1 (`readers.tsv` method, extended).
2. **Field-read heuristic (§4.2) is INF(med).** Next: decompile the cluster functions (`0x1409589f0`, `0x140533bc0`,
   `0x140613910`, `0x14077a377`, `0x140612660`, `0x1405aca50`) with `dec.sh` and confirm each `[obj+off]` use.
3. **`MI+0x1168` period values and `FUN_140a74d70` units** (decides whether the `== 3 && minute ≥ 20` arm is ever
   false). Next: decompile `FUN_140a3c070`/`FUN_140a74db0` and join with ClockRecordRE's period table; or one live
   read of `MI+0x1168` + `FUN_140a74d70` inputs in each half (ClockLiveRE window).
4. **Identity of the 8 slots and `MA+0x25ff4`** in the predicate. Next: decompile `FUN_140a7d420/7d450/7daf0/7db40`
   and find writers of `+0x25ff4` (same disp32 scan as §6.1).
5. **Live confirmation of §6** (optional): sample `T+0xa53c/a540/a544`, score, `MI+0x11a0`, ball x and
   `MA+0x3c+0x14·side` at 20 Hz in a second half; expect rises only on frames ≡ 0 mod 4 and `a540` = that frame.
6. **Slot 2 of defence and pullAway** is VM-obfuscated; their blob layout is inferred from the rule and checked by anchors
   (defence 5 vanilla + 1 Righetti, pullAway none). Next: one live read of registry 186/193 objects vs `ctmap_all.tsv`.

## 8. Evidence ledger

| ID | claim | conf | evidence |
|---|---|---|---|
| EV-TCR-0001 | constant_team registry 183–198 = the 16 objects in name order; records at 0x142b49810+16·i | Confirmed | table bytes; matches 0xb7/0xbc/0xc2/0xc4 uses |
| EV-TCR-0002 | vtable slot 1 = JSON deser (name→struct off/type), slot 2 = binary loader | Confirmed | 0x141e79ce0, 0x15b482750, 0x15b455d80, 0x15b3102d0 |
| EV-TCR-0003 | blob layout rule (sorted keys, 4 B, packed bools, offsets for lists/sub-objects; flat struct = blob+8) | Confirmed (5 loaders) / High (2 obfuscated) | §2.3; 87 vanilla + 32 live + subConcept anchors |
| EV-TCR-0004 | full named map, 720 fields, and the 445-field stock/mod diff | Confirmed | `ctmap.py` (asserts pass), `ctmap_all.tsv`, `ctmap_diff.tsv` |
| EV-TCR-0005 | mod = Righetti 0.6.4 derivative with re-tunes | High | 14/17 Righetti values exact |
| EV-TCR-0006 | alphabetical-index rule killed for basePosition/support | Confirmed | packed bools 0x108–0x10a, 0x134–0x136; support style list |
| EV-TCR-0007 | getter sites + 76 field reads | Confirmed (sites) / Tentative-med (reads) | `readers.tsv` |
| EV-TCR-0008 | T+0xa53c stores = 5 sites; setter store 0x1405361ed in FUN_140536170 | Confirmed | disp32 scan; decompile + raw |
| EV-TCR-0009 | setter value = FUN_140535650 predicate (§6.3); rising edge stamps T+0xa540 = MI+0x1198 | Confirmed (structure) / Tentative (field meanings) | raw 0x140535650–0x1405359fd |
| EV-TCR-0010 | evaluated every 4th frame from team tick, AI teams only (T+0x255==0), skipped in stoppage 2 | Confirmed (code) / High (meaning of 0x255) | 0x1404f7955, FUN_140535c60 |
| EV-TCR-0011 | TeamAILeftovers "no displacement store" for a53c is wrong | Confirmed | 0x1405361ed |
