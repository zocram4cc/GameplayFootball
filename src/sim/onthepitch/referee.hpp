// written by bastiaan konings schuiling 2008 - 2015
// this work is public domain. the code is undocumented, scruffy, untested, and should generally not
// be used for anything important. i do not offer support, so don't ask. to be used for inspiration
// :)

#ifndef _HPP_REFEREE
#define _HPP_REFEREE

#include <optional>

#include "../gamedefines.hpp"
#include "cardbook.hpp"
#include "pesjudge.hpp"
#include "pesstoppage.hpp"

using namespace blunted;

class Match;
class Team;

struct RefereeBuffer {
  bool active;
  e_SetPiece desiredSetPiece;
  signed int teamID;
  unsigned long stopTime;
  // When the players are released from the hold PES makes them act out
  // (docs/re/pes-re-foul-choreography.md 9.3): stopTime + the hold, 2.5 s
  // out-of-play, 5.0 s a foul, +2.5 s either way while a player is down.
  unsigned long holdTime;
  unsigned long prepareTime;
  unsigned long startTime;
  Vector3 restartPos;
  Player* taker;
  bool endPhase;
  // Each stage of a restart fires once, when its moment has passed rather than on the
  // exact tick it falls on. The comparisons here were `==` against the match clock,
  // which only holds while that clock advances in single steps: under a time scale, or
  // any frame long enough to skip a tick, the whistle was never blown and the restart
  // was simply lost - a stoppage that never restarted. Measured: a full match under a
  // 10x scale produced three kick-offs and not one other set piece.
  bool prepared;
  bool started;
  // Where the chief referee stands for this restart (pes::ChiefRefereeSpot), held
  // until it is taken. Empty when PES leaves him where he is.
  std::optional<Vector3> chiefSpot;
};

struct Foul {
  Player* foulPlayer;
  Player* foulVictim;
  int foulType;  // 0: nothing, 1: foul, 2: yellow, 3: red
  bool advantage;
  // PES's advantage (docs/re/pes-re-duels-fouls.md 3): decided once, in the
  // last third of the whistle hold, then a clock that runs out or is called back.
  bool advantageDecided = false;
  float advantageRemaining_s = 0.0f;
  Player* advantageLastToucher = nullptr;
  unsigned long foulTime;
  Vector3 foulPosition;
  bool hasBeenProcessed;
};

// A player PES 2021 has flagged offside (docs/re/pes-re-offside.md 4): where he
// stood when his team last played the ball, and PES's category of the call -
// 0 beyond by more than 4 m, 1 beyond by 1-4 m, 2 beyond by less than a metre.
// Category 3 (onside, near level) is never punishable and never stored here.
struct OffsideFlag {
  Vector3 positionAtTouch;
  int category;
};

class Referee {
public:
  Referee(Match* match);
  virtual ~Referee();

  void Process();

  void PrepareSetPiece(e_SetPiece setPiece);

  const RefereeBuffer& GetBuffer() const { return buffer; };

  // The schedule PES runs after a whistle, for the callers that arm a restart
  // themselves (the dead-ball arm in Process and the late-card window).
  RefereeBuffer& Buffer() { return buffer; }
  const pes::StoppageConstants& GetStoppageConstants() const;

  void AlterSetPiecePrepareTime(unsigned long newTime_ms);

  void BallTouched();
  void TripNotice(Player* tripee, Player* tripper,
                  int tackleType);  // 1 == standing tackle resulting in little trip, 2 == standing
                                    // tackle resulting in fall, 3 == sliding tackle
  // `heldWhistle`: the per-tick call in open play, which holds the whistle and
  // runs the advantage; the out-of-play paths whistle a pending foul at once.
  bool CheckFoul(bool heldWhistle = false);

  // Drops any queued restart. Used when the shootout takes over the pitch, so
  // no kick-off or goal kick is staged underneath it.
  void CancelSetPiece();

  Player* GetCurrentFoulPlayer() { return foul.foulPlayer; }
  int GetCurrentFoulType() { return foul.foulType; }

protected:
  Match* match;

  RefereeBuffer buffer;
  // Whether the "goal kick held" notice has already been logged for this restart.
  bool clearingLogged = false;
  // When the current restart was first held for a player in the way, so the release
  // can say how long the taker waited and how far the man had gone.
  unsigned long clearingHeldFrom_ms = 0;
  float clearingNearest_m = 0.0f;
  std::string clearingRule;
  bool clearingAnnounced = false;

  int afterSetPieceRelaxTime_ms;  // throw-ins cause immediate new throw-ins, because ball is still
                                  // outside the lines at the moment of throwing ;)

  // The players currently flagged offside, with PES's category of each call.
  std::map<Player*, OffsideFlag> offsidePlayers;  // player, position at time of touch

  // PES 2021's pending offside (docs/re/pes-re-duels-fouls.md 5.1): a flagged
  // player who plays the ball sets a pending foul whose whistle waits out the
  // offside whistle hold (gs/2 frames), and a category-2 flag additionally
  // waits for a shot in progress to finish (DelayWhistle). A null player means
  // nothing pending.
  struct PendingOffside {
    Player* player = nullptr;
    Vector3 spot;  // where he became involved: the restart spot (Law 11)
    int category = 0;
    unsigned long since_ms = 0;
  } pendingOffside;

  Foul foul;

  CardBook::Book cardBook;

  // Law 5: cards owed from advantage-eaten fouls are shown at the next
  // stoppage.
  void IssueDeferredCards();

  // Law 12 DOGSO: escalates a foul on a player who was through on goal.
  // PES 2021's foul decision for one contact: this engine's foul type 0..3.
  int JudgeContact(Player* tripee, Player* tripper, pes::JudgeAction action);
  void RecordFoul(Player* tripee, Player* tripper, int foulType);
  float NearestToBall_m(Team* team) const;

  // PES 2021's offside whistle (docs/re/pes-re-duels-fouls.md 5.1): after the
  // gs/2 whistle hold from the involvement, and for a category-2 flag
  // (DelayWhistle) only once no shot is in progress.
  void CommitPendingOffside();

  // Which period's added time has been announced already.
  e_MatchPhase addedTimeAnnouncedPhase;
  // When the match was first found with play off, nothing armed and the ball
  // out of play. Zero means "not dead"; otherwise it is the real time that was.
  unsigned long deadBallSince_ms = 0;
  // Whether the deferral cap has already been reported, so a long hold says so
  // once rather than every tick.
  bool deferralRefusedLogged = false;
};

#endif
