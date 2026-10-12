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

// An idle hand sequence: used when a clip has no hand timeline, or its
// Sequenceinfo bit hands the hand to the fallback (pes-re-animselect.md 11.3).
struct HandFallback {
  int key = 0, category = 0;  // from the exe table at 0x142b67ef0
  int rawId = 0;              // Sequenceinfo id for the key (< 216)
  std::vector<HandEvent> left, right;
  float durationSeconds = 0;  // last end frame / 30
};

struct HandSequences {
  std::vector<std::string> poses;  // index -> hand gani stem
  HandEvents left, right;          // by motionNo
  std::vector<HandFallback> fallbacks;
  std::unordered_map<int, uint8_t> useFallback;  // motionNo -> bit 0 left, bit 1 right
};
inline constexpr const char* kHandPoseDir = "common/anime/FoxAnim/Hand/Animations/";
// The exe supplies the fallback table; without it there are no fallbacks.
std::optional<HandSequences> LoadHandSequences(const PesVfs& vfs, const class PeImage* exe, std::string* error);

// Idle hands of `category` at `t` seconds: the category's entries play in
// table order, each for its duration, then repeat.
std::vector<HandWeight> EvaluateFallback(const HandSequences& hs, int category, float t, bool right);

}  // namespace pes
