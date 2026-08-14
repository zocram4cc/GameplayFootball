# Ruleset audit: GameplayFootball vs the Laws of the Game and PES 2021

Three-way comparison of what a football match is supposed to do, what PES 2021
actually does, and what GameplayFootball implements today.

Sources:

- **Laws** — the IFAB Laws of the Game, current edition. Law numbers are cited so
  each row can be checked.
- **PES 2021** — the referee and card system traced in
  `User_Aoba_PES2021 Engine Research - Rigged Wiki.pdf`, cross-verified by that
  research against a clean PES 2020 binary. See `PES21_TACTICS.md` for the wider
  engine context. Where that research is silent, this document says so rather
  than guessing — PES's offside implementation, handball and keeper laws were
  never traced, and absence of evidence is not evidence of absence.
- **PES 2021 data** — read from the local install with
  `tools/pes21_import/wesys_constant.py`, including the PES 2015/16-era archives
  that ship alongside it and still carry readable field names. See
  `PES21_TACTICS.md` §8.
- **GF** — `src/onthepitch/referee.cpp` / `.hpp`, `refereeprofile.cpp/.hpp`,
  `officials.cpp`, `matchprogression.cpp`, `penaltyshootout*.cpp`, the foul
  producers in `match.cpp`, and `AIsupport/AIfunctions.cpp`.

`tools/pes21_import/foul_severity.py` is a runnable transcription of PES's
foul-scoring pipeline with its real constants — `table` prints the severity
bands, `sweep` shows the score surface over speed and contact, `eval` scores a
single challenge. It is a reference for choosing GF's own numbers, not an
importer.

Line numbers were taken from a working tree that the parent session is editing
concurrently; function names are the durable reference.

---

## 1. How GF's referee works today

Worth 30 lines of orientation, because the audit table only makes sense against
it.

**State.** There is no `e_GameMode` and no foul-type enum. The referee's entire
state is `RefereeBuffer` (a queued restart: kind, team, three timestamps,
position, taker) plus one `Foul` struct (`referee.hpp:19-39`), whose severity is
a bare `int foulType` documented in a comment as `0: nothing, 1: foul, 2: yellow,
3: red`. The restart vocabulary is seven values — `e_SetPiece` in
`gamedefines.hpp`: none, kick-off, goal kick, free kick, corner, throw-in,
penalty. There is no dropped ball and no direct/indirect distinction.

**Loop.** `Referee::Process()` runs on the 10 ms tick and splits in two. In play,
it checks four things in fixed order, each calling `CheckFoul()` first so a
pending foul beats an out-of-play event: period end, ball behind the goal line,
ball over the touchline, then `CheckFoul()` again. Out of play, it advances the
queued restart by exact timestamp equality (`stopTime + 300` → short whistle,
`prepareTime` → position players, `prepareTime + 2000` → whistle and start), and
considers the restart taken when the taker's touch animation lands.

**Foul detection lives in `Match`, not the referee.** Two independent producers
call `Referee::TripNotice(tripee, tripper, tackleType)`: a body-collision
sensitivity ladder (balance stat, penetration depth, ball proximity → thresholds
0.38 / 0.48 / 0.58 → type 1 or 2) and a tackle AABB test between the tackler's
body parts and the victim's feet/lower legs (type 3 for a slide, type 1 for an
`Interfere`).

**Decision.** `TripNotice` handles only types 2 and 3; **type 1 is silently
dropped**. Type 2 is always `foulType = 1` — never a card. Type 3 computes a
severity in roughly `[0, 2]`:

```
severity  = pow(clamp(|touchFrame - currentFrame| / touchFrame), 0.7) * 0.5   // timing error
severity += NormalizedClamp(|ballPos - touchPos|, 0, 2) * 0.5                 // distance from the ball
severity += dot(normalize(victimPos - tripperPos), victimFacing) * 0.5 + 0.5  // from behind?
```

and `RefereeProfile::GetFoulType` maps it: `>= red(2.0)` → 3, `>= yellow(1.4)` →
2, `> foul(1.0)` → 1, else 0. The three thresholds scale ×0.85 (strict) or ×1.2
(lenient) from the `referee_profile` config key.

**Penalty test.** A plain rectangle: `|y| < 20.15 - lineHalfW` and
`x * -victimSide > pitchHalfW - 16.5 + lineHalfW` (`CheckFoul`).

**Cards.** `Player::GiveYellowCard` does `cards++`, `GiveRedCard` does
`cards += 3`; `cards > 1` triggers `SendOff()`, so a second yellow sends off
implicitly. Cards accumulate for the whole match (`cards` is zeroed only in the
constructor), the sent-off player is deactivated so the team really does play
with ten, and he cannot be substituted.

---

## 2. The table

Status vocabulary: **has** = implemented and behaves as the Law describes;
**differs** = implemented but not to the Law; **lacks** = no implementation.

### Law 5 — the referee

| Rule | Laws | PES 2021 | GF | Pointer |
|---|---|---|---|---|
| Advantage: play on when the offended team benefits | Law 5 | not traced; a gate suppressing cards while a scene-node flag is set is *suspected* to be advantage/cutscene | **has** — `advantage` flag, 600 ms grace, window of 1800/3000/4500 ms by profile, ends early when possession is lost | `Referee::CheckFoul`, `RefereeProfile::GetAdvantageWindow_ms` |
| Card must still be shown at the next stoppage if the advantage runs | Law 5 | second-booking history is kept in a per-player array in the judge object | **lacks** — the foul *and its card* are thrown away when the window expires; the code says so: `// todo: yellow cards need to be remembered though ;)` | `Referee::CheckFoul` |
| Advantage signalled to players/spectators | Law 5 | — | **differs** — a debug-only `SpamMessage("advantage")` behind `!IsReleaseVersion()` | `Referee::TripNotice` |
| Referee strictness varies by official | not a Law | **lacks** — thresholds are hardcoded constants, identical for every referee; the menu's "referee strictness" does nothing | **has** — three profiles scaling all three thresholds and the advantage window | `refereeprofile.cpp` |
| Stop play for a serious injury | Law 5 | **has** — a graded injury model on the same 0–255 damage scale as the foul score: four severity levels (micro 120, minor 180, middle 220, serious 240) and six symptom types (bruise 120, inflammation 160, laceration 220, torn muscle 230, ligament 240, fracture 250) | **differs** — `Injure(tripType * 0.04f)` reduces stats by a continuous factor; there is no severity ladder, no symptom, and play never stops | `match.cpp` collision path; PES side from `ai/judge/injury.json` |

### Law 7 — duration

| Rule | Laws | PES 2021 | GF | Pointer |
|---|---|---|---|---|
| Allowance for time lost (subs, injuries, cards, celebrations, VAR) | Law 7 | **has** — `lossTime` byte in the match state block, alongside minute and scores | **lacks** — the clock stops entirely during restarts, so nothing accrues and nothing is added on | `Match::Process` clock advance; `matchprogression.cpp` |
| Half ends at a sensible moment, not mid-attack | convention | not traced | **has** — whistles once past the period end when the ball is within a 10 m central band, hard cap 3 minutes | `MatchProgression::ShouldEndPeriod` |
| Half-time interval | Law 7 | **has** — `MATCH_PHASE_HALFTIME` and `MATCH_PHASE_EX_INTERVAL` are distinct phases | **differs** — no half-time phase in `e_MatchPhase`; a signal opens a menu page instead | `gametypes.hpp`, `Referee::Process` |
| Extra time, then penalties | Law 7/10 | **has** — distinct phases including `MATCH_PHASE_PKMATCH` | **has** — ET only when level, both periods always played, penalties only after a level ET | `MatchProgression::GetNext` |

### Law 8 — start and restart

| Rule | Laws | PES 2021 | GF | Pointer |
|---|---|---|---|---|
| Kick-off; opponents outside the centre circle | Law 8 | **has** — 10.15 m exclusion for play kind `KICK_OFF` | **has** — restart at centre, opponents positioned by `PrepareSetPiece` | `Referee::PrepareSetPiece` |
| Dropped ball, to the team that last touched, others ≥ 4 m | Law 8 | **lacks** — no dropped-ball play kind in the enum (`INPLAY`, `KICK_OFF`, `THROW_IN`, `GOAL_KICK`, `CORNER_KICK`, `FREE_KICK`, `PENALTY_KICK`) | **lacks** — no `e_SetPiece` value for it | `gamedefines.hpp` |
| Ball off the referee → dropped ball | Law 8 | **lacks** | **lacks** — officials are non-colliding props | `officials.cpp` |

### Law 11 — offside

| Rule | Laws | PES 2021 | GF | Pointer |
|---|---|---|---|---|
| Offside position = ahead of the ball **and** the second-last opponent | Law 11 | not traced (only an `OFFSIDETRAP` AI action is documented) | **has** — explicitly the one-but-deepest opponent, and `max(that, ballX)` | `AI_GetOffsideLine` |
| Cannot be offside in your own half | Law 11 | not traced | **has** — the line is clamped so it never crosses the halfway line | `AI_GetOffsideLine` |
| Level with the defender is onside | Law 11 | not traced | **differs** — a fixed 0.20 m relaxation is applied in the attacker's favour, which is a house rule rather than the Law's "any part of the body that can score" test | `Referee::BallTouched` |
| Judged at the moment the ball is played by a teammate | Law 11 | not traced | **has** — positions are snapshotted into `offsidePlayers` on every touch | `Referee::BallTouched` |
| Offence only if involved in active play (interfering with play / an opponent / gaining an advantage) | Law 11 | not traced | **differs** — the *only* test is whether that player makes the next touch. A player standing offside who blocks the keeper's view is never penalised; a player who is nowhere near the action but happens to touch next always is | `Referee::BallTouched` |
| **No offside directly from a throw-in** | Law 11 | not traced | **has** | `Referee::BallTouched` |
| **No offside directly from a goal kick** | Law 11 | not traced | **lacks** — flagging is skipped only for throw-ins | `Referee::BallTouched` |
| **No offside directly from a corner kick** | Law 11 | not traced | **lacks** — same | `Referee::BallTouched` |
| Deliberate play by a defender resets the phase; a deflection or save does not | Law 11 | not traced | **lacks** — `offsidePlayers` is cleared on *every* touch, by either team, so any defender contact at all resets the phase | `Referee::BallTouched` |
| Restart: indirect free kick where the offence occurred | Law 11 | not traced | **differs** — a free kick (no indirect concept) at the offending player's position *at the previous touch*, not where he became involved | `Referee::BallTouched` |
| Assistant flags offside | Law 6 | not traced | **lacks** — linesmen track the line visually and decide nothing | `refereecontroller.cpp` |

### Law 12 — fouls and misconduct

| Rule | Laws | PES 2021 | GF | Pointer |
|---|---|---|---|---|
| Careless / reckless / excessive force ladder | Law 12 | **has** — a three-component score (contact intensity 0–40 + relative velocity 0–30 + context 0–80) against three thresholds | **differs** — a comparable severity idea, but reachable only from a sliding tackle | `Referee::TripNotice`, `RefereeProfile::GetFoulType` |
| Fouls from behind judged more harshly | Law 12 guidance | **has** — an approach-angle term worth up to 30 points, zero below 22.5°, maximum past 60° | **has** — a dot-product term worth half the range | `Referee::TripNotice` |
| Speed of the challenge counts | Law 12 guidance | **has** — relative velocity, 0 below 2 km/h, saturating at 30 | **lacks** — velocity is not an input; timing error and ball distance are | `Referee::TripNotice` |
| Standing challenges are not automatically innocent | Law 12 | **has** — a static challenge takes a ×1.0 (no reduction) contact multiplier, a minimum velocity score of 5, and a lunge from standing is penalised *more* than a running challenge | **differs** — a standing tackle is hardcoded to `foulType = 1`, never a card | `Referee::TripNotice` |
| Referees are more reluctant to give penalties than free kicks | Law 12 practice | **has** — the foul threshold rises from 40 outside the box to 60 inside; the yellow threshold stays 80 either way | **lacks** — the same severity ladder applies everywhere; location only changes the restart | `Referee::CheckFoul` |
| Caution (yellow) | Law 12 | **has** — severity 3 | **has** — `foulType == 2` | `Referee::CheckFoul` |
| Second caution → sending-off | Law 12 | **has** — a per-player previous-foul byte plus a second-booking flag; the match event system turns severity 3 + flag into a red | **has** — implicitly, via `cards > 1` | `player.hpp`, `player.cpp` |
| Straight red for serious foul play | Law 12 | **differs** — *unreachable*. The score can exceed the red threshold of 110, but a gate unconditionally caps severity 4 to 3. Every red card in normal PES play is a second yellow | **has** — severity ≥ 2.0 gives `foulType == 3` directly | `RefereeProfile::GetFoulType` |
| DOGSO — denying an obvious goal-scoring opportunity | Law 12 | **has** — fouled player ahead of a reference line and within the penalty-area width (20.12 m); sets severity 4, and adds +30 to the context score | **lacks** — no goal-denial concept at all | — |
| DOGSO in the box with an attempt to play the ball = caution, not red (IFAB 2016) | Law 12 | **has** — this is exactly what the severity-4 cap implements, though PES applies it everywhere rather than only in the box | **lacks** | — |
| Stopping a promising attack (SPA) = caution | Law 12 | not traced | **lacks** | — |
| Cards for non-contact offences (dissent, time wasting, celebration, shirt-pull, off-the-ball) | Law 12 | not traced | **lacks** — the only route to any card is a sliding tackle | `Referee::TripNotice` |
| Simulation / diving = caution | Law 12 | **lacks** — `DIVE` exists as an AI action but no card path reads it | **lacks** | — |
| **Handball** | Law 12 | not traced | **lacks** — no hand or arm participates in ball collision; no offence, no penalty, no red for denying a goal by hand | `match.cpp` collision test uses feet and lower legs only |
| Keeper: six-second rule → indirect free kick | Law 12 | not traced | **lacks** | — |
| Keeper: handling a deliberate back-pass or a teammate's throw-in → indirect free kick | Law 12 | not traced | **lacks** — catching is unrestricted | — |
| Keeper handling outside the area | Law 12 | not traced | **lacks** | — |
| Cards recorded and reported | Law 12 | **has** — severity, second-booking flag, minute, position and both player slots are written to a foul event record | **lacks** — `MatchData` counts goals, possession, shots and fouls; no card counters, no display, no persistence, no career suspensions | `matchdata.hpp`, `matchhistory.cpp` |
| Foul count reflects actual whistles | statistics | — | **differs** — `AddFoul` fires on the collision path *before the referee decides*, and never for sliding tackles, so the displayed count is unrelated to the calls given | `match.cpp` |
| Sending-off leaves the team a man short | Law 3 | **has** — a sent-off flag in the match control state | **has** — `Deactivate()`, and `GetActivePlayers` filters, so AI, offside and possession all see ten | `player.cpp`, `team.cpp` |
| Match abandoned below seven players | Law 3 | not traced | **differs** — `GameOver()` is called at ≤ 6 players but no winner is awarded; the code notes both problems | `player.cpp` |

### Laws 13, 14 — free kicks and penalties

| Rule | Laws | PES 2021 | GF | Pointer |
|---|---|---|---|---|
| Direct vs indirect free kick | Law 13 | **lacks** as a distinction — one `FREE_KICK` play kind | **lacks** — one `e_SetPiece_FreeKick` | `gamedefines.hpp` |
| Opponents 9.15 m from a free kick | Law 13 | **has** — 9.15 m keyed off the play kind | **has** — but as one-off positioning at prepare time, never re-checked | `teamAIcontroller.cpp` wall/exclusion setup |
| Wall | Law 13 | **has** — dedicated wall actions (`WALL_JUMP`, `WALL_NOT_JUMP`, `WALL_PRESS`) | **has** — a three-man wall placed at 9.15 m | `teamAIcontroller.cpp` |
| Encroachment → retake | Law 13/14 | not traced | **lacks** — nothing is re-checked after the whistle | — |
| Penalty from the mark, keeper on the goal line | Law 14 | **has** — a dedicated `KP_MOVE_PENALTY_KICK` positioning action and 10.15 m arc exclusion | **differs** — the spot and the arc exclusion are set up, but there is no keeper-line rule; in the shootout the keeper is simply teleported back if he strays more than 12 m | `Referee::CheckFoul`, `penaltyshootoutcontroller.cpp` |
| Penalty encroachment, double touch, retakes | Law 14 | not traced | **lacks** | — |
| Taker may not touch twice before another player | Law 13/14/15/16/17 | not traced | **lacks** | — |

### Laws 15, 16, 17 — restarts

| Rule | Laws | PES 2021 | GF | Pointer |
|---|---|---|---|---|
| Throw-in awarded to the opponents of the last toucher, from where it crossed | Law 15 | **has** — `THROW_IN` play kind, 6.0 m opponent distance | **has** — restart clamped to the touchline, with a 400 ms relax timer so the throw itself does not retrigger an out-of-play call | `Referee::Process` |
| Foul throw (feet, both hands, over the head) | Law 15 | **has** — four throw-in delivery actions, so the *form* is modelled | **lacks** — no legality check | — |
| Cannot score directly from a throw-in | Law 15 | not traced | **lacks** | — |
| Goal kick from anywhere in the goal area; in play once kicked | Law 16 | **has** — `GOAL_KICK`, 9.15 m exclusion | **differs** — always restarted at a single fixed point (`pitchHalfW * 0.92`), and opponents are not required to be outside the area | `Referee::Process` |
| Corner from the corner arc, opponents 9.15 m | Law 17 | **has** — `CORNER_KICK`, 9.15 m | **has** — corner position chosen from the ball's y sign | `Referee::Process` |
| Corner vs goal kick decided by who touched it last | Law 17/16 | not traced | **has** | `Referee::Process` |

### Law 10 — penalty shootout

| Rule | Laws | GF | Pointer |
|---|---|---|---|
| Five kicks each, alternating | Law 10 | **has** | `penaltyshootout.cpp` |
| Stop early once mathematically decided | Law 10 | **has** | `PenaltyShootout::GetWinner` |
| Sudden death on equal kicks taken | Law 10 | **has** | `PenaltyShootout::IsSuddenDeath` |
| Coin toss for end and for order | Law 10 | **has** | `penaltyshootoutcontroller.cpp` |
| All eligible players must kick before anyone repeats | Law 10 | **differs** — enforced per rotation cycle rather than across the squad | `penaltyshootout.cpp` |
| Reduce to equal numbers | Law 10 | **lacks** | — |
| Keeper must stay on the line; encroachment retakes | Law 10/14 | **lacks** — a stray keeper is teleported back, once per kick | `penaltyshootoutcontroller.cpp` |
| Ball is dead after the first contact (no rebounds) | Law 10 | **has**, but incidentally — the outcome is pre-rolled from stats and the physics result is discarded | `PenaltyShootoutController::SetUpKick` |

### VAR

| Rule | Laws | GF | Pointer |
|---|---|---|---|
| Review of goals, penalties, red cards, mistaken identity | VAR protocol | **lacks in practice** — `ShouldReviewOffside`, `ShouldReviewPenalty` and `varReviewDuration_ms` are written and unit-tested, and have no production caller | `refereeprofile.cpp` |

---

## 3. A blocker worth naming first

There is **no test for `Referee`**. `referee_profile_test.cpp`,
`match_progression_test.cpp`, `penalty_shootout_test.cpp` and
`substitutions_test.cpp` exist, but offside, advantage, the penalty-area test,
card issuance and the restart state machine are entirely untested — and cannot
be tested, because `Referee`'s constructor loads sound resources and touches
`Scene3D`.

Every fix below is a behaviour change to untested code. The cheapest way to make
all five safe is the same move the codebase already uses everywhere else
(`aitactics.hpp`, `matchprogression.hpp`, `refereeprofile.hpp`,
`penaltyshootout.cpp`): **lift the decisions into free functions over plain
data**, leave `Referee` as the thin part that owns whistles and timers, and test
the free functions headlessly. Do that first, or accept that everything after it
is unverifiable.

---

## 4. The five most impactful gaps

### 1. Offside is flagged from corners, goal kicks and free kicks

**Law 11.** No offside directly from a goal kick, a corner kick, or a throw-in.
GF exempts only throw-ins:

```cpp
// Referee::BallTouched
if (match->IsInPlay() &&
    (buffer.active == false ||
     (buffer.active == true && buffer.desiredSetPiece != e_SetPiece_ThrowIn))) {
  // ... snapshot offside players
```

So every corner is a phantom-offside generator: attackers stand beyond the
second-last defender by design, they are all recorded, and the first one to touch
next is penalised. This is the most visible wrong call in the engine.

**Fix.** Widen the exemption and, while the condition is open, make it read as
the Law rather than as a buffer check:

```cpp
// referee.hpp (or a new offside.hpp, see §3)
bool OffsideAppliesTo(e_SetPiece restart) {
  return restart != e_SetPiece_ThrowIn &&
         restart != e_SetPiece_GoalKick &&
         restart != e_SetPiece_Corner;
}
```

Two further corrections in the same function, both cheap:

- The phase currently resets on *any* touch by *either* team
  (`offsidePlayers.clear()` at the top of every `BallTouched`). Law 11 resets
  only on a defender's **deliberate play**, not on a deflection or a save. Clear
  the map only when the toucher is a teammate of the recorded players, or when a
  defender's touch was deliberate — GF can approximate "deliberate" with the
  toucher's function type (an `Interfere` or a controlled trap is deliberate; a
  collision-driven deflection is not).
- The restart position should be where the offside player became involved, not
  his position at the previous touch. The stored `Vector3` is the wrong one; use
  his position at the moment of his touch.

### 2. Advantage throws the card away

**Law 5.** If the advantage runs, the caution or sending-off is still shown at
the next stoppage. GF discards the whole foul, and says so:

```cpp
// Referee::CheckFoul
if (match->GetActualTime_ms() - RefereeProfile::GetAdvantageWindow_ms(profile) > foul.foulTime) {
  // cancel foul, advantage took long enough
  // todo: yellow cards need to be remembered though ;)
  foul.foulPlayer = 0;
  foul.foulType = 0;
}
```

**Fix.** Keep a small deferred-sanction queue on `Referee`:

```cpp
struct DeferredCard { Player* player; int foulType; unsigned long foulTime; };
std::vector<DeferredCard> deferredCards;
```

At the expiry branch, if `foul.foulType >= 2`, push a `DeferredCard` before
clearing. Then drain the queue at the next natural stoppage — the same places
that already queue a restart in `Referee::Process` — issuing the card without
changing the restart. That is exactly the Law: the *free kick* is waived, the
*sanction* is not. It also removes a curious incentive in the current code, where
fouling a player who keeps possession is free.

The same queue is the right home for the sending-off ceremony timing, which is
currently a magic `+ 6000` with the comment "need to find out proper moment"
attached to both `GiveYellowCard` and `GiveRedCard`.

### 3. Only sliding tackles can be carded, and one whole foul class is dropped

Three defects compound in `Referee::TripNotice`:

- `tackleType == 1` — every `Interfere` tackle and every light body trip — is
  never handled by any branch, so it is silently discarded.
- `tackleType == 2` (a standing tackle that puts a man down) is hardcoded to
  `foul.foulType = 1`. A standing challenge in GF can never be a caution, no
  matter how bad it is.
- Consequently `RefereeProfile`'s whole yellow/red ladder is reachable from
  exactly one animation path.

PES's answer is instructive: it scores *every* contact through the same
three-component pipeline regardless of how the contact arose, and specifically
refuses to treat standing challenges as innocent — a static tackle takes no
contact reduction, carries a minimum velocity score, and a lunge from standing is
penalised *more* than a running challenge.

**Fix.** Give GF one severity function that every producer feeds:

```cpp
// src/onthepitch/foulseverity.hpp   (pure, headless-testable)
namespace FoulSeverity {

struct Contact {
  float contactIntensity = 0.0f;   // from the collision penetration/sensitivity ladder
  float relativeSpeed_ms = 0.0f;   // |tripperVel - tripeeVel|
  float approachAngle_deg = 0.0f;  // 0 = face to face, 180 = from behind
  float ballDistance_m = 0.0f;     // how far the challenge was from the ball
  float timingError = 0.0f;        // existing touch-frame term
  bool  fromStanding = false;      // the tackler was static
  bool  insidePenaltyArea = false;
  bool  deniesGoalScoringOpportunity = false;
};

// PES's own shape: contact + velocity + context, each bounded.
float Score(const Contact& c);

// Location-dependent, PES's own asymmetry: harder to give a penalty than a
// free kick, but once given the card ladder is the same.
struct Thresholds { float foul; float yellow; float red; };
Thresholds GetThresholds(bool insidePenaltyArea, RefereeProfile::e_Profile profile);

int GetFoulType(const Contact& c, RefereeProfile::e_Profile profile);  // 0..3
}
```

Then `TripNotice` becomes a translator from the three tackle types into a
`Contact`, and the collision producer in `match.cpp` calls it too instead of
incrementing a foul statistic on its own authority. `tools/pes21_import/foul_severity.py`
is a runnable reference implementation of the PES pipeline with its real
constants, useful for choosing GF's numbers by comparison.

Fix the statistic while you are there: `matchData->AddFoul` currently fires on
the collision path before the referee has decided anything, and never fires for
sliding tackles, so the displayed foul count describes neither.

### 4. No DOGSO, and therefore no real red card

GF's only red is severity ≥ 2.0 on a slide. There is no notion of denying a
goal — the offence that produces most real sendings-off.

PES's check is small and GF already has both of its inputs:

```cpp
bool IsDOGSO(rosterData, foulingPlayer, fouledPlayer) {
    // ahead of the reference (defensive line) position in the attack direction
    if (attackDir * fouledPos.X > attackDir * referenceX)
        // and within the width of the penalty area
        if (fabs(fouledPos.Z) < penaltyAreaHalfWidth)   // 20.12 m
            return true;
    return false;
}
```

`AI_GetOffsideLine` already gives the reference line, and the penalty-area
half-width is the `20.15` constant already sitting in `Referee::CheckFoul`.

**Fix.** Add `deniesGoalScoringOpportunity` to the `Contact` above, computed
exactly this way, and let it push severity up a rung. Then decide the IFAB 2016
question deliberately rather than by omission: **DOGSO outside the box is a
sending-off; DOGSO inside the box, where the tackler made a genuine attempt to
play the ball, is a caution plus the penalty.** GF can express "genuine attempt"
with the ball-distance term it already computes. Note that PES gets this half
wrong — it caps *every* severity-4 to a yellow, everywhere on the pitch, so a
scything foul on the halfway line can never be a straight red. Do not copy that.

While here, reconsider PES's card-eligibility gate, which only permits cards when
the fouled player is in his attacking half. It is a crude proxy for "the foul
mattered", and GF's severity model can express that better through DOGSO and
promising-attack terms.

### 5. No added time

The clock only advances while `IsInPlay() && !IsInSetPiece()`, so it stops dead
for every restart and there is nothing to add on. `MatchProgression::ShouldEndPeriod`
is often mistaken for stoppage time but is not: it is a window (up to 3 minutes
past the scheduled end) in which the referee looks for a neutral moment to
whistle, measured on the same running clock.

The consequence is that GF has no late-game clock pressure. A team leading 1–0 at
90:00 knows the game ends at the next neutral ball, and `MatchMentality`'s
time-wasting behaviour, which starts at 85 minutes, has nothing to actually gain.

**Fix.** Accumulate an allowance and expose it:

```cpp
// matchprogression.hpp
struct Stoppage {
  unsigned long accrued_ms = 0;
};

// The Law's own list, with PES's own precedent for storing it (a `lossTime`
// byte in the match state alongside the minute).
const unsigned long stoppagePerSubstitution_ms   = 30000;
const unsigned long stoppagePerCard_ms           = 30000;
const unsigned long stoppagePerGoal_ms           = 60000;
const unsigned long stoppagePerInjury_ms         = 30000;

void AddStoppage(Stoppage& s, ...);
unsigned long GetPeriodEndTime_ms(e_MatchPhase phase, const Stoppage& s);
```

`Referee::Process` already has every trigger in hand: it queues the restart for a
goal, it issues the cards, and `Substitutions` reports accepted changes. Feed
`ShouldEndPeriod` the extended end time and keep the existing neutral-band search
on top of it — that combination is *better* than real refereeing practice and
should be kept.

Then surface it. A "+3" indicator at the end of a period is the whole point of
the mechanic; without a display it is invisible, exactly as PES's silent fatigue
degradation is.

---

## 5. Next tier

Ranked, in case the five above land:

1. **Handball** (Law 12) — the largest genuinely absent Law. It needs collision
   work first: GF's tackle test only considers feet and lower legs, and no arm
   geometry participates in ball contact at all. Until arms can touch the ball
   there is nothing to adjudicate.
2. **Keeper laws** — the six-second rule and the back-pass rule. Both are cheap
   once an indirect free kick exists, and both change build-up play more than
   their size suggests.
3. **Indirect free kicks** — an eighth `e_SetPiece` value, or a flag on the
   existing one. Prerequisite for the two above.
4. **Card bookkeeping** — counters in `MatchData`, columns in `MatchHistory`,
   suspensions in career mode. PES writes a full foul event record for exactly
   this reason.
5. **Encroachment and retakes** at free kicks and penalties — GF positions
   players correctly at prepare time and then never looks again.
6. **Wire up VAR** — `ShouldReviewOffside` and `ShouldReviewPenalty` are written
   and tested and called by nothing. Given fix 1 above, a close offside is
   precisely the case worth reviewing.
7. **Dropped ball** — needed for the injury stoppage that Law 5 also implies.
