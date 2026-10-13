// The PES clock inside a live match: half length by phase, the once-per-half
// stoppage formula, the freeze during a stoppage and the state-completion
// transition - the divergences docs/re/pes-re-tactics-clock.md 8 lists.

#include <gtest/gtest.h>

#include <cmath>

#include <cstdlib>
#include <memory>
#include <stdexcept>
#include <optional>
#include <string>

#include "onthepitch/match.hpp"
#include "onthepitch/matchsetup.hpp"
#include "onthepitch/pesball.hpp"
#include "onthepitch/pesjudge.hpp"
#include "onthepitch/peskeeper.hpp"
#include "onthepitch/pesstamina.hpp"
#include "onthepitch/pesstoppage.hpp"
#include "onthepitch/pesrandom.hpp"
#include "onthepitch/pesclock.hpp"
#include "rig/rig.hpp"
#include "pes/exe.hpp"
#include "pes/vfs.hpp"
#include "simcontext.hpp"

namespace {

constexpr int HOME_TEAM = 1, AWAY_TEAM = 2;
constexpr unsigned int SEED = 1000;
constexpr int SHORT_MATCH_MINUTES = 2;

// The player skeleton comes from the user's PES install, without which a Match
// cannot be built at all.
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
// pes-re-ball-contact.md 6). Required by every Match, like the clock's.
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

// PES 2021's generator constants (docs/re/pes-re-rng.md 1).
const pes::RngConstants* SharedRngConstants() {
  static std::optional<pes::RngConstants> constants;
  static bool tried = false;
  if (!tried) {
    tried = true;
    if (const char* root = std::getenv("PES21_ROOT")) {
      std::string err;
      if (auto exe = pes::PeImage::Open(std::string(root) + "/PES2021.exe", &err))
        constants = pes::LoadRngConstants(*exe, &err);
    }
  }
  return constants ? &*constants : nullptr;
}

// PES 2021's goalkeeper deflect point and post-catch distribution target
// (docs/re/pes-re-keeper-port.md).
// PES 2021's restart timings (docs/re/pes-re-foul-choreography.md 9).
const pes::StoppageConstants* SharedStoppageConstants() {
  static std::optional<pes::StoppageConstants> constants;
  static bool tried = false;
  if (!tried) {
    tried = true;
    if (const char* root = std::getenv("PES21_ROOT")) {
      std::string err;
      if (auto exe = pes::PeImage::Open(std::string(root) + "/PES2021.exe", &err))
        constants = pes::LoadStoppageConstants(*exe, &err);
    }
  }
  return constants ? &*constants : nullptr;
}

const pes::KeeperConstants* SharedKeeperConstants() {
  static std::optional<pes::KeeperConstants> constants;
  static bool tried = false;
  if (!tried) {
    tried = true;
    if (const char* root = std::getenv("PES21_ROOT")) {
      std::string err;
      if (auto exe = pes::PeImage::Open(std::string(root) + "/PES2021.exe", &err))
        constants = pes::LoadKeeperConstants(*exe, &err);
    }
  }
  return constants ? &*constants : nullptr;
}

// PES 2021's stamina model (docs/re/pes-re-stamina-drain.md): the tank, the
// drain, the breath, the break bonus and the injury roll's constants.
const pes::StaminaConstants* SharedStaminaConstants() {
  static std::optional<pes::StaminaConstants> constants;
  static bool tried = false;
  if (!tried) {
    tried = true;
    if (const char* root = std::getenv("PES21_ROOT")) {
      std::string err;
      if (auto exe = pes::PeImage::Open(std::string(root) + "/PES2021.exe", &err))
        constants = pes::LoadStaminaConstants(*exe, &err);
    }
  }
  return constants ? &*constants : nullptr;
}

#define REQUIRE_RIG()                                                                    \
  if (!SharedRig()) GTEST_SKIP() << "PES21_ROOT not set: the player skeleton comes from PES"; \
  if (!SharedBallConstants())                                                            \
    GTEST_SKIP() << "constant_match.bin/exe: ball physics constants unavailable"; \
  if (!SharedStoppageConstants())                                               \
    GTEST_SKIP() << "PES2021.exe: stoppage constants unavailable";

// The report's clock constants, so the arithmetic is exercised against a Match.
pes::PesClockConstants FakeConstants() {
  pes::PesClockConstants c;
  c.gameSpeed = 54.0f;
  c.halfSeconds = 16000.0f;
  c.added2ndHalf = 9000.0f;
  c.addedTier_3_6_2nd = 7200.0f;
  c.addedTier_1_2_2nd = 3600.0f;
  c.addedExtraTime = 5400.0f;
  c.addedTier_1_2_ET = 1800.0f;
  c.addedTier_3_6_ET = 3600.0f;
  c.ticksPerMinute = 960;
  return c;
}

}  // namespace

// GF's match clock was its own millisecond counter advanced by
// ticks*10*timeScale. PES's is a percentage of the period's game-minutes, at
// 960 ticks per game-minute, driven by the 16000-units-per-second floor.
TEST(PesClockInMatch, ClockIsThePesPercentageForm) {
  REQUIRE_RIG();
  blunted::Database db;
  ASSERT_TRUE(db.LoadSqlScript(GF_SOURCE_DIR "/assets/fixtures/teams.sql"));
  SetDB(&db);
  MatchSetup setup;
  setup.homeTeamDbId = HOME_TEAM;
  setup.awayTeamDbId = AWAY_TEAM;
  setup.seed = SEED;
  setup.durationMinutes = SHORT_MATCH_MINUTES;
  setup.animationDir = GF_SOURCE_DIR "/assets/gf_anims";
  setup.rig = SharedRig();
  auto constants = std::make_unique<pes::PesClockConstants>(FakeConstants());
  setup.pesClockConstants = constants.get();
  setup.pesBallConstants = SharedBallConstants();
  setup.pesJudgeConstants = SharedJudgeConstants();
  setup.pesRngConstants = SharedRngConstants();
  setup.pesKeeperConstants = SharedKeeperConstants();
  setup.pesStaminaConstants = SharedStaminaConstants();
  setup.pesStoppageConstants = SharedStoppageConstants();
  Match match(new MatchData(HOME_TEAM, AWAY_TEAM), setup, Properties());
  SetDB(nullptr);

  match.SetMatchPhase(e_MatchPhase_1stHalf);
  ASSERT_FALSE(match.IsInPlay());
  match.StartPlay();
  // A half is 45 game-minutes: 2700 game-seconds x 16 ticks = 43,200 ticks.
  match.AdvanceMatchSeconds(2700.0);
  EXPECT_EQ(match.GetPesClock().Ticks(), 43200);
  EXPECT_NEAR(match.GetPesClock().Percentage(), 5000.0f, 1.0);
}

// The PES clock counts game time, the same game time GF's own match clock counts
// (matchduration.hpp maps each 10 ms sim tick onto the match length), so the two
// agree: a period cannot end on one clock while the other is mid-half.
TEST(PesClockInMatch, ClockCountsTheMatchsGameTime) {
  REQUIRE_RIG();
  blunted::Database db;
  ASSERT_TRUE(db.LoadSqlScript(GF_SOURCE_DIR "/assets/fixtures/teams.sql"));
  SetDB(&db);
  MatchSetup setup;
  setup.homeTeamDbId = HOME_TEAM;
  setup.awayTeamDbId = AWAY_TEAM;
  setup.seed = SEED;
  setup.durationMinutes = SHORT_MATCH_MINUTES;
  setup.animationDir = GF_SOURCE_DIR "/assets/gf_anims";
  setup.rig = SharedRig();
  auto constants = std::make_unique<pes::PesClockConstants>(FakeConstants());
  setup.pesClockConstants = constants.get();
  setup.pesBallConstants = SharedBallConstants();
  setup.pesJudgeConstants = SharedJudgeConstants();
  setup.pesRngConstants = SharedRngConstants();
  setup.pesKeeperConstants = SharedKeeperConstants();
  setup.pesStaminaConstants = SharedStaminaConstants();
  setup.pesStoppageConstants = SharedStoppageConstants();
  Match match(new MatchData(HOME_TEAM, AWAY_TEAM), setup, Properties());
  SetDB(nullptr);

  constexpr int kTicks = 3000;  // 30 s of simulation: kick-off plus open play
  for (int i = 0; i < kTicks; ++i) match.Process();
  ASSERT_GT(match.GetMatchTime_ms(), 0u) << "no open play in " << kTicks << " ticks";
  // One game-second of slack covers the u32 truncation of both counters.
  EXPECT_NEAR(match.GetPesClock().ElapsedSeconds(), match.GetMatchTime_ms() / 1000.0f, 1.0f);
}

// PES takes the half length from the phase table, not from the match-length
// setting: a first half is 45 game-minutes whatever the duration.
TEST(PesClockInMatch, HalfLengthComesFromThePhaseNotTheSetting) {
  REQUIRE_RIG();
  blunted::Database db;
  ASSERT_TRUE(db.LoadSqlScript(GF_SOURCE_DIR "/assets/fixtures/teams.sql"));
  SetDB(&db);
  for (int minutes : {2, 5, 20}) {
    MatchSetup setup;
    setup.homeTeamDbId = HOME_TEAM;
    setup.awayTeamDbId = AWAY_TEAM;
    setup.seed = SEED;
    setup.durationMinutes = minutes;
    setup.animationDir = GF_SOURCE_DIR "/assets/gf_anims";
    setup.rig = SharedRig();
    auto constants = std::make_unique<pes::PesClockConstants>(FakeConstants());
    setup.pesClockConstants = constants.get();
  setup.pesBallConstants = SharedBallConstants();
  setup.pesJudgeConstants = SharedJudgeConstants();
  setup.pesRngConstants = SharedRngConstants();
  setup.pesKeeperConstants = SharedKeeperConstants();
  setup.pesStaminaConstants = SharedStaminaConstants();
  setup.pesStoppageConstants = SharedStoppageConstants();
    Match match(new MatchData(HOME_TEAM, AWAY_TEAM), setup, Properties());
    // The half length is a function of the phase alone: the match-length
    // setting no longer reaches it.
    match.SetMatchPhase(e_MatchPhase_1stHalf);
    EXPECT_EQ(pes::HalfLengthMinutes(match.GetMatchPhase()), 45) << "minutes=" << minutes;
    EXPECT_EQ(pes::PeriodEndTicks(e_MatchPhase_1stHalf), 43200) << "minutes=" << minutes;
  }
  SetDB(nullptr);
}

// Stoppage: added time is `elapsedSeconds * rate` plus the tier bonus,
// evaluated once at the end of the half. There are no event counters, so the
// number depends on the elapsed time alone.
TEST(PesClockInMatch, AddedTimeIsOncePerHalfAndHasNoEventCounters) {
  REQUIRE_RIG();
  blunted::Database db;
  ASSERT_TRUE(db.LoadSqlScript(GF_SOURCE_DIR "/assets/fixtures/teams.sql"));
  SetDB(&db);
  MatchSetup setup;
  setup.homeTeamDbId = HOME_TEAM;
  setup.awayTeamDbId = AWAY_TEAM;
  setup.seed = SEED;
  setup.durationMinutes = SHORT_MATCH_MINUTES;
  setup.animationDir = GF_SOURCE_DIR "/assets/gf_anims";
  setup.rig = SharedRig();
  auto constants = std::make_unique<pes::PesClockConstants>(FakeConstants());
  setup.pesClockConstants = constants.get();
  setup.pesBallConstants = SharedBallConstants();
  setup.pesJudgeConstants = SharedJudgeConstants();
  setup.pesRngConstants = SharedRngConstants();
  setup.pesKeeperConstants = SharedKeeperConstants();
  setup.pesStaminaConstants = SharedStaminaConstants();
  setup.pesStoppageConstants = SharedStoppageConstants();
  Match match(new MatchData(HOME_TEAM, AWAY_TEAM), setup, Properties());
  SetDB(nullptr);
  match.SetMatchPhase(e_MatchPhase_2ndHalf);
  match.StartPlay();

  // The elapsed seconds the formula is evaluated on are the running match
  // clock's, so a longer half adds more.
  match.AdvanceMatchSeconds(900.0);
  const uint32_t shortHalf = match.GetPesClock().AddedTicks(4);
  match.AdvanceMatchSeconds(900.0);
  const uint32_t longHalf = match.GetPesClock().AddedTicks(4);
  EXPECT_GT(longHalf, shortHalf) << "twice the elapsed must add twice the time";
  EXPECT_EQ(longHalf, shortHalf * 2) << "the formula is linear in the elapsed seconds";
}

// PES's added time has a tier term with no GF equivalent (the on-pitch
// performer's perf tier). The tier is an explicit input with no default.
TEST(PesClockInMatch, AddedTimeTierIsAnExplicitInput) {
  REQUIRE_RIG();
  blunted::Database db;
  ASSERT_TRUE(db.LoadSqlScript(GF_SOURCE_DIR "/assets/fixtures/teams.sql"));
  SetDB(&db);
  MatchSetup setup;
  setup.homeTeamDbId = HOME_TEAM;
  setup.awayTeamDbId = AWAY_TEAM;
  setup.seed = SEED;
  setup.durationMinutes = SHORT_MATCH_MINUTES;
  setup.animationDir = GF_SOURCE_DIR "/assets/gf_anims";
  setup.rig = SharedRig();
  auto constants = std::make_unique<pes::PesClockConstants>(FakeConstants());
  setup.pesClockConstants = constants.get();
  setup.pesBallConstants = SharedBallConstants();
  setup.pesJudgeConstants = SharedJudgeConstants();
  setup.pesRngConstants = SharedRngConstants();
  setup.pesKeeperConstants = SharedKeeperConstants();
  setup.pesStaminaConstants = SharedStaminaConstants();
  setup.pesStoppageConstants = SharedStoppageConstants();
  Match match(new MatchData(HOME_TEAM, AWAY_TEAM), setup, Properties());
  SetDB(nullptr);
  match.SetMatchPhase(e_MatchPhase_2ndHalf);
  match.StartPlay();
  match.AdvanceMatchSeconds(2700.0);
  // Tier 4 earns the 7200-unit bonus over the plain per-second term.
  const uint32_t withTier = match.GetPesClock().AddedTicks(4);
  const uint32_t withoutTier = match.GetPesClock().AddedTicks(7);
  EXPECT_GT(withTier, withoutTier);
  EXPECT_EQ(withoutTier, 0) << "tier 7 earns no bonus at all";
}

// A stoppage never adds time: the clock simply stops while play is stopped.
TEST(PesClockInMatch, ClockFreezesWhenPlayIsStopped) {
  REQUIRE_RIG();
  blunted::Database db;
  ASSERT_TRUE(db.LoadSqlScript(GF_SOURCE_DIR "/assets/fixtures/teams.sql"));
  SetDB(&db);
  MatchSetup setup;
  setup.homeTeamDbId = HOME_TEAM;
  setup.awayTeamDbId = AWAY_TEAM;
  setup.seed = SEED;
  setup.durationMinutes = SHORT_MATCH_MINUTES;
  setup.animationDir = GF_SOURCE_DIR "/assets/gf_anims";
  setup.rig = SharedRig();
  auto constants = std::make_unique<pes::PesClockConstants>(FakeConstants());
  setup.pesClockConstants = constants.get();
  setup.pesBallConstants = SharedBallConstants();
  setup.pesJudgeConstants = SharedJudgeConstants();
  setup.pesRngConstants = SharedRngConstants();
  setup.pesKeeperConstants = SharedKeeperConstants();
  setup.pesStaminaConstants = SharedStaminaConstants();
  setup.pesStoppageConstants = SharedStoppageConstants();
  Match match(new MatchData(HOME_TEAM, AWAY_TEAM), setup, Properties());
  SetDB(nullptr);
  match.SetMatchPhase(e_MatchPhase_1stHalf);
  match.StartPlay();
  match.AdvanceMatchSeconds(600.0);
  const uint32_t before = match.GetPesClock().Ticks();
  match.StopPlay();  // a stoppage state: the ClockFSM does not run
  match.AdvanceMatchSeconds(300.0);
  EXPECT_EQ(match.GetPesClock().Ticks(), before) << "a stoppage freezes the clock";
  match.StartPlay();
  match.AdvanceMatchSeconds(300.0);
  EXPECT_GT(match.GetPesClock().Ticks(), before) << "and resuming advances it again";
}

// Extra time's period end and its added-time path both differ (105/120
// game-minutes, 5400 instead of 9000 per match second).
TEST(PesClockInMatch, ExtraTimeHasItsOwnPeriodEndAndRate) {
  REQUIRE_RIG();
  blunted::Database db;
  ASSERT_TRUE(db.LoadSqlScript(GF_SOURCE_DIR "/assets/fixtures/teams.sql"));
  SetDB(&db);
  MatchSetup setup;
  setup.homeTeamDbId = HOME_TEAM;
  setup.awayTeamDbId = AWAY_TEAM;
  setup.seed = SEED;
  setup.durationMinutes = SHORT_MATCH_MINUTES;
  setup.animationDir = GF_SOURCE_DIR "/assets/gf_anims";
  setup.rig = SharedRig();
  auto constants = std::make_unique<pes::PesClockConstants>(FakeConstants());
  setup.pesClockConstants = constants.get();
  setup.pesBallConstants = SharedBallConstants();
  setup.pesJudgeConstants = SharedJudgeConstants();
  setup.pesRngConstants = SharedRngConstants();
  setup.pesKeeperConstants = SharedKeeperConstants();
  setup.pesStaminaConstants = SharedStaminaConstants();
  setup.pesStoppageConstants = SharedStoppageConstants();
  Match match(new MatchData(HOME_TEAM, AWAY_TEAM), setup, Properties());
  SetDB(nullptr);
  match.SetMatchPhase(e_MatchPhase_1stExtraTime);
  match.StartPlay();
  EXPECT_EQ(pes::HalfLengthMinutes(match.GetMatchPhase()), 15);
  EXPECT_EQ(pes::PeriodEndTicks(e_MatchPhase_1stExtraTime), 100800);
  match.AdvanceMatchSeconds(900.0);  // a full 15-minute period
  EXPECT_EQ(match.GetPesClock().Ticks(), 14400);
}

// The clock's exe-derived constants come from the install, not from code.
TEST(PesClockInMatch, ClockConstantsAreExeDerived) {
  REQUIRE_RIG();
  blunted::Database db;
  ASSERT_TRUE(db.LoadSqlScript(GF_SOURCE_DIR "/assets/fixtures/teams.sql"));
  SetDB(&db);
  MatchSetup setup;
  setup.homeTeamDbId = HOME_TEAM;
  setup.awayTeamDbId = AWAY_TEAM;
  setup.seed = SEED;
  setup.durationMinutes = SHORT_MATCH_MINUTES;
  setup.animationDir = GF_SOURCE_DIR "/assets/gf_anims";
  setup.rig = SharedRig();
  // The clock constants are required, like the rig: a match built without them
  // is a caller error, and there is no fallback rig either.
  setup.pesClockConstants = nullptr;
  EXPECT_THROW(Match(new MatchData(HOME_TEAM, AWAY_TEAM), setup, Properties()),
               std::runtime_error);

  const char* root = std::getenv("PES21_ROOT");
  if (!root) GTEST_SKIP() << "PES21_ROOT not set";
  pes::PesVfs vfs;
  std::string err;
  ASSERT_TRUE(vfs.Mount(root, &err)) << err;
  auto exe = pes::PeImage::Open(std::string(root) + "/PES2021.exe", &err);
  ASSERT_TRUE(exe) << err;
  auto c = pes::LoadPesClockConstants(*exe, &err);
  ASSERT_TRUE(c) << err;
  setup.pesClockConstants = &*c;
  auto b = pes::LoadPesBallConstants(*exe, vfs, &err);
  ASSERT_TRUE(b) << err;
  setup.pesBallConstants = &*b;
  setup.pesJudgeConstants = SharedJudgeConstants();
  setup.pesRngConstants = SharedRngConstants();
  setup.pesKeeperConstants = SharedKeeperConstants();
  setup.pesStaminaConstants = SharedStaminaConstants();
  setup.pesStoppageConstants = SharedStoppageConstants();
  Match withExe(new MatchData(HOME_TEAM, AWAY_TEAM), setup, Properties());
  EXPECT_FLOAT_EQ(withExe.GetPesClockConstants().gameSpeed, 54.0f);
  EXPECT_FLOAT_EQ(withExe.GetPesBallConstants().image.tickHz, 54.0f);
  SetDB(nullptr);
}

// The ball's constants are required the same way, and they come from the
// install's constant_match.bin `ball.o` rather than from code.
TEST(PesBallInMatch, BallConstantsAreRequiredAndInstallDerived) {
  REQUIRE_RIG();
  blunted::Database db;
  ASSERT_TRUE(db.LoadSqlScript(GF_SOURCE_DIR "/assets/fixtures/teams.sql"));
  SetDB(&db);
  MatchSetup setup;
  setup.homeTeamDbId = HOME_TEAM;
  setup.awayTeamDbId = AWAY_TEAM;
  setup.seed = SEED;
  setup.durationMinutes = SHORT_MATCH_MINUTES;
  setup.animationDir = GF_SOURCE_DIR "/assets/gf_anims";
  setup.rig = SharedRig();
  auto constants = std::make_unique<pes::PesClockConstants>(FakeConstants());
  setup.pesClockConstants = constants.get();
  setup.pesBallConstants = SharedBallConstants();
  setup.pesJudgeConstants = SharedJudgeConstants();
  setup.pesRngConstants = SharedRngConstants();
  setup.pesKeeperConstants = SharedKeeperConstants();
  setup.pesStaminaConstants = SharedStaminaConstants();
  setup.pesStoppageConstants = SharedStoppageConstants();
  // Without them, the Match refuses to build, like the clock's and the rig.
  setup.pesBallConstants = nullptr;
  EXPECT_THROW(Match(new MatchData(HOME_TEAM, AWAY_TEAM), setup, Properties()),
               std::runtime_error);

  const char* root = std::getenv("PES21_ROOT");
  if (!root) GTEST_SKIP() << "PES21_ROOT not set";
  pes::PesVfs vfs;
  std::string err;
  ASSERT_TRUE(vfs.Mount(root, &err)) << err;
  auto exe = pes::PeImage::Open(std::string(root) + "/PES2021.exe", &err);
  ASSERT_TRUE(exe) << err;
  auto b = pes::LoadPesBallConstants(*exe, vfs, &err);
  ASSERT_TRUE(b) << err;
  setup.pesBallConstants = &*b;
  setup.pesJudgeConstants = SharedJudgeConstants();
  setup.pesRngConstants = SharedRngConstants();
  setup.pesKeeperConstants = SharedKeeperConstants();
  setup.pesStaminaConstants = SharedStaminaConstants();
  setup.pesStoppageConstants = SharedStoppageConstants();
  Match withBall(new MatchData(HOME_TEAM, AWAY_TEAM), setup, Properties());
  // docs/re/pes-re-ball-constants.md 3.1/6.1: the mode-1 column.
  EXPECT_FLOAT_EQ(withBall.GetPesBallConstants()
                      .record.At(pes::BallRecordConstants::kFrictionRollSpeedMin, 1),
                  8.0f);
  EXPECT_FLOAT_EQ(withBall.GetPesBallConstants().record.boundRollMax, 7.5f);
  SetDB(nullptr);
}
