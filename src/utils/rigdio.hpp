// A native reimplementation of rigdio (github.com/the4chancup/rigdio), the
// /4cc/ match-music player: parsing of .4ccm music exports, the condition
// grammar, and the song-selection state machine, all 1:1 with rigdio v2.2.0.
// The exact semantics, including how malformed input fails, are documented in
// docs/RIGDIO.md and mirrored from rigdio's rigparse.py / condition.py /
// legacy.py / gamestate.py.
//
// Everything here is pure: no engine types, no file system, no audio. The
// engine adapter (src/onthepitch/rigdiodirector.*) feeds it match events and
// plays the files it picks. Unit-tested in tests/utils/rigdio_test.cpp.

#ifndef _HPP_UTILS_RIGDIO
#define _HPP_UTILS_RIGDIO

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace rigdio {

// --- tokenization (condition.py processTokens) ---------------------------

// Splits one `;`-field into tokens: whitespace-separated, with `[...]`
// joining tokens with spaces and a leading `\` escaping a token's first
// character. Returns false on the malformed bracket runs that crash rigdio
// (a single `[x]` token, or an unterminated `[`): a false here fails the
// whole file load, as it does there.
bool ProcessTokens(const std::string& field, std::vector<std::string>& out);

// --- conditions and instructions ------------------------------------------

enum class Cond {
  Goals, TeamGoals, Lead, Opponent, Match, Home, First, Comeback,
  Every, MostGoals, Once, Time, Special, Not,
};

enum class Instr {
  Start, Speed, Randomise, Pause, End, Warcry, Unrandom, Louder, Advance,
  Event,
};

enum class Op { LT, GT, LE, GE, EQ, NE };

struct Condition {
  Cond type = Cond::Home;
  Op op = Op::EQ;          // Goals / TeamGoals / Lead / Time
  double value = 0;        // numeric operand of the above, and Every's n
  // rigdio builds Goals/Every comparisons without validating the operand and
  // only explodes when the entry is checked; the exception aborts the whole
  // pick (nothing plays for that trigger). Faithfully mirrored: a check of
  // this condition yields CheckResult::Crash.
  bool crashesOnCheck = false;
  std::vector<std::string> args;   // Opponent / Match teams, MostGoals player,
                                   // Special label
  std::shared_ptr<Condition> sub;  // Not
  bool spent = false;              // Once: a later check unloads the entry
};

struct Instruction {
  Instr type = Instr::Randomise;
  double startSeconds = 0.0;  // Start
  double speed = 1.0;         // Speed
  bool pauseRestart = false;  // Pause restart (vs continue)
  int pauseEvery = 1;         // Pause restart every n
  bool endStop = false;       // End stop ("loop" is an explicit no-op)
  std::string eventType;      // Event: red / yellow / owngoal / sub
};

// One line of a .4ccm: a file plus the conditions gating it and the
// instructions shaping its playback.
struct Entry {
  std::string pname;  // field 0 as written (post-strip); "goal" for appended
                      // default goalhorns — its `goals` conditions count the
                      // generic goal tally, per rigdio
  std::string file;   // field 1, relative to the export folder
  std::vector<Condition> conditions;
  std::vector<Instruction> instructions;

  // Derived playback traits (ConditionPlayer.__init__ / instruction prep).
  bool loop = true;          // pname not in {victory, chant}, minus end/warcry/advance
  bool warcry = false;
  bool randomise = false;
  bool unrandom = false;
  bool louder = false;
  bool advance = false;
  bool endStop = false;
  bool hasStart = false;
  double startSeconds = 0.0;
  double speed = 1.0;
  bool pauseRestart = false;
  int pauseEvery = 1;
  std::string eventType;     // non-empty routes the entry to the event table

  // Selection state.
  bool firstPlay = true;     // start-seek arming (reset by reload semantics)
  // The entry's own playback position (rigdio: each entry keeps its mpv
  // player for the whole match, so the same entry resumes even without
  // sync); the file-keyed cache in MatchSession is what sync adds.
  double position = 0.0;

  // Stable identity inside a Picker (the vector reshuffles on unload); the
  // advance `skip` is matched on this, as Python matches object identity.
  int uid = -1;
  int pauseCount = 0;        // pause restart every n
};

struct TeamMusic {
  std::string tname;          // lowercased, unstripped, per rigparse
  bool nameFromFile = false;  // no name line: filename stem was used
  bool sync = true;
  bool normalize = true;
  // File-order entry lists per player key; reserved keys ("anthem",
  // "victory", "goal", "chant") included. Non-reserved players already have
  // the goal list appended (rigparse's default-goalhorn fallback).
  std::map<std::string, std::vector<Entry>> players;
  // Entries carrying `event <type>`, keyed by type.
  std::map<std::string, std::vector<Entry>> events;
};

struct ParseResult {
  bool ok = false;
  std::string error;  // first fatal error, at rigdio's failure points
  TeamMusic team;
};

// Parses .4ccm text. `filenameStem` stands in for the team name when the
// file has no name line (rigparse uses the file's basename).
ParseResult Parse(const std::string& text, const std::string& filenameStem);

// --- file resolution (rigparse songCheck) ----------------------------------
//
// `listing` holds the export folder's direct children (names only);
// `exactExists` says whether `name` resolves as given (it may point into a
// subfolder, which the listing scans can never match). Returns the file name
// to use — the original `name` when nothing matched, which the caller will
// find missing, as rigdio does.
std::string SongCheck(const std::string& name, bool exactExists,
                      const std::vector<std::string>& listing,
                      bool applyNormalize);

// --- game state (gamestate.py) ---------------------------------------------

struct GameState {
  int score[2] = {0, 0};                       // [0]=home, [1]=away
  std::map<std::string, int> scorers[2];       // pname -> goals
  std::string names[2];                        // lowercased tnames
  std::string gametype = "group";
  int minute = 0;                              // goal minute (match clock)

  int TeamScore(bool home) const { return score[home ? 0 : 1]; }
  int OpponentScore(bool home) const { return score[home ? 1 : 0]; }
  const std::string& OpponentName(bool home) const { return names[home ? 1 : 0]; }
  const std::map<std::string, int>& TeamScorers(bool home) const {
    return scorers[home ? 0 : 1];
  }
  int PlayerGoals(const std::string& pname, bool home) const;
  // score() — the goal is counted BEFORE the horn is chosen.
  void Score(const std::string& pname, bool home);
};

enum class CheckResult { No, Yes, Unload, Crash };

// One condition against the state, for the entry pname it is bound to.
CheckResult Check(Condition& c, const GameState& gs, const std::string& pname,
                  bool home);

// All of an entry's conditions (AND); Unload wins over No.
CheckResult CheckEntry(Entry& e, const GameState& gs, bool home);

// --- selection (legacy.py PlayerManager.getSong) ---------------------------

using Rng = std::function<int(int)>;  // uniform int in [0, n)

// One player key's entry list with rigdio's selection walk: file-order
// first-match, permanent removal on unload, all-randomise random pick,
// warcry arming, advance skip and its fallbacks.
class Picker {
 public:
  Picker() = default;
  explicit Picker(std::vector<Entry> entries) : entries_(std::move(entries)) {}

  // A trigger. `skip` marks the entry that just ended (advance rerun).
  // nullptr = nothing to play (rigdio's SongNotFound).
  Entry* Pick(const GameState& gs, bool home, const Rng& rng,
              const Entry* skip = nullptr);

  bool warcryArmed = true;
  std::vector<Entry>& entries() { return entries_; }

 private:
  std::vector<Entry> entries_;
};

// --- per-match orchestration ------------------------------------------------
//
// What the streamer's hands do in rigdio: credit the goal, pick the horn,
// pause at kickoff, resume on the next goal (the sync position cache, keyed
// by file), play anthems and the victory anthem, fire chants.

struct PlayAction {
  std::string file;          // relative to the export folder
  double seekSeconds = 0.0;  // resume position or start instruction
  double speed = 1.0;
  bool loop = true;
  bool isWarcry = false;
  bool advance = false;
  bool endStop = false;
  bool home = true;
  std::string pname;
  bool louder = false;   // normalization boost marker
};

class MatchSession {
 public:
  MatchSession(TeamMusic homeTeam, TeamMusic awayTeam,
               const std::string& gametype, Rng rng);

  GameState& State() { return gs_; }
  const TeamMusic& Team(bool home) const { return home ? home_ : away_; }

  // A goal for `scorer` (engine player name, matched case-insensitively
  // against .4ccm player names; no match = the generic "goal" list and
  // tally). `minute` is the match minute; `now` is the engine clock in
  // seconds, for the position cache. Own goals must not come here.
  std::optional<PlayAction> OnGoal(bool home, const std::string& scorer,
                                   int minute, double now);
  // The horn faded out at kickoff.
  void OnHornPaused(bool home, double now);
  // The horn hit EOF naturally: warcry chain, advance, or end stop.
  std::optional<PlayAction> OnHornEnded(bool home, double now);

  std::optional<PlayAction> Anthem(bool home, double now);
  std::optional<PlayAction> Victory(bool home, double now);

  // Manual chant fire; denied (nullopt) while one is playing.
  std::optional<PlayAction> Chant(bool home);
  void ChantEnded();
  bool ChantActive() const { return chantActive_; }

  // Event clip (red / yellow / owngoal / sub) for a player, once per game
  // minute per type, random among his clips. (event.py checkAndPlay)
  std::optional<PlayAction> OnEvent(bool home, const std::string& etype,
                                    const std::string& player, int minute);

  // The engine learned a file's duration (seconds); resume positions wrap.
  void SetDuration(bool home, const std::string& file, double seconds);
  double CachedPosition(bool home, const std::string& file) const;

 private:
  struct Side;
  Side& SideFor(bool home);
  std::optional<PlayAction> Play(bool home, Picker& picker, Entry* e,
                                 double now, bool isGoalhorn);

  TeamMusic home_, away_;
  GameState gs_;
  Rng rng_;

  struct Side {
    std::map<std::string, Picker> pickers;  // per player key incl. reserved
    Picker* playing = nullptr;              // picker owning the live horn
    int playingUid = -1;                    // entry uid within it
    bool playingIsGoalhorn = false;
    double playStarted = 0.0;
    double playSeek = 0.0;
    double playSpeed = 1.0;
    std::string playFile;
    std::vector<int> chantPlayCounts;             // decay-weighted random
    std::map<std::string, int> lastEventMinute;   // per event type
  };
  Side sides_[2];

  bool sync_[2] = {true, true};
  bool chantActive_ = false;
  std::map<std::string, double> positionCache_;  // file -> seconds
  std::map<std::string, double> durations_;
};

}  // namespace rigdio

#endif
