#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <optional>
#include <string>

#include "data/matchdata.hpp"
#include "onthepitch/match.hpp"
#include "onthepitch/matchreport.hpp"
#include "onthepitch/matchsetup.hpp"
#include "onthepitch/pesball.hpp"
#include "onthepitch/pesjudge.hpp"
#include "onthepitch/peskeeper.hpp"
#include "onthepitch/pesstamina.hpp"
#include "onthepitch/pesstoppage.hpp"
#include "onthepitch/pesrandom.hpp"
#include "onthepitch/pesclock.hpp"
#include "pes/exe.hpp"
#include "pes/vfs.hpp"
#include "rig/rig.hpp"
#include "simcontext.hpp"

// Headless AI-vs-AI matches on the synthetic fixture, with the player skeleton
// built from the user's PES install (skipped without one).
namespace {

constexpr int HOME_TEAM = 1, AWAY_TEAM = 2;
constexpr unsigned int SEED = 1000;
constexpr int SHORT_MATCH_MINUTES = 2;  // long enough for set pieces and shots
constexpr int FULL_MATCH_MINUTES = 5;
// A match must end: the clock covers the duration plus generous added time.
constexpr unsigned long kMaxSimTime_ms = 30ul * 60ul * 1000ul;

const pesrig::Rig* SharedRig() {
  static std::optional<pesrig::Rig> rig;
  static bool tried = false;
  if (!tried) {
    tried = true;
    const char* root = std::getenv("PES21_ROOT");
    if (!root) return nullptr;
    pes::PesVfs vfs;
    std::string err;
    if (vfs.Mount(root, &err)) rig = pesrig::BuildRig(vfs, &err);
  }
  return rig ? &*rig : nullptr;
}

// PES 2021's match-clock constants, read from the install's own exe by the same
// rule the rig is (docs/re/pes-re-tactics-clock.md 7.2). Required by every Match.
const pes::PesClockConstants* SharedClockConstants() {
  static std::optional<pes::PesClockConstants> constants;
  static bool tried = false;
  if (!tried) {
    tried = true;
    if (const char* root = std::getenv("PES21_ROOT")) {
      pes::PesVfs vfs;
      std::string err;
      if (vfs.Mount(root, &err)) {
        if (auto exe = pes::PeImage::Open(std::string(root) + "/PES2021.exe", &err))
          constants = pes::LoadPesClockConstants(*exe, &err);
      }
    }
  }
  return constants ? &*constants : nullptr;
}

// PES 2021's ball physics constants: the `ball.o` record of
// constant_match.bin plus the image values (docs/re/pes-re-ball-contact.md 6).
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


#define REQUIRE_PES_INSTALL()                                          \
  if (!SharedRig()) GTEST_SKIP() << "PES21_ROOT not set: the player skeleton comes from PES"; \
  if (!SharedClockConstants())                                                          \
    GTEST_SKIP() << "PES2021.exe: match-clock constants unavailable" \
  if (!SharedStoppageConstants()) GTEST_SKIP() << "PES2021.exe: stoppage constants unavailable"

struct Outcome {
  bool finished = false;
  uint64_t hash = 0;
  std::string report;
  int goals[2] = {0, 0};
};

Outcome Play(unsigned int seed, int minutes) {
  blunted::Database db;
  EXPECT_TRUE(db.LoadSqlScript(GF_SOURCE_DIR "/assets/fixtures/teams.sql"));
  SetDB(&db);
  MatchSetup setup;
  setup.homeTeamDbId = HOME_TEAM;
  setup.awayTeamDbId = AWAY_TEAM;
  setup.seed = seed;
  setup.durationMinutes = minutes;
  setup.animationDir = GF_SOURCE_DIR "/assets/gf_anims";
  setup.rig = SharedRig();
  setup.pesClockConstants = SharedClockConstants();
  setup.pesBallConstants = SharedBallConstants();
  setup.pesJudgeConstants = SharedJudgeConstants();
  setup.pesRngConstants = SharedRngConstants();
  setup.pesKeeperConstants = SharedKeeperConstants();
  setup.pesStaminaConstants = SharedStaminaConstants();
  setup.pesStoppageConstants = SharedStoppageConstants();

  Outcome out;
  {
    // Match owns and deletes its MatchData.
    Match match(new MatchData(HOME_TEAM, AWAY_TEAM), setup, Properties());
    while (!match.IsGameOver() && match.GetSimTime_ms() < kMaxSimTime_ms) match.Process();
    out.finished = match.IsGameOver();
    out.hash = match.GetDeterminismHash();
    out.goals[0] = match.GetMatchData()->GetGoalCount(0);
    out.goals[1] = match.GetMatchData()->GetGoalCount(1);
    char* buf = nullptr;
    size_t len = 0;
    FILE* mem = open_memstream(&buf, &len);
    PrintMatchReport(match, mem);
    std::fclose(mem);
    out.report.assign(buf, len);
    std::free(buf);
  }
  SetDB(nullptr);
  return out;
}

#define REQUIRE_RIG() \
  if (!SharedRig()) GTEST_SKIP() << "PES21_ROOT not set: the player skeleton comes from PES"; \
  if (!SharedClockConstants())                                                  \
    GTEST_SKIP() << "PES2021.exe: match-clock constants unavailable"; \
  if (!SharedBallConstants())                                                   \
    GTEST_SKIP() << "constant_match.bin/exe: ball physics constants unavailable"; \
  if (!SharedStoppageConstants())                                               \
    GTEST_SKIP() << "PES2021.exe: stoppage constants unavailable";

}  // namespace

// Player IDs phase per-player work ((time + id * 10) % 100 in Player::Process):
// a process-wide counter shifted them in a second match and the same seed then
// played a different game.
TEST(SimMatch, PlayerIdsStartAtZeroInEveryMatch) {
  REQUIRE_RIG();
  blunted::Database db;
  ASSERT_TRUE(db.LoadSqlScript(GF_SOURCE_DIR "/assets/fixtures/teams.sql"));
  SetDB(&db);
  MatchSetup setup;
  setup.homeTeamDbId = HOME_TEAM;
  setup.awayTeamDbId = AWAY_TEAM;
  setup.seed = SEED;
  setup.durationMinutes = SHORT_MATCH_MINUTES;
  setup.pesStaminaConstants = SharedStaminaConstants();
  setup.animationDir = GF_SOURCE_DIR "/assets/gf_anims";
  setup.rig = SharedRig();
  setup.pesClockConstants = SharedClockConstants();
  setup.pesBallConstants = SharedBallConstants();
  setup.pesJudgeConstants = SharedJudgeConstants();
  setup.pesRngConstants = SharedRngConstants();
  setup.pesKeeperConstants = SharedKeeperConstants();
  setup.pesStaminaConstants = SharedStaminaConstants();
  setup.pesStoppageConstants = SharedStoppageConstants();
  for (int match = 0; match < 2; ++match) {
    Match m(new MatchData(HOME_TEAM, AWAY_TEAM), setup, Properties());
    std::vector<Player*> players;
    m.GetAllTeamPlayers(0, players);
    EXPECT_EQ(players.at(0)->GetID(), 0) << "match " << match;
  }
  SetDB(nullptr);
}

// Trailing late, the CPU manager makes a team press (AIManager::ChoosePhilosophy).
// With it switched off for that team, the set philosophy stands all match. The
// positive run keeps the negative one from passing vacuously.
bool EverPressesWhileTrailing(bool aiManagerRunsHome) {
  blunted::Database db;
  EXPECT_TRUE(db.LoadSqlScript(GF_SOURCE_DIR "/assets/fixtures/teams.sql"));
  SetDB(&db);
  MatchSetup setup;
  setup.homeTeamDbId = HOME_TEAM;
  setup.awayTeamDbId = AWAY_TEAM;
  setup.seed = SEED;
  setup.durationMinutes = 1;
  setup.animationDir = GF_SOURCE_DIR "/assets/gf_anims";
  setup.rig = SharedRig();
  setup.pesClockConstants = SharedClockConstants();
  setup.pesBallConstants = SharedBallConstants();
  setup.pesJudgeConstants = SharedJudgeConstants();
  setup.pesRngConstants = SharedRngConstants();
  setup.pesKeeperConstants = SharedKeeperConstants();
  setup.pesStaminaConstants = SharedStaminaConstants();
  setup.pesStoppageConstants = SharedStoppageConstants();
  setup.aiManagerRuns[0] = aiManagerRunsHome;
  bool pressed = false;
  {
    MatchData* data = new MatchData(HOME_TEAM, AWAY_TEAM);
    Match match(data, setup, Properties());
    // A deficit no 1-minute match can close: with PES's ball the home side scores
    // in this window, and a one-goal deficit ended the "trailing" precondition.
    constexpr int kAwayLead = 3;
    data->SetGoalCount(1, kAwayLead);
    while (!match.IsGameOver() && match.GetSimTime_ms() < kMaxSimTime_ms && !pressed) {
      match.Process();
      if (data->GetGoalCount(0) >= data->GetGoalCount(1)) break;  // no longer trailing
      pressed = match.GetTeam(0)->GetController()->GetPhilosophy() ==
                TeamPhilosophy::e_Philosophy_Gegenpressing;
    }
    EXPECT_LT(data->GetGoalCount(0), data->GetGoalCount(1)) << "home must still trail";
  }
  SetDB(nullptr);
  return pressed;
}

TEST(SimMatch, AIManagerSwitchGovernsTactics) {
  REQUIRE_RIG();
  EXPECT_TRUE(EverPressesWhileTrailing(true));
  EXPECT_FALSE(EverPressesWhileTrailing(false));
}

TEST(SimMatch, SameSeedPlaysTheSameMatch) {
  REQUIRE_RIG();
  const Outcome a = Play(SEED, SHORT_MATCH_MINUTES);
  const Outcome b = Play(SEED, SHORT_MATCH_MINUTES);
  ASSERT_TRUE(a.finished);
  EXPECT_EQ(a.hash, b.hash);
  EXPECT_EQ(a.report, b.report);
}

TEST(SimMatch, AnotherSeedPlaysAnotherMatch) {
  REQUIRE_RIG();
  EXPECT_NE(Play(SEED, SHORT_MATCH_MINUTES).hash, Play(SEED + 1, SHORT_MATCH_MINUTES).hash);
}

TEST(SimMatch, FullMatchRunsToTheWhistle) {
  REQUIRE_RIG();
  const Outcome o = Play(SEED, FULL_MATCH_MINUTES);
  ASSERT_TRUE(o.finished);
  EXPECT_NE(o.report.find("Full match complete: HOM " + std::to_string(o.goals[0]) + " - " +
                          std::to_string(o.goals[1]) + " AWA"),
            std::string::npos)
      << o.report;
  EXPECT_NE(o.report.find("[determinism] "), std::string::npos);
}
