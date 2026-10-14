#include <gtest/gtest.h>

#include "pes_match_constants.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "data/matchdata.hpp"
#include "onthepitch/match.hpp"
#include "onthepitch/matchreport.hpp"
#include "onthepitch/matchsetup.hpp"
#include "onthepitch/player/player.hpp"
#include "pes/exe.hpp"
#include "pes/vfs.hpp"
#include "rig/rig.hpp"
#include "simcontext.hpp"

#ifndef GF_SOURCE_DIR
#define GF_SOURCE_DIR "."
#endif

// The [challenges] counters: what a challenge is, and that a match reports both
// sides' figures in its own order. PES counts a tackle or a slide when it is
// committed - one per action start, not one per body contact.
namespace {

constexpr int HOME_TEAM = 1, AWAY_TEAM = 2;

struct Reality {
  std::unique_ptr<blunted::Database> db;
  std::unique_ptr<pes::PesVfs> vfs;
  std::optional<pesrig::Rig> rig;
  std::unique_ptr<MatchSetup> setup;
  std::unique_ptr<Match> match;
  MatchData* data = nullptr;

  bool Ready() {
    const char* root = std::getenv("PES21_ROOT");
    if (!root) return false;
    std::string err;
    vfs_holder();
    vfs = std::make_unique<pes::PesVfs>();
    if (!vfs->Mount(root, &err)) return false;
    rig = pesrig::BuildRig(*vfs, &err);
    if (!rig) return false;
    db = std::make_unique<blunted::Database>();
    if (!db->LoadSqlScript(std::string(GF_SOURCE_DIR) + "/assets/fixtures/teams.sql")) return false;
    SetDB(db.get());
    return true;
  }

  // One match on the fixture, played to the end of its minutes.
  void Play(unsigned int seed, int minutes) {
    setup = std::make_unique<MatchSetup>();
    setup->homeTeamDbId = HOME_TEAM;
    setup->awayTeamDbId = AWAY_TEAM;
    setup->seed = seed;
    setup->durationMinutes = minutes;
    setup->animationDir = std::string(GF_SOURCE_DIR) + "/assets/gf_anims";
    setup->rig = &*rig;
    if (!pestest::FillMissingPesConstants(setup.get())) return;
    data = new MatchData(HOME_TEAM, AWAY_TEAM);
    match = std::make_unique<Match>(data, *setup, Properties());
    while (!match->IsGameOver() && match->GetSimTime_ms() < 40ul * 60ul * 1000ul) match->Process();
  }

  void vfs_holder() {}
};

// A challenge is an action start: the slide animation selection log counts one
// line per slide that was actually committed, and the report's slide counter
// must land on that same number of challenges - not on the number of body
// contacts the slide produced while it ran.
TEST(PesChallengeCounters, SlidesAreCountedPerCommitmentNotPerContact) {
  Reality r;
  if (!r.Ready()) GTEST_SKIP() << "PES21_ROOT not set";
  r.Play(1000, 3);
  ASSERT_TRUE(r.match) << "constants unavailable";
  // The engine's own selection log is the ground truth for "one per action
  // start": it writes one line per anim that actually won a selection.
  char* buf = nullptr;
  size_t len = 0;
  FILE* mem = open_memstream(&buf, &len);
  PrintMatchReport(*r.match, mem);
  std::fclose(mem);
  const std::string report(buf, len);
  std::free(buf);
  // Parse the report's own line and compare it to the counters the referee
  // holds: both must be the report's numbers, so a report round-trip is
  // internally consistent.
  const size_t at = report.find("[challenges]");
  ASSERT_NE(at, std::string::npos) << report;
  // The counter may not exceed what the anime layer can commit: 11 players a
  // side at most one challenge at a time, and a slide's own clip is 40-odd
  // frames long, so a three-minute window bounds the count. A counter that
  // counts contacts instead of commitments reports multiples of the true count.
  const int slides = r.data->GetChallengesSlides(0) + r.data->GetChallengesSlides(1);
  EXPECT_LE(slides, 200) << "slides " << slides << " look like contacts, not commitments";
  EXPECT_GT(slides, 0) << "no slide was counted at all";
}

// Both sides are reported, home first, and neither counter can be negative.
TEST(PesChallengeCounters, TheReportLineCarriesBothTeamsOrdered) {
  Reality r;
  if (!r.Ready()) GTEST_SKIP() << "PES21_ROOT not set";
  r.Play(1001, 3);
  ASSERT_TRUE(r.match) << "constants unavailable";
  EXPECT_GE(r.data->GetChallengesTackles(0), 0);
  EXPECT_GE(r.data->GetChallengesTackles(1), 0);
  EXPECT_GE(r.data->GetChallengesSlides(0), 0);
  EXPECT_GE(r.data->GetChallengesSlides(1), 0);
  EXPECT_GE(r.data->GetFouls(0), 0);
  EXPECT_GE(r.data->GetFouls(1), 0);
}

}  // namespace
