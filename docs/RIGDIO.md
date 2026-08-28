# Rigdio: the .4ccm export format and its exact semantics

This documents rigdio v2.2.0 (github.com/the4chancup/rigdio, commit ad604a1) as
implemented — not as its README describes it. The engine's native
reimplementation (`src/utils/rigdio.*`, `src/onthepitch/rigdiodirector.*`)
follows this file; every deliberate divergence is listed at the end.

rigdio is the /4cc/ match-music player. A team ships an "audio export": a
folder of audio files plus one `.4ccm` control file. rigdio loads one export
per side (home/away) and a human streamer presses buttons: anthem before the
match, a player's button when he scores, victory at full time, chants at will.
The `.4ccm` decides *which file* each button plays, via conditions evaluated
against the current game state.

Source of truth per topic:

| Topic | rigdio source |
|---|---|
| File parsing | `rigparse.py` (`parse`, `songCheck`, `reserved`) |
| Token quoting | `condition.py` (`processTokens`) |
| Conditions / instructions | `condition.py` |
| Game state | `gamestate.py` |
| Selection & playback | `legacy.py` (`ConditionPlayer`, `PlayerManager`) |
| Button semantics | `songgui.py`, `rigdio.py` |
| Chants | `chantswindow.py` |
| Events (cards/subs/own goals) | `event.py` |
| Defaults | `config.py` |

## 1. File structure

A `.4ccm` is a line-oriented text file. Every line is `strip()`ped first.
Processing order:

1. **Leading noise**: blank lines and lines whose first character is `#` are
   skipped *before the name line only* (mid-file, they are skipped by the
   entry loop too). A file of nothing but blanks/comments crashes rigdio
   (IndexError) — i.e. the load fails.
2. **Name line**: the first surviving line, split on `;`. It is the name line
   iff field 0 is exactly `name` (case-sensitive, and *not* re-stripped — a
   line that was `name ;x` has field 0 `name ` and does **not** match). Then
   `tname = field1.lower()` — **not stripped**, so `name; dbg` yields
   `" dbg"` with the space. If the first line is not a name line, `tname`
   defaults to the .4ccm filename stem and the line is *not consumed* (it is
   parsed as an ordinary entry).
3. **Flag lines**: zero or more lines directly after the name line whose
   first `;`-field, stripped and lowercased, is `sync` or `normalize`, in any
   order, repeatable (last wins). Value = field 1, stripped + lowercased;
   the flag is **enabled unless** the value is one of `no`, `off`, `false`,
   `0` (a missing value means enabled). Both default to enabled.
   - `sync`: goalhorns resume where they left off (section 6).
   - `normalize`: loudness normalization opt-out (section 7).
   A `sync;...`/`normalize;...` line anywhere *later* in the file is parsed
   as an entry whose player name is reserved but has no default-filename
   mapping → KeyError → **whole load fails** (only if it has no `;`, see 4).
4. **Entries**: every remaining non-blank line not starting with `#`, split
   on `;`, every field `strip()`ped:

   ```
   <player>;<filename>;<condition or instruction>;<condition or instruction>;...
   ```

   - Field 0 is the player name. The **reserved** names are `anthem`,
     `victory`, `goal`, `chant` (also `name`, `;event`, `sync`, `normalize`,
     which cannot occur usefully). Reserved matching is **case-sensitive**:
     `Anthem;x.mp3` is a *player* called "Anthem" with a goalhorn.
   - If the line has **no `;` at all**, a default filename is assumed:
     `"<tname> - <Fancy>.mp3"` where Fancy is `Goalhorn` / `Anthem` /
     `Victory Anthem` / `Chant` for the reserved names, and
     `"<tname> - <player> Goalhorn.mp3"` for players. A bare `name`, `sync`,
     `normalize` or `;event` line here → KeyError → load fails.
   - Fields 2..n are each tokenized (section 2) and built into one condition
     or instruction each (sections 3/4). An **empty** field (trailing `;`,
     or `a;;b`) builds `None` and crashes rigdio (AttributeError) → load
     fails. An unrecognized first token → ValueError → load fails.
   - Entries whose instruction list contains `event <type>` are routed into
     a separate **events** table keyed by type (section 8), not the player
     table. Everything else appends, in file order, to the player-name key.

5. **Default-goalhorn fallback**: after parsing, the entry list of *every
   non-reserved player* is extended with the whole `goal` list (the entries
   keep their own pname `goal` — this matters for `goals` counting, section
   5). If any non-reserved player exists and there is **no** `goal` entry at
   all, rigdio dies with KeyError → load fails.

Line ordering within one player key is the priority order for selection
(section 6). Duplicate reserved keys simply append (e.g. five `anthem` lines
= a five-entry anthem list).

## 2. Token quoting (`processTokens`)

Each condition field is split on whitespace, then:

- A token starting with `\` has that one escape character removed.
- A token starting with `[` opens a quoted run: following tokens are joined
  with single spaces until a token *ending* with `]` (whose second-to-last
  char is not `\`). The `[` and `]` are removed; a trailing `\]` keeps its
  `]` and drops the `\`. E.g. `special [Gogeta Da MVP]` → tokens
  `special`, `Gogeta Da MVP`.
- A **single** token `[x]`, or an unterminated `[`, crashes rigdio
  (IndexError) → load fails.

## 3. Conditions

An entry plays only if **all** of its conditions pass (logical AND). Checking
happens **after** the goal has been counted (section 5). `home` below means
"this .4ccm was loaded in the home slot".

Comparison operators where applicable: `<ge> ::= < > <= >= == !=`; a bare `=`
is rewritten to `==`. Anything else → load fails.

| Syntax | Passes when |
|---|---|
| `goals <op> <n>` | this entry's *pname* has scored `<op> n` goals (including the one just scored). For default-goal entries appended to a player, pname is `goal`, i.e. it counts goals credited to the generic goal button, **not** the current scorer. |
| `teamgoals <op> <n>` | team's total score `<op> n`. |
| `lead <op> <n>` | (team score − opponent score) `<op> n`. |
| `opponent <team> [<team>...]` | opponent team name is in the list, **case-sensitively** — team names are lowercased at load, so in practice tokens must be lowercase; bracket-quoted tokens containing spaces are split again on spaces. |
| `match <type> [<type>...]` | game type (lowercased) is in the lowercased list. The single token `knockouts` (any case) expands to `ro16 quarterfinal semifinal final third-place`. Known types: Group, Survival, RO16, Quarterfinal, Semifinal, Final, Third-Place, Boss, Consolation (free text allowed). |
| `home` | team is the home team. (`not home` = away.) |
| `first` | team score == 1 (i.e. this goal was the team's first). |
| `comeback` | team score <= opponent score AND opponent score > 0, evaluated post-goal. |
| `every <n>` | pname's goal count % n == 0. Non-numeric or zero `n` only explodes at check time (entry effectively never plays; rest keeps working). |
| `mostgoals [player]` | pname (or the bracket-quoted named player) has at least as many goals as every scorer on the team. |
| `once` | passes the first time it is *checked*; every later check unloads the entry permanently (section 6). |
| `time <op> <minute>` | goal minute `<op> minute` (integers). If the op is one of `<`, `<=`, `==` and the current minute is already past, the entry is unloaded permanently. Non-integer minute → load fails. In rigdio the minute comes from a prompt; the engine uses the match clock. |
| `special [label]` | always false. Marks a victory-anthem entry as a manually selectable "special VA" (MVP anthems); rigdio lists it in a dropdown and only plays it when the streamer picks it. |
| `not <condition...>` | negates the single condition built from the remaining tokens (comma-splitting exists in the code but only the first subcondition is checked). |

`or` / `and` / `if` meta-conditions exist in the code but are **commented out
of the registry** — using them in a .4ccm fails the load.

`goals`/`teamgoals`/`lead` with a non-numeric operand parse fine but explode
at check time (Python `eval`), which in rigdio's UI means that entry
effectively never plays while everything else keeps working.

## 4. Instructions

Instructions ride on the same `;`-separated fields and modify playback rather
than gate it:

| Syntax | Effect |
|---|---|
| `start <t>` | first play (and each restart) seeks to `t`, given as `[[dd:]hh:]mm:ss` with fractional seconds allowed (`0:30.5`). Unparseable `t` → load fails (TypeError via None). |
| `speed <x>` | playback speed multiplier (mpv range 0.25–4.0). |
| `randomise` | if **all** of a player's entries carry it, every trigger picks uniformly at random from all of them, **ignoring conditions** (they are still checked first, so `once`/`time` unloads still happen). If only some entries carry it, it does nothing. On chants it marks the chant for the random pool. |
| `pause continue` | (default) pausing just pauses. |
| `pause restart [every <n>]` | every n-th pause (default every) seeks back to the start time. NOTE: with `sync` on (the default), the position cache is saved *before* the seek and restored on the next play, so `pause restart` is only observable when `sync;no` — faithful to rigdio's code. |
| `end stop` | at natural end-of-file, stop instead of looping, reset to the beginning (start-time seek runs again next play; the sync position cache entry is cleared). |
| `end loop` | explicit no-op (looping is the default). |
| `warcry` | entry does not loop; when it *ends naturally*, the first valid non-warcry entry of the same player starts immediately. The warcry plays again on the player's next trigger. |
| `unrandom` | chants only: excluded from the random-chant pool (still manually playable in rigdio). |
| `louder` | marks track for extra volume boost when normalization is on (rigdio adds a per-team boost slider, default +5 dB). |
| `advance` | entry does not loop; when it ends naturally, selection reruns with this entry skipped (fallback: first non-warcry entry, then the skipped entry itself). |
| `event <red\|yellow\|owngoal\|sub>` | routes the entry to the event table (section 8). Repeat off. Unknown type → load fails. |

Default looping: an entry loops iff its pname is not `victory` or `chant`
(so anthems and all goalhorns loop; a *player named* "victory" would not).

## 5. Game state and the goal flow

rigdio's `GameState` holds per-side: score, `{player name: goals}` scorer
tally, team name (lowercased tname), plus a game type and the prompted goal
minute. **The goal is scored first, then the horn is chosen**: pressing a
player button increments both team score and that player's tally *before*
conditions are evaluated. So after the team's first goal, `first` passes and
`goals == 1` passes for the scorer.

Goals scored by a player without his own button are credited to the pname
`goal` via the generic goal button — which is exactly what the appended
default-goal entries count with their `goals` conditions.

## 6. Selection and cross-goal persistence

Per player key, rigdio keeps the parsed entry list in file order and picks
(`PlayerManager.getSong`):

1. If warcry mode is armed (it is at match start and again after every
   non-warcry pick) and **all** the player's warcry entries carry
   `randomise`, pick one of the warcry entries at random (conditions
   ignored).
2. Walk the list in order. For each entry: check its conditions. A check may
   throw "unload" (`once` re-check, expired `time`) → the entry is removed
   from the list *permanently for this match* and the walk continues.
3. If the checked entry has `randomise` (and is not a warcry): if **all**
   non-warcry entries of this player have `randomise`, return a uniformly
   random non-warcry entry of this player (regardless of conditions).
4. If the entry's conditions all passed: in warcry mode return it (warcry or
   not); once a warcry has been consumed, return the first *non-warcry*
   passing entry.
5. If nothing matched and this rerun was caused by `advance`, fall back to
   the first non-warcry entry that isn't the one that just ended, then to
   the ended entry itself. Otherwise: **no song** (rigdio shows "Song for X
   not found"; nothing plays; the goal stays counted).

Playback state across goals (`sync`, default on):

- Every **goalhorn** (not anthem/victory/chant, not warcries) saves its
  playback position, keyed by *absolute file path*, whenever it is paused or
  faded out, and restores it on the next play. This is the "resume where it
  left off" behaviour: a horn paused at 0:52 for kickoff continues at 0:52
  on the next goal — even if the next goal picks a *different entry that
  plays the same file* (the cache is per file, not per entry).
- `start <t>` seeks only on the entry's first play after (re)load; a resumed
  play restores the cached position instead.
- `end stop` reaching EOF, or rigdio's reset button, clears the file's cache
  entry and re-arms the first-play seek.
- With `sync;no`, nothing is cached and every play starts from the beginning
  (or the `start` time).

Anthems and victory anthems: same selection walk, no position cache. The
away anthem is played first; starting the home anthem pauses (fades) the
away one — the away-then-home order is rigdio streamer convention, encoded
in its "away button hook". Victory anthems: `special` entries are false, so
the file-order walk picks the first passing non-special entry; warcry
victory entries play first and chain into the anthem (hdg ships exactly
this). Fade-out on pause takes 2 seconds (all four types, `config.py`).

## 7. Locating and preparing audio files

The `<filename>` field is relative to the .4ccm's folder. Resolution
(`songCheck`), where "scan" = case-insensitive comparison against the
folder's *direct* children:

1. If normalization will NOT be applied (global switch off, or team said
   `normalize;no`): scan for a file whose stem equals `<stem>_normalized`
   (any extension); if found, use it.
2. Exact path; else scan for the full filename case-insensitively (this is
   the fix for case-sensitive filesystems); else scan for
   `<stem>_normalized`; else the file is missing.
3. Missing files are collected and the **whole team load is rejected** with
   the list (FileNotFoundError dialog).

Normalization (global default ON, target −14 dBFS mean RMS): rigdio measures
each track with ffmpeg `volumedetect` and applies `target − mean` dB of gain,
with a limiter when that would push the peak above 0 dBFS; chants use the RMS
of their loudest 20% of one-second windows instead of the whole-track mean;
`louder` tracks get the boost slider's dB added.

## 8. Events (cards, substitutions, own goals)

Entries carrying `event red|yellow|owngoal|sub` are grouped by type and by
UPPERCASED player name. When the event happens for that player at game minute
m, and m is later than the last minute this event type fired, one of the
player's clips for that type is played at random, once, no loop.

## 9. Chants

`chant` entries form a per-team pool, fired manually and one at a time
(a fire request while any chant is playing is denied). Random firing picks
with exponential-decay weighting: weight = 0.3^(times already picked this
load), so repeats become increasingly rare; `unrandom` chants are excluded
from the pool. A chant always starts from the beginning (its player is
reloaded first). A chant timer (default on, 30 s, adjustable 20–60 s) fades
a chant out when it runs long; a "stop chant early" control fades it out
immediately. Chants never loop.

## 10. The engine mapping

What the streamer does by hand in rigdio, the engine does off match events:

| rigdio | engine (`RigdioDirector`) |
|---|---|
| Load home/away export | `music/<tag>/**.4ccm` at match start; `<tag>` = lowercased alphanumerics of the team name (`/hdg/` → `hdg`, `2HUG` → `2hug` — `install_team.py art_tag()`), team 0 = home. `<tag>.4ccm` preferred, else the lexicographically last `.4ccm`. |
| Away anthem button, then home | Entrance presentation: away anthem at walkout start, faded into the home anthem at half the entrance. |
| Player goal button | On goal: scorer's last name matched case-insensitively against .4ccm player names; no entry → the generic `goal` tally/list, exactly like the streamer pressing "goal". Own goals never play goal music (rigdio streamers don't press the scorer's button; the `owngoal` event fires instead). |
| Pause at kickoff | Horn fades out 2 s when the goal sequence ends; position cached per file (`sync`). |
| Victory button at full time | `Match::GameOver()`: winning team's victory list (draw → nothing). |
| Chant buttons | Coach mode: `Z` fires a random home chant, `X` a random away chant; same key again = stop early. 30 s timer, 0.3 decay, `unrandom` honoured. |
| Game type dropdown | `rigdio_gametype` config (default `group`). |
| Goal-minute prompt (`time`) | Match scoreboard minute, `matchTime_ms / 60000`. |

Config keys: `rigdio_dir` (default `music`), `rigdio_enabled` (default on),
`rigdio_normalize` (default on, = rigdio's `normalize_volume`),
`rigdio_gametype`.

## 11. Deliberate divergences from rigdio

Parsing and selection are 1:1, including load-failure conditions (anything
that raises during rigdio's `parse()` is a fatal parse error here, with the
same trigger). The divergences, all playback/operational:

1. **Missing files don't reject the team.** rigdio refuses the whole export
   and a human fixes it; unattended, the engine logs the same list
   (`rigdio: missing files for /<tag>/: ...`) and plays what exists. (The
   live dbg VGL26 export references three nonexistent files and would not
   load in rigdio at all.)
2. **Undecodable formats.** rigdio plays anything mpv can (e.g. an `.mp4`
   chant). The engine decodes wav/ogg/mp3; anything else is logged and
   skipped like a missing file. No manual special-VA/dropdown UI: `special`
   entries are simply never picked, as in rigdio's default behaviour.
3. **Normalization limiter.** rigdio runs mpv's `alimiter` when gain would
   clip; the engine instead caps the gain so the peak stays at 0 dBFS
   (peaky tracks end up slightly quieter than rigdio would play them).
   Loud-part chant analysis uses the same top-20%-windows RMS.
4. (Not a divergence, but easy to get wrong:) **conditions that crash
   rigdio's check** — non-numeric operands, `every 0` — are mirrored
   faithfully: when the selection walk reaches such an entry, the whole
   pick aborts and nothing plays for that trigger, exactly like rigdio's
   uncaught exception in the button callback. The entry list is unchanged.
5. **Undo, playback-speed slider, manual per-chant buttons, title.log,
   dark mode** — streamer-UI affordances with no engine equivalent.
