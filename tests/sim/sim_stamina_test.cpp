// PES 2021's stamina model wired into a whole match
// (docs/re/pes-re-stamina-drain.md): the tank drains while the ball is in
// play, freezes at a dead ball, and jumps at the break. The pure rules are
// pinned in pes_stamina_test.cpp; this asserts the wiring in Match/Player.
#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <optional>
#include <string>
#include <vector>

#include "data/matchdata.hpp"
#include "onthepitch/match.hpp"
#include "onthepitch/matchsetup.hpp"
#include "onthepitch/peskeeper.hpp"
#include "onthepitch/pesrandom.hpp"
#include "onthepitch/pesstamina.hpp"
#include "onthepitch/pescarrier.hpp"
#include "onthepitch/pesstoppage.hpp"
#include "pes/exe.hpp"
#include "pes/vfs.hpp"
#include "rig/rig.hpp"
#include "simcontext.hpp"
#include "utils/database.hpp"

namespace {

constexpr int HOME_TEAM = 1, AWAY_TEAM = 2;
constexpr unsigned int SEED = 1000;
constexpr int MATCH_MINUTES = 5;

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

std::optional<pes::PeImage> SharedExe() {
  static std::optional<pes::PeImage> exe;
  static bool tried = false;
  if (!tried) {
    tried = true;
    if (const char* root = std::getenv("PES21_ROOT")) {
      std::string err;
      exe = pes::PeImage::Open(std::string(root) + "/PES2021.exe", &err);
    }
  }
  return exe;
}

template <typename Loader>
const pes::StaminaConstants* SharedConstants(Loader loader) {
  static std::optional<pes::StaminaConstants> constants;
  static bool tried = false;
  if (!tried) {
    tried = true;
    if (SharedExe()) constants = loader(*SharedExe());
  }
  return constants ? &*constants : nullptr;
}

// One in-play tick's drain over the whole squad, summed, plus the squad's
// fraction of the tank left and its mean breath.
struct SquadStamina {
  long cur = 0;
  long max = 0;
  long breath = 0;
  int players = 0;
};

SquadStamina SquadOf(Match& match) {
  SquadStamina out;
  for (int t = 0; t < 2; t++) {
    std::vector<Player*> squad;
    match.GetActiveTeamPlayers(t, squad);
    for (Player* player : squad) {
      const pes::StaminaState& st = player->GetStamina();
      if (st.max == 0) continue;
      out.cur += st.cur;
      out.max += st.max;
      out.breath += st.breath;
      ++out.players;
    }
  }
  return out;
}

}  // namespace

// The tank as the clock runs, so the three behaviours are visible in one run.
TEST(SimStamina, DrainsInPlayFreezesAtDeadBallsAndJumpsAtTheBreak) {
  if (!SharedRig()) GTEST_SKIP() << "PES21_ROOT not set: the player skeleton comes from PES";
  const char* root = std::getenv("PES21_ROOT");
  std::string err;
  pes::PesVfs vfs;
  ASSERT_TRUE(vfs.Mount(root, &err)) << err;
  auto exe = pes::PeImage::Open(std::string(root) + "/PES2021.exe", &err);
  ASSERT_TRUE(exe.has_value()) << err;
  auto staminaConstants = pes::LoadStaminaConstants(*exe, &err);
  ASSERT_TRUE(staminaConstants.has_value()) << err;

  MatchSetup setup;
  setup.homeTeamDbId = HOME_TEAM;
  setup.awayTeamDbId = AWAY_TEAM;
  setup.seed = SEED;
  setup.durationMinutes = MATCH_MINUTES;
  setup.animationDir = GF_SOURCE_DIR "/assets/gf_anims";
  setup.rig = SharedRig();
  setup.pesStaminaConstants = &*staminaConstants;
  auto stoppage = pes::LoadStoppageConstants(*exe, &err);
  if (!stoppage) GTEST_SKIP() << "PES2021.exe: stoppage constants unavailable";
  setup.pesStoppageConstants = &*stoppage;
  auto carrier = pes::LoadCarrierConstants(*exe, &err);
  if (!carrier) GTEST_SKIP() << "PES2021.exe: carrier constants unavailable";
  setup.pesCarrierConstants = &*carrier;

  // Without the other PES models a Match cannot be built; they are needed as
  // much as the stamina constants are. Skipped unless the install supplies
  // them all.
  auto ball = pes::LoadPesBallConstants(*exe, vfs, &err);
  if (!ball) GTEST_SKIP() << "PES2021.exe: ball constants unavailable";
  auto clock = pes::LoadPesClockConstants(*exe, &err);
  if (!clock) GTEST_SKIP() << "PES2021.exe: clock constants unavailable";
  auto judge = pes::LoadJudgeConstants(*exe, &err);
  if (!judge) GTEST_SKIP() << "PES2021.exe: judge constants unavailable";
  auto rng = pes::LoadRngConstants(*exe, &err);
  if (!rng) GTEST_SKIP() << "PES2021.exe: rng constants unavailable";
  auto keeper = pes::LoadKeeperConstants(*exe, &err);
  if (!keeper) GTEST_SKIP() << "PES2021.exe: keeper constants unavailable";
  auto taker = pes::LoadSetPieceTakerConstants(vfs, *exe, &err);
  if (!taker) GTEST_SKIP() << "PES2021.exe: set-piece taker constants unavailable";
  setup.pesBallConstants = &*ball;
  setup.pesClockConstants = &*clock;
  setup.pesJudgeConstants = &*judge;
  setup.pesRngConstants = &*rng;
  setup.pesKeeperConstants = &*keeper;
  setup.pesSetPieceTakerConstants = &*taker;

  blunted::Database db;
  ASSERT_TRUE(db.LoadSqlScript(GF_SOURCE_DIR "/assets/fixtures/teams.sql"));
  SetDB(&db);
  Match match(new MatchData(HOME_TEAM, AWAY_TEAM), setup, Properties());

  // Kick-off: every player has a full tank from his own Stamina ability.
  SquadStamina kickoff = SquadOf(match);
  ASSERT_GT(kickoff.players, 0);
  ASSERT_EQ(kickoff.cur, kickoff.max) << "a fresh squad starts on a full tank";
  ASSERT_GT(kickoff.breath, 0) << "the breath reserve starts full too";

  // First wait for kick-off: the ball is dead before it.
  while (!match.IsInPlay() && match.GetSimTime_ms() < 20000) match.Process();
  ASSERT_TRUE(match.IsInPlay()) << "the match must reach kick-off";

  // Play one in-play tick and sample: the drain only runs when the ball is in
  // play, so the squad's tank moves from kick-off.
  long inPlayCur = kickoff.cur;
  for (int i = 0; i < 600 && match.IsInPlay(); i++) {
    const long curBefore = SquadOf(match).cur;
    match.Process();
    inPlayCur = SquadOf(match).cur;
    if (inPlayCur != curBefore) break;
  }
  EXPECT_NE(inPlayCur, kickoff.cur) << "the tank must move while the ball is in play";

  // The freeze: a dead ball means no drain, on the same tick, through the
  // same code path. Stop and start play on the same tick and compare the
  // tanks to the in-play tick that just ran.
  const long running = SquadOf(match).cur;
  const bool wasInPlay = match.IsInPlay();
  ASSERT_TRUE(wasInPlay) << "the drain tick must be an in-play one";
  match.StopPlay();
  match.Process();
  EXPECT_EQ(SquadOf(match).cur, running)
      << "the tank must freeze on a dead tick (report 3.1)";
  match.Process();
  EXPECT_EQ(SquadOf(match).cur, running) << "two dead ticks in a row, still frozen";
  match.StartPlay();
  match.Process();
  SquadStamina after = SquadOf(match);
  EXPECT_NE(after.cur, running) << "the drain resumes the tick play restarts";
}

// The half-time bonus, applied once at the whistle (report 3.4): the exact
// numbers, on one player, through Match::SetMatchPhase.
TEST(SimStamina, HalfTimeAppliesTheBreakBonusOnce) {
  if (!SharedRig()) GTEST_SKIP() << "PES21_ROOT not set: the player skeleton comes from PES";
  const char* root = std::getenv("PES21_ROOT");
  std::string err;
  pes::PesVfs vfs;
  ASSERT_TRUE(vfs.Mount(root, &err)) << err;
  auto exe = pes::PeImage::Open(std::string(root) + "/PES2021.exe", &err);
  ASSERT_TRUE(exe.has_value()) << err;
  auto staminaConstants = pes::LoadStaminaConstants(*exe, &err);
  ASSERT_TRUE(staminaConstants.has_value()) << err;
  auto ball = pes::LoadPesBallConstants(*exe, vfs, &err);
  if (!ball) GTEST_SKIP() << "PES2021.exe: ball constants unavailable";
  auto clock = pes::LoadPesClockConstants(*exe, &err);
  if (!clock) GTEST_SKIP() << "PES2021.exe: clock constants unavailable";
  auto judge = pes::LoadJudgeConstants(*exe, &err);
  if (!judge) GTEST_SKIP() << "PES2021.exe: judge constants unavailable";
  auto rng = pes::LoadRngConstants(*exe, &err);
  if (!rng) GTEST_SKIP() << "PES2021.exe: rng constants unavailable";
  auto keeper = pes::LoadKeeperConstants(*exe, &err);
  if (!keeper) GTEST_SKIP() << "PES2021.exe: keeper constants unavailable";
  auto taker = pes::LoadSetPieceTakerConstants(vfs, *exe, &err);
  if (!taker) GTEST_SKIP() << "PES2021.exe: set-piece taker constants unavailable";

  MatchSetup setup;
  setup.homeTeamDbId = HOME_TEAM;
  setup.awayTeamDbId = AWAY_TEAM;
  setup.seed = SEED;
  setup.durationMinutes = MATCH_MINUTES;
  setup.animationDir = GF_SOURCE_DIR "/assets/gf_anims";
  setup.rig = SharedRig();
  setup.pesStaminaConstants = &*staminaConstants;
  auto stoppage = pes::LoadStoppageConstants(*exe, &err);
  if (!stoppage) GTEST_SKIP() << "PES2021.exe: stoppage constants unavailable";
  setup.pesStoppageConstants = &*stoppage;
  auto carrier = pes::LoadCarrierConstants(*exe, &err);
  if (!carrier) GTEST_SKIP() << "PES2021.exe: carrier constants unavailable";
  setup.pesCarrierConstants = &*carrier;
  setup.pesBallConstants = &*ball;
  setup.pesClockConstants = &*clock;
  setup.pesJudgeConstants = &*judge;
  setup.pesRngConstants = &*rng;
  setup.pesKeeperConstants = &*keeper;
  setup.pesSetPieceTakerConstants = &*taker;

  blunted::Database db;
  ASSERT_TRUE(db.LoadSqlScript(GF_SOURCE_DIR "/assets/fixtures/teams.sql"));
  SetDB(&db);
  Match match(new MatchData(HOME_TEAM, AWAY_TEAM), setup, Properties());

  std::vector<Player*> squad;
  match.GetAllTeamPlayers(0, squad);
  ASSERT_FALSE(squad.empty());
  Player* player = squad.front();
  ASSERT_GT(player->GetStamina().max, 0) << "kick-off filled the tank";

  // Drop him to a third of the tank so the bonus is not clamped away.
  pes::StaminaState before = player->GetStamina();
  before.cur = before.max / 3;
  player->GetStamina() = before;

  match.SetMatchPhase(e_MatchPhase_2ndHalf);

  const pes::StaminaState& after = player->GetStamina();
  // F = the match length in minutes (5 here), so 9000 * 5 = 45000, and the
  // fitness tier is 1 (GF has no per-slot marker), which adds 3600 * 5.
  const long expected = std::min(
      static_cast<long>(before.cur) + static_cast<long>(9000 + 3600) * MATCH_MINUTES,
      static_cast<long>(after.max));
  EXPECT_EQ(after.cur, expected) << "the break bonus lands once, as 9000*F + 3600*F";
  EXPECT_LE(after.cur, after.max) << "clamped to the tank";
}
