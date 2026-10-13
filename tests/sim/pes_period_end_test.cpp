// PES 2021's period-end rules in the referee: the half length comes from the
// phase table, added time is the once-per-half linear formula, and the period
// changes only when the restart state has completed (report 2.1, 3.2, 5.4).

#include <gtest/gtest.h>

#include <cstdlib>
#include <memory>
#include <string>

#include "onthepitch/match.hpp"
#include "onthepitch/matchsetup.hpp"
#include "onthepitch/pesball.hpp"
#include "onthepitch/pesjudge.hpp"
#include "onthepitch/pesclock.hpp"
#include "pes/exe.hpp"
#include "pes/vfs.hpp"
#include "rig/rig.hpp"
#include "simcontext.hpp"

namespace {

constexpr int HOME_TEAM = 1, AWAY_TEAM = 2;
constexpr unsigned int SEED = 1000;
constexpr int SHORT_MATCH_MINUTES = 2;

// The player skeleton comes from the user's PES install.
const pesrig::Rig* SharedRig() {
  static std::optional<pesrig::Rig> rig;
  static bool tried = false;
  if (!tried) {
    tried = true;
    if (const char* root = std::getenv("PES21_ROOT")) {
      pes::PesVfs vfs;
      std::string err;
      if (vfs.Mount(root, &err)) rig = pesrig::BuildRig(vfs, &err);
    }
  }
  return rig ? &*rig : nullptr;
}

// PES 2021's ball physics constants, from the user's install (docs/re/
// pes-re-ball-contact.md 6). The Match requires them like the clock's.
const pes::PesBallConstants* SharedBallConstants() {
  static std::optional<pes::PesBallConstants> constants;
  static bool tried = false;
  if (!tried) {
    tried = true;
    if (const char* root = std::getenv("PES21_ROOT")) {
      pes::PesVfs vfs;
      std::string err;
      if (vfs.Mount(root, &err)) {
        if (auto exe = pes::PeImage::Open(std::string(root) + "/PES2021.exe", &err))
          constants = pes::LoadPesBallConstants(*exe, vfs, &err);
      }
    }
  }
  return constants ? &*constants : nullptr;
}

// PES 2021's foul-decision constants (docs/re/pes-re-duels-fouls.md 2).
const pes::JudgeConstants* SharedJudgeConstants() {
  static std::optional<pes::JudgeConstants> constants;
  static bool tried = false;
  if (!tried) {
    tried = true;
    if (const char* root = std::getenv("PES21_ROOT")) {
      std::string err;
      if (auto exe = pes::PeImage::Open(std::string(root) + "/PES2021.exe", &err))
        constants = pes::LoadJudgeConstants(*exe, &err);
    }
  }
  return constants ? &*constants : nullptr;
}

#define REQUIRE_RIG()                                                                    \
  if (!SharedRig()) GTEST_SKIP() << "PES21_ROOT not set: the player skeleton comes from PES"; \
  if (!SharedBallConstants())                                                            \
    GTEST_SKIP() << "constant_match.bin/exe: ball physics constants unavailable"

// A match in the given phase with PES's clock constants installed, and the
// simulation clock driven to `seconds` of elapsed game time.
std::unique_ptr<Match> MatchAtPhase(e_MatchPhase phase, float seconds) {
  blunted::Database db;
  static std::unique_ptr<blunted::Database> held;
  // The engine reads a process-wide database; keep it alive for the match.
  held = std::make_unique<blunted::Database>();
  if (!held->LoadSqlScript(GF_SOURCE_DIR "/assets/fixtures/teams.sql")) return nullptr;
  SetDB(held.get());
  MatchSetup setup;
  setup.homeTeamDbId = HOME_TEAM;
  setup.awayTeamDbId = AWAY_TEAM;
  setup.seed = SEED;
  setup.durationMinutes = SHORT_MATCH_MINUTES;
  setup.animationDir = GF_SOURCE_DIR "/assets/gf_anims";
  setup.rig = SharedRig();
  static pes::PesClockConstants constants;
  constants.gameSpeed = 54.0f;
  constants.halfSeconds = 16000.0f;
  constants.added2ndHalf = 9000.0f;
  constants.addedTier_3_6_2nd = 7200.0f;
  constants.addedTier_1_2_2nd = 3600.0f;
  constants.addedExtraTime = 5400.0f;
  constants.addedTier_1_2_ET = 1800.0f;
  constants.addedTier_3_6_ET = 3600.0f;
  constants.ticksPerMinute = 960;
  setup.pesClockConstants = &constants;
  setup.pesBallConstants = SharedBallConstants();
  setup.pesJudgeConstants = SharedJudgeConstants();
  if (!setup.pesBallConstants) return nullptr;
  auto match = std::make_unique<Match>(new MatchData(HOME_TEAM, AWAY_TEAM), setup, Properties());
  match->SetMatchPhase(phase);
  match->StartPlay();
  match->AdvanceMatchSeconds(seconds);
  return match;
}

}  // namespace

// The period end the referee blows at is the phase table's own value, not GF's
// fixed millisecond schedule: a first half is 45 game-minutes (2700 seconds).
TEST(PesPeriodEnd, WhistleIsThePhaseTablesOwnValue) {
  REQUIRE_RIG();
  auto match = MatchAtPhase(e_MatchPhase_1stHalf, 2699.0f);
  ASSERT_TRUE(match);
  EXPECT_FALSE(match->GetPesClock().IsPeriodDue()) << "44:59 must not end the period";
  match->AdvanceMatchSeconds(1.0f);
  EXPECT_TRUE(match->GetPesClock().IsPeriodDue()) << "45:00 does";
}

// A second half runs 45 game-minutes of its own from its kick-off, and extra
// time's period is 15 (report 2.1's 45/90/105/120 by phase).
TEST(PesPeriodEnd, EachPeriodHasItsOwnEnd) {
  REQUIRE_RIG();
  // 44:59 into the second half, counted from its own kick-off.
  auto match = MatchAtPhase(e_MatchPhase_2ndHalf, 2699.0f);
  ASSERT_TRUE(match);
  EXPECT_FALSE(match->GetPesClock().IsPeriodDue());
  match->AdvanceMatchSeconds(1.0f);
  EXPECT_TRUE(match->GetPesClock().IsPeriodDue());

  // 14:59 into a period of extra time, likewise from its own kick-off.
  auto extra = MatchAtPhase(e_MatchPhase_1stExtraTime, 899.0f);
  ASSERT_TRUE(extra);
  EXPECT_FALSE(extra->GetPesClock().IsPeriodDue());
  extra->AdvanceMatchSeconds(1.0f);
  EXPECT_TRUE(extra->GetPesClock().IsPeriodDue());
}

// Added time is `elapsedSeconds * rate` evaluated once at the end of the half,
// with the tier bonus on top; the referee blows at the period end plus it.
TEST(PesPeriodEnd, AddedTimeExtendsTheWhistle) {
  REQUIRE_RIG();
  auto match = MatchAtPhase(e_MatchPhase_2ndHalf, 9000.0f);
  ASSERT_TRUE(match);
  // 9000 elapsed seconds at 7200 clock units per second (tier 4), 1000 units per
  // tick and 960 ticks per game-minute.
  const float added = match->GetPesClock().PeriodEndWithAdded(4);
  EXPECT_GT(added, 45.0f) << "the tier bonus extends the period";
  // Tier 7 earns no bonus, so the whistle is the period's own end.
  EXPECT_FLOAT_EQ(match->GetPesClock().PeriodEndWithAdded(7), 45.0f);
}

// The whistle is announced once, from the formula, not from an event counter.
TEST(PesPeriodEnd, AddedTimeIsAnnouncedFromTheFormula) {
  REQUIRE_RIG();
  auto match = MatchAtPhase(e_MatchPhase_2ndHalf, 2700.0f);
  ASSERT_TRUE(match);
  // 2700 s x 7200 / 1000 = 19,440 ticks, which at 960 per game-minute is over
  // twenty game-minutes of added time; the board shows at most nine.
  EXPECT_LE(match->GetPesClock().AddedMinutes(4), pes::kMaxAnnouncedAddedMinutes);
  EXPECT_GE(match->GetPesClock().AddedMinutes(4), 9);
  // Tier 7's board reads zero: no bonus at all.
  EXPECT_EQ(match->GetPesClock().AddedMinutes(7), 0);
}

// A stoppage freezes the clock while the period is running, and the whistle is
// not advanced by it (report 3.3: a stoppage adds no time).
TEST(PesPeriodEnd, AStoppageDoesNotExtendThePeriod) {
  REQUIRE_RIG();
  auto match = MatchAtPhase(e_MatchPhase_1stHalf, 2400.0f);
  ASSERT_TRUE(match);
  const uint32_t atTheStoppage = match->GetPesClock().Ticks();
  match->StopPlay();
  match->AdvanceMatchSeconds(300.0f);
  EXPECT_EQ(match->GetPesClock().Ticks(), atTheStoppage);
  // The period is still short of its end, exactly as far as it was.
  EXPECT_FALSE(match->GetPesClock().IsPeriodDue());
  match->StartPlay();
  match->AdvanceMatchSeconds(300.0f);
  EXPECT_TRUE(match->GetPesClock().IsPeriodDue()) << "only real play reaches the end";
}
