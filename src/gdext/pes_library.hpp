#pragma once

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/classes/texture.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <array>
#include <optional>

#include "pes/camera.hpp"
#include "onthepitch/pesball.hpp"
#include "onthepitch/pesjudge.hpp"
#include "onthepitch/peskeeper.hpp"
#include "onthepitch/pesstamina.hpp"
#include "onthepitch/pesstoppage.hpp"
#include "onthepitch/pesrandom.hpp"
#include "onthepitch/pesclock.hpp"
#include "pes/editsave.hpp"
#include "gdext/pes_marking.hpp"
#include "pes/handseq.hpp"
#include "rig/clip_pose.hpp"
#include "pes/vfs.hpp"
#include "rig/rig.hpp"

namespace godot {

// The user's PES 2021 install: its file system and the animation rig built from it.
class PesLibrary : public RefCounted {
  GDCLASS(PesLibrary, RefCounted)

 public:
  Error open(const String& root);
  String mount_report() const;
  PackedStringArray list(const String& prefix) const;
  // The user's EDIT00000000: each player's appearance as the game shows it.
  // Without it, players fall back to PlayerAppearance.bin, as in the game.
  Error load_edit_save(const String& path);
  // A PES texture as the matching Godot type (ImageTexture, Texture2DArray,
  // Cubemap); null when missing or unreadable.
  Ref<Texture> load_texture(const String& path) const;

  const pes::PesVfs& vfs() const { return vfs_; }
  const pesrig::Rig* rig() const { return rig_ ? &*rig_ : nullptr; }
  // Save over PlayerAppearance.bin, by player id.
  const pes::AppearanceTable& appearances() const { return appearances_; }
  // Hand rig: motionNo -> clip name (from PES2021.exe), per-clip hand timelines
  // and the 162 hand poses. Null when the install lacks them; hands then rest.
  const std::vector<std::string>* motion_names() const { return motionNames_ ? &*motionNames_ : nullptr; }
  const pes::HandSequences* hand_sequences() const { return handSequences_ ? &*handSequences_ : nullptr; }
  const pesrig::HandPoseLibrary* hand_poses() const { return handPoses_ ? &*handPoses_ : nullptr; }
  // Name glyphs (from PES2021.exe) and name folding; null without the exe.
  const MarkingFonts* marking_fonts() const { return markingFonts_ ? &*markingFonts_ : nullptr; }
  // Speed-class bounds, km/h (from PES2021.exe); null without the exe.
  const std::array<float, 4>* speed_classes() const { return speedClasses_ ? &*speedClasses_ : nullptr; }
  // PES 2021's match-clock constants (from PES2021.exe, docs/re/pes-re-tactics-clock.md
  // 7.2). Always present once the library is open: a GfMatch cannot run without them.
  const pes::PesClockConstants* clock_constants() const {
    return clockConstants_ ? &*clockConstants_ : nullptr;
  }
  // Dynamic Wide broadcast camera tables (exe + constant_match.bin); null when missing.
  const pes::DynamicWideParams* camera_params() const { return cameraParams_ ? &*cameraParams_ : nullptr; }
  // PES 2021's ball physics constants (`ball.o` in constant_match.bin plus the
  // image values, docs/re/pes-re-ball-contact.md 6). Required by every GfMatch.
  const pes::PesBallConstants* ball_constants() const {
    return ballConstants_ ? &*ballConstants_ : nullptr;
  }
  // PES 2021's foul-decision constants (docs/re/pes-re-duels-fouls.md 2).
  const pes::JudgeConstants* judge_constants() const {
    return judgeConstants_ ? &*judgeConstants_ : nullptr;
  }
  // PES 2021's generator constants (docs/re/pes-re-rng.md 1).
  const pes::RngConstants* rng_constants() const {
    return rngConstants_ ? &*rngConstants_ : nullptr;
  }
  // PES 2021's goalkeeper deflect point and post-catch distribution target
  // (docs/re/pes-re-keeper-port.md).
  const pes::KeeperConstants* keeper_constants() const {
    return keeperConstants_ ? &*keeperConstants_ : nullptr;
  }
  // PES 2021's stamina model (docs/re/pes-re-stamina-drain.md).
  const pes::StaminaConstants* stamina_constants() const {
    return staminaConstants_ ? &*staminaConstants_ : nullptr;
  }

  // PES 2021's restart timings (docs/re/pes-re-foul-choreography.md 9).
  const pes::StoppageConstants* stoppage_constants() const {
    return stoppageConstants_ ? &*stoppageConstants_ : nullptr;
  }

 protected:
  static void _bind_methods();

 private:
  pes::PesVfs vfs_;
  std::optional<pesrig::Rig> rig_;
  pes::AppearanceTable appearances_;
  std::optional<std::vector<std::string>> motionNames_;
  std::optional<pes::HandSequences> handSequences_;
  std::optional<pesrig::HandPoseLibrary> handPoses_;
  std::optional<MarkingFonts> markingFonts_;
  std::optional<std::array<float, 4>> speedClasses_;
  std::optional<pes::DynamicWideParams> cameraParams_;
  std::optional<pes::PesClockConstants> clockConstants_;
  std::optional<pes::PesBallConstants> ballConstants_;
  std::optional<pes::JudgeConstants> judgeConstants_;
  std::optional<pes::RngConstants> rngConstants_;
  std::optional<pes::KeeperConstants> keeperConstants_;
  std::optional<pes::StaminaConstants> staminaConstants_;
  std::optional<pes::StoppageConstants> stoppageConstants_;
};

inline String ToGd(const std::string& s) { return String::utf8(s.c_str()); }
inline std::string FromGd(const String& s) { return s.utf8().get_data(); }

}  // namespace godot
