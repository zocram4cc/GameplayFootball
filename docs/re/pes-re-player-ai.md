# PES 2021 player AI: decisions, plans and keepers — evidence report

Scope: how PES 2021's match AI actually decides. This report covers what
`pes-re-matchlogic.md` §5 only mapped: the ball carrier's think-unit system,
the off-ball run choosers, team plans and pressing, goalkeeper decisions,
where attributes and playing styles enter, the data-driven AI tables in the
VFS, and live AI state.

Tags: **OBS** = observed (method stated), **INF** = inferred (confidence
stated), **EXT** = third-party write-up, not verified here. Killed hypotheses
are recorded. Verbatim PES values appear as evidence (owner decision); code
reads its values from the install, never from this report.

## 0. Sources, method, state

- **Image**: `<scratch>/pesmem.bin`, a linear dump of the live PES2021.exe, base
  `0x140000000`, so `VA = 0x140000000 + file_offset`. Sections: code+inline
  literals `.trace` 0x140001000–0x14252e800, `.rdata` 0x14252f000–0x14339b000,
  `.data` 0x14339b000–0x143ac5000, `.pdata` (158,632 function records) 0x143ac5000–0x143c96000.
- **Tooling**: capstone disassembly of every `.pdata` function plus a verified
  rip-relative xref index (346 k instruction→data edges) and a direct call
  graph (856 k edges); Ghidra headless decompile of selected functions from a
  private copy of the fully analysed project (the shared project is never
  touched, never re-analysed).
- **Live**: PES2021.exe pid <pid>, mid-match throughout, read-only
  `/proc/<pid>/mem`.
- **Data**: `common/match/constant/*.bin` from `Data/dt18_all.cpk`
  (decompressed WESYS containers), parsed with the established
  alphabetical-key rule.

## 1. Architecture recap (OBS, from the previous report)

Five layers, all ticked from the same frame loop (one logic frame = 1/54 game
second, `GetGameSpeed()` @0x141E532D0 → float at 0x14351AE54 = 54.0):

| layer | owner | this report |
|---|---|---|
| team brain | `match::ai::Team`, 47 × `SubConcept*`, `ZoneDefenceManager`, `ai::Judge` | §4 |
| off-ball choice | `ActionSelectorManager` + 15 × `ActionSelector*` | §3 |
| player brain | `PlayerAIBase` → `PlayerOffence` / `PlayerDefence` / `PlayerGKAuto` | §2, §5 |
| ball carrier | `bp::BallPlayer` + 26 × `bp::ThinkUnit*` + 11 × `bp::ImageUnit*` | §2 |
| execution | 112 × `match::player::Action*` driven by 127 AI action ids | §6 |

The per-player AI chain is `… → 0x140929a26 (per-player AI process, ~0x5E6 B,
gated by `*(u8*)(matchInfo+0x11B0) == 0x16`, i.e. 22 players) → 0x14092A340 →
0x14092C9F0 (carrier sense: split `playerIndex % 11` slot, read the per-slot
formation index at `teamData + 0x4C00 + slot*4`, call the registry getters) →
0x1421019D0 → BallPlayer update 0x142101AA7 → **`BallPlayer::MainEval`
0x142110D30**`. OBS: call graph and decompiles of 0x14092C9F0 and 0x1421019D0.

## 2. The ball carrier: BallPlayer and its think units

### 2.1 Action id table (OBS)

The engine indexes every AI action by id 0..126 in a pointer table at
**0x1434A4D50** (names, 127 entries):

| id | name | id | name |
|---|---|---|---|
| 0 | NOTHING | 21 | CLEAR |
| 1 | BASE_POSITION | 22 | SHORT_PASS |
| 2 | FREE_RUN | 23 | POST_PLAY_PASS |
| 3 | DRIBBLE | 24 | THROUGH_PASS |
| 4 | TRAP | 25 | SHOOT |
| 5 | PASS_GET | 26 | KICK_FEINT |
| 6 | MOVE_ROUTE | 27 | FEINT |
| 7 | MOVE_KEEP | 28 | SWITCH_PASS |
| 8 | MOVE_STOP_THINK | 29 | SPACE_RUN_SECOND_LINE |
| 9 | MOVE_STOP_FRONT | 30 | SPACE_RUN_CHANCE |
| 10 | MOVE_STOP_GOAL | 31 | SPACE_RUN_COUNTER |
| 11 | GOAL_NET_DODGE | 32 | LINE_BREAK |
| 12 | POST_DODGE | 33 | DIAGONAL_RUN |
| 13 | GOAL_KEEPER_DODGE | 34 | PASS_SUPPORT |
| 14 | HUMAN_DODGE | 35 | PASS_SUPPORT_THROWIN |
| 15 | JOSTLE | 36 | PASS_AND_GO_RUN |
| 16 | MOVE_ON_PASS | 37 | ONE_TWO_PASS_RUN |
| 17 | WAIT_TIMER | 38 | CENTERING_GET |
| 18 | LONG_PASS | 39 | OVERLAP_SB |
| 19 | CENTERING | 40 | OVERLAP_CB |
| 20 | EARLY_CROSS | 41 | BALL_DODGE |

(continues: 42 GOAL_GET, 43 POST_PLAY, 44 TEAMMATE_MOVING, 45 SHORT_CORNER_MOVE,
46 GK_COACHING_RUN, 47 PULL_AWAY, 48 SLIDING, 49 TACKLE, 50 COVER, 51 DELAY,
52 PRESS, 53 SAND, 54 MARK, 55 DELAY_MARK, 56 BLOCK, 57 CONTACT, 58 MATCH_UP,
59 OFFSIDETRAP, 60 GOAL_COVER, 61 PASS_COURSE_CUT, then the 25 `KP_*`
keeper actions 62..86, 87 REAL_DATA, 88 STAGGER, 89 FALL_DOWN, 90 DIVE,
91 BALL_LOST, 92 OVER_TAKEN, 93..96 FREEKICK_*, 97 CORNERKICK, 98 GOALKICK,
99..102 THROWIN_*, 103 KICKOFF, 104 PENALTY_KICK, 105..107 WALL_*,
108 QUICK_RESTART, 109 POST_MAN, 110/111 SEAMLESS_THROWIN*, 112..120 DEMO_*,
121 FREE_TRAINING_BREAK, 122..123 PK_*, 124 REAL_DATA, 125 TRAINING_MOVE_ROUTE,
126 PUSH; a 127th tail entry PRESS and a 128th PULL sit after the table.)

A second table at **0x1426484F0** holds one u32 per id; it is *not* the action
property table the previous report assumed: 0x140A6DD30 indexes it with the
player's **registered position** (`*(u8*)(playerBlock+1)`, 0..12) and tests
`& 0x2C0`, i.e. it is a **per-position property table** (bits 6, 7, 9 set only
for SS/CF) used by an offside/shoot-position test that returns one of three
constants (0x14259D648, 0x1425A8A28, 0x1425CB284). OBS.

### 2.2 The think-unit table (OBS)

BallPlayer embeds the carrier brain. Struct offsets established from
`MainEval` 0x142110D30, its callees and the accessors:

| offset | content | evidence |
|---|---|---|
| +0x0C | player index (u32) | MainEval passes `param_1+0x1C` to the roster getters |
| +0x10 | active flag | MainEval `if (*(char*)+0x10)` |
| +0x14 | `bp::Intention` | 0x142117370/0x142117350 |
| +0x18 | flags word | MainEval `& 0x10` |
| +0x1C | player index (dup) | getters |
| +0x60, +0x78, +0x7C, +0x80 | context handles | MainEval |
| +0xAB8 | sub-context (overlap/pull-away list?) | 0x140699820 in adopt |
| +0xBC, +0xE4, +0xE7 | flags/thresholds | MainEval/adopt |
| +0xC58 | 11-entry candidate list | 0x142118C90 / 0x142118D40 in adopt |
| +0xEC | image unit A | MainEval |
| +0x33A0..+0x3DD0+ | **25 embedded think-unit objects** | 0x1421105B0 |
| +0x3E20 | **think-unit table** (see below) | 0x14211EF20/EF40/EF80 |
| +0x41E4 | adopted answer id (0x19 = none) | MainEval, 0x142119D20 |
| +0x41E8 | adopted answer payload (0x1C B) | 0x142119D20 clears {0, 0x19, 0…} |
| +0x4160 | 25-byte "was evaluated" mask | 0x142110370 writes 25 × 0x01 |
| +0x425A | set to 1 when MainEval returns true | tick entry 0x142101AA7 |

**Think-unit table at +0x3E20** (accessors 0x14211EF20/EF40/EF80):

| +0x00 | u32 count (≤ 25) |
| +0x04 | u32 kindId[25] — the active list, in insertion order |
| +0x68 | int8 order[25] — 0xFF = not in list, else the insertion index |
| +0x84 + kind*0x1C | 28-byte result record per kind: {u32 id, u32 …} written by 0x14211F050 |

- `0x14211EE00(table, kind)` = append: only if `order[kind] < 0`, then
  `kindId[count] = kind; count++; order[kind] = count-1` (OBS).
- `0x14211EE90(table)` = clear: count = 0, order = 25 × 0xFF (OBS).
- `0x14211EF80(table, a, b)` = `order[a] < order[b]` (with −1 mapped to 0x19),
  i.e. "a was activated before b" — the units' relative priority (OBS).

**Think-unit objects** (embedded, one per kind, 28-byte result records in the
table are the *outputs*; the objects are 0x40..0x2D8 bytes). The kind →
object map is a switch, `0x1421105B0` (OBS):

| kind | object offset | kind | object offset |
|---|---|---|---|
| 0 | +0x3678 | 13 | +0x39E0 |
| 1 | +0x36C0 | 14 | +0x3A20 |
| 2 | +0x3700 | 15 | +0x3A60 |
| 3 | +0x3740 | 16 | +0x33A0 (large, 0x2D8) |
| 4 | +0x3780 | 17 | +0x3AC8 (large, 0x238) |
| 5 | +0x37C8 | 18 | +0x3B30 |
| 6 | +0x3808 | 19 | +0x3C70 |
| 7 | +0x3848 | 20 | +0x3CB8 |
| 8 | +0x3888 | 21 | +0x3D40 |
| 9 | +0x38C8 | 22 | +0x3D88 |
| 10 | +0x3918 | 23 | +0x3DD0 |
| 11 | +0x3958 | 24 | +0x3E18 |
| 12 | +0x3998 | | |

*(kind 24 offset inferred from the 0x48-byte stride of kinds 19..23; verify.)*

Each think unit is polymorphic: `0x14211EEC0(unit, ctx)` calls
`vtable[2]` (offset 0x10) as `evaluate(unit, out, ctx)` when the unit's state
at `+0x38` is 1, then copies `*(ctx+0x20)+0x40..0x48` and `+4` into the unit
and sets state 2; `0x1408AEF50(unit)` reads `+0x38` (OBS). Result structs are
0x1C bytes: 4 × u32, u64, u32.

### 2.3 Which units are active, and in what order (OBS)

`0x1421112C0` (the main builder, called from `0x1421111B0`) appends kinds to the
table in this order:

1. always: **0x17, 0x16, 0x00, 0x01, 0x11, 0x0E, 0x0B, 0x0C**
2. then a switch on `*(u32*)(ctx7 + 0xA0)` (the situation/mode id, 1..9, else
   "normal play"):
   - 1, 2, 3 (restart-ish): unit 2 unless `*(u8*)(ctx7+0xD9)`, then unit 3 in
     some sub-cases, and unit 7 if `*(ctx5+0xB44) == 3`; falls through to
     "case 4"
   - 4: units 3, 2, then 4
   - 5: unit 3 if `*(float*)(ctx4+0x40) * *(float*)(ctx+0x14) < *(cfg+0x18) − 0.2F`
     (0x14258C264), then unit 7
   - 6: unit 9; 7: unit 5; 8: nothing extra; 9: unit 0x0D
   - default (normal): units 2, 3, 0x10 (and nothing more if the image-unit
     counter at `+0x32B0` returns 4), plus 4
3. finally, conditional: unit 9 if `*(int*)(ctx3+4) == 3 && *(char*)(ctx4+0x6C)`
   and not already present; units 7 and 6 if `*(u8*)(ctx7+0xD8)` or
   `*(int*)(ctx7+0xC4) == 3`.

Free-kick/corner style variants are in `0x1421111B0`: if the player's
formation-slot index is 0 the list becomes 0xE, 0x14, 0x15, 0x12; if
`*(BallPlayer+0x78) == 1` it is 0xE, 0x18; otherwise `0x1421112C0` runs and
then 0xE (and one of 0x17/0x16/0x00/0x01/0x11/0x0B/0x0C) is skipped when the
gate id is 0/22 or `0x15 < teamId == playerId`. OBS.

`0x142110370` then runs one frame of the list: for each active kind it calls
`0x142111610(BallPlayer, ctx, kind, unit)` (a per-kind *situation gate*: kinds
0x11, 3 and 6 additionally require the team flag id 0x1E / 0x1C / 0x1D set in
the team bitmask built by 0x140A40BD0(teamData, out), and match mode
`*(matchInfo+0x116C)` must be 1, except a mode-2 carve-out) and then evaluates
the unit, copying its 0x1C-byte result into the table with `0x14211F050`
unless the intention bits 3/4 disable it. OBS.

### 2.4 Adoption (OBS, part)

`MainEval` 0x142110D30, in order:
1. refresh the `Intention` at +0x14 (0x142117370);
2. evaluate the four image units (0x142119390 at +0xEC, 0x1421197B0 at
   +0x32B0, 0x14211D710 at +0x32C0, 0x14211ED10 → local 72-byte struct);
3. if not the in-play mode, force the answer to action **0x10 MOVE_ON_PASS**
   (or **0x13 CENTERING** … see §2.5) — actually the code sets action id
   `0x10`/`0x13` via 0x1421102D0 and copies the payload;
4. otherwise adopt: `0x14210F110(BallPlayer, ctx)` walks the active list in
   insertion order and returns the first kind whose gates pass, subject to the
   table's own de-duplication (`0x14211EF80`);
5. the deferred branch: if `gameSpeed * 0.5 + DAT_142588200` (a float at
   0x142588200 ≈ 0.5) is less than `*(matchInfo+0x1198) − *(BallPlayer+0x4224)`
   (a frame counter delta) **and** intention bit 3 or 4 is set, the answer is
   re-adopted through 0x1421111B0 → 0x142110370.

`MainEval` returns bool; the tick entry 0x142101AA7 sets
`*(BallPlayer+0x425A) = 1` on true (OBS) — this is the "carrier decided this
frame" flag.

*[§2.5..2.x: per-unit scoring, inputs and formulas — contributed by the
ball-carrier analysis, see the section file.]*

## 3. Off-ball run choosers

*[pending]*

## 4. Team plans, defensive positioning and pressing

*[pending]*

## 5. Goalkeeper AI

*[pending]*

## 6. Attributes, styles and skills in decisions

*[pending]*

## 7. Data-driven AI tables in the VFS

*[pending]*

## 8. Live AI state

*[pending]*

## 9. Status

| question | status |
|---|---|
| action id table | **closed** (127 ids, plus the per-position table at 0x1426484F0) |
| think-unit table/objects | **closed** (layout, activation order, adoption order) |
| per-unit identity and formulas | open |
| off-ball selectors | open |
| team plans/pressing | open |
| goalkeeper | open |
| attribute formulas | open |
| VFS AI tables | open |
| live state | open |
