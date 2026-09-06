# PES 2021 tactics internals, and what GameplayFootball should take from them

Two sources, and they turned out to complement each other almost perfectly:

- **`User_Aoba_PES2021 Engine Research - Rigged Wiki.pdf`** (90 pages) — the
  implyingrigged community's reverse-engineering of the PES 2021 PC executable:
  IDA Pro decompilation plus live Cheat Engine reads, cross-verified against a
  clean PES 2020 binary where PES 2021's Denuvo obfuscation hid constants. This
  gives the *logic*.
- **The local PES 2021 install's own data files**, opened for this document with
  a new reader, `tools/pes21_import/wesys_constant.py`. This gives the
  *parameters* — and, in the PES 2015/16-era archives that ship alongside,
  Konami's own **named, commented, human-readable** versions of several systems
  the PDF could only describe from assembly. See §8.

Sections 1–7 are engine behaviour: hardcoded logic and `.rdata` tables in the
executable. Section 8 is the data. Sections 9–12 are the comparison with
GameplayFootball and what to do about it.

Read all of it as a design reference — a football engine that shipped for twenty
years, and the mechanics it converged on — not as something to copy byte for
byte. Several of the systems below are shipped bugs, and they are flagged as
such.

## What the exe research does and does not cover

It **does** cover, with traced formulas: the 21-bit playing-style system and its
position-compatibility gate; the 41-bit player-skill system and the three skills
whose effects are fully traced; the five-array live stat modifier pipeline
(condition arrows, position familiarity, super-sub, stamina drain) with every
multiplier; the hidden fatigue performance-tier system; the physical duel score
cascade; the 126-entry AI action model with its property/transition tables; the
difficulty→AI-capability tier system; and the complete referee foul-scoring and
card pipeline (that last one drives `RULESET_AUDIT.md`).

It does **not** cover team-level tactics data: attack/defence *levels*, fluid
formation, or the advanced-instruction presets (gegenpress, tiki-taka, long ball
counter, hug the touchline). Those are not exe logic — they are parameters, and
the research defers them to a companion page we do not have a copy of.

**§8 recovers much of that from the install itself**, including the
attack/defence level mechanism in full and Konami's own difficulty table with
named fields. What remains genuinely unknown is listed in §12.

Practical consequence for us: GF's `TeamInstructions` already models the
*instruction layer* the way PES exposes it to the player (see the map below), so
the interesting borrowings are the layers above and below it — how a coach
changes his approach during a match, how a style is gated by position, how stats
are modified live, how difficulty is expressed, and how duels resolve. That is
where GF is thinnest and PES is deepest.

---

## 1. Playing styles

### The bitmask and the position gate

Each player carries a 21-bit playing-style mask at player data `+0x48`. A style
is active only when **both** conditions hold:

```
bit      = 1 << styleBitNumber
isActive = (bit & playerStyleBitmask) != 0      // the player has the style
        && (bit & positionMask[posIndex]) != 0  // his formation slot allows it
```

No code path bypasses the position half of that test. The position index used is
the **formation slot**, not the player's natural position — a natural CMF played
at DMF is checked against the DMF mask.

Style bits, grouped by position zone (the engine's own ordering, not the Edit
Mode menu order):

| Bit | Internal name | Display name | Zone |
|---|---|---|---|
| 0 | `GOAL_POACHER` | Goal Poacher | CF/SS |
| 1 | `DUMMY_RUNNER` | Dummy Runner | CF/SS |
| 2 | `FOX_IN_THE_BOX` | Fox in the Box | CF |
| 3 | `POST_PLAYER` | Target Man | CF |
| 4 | `CHANCE_MAKER` | Creative Playmaker | Wing/Mid |
| 5 | `WING_STRIKER` | Prolific Winger | Wing |
| 6 | `INSIDE_PLAYER` | Roaming Flank | Wing |
| 7 | `CROSSER` | Cross Specialist | Wing |
| 8 | `CLASSIC_NO_10` | Classic No. 10 | Midfield |
| 9 | `FREE_ROAMING` | Hole Player | Midfield |
| 10 | `BOX_TO_BOX` | Box-to-Box | Midfield |
| 11 | `ANCHOR_MAN` | Anchor Man | DMF |
| 12 | `ENFORCER` | The Destroyer | DMF/CB |
| 13 | `PLAY_MAKER` | Orchestrator | DMF/CMF |
| 14 | `BUILD_UP` | Build Up | CB |
| 15 | `OVERLAP` | Extra Frontman | CB |
| 16 | `OFFENSIVE_SB` | Offensive Full-back | LB/RB |
| 17 | `DEFENSIVE_SB` | Defensive Full-back | LB/RB |
| 18 | `INNERLAP` | Full-back Finisher | LB/RB |
| 19 | `OFFENSIVE_GK` | Offensive Goalkeeper | GK |
| 20 | `CLASSICAL_GK` | Defensive Goalkeeper | GK |

The compatibility table (10 merged formation positions — left/right variants
share an index, because style compatibility depends on role, not side):

| Index | Position | Mask | Compatible styles |
|---|---|---|---|
| 0 | GK | `0x180000` | Offensive GK, Defensive GK |
| 1 | CB | `0x0D000` | Destroyer, Build Up, Extra Frontman |
| 2 | LB/RB | `0x70000` | Offensive FB, Defensive FB, FB Finisher |
| 3 | DMF | `0x3C00` | B2B, Anchor Man, Destroyer, Orchestrator |
| 4 | CMF | `0x3700` | Classic No.10, Hole Player, B2B, Destroyer, Orchestrator |
| 5 | LMF/RMF | `0x6D0` | Creative PM, Roaming Flank, Cross Specialist, Hole Player, B2B |
| 6 | AMF | `0x712` | Dummy Runner, Creative PM, Classic No.10, Hole Player, B2B |
| 7 | SS | `0x113` | Goal Poacher, Dummy Runner, Creative PM, Classic No.10 |
| 8 | LWF/RWF | `0xF0` | Creative PM, Prolific Winger, Roaming Flank, Cross Specialist |
| 9 | CF | `0x0F` | Goal Poacher, Dummy Runner, Fox in the Box, Target Man |

(A known shipped bug: index 7 / SS omits bit 9, so Hole Player at SS gets
nothing, contradicting Konami's own documentation.)

### What the styles actually do

Two dispatch mechanisms exist. A `switch`-based dispatch (`CheckPlayingStyle`)
covers only five styles and half its cases have zero callers; every style is
really implemented through 216 direct calls to `CheckStyleAtPosition`, mostly
inside one master positioning function that checks 16 styles across 37 calls.
The effects are almost entirely **positioning boundary offsets in metres** plus
**run-priority multipliers**:

| Style | Axis | Effect |
|---|---|---|
| Hole Player (9) | X | +8 m forward boundary; ×1.3 run priority; +14.0 run distance; bypasses a gate that blocks other players' runs; ×0.7 *de*prioritized for box runs; permanent enhanced (`_good`) run parameters |
| Goal Poacher (0) | X, Z | X pushed to the defensive line ("off the shoulder of the last defender"); Z narrowed central; lower chance-creation threshold (85 vs 100); drives the pull-away run system |
| Fox in the Box (2) | Z | Z constrained to ±5 m of box width; ×1.3 selection weight for the primary box run |
| Target Man (3) | X | Moves toward the ball carrier |
| Creative Playmaker (4) | X | Adjusts when near the Goal Poacher |
| Prolific Winger (5) | Z | ±5 m toward/away from the touchline depending on ball side; enhanced run params under strict spatial conditions |
| Roaming Flank (6) | Z | ±15 m — largest lateral shift of any style; cuts inside when the ball is on the far side |
| Cross Specialist (7) | Z | Z boundary shrinks 1 m toward the touchline (stays wide) |
| Classic No. 10 (8) | X | +1 m back limit (cannot drop deep) and +8 m forward limit |
| Box-to-Box (10) | X | Dynamic ±10–15 m relative to the ball; +50% marking coverage range |
| Anchor Man (11) | X | +8 m forward limit |
| Orchestrator (13) | X | −6 m (sits deeper to start play) |
| Build Up (14) | X | +6 m forward from CB, suppressed when the angle to goal < 35° |
| Offensive Full-back (16) | X | +3 m forward |
| Defensive Full-back (17) | X | −3 m deeper |
| Full-back Finisher (18) | Z | ±15 m, same magnitude as Roaming Flank |
| Dummy Runner (1) | — | +87% diagonal-run trigger range, +100% angle, +60% run length |

The lesson: a "playing style" in PES is *not* a stat bonus. It is a shift in the
player's positional envelope plus a change in how eagerly he is picked for a
particular run. That is cheap and it reads clearly on screen.

### The `_good` parameter trick

Run parameters come in normal and `_good` (enhanced: wider scanning, more areas
checked, faster support, lower trigger thresholds) variants, selected by:

```
if (gameState == 2                          // counter-attack / advantage state: EVERYONE
 || CheckPlayingStyle(player, dispatch30)   // Goal Poacher at CF/SS, Hole Player behind the line
 || ProlificWinger_GoodCheck(player))       // Prolific Winger, strict spatial test
    use _GOOD values
```

So the same enhanced-behaviour switch is reachable three ways: by game state
(everybody gets it on the counter), by style-at-position, and by a spatial test.
One mechanism, three grades of access.

---

## 2. Player skills, and the three with traced mechanics

41 skill bits at player data `+0x40` (bits 32–40 in a second DWORD at `+0x44`,
added post-launch), ordered by functional category: dribble (0–5), shooting
(6–10), quick play (11–16), specialist (17–20), defensive (21–24), mental
(25–27), technical (28–31), post-launch (32–40). Notable: `MALICIA`
(Gamesmanship), `CHASING` (Track Back), `MAN_MARK`, `INTERCEPT`,
`THROUGH_PASS`, `PK_STOPPER`.

Most checks are direct AND masks, not the dedicated `HasSkill` helper (which has
three callers in the whole binary). Only three skills are fully traced, all in
the *mental* category, and all three modify fatigue:

- **Captaincy** (bit 25): checked on the team captain, applies **team-wide**,
  raises the fatigue threshold 60 → 70%.
- **Fighting Spirit** (bit 27): individual, threshold **+15 percentage points**.
- **Super-sub** (bit 26): second-half substitute stat boost, see §3.

They stack: no skills 60%, Captaincy 70%, Fighting Spirit 75%, both 85%.

---

## 3. The live stat modifier pipeline

Every frame, for every player (including bench), PES recomputes live stats as a
product of five 60-entry float arrays:

```
live_stat = base_stat × Array1 × Array2 × Array3 × Array4 × Array5
```

then clamps: **all 25 performance stats to 40–99**. A player already at 99 gets
nothing from any positive modifier — a real balance property, not an accident.

| Array | Purpose | Status |
|---|---|---|
| 1 | unknown | never observed ≠ 1.0 |
| 2 | condition arrows (form) | fully mapped |
| 3 | position familiarity | fully mapped |
| 4 | super-sub | fully mapped |
| 5 | live stamina drain | continuous, partly mapped |

### Array 2 — condition arrows (per-match form)

Three stat tiers, five arrow states. Assigned at match start.

| Arrow | Tier 1 (awareness, GK, kicking power) | Tier 2 (technical + speed/jump) | Tier 3 (physique, stamina) |
|---|---|---|---|
| Terrible (purple) | 0.84 | 0.88 | 0.88 |
| Poor (blue) | 0.92 | 0.94 | 0.94 |
| Normal (green) | 1.00 | 1.00 | 1.00 |
| Great (orange) | 1.06 | 1.05 | 1.03 |
| Top (red) | 1.12 | 1.09 | 1.06 |

Note the asymmetry: bad form hits Tier 2 and Tier 3 identically, good form
differentiates all three, and awareness benefits most. Form is a *mental*
modifier first and a physical one last.

### Array 3 — position familiarity

Read from a 13-byte-per-player table: byte 0 is the registered position, bytes
1–13 are the familiarity rating at each of 13 formation positions (**left and
right treated separately** here, unlike the style compatibility table). Ratings
map straight through from the database: A = 2, B = 1, C = 0.

| Rating | Tier 0 (technical) | Tier 1 (awareness/athletic) | Tier 2 (physique) |
|---|---|---|---|
| C (unfamiliar) | 0.80 | 0.82 | 0.84 |
| B (partial) | 0.92 | 0.94 | 0.96 |
| A (natural) | 1.00 | 1.00 | 1.00 |

Out of position costs technique (−20%) far more than physique (−16%). A striker
at centre-back keeps his body and loses his football brain.

Critically: this is driven by the **formation assignment**, not by where the
player physically stands. A CF who drops back to defend a corner takes no
penalty; a CF *reassigned* to CB takes the full penalty even while he is
standing in the opponent's box.

### Array 4 — super-sub

Activates when the player has the skill **and** a match-period field is 1–4
(second half or extra time), on the single frame the flag flips. 23 stats in
three tiers: awareness/athletic ×1.064, technical ×1.048, physique ×1.030.
Tight Possession and Aggression (added in PES 2020) are missing from the table —
a shipped oversight. With the 99 clamp, the real-world effect is +0 to +5 points,
worth most to players in the 75–94 band.

### Array 5 — live stamina drain

Continuous, fluctuating, recovers partially during walking. Physical stats go
first (Physical Contact, Balance, Acceleration observed degrading by −7% around
the 25th minute; technical and awareness stats had not started).

### And separately: the hidden fatigue performance tier

A *second*, threshold-based fatigue system, invisible to the player, that
degrades the quality of each individual action decision:

```
depletion = (max_stamina - remaining) / max_stamina * 100

threshold = 60.0
if (captainHasCaptaincy)  threshold = 70.0        // team-wide
if (playerHasFightingSpirit) threshold += 15.0    // individual
threshold = min(threshold, 100.0)

if (depletion >= threshold) tier -= 1             // 3 -> 2
if (severelyFatigued)       tier -= 1             // 2 -> 1, floor is 1
```

Tier → quality float: 3 = 16.0 (full), 2 = 11.0, 1 = 5.0 (≈30% of normal). The
float controls positioning range and action accuracy for that evaluation. Certain
roles, the free-kick taker, players on the substitution list and the captain are
exempt under conditions.

**Do not copy** the companion "dynamic team morale" loop in the same function: it
iterates teammates, calls the getter, *discards the result*, and re-reads the
current player's own struct at an offset that holds float data — so it never
fires. The intent was worth having (a teammate making a key play temporarily
lifts the whole team's stamina resilience, creating momentum swings); the
implementation is dead. Nine independent verifications in the source.

---

## 4. Difficulty is not a stat multiplier — it is an AI capability ladder

This is the single most important thing in the document for GF.

PES does **not** nerf CPU player stats by difficulty. It sets one integer tier
(0–6, one per difficulty level, `Beginner`…`Legend`) and that tier gates
*which AI subsystems are allowed to run at all*, plus how long the AI deliberates
before committing to a kick, plus how much slop is added to pass zones and run
trajectories.

Human-controlled teams are forced to tier 5 regardless of the difficulty
setting, so a human's own AI teammates always operate at Superstar capability and
only the opponent scales.

| Tier | Difficulty | Kick hold (off-ball / on-ball, frames) | Defensive shape | AI features unlocked | Pressing | Pass/run precision |
|---|---|---|---|---|---|---|
| 0 | Beginner | 60 / 40 | line forced +10 m forward, +40% wider | none | none | pass zone +6.7 m, run +0.50 |
| 1 | Amateur | 40 / 30 | +7.5 m, +25% wider | none | none | +5.6 m, +0.42 |
| 2 | Regular | 30 / 20 | +5.0 m, normal width | offside trap; per-player role overrides | none | +4.5 m, +0.33 |
| 3 | Professional | 20 / 10 | +2.5 m, line compression on | scoreline-based pressing trigger | conditional | +3.35 m, +0.25 |
| 4 | Top Player | 10 / 5 | normal | duel engagement timer halved (presses harder) | conditional | +2.2 m, +0.17 |
| 5 | Superstar | 0 / 0 | normal | smart pass-target selection; tactical evaluator; scoreline response; pressing coordinator | 6.0 m base | +1.12 m, +0.08 |
| 6 | Legend | 0 / 0 | normal | all of the above, plus forced pressing evaluation and a validation bypass | 12.0 m base, +10 m angle bonus | exact (0.0 m) |

The individual gates, so they can be lifted one at a time:

| Threshold | System disabled below it |
|---|---|
| tier > 1 | defensive line push / offside trap decision |
| tier ≥ 2 | player tactical role override |
| tier > 2 | defensive line compression (below this, defenders hold individual positions → loose, exploitable shape) |
| tier ≥ 3 | scoreline-based pressing trigger |
| tier ≥ 4 | ×0.5 duel engagement timer ("higher difficulty AI wants the ball more") |
| tier ≥ 5 | smart pass targeting, tactical instruction evaluation, scoreline response, the entire pressing coordinator |
| tier == 6 | forced pressing evaluation, live-stat validation bypass, +10 m approach-angle pressing range, 12 m vs 6 m base pressing distance, aggressive role upgrade |

Two continuous scales use `(6 - tier) / 6` — the only pattern that separates
every tier including Superstar from Legend: pass answer-point zone width and
off-ball run trajectory looseness.

Two shipped bugs worth knowing about, both cautionary: the live tier
reassignment path maps difficulty through a `switch` **missing case 6**, so
Legend falls through to the default and returns 2 (Regular) on every tactical
change — a Legend COM-vs-COM match oscillates between tier 6 and tier 2 all
game. And with tiers 5–6 producing a 0-frame hold, kick actions chain straight
back into the "stopped, decide what to kick" state, giving the "box-to-box
ping-pong" the community complains about. Community testing found that setting
the tier-5/6 duration entries to 50–100 frames restored midfield play. **A
zero-latency AI is a worse AI**, not a harder one.

---

## 5. Physical duels

PES 2021 still runs the PES **2015** duel pipeline (RTTI confirms
`match::anime::action::Jostle`); the native PES21 replacement exists but is
incomplete, so it was left switched off. The legacy path:

1. flag distributor — copies the enable flag to per-player data each frame
2. per-frame evaluator — all 11 players per team, ~660 checks/second at 60 fps
3. pre-engagement filter — "should I engage?"
4. engagement level decision — "how aggressively?" (level 1–4)
5. action state dispatcher — assigns `DELAY` / `PRESS` / `MATCH_UP` / `WAIT_TIMER`
6. duel score calculator — "who wins?"

Filter rules worth stealing outright:

- **at most 3 players may engage one target** (offset check `≥ 4`) — this is what
  stops the whole defence collapsing onto the ball;
- always engage an opponent in `DRIBBLE`, `MOVE_STOP_FRONT` or `MOVE_STOP_GOAL`
  (he is vulnerable), never engage a keeper in `KP_PRESS` or `KP_SAVING_MOVE`;
- engagement level is capped by distance: <0.8 m → 1, 0.8–1.2 → 2, 1.2–1.5 → 3,
  ≥1.5 m → 4. Committing hard requires room to commit;
- an engagement stat drives aggression *inversely*: ≤60 → maximum engagement,
  60–95 → `(95 − stat)/35`, ≥95 → minimum. Better players are more controlled.

The score is a base constant scaled to 60%, plus a stat penalty
`(99 − clamp(stat, 50, 99)) / (49 / randomModifier)` using separate attacker
(`+20`) and defender (`+21`) stat bytes, times a cascade of situational
multipliers that stack **multiplicatively**:

| Condition | ×    |
|---|---|
| defending rather than attacking | 0.5 |
| duel state `DELAY` while attacking / defending | 0.4 / 0.5 |
| various action categories | 0.3 |
| marking conditions (4 variants, each) | 0.2 |
| player form ≥ 4 | 0.5 |
| **attacker near the opponent's penalty area** | **0.2** |
| **defender near his own penalty area** | **0.8** |

with a floor guaranteeing no score below 1.0. Worst-case stacking reaches
0.0072, effectively zero.

That penalty-area pair is a deliberate, hardcoded, stat-independent thumb on the
scale: a 99-Physical-Contact striker still takes ×0.2 inside the box while the
centre-back takes only ×0.8. It manufactures "last-ditch defending" without
touching anyone's attributes.

---

## 6. The action model

PES's AI speaks in a flat vocabulary of **126 named actions** (`NOTHING`,
`FREE_RUN`, `DRIBBLE`, `TRAP`, `PASS_GET`, … `SHOOT`, `THROUGH_PASS`,
`SPACE_RUN_COUNTER`, `LINE_BREAK`, `DIAGONAL_RUN`, `OVERLAP_SB`, `PULL_AWAY`,
`PRESS`, `SAND`, `MARK`, `OFFSIDETRAP`, `PASS_COURSE_CUT`, 26 keeper actions,
19 set-piece actions, …), with two 94-entry side tables:

- an **action properties bitmask** table (30 bits in use) that classifies each
  action, e.g. bit 16 "stopped evaluating" and bit 17 "in-action modifiable"
  together form the *kick gate*: 30 actions route to the kick decider, the other
  64 route to the full AI evaluation;
- an **action transition** table giving each action its default successor when it
  completes or is cancelled. `FREE_RUN` is the fallback for 40 actions — the
  ground state of the AI. Kick actions all fall back to a `MOVE_STOP_*` state,
  which has the kick gate bit set, which re-triggers the kick decider.

A named, tabulated action vocabulary with explicit properties and explicit
fallbacks is an unglamorous but very strong architectural idea: it makes AI
behaviour enumerable, greppable and debuggable, and it is how the difficulty
tiers, the duel states and the playing-style runs all manage to talk about the
same things.

## 7. Pitch geometry

One shared 8-float struct, FIFA-standard, consulted by AI, duels, and the
referee alike:

```
halfLength 52.56  halfWidth 34.06  goalHalfWidth 3.66  goalHeight 2.44
goalAreaY  47.06  goalAreaHalfWidth 9.16
penaltyAreaY 36.14  penaltyAreaHalfWidth 20.12
```

Set-piece exclusion distances keyed off play kind: kick-off 10.15 m, throw-in
6.0 m, goal kick / corner / free kick 9.15 m, penalty 10.15 m.

---

## 8. The data side: what the install itself says

The exe research describes logic; the numbers live in data. Opening those files
turned out to answer several questions the research left open — including the
attack/defence level question this document was originally asked about.

### Where the parameters actually are

There is no `dt18` folder of loose `.o` files, which is how the community
usually describes it. The `.o` objects are **named sections inside nine
`constant_*.bin` archives** in `Data/dt18_all.cpk` under
`common/match/constant/`. The container format, decoded for this work and
implemented in `tools/pes21_import/wesys_constant.py`:

```
16-byte wrapper: [3 tag bytes]["WESYS"][u32 zlib size][u32 raw size] -> zlib deflate
inflated:  u32 count | u32 table_off (=8)
           count * { u32 data_off, u32 size, u32 name_off }
           NUL-terminated names | payloads (mixed LE f32/i32) | trailer "v4.0"
```

The three tag bytes are a useful tell: `ff 10 51` is a stock Konami file,
`00 10 01` is a repacked mod.

79 objects across the nine archives, grouped much as you would hope:

- `constant_team.bin` (16) — `basePosition`, `spaceRun`, `lineBreak`,
  `diagonalRun`, `pullAway`, `overlap`, `centeringGet`, `combination`,
  `support`, `defence`, `defenceCover`, `defenceMark`, `selectorVision`,
  `pairAnime`, `subConcept`, `teamId`
- `constant_player.bin` (36) — `ballplayer` (75 KB, the largest object in the
  game), `ballplayerGk`, `ballplayerSetplay`, `dribble`, `shoot`, `trap`,
  `feint`, `press`, `matchup`, `tackle`, `sliding`, `block`, `contact`,
  `grounderpass`, `flypass`, `throughpass`, `centering`, `passget`,
  `moveOnPass`, `freemove`, `avoid`, `reaction`, `motivation`, `playStyle`, `gk`,
  `freekick`, `penaltykick`, `goalKick`, …
- `constant_match.bin` (27) — `ball`, `pesSmart`, `cpuLevel`, `cursor`,
  `injury`, `rating`, `teamEmotion`, `throwin`, `modeMatchup`, `pes15Test`,
  `setplayGuide*`, `userPlayTendencyTest`

`Data/dt10_x64.cpk` additionally holds `pesdb/Tactics.bin`,
`TacticsFormation.bin` and `MyclubTactics*.bin` — the per-team tactics records.
`dt16_all.cpk` is 2158 menu-flow JSON files and contains no gameplay AI at all.

### The Rosetta stone: PES 2015/16-era archives in the same install

`gpe/dt18_win.cpk`, `gpe/stock16.cpk` and `gpe/gpe2/pes18.cpk` hold the *same
systems* from a few years earlier, before Konami compiled them — 415 files of
**human-readable JSON and XML with Japanese comments and real field names**,
under `common/match/ai/`: `spaceRun.json`, `defenceMark.json`, `defence.json`,
`defenceCover.json`, `combination.json`, `lineBreak.json`, `passSupport.json`,
`basePosition.json`, `cpuLevel.json`, plus behaviour-tree XML and 166
`positionKickOff_<formation>_{offence,defence}.fox` set-piece position tables.

Caveat before trusting it too far: the vocabulary transfers, the *layout* does
not. PES 2021's objects grew — `spaceRun` went from 46 named keys to 60 dwords,
`defenceMark` 45 → 80, `basePosition` 105 → 216, `throughpass` 42 → 120. Only
`goalKick` matches one-to-one, and probably by coincidence. Labelling PES 2021
offsets means matching value *patterns* per object, not indices.

### Attack/defence level, recovered in full

`common/match/ai/team/CoachAttackLevelChange.xml` is a behaviour tree that
outputs one of three coach attack levels — `DEFENSIVE`, `BALANCE`, `OFFENSIVE` —
with Japanese comments on every node. Decoded (connectors are ordered
[negative, positive]):

```
if (not in play)                    -> BALANCE
else if (second half, past 30')     -> diff >= +1 ? DEFENSIVE
                                     : diff <= -1 ? OFFENSIVE : BALANCE
else if (second half)               -> diff >= +2 ? DEFENSIVE
                                     : diff <= -2 ? OFFENSIVE : BALANCE
else (first half)                   -> BALANCE
```

Two things here are better than what GF does today, and both are nearly free:

1. The comparison is `CheckTargetGoalDifference` — literally "compare the
   **target** goal difference with the current goal difference". The coach reacts
   to his *objective*, not the raw scoreline. A side that needs a draw sits deep
   at 0–0; a side that needs two goals attacks at 1–0 up. GF's
   `MatchMentality::Decide` uses the raw goal difference and so cannot express
   either.
2. The thresholds **tighten as the match runs out**: ±2 through the second half,
   ±1 after the 75th minute. One number, and it produces the whole late-game
   shape.

A companion `cpuLevel.json` comment shows the level was meant to go further —
`//,"tmAtackLevel": ... // 攻撃レベル(LEVEL1〜5想定)` ("attack level, LEVEL1–5
assumed"), commented out and never shipped. The three-value version is what
exists.

Sibling trees: `patternSelector.xml` branches on `CheckOffence` into
`offencePatternSelector.xml` / `defencePatternSelector.xml`, both of which ship
as `ConditionNone -> NONE`, i.e. the set-piece pattern system exists as
infrastructure with no patterns loaded.

### Konami's own difficulty table

`cpuLevel.json` is the named, commented ancestor of the tier system §4
reconstructs from assembly — six difficulty columns, marked ☆ to ☆☆☆☆☆☆, and
per-feature values. Translated in full:

| Field | ☆ | ☆☆ | ☆☆☆ | ☆☆☆☆ | ☆☆☆☆☆ | ☆☆☆☆☆☆ | Meaning |
|---|---|---|---|---|---|---|---|
| `dfKickReactionAddWait` | 12 | 8 | 4 | 3 | 0 | 0 | extra frames before reacting to a kick |
| `dfDribbleReactionAddWait` | 5 | 5 | 3 | 3 | 0 | 0 | extra frames before reacting to a dribble |
| `dfPassPositioning` | 0 | 0 | 1 | 1 | 1 | 1 | positions to cut passing lanes |
| `dfMark` | 0 | 1 | 1 | 1 | 1 | 1 | marks at all |
| `dfPress` | 0 | 1 | 1 | 1 | 1 | 1 | presses at all |
| `dfSand` | 0 | 0 | 0 | 1 | 1 | 1 | double-teams |
| `obSpaceRun` | 0 | 0 | 1 | 1 | 1 | 1 | makes runs into space |
| `obCounterRun` | 0 | 0 | 0 | 1 | 1 | 1 | makes counter-attacking runs |
| `obLineBreak` | 0 | 0 | 0 | 1 | 1 | 1 | breaks the defensive line |
| `obDiagonalRun` | 0 | 1 | 1 | 1 | 1 | 1 | makes diagonal runs |
| `bpShootTimingAddWait` | 5 | 3 | 3 | 1 | 0 | 0 | extra frames before shooting |
| `bpPassTimingAddWait` | 5 | 3 | 3 | 1 | 0 | 0 | extra frames before passing |
| `bpDirectPlay` | 0 | 0 | 1 | 1 | 1 | 1 | plays first-time |
| `gkKickReactionAddWait` | 5 | 3 | 3 | 2 | 0 | 0 | keeper reaction delay |
| `cpJostleWinRate` | 0 | 0 | 0 | 0 | 0 | 0 | duel bias toward the cursor player (unused) |

**Not one stat is touched.** Difficulty is reaction delay plus a set of boolean
capability switches — exactly the design §4 infers from PES 2021's assembly, here
stated outright by Konami with field names. This is the single strongest piece
of evidence behind borrow item 1, and it hands over usable numbers: a beginner
AI is 12 frames (200 ms) slower to react and simply does not press, mark, or run
beyond the line.

### What experienced modders actually change

Diffing stock `dt18_all.cpk` against `download/4cc_06_gameplay.cpk` — the 4cc
community's gameplay mod, the one actually enabled in this install — 46 of 79
objects differ. The concentration is informative: **`constant_team.bin` is the
most heavily rewritten archive**, i.e. the knobs that matter to people who play
this game seriously are collective movement and defensive shape, not shooting or
dribbling.

| Object | Changed | What the direction of travel says |
|---|---|---|
| `basePosition` | 107/216 | the base shape is largely rewritten, including an eight-value block re-signed entirely |
| `defenceMark` | 67/80 | marking pickup radius `22.5 → 50` — more than doubled |
| `spaceRun` | 46/60 | ball-distance gates tightened `90 → 80`, `110 → 100`; two dormant values `0 → 95`, `10 → 100`, switching on a check stock leaves off |
| `support` | 33/60 | |
| `defence` | 28/36 | defensive-line distances pulled in hard: `50 → 24`, `35 → 28`, `70 → 50` |
| `subConcept` | 27/52 | |
| `defenceCover` | 26/36 | |
| `diagonalRun` | 20/24 | `45 → 64`, `80 → 88`, and a `0 → 1` that enables a disabled branch |
| `lineBreak` | 11/16 | uniformly widened — `60,80,60,90,15,25 → 82,120,90,128,22,42`, counts `3,5 → 4,7`: many more line-breaking runs |
| `pullAway` | 10/12 | every single threshold raised |
| `throughpass` | 63/120 | range gates cut hard: `120,85,85 → 70,60,55` |
| `ballplayerShoot` | 16 → 224 bytes | the only object that *changes size*: stock ships a zeroed 4-dword stub, the mod ships a populated 56-dword object |
| `pesSmart` | 37/212 | assistance curves lowered across the board; three boolean flags turned on |
| `motivation` | 2 | swing more than doubled, `0.95, 1.05 → 2, 2.5` |
| `ballplayerGk` | 7 | keeper comes off his line less far: `35 → 26`, `30,35,6 → 25,26,3` |
| `press` / `reaction` | 1 each | one boolean on, one boolean off |

Read as a whole the mod says: **make off-the-ball movement much more frequent and
much more varied, tighten the defensive line, cut the range of assisted through
balls, and reduce automatic assistance.** That is a coherent design thesis, and
it is close to what GF's own `AITactics` run and pressure triggers already model
— which is encouraging, because it means GF is tuning the right knobs and can
lean on this diff for defaults.

Reproduce with:

```sh
python3 tools/pes21_import/cpk.py "<PES21>/Data/dt18_all.cpk" out/stock
python3 tools/pes21_import/cpk.py "<PES21>/download/4cc_06_gameplay.cpk" out/mod
python3 tools/pes21_import/wesys_constant.py list out/stock/common/match/constant/constant_team.bin
python3 tools/pes21_import/wesys_constant.py show out/stock/common/match/constant/constant_team.bin pullAway
python3 tools/pes21_import/wesys_constant.py diff \
    out/stock/common/match/constant/constant_team.bin \
    out/mod/common/match/constant/constant_team.bin
```

---

## 9. What GameplayFootball already has

Mapped file by file. GF's *instruction* and *philosophy* layers are in good
shape structurally — but see the wiring defect immediately below, which today
silences half of both.

### Fix this first: half the tactics pipeline is computed and then ignored

`TeamAIController::UpdateTactics` builds `liveTeamTactics` through the full
chain — base defaults → philosophy preset → user slider mods → advanced
instructions → reactive mentality (`src/onthepitch/teamAIcontroller.cpp:1325-1427`).
But `liveTeamTactics` is only ever *read* for `position_*` keys
(`teamAIcontroller.cpp:426-459`, `:1421`, `:1426`). Every other slider is read
straight from the raw team data, bypassing the whole pipeline:

| Slider | Read raw at |
|---|---|
| `team_pressure` | `teamAIcontroller.cpp:252`, `:1235` |
| `counter_attack` | `teamAIcontroller.cpp:286`, `:307`, `:346`, `:419`, `:1188` |
| `support_distance` | `elizacontroller.cpp:773`, `humanoid.cpp:3117` |
| `dribble_offensiveness`, `dribble_centermagnet` | `AIfunctions.cpp:460`, `:488` |

So Gegenpressing's `team_pressure = 1.0`, Tiki-Taka's `support_distance = 0.25`
and `counter_attack = 0.3`, Park the Bus's `team_pressure = 0.15`, and the
`TikiTaka` / `LongBallCounter` / `FrontlinePressure` / `AggressiveDefence`
instruction nudges all land in a property set nothing consults. Only the
positional half of philosophies and instructions reaches the pitch.

Three smaller wiring defects in the same area:

- `teamTacticsModMultipliers` (`teamAIcontroller.cpp:80-89`) has no entries for
  `position_*_ownhalf_factor` or `position_*_midfieldfocus_strength`, so user
  sliders for those four are silently multiplied by 0.
- Zone pressure and the counter-attack trigger are both gated on
  `team->GetHumanGamerCount() == 0` (`teamAIcontroller.cpp:245`, `:272`) — a
  human-controlled side never presses as a unit and never springs a counter.
- `TeamPhilosophy::PressesOnPossessionLoss` — the counter-press-on-loss switch,
  the defining behaviour of Gegenpressing — has **no caller anywhere**.

None of the borrowings below are worth much until this is fixed; a one-line
routing change unlocks tactical depth that is already written and tested.

### The map

| PES concept | GF status | Where |
|---|---|---|
| Tactical archetypes (gegenpress / tiki-taka / park the bus) | **has, partly inert** — 4 philosophies with preset sliders, stamina drain multiplier, pass error multiplier, trap-depth adaptation, picked by CPU managers and from the game plan menu; but the pressure/counter/support half of each preset never reaches the pitch, and the counter-press switch is uncalled | `src/onthepitch/teamphilosophy.hpp/.cpp`, `src/onthepitch/aimanager.cpp:15`, `src/menu/gameplan.cpp:405`, applied at `teamAIcontroller.cpp:1356` |
| Team mentality ladder | **has** — 5 rungs, `PushUp`/`DropBack`, d-pad presets, applied as slider offsets (the depth half lands, the pressure half does not) | `src/onthepitch/teaminstructions.hpp/.cpp`, driven from `src/onthepitch/match.cpp` |
| Advanced instructions | **has, partly inert** — 7: frontline pressure, deep defensive line, aggressive defence, hug the touchline, centre shading, tiki-taka, long ball counter. The two purely positional ones (hug the touchline, centre shading) work fully; tiki-taka and long ball counter are entirely inert | `src/onthepitch/teaminstructions.cpp`, applied at `teamAIcontroller.cpp:1411` |
| Reactive in-match mentality (chase / kill the game) | **has** — desperation from 80', time-wasting from 85', formation override, corner-sheltering | `src/onthepitch/matchmentality.hpp/.cpp`, `src/onthepitch/AIsupport/AIfunctions.cpp:494` |
| Playing styles / cards | **has** — PES's 21 Playing Styles and 7 COM cards (`src/data/playingstyles.hpp/.cpp`) and the 41 Player Skills (`src/data/playerskills.hpp/.cpp`, table in `docs/PES21_IMPORT.md`), each read by a real decision; the eight trick-move skills and the Trickster card queue the imported feint clips from `PlayerController::_BallControlCommand`. Flip Flap, Scotch Move and Heel Trick have no clip and are not wired | `playerskills.cpp`, consumed in `elizacontroller.cpp`, `playercontroller.cpp`, `humanoid.cpp`, `humanoid_utils.cpp`, `AIfunctions.cpp`, `teamAIcontroller.cpp`, `penaltyshootoutcontroller.cpp`, `referee.cpp`, `match.cpp`, `player.cpp` |
| Position-compatibility gate for styles | **partial** — `PlayingStyles::SuitsRole` / `PlayerSkills::SuitsRole` gate *inference* only; a card the database names fires wherever the player is deployed | `src/data/playingstyles.cpp`, `src/data/playerskills.cpp` |
| Position familiarity stat penalty | **absent** — `PlayerData::GetRoles()` already holds a playable-position list, unused for this | `src/data/playerdata.hpp:25` |
| Per-match form / condition arrows | **absent from gameplay** — career mode keeps `int form = 50` and `int condition = 70` but no on-pitch code reads either | `src/data/careerdata.hpp:60,233` |
| Team spirit / chemistry / morale | **absent from gameplay** — `MoraleManager` is fully written, compiled, and instantiated by nothing; its `GetPerformanceMultiplier` (0.85–1.15) never reaches `Player::GetStat` | `src/data/moraledata.hpp/.cpp` |
| Super-sub | **has** — bench bonus in the substitution pick (`PlayerSkills::GetSubstituteBonus`) | `src/onthepitch/match.cpp` (substitution candidates) |
| Live stat modifier pipeline | **partial and flat** — one function multiplies a blanket difficulty factor, a blanket fatigue factor and injury; carries the literal comment `todo: some stats are more affected by fatigue than others` | `src/onthepitch/player/player.cpp:658` |
| Stat clamping | **absent** — no floor/ceiling after modifiers | — |
| Continuous stamina drain | **has** — `fatigueFactorInv`, workload-weighted, philosophy-scaled | `src/onthepitch/player/playerbase.hpp:102`, `src/onthepitch/player/player.cpp:455`, `src/onthepitch/gameplaytuning.hpp:56` |
| Threshold-based fatigue performance tier | **absent** — GF's fatigue is purely continuous |  — |
| Mental skills modifying fatigue (Captaincy, Fighting Spirit) | **absent** — no captain concept in the trait set | — |
| Clutch / pressure mental model | **has** — GF's own take: clutch technical bonus in the last ten minutes of a close game, stumble chance under pressure with a youth penalty | `src/onthepitch/matchpressure.hpp/.cpp`, `elizacontroller.cpp:1124` |
| Difficulty as AI capability tiers | **absent** — one float `match_difficulty`, read in exactly three places: a blanket CPU stat multiplier, reaction time, and hunt distance | `src/onthepitch/match.hpp:271`, `src/onthepitch/player/player.cpp:664`, `src/onthepitch/player/controller/playercontroller.cpp:64`, `elizacontroller.cpp:429` |
| Kick deliberation hold | **absent** | — |
| Duel score cascade | **absent** — tackles are resolved by the trip/foul path, no scored contest | `src/onthepitch/referee.cpp:338` |
| Max-3-engagement limit on one target | **absent** | — |
| Near-box defender advantage | **absent** | — |
| Offside trap / defensive line | **has** — `offsideTrapX`, philosophy-adapted, applied to the back X bound | `src/onthepitch/teamAIcontroller.cpp:166-184,533` |
| Zone pressure, counter tendency, support web | **has** — pure tunable functions | `src/onthepitch/aitactics.hpp` |
| Named action vocabulary + properties/transition tables | **absent** — GF uses function types and per-controller state | — |
| Shared pitch geometry struct | **partial** — `pitchHalfW` / `pitchHalfH` / `lineHalfW` constants | `src/onthepitch/defines.hpp` |
| Formations | **has** — 7 shapes, 10 roles matching PES's merged position set almost exactly | `src/data/formations.hpp` |

---

## 10. BORROW / ENHANCE, prioritized

Ranked by (simulation quality gained) ÷ (code touched).

| # | Item | Why | Effort |
|---|---|---|---|
| 0 | **Route the non-positional sliders through `liveTeamTactics`** | Not a borrowing at all — a wiring fix. Half of the philosophy and instruction systems, already written and unit-tested, is currently computed and discarded. Highest ratio in the list by a wide margin, and everything else reads better once tactics actually apply. | XS |
| 1 | **Difficulty as capability tiers, not a stat tax** | GF currently multiplies every CPU stat by `0.3 + 0.7 × difficulty`. At low difficulty that produces slow, weak, *unrealistic* players; PES instead ships full-strength players with fewer AI faculties and slower decisions. Confirmed twice over — inferred from PES 2021 assembly in §4, and stated outright with Konami's own field names in `cpuLevel.json` (§8), which also supplies the numbers. Strictly better football at every setting, and it retires an ugly global hack. | M |
| 1b | **Coach attack level driven by *target* goal difference** | `CoachAttackLevelChange.xml` (§8) is a five-node decision tree GF could adopt almost verbatim, and it is strictly better than `MatchMentality::Decide` in two ways: it compares against the manager's objective rather than the raw scoreline, and it tightens its thresholds from ±2 to ±1 after the 75th minute. Both are small edits to a function GF already has and already tests. | S |
| 2 | **Position familiarity penalty** | The data is already there (`PlayerData::GetRoles()`) and it is the single cheapest way to make squad selection matter. Three tiers × three ratings = nine constants. | S |
| 3 | **Tiered stat modifier pipeline + clamping** | Turns `Player::GetStat`'s flat multiplier into PES's layered product, closes an existing in-code TODO, and is the socket every later item (form, super-sub, familiarity, fatigue tiers) plugs into. Do this before 2, 4 and 6. | M |
| 4 | **Per-match form (condition arrows)** | Five states, three stat tiers, fifteen numbers. Enormous perceived variety for almost no code; also gives commentary and menus something to talk about. | S |
| 5 | **Duel score cascade with the near-box thumb and the 3-engagement cap** | GF has no scored physical contest and no cap on how many defenders converge. The cap alone fixes swarming; the ×0.2/×0.8 penalty-area asymmetry manufactures last-ditch defending for free. | M |
| 6 | **Fatigue performance tier + captain/fighting-spirit mental traits** | Layers a threshold penalty on top of GF's continuous drain, so the last twenty minutes degrade *decisions* and not just top speed — and gives GF its first team-wide leadership mechanic. Skip PES's broken dynamic-morale loop, or better, implement what it *intended*. | M |
| 7 | **Position gate on traits** | One line at each trait check, but it needs a role→trait compatibility table first. Prevents a Fox in the Box centre-back. | S |
| 8 | **Super-sub trait** | Trivial once 3 exists; rewards bench management. | S |
| 9 | **Style effects expressed as positional-envelope offsets in metres** | GF's traits are mostly stat/behaviour tweaks; PES's are boundary shifts. Adopting the idiom (`+8 m forward`, `±15 m lateral`, `−6 m deeper`) would make Roaming Flank, Orchestrator, Build Up and Anchor Man expressible in `TeamAIController` with no new machinery. | M |
| 10 | **`_good`-style graded run parameters** | One enhanced parameter set reachable three ways (game state, style-at-position, spatial test) is a very economical way to make counter-attacks feel different from build-up for everybody at once. | M |
| 11 | **Kick deliberation hold** | A per-tier "hold before committing" window, exempting core actions (pass / through pass / shot). Directly buys GF a midfield instead of end-to-end ping-pong at low tiers. | S |
| 12 | **Named action vocabulary with property + transition tables** | The right long-term architecture, but a large refactor of GF's controller state. Park it; borrow the *idea* (explicit fallback per state) opportunistically. | L |
| 13 | **Single shared pitch-geometry struct** | Tidiness: referee, AI and physics reading one FIFA-standard struct instead of scattered constants. Also a prerequisite for a proper penalty-area test in the rules work. | S |
| 14 | **Wire `MoraleManager` in as team spirit** | PES's own team-spirit handle turned out to be registered and never read — an honest reminder that GF's orphaned `MoraleManager` is the same failure. It is already written and clamps to 0.85–1.15; adopting it as a layer in the sketch-3 pipeline costs almost nothing and gives career mode a route onto the pitch. | S |
| 15 | **Give Prolific Winger and Box-to-Box real effects** | Both are assigned to players and read by nothing. PES's versions are cheap: Prolific Winger is a ±5 m touchline shift, Box-to-Box is a dynamic ±10–15 m ball-relative X plus +50% marking range. Two of the smallest wins available. | S |

Explicitly **do not** borrow: the broken dynamic morale loop; the incomplete
native duel system; the zero-frame deliberation at high tiers; the SS/Hole
Player mask omission; the Super-sub table that forgot two stats; the missing
`case 6` in the tier mapping. They are useful mainly as evidence of what goes
wrong.

---

## 11. Implementation sketches for the top five

### 1. Difficulty as capability tiers

New header, no dependencies, testable in isolation — same idiom as
`aitactics.hpp`:

```cpp
// src/onthepitch/aidifficulty.hpp
namespace AIDifficulty {

enum e_Tier {  // 0..6, mirroring PES Beginner..Legend
  e_Tier_Beginner = 0, e_Tier_Amateur, e_Tier_Regular, e_Tier_Professional,
  e_Tier_TopPlayer, e_Tier_Superstar, e_Tier_Legend, e_Tier_Count,
};

// Existing config is a float in [0, 1]; keep it and derive the tier, so
// football.config stays valid.
e_Tier FromDifficulty(float matchDifficulty);

// A human-controlled side's own AI always plays at Superstar: the manager
// should never be sabotaged by his own teammates.
e_Tier ForTeam(float matchDifficulty, bool humanControlled);

// Feature gates. The first block is Konami's own cpuLevel.json (§8), which is
// the more trustworthy source: named fields, six columns, no stat changes.
bool AllowsMarking(e_Tier t);              // dfMark:          off at Beginner only
bool AllowsPressing(e_Tier t);             // dfPress:         off at Beginner only
bool AllowsPassLaneCutting(e_Tier t);      // dfPassPositioning: t >= Regular
bool AllowsDoubleTeam(e_Tier t);           // dfSand:          t >= Professional
bool AllowsSpaceRuns(e_Tier t);            // obSpaceRun:      t >= Regular
bool AllowsCounterRuns(e_Tier t);          // obCounterRun:    t >= Professional
bool AllowsLineBreakRuns(e_Tier t);        // obLineBreak:     t >= Professional
bool AllowsDiagonalRuns(e_Tier t);         // obDiagonalRun:   off at Beginner only
bool AllowsFirstTimePlay(e_Tier t);        // bpDirectPlay:    t >= Regular

// From the PES 2021 trace (§4), where cpuLevel.json is silent.
bool AllowsOffsideTrap(e_Tier t);          // t >= e_Tier_Regular
bool AllowsLineCompression(e_Tier t);      // t >  e_Tier_Regular
bool AllowsScorelinePressing(e_Tier t);    // t >= e_Tier_Professional
bool AllowsSmartPassTarget(e_Tier t);      // t >= e_Tier_Superstar

// Reaction delays, in milliseconds. cpuLevel.json gives frames at 60 fps:
//   kick reaction    12, 8, 4, 3, 0, 0   -> 200, 133, 67, 50, 0, 0
//   dribble reaction  5, 5, 3, 3, 0, 0   ->  83,  83, 50, 50, 0, 0
//   shoot / pass      5, 3, 3, 1, 0, 0   ->  83,  50, 50, 17, 0, 0
//   keeper kick       5, 3, 3, 2, 0, 0   ->  83,  50, 50, 33, 0, 0
// GF's seven tiers interpolate the six columns; this is where the existing
// `reactionTime_ms += (1 - difficulty) * 100` in playercontroller.cpp belongs.
unsigned int GetReactionDelay_ms(e_Tier t, e_ReactionKind kind);

// Graduated handicaps, PES 2021's own shapes.
float GetDefensiveLineForwardPush_m(e_Tier t);  // 10, 7.5, 5, 2.5, 0, 0, 0
float GetDefensiveWidthLooseness(e_Tier t);     // 0.40, 0.25, 0, 0, 0, 0, 0
float GetPrecisionFactor(e_Tier t);             // (6 - t) / 6
float GetPassZoneSlack_m(e_Tier t);             // GetPrecisionFactor(t) * 6.7f
float GetRunTrajectorySlack(e_Tier t);          // GetPrecisionFactor(t) * 0.5f
unsigned int GetKickHold_ms(e_Tier t, bool onBall);  // 1000,667,500,333,167,0,0 / two thirds of that

}
```

The run gates map straight onto machinery GF already has:
`AllowsCounterRuns` onto `AITactics::ShouldLaunchCounter`, `AllowsPressing` and
`AllowsDoubleTeam` onto `ShouldStartZonePressure` and the secondary-presser
selection, `AllowsSpaceRuns` onto `GetAttackingRunThreshold`. Most of item 1 is
therefore adding an `if` at call sites that already exist.

Then:

- `src/onthepitch/player/player.cpp:664` — **delete** the
  `multiplier = 0.3f + 0.7f * GetMatchDifficulty()` line. This is the point of
  the change: CPU players stop being weaker and start being less clever.
- `src/onthepitch/teamAIcontroller.cpp:166-184` — gate the offside-trap
  calculation on `AllowsOffsideTrap`, and add
  `GetDefensiveLineForwardPush_m` to the computed line depth. Below Regular
  the line simply does not spring.
- `src/onthepitch/teamAIcontroller.cpp:533` — gate the back-bound compression
  on `AllowsLineCompression`.
- `elizacontroller.cpp:429` — replace the ad-hoc `0.3f + difficulty * 0.7f`
  hunt-distance scaling with `GetPrecisionFactor`-derived slack, so all the
  difficulty slop comes from one place.
- pass-target and support-run selection: add `GetPassZoneSlack_m` to the
  tolerance when rating a target, and gate any "best available option" search on
  `AllowsSmartPassTarget` — below Superstar, pick the first acceptable option
  rather than the optimum.
- `Team` caches its tier once at kickoff via `ForTeam`, so nothing re-derives it
  per frame.

Tests: one per gate boundary, plus a monotonicity test that
`GetPrecisionFactor` is non-increasing and hits exactly 0 at Legend.

### 2. Position familiarity

```cpp
// src/data/positionfamiliarity.hpp
namespace PositionFamiliarity {

enum e_Level { e_Level_Unfamiliar = 0, e_Level_Partial, e_Level_Natural };
// PES's three stat tiers: technical hit hardest, physique least.
enum e_StatTier { e_StatTier_Technical = 0, e_StatTier_Awareness, e_StatTier_Physique };

e_StatTier GetStatTier(const char* statName);

// Unfamiliar 0.80/0.82/0.84, partial 0.92/0.94/0.96, natural 1.0.
float GetMultiplier(e_Level level, e_StatTier tier);

// PlayerData::GetRoles() is the playable-position list: exact match is natural,
// an adjacent role in the same zone is partial, anything else unfamiliar.
e_Level GetLevel(const std::vector<e_PlayerRole>& roles, e_PlayerRole deployedRole);

}
```

`GetLevel`'s zone adjacency table follows PES's grouping: CB↔LB/RB,
DM↔CM, CM↔AM, LM↔RM, AM↔CF. Feed it from the player's *formation slot role*
(`Formations::Slot::role` via `TeamAIController`), never from his live position —
PES is explicit that a forward tracking back takes no penalty, and that
distinction is what stops the mechanic feeling arbitrary.

Consumed inside the pipeline from sketch 3.

### 3. Layered stat pipeline in `Player::GetStat`

`src/onthepitch/player/player.cpp:658` becomes a product of named layers rather
than one opaque multiplier:

```cpp
float Player::GetStat(const char* name) const {
  const float base = playerData->GetStat(name);
  const StatModifiers::Tier tier = StatModifiers::GetTier(name);

  float m = 1.0f;
  m *= StatModifiers::GetFormMultiplier(form, tier);                 // Array 2
  m *= PositionFamiliarity::GetMultiplier(familiarity, tier);        // Array 3
  m *= StatModifiers::GetSuperSubMultiplier(traits, cameOnInSecondHalf, tier);  // Array 4
  m *= StatModifiers::GetFatigueMultiplier(GetFatigueFactorInv(), tier);        // Array 5
  m *= 1.0f - injuryLevel * 0.4f;

  return StatModifiers::Clamp(base * m);   // PES clamps 40-99 on a 0-99 scale
}
```

Two details matter. First, the tier lookup must be a `switch`/lookup on an
interned stat id, not `strcmp` per stat per frame — GF already string-compares
here and it should be fixed while the function is open. Second,
`GetFatigueMultiplier` replaces today's flat `0.7 + 0.3 × fatigueFactorInv` with
tier-dependent slopes (physique first, technique next, awareness last), which is
exactly what the existing TODO on line 667 asks for.

Cache the per-player modifier product and invalidate on the events PES uses:
formation/position change, substitution, and half transitions. Nothing here
needs to recompute per frame except the fatigue layer.

### 4. Per-match form

```cpp
// src/data/formdata.hpp
namespace Form {
enum e_Arrow { e_Arrow_Terrible = 0, e_Arrow_Poor, e_Arrow_Normal, e_Arrow_Great, e_Arrow_Top };

// Deterministic from player id + match seed, so the same fixture always plays
// out the same way and replays stay honest. Weighted toward Normal.
e_Arrow RollForMatch(int playerDatabaseID, unsigned int matchSeed, float conditioning);

float GetMultiplier(e_Arrow arrow, StatModifiers::Tier tier);   // the 15-value table
}
```

Roll at match construction, store on `PlayerData` alongside `traits`, surface it
in the lineup menu as an arrow glyph (PES's own affordance — the mechanic is
worth much less if the player cannot see it). Feed `GetMultiplier` into sketch 3.
The `conditioning` input lets career mode bias the roll without changing the
in-match code.

### 5. Duel score cascade

New pure module beside `aitactics.hpp`, so it is unit-testable without a match:

```cpp
// src/onthepitch/duel.hpp
namespace Duel {

struct Context {
  bool attacking = true;
  bool nearOwnBox = false;          // defender in his own penalty area
  bool nearOpponentBox = false;     // attacker in the opponent's penalty area
  int  engagingTeammates = 0;       // how many are already on this target
  float physicalStat = 0.5f;        // 0..1
  float engagementStat = 0.5f;      // 0..1, higher = more controlled
  float distance_m = 1.0f;
};

// PES caps this at 3. The single highest-value line in the whole module.
const int maxEngagementsPerTarget = 3;
bool MayEngage(const Context& c, bool targetIsVulnerable, bool targetIsKeeper);

// 1 at <0.8 m, 2 at 0.8-1.2, 3 at 1.2-1.5, 4 beyond: you cannot commit hard
// without room to commit.
int GetEngagementLevel(float distance_m);

// Inverse: a low engagement stat engages maximally, a high one stays controlled.
float GetEngagementAggression(float engagementStat);

// Multiplicative cascade with a floor, PES's own constants: 0.5 defending,
// 0.2 attacker near the opponent's box, 0.8 defender near his own.
float GetScore(const Context& c, float randomSample);

}
```

Wire `MayEngage` into the presser/marker selection in `TeamAIController` (the
same place that already picks a primary and secondary presser via
`AITactics::GetSecondaryPressureRolePenalty`) — that alone stops swarming.
Wire `GetScore` into `Referee::TripNotice`'s neighbourhood in
`src/onthepitch/referee.cpp:338`: today a contest is resolved implicitly by
whoever's tackle animation lands. Scoring it first, with the penalty-area
asymmetry, gives defenders their hardcoded home advantage in the box without
touching a single stat.

---

## 12. Where PES data would still help

The exe research is complete on its own terms, and §8 closed most of the data
gaps. What is genuinely still open, roughly in order of what it would buy:

- **A byte-level field map for the PES 2021 `.o` objects.** The PES 2015/16 JSON
  gives the vocabulary (`checkBallDist_chance`, `chanceScore`, `angleWidth0`,
  `sectorDist1`, …) but the layouts grew and no longer line up. Labelling them
  means correlating value *patterns* per object across the two generations —
  tractable, and it would turn the 4cc diff in §8 from "these offsets moved" into
  "these behaviours were tuned". Highest-value follow-up.
- **`normal` vs `_good` parameter pairs.** §1 shows the mechanism and the three
  triggers; the actual paired values sit inside `spaceRun.o` and would show how
  much enhancement PES thought was appropriate.
- **Fluid formation.** Not found in either source. Likely in
  `dt10_x64.cpk`'s `pesdb/Tactics.bin` / `TacticsFormation.bin`, which were
  located but not decoded.
- **Per-instruction parameter deltas** — what "hug the touchline" actually writes.
  Same files as above.
- **The COM playing-style system** (Trickster, Mazing Run, Speeding Bullet,
  Incisive Run, Long Ball Expert, Early Cross, Long Ranger) — a separate bitmask
  whose offset and readers are untraced.
- **What sets a condition arrow**, and how it relates to the 0–3 `Conditioning`
  stat.
- **Set-piece routines.** 166 `positionKickOff_<formation>_{offence,defence}.fox`
  tables exist in the PES 2016 archives, and the pattern-selector trees that
  should consume them ship empty. Worth a look if GF ever wants designed set
  pieces rather than generated positions.

Note also `liveCT/` in the install: 78 Cheat Engine tables, one per `.o`. They
are auto-generated byte scans with no field names, so they do not help with
labelling.
