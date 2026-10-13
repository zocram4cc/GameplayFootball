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

  Match match(new MatchData(home, away), setup, config);  // Match owns the MatchData
  while (!match.IsGameOver()) match.Process();
  PrintMatchReport(match, stdout);
  return 0;
}
