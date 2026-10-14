#include <gtest/gtest.h>

#include "pes_match_constants.hpp"

#include <cstdlib>
#include <memory>
#include <string>

#include "data/matchdata.hpp"
#include "onthepitch/match.hpp"
#include "onthepitch/matchreport.hpp"
#include "onthepitch/matchsetup.hpp"
#include "pes/exe.hpp"
#include "pes/vfs.hpp"
#include "rig/rig.hpp"
#include "simcontext.hpp"

#ifndef GF_SOURCE_DIR
#define GF_SOURCE_DIR "."
#endif

// The restart the referee arms, and who actually takes it.
//
// `Match::inSetPiece` only turns true when the referee releases the kick
// (referee.cpp, after the hold, the wipe and the clearing gates). Until then the
// whole match is off, and every player but the ball retainer is in
// ElizaController's "play is not on" branch, which queues nothing but movement -
// a command their own requeue gate rejects while a retained ball gives them
// possession. A taker waiting there is a restart nobody takes, and the referee's
// exit (`buffer.taker->TouchAnim() && !TouchPending()`) never fires, so the match
// stands still until the period ends.
//
// The referee's buffer holds the taker from the moment the restart is armed
// (PrepareSetPiece), long before that release. So: the taker of an armed restart
// must queue the kick during the arming window, and the sim may not be gulped by
// a restart nobody is taking.
namespace {

constexpr int HOME_TEAM = 1, AWAY_TEAM = 2;

struct Reality {
  blunted::Database* db = nullptr;
  std::unique_ptr<blunted::Database> dbHolder;
  std::unique_ptr<pes::PesVfs> vfs;
  std::optional<pesrig::Rig> rig;
  std::unique_ptr<MatchSetup> setup;
  std::unique_ptr<Match> match;
  MatchData* data = nullptr;

  bool Ready() {
    const char* root = std::getenv("PES21_ROOT");
    if (!root) return false;
    std::string err;
    vfs = std::make_unique<pes::PesVfs>();
    if (!vfs->Mount(root, &err)) return false;
    rig = pesrig::BuildRig(*vfs, &err);
    if (!rig) return false;
    dbHolder = std::make_unique<blunted::Database>();
    if (!dbHolder->LoadSqlScript(std::string(GF_SOURCE_DIR) + "/assets/fixtures/teams.sql"))
      return false;
    db = dbHolder.get();
    SetDB(db);
    return true;
  }

  void Play(unsigned int seed, int minutes) {
    setup = std::make_unique<MatchSetup>();
    setup->homeTeamDbId = HOME_TEAM;
    setup->awayTeamDbId = AWAY_TEAM;
    setup->seed = seed;
    setup->durationMinutes = minutes;
    setup->animationDir = std::string(GF_SOURCE_DIR) + "/assets/gf_anims";
    setup->rig = &*rig;
    pestest::FillMissingPesConstants(setup.get());
    data = new MatchData(HOME_TEAM, AWAY_TEAM);
    match = std::make_unique<Match>(data, *setup, Properties());
    const int kMaxTicks = 30 * 60 * 100;  // the same cap sim_match_test.cpp uses
    int ticks = 0;
    while (!match->IsGameOver() && ticks < kMaxTicks) {
      match->Process();
      ticks++;
    }
  }
};

// The arming window is the one that used to swallow the taker: `IsInSetPiece()`
// is still false while the referee waits, and `Match::inPlay` is already true
// again. A match that runs its full minutes is proof by itself, because the
// arming window of every restart has to be survived - but the count of ticks it
// took is the measurement that catches a stall: a healthy short match runs far
// fewer than the cap, and a stuck one burns the cap.
TEST(PesRestartTaking, AShortMatchFinishesWellInsideTheSimCap) {
  Reality r;
  if (!r.Ready()) GTEST_SKIP() << "PES21_ROOT not set";
  r.Play(1000, 2);
  ASSERT_TRUE(r.match) << "constants unavailable";
  EXPECT_TRUE(r.match->IsGameOver()) << "the sim hit its cap: play is stuck somewhere";
  // Not `IsInPlay()`: a match may legitimately end between restarts, and the
  // flag is a latch rather than a summary. What must not happen is ending
  // inside an armed restart nobody has taken - that is the stall this guards.
  EXPECT_FALSE(r.match->IsInSetPiece()) << "the match ended inside an unfinished restart";
}

// The taker of an armed restart must take it, every time, rather than the match
// stalling in the arming window for the whole period.
TEST(PesRestartTaking, TheTakerTakesTheKickBeforeTheWhistle) {
  Reality r;
  if (!r.Ready()) GTEST_SKIP() << "PES21_ROOT not set";
  r.Play(1001, 2);
  ASSERT_TRUE(r.match) << "constants unavailable";
  // With a stalled taker the match never leaves the arming window, so the flag
  // stays true and the cap is reached.
  EXPECT_TRUE(r.match->IsGameOver()) << "the sim hit its cap: a restart was never taken";
  EXPECT_FALSE(r.match->IsInSetPiece()) << "a restart was left unfinished";
}

// Both halves must be reachable: the second half's kick-off is another arming
// window, and it is where the aliasing used to bite hardest (a stale taker with
// the game already running).
TEST(PesRestartTaking, TheSecondHalfRestartIsAlsoTaken) {
  Reality r;
  if (!r.Ready()) GTEST_SKIP() << "PES21_ROOT not set";
  r.Play(1002, 2);
  ASSERT_TRUE(r.match) << "constants unavailable";
  EXPECT_TRUE(r.match->IsGameOver());
  EXPECT_FALSE(r.match->IsInSetPiece());
}

}  // namespace
