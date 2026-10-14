#include "gdext/pes_library.hpp"

#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include "gdext/pes_texture.hpp"
#include "pes/exe.hpp"
#include "pes/locomotion.hpp"

namespace godot {
namespace {
constexpr const char* EXE_NAME = "PES2021.exe";
}  // namespace

void PesLibrary::_bind_methods() {
  ClassDB::bind_method(D_METHOD("open", "root"), &PesLibrary::open);
  ClassDB::bind_method(D_METHOD("mount_report"), &PesLibrary::mount_report);
  ClassDB::bind_method(D_METHOD("list", "prefix"), &PesLibrary::list);
  ClassDB::bind_method(D_METHOD("load_edit_save", "path"), &PesLibrary::load_edit_save);
  ClassDB::bind_method(D_METHOD("load_texture", "path"), &PesLibrary::load_texture);
}

Error PesLibrary::open(const String& root) {
  std::string err;
  if (!vfs_.Mount(FromGd(root), &err)) {
    UtilityFunctions::push_error("PES21 mount failed: ", ToGd(err));
    return ERR_FILE_NOT_FOUND;
  }
  rig_ = pesrig::BuildRig(vfs_, &err);
  if (!rig_) {
    UtilityFunctions::push_error("PES21 rig failed: ", ToGd(err));
    return ERR_PARSE_ERROR;
  }
  auto file = vfs_.Read(pes::kPlayerAppearancePath, &err);
  auto table = file ? pes::ParsePlayerAppearanceFile(*file, &err) : std::nullopt;
  if (table) appearances_ = std::move(*table);
  else UtilityFunctions::push_warning("PES21 PlayerAppearance.bin unreadable: ", ToGd(err));
  // Hand rig. The motion name table lives in the game's executable.
  auto exe = pes::PeImage::Open(FromGd(root) + "/" + EXE_NAME, &err);
  if (exe) motionNames_ = pes::MotionNames(*exe, vfs_, &err);
  if (motionNames_) handSequences_ = pes::LoadHandSequences(vfs_, &*exe, &err);
  if (handSequences_) handPoses_ = pesrig::LoadHandPoses(vfs_, handSequences_->poses, &err);
  if (!handPoses_) UtilityFunctions::push_warning("PES21 hand rig unavailable, hands stay at rest: ", ToGd(err));
  if (exe) speedClasses_ = pes::SpeedClassLimits(*exe);
  if (exe) cameraParams_ = pes::LoadDynamicWide(*exe, vfs_, &err);
  // PES 2021's match-clock constants, from the same exe (docs/re/pes-re-tactics-clock.md
  // 7.2: every clock value lives in the image). Required by every GfMatch.
  if (exe) clockConstants_ = pes::LoadPesClockConstants(*exe, &err);
  if (!clockConstants_) {
    UtilityFunctions::push_error("PES21 match-clock constants unavailable: ", ToGd(err));
    return ERR_PARSE_ERROR;
  }
  // PES 2021's ball physics: the `ball.o` record of constant_match.bin plus the
  // image values (docs/re/pes-re-ball-contact.md 6).
  if (exe) ballConstants_ = pes::LoadPesBallConstants(*exe, vfs_, &err);
  if (!ballConstants_) {
    UtilityFunctions::push_error("PES21 ball physics constants unavailable: ", ToGd(err));
    return ERR_PARSE_ERROR;
  }
  if (exe) judgeConstants_ = pes::LoadJudgeConstants(*exe, &err);
  if (!judgeConstants_) {
    UtilityFunctions::push_error("PES21 foul-decision constants unavailable: ", ToGd(err));
    return ERR_PARSE_ERROR;
  }
  if (exe) rngConstants_ = pes::LoadRngConstants(*exe, &err);
  if (!rngConstants_) {
    UtilityFunctions::push_error("PES21 RNG constants unavailable: ", ToGd(err));
    return ERR_PARSE_ERROR;
  }
  // PES 2021's goalkeeper deflect point and post-catch distribution target
  // (docs/re/pes-re-keeper-port.md).
  if (exe) keeperConstants_ = pes::LoadKeeperConstants(*exe, &err);
  if (!keeperConstants_) {
    UtilityFunctions::push_error("PES21 keeper constants unavailable: ", ToGd(err));
    return ERR_PARSE_ERROR;
  }
  // PES 2021's stamina, fatigue and injury model
  // (docs/re/pes-re-stamina-drain.md).
  if (exe) staminaConstants_ = pes::LoadStaminaConstants(*exe, &err);
  if (!staminaConstants_) {
    UtilityFunctions::push_error("PES21 stamina constants unavailable: ", ToGd(err));
    return ERR_PARSE_ERROR;
  }
  // PES 2021's ball-carrier decision layer (docs/re/pes-re-carrier-ai.md).
  if (exe) carrierConstants_ = pes::LoadCarrierConstants(*exe, &err);
  if (!carrierConstants_) {
    UtilityFunctions::push_error("PES21 carrier-decision constants unavailable: ", ToGd(err));
    return ERR_PARSE_ERROR;
  }
  // PES 2021's open-play defensive shape (docs/re/pes-re-defensive-shape.md 6, 7, 14).
  if (exe) defensiveShapeConstants_ = pes::LoadDefensiveShapeConstants(*exe, &err);
  if (!defensiveShapeConstants_) {
    UtilityFunctions::push_error("PES21 defensive-shape constants unavailable: ", ToGd(err));
    return ERR_PARSE_ERROR;
  }
  if (exe) defenderDecisionConstants_ = pes::LoadDefenderDecisionConstants(*exe, vfs_, &err);
  if (!defenderDecisionConstants_) {
    UtilityFunctions::push_error("PES21 defender-decision constants unavailable: ", ToGd(err));
    return ERR_PARSE_ERROR;
  }
  if (exe) pesClips_ = pes::LoadPesClips(*exe, vfs_, &err);
  if (!pesClips_) {
    UtilityFunctions::push_error("PES21 body clips unavailable: ", ToGd(err));
    return ERR_PARSE_ERROR;
  }
  if (exe) contactConstants_ = pes::LoadContactConstants(*exe, vfs_, &err);
  if (!contactConstants_) {
    UtilityFunctions::push_error("PES21 contact constants unavailable: ", ToGd(err));
    return ERR_PARSE_ERROR;
  }
  if (exe) strategyConstants_ = pes::LoadStrategyConstants(*exe, &err);
  if (!strategyConstants_) {
    UtilityFunctions::push_error("PES21 strategy constants unavailable: ", ToGd(err));
    return ERR_PARSE_ERROR;
  }
  if (exe) teamConceptConstants_ = pes::LoadTeamConceptConstants(*exe, vfs_, &err);
  if (!teamConceptConstants_) {
    UtilityFunctions::push_error("PES21 team concept constants unavailable: ", ToGd(err));
    return ERR_PARSE_ERROR;
  }
  if (exe) substitutionConstants_ = pes::LoadSubstitutionConstants(*exe, &err);
  if (!substitutionConstants_) {
    UtilityFunctions::push_error("PES21 substitution constants unavailable: ", ToGd(err));
    return ERR_PARSE_ERROR;
  }
  if (exe) stoppageConstants_ = pes::LoadStoppageConstants(*exe, &err);
  if (!stoppageConstants_) {
    UtilityFunctions::push_error("PES21 stoppage constants unavailable: ", ToGd(err));
    return ERR_PARSE_ERROR;
  }
  if (exe) kickErrorConstants_ = pes::LoadKickErrorConstants(*exe, &err);
  if (!kickErrorConstants_) {
    UtilityFunctions::push_error("PES21 kick error constants unavailable: ", ToGd(err));
    return ERR_PARSE_ERROR;
  }
  // PES 2021's set-piece taker wait: the corner/free-kick/penalty gates
  // (docs/re/pes-re-setpiece-choreography.md 9 item 2).
  if (exe) setPieceTakerConstants_ = pes::LoadSetPieceTakerConstants(vfs_, *exe, &err);
  if (!setPieceTakerConstants_) {
    UtilityFunctions::push_error("PES21 set-piece taker constants unavailable: ", ToGd(err));
    return ERR_PARSE_ERROR;
  }
  // PES 2021's set-piece team AI: the jog budget, the wall and the free-kick
  // taker waterfall (docs/re/pes-re-setpiece-choreography.md 11).
  if (exe) setPieceTeamConstants_ = pes::LoadSetPieceTeamConstants(*exe, &err);
  if (!setPieceTeamConstants_) {
    UtilityFunctions::push_error("PES21 set-piece team constants unavailable: ", ToGd(err));
    return ERR_PARSE_ERROR;
  }
  if (!cameraParams_) UtilityFunctions::push_warning("PES21 broadcast camera tables unavailable: ", ToGd(err));
  auto glyphs = exe ? pes::ReadNameGlyphs(*exe, &err) : std::nullopt;
  auto nameFont = pes::LoadNameFontMap(vfs_, &err);
  if (glyphs && nameFont) markingFonts_ = MarkingFonts{std::move(*glyphs), std::move(*nameFont)};
  else UtilityFunctions::push_warning("PES21 name glyphs unavailable, names not drawn: ", ToGd(err));
  return OK;
}

Error PesLibrary::load_edit_save(const String& path) {
  const PackedByteArray bytes = FileAccess::get_file_as_bytes(path);
  if (bytes.is_empty()) {
    UtilityFunctions::push_error("EDIT save unreadable: ", path);
    return ERR_FILE_CANT_OPEN;
  }
  std::string err;
  auto save = pes::ReadEditSave(std::span(bytes.ptr(), size_t(bytes.size())), &err);
  if (!save) {
    UtilityFunctions::push_error("EDIT save rejected: ", ToGd(err));
    return ERR_PARSE_ERROR;
  }
  for (auto& [id, record] : save->appearances) appearances_[id] = record;  // the save wins
  return OK;
}

String PesLibrary::mount_report() const { return ToGd(vfs_.MountReport()); }

PackedStringArray PesLibrary::list(const String& prefix) const {
  PackedStringArray out;
  for (const auto& p : vfs_.List(FromGd(prefix))) out.push_back(ToGd(p));
  return out;
}

Ref<Texture> PesLibrary::load_texture(const String& path) const {
  std::string err;
  Ref<Texture> t = LoadPesTexture(vfs_, FromGd(path), &err);
  if (t.is_valid()) t->set_name(path);
  return t;
}

}  // namespace godot
