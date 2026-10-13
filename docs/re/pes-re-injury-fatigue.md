# PES 2021 stamina, fatigue and injury — evidence report

Scope: read-only RE of PES 2021 (PES2021.exe image in `<scratch>/pesmem.bin`, base 0x140000000, 64 MB live
`.trace`/`.rdata`/`.data` dump; on-disk `PES2021.exe`; the install VFS read through the repo tool
`pesfmt_dump`). Goal: the stamina drain/regeneration, the fatigue presentation and the injury system as
implementable rules, with the runtime source of every constant. **No PES-derived bytes are committed**;
every table below is a layout plus a handful of values.

Tags: **OBS** = observed, with the method. **INF** = inferred, with confidence. **EXT** = third-party claim.
The game was **not running** during this session (`pgrep -f PES2021.exe` empty); every datum is from the
image, the bins or the exe strings. This file extends `pes-re-matchlogic.md` (ability/attribute
plumbing), `pes-re-sim-motion-mapping.md` (clip selection) and `pes-re-tactics-clock.md` (clock/stoppage).

**File-offset rule used:** file offset = `VA − 0x140000000` (verified on `.rdata` string `s_lossTime`
and floats; the PE section table in the image is partly obfuscated, e.g. `.impdata` vsize overlaps
`.reloc`, so the on-disk `PointerToRawData` values are not used).

---

## 0. Status per priority item

| # | Item | Status | Where |
|---|---|---|---|
| 1 | Stamina model: drain, regen, thresholds | **partly closed** — the AI's stamina *tiering* is literal (OBS), the fatigue-driven clip *families* are literal (OBS), the numeric drain formula is **open** | §2, §5 |
| 2 | Fatigue presentation | **closed as a clip/state inventory** (OBS), incl. exact clip names and durations | §3 |
| 3 | Injury: states, parts, causes, severity, time-out, recovery, substitution | **closed for the state vocabulary and enums** (OBS); severity formula and time-out numbers **open** (the tuning object is decoded but its field semantics are not) | §4 |
| 4 | Where the values live | **closed** — `common/match/constant/constant_match.bin`, `injury.o` decoded with its full layout and values; class registry enumerated | §1 |
| 5 | GF-divergence list | done | §6 |
| 6 | Open items + what was tried | done | §7 |

---

## 1. Where the values live (runtime-readable)

### 1.1 The two constant containers

| Container | Disk size | WESYS zlib → flat size | Objects |
|---|---|---|---|
| `common/match/constant/constant_match.bin` | 4,817 B | 22,544 B | 27 |
| `common/match/constant/constant_player.bin` | 40,643 B | 82,784 B | 36 |

(OBS: `pesfmt_dump cat` for the bytes; my decoder `if/wesys.py` implements exactly
`src/pes/uniform.cpp:ParseUniformTable`: 16-byte header (`ff 10 51 'WESYS'`, u32 zlib size), zlib from
+0x10, then `u32 count` at 0, `u32 tableOffset` at 4, then `count × 12-byte` records
`{u32 off, u32 size, u32 nameOffset}`, every name a NUL-terminated string in the **same flat space**
(`nameOffset` is absolute within the flat blob). Objects are the `[off, off+size)` slices; some
objects' fields are self-relative u32 pointers into the flat space, but for `injury.o` they are plain
integers — see §1.3.)

Both files are read by the same loader, which is driven by a **compiled-in path table**:
- `cpk_dat/common/match/constant/constant_match.bin` … `constant_shootAging.bin` — 9 consecutive
  `char*` entries in the image at **0x143445e70** (OBS: strings at 0x142593838 … 0x1425939f8).
- The loader: `FUN_140419621` (init) loops 9 times over that table calling
  `FUN_141e5bc40(*(0x1437f4800), path)` @0x141e5bc40 → `FUN_141e5bee0` (cache lookup by path, else
  `FUN_141e5d700` → `FUN_141e5d8c0`, a 0x98-byte wrapper object holding a 0x80-byte name buffer and a
  file handle, vtable assigned in the ctor). `FUN_14069f1c0` is only a refcount bump. So the
  container is opened once at boot and kept; the per-object parse happens on the consumer side.

### 1.2 Object name ↔ engine class (OBS)

The exe carries the RTTI TypeDescriptors of all 87 constant classes in `.data` at
**0x1436b5e58 … 0x1436b7e48** (OBS: regex `.\?AV<X>@constant@@` with the shared type_info vftable
0x142992d80; every entry is `{u64 vftable, u64 0, char name[], pad}`). The **object name in the WESYS
record table is the class name with its first letter lower-cased plus `.o`** — this holds for every one
of the 63 object names I could match, e.g. `Injury`→`injury.o`, `Dribble`→`dribble.o`,
`AnimeAgingFreeMove`→`animeAgingFreeMove.o`, `Pes15Test`→`pes15Test.o`, `BallplayerGk`→`ballplayerGk.o`,
`InplayDemo`→`inplayDemo.o`, `Motivation`→`motivation.o` (OBS: class list vs object list, 1:1).
So an engine that ships the loader can locate `injury.o` without any name table.

The 87 classes (OBS, addresses in the image): ArchiveFile, ConstantFile, ConstantData,
BallplayerAnalyze, MlScreenShot, BallplayerClear, TutorialNone, Dribble, Shoot, PairAnime, BlockDebug,
CenteringGet, Matchup, BallplayerDebug, ModeMatchup, BallplayerDribble, SetplayGuideGoalKick, Feint,
Motivation, Combination, Sliding, CameraSetplay, AnimeAgingDribble, BallplayerFeint, MoveOnPass,
SetplayGuideFreeKickFar, TutorialSetting, AnimeAgingFreeMove, PullAway, Pes15Test, Tackle, Command,
BallplayerGk, Defence, Trap, Flypass, AnimeAgingKick, TeamEmotion, SelectorVision, CpuLevel,
AnimeAgingTrap, BallplayerPass, DefenceCover, StudiumPositionData, Passget, Freekick,
BallplayerPlayImage, Cursor, SpaceRun, Throughpass, BallplayerSetplay, Throwin, Freemove,
SetplayGuideFreeKickMiddle, DefenceMark, SubConcept, BallplayerShoot, FeintCommand, Ball, Rating, Gk,
Penaltykick, UserPlayTendencyTest, Support, Block, DiagonalRun, Avoid, GoalKick, SetplayGuideCommon,
PlayStyle, LineBreak, TeamId, **Injury**, BasePosition, Centering, PositionNone,
SetplayGuideFreeKickNear, Overlap, Press, Ballplayer, **InplayDemo**, Reaction, Contact, Grounderpass,
PesSmart, SetplayGuideCornerKick, CameraInplay.

### 1.3 `injury.o` — path, offset rule, layout (OBS)

- Container: `common/match/constant/constant_match.bin` (dt18_all.cpk; the byte-identical
  `dt18_all - Copia.cpk` is skipped by the mounter).
- Record: one of the 27 objects, at **flat offset 0x32f0, size 48 bytes** of the *inflated* blob
  (OBS: my decoder prints `injury.o off=0x00032f0 size=48`; the offsets in the record table are
  absolute within the inflated blob, which starts at flat 0 — the record table itself sits early in
  that blob, and `injury.o` is the 12th record).
- Runtime source rule for the engine: open the container, inflate, walk the 27 records, match the
  record whose name is `injury.o`, and the 48 bytes at `[off, off+48)` are the tuning record.

**Field layout (OBS, raw):** 12 × u32, all plain integers, no pointers, no floats:

| field | offset | value |
|---|---|---|
| 0 | +0x00 | 0 |
| 1 | +0x04 | 120 |
| 2 | +0x08 | 220 |
| 3 | +0x0C | 180 |
| 4 | +0x10 | 240 |
| 5 | +0x14 | 120 |
| 6 | +0x18 | 250 |
| 7 | +0x1C | 160 |
| 8 | +0x20 | 220 |
| 9 | +0x24 | 240 |
| 10 | +0x28 | 230 |
| 11 | +0x2C | 0 |

(OBS: the 48 bytes are, as little-endian u32s, `00000000 00000078 000000dc 000000b4 000000f0
00000078 000000fa 000000a0 000000dc 000000f0 000000e6 00000000` (decimal 0, 120, 220, 180, 240,
120, 250, 160, 220, 240, 230, 0); a search of the whole image for these bytes and for the 10 non-zero
values individually finds **no runtime copy** in `.data`/`.bss`, i.e. the object is parsed into a heap
instance whose consumers I could not locate — see §7.)

Candidate readings of those ten numbers (all **INF**, none confirmed):
- 5 × (min,max) pairs starting at +0x04: `(120,220) (180,240) (120,250) (160,220) (240,230)`; field 0
  and field 11 are 0. If the unit is **0.1 s** the pairs read 12.0–22.0 s, 18.0–24.0 s,
  12.0–25.0 s, 16.0–22.0 s, 24.0–23.0 s — i.e. per-severity **time-out ranges** in the 12–25 s band
  that matches a real injury stoppage. If the unit is **frames at 30 fps** they read 4.0–7.3 s, which
  is too short for a stoppage. The last pair inverts (240 ≥ 230), so either the pair split is wrong
  or the top bucket is a fixed 24 s with a 23 s fallback.
- alternative: 10 independent per-part/per-severity thresholds.

### 1.4 Other objects relevant to this area (OBS, decoded)

`constant_player.bin` — `contact.o` (80 B, all floats: 56.0, 0.75, 0.45, 0.5, 0.5, 0.15, 0.2, 0.6,
0.5, 0.8, 0.8, 24.0, 16.0, 0.2, 0.04, 0.15, 0.03), `tackle.o` (176 B incl. 0.4/0.5/0.7 severity
coefficients and 135/157.5/168.75/105° angle tables), `sliding.o` (32 B: 2.0, 0.75, 14.0, 1.2, 0.9),
`block.o`, `matchup.o`, `motivation.o` (32 B: {16, 0, 0, 0, 0.95, 1.05}), `dribble.o`, `gk.o`,
`press.o`, `reaction.o`, `feint.o` (all 16 B), `flypass/grounderpass/throughpass/passget/shoot/trap/
playStyle/ballplayer/ballplayerDebug` (too large to enumerate here, same record rule).
`constant_match.bin` — `animeAgingFreeMove.o` (32 B: `{1, 128, 22.5f, 16, 0, 0, 0, 0}`),
`animeAgingDribble.o` (48 B: `{257, 128, 0, 90.0f, 1, 3, 22.5f, 16, 0, 1, 0, 0}`),
`animeAgingKick.o` (32 B: `{1, 22.5f, 16, 0, 128, 0, 0, 0}`),
`animeAgingTrap.o` (64 B: `{22.5f, 16, 0, 22.5f, 3, 1.0f, 0.6f, 30, 1, 0, 1.5f, 0, 128, 0, 0, 0}`),
`inplayDemo.o` (32 B: `{0.3f, 5.0f, 5.0f, 3.0f, 256, 0, 0, 0}`), `cursor.o`, `command.o`, `pesSmart.o`,
`teamEmotion.o` (448 B — the tail is an inline name pool: `GOAL`, `GOAL_WO…`, `ALL_OFF`,
`OFF_SIDE`, `DE_HALF`, `CHANCE`, `OUT`, `PRESS`, `TAKE`, `…`).

**The `animeAging*` objects are a small-integer/few-float shape (32–64 B) matching the four animation
families that carry a `_tired` variant (free-move/locomotion, dribble, kick, trap)** — INF (med-low):
their contents are dominated by **22.5f / 90.0f** (angle steps, the same 22.5° bucket PES uses for the
Animetable columns) plus small integers {1, 3, 16, 30} and floats {0.6, 1.0, 1.5}. Two readings remain
open and I could not separate them: (a) *age*-driven animation aging (a young player's run cycle
differs from an old player's — the word "aging" points here), or (b) *fatigue*-driven aging (the
families that have `_tired` clips). Either way it is an **animation-blend** parameter set, not a
physics slowdown: PES changes the clip/blend, not the locomotion model. What each field means is
**open** (§7).

### 1.5 Other runtime-readable sources used here

- Clip metadata: `common/anime/Mbinfo/json/anim_infos.json` (15.7 MB, plain JSON in dt13) — per-clip
  `end_frame`, `cancel_speed`, `root_pos_blend_*`, `foot_type`, `is_on_memory`. Used for every
  duration in §3/§4 (OBS).
- Animation selection tables (`Animetable.bin`, `Freemove.bin`, …) — see `pes-re-sim-motion-mapping.md`
  §2/§6; the `stagger_*`/`fall_*`/`riseup_*` rows are enumerated there.
- Ability plumbing (Player.bin id 0x22 = Stamina, 6-bit 40..99, UI min/max from the accessor table
  0x143529930) — `pes-re-matchlogic.md` §1.2.
- HUD strings: `staminaGauge_on` @0x1426283c0, `staminaGauge_off` @0x1426283d0, `d2_stamina_gauge`,
  `icon_injury`, `BL_injury`.

---

## 2. Stamina: what PES actually does (OBS)

### 2.1 The stamina value has **no script-visible name** — it is pure C++

OBS: an exhaustive scan of every `[A-Za-z0-9_]*(stamina|STAMINA)[A-Za-z0-9_]*` string in the image
yields only 27 hits, all of them one of:
- the **ability name** `STAMINA` @0x1425d80f8 (in the ability-name pool next to `SPEED`,
  `PHYSICAL_CONTACT` — the Player.bin ability enum, id 0x22, 6-bit, 40..99);
- the **HUD gauge** `staminaGauge_on/off` @0x1426283c0/0x1426283d0 and `d2_stamina_gauge`
  @0x142b64c4b;
- **network/mail** serialization names `m_stamina` @0x142afcc98, `stamina` @0x142b18e48,
  `NUM_STAMINA` @0x14295ca08, `tiredness` (12 separate copies, all in the `gameplayer_list`
  mail/Lua structures at 0x142ae3320 … 0x142b23740);
- the AI play-data buckets of §2.2.

There is **no in-match Lua/SD field named stamina or fatigue** (the SD enum-name table at
**0x14344a000** — 213 entries, OBS — contains `InjuryPart`, `InjuryCause`, `FallDownReason`,
`SubstitutionSituation`, `SubstitutionType`, `SubstitutionPlayerTypeIn/Out`,
`LeadingRiseOrStretcherDemo`, `DemoMomentKind`, `EnterDemoType/Status`, `EndDemoStatus`,
`ParameterAbility`, … but no stamina/fatigue entry). The matching SD *value* name table
(**0x14344c1e0**, 177 entries, OBS) carries the statistics vocabulary (`WALK/JOG/RUN/DASH`,
`GOAL`, `SHOOT_SUCCESS`, `TACKLE_SUCCESS`, `C/B/A/S/SS/SSS` condition grades, `STOP_*`,
`STEAL_*`, `END_KIND_*` …) and likewise has no stamina or fatigue value.

One demo-status value **is** injury related (OBS): the `ScriptBind` cluster at **0x1425ec8a8**
registers a demo-kind/status list whose values include `injury worry` between `target decide` and
`run board`:
`Sucsses, kind demo, wait blend, foot step, target decide, injury worry, run board, demo wait,
girigiri goal, contact goal, Undefinded` — so the in-play demo layer has a named **injury-worry**
status (the pre-injury worry animation) distinct from the injury itself.

INF (high): the in-match stamina/fatigue value lives inside the player game object (no name), is
updated per tick in C++, and only surfaces through (a) the animation family swap of §3 and (b) the AI
tier of §2.2. **GF must therefore treat PES stamina as a first-class sim variable that it owns, not
read it from anywhere.**

### 2.2 The AI's stamina tiering is literal (OBS)

The AI's "play data" name pool (image **0x142b407a8 … 0x142b40ad8**, contiguous NUL-terminated
strings — CORRECTED 2026-10-13 (second pass); the first pass quoted 0x142b411a8 …, a 0xA00 error from
a wrong file-offset rule: **file offset = VA − 0x140000000** throughout this image, there is no
`.rdata` shift)
contains exactly **20 stamina-keyed names**, five action families × four tiers:

```
PLAY_DATA_SHOOT_DASH_STAMINA_20_15        PLAY_DATA_SHOOT_DASH_STAMINA_15_10
PLAY_DATA_SHOOT_DASH_STAMINA_10_5         PLAY_DATA_SHOOT_DASH_STAMINA_5_0
… same for GROUNDER_PASS, FLY_PASS, THROUGH_PASS, CROSS
```

OBS reading: when a player **dashes** and then performs one of {shoot, grounder pass, fly pass,
through pass, cross}, the AI looks up a *different tuned row* depending on his stamina, in four
buckets `20..15 / 15..10 / 10..5 / 5..0`. INF (high): the internal stamina quantity is therefore
effectively a **0..20 scale** (or a fraction whose fifth-bands are named after 20), and the *only*
behavioural stamina thresholds PES ships are **20, 15, 10, 5, 0** — five boundaries, four bands.
No other stamina-keyed AI table exists in the pool (OBS: the pool holds 1,112 `PLAY_DATA_*` names;
only these 20 contain `STAMINA`).

Note there are **no `WALK`/`SPRINT` variants** of these names (OBS), so the tier is not keyed by speed
class, only by the action + the dash flag.

### 2.2b The condition display is graded 0..9 (OBS)

The player-card UI registers a condition icon with **ten grades**: `cmnIconCondition` (string at
**0x1427befc8**, a second copy at 0x1427bb998 — CORRECTED 2026-10-13 (second pass), the first pass
quoted 0x1427bb9b0 for this string, which is the *position* pool) with
`condition_0` … `condition_9` at **0x1427bb9b0 … 0x1427bba40** (OBS, contiguous name pool). The
match/team UI also has `BL_condition_0..4` + `BL_condition_rapid` at 0x142730938..0x142730988 (OBS).
INF (med): a 0..20 in-match stamina maps onto the 10 display grades at 2 units per grade, which is
consistent with the 4 AI tiers of §2.2 sitting at 20/15/10/5/0 (i.e. every 5 stamina units = 2.5
display grades). The mapping is a display detail, not a model input.

### 2.3 The speed cap / willingness thresholds — open

- OBS: the four tier boundaries are the only literal stamina thresholds found anywhere (§2.2), and
  they apply to the *action choice*, not to the speed cap.
- The Lua/SD concept pool has `WARNING_FATIGUE` @0x14295ab68, `FATIGUE_OVER_DAMAGE` @0x14295ab90,
  `ACCUMULATE_FATIGUE` @0x14295abe0 and `ACCUMULATE` @0x14295abb8 (OBS) — INF: the AI concept layer
  knows two fatigue states (warning, over-damage) and one "fatigue accumulated" counter. These are
  *concept* names, not numeric thresholds.
- INF (med): the actual speed cap and the press/shoot willingness gate are computed in the C++ player
  update from the stamina value with compiled-in constants; I could not locate that function (§7).

---

## 3. Fatigue presentation (OBS)

Fatigue is presented by **clip-family swap plus a dedicated in-play demo**, not by a model or gait
change that the sim has to reproduce:
- Locomotion family swap: `run_4_2_tired` (end_frame 150), `run_4_2_tired_v2` (74),
  `run_turn_run_2_2_tired`, `run_turn_run_2_4_tired`, `run_turn_run_3_3_tired`,
  `run_turn_run_4_4_tired`, `goto_run_0_4_tired` (all named in the image clip-name table
  0x1427b…, OBS; the last five are mtar entries with no `anim_infos.json` record in dt13).
- Idle family: `idle_tired_v4` (300), `idle_tired_v5` (35), `idle_tired_v6` (50),
  `idle_tired_v7` (282) — OBS (end_frame from anim_infos.json).
- Set-piece/idle demos: `demo_move_tired_000`, `demo_move_tired_045_1/2`, `demo_move_tired_090`,
  `demo_move_tired_3_0`, `demo_move_tired_nose`, `demo_move_tired_sweat`, `demo_moveTired_0_0`,
  `kickoff_loop_tired`, `kickoff_loop_tired_mild1/mild2`, `HalfEnd_1_1_Tired`,
  `HalfEnd_3_0_Tired` (all OBS as exe strings; the `_mild1/2` and `_sweat` variants give a graded
  presentation).
- Anime layer: the RTTI class **`InplayDemoFatigueInjury`** (TD at 0x14361be10, OBS) and the animation
  kind **`ANIME_DEMO_INPLAY_FATIGUE_INJURY`** @0x142652b98 next to `ANIME_DEMO_INJURY` @0x142652c40
  (OBS) — i.e. fatigue and injury share one in-play demo channel, with a warning clip first.
- `animeAging*` tuning objects (§1.4) — the engine-side aging of these families.
- HUD: `staminaGauge_on/off` (OBS).
- A named **injury-worry** demo status exists in the in-play demo layer (`injury worry`, §2.1) — the
  "he's hurting" beat that precedes a real injury (INF, high).

Rule for GF: **fatigue = a clip-family/sublibrary switch driven by a 0..20 stamina value**, with the
switch happening at the AI tier boundaries (§2.2); the visible "gait change" is PES's `_tired` clips,
which GF must obtain from the PES clip set rather than synthesise. (The `animeAging*` tuning objects
are the engine-side parameter set for the aging of these families, though whether "aging" there means
age or fatigue is open, §1.4.)

---

## 4. Injury (OBS)

### 4.1 The states — the six-state situation machine the motion report refers to

OBS from the image's injury debug-print pool (`[INJURY]` tag strings at 0x1425ed7f0 …
0x1425edc80, each followed by a Shift-JIS message):

| string (VA) | message | reading |
|---|---|---|
| 0x1425ed800 | `Can't Injury action (action=%d).` | action gate |
| 0x1425ed838 | `OrderInfo == NULL pno=%d` | per-player order lookup |
| 0x1425ed868 | `memberInfo is not valid. pno=%d.` | roster lookup |
| 0x1425ed8a0 | `怪我症度 injuryLevel=%d` ("injury severity") | **injuryLevel** |
| 0x1425ed8d0 | `怪我痛み injuryOuch=%d` ("injury pain") | **injuryOuch** |
| 0x1425ed900 | `？！BoutofplayReason=%d` | **BoutofplayReason** (why he's out of play) |
| 0x1425ed940 | `退場 C.S.[P K] …` | ejection / card + penalty-kick handling |
| 0x1425ed978 | `PK L …` | penalty-kick branch |
| 0x1425ed9a0 | `inplay … GK …` | in-play GK branch |
| 0x1425ed9d0 | `DemoState 一時 C.v=%d` | **DemoState** (the demo state) |
| 0x1425eda58 … | `TO INJURY_LOOP_START`, `INJURY_LOOP_START`, `TO INJURY_LOOP`, `INJURY_LOOP`, `INJURY_LOOP timer=%d`, `TO INJURY_STANDUP(INJURY_LOOP)`, `INJURY_STANDUP(INJURY_LOOP_START)`, `INJURY_STANDUP` | **the state machine** |

So the machine is (OBS, literal): `INJURY_LOOP_START → INJURY_LOOP (timed) → INJURY_STANDUP`, with
the state reachable from either the loop-start or the loop, and `injuryLevel` + `injuryOuch`
published per player. The "six-state injury situation machine" is the combination of that FSM with
`BoutofplayReason`/`DemoState`; the exact six encodings are **open** (§7).

### 4.2 Injury parts and causes (OBS, exact enums)

`INJURY_PART_*` name pool, in table order (OBS, image 0x142b42fe8 … 0x142b43150):
`INJURY_PART_NOTHING`, `INJURY_PART_LEFT_THIGH`, `INJURY_PART_LEFT_LEG`, `INJURY_PART_LEFT_FOOT`,
`INJURY_PART_RIGHT_THIGH`, `INJURY_PART_RIGHT_LEG`, `INJURY_PART_RIGHT_FOOT` — **7 parts, lower body
only, left/right symmetric, nothing above the thigh** (OBS: that is the complete set in the pool).
Causes (OBS, the `PLAY_DATA_*` pool): `PLAY_DATA_INJURY_CONTACT`, `PLAY_DATA_INJURY_FATIGUE`,
`PLAY_DATA_INJURY_FATIGUE_WARN` — **contact, fatigue, and a fatigue warning that precedes a fatigue
injury**. Warning/omen state: `MOTION_KIND_INJURY_OMEN` @0x142b45d48 and `INJURY_OMEN` (OBS).
Absence icons: `injury_0`, `injury_1`, `injury_2`, `injury_3` @0x1427beae0 (OBS — four graded absence
states in the UI).
Referee/stat hooks: `PLAY_DATA_STAGGER_BY_ENEMY_TACKLE`, `PLAY_DATA_FALLDOWN_BY_ENEMY_TACKLE`,
`PLAY_DATA_STAGGER_BY_ENEMY_SLIDING`, `PLAY_DATA_FALLDOWN_BY_ENEMY_SLIDING`,
`PLAY_DATA_STAGGER_NUM`, `PLAY_DATA_FALLDOWN_NUM`, `PLAY_DATA_INJURY_CONTACT/FATIGUE/FATIGUE_WARN`
(OBS) — i.e. the statistics layer counts a **stagger** and a **fall** separately, and each is split
into *by tackle* and *by sliding*.

### 4.3 How a player gets injured

- **Contact cause** (OBS): the tackle/slide contact path. The `tackle.o` and `sliding.o` tuning
  objects carry the severity coefficients (e.g. `sliding.o` 32 B = {2.0, 0.75, 14.0, 1.2, 0.9};
  `tackle.o` 176 B includes 0.4/0.5/0.7 triples and a 105/135/157.5/168.75° angle table), and the
  stat counters distinguish stagger-by-tackle / falldown-by-tackle / stagger-by-sliding /
  falldown-by-sliding (§4.2). INF (high): the tackle *type* (standing vs sliding) and the contact
  geometry pick the stagger-vs-fall branch, and severity is scored in `contact.o`/`tackle.o`.
- **Fatigue cause** (OBS): `PLAY_DATA_INJURY_FATIGUE` with a `_WARN` precursor, i.e. a player whose
  stamina is exhausted can be injured *without contact*; GF has no such path (§6).
- Injury **resistance** is a Player.bin ability (id 0x2E, 2-bit +1 → C/B/A, `pes-re-matchlogic.md`
  §1.2) — OBS that the rating exists; how it scales the probability is **open**.

### 4.4 Time out, recovery, substitution

- Time out / lie duration: the `injury.o` numbers of §1.3 (open semantics) plus the clips of §4.5
  (`endure` loops of 112–165 frames = 3.7–5.5 s at 30 fps, `rise` 108–149 frames, `comeback` 138
  frames) — OBS as clip inventory, INF as the mapping to `INJURY_LOOP timer`.
- Substitution vocabulary (OBS, the SD enum-name table 0x14344a000): `SubstitutionSituation`,
  `SubstitutionType`, `SubstitutionPlayerTypeIn`, `SubstitutionPlayerTypeOut`, and
  `LeadingRiseOrStretcherDemo` — i.e. PES has an explicit **stretcher** demo state and a *leading
  rise* state, and a typed substitution reason. The numeric rules are **open** (§7).
- Stretcher/carry-off: the absence icons `injury_0..3` and the `LeadingRiseOrStretcherDemo` enum (OBS).
- The referee's involvement is the existing `judge_*` clip rows (OBS,
  `pes-re-sim-motion-mapping.md` §9.5).

### 4.5 The injury clip chain, with durations (OBS)

Lie-state enum, from `anim_infos.json` → `lists.democonnect_state` (OBS):
`[0] knee_l, [1] knee_r, [2] allfours, [3] facedown, [4] faceup, [5] fallside, [6] oblique,
[7] run, [8] slant, [9] side, [10] backslant, [11] back, [12] idle, [13] rise`.
(The motion report's "2 allfours / 3 facedown / 4 faceup" matches these indices exactly — OBS cross-check.)

The `dm_injury_*` family (OBS, `anim_infos.json` records, `end_frame` at 30 fps):

| clip | end_frame | sec |
|---|---|---|
| `dm_injury_allfours_endure_Rside_Rknee` | 115 | 3.83 |
| `dm_injury_endureLoop_Rside_Rknee` | 112 | 3.73 |
| `dm_injury_facedown_endure_Rside_Rknee` | 135 | 4.50 |
| `dm_injury_faceup_endure_Rknee` | 90 | 3.00 |
| `dm_injury_endureLoop_Rknee` | 165 | 5.50 |
| `dm_injury_tumbleLoop_Rknee` | 71 | 2.37 |
| `dm_injury_rise_Rknee` | 108 | 3.60 |
| `dm_injury_rise_Rside_Rknee` | 149 | 4.97 |
| `dm_injury_comeback` | 138 | 4.60 |
| `dm_injury_comeback_sit_loop_01` | 1 | 0.03 |

Read as: **endure** (loop, keyed by lie state and by side) → **rise** → **comeback** (walk-off or
sit-loop). The non-knockdown clips are `stagger_*` (49 rows / 233 cells) and `fall_*` (62 rows /
446 cells) and `riseup_<allfours|facedown|faceup>_0_{0,3}` (6 rows) — see
`pes-re-sim-motion-mapping.md` §1.3/§9.3 for the full row grammar.
Sample durations (OBS, same source): `fall_upbody_3_0_000_hold` 230 f, `fall_upbody_3_0_180_hold`
242 f, `fall_lowbody_3_0_090_plunge` 188 f, `fall_lowbody_0_0_180_kneedown` 150 f,
`fall_lowbody_3_0_180_divehead_slide` 170 f, `stagger_lowbody_3_3_000_hard` 64 f,
`stagger_upbody_3_3_090_quickly` 46 f, `riseup_*_0_0_*` 36–50 f.

---

## 4b. Stamina — second pass (2026-10-13, decompiler working)

**CORRECTED 2026-10-13 (orchestrator).** The on-disk/live labels above are reversed. Measured: on-disk exe `.data 0x14351ad20` = 1/48 (the default); live on the title screen = 1/48; live in a match (3 samples) = 1/54; `pesmem.bin` (dumped mid-match) = 1/54. So the slot defaults to 1/48 and is set to 1/54 while a match runs - never read it from the exe file for match timing.

**RESOLVED 2026-10-13 (orchestrator, live in-match read).** During a match, `DAT_14351ad20` = 0.0185185 (1/54) on three samples 0.5 s apart, with `DAT_14351ae54` = 54.0 and `DAT_14351ae50` = 54. The 1/48 value above is title-screen only (the period at `entObj+0x5c` is 1.125 there). **The match runs on a 1/54 s step**; the clock, camera and ball reports' 54 Hz assumption holds.


**Status: the formula is still OPEN.** This section records what the second pass proved (all of it
negative for the drain itself, all of it reusable), plus two corrections.

### 4b.1 The tick/step chain (OBS)

The sim step is a runtime-initialised `.data` float, not a compiled-in `1/54`:

| step | what | address |
|---|---|---|
| divisor | `FUN_141e532d0()` returns the global `DAT_14351ae54` | @0x141e532d0 |
| `DAT_14351ae54` | initialised from the rodata constant **54.0** @0x14259d624 by `FUN_141e532f0`, which also writes int `54` (0x36) into `DAT_14351ae50` | @0x141e532f0 |
| period | `*(float*)(entObj + 0x5c)` — a per-match period on an entity object | — |
| writer | `FUN_14042b6c0`: `dt = *(float*)(lVar3 + 0x5c) / (float)FUN_141e532d0()` then `FUN_141b985a0(uVar4, xmm6)` | @0x14042b6c0 |
| store | `FUN_141b985a0` → `movss %xmm6,[rip+0x198275a]` = `DAT_14351ad20` | @0x141b985a0 / @0x141b985be |

So any drain must be expressed as `rate × DAT_14351ad20`, and the *only* writer of `DAT_14351ad20`
is this chain (@0x141b985be, single site).

### 4b.2 Live-process observation (OBS, title screen only)

Read read-only from `/proc/<pid>/mem` of the running `PES2021.exe` (game **not** driven into a
match; image base 0x140000000, `r--p`/`r-xp`/`rw-p` regions):

- `DAT_14351ad20` on disk = `0.0185185` (= 1/54); **live = `0.0208333` (= 1/48)**.
- `DAT_14351ae54` = 54.0 on disk **and** live — unchanged.
- therefore the live period at `entObj+0x5c` is `0.0208333 × 54 = 1.125`.

**This is a title-screen reading.** Whether the match itself runs at 1/48 or 1/54 is
**UNVERIFIED** until the slot is read during a match; the value may be re-derived at match start
(and the ball-spin-scale case in this report shows `.data` slots do get re-initialised). GF should
read the period from its own match setup, not assume either number.

### 4b.3 Why the tier names have no xrefs (OBS)

The 20 `PLAY_DATA_*_DASH_STAMINA_*` strings at 0x142b407a8 … 0x142b40ad8 (corrected, §2.2) have
**zero code references of any kind**: no rip-relative `lea`/`mov`, no u64/u32/RVA pointer in the
dump, no `strcode32`/`strcode`/`gz_hash` of any name anywhere in the image, Ghidra
`getReferencesTo` returns nothing, and **no pointer to any of them exists in the live process**
(scan of all image regions `0x140000000–0x143d14000`). They sit in a compiled-in descriptor table
at **0x14161e000** whose 8-byte entries are obfuscated on disk (e.g. the entry at 0x14161efc0 reads
`aa 41 0f 59 c1 0f c6 d2`) and are rewritten by an initializer at load — i.e. an at-rest encrypted
name table. This is why string-mining the tier names cannot reach the consumer: the consumer walks
the table, it never embeds the strings.

### 4b.4 The condition-display chain (OBS) — a grade, not the stamina value

- `FUN_140e5b200` ctor: `vtable 0x1427bbb08`, resolves `cmnIconCondition` through vtbl+8, and stores
  the icon index as an **int at object+0x98**; the 10 `condition_*` names are reached through the
  pointer table at **0x1434c3370** (`0x1427bb9b0…0x1427bba40`).
- `FUN_140e5b280` (vtbl+0x10) selects `conditionN` by index; `FUN_140e5b2c0` reads it back.
- Callers: `FUN_140e6bb20` maps only `param_2 ∈ {0,2,4}` → `condition_4 / condition_2 / condition_0`;
  its sole caller `FUN_140ddd540` is a UI card updater whose switch also drives
  `FUN_140e6d450` (`switch_condition`) and `FUN_140e6ba50` (`cmnIconRatingUpdate`).
- Downstream producers are UI plumbing only (`FUN_140e8a4a0` → `FUN_140e8ad80` → `FUN_140e9363c` →
  `FUN_140e94230`, all vector/list code).

So the 0..9 display is an *already-graded integer* handed to the UI; the function that grades it
out of the stamina value was **not** located.

### 4b.5 Recommended next step (single actionable item)

Recover the initializer of the descriptor table at 0x14161e000: no rip-relative store to it exists,
so it is filled through a computed base (a `.bss` pointer or a page of relocated pointers dumped by
the loader). Find the writer (a `memset`-like or element-by-element store loop covering
0x14161e000…), then run Ghidra `getReferencesTo` on the *decrypted* entry for
`PLAY_DATA_SHOOT_DASH_STAMINA_20_15` to land directly on the band function. That function's caller
chain is where the stamina field is read, and its own update site is the drain (which must be scaled
by `DAT_14351ad20`).

---

## 5. GF behaviours that differ from PES

Each row: GF's current rule (file:line in the GF tree) → the PES rule that replaces it.

| # | GF today | PES rule (this report) | Action for the engine |
|---|---|---|---|
| 1 | `fatigueFactorInv` decays **only with distance**: `drain = distance*0.00003*(2−staminaStat)*workload*drainMult*(1/matchDurationFactor)`, clamped 0.01..1 (`src/onthepitch/player/player.cpp:541-543`, helpers `gameplaytuning.hpp:668` `GetFatigueWorkloadFactor`, `playerskills.cpp:588` `GetFatigueDrainMultiplier`, `teamphilosophy.cpp:95` Gegenpressing ×1.2) | Drain is driven by the **AI action tiers** of §2.2 (shoot/pass/cross while dashing change with stamina bands 20..0) and by the **fatigue injury** path; the visible effect is a clip-family swap (§3) and there is **no script-visible stamina field** | Replace GF's single continuous workload factor with a sim stamina value on a 0..20 scale; expose the four tier boundaries as the only threshold constants; map clip choice to the `_tired` families |
| 2 | **No rest regeneration** at all: GF's `fatigueFactorInv` only ever decreases | PES's stamina bands are reached *from a value that recovers* — the 5_0 band existing means the value must come back up during a half, otherwise every player ends in the bottom band (INF, high) | Add a regeneration term for walking/idle/standing; the rate is **open** (§7 item 1) |
| 3 | Fatigue scales **all** stats by `0.7+0.3*fatigueFactorInv` (`player.cpp:764`) and injury scales all by `1−0.4*injuryLevel` (`player.cpp:766`) | PES's ability groups are explicit (the three `.rdata` tier maps: awareness+GK+KP+Acc / technical / speed+jump / physique — `pes-re-matchlogic.md` §1.3), and stamina is a *tier index* into per-action tables, not a global multiplier | Scale per ability group, not globally; drop the flat 0.7/0.3 and the 0.4 injury factor until the PES coefficients are found |
| 4 | Injury comes **only from trips**: `p1->Injure(tripType*0.04f)` at `match.cpp:6722` and `:6741`; `injuryLevel` accumulates additively and is clamped 0..1 (`playerbase.hpp:122`) | PES has **two causes**: contact (`PLAY_DATA_INJURY_CONTACT`) and **fatigue** (`PLAY_DATA_INJURY_FATIGUE`, preceded by `_WARN`), plus a per-player **injuryOuch** (pain) value separate from the severity level (§4.1–4.3) | Split GF's single `injuryLevel` into `level` (severity) + `ouch` (pain) and add the fatigue-injury cause with its warning state |
| 5 | Injury has **no part**: `Injure()` knows nothing about which body part | `INJURY_PART_{NOTHING,LEFT_THIGH,LEFT_LEG,LEFT_FOOT,RIGHT_THIGH,RIGHT_LEG,RIGHT_FOOT}` — 7 lower-body parts, L/R symmetric (§4.2) | Add the part to the injury record; the part should gate which `endure`/`rise` clip plays (Rknee/Rside variants exist for a reason) |
| 6 | Injury affects only **stats**; the presentation is GF's own trip anims | PES drives a dedicated demo: `INJURY_LOOP_START → INJURY_LOOP (timer) → INJURY_STANDUP`, plus `BoutofplayReason`, `DemoState`, the `dm_injury_*` endure→rise→comeback chain and the **stretcher / leading-rise** demo (`LeadingRiseOrStretcherDemo`) with four graded absence icons `injury_0..3` | Implement the FSM as literal states and drive it from `injury.o`'s timers once decoded; use the PES clip chain, not GF's `foulsequence` clips |
| 7 | Substitution trigger: injury at `injuryLevel ≥ 0.25` or fatigue after `fatigueSubstitutionTime_ms` with `fatigueFactorInv ≤ tiredFatigueFactorInv`, best-average player on (`aimanager.cpp:54-100`, `aimanager.hpp:39`) | PES types the substitution: `SubstitutionSituation`, `SubstitutionType`, `SubstitutionPlayerTypeIn/Out` (§4.4) | Keep GF's selection logic but tag the substitution with the PES situation/type vocabulary |
| 8 | `FoulSeverity` scores the *foul* (card) only; injury severity is incidental (`tripType*0.04`) | PES scores contact through one pipeline with per-tackle-type severity coefficients (`tackle.o`, `sliding.o`, `contact.o`) and records stagger/fall × tackle/slide separately in the statistics layer (§4.2) | Score injury severity in the same place the foul is scored, using the tackle-type coefficients; the exact coefficients are **open** (§7) |
| 9 | Stamina affects the AI nowhere (GF's AI does not consult `fatigueFactorInv` for action choice) | The AI reads stamina **per action** (shoot/grounder pass/fly pass/through pass/cross, dashing only) in four bands (§2.2), plus `WARNING_FATIGUE` / `FATIGUE_OVER_DAMAGE` / `ACCUMULATE_FATIGUE` concepts (§2.3) | Gate GF's action scoring on the stamina band; add the two fatigue concepts to the AI's situation evaluation |

---

## 6. Open items, and what was tried

1. **The stamina drain / regeneration formula** — OPEN. Still open after the second pass (§4b): the
   **stamina field offset**, the **drain expression**, the **regeneration**, the **0..20 tier
   mapping** and the **Stamina-attribute (Player.bin 0x22, 40..99) input** are all unrecovered. Tried:
   - exhaustive string mining for `stamina/tiredness/fatigue/condition` (only the ability name, the HUD
     gauge, the mail/Lua `m_stamina`/`tiredness`, and the 20 AI tier names exist — §2.1);
   - the SD enum-name table at 0x14344a000 (213 entries, no stamina/fatigue entry) and the SD value-name
     table at 0x14344c1e0 (177 entries, no stamina/fatigue value);
   - the `m_stamina` code reference (0x141ca5d0f, `lea rdx,[rip+0xe56f82]` → 0x142afcc98, followed by a
     call with an output buffer) — it is the **mail/Lua field-name serializer**, not a consumer;
   - searching `.trace` for `imul …,0x128` (the 0x128-byte per-player block stride of
     `pes-re-matchlogic.md` §1.3) — only 3 sites, none a per-tick player loop;
   - searching for byte reads of the player block at +0x26/+0x27/+0x2D/+0x2E/+0x2F (the ability/condition
     byte range) — 79–126 hits each, all untriageable without more time;
   - searching for a `cmp …,0xf` / `cmp …,0xa` / `cmp …,0x5` cluster (the 20/15/10/5 tier boundaries) —
     28 candidates for 15-then-5 within 0x100 bytes, three decompiled (0x140461f83, 0x141e9cf40,
     0x14197f600) and all are mesh/camera/format code;
   - the per-tick functions `FUN_140925750`/`FUN_1408BF090`/`FUN_1408BFCF0` (decompiled): set-piece/phase
     dispatch and a 25-tick bookkeeping call, no stamina write;
   - the condition/familiarity/super-sub `.rdata` tables @0x142632CC0 / @0x142632EB0 / @0x142632F74 —
     these are ability-index lists (ids 20,21,22,34,35,36,37 …), i.e. rating modifiers, not drain rates.
   **Added in the second pass (2026-10-13):**
   - hash lookups — `strcode32`/`strcode`/`gz_hash` of all 20 tier names and of every
     `PLAY_DATA_*` name: **0 hits** in the image (§4b.3);
   - pointer scans — rip-relative `lea`/`mov`, u64/u32/RVA forms, and a full live-memory scan of
     `0x140000000–0x143d14000` for pointers to the tier names: **0 hits**; the names live in the
     obfuscated descriptor table at 0x14161e000 (§4b.3);
   - tier-boundary integer clusters — 168 `cmp …,0x14` sites, 7 with a 15/10 compare within 0x30
     bytes; 4 decompiled (0x140102ad0, 0x1402ebeb0, 0x1402ed340, 0x1405ae940) — set-piece/category
     predicates, not stamina;
   - tier-boundary float clusters — 600 `comiss/ja` sites against 20.0/15.0/10.0/5.0, 26 clusters with
     ≥2 constants, **0 with all four**;
   - scale constants — `imul …,0x14` (40 sites, 4 decompiled: vector/manager code), a `/5`
     (`0xcccccccd`) within 16 bytes of a 20-compare (0 sites), `1/48` and `1/108` (absent);
   - the tick route (§4b.1) — followed the only writer of the step float; it computes `dt`, it does
     not touch stamina;
   - the condition-icon route (§4b.4) — reached the card widget and its 0..9 grade index at
     object+0x98, but only through UI plumbing; the grader was not located;
   - live-process `.data` diff — the step slot `DAT_14351ad20` is the only live-changed float in its
     page, and its live value 1/48 is derived from a match-period field, not from a stamina constant
     (§4b.2).
   Next: recover the initializer of the descriptor table at 0x14161e000, then
   `getReferencesTo` the decrypted `PLAY_DATA_SHOOT_DASH_STAMINA_20_15` entry (§4b.5). Reading the
   player game object (`GetPlayerGameObject` @0x141B59E20) layout and the `InplayDemoFatigueInjury`
   class (TD 0x14361be10) remain the other entry points.
2. **`injury.o` field semantics** — OPEN (§1.3). The loader chain was followed to the file-cache
   (`FUN_141e5bc40 → FUN_141e5bee0 → FUN_141e5d700/FUN_141e5d8c0`, all decompiled OBS), but the
   per-class field reader could not be located: the image contains the class RTTI TypeDescriptors but
   **no Complete Object Locators** (a u64 search for every `Injury`/`InjuryBase`/`GoalKick`/
   `AvoidBase` TD address yields 0 hits — the COL/vtable region is outside the 64 MB dump), and the
   string `"injury.o"` is *not in the exe* (OBS: byte search), so the object is matched through the
   compiled-in class registry rather than a name literal. No runtime copy of the 48 bytes exists in
   `.data`/`.bss` of the dump.
3. **The six `DemoState`/situation encodings** — OPEN. Tried: the `[INJURY]` print pool gives the state
   *names* (4.1) but the encodings are compiled in; the `InplayDemoFatigueInjury` RTTI TD was located
   but not its vtable (same reason as item 2).
4. **Injury probability from tackle severity / collision speed / foul type** — OPEN. The tuning objects
   are decoded (§1.4) but their coefficients are unlabelled; `FoulSeverity` in GF already models the
   *card* side and can be extended once the coefficients are known.
5. **Substitution rules and the stretcher timing** — OPEN; only the vocabulary (§4.4) is closed.
6. **How injury resistance (Player.bin id 0x2E, C/B/A) scales the probability** — OPEN.

Everything else in §1–§4 is OBS with the address/file that makes it readable at runtime.

---

## 7. Reproduction notes (for whoever picks this up)

- WESYS decode: `src/pes/uniform.cpp:ParseUniformTable` (already in the repo) reads both containers;
  my scratch decoder only added the object listing.
- Image helpers in `<scratch>/InjuryFatigueRE/if/`: `wesys.py` (container listing),
  `leascan.py` + `leatargets.pkl` (rip-relative `lea` index over `.trace`, 102,088 distinct targets —
  useful to prove a string is *not* referenced from code), `dis.py` (objdump + `.pdata` function-bounds
  lookup via the RUNTIME_FUNCTION table at 0x143ac5000).
- Headless decompile that worked:
  `/opt/ghidra/support/analyzeHeadless $HOME/ghidra-projects/scratch/main PES21 -process pesmem.bin
   -noanalysis -readOnly -scriptPath <scratch>/ghk/scripts -postScript DecompAt.class <ABS_OUT>.txt 0xADDR`
  (note: `DecompAt.class`, not `.java` — `.java` fails with ClassNotFoundException).
- No PES-derived bytes, name lists or number tables are committed anywhere by this report.
