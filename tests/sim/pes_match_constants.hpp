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
#include "onthepitch/pesdefshape.hpp"
#include "onthepitch/pesdefenderdecisions.hpp"
#include "onthepitch/pesjudge.hpp"
#include "onthepitch/pescontact.hpp"
#include "onthepitch/pesclips.hpp"
#include "onthepitch/pesdodge.hpp"
#include "onthepitch/pestackle.hpp"
#include "onthepitch/peskeeper.hpp"
#include "onthepitch/pesrandom.hpp"
#include "onthepitch/pessetpiecetaker.hpp"
#include "onthepitch/pessetpieceteam.hpp"
#include "onthepitch/pesstamina.hpp"
#include "onthepitch/pesstoppage.hpp"
#include "onthepitch/peskickerror.hpp"
#include "onthepitch/pesoffball.hpp"
#include "onthepitch/pesstrategy.hpp"
#include "onthepitch/pesteamconcepts.hpp"
#include "onthepitch/pessubs.hpp"
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
  std::optional<pes::KickErrorConstants> kickError;
  std::optional<pes::SetPieceTakerConstants> setPieceTaker;
  std::optional<pes::SetPieceTeamConstants> setPieceTeam;
  std::optional<pes::StaminaConstants> stamina;
  std::optional<pes::CarrierConstants> carrier;
  std::optional<pes::DefensiveShapeConstants> defensiveShape;
  std::optional<pes::DefenderDecisionConstants> defenderDecisions;
  std::optional<pes::ContactConstants> contact;
  std::optional<pes::PesClipSet> clips;
  std::optional<pes::DodgeConstants> dodge;
  std::optional<pes::Animetable> animetable;
  std::optional<pes::Mbinfo> mbinfo;
  std::optional<pes::TackleConstants> tackle;
  std::optional<pes::StrategyConstants> strategy;
  std::optional<pes::TeamConceptConstants> teamConcepts;
  std::optional<pes::SubstitutionConstants> substitutions;
  std::optional<pes::offball::OffBallConstants> offBall;
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
  c.kickError = pes::LoadKickErrorConstants(*exe, &err);
  if (mounted) c.setPieceTaker = pes::LoadSetPieceTakerConstants(vfs, *exe, &err);
  c.setPieceTeam = pes::LoadSetPieceTeamConstants(*exe, &err);
  c.stamina = pes::LoadStaminaConstants(*exe, &err);
  c.carrier = pes::LoadCarrierConstants(*exe, &err);
  c.defensiveShape = pes::LoadDefensiveShapeConstants(*exe, &err);
  if (mounted)
    c.defenderDecisions = pes::LoadDefenderDecisionConstants(*exe, vfs, &err);
  if (mounted) c.contact = pes::LoadContactConstants(*exe, vfs, &err);
  if (mounted) c.clips = pes::LoadPesClips(*exe, vfs, &err);
  if (mounted) c.dodge = pes::LoadDodgeConstants(*exe, &err);
  if (mounted) c.animetable = pes::LoadAnimetable(*exe, vfs, &err);
  if (mounted) c.mbinfo = pes::LoadMbinfo(vfs, &err);
  if (mounted) c.tackle = pes::LoadTackleConstants(*exe, &err);
    c.strategy = pes::LoadStrategyConstants(*exe, &err);
    c.substitutions = pes::LoadSubstitutionConstants(*exe, &err);
  if (mounted) c.teamConcepts = pes::LoadTeamConceptConstants(*exe, vfs, &err);
  if (mounted) c.offBall = pes::offball::LoadOffBallConstants(*exe, vfs, &err);
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
  fill(setup->pesKickErrorConstants, c.kickError);
  fill(setup->pesSetPieceTakerConstants, c.setPieceTaker);
  fill(setup->pesSetPieceTeamConstants, c.setPieceTeam);
  fill(setup->pesStaminaConstants, c.stamina);
  fill(setup->pesCarrierConstants, c.carrier);
  fill(setup->pesDefensiveShapeConstants, c.defensiveShape);
  fill(setup->pesDefenderDecisionConstants, c.defenderDecisions);
  fill(setup->pesContactConstants, c.contact);
  fill(setup->pesClips, c.clips);
  fill(setup->pesDodgeConstants, c.dodge);
  fill(setup->pesAnimetable, c.animetable);
  fill(setup->pesMbinfo, c.mbinfo);
  fill(setup->pesTackleConstants, c.tackle);
  fill(setup->pesStrategyConstants, c.strategy);
  fill(setup->pesTeamConceptConstants, c.teamConcepts);
  fill(setup->pesSubstitutionConstants, c.substitutions);
  fill(setup->pesOffBallConstants, c.offBall);
  return ok;
}

}  // namespace pestest
