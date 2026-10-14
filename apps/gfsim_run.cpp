// Plays one headless AI-vs-AI match and prints GameplayFootball's report.
//   gfsim_run --db-sql FILE --home ID --away ID --pes21-root DIR
//             [--seed N] [--minutes N] [--timescale X] [--anims DIR] [--set key=value]...
#include <cstdio>
#include <cstdlib>
#include <string>

#include "data/matchdata.hpp"
#include "onthepitch/match.hpp"
#include "onthepitch/matchreport.hpp"
#include "onthepitch/matchsetup.hpp"
#include "onthepitch/pesball.hpp"
#include "onthepitch/pessetpiecetaker.hpp"
#include "onthepitch/pessetpieceteam.hpp"
#include "onthepitch/pesstamina.hpp"
#include "onthepitch/pesstoppage.hpp"
#include "onthepitch/pesclock.hpp"
#include "pes/exe.hpp"
#include "pes/vfs.hpp"
#include "rig/rig.hpp"
#include "simcontext.hpp"

namespace {

constexpr unsigned int DEFAULT_SEED = 1;
constexpr int DEFAULT_MINUTES = 20;    // GF's default match length in real minutes
constexpr float DEFAULT_TIMESCALE = 1.0f;
constexpr const char* DEFAULT_ANIMS = "assets/gf_anims";
constexpr int EXIT_USAGE = 2;

int Usage(const char* why) {
  std::fprintf(stderr, "%s\nusage: gfsim_run --db-sql FILE --home ID --away ID --pes21-root DIR "
                       "[--seed N] [--minutes N] [--timescale X] [--anims DIR] [--set key=value]...\n", why);
  return EXIT_USAGE;
}

}  // namespace

int main(int argc, char** argv) {
  std::string dbSql, root, anims = DEFAULT_ANIMS;
  if (const char* env = std::getenv("PES21_ROOT")) root = env;
  int home = 0, away = 0, minutes = DEFAULT_MINUTES;
  unsigned int seed = DEFAULT_SEED;
  float timeScale = DEFAULT_TIMESCALE;
  Properties config;
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    if (i + 1 >= argc) return Usage(("missing value for " + a).c_str());
    const std::string v = argv[++i];
    if (a == "--db-sql") dbSql = v;
    else if (a == "--home") home = std::atoi(v.c_str());
    else if (a == "--away") away = std::atoi(v.c_str());
    else if (a == "--pes21-root") root = v;
    else if (a == "--seed") seed = unsigned(std::strtoul(v.c_str(), nullptr, 10));
    else if (a == "--minutes") minutes = std::atoi(v.c_str());
    else if (a == "--timescale") timeScale = std::strtof(v.c_str(), nullptr);
    else if (a == "--anims") anims = v;
    else if (a == "--set") {
      const size_t eq = v.find('=');
      if (eq == std::string::npos) return Usage(("--set needs key=value: " + v).c_str());
      config.Set(v.substr(0, eq).c_str(), v.substr(eq + 1));
    } else {
      return Usage(("unknown argument " + a).c_str());
    }
  }
  if (root.empty()) return Usage("need --pes21-root (the player skeleton comes from your PES 2021 install)");
  if (dbSql.empty() || home <= 0 || away <= 0) return Usage("need --db-sql, --home and --away");

  std::string err;
  pes::PesVfs vfs;
  if (!vfs.Mount(root, &err)) return std::fprintf(stderr, "PES21 mount failed: %s\n", err.c_str()), 1;
  auto rig = pesrig::BuildRig(vfs, &err);
  if (!rig) return std::fprintf(stderr, "PES21 rig failed: %s\n", err.c_str()), 1;
  // PES 2021's match-clock constants, read from the install's own exe
  // (docs/re/pes-re-tactics-clock.md 7.2: every clock value lives in the image,
  // none in a WESYS object).
  auto exe = pes::PeImage::Open(root + "/PES2021.exe", &err);
  if (!exe) return std::fprintf(stderr, "PES2021.exe: %s\n", err.c_str()), 1;
  auto clockConstants = pes::LoadPesClockConstants(*exe, &err);
  if (!clockConstants) return std::fprintf(stderr, "%s\n", err.c_str()), 1;
  // PES 2021's ball physics: the `ball.o` record of constant_match.bin plus the
  // image values (docs/re/pes-re-ball-contact.md 6).
  auto ballConstants = pes::LoadPesBallConstants(*exe, vfs, &err);
  if (!ballConstants) return std::fprintf(stderr, "%s\n", err.c_str()), 1;
  auto judgeConstants = pes::LoadJudgeConstants(*exe, &err);
  if (!judgeConstants) return std::fprintf(stderr, "%s\n", err.c_str()), 1;
  auto rngConstants = pes::LoadRngConstants(*exe, &err);
  if (!rngConstants) return std::fprintf(stderr, "%s\n", err.c_str()), 1;
  // PES 2021's goalkeeper deflect point and post-catch distribution target
  // (docs/re/pes-re-keeper-port.md).
  auto keeperConstants = pes::LoadKeeperConstants(*exe, &err);
  if (!keeperConstants) return std::fprintf(stderr, "%s\n", err.c_str()), 1;
  // PES 2021's set-piece taker wait: how long the AI holds a restart before the
  // kicker takes it (docs/re/pes-re-setpiece-choreography.md 9 item 2).
  auto setPieceTakerConstants = pes::LoadSetPieceTakerConstants(vfs, *exe, &err);
  if (!setPieceTakerConstants) return std::fprintf(stderr, "%s\n", err.c_str()), 1;
  // PES 2021's stamina model (docs/re/pes-re-stamina-drain.md): the tank, the
  // drain, the breath, the break bonus and the injury roll's constants, all
  // read from the install's own exe.
  auto staminaConstants = pes::LoadStaminaConstants(*exe, &err);
  if (!staminaConstants) return std::fprintf(stderr, "%s\n", err.c_str()), 1;
  auto stoppageConstants = pes::LoadStoppageConstants(*exe, &err);
  if (!stoppageConstants) return std::fprintf(stderr, "%s\n", err.c_str()), 1;
  // PES 2021's ball-carrier decision layer (docs/re/pes-re-carrier-ai.md).
  auto carrierConstants = pes::LoadCarrierConstants(*exe, &err);
  if (!carrierConstants) return std::fprintf(stderr, "%s\n", err.c_str()), 1;
  auto setPieceTeamConstants = pes::LoadSetPieceTeamConstants(*exe, &err);
  if (!setPieceTeamConstants) return std::fprintf(stderr, "%s\n", err.c_str()), 1;
  // PES 2021's open-play defensive shape (docs/re/pes-re-defensive-shape.md 6, 7, 14).
  auto defensiveShapeConstants = pes::LoadDefensiveShapeConstants(*exe, &err);
  if (!defensiveShapeConstants) return std::fprintf(stderr, "%s\n", err.c_str()), 1;
  // PES 2021's engaging-defender decisions (docs/re/pes-re-defender-decisions.md):
  // the reaction table and its gates, .rdata plus two json records out of the
  // install's constant files.
  auto defenderDecisionConstants = pes::LoadDefenderDecisionConstants(*exe, vfs, &err);
  if (!defenderDecisionConstants) return std::fprintf(stderr, "%s\n", err.c_str()), 1;
  // PES 2021's CPU strategy decider (docs/re/pes-re-match-management.md 1).
  auto strategyConstants = pes::LoadStrategyConstants(*exe, &err);
  if (!strategyConstants) return std::fprintf(stderr, "%s\n", err.c_str()), 1;

  blunted::Database db;
  if (!db.LoadSqlScript(dbSql)) return std::fprintf(stderr, "cannot load %s\n", dbSql.c_str()), 1;
  SetDB(&db);

  MatchSetup setup;
  setup.homeTeamDbId = home;
  setup.awayTeamDbId = away;
  setup.seed = seed;
  setup.durationMinutes = minutes;
  setup.timeScale = timeScale;
  setup.animationDir = anims;
  setup.rig = &*rig;
  setup.pesClockConstants = &*clockConstants;
  setup.pesBallConstants = &*ballConstants;
  setup.pesJudgeConstants = &*judgeConstants;
  setup.pesRngConstants = &*rngConstants;
  setup.pesKeeperConstants = &*keeperConstants;
  setup.pesSetPieceTakerConstants = &*setPieceTakerConstants;
  setup.pesSetPieceTeamConstants = &*setPieceTeamConstants;
  setup.pesStaminaConstants = &*staminaConstants;
  setup.pesStoppageConstants = &*stoppageConstants;
  setup.pesCarrierConstants = &*carrierConstants;
  setup.pesDefensiveShapeConstants = &*defensiveShapeConstants;
  setup.pesDefenderDecisionConstants = &*defenderDecisionConstants;
  setup.pesStrategyConstants = &*strategyConstants;

  Match match(new MatchData(home, away), setup, config);  // Match owns the MatchData
  while (!match.IsGameOver()) match.Process();
  PrintMatchReport(match, stdout);
  return 0;
}
