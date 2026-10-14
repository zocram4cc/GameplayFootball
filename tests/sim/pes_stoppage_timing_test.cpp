// PES 2021's stoppage timings (docs/re/pes-re-foul-choreography.md 9): the foul
// hold, the out-of-play hold (offside included), the +2.5 s when a player is
// down, and the whole stop-to-restart bracket.
//
// The bracket's values come from the user's own PES2021.exe; the numbers a
// synthetic set of constants produce here pin the arithmetic, and the install
// test checks the loader against the exe through relations, not transcribed
// values.

#include <gtest/gtest.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "onthepitch/foulsequence.hpp"
#include "onthepitch/match.hpp"
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

namespace {

constexpr int HOME_TEAM = 1, AWAY_TEAM = 2;
constexpr unsigned int SEED = 1000;
constexpr int SHORT_MATCH_MINUTES = 2;
constexpr unsigned long kMaxSimTime_ms = 30ul * 60ul * 1000ul;

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

const pes::CarrierConstants* SharedCarrierConstants() {
  static std::optional<pes::CarrierConstants> constants;
  static bool tried = false;
  if (!tried) {
    tried = true;
    if (const char* root = std::getenv("PES21_ROOT")) {
      std::string err;
      if (auto exe = pes::PeImage::Open(std::string(root) + "/PES2021.exe", &err))
        constants = pes::LoadCarrierConstants(*exe, &err);
    }
  }
  return constants ? &*constants : nullptr;
}

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

const pes::PesClockConstants* SharedClockConstants() {
  static std::optional<pes::PesClockConstants> constants;
  static bool tried = false;
  if (!tried) {
    tried = true;
    if (const char* root = std::getenv("PES21_ROOT")) {
      std::string err;
      if (auto exe = pes::PeImage::Open(std::string(root) + "/PES2021.exe", &err))
        constants = pes::LoadPesClockConstants(*exe, &err);
    }
  }
  return constants ? &*constants : nullptr;
}

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

// PES 2021's set-piece taker wait (docs/re/pes-re-setpiece-choreography.md 9
// item 2): the corner/free-kick/penalty gates read from the exe.
const pes::SetPieceTakerConstants* SharedSetPieceTakerConstants() {
  static std::optional<pes::SetPieceTakerConstants> constants;
  static bool tried = false;
  if (!tried) {
    tried = true;
    if (const char* root = std::getenv("PES21_ROOT")) {
      pes::PesVfs vfs;
      std::string err;
      if (vfs.Mount(root, &err)) {
        if (auto exe = pes::PeImage::Open(std::string(root) + "/PES2021.exe", &err))
          constants = pes::LoadSetPieceTakerConstants(vfs, *exe, &err);
      }
    }
  }
  return constants ? &*constants : nullptr;
}

#define REQUIRE_RIG()                                                                    \
  if (!SharedRig()) GTEST_SKIP() << "PES21_ROOT not set: the player skeleton comes from PES"; \
  if (!SharedStoppageConstants()) GTEST_SKIP() << "PES2021.exe: stoppage constants unavailable"; \
  if (!SharedCarrierConstants()) GTEST_SKIP() << "PES2021.exe: carrier constants unavailable"; \
  if (!SharedSetPieceTakerConstants()) \
    GTEST_SKIP() << "PES2021.exe: set-piece taker constants unavailable"

// A synthetic set: the PES values in disguise, so the arithmetic is pinned
// without the install. 5.0 foul hold, 2.5 out-of-play hold, +2.5 down.
pes::StoppageConstants Synthetic() {
  pes::StoppageConstants k{};
  k.holdKind1_s = 5.0f;
  k.holdKind5_s = 2.5f;
  k.downedPlayerExtra_s = 2.5f;
  k.offsideHold_s = 3.0f;
  k.wipe_s = 0.1f;
  k.fadeIn_s = 1.0f;
  k.restartTail_s = 0.25f;
  k.fadeHalf_s = 0.5f;
  k.sceneRefereeRange_m = 5.0f;
  k.sceneExciteRange_m = 2.0f;
  k.judgeApproachSq_m2 = 16.0f;
  k.sceneGate_s = 3.0f;
  return k;
}

constexpr int kFoulHoldKind = 1;
constexpr int kOutOfPlayHoldKind = 5;

}  // namespace

TEST(PesStoppageTiming, AFoulHoldsFiveSeconds) {
  const auto k = Synthetic();
  // docs/re/pes-re-foul-choreography.md 9.2: 272 live frames = 5.04 s, no scene.
  EXPECT_EQ(FoulSequence::HoldEndsAt_ms(0, k, false, kFoulHoldKind) - 0, 5000u);
}

TEST(PesStoppageTiming, AnOutOfPlayStopHoldsTwentyFiveHundredths) {
  const auto k = Synthetic();
  // 9.3: live 135-140 frames = 2.5 s for every 0xb stop, offside included.
  EXPECT_EQ(FoulSequence::HoldEndsAt_ms(0, k, false, kOutOfPlayHoldKind) - 0, 2500);
  EXPECT_EQ(FoulSequence::HoldEndsAt_ms(0, k, true, kOutOfPlayHoldKind) - 0, 5000);
}

TEST(PesStoppageTiming, APlayerDownAddsTwentyFiveHundredths) {
  const auto k = Synthetic();
  // 9.3: base + 2.5 s (0x14259bde4) while FUN_1404441d0 reports a man down.
  const unsigned long upright = FoulSequence::HoldEndsAt_ms(0, k, false, kOutOfPlayHoldKind);
  const unsigned long down = FoulSequence::HoldEndsAt_ms(0, k, true, kOutOfPlayHoldKind);
  EXPECT_EQ(down - upright, 2500u);
}

TEST(PesStoppageTiming, TheWholeBracketMatchesTheLiveStop) {
  const auto k = Synthetic();
  // 9.6's measured ordinary foul: stop -> armed 407 frames = 7.54 s, of which
  // the hold is 272, the wipe + UI 16, the teleport 40, the fade-in 59 and the
  // tail 18. GF models hold (5.0 s) + wipe (0.1 s) as the prepare, and keeps
  // PES's teleport + UI + fade + tail as the take-after-prepare, so its whole
  // bracket is 7.33 s against PES's 7.54 s - same stop, the presentation beats
  // this engine does not play folded into its own numbers.
  EXPECT_EQ(FoulSequence::RestartPrepareAt_ms(0, k, false, kFoulHoldKind) - 0, 5100);
  const unsigned long take = FoulSequence::RestartTakeAt_ms(0, k, false, kFoulHoldKind);
  EXPECT_EQ(take, 7330u);
  EXPECT_LT(take - 0, 7540u);  // PES's own 7.54 s
}

TEST(PesStoppageTiming, OffsideHoldsTwentyFiveHundredthsNotPesSevenSeconds) {
  const auto k = Synthetic();
  // 9.9 killed "offside hold = 3.0 s": 3.0 s is only for `MI+0x1588 == 1`,
  // which this engine never produces. The measured 275-frame offside stop is
  // a 2.5 s hold plus the restart tail, not the seven seconds GF ran.
  const unsigned long offside = FoulSequence::RestartTakeAt_ms(0, k, false, kOutOfPlayHoldKind);
  EXPECT_EQ(offside, 4830u);
  EXPECT_LT(offside, 7000u);
}

TEST(PesStoppageTiming, AScenceGateIsThreeSeconds) {
  const auto k = Synthetic();
  // 9.2: `fps*3/env+0x5c` frames, the 3x compiled into address arithmetic.
  EXPECT_NEAR(k.sceneGate_s, 3.0f, 1e-6);
  EXPECT_NEAR(std::sqrt(k.judgeApproachSq_m2), 4.0f, 1e-5);
}

TEST(PesStoppageTiming, ScheduleIsSelfConsistent) {
  const auto k = Synthetic();
  EXPECT_TRUE(FoulSequence::ScheduleIsConsistent());
  EXPECT_GT(FoulSequence::RestartPrepareAt_ms(0, k, false, kFoulHoldKind),
            FoulSequence::HoldEndsAt_ms(0, k, false, kFoulHoldKind));
  EXPECT_GT(FoulSequence::RestartTakeAt_ms(0, k, false, kFoulHoldKind),
            FoulSequence::RestartPrepareAt_ms(0, k, false, kFoulHoldKind));
}

// The install test: the values the loader reads, checked against the report's
// relations rather than transcribed numbers, so a different build of the game
// (different speeds) is caught by its own arithmetic instead of a pin.
TEST(PesStoppageInstall, LoadsFromTheExe) {
  const char* root = std::getenv("PES21_ROOT");
  if (!root) GTEST_SKIP() << "PES21_ROOT not set";
  std::string err;
  auto exe = pes::PeImage::Open(std::string(root) + "/PES2021.exe", &err);
  ASSERT_TRUE(exe) << err;
  auto k = pes::LoadStoppageConstants(*exe, &err);
  ASSERT_TRUE(k) << err;
  // A foul holds longer than an out-of-play stop (5.0 vs 2.5 s).
  EXPECT_GT(k->holdKind1_s, k->holdKind5_s);
  // A player down costs the same again: PES's +2.5 s is the table[5] value.
  EXPECT_NEAR(k->downedPlayerExtra_s, k->holdKind5_s, 1e-5);
  // The wipe is the shortest beat and the fade-in the longest of the tail.
  EXPECT_GT(k->fadeIn_s, k->restartTail_s);
  EXPECT_GT(k->restartTail_s, k->wipe_s);
  // The offside hold is an instruction immediate: 3.0 s, not PES's table[5].
  EXPECT_GT(k->offsideHold_s, k->holdKind5_s);
  EXPECT_NEAR(k->offsideHold_s, 3.0f, 1e-5);
  // The referee approaches to within 4 m (d^2 < 16.0) and no closer.
  EXPECT_NEAR(std::sqrt(k->judgeApproachSq_m2), 4.0f, 0.01);
  // The scene's referee range is the 5 m its gate waits for.
  EXPECT_GT(k->sceneRefereeRange_m, k->sceneExciteRange_m);
}

// The whole stop, measured inside a live match: the bracket the referee
// schedules for a stop (stop -> prepare -> take) is PES's, not GF's old 15 s.
// The stop's hold, wipe, fade-in and tail are all of it.
struct Stop {
  unsigned long stop, prepare, take;
  e_SetPiece kind;
};

std::vector<Stop> MeasureStops(int minutes) {
  std::vector<Stop> stops;
  blunted::Database db;
  EXPECT_TRUE(db.LoadSqlScript(GF_SOURCE_DIR "/assets/fixtures/teams.sql"));
  SetDB(&db);
  MatchSetup setup;
  setup.homeTeamDbId = HOME_TEAM;
  setup.awayTeamDbId = AWAY_TEAM;
  setup.seed = SEED;
  setup.durationMinutes = minutes;
  setup.animationDir = GF_SOURCE_DIR "/assets/gf_anims";
  setup.rig = SharedRig();
  setup.pesClockConstants = SharedClockConstants();
  setup.pesBallConstants = SharedBallConstants();
  setup.pesJudgeConstants = SharedJudgeConstants();
  setup.pesRngConstants = SharedRngConstants();
  setup.pesKeeperConstants = SharedKeeperConstants();
  setup.pesSetPieceTakerConstants = SharedSetPieceTakerConstants();
  setup.pesStoppageConstants = SharedStoppageConstants();
  setup.pesCarrierConstants = SharedCarrierConstants();
  setup.pesStaminaConstants = SharedStaminaConstants();

  Match match(new MatchData(HOME_TEAM, AWAY_TEAM), setup, Properties());
  // The referee arms a stop once, so a buffer seen active with a fresh stopTime
  // is a new stop; record the moment its stages pass.
  unsigned long lastStop = 0;
  while (!match.IsGameOver() && match.GetSimTime_ms() < kMaxSimTime_ms) {
    match.Process();
    const RefereeBuffer& b = match.GetReferee()->GetBuffer();
    if (b.active && b.stopTime != 0 && b.stopTime != lastStop && b.prepareTime > b.stopTime) {
      lastStop = b.stopTime;
      stops.push_back({b.stopTime, b.prepareTime, b.startTime, b.desiredSetPiece});
    }
  }
  SetDB(nullptr);
  return stops;
}

TEST(PesStoppageTiming, AMeasuredStopRunsPesBracketNotFifteenSeconds) {
  REQUIRE_RIG();
  const auto stops = MeasureStops(SHORT_MATCH_MINUTES);
  ASSERT_FALSE(stops.empty()) << "no stop was staged in a live match";
  // The two brackets this port produces, with PES's late-card ceremony (9.5:
  // the step-6 scene, 3.0 s) on top where a card is due. Everything else is a
  // stop that GF's old 15 s schedule would have called a stoppage.
  constexpr unsigned long kOutOfPlayTake_ms = 4830;  // hold 2.5 + wipe/tele/fade/tail
  constexpr unsigned long kFoulTake_ms = 7330;       // hold 5.0 + the same
  constexpr unsigned long kLateCardOnScreen_ms = 3000;
  int measured = 0;
  for (const Stop& s : stops) {
    // A kick-off is not a stop this report times: PES's bracket is the foul and
    // out-of-play stops, and a goal's celebration beats (which hold the kick-off
    // back) are the next plan's business. Everything else the whistle stops is.
    if (s.kind == e_SetPiece_KickOff) continue;
    const unsigned long takeDelay = s.take - s.stop;
    ++measured;
    std::fprintf(stderr, "[stoppage] kind %d stop %lu take %lu ms\n", (int)s.kind, s.stop, takeDelay);
    // PES's whole stop is 7.54 s (407 frames) with no scene; a late card adds
    // its own 3.0 s scene on top. GF's old schedule put the restart 15 s out.
    // A player down adds PES's +2.5 s to the hold (9.2); with the stamina model
    // in, knocks happen, so every bracket may carry it.
    const unsigned long downed_ms =
        static_cast<unsigned long>(std::lround(SharedStoppageConstants()->downedPlayerExtra_s * 1000.0f));
    bool pesBracket = false;
    for (unsigned long base : {kOutOfPlayTake_ms, kFoulTake_ms})
      for (unsigned long down : {0ul, downed_ms})
        for (unsigned long card : {0ul, kLateCardOnScreen_ms})
          pesBracket |= takeDelay == base + down + card;
    EXPECT_TRUE(pesBracket)
        << "stop at " << s.stop << " (set piece " << (int)s.kind << ") ran " << takeDelay
        << " ms to the take, which is neither PES's out-of-play bracket ("
        << kOutOfPlayTake_ms << "), its foul bracket (" << kFoulTake_ms
        << "), with or without a downed player (+2.5 s) and the late-card ceremony ("
        << kFoulTake_ms + kLateCardOnScreen_ms << ")";
  }
  EXPECT_GT(measured, 0) << "no foul or out-of-play stop was staged in a live match";
}

// The late-card window: the card is on screen 3.0 s into the stop, not six
// seconds after the whistle as GF had it. That is a behaviour of the referee's
// own path, checked through the constants the report maps onto it (9.5/9.8).
TEST(PesStoppageTiming, LateCardIsOnScreenThreeSecondsIntoTheStop) {
  const auto k = Synthetic();
  constexpr unsigned long kCardOnScreen_ms = 3000;
  EXPECT_LT(kCardOnScreen_ms, FoulSequence::HoldEndsAt_ms(0, k, false, kFoulHoldKind));
  EXPECT_NEAR(k.sceneGate_s * 1000.0f, kCardOnScreen_ms, 1.0);
}
