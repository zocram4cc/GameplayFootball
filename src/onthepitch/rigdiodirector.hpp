// The rigdio streamer's hands, native: loads each team's .4ccm music export
// (docs/RIGDIO.md), and plays the right track at the right moment of the
// match - anthems during the entrance (away first, then home), the scorer's
// goal music under the full condition grammar with cross-goal resume, the
// winner's victory anthem at full time, and coach-mode manual chants
// (Z = home, X = away; the same key stops a chant early).
//
// All rigdio semantics live in the pure module (src/utils/rigdio.*); this
// class only watches the Match and drives Sound objects through the same
// OpenAL path as the team chant loops.

#ifndef _HPP_ONTHEPITCH_RIGDIODIRECTOR
#define _HPP_ONTHEPITCH_RIGDIODIRECTOR

#include <map>
#include <memory>
#include <string>

#include "scene/objects/sound.hpp"
#include "utils/rigdio.hpp"

class Match;
class Player;


class RigdioDirector {
 public:
  explicit RigdioDirector(Match* match);
  ~RigdioDirector();

  // Once per Match::Process: entrance/goal/kickoff/chant state machine.
  void Update();
  // From Match::GameOver: the winning team's victory anthem (draw: nothing).
  void OnFullTime();
  // Before the scene goes down.
  void Exit();

  bool IsActive() const { return session_ != nullptr; }

 private:
  // A loaded file: one Sound per file, shared by every entry that names it.
  struct Track {
    boost::intrusive_ptr<blunted::Sound> sound;
    double durationSeconds = 0.0;
    bool usable = false;
  };
  // One playing slot (per-side horn, per-side anthem, victory, chant, event).
  struct Channel {
    Track* track = nullptr;
    rigdio::PlayAction action;
    bool active = false;
    unsigned long startedWall_ms = 0;
    float gain = 0.0f;         // current, fades toward target
    float target = 0.0f;
    bool pauseWhenSilent = false;  // horn kickoff fade -> Pause() at zero
    bool chant = false;            // notifies ChantEnded at EOF/stop
  };

  Track* Load(bool home, const std::string& file, bool louder);
  void Start(Channel& ch, bool home, const rigdio::PlayAction& act);
  void FadeOut(Channel& ch);
  void Advance(Channel& ch, float dt);   // fade step + EOF handling
  bool AtEnd(const Channel& ch, unsigned long now_ms) const;
  double NowSeconds() const;

  Match* match_;
  std::unique_ptr<rigdio::MatchSession> session_;
  std::string exportDir_[2];  // per side, with trailing '/'
  bool normalize_ = true;
  float volume_ = 0.8f;

  std::map<std::string, Track> tracks_;  // key: full path
  Channel anthem_[2];
  Channel horn_[2];
  Channel victory_;
  Channel chantCh_;
  Channel event_;

  enum class EntrancePhase { Idle, Away, AwayFading, Home, Done };
  EntrancePhase entrance_ = EntrancePhase::Idle;
  int seenGoals_ = 0;
  bool victoryPlayed_ = false;
  unsigned long lastWall_ms = 0;
  // Cards and substitutions become rigdio events (event.py): polled off the
  // match state each Update, so no referee or team code needs a hook.
  std::map<Player*, int> seenCards_;
  std::map<Player*, bool> knownPlayers_;
  bool rosterSeeded_ = false;
};

#endif
