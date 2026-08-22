// written by bastiaan konings schuiling 2008 - 2015
// this work is public domain. the code is undocumented, scruffy, untested, and should generally not
// be used for anything important. i do not offer support, so don't ask. to be used for inspiration
// :)

#ifndef _HPP_REFEREE
#define _HPP_REFEREE

#include "../gamedefines.hpp"
#include "cardbook.hpp"
#include "defines.hpp"
#include "refereeprofile.hpp"
#include "scene/objects/sound.hpp"
#include "scene/scene3d/scene3d.hpp"

using namespace blunted;

class Match;

struct RefereeBuffer {
  bool active;
  e_SetPiece desiredSetPiece;
  signed int teamID;
  unsigned long stopTime;
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
};

struct Foul {
  Player* foulPlayer;
  Player* foulVictim;
  int foulType;  // 0: nothing, 1: foul, 2: yellow, 3: red
  bool advantage;
  unsigned long foulTime;
  Vector3 foulPosition;
  bool hasBeenProcessed;
};

class Referee {
public:
  Referee(Match* match);
  virtual ~Referee();

  void Process();

  void PrepareSetPiece(e_SetPiece setPiece);

  const RefereeBuffer& GetBuffer() const { return buffer; };

  void AlterSetPiecePrepareTime(unsigned long newTime_ms);

  void BallTouched();
  void TripNotice(Player* tripee, Player* tripper,
                  int tackleType);  // 1 == standing tackle resulting in little trip, 2 == standing
                                    // tackle resulting in fall, 3 == sliding tackle
  bool CheckFoul();

  // Drops any queued restart. Used when the shootout takes over the pitch, so
  // no kick-off or goal kick is staged underneath it.
  void CancelSetPiece();

  Player* GetCurrentFoulPlayer() { return foul.foulPlayer; }
  int GetCurrentFoulType() { return foul.foulType; }

protected:
  Match* match;

  std::shared_ptr<Scene3D> scene3D;

  RefereeBuffer buffer;
  // Whether the "goal kick held" notice has already been logged for this restart.
  bool clearingLogged = false;
  // When the current restart was first held for a player in the way, so the release
  // can say how long the taker waited and how far the man had gone.
  unsigned long clearingHeldFrom_ms = 0;
  float clearingNearest_m = 0.0f;
  std::string clearingRule;

  int afterSetPieceRelaxTime_ms;  // throw-ins cause immediate new throw-ins, because ball is still
                                  // outside the lines at the moment of throwing ;)

  std::map<Player*, Vector3> offsidePlayers;  // player, position at time of touch

  Foul foul;

  CardBook::Book cardBook;

  // Law 5: cards owed from advantage-eaten fouls are shown at the next
  // stoppage.
  void IssueDeferredCards();

  // Law 12 DOGSO: escalates a foul on a player who was through on goal.
  int ApplyGoalDenial(int foulType, Player* tripee, Player* tripper, float ballDistance_m);

  RefereeProfile::e_Profile profile;

  // Which period's added time has been announced already.
  e_MatchPhase addedTimeAnnouncedPhase;

  boost::intrusive_ptr<Sound> whistle[4];  // 0: short, 1: long, 2: half time, 3: full time
};

#endif
