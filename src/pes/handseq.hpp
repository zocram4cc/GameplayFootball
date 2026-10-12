#pragma once

// Hand poses during a body clip: each body clip (motionNo) carries a left and a
// right timeline of hand-pose events; the poses are 162 single-frame hand ganis.
// docs/re/pes-re-animselect.md 11.

#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include "pes/vfs.hpp"

namespace pes {

struct HandEvent {
  int clip = 0;        // index into the hand pose list
  int startFrame = 0;  // body-clip frames, 30 Hz
  int endFrame = 0;
};
using HandEvents = std::unordered_map<int, std::vector<HandEvent>>;  // by motionNo, sorted by start

// Handlbase.bin / Handrbase.bin: 12-byte records; the end-frame bias differs
// by side.
std::optional<HandEvents> ParseHandEvents(std::span<const uint8_t> blob, bool right, std::string* error);

struct HandWeight {
  int clip = 0;
  float weight = 0;
};
// At most 4 poses with weights summing to 1 (FUN_141ebb160).
std::vector<HandWeight> EvaluateHand(const std::vector<HandEvent>& events, float timeSeconds);

// The game's hand pose order: the clip stems sorted by upper case.
std::vector<std::string> HandPoseOrder(std::vector<std::string> stems);

struct HandSequences {
  std::vector<std::string> poses;  // index -> hand gani stem
  HandEvents left, right;
};
inline constexpr const char* kHandPoseDir = "common/anime/FoxAnim/Hand/Animations/";
std::optional<HandSequences> LoadHandSequences(const PesVfs& vfs, std::string* error);

}  // namespace pes
