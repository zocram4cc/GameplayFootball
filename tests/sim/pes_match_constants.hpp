#pragma once

// Every PES constants struct a Match requires, loaded once per test binary from
// the install under PES21_ROOT. A test that builds a MatchSetup sets the
// constants it cares about and calls FillMissingPesConstants for the rest, so a
// port that adds a new required struct extends this one function instead of
// every test file. Returns false (the caller skips) when the install lacks any.

#include <cstdlib>
#include <optional>
#include <string>

#include "onthepitch/matchsetup.hpp"
#include "onthepitch/pesball.hpp"
#include "onthepitch/pescarrier.hpp"
#include "onthepitch/pesclock.hpp"
#include "onthepitch/pesjudge.hpp"
#include "onthepitch/peskeeper.hpp"
#include "onthepitch/pesrandom.hpp"
#include "onthepitch/pessetpiecetaker.hpp"
#include "onthepitch/pessetpieceteam.hpp"
#include "onthepitch/pesstamina.hpp"
#include "onthepitch/pesstoppage.hpp"
#include "pes/exe.hpp"
#include "pes/vfs.hpp"

namespace pestest {

struct PesMatchConstants {
  std::optional<pes::PesClockConstants> clock;
  std::optional<pes::PesBallConstants> ball;
  std::optional<pes::JudgeConstants> judge;
  std::optional<pes::RngConstants> rng;
  std::optional<pes::KeeperConstants> keeper;
  std::optional<pes::StoppageConstants> stoppage;
  std::optional<pes::SetPieceTakerConstants> setPieceTaker;
  std::optional<pes::SetPieceTeamConstants> setPieceTeam;
  std::optional<pes::StaminaConstants> stamina;
  std::optional<pes::CarrierConstants> carrier;
};

inline const PesMatchConstants& SharedPesMatchConstants() {
  static PesMatchConstants c;
  static bool tried = false;
  if (tried) return c;
  tried = true;
  const char* root = std::getenv("PES21_ROOT");
  if (!root) return c;
  std::string err;
  auto exe = pes::PeImage::Open(std::string(root) + "/PES2021.exe", &err);
  if (!exe) return c;
  pes::PesVfs vfs;
  const bool mounted = vfs.Mount(root, &err);
  c.clock = pes::LoadPesClockConstants(*exe, &err);
  if (mounted) c.ball = pes::LoadPesBallConstants(*exe, vfs, &err);
  c.judge = pes::LoadJudgeConstants(*exe, &err);
  c.rng = pes::LoadRngConstants(*exe, &err);
  c.keeper = pes::LoadKeeperConstants(*exe, &err);
  c.stoppage = pes::LoadStoppageConstants(*exe, &err);
  if (mounted) c.setPieceTaker = pes::LoadSetPieceTakerConstants(vfs, *exe, &err);
  c.setPieceTeam = pes::LoadSetPieceTeamConstants(*exe, &err);
  c.stamina = pes::LoadStaminaConstants(*exe, &err);
  c.carrier = pes::LoadCarrierConstants(*exe, &err);
  return c;
}

// Fills every constants pointer the caller left null. False when one of them is
// still missing afterwards (no install, or the install lacks it).
inline bool FillMissingPesConstants(MatchSetup* setup) {
  const PesMatchConstants& c = SharedPesMatchConstants();
  bool ok = true;
  auto fill = [&ok](auto*& slot, const auto& loaded) {
    if (!slot && loaded) slot = &*loaded;
    ok &= slot != nullptr;
  };
  fill(setup->pesClockConstants, c.clock);
  fill(setup->pesBallConstants, c.ball);
  fill(setup->pesJudgeConstants, c.judge);
  fill(setup->pesRngConstants, c.rng);
  fill(setup->pesKeeperConstants, c.keeper);
  fill(setup->pesStoppageConstants, c.stoppage);
  fill(setup->pesSetPieceTakerConstants, c.setPieceTaker);
  fill(setup->pesSetPieceTeamConstants, c.setPieceTeam);
  fill(setup->pesStaminaConstants, c.stamina);
  fill(setup->pesCarrierConstants, c.carrier);
  return ok;
}

}  // namespace pestest
