// Hand pose timelines per body clip. Record layout and evaluation: docs/re/
// pes-re-animselect.md 11.2 and 11.4 (FUN_141ed48f0, FUN_141ebb160/640).
#include "pes/handseq.hpp"

#include "pes/exe.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>

namespace pes {
namespace {

constexpr size_t RECORD_BYTES = 12;
constexpr uint32_t ID_MASK = 0x1fff;  // w0 bits 0-12
constexpr int MOTION_ID_BASE = 216;   // id = motionNo + 216; ids below go to an overflow table
constexpr int CLIP_SHIFT = 20, START_SHIFT = 10;
constexpr uint32_t CLIP_MASK = 0xff, FRAME_MASK = 0x3ff;
constexpr int START_BIAS = 11, LEFT_END_BIAS = 8, RIGHT_END_BIAS = 9;
constexpr float EVENT_FRAME_S = 1.0f / 30.0f;  // body-clip frames
constexpr size_t MAX_POSES = 4;               // four tracks per hand control

constexpr const char* HAND_LEFT_FILE = "common/anime/FHSequence/bin/Handlbase.bin";
constexpr const char* SEQUENCE_INFO_FILE = "common/anime/FHSequence/bin/Sequenceinfo.bin";
// FUN_141eb9a30's fallback table: 17 x {i16 sequenceKey, i16 category}.
constexpr uint64_t FALLBACK_TABLE_VA = 0x142b67ef0;
constexpr size_t FALLBACK_ENTRIES = 17;
// Sequenceinfo.bin: 8-byte records; w0 bits 0-12 id, 13-24 key, bit 30 "right
// hand keeps the fallback", bit 31 the same for the left.
constexpr size_t SEQUENCE_INFO_BYTES = 8;
constexpr int SEQUENCE_KEY_SHIFT = 13;
constexpr uint32_t SEQUENCE_KEY_MASK = 0xfff;
constexpr int RIGHT_FALLBACK_BIT = 30, LEFT_FALLBACK_BIT = 31;
constexpr const char* HAND_RIGHT_FILE = "common/anime/FHSequence/bin/Handrbase.bin";

template <typename T>
T Load(const uint8_t* p) { T v; std::memcpy(&v, p, sizeof v); return v; }

// Every record by its raw id (fallback sequences live at ids below 216).
std::unordered_map<int, std::vector<HandEvent>> EventsByRawId(std::span<const uint8_t> blob, bool right) {
  std::unordered_map<int, std::vector<HandEvent>> out;
  for (size_t at = 0; at + RECORD_BYTES <= blob.size(); at += RECORD_BYTES) {
    const uint32_t w0 = Load<uint32_t>(&blob[at]), w1 = Load<uint32_t>(&blob[at + 4]);
    out[int(w0 & ID_MASK)].push_back({int((w1 >> CLIP_SHIFT) & CLIP_MASK), int((w1 >> START_SHIFT) & FRAME_MASK) - START_BIAS,
                                      int(w1 & FRAME_MASK) - (right ? RIGHT_END_BIAS : LEFT_END_BIAS)});
  }
  return out;
}

}  // namespace

std::optional<HandEvents> ParseHandEvents(std::span<const uint8_t> blob, bool right, std::string* error) {
  if (blob.size() % RECORD_BYTES != 0) {
    *error = "hand sequence: size is not whole 12-byte records";
    return std::nullopt;
  }
  HandEvents out;
  for (size_t at = 0; at < blob.size(); at += RECORD_BYTES) {
    const uint32_t w0 = Load<uint32_t>(&blob[at]), w1 = Load<uint32_t>(&blob[at + 4]);
    const int motion = int(w0 & ID_MASK) - MOTION_ID_BASE;
    if (motion < 0) continue;  // overflow-table keys: not body clips
    out[motion].push_back({int((w1 >> CLIP_SHIFT) & CLIP_MASK), int((w1 >> START_SHIFT) & FRAME_MASK) - START_BIAS,
                           int(w1 & FRAME_MASK) - (right ? RIGHT_END_BIAS : LEFT_END_BIAS)});
  }
  for (auto& [m, events] : out)
    std::stable_sort(events.begin(), events.end(), [](const HandEvent& a, const HandEvent& b) { return a.startFrame < b.startFrame; });
  return out;
}

std::vector<HandWeight> EvaluateHand(const std::vector<HandEvent>& events, float t) {
  if (events.empty()) return {};
  auto s = [&](size_t i) { return events[i].startFrame * EVENT_FRAME_S; };
  auto e = [&](size_t i) { return events[i].endFrame * EVENT_FRAME_S; };
  auto ramp = [](float x, float a, float b) { return b > a ? std::clamp((x - a) / (b - a), 0.0f, 1.0f) : 1.0f; };
  // In-window events; the last one stays active from its start on.
  std::vector<HandWeight> out;
  for (size_t i = 0; i < events.size() && out.size() < MAX_POSES; ++i) {
    const bool last = i + 1 == events.size();
    if (t < s(i) || (t > e(i) && !last)) continue;
    if (out.empty()) {
      out.push_back({events[i].clip, 1.0f});
      continue;
    }
    // Overlap with the previous in-window event: crossfade across it.
    const float w = ramp(t, s(i), e(i - 1));
    for (auto& p : out) p.weight *= 1.0f - w;
    out.push_back({events[i].clip, w});
  }
  if (!out.empty()) return out;
  // Between two events: interpolate from the earlier pose to the next.
  for (size_t i = 1; i < events.size(); ++i)
    if (e(i - 1) <= t && t <= s(i)) {
      const float w = ramp(t, e(i - 1), s(i));
      return {{events[i - 1].clip, 1.0f - w}, {events[i].clip, w}};
    }
  // ponytail: before the first event the first pose holds (the decompile
  // covers windows and gaps only); revisit if a clip shows a pop at its start.
  return {{events.front().clip, 1.0f}};
}

std::vector<std::string> HandPoseOrder(std::vector<std::string> stems) {
  auto upper = [](std::string s) {
    for (char& c : s) c = char(std::toupper(static_cast<unsigned char>(c)));
    return s;
  };
  std::stable_sort(stems.begin(), stems.end(), [&](const std::string& a, const std::string& b) { return upper(a) < upper(b); });
  return stems;
}

std::optional<HandSequences> LoadHandSequences(const PesVfs& vfs, const PeImage* exe, std::string* error) {
  HandSequences hs;
  std::vector<std::string> stems;
  for (const auto& path : vfs.List(kHandPoseDir))
    if (path.ends_with(".gani")) stems.push_back(path.substr(path.rfind('/') + 1, path.size() - path.rfind('/') - 6));
  hs.poses = HandPoseOrder(std::move(stems));
  std::unordered_map<int, std::vector<HandEvent>> raw[2];
  for (auto [file, right] : {std::pair{HAND_LEFT_FILE, false}, std::pair{HAND_RIGHT_FILE, true}}) {
    auto blob = vfs.Read(file, error);
    auto events = blob ? ParseHandEvents(*blob, right, error) : std::nullopt;
    if (!events) return std::nullopt;
    (right ? hs.right : hs.left) = std::move(*events);
    raw[right] = EventsByRawId(*blob, right);
  }
  // Sequenceinfo: per-clip fallback bits, and key -> raw id for the fallbacks.
  auto info = vfs.Read(SEQUENCE_INFO_FILE, error);
  if (!info) return std::nullopt;
  std::unordered_map<int, int> idOfKey;
  for (size_t at = 0; at + SEQUENCE_INFO_BYTES <= info->size(); at += SEQUENCE_INFO_BYTES) {
    const uint32_t w0 = Load<uint32_t>(&(*info)[at]);
    const int id = int(w0 & ID_MASK), key = int((w0 >> SEQUENCE_KEY_SHIFT) & SEQUENCE_KEY_MASK);
    idOfKey.emplace(key, id);
    const uint8_t bits = uint8_t(((w0 >> LEFT_FALLBACK_BIT) & 1) | (((w0 >> RIGHT_FALLBACK_BIT) & 1) << 1));
    if (bits && id >= MOTION_ID_BASE) hs.useFallback[id - MOTION_ID_BASE] = bits;
  }
  if (!exe) return hs;
  uint8_t table[FALLBACK_ENTRIES * 4];
  if (!exe->ReadVa(FALLBACK_TABLE_VA, sizeof table, table)) {
    *error = "hand fallback table unreadable: not the PES 2021 executable this table was found in";
    return std::nullopt;
  }
  for (size_t i = 0; i < FALLBACK_ENTRIES; ++i) {
    HandFallback f;
    f.key = Load<int16_t>(&table[4 * i]);
    f.category = Load<int16_t>(&table[4 * i + 2]);
    auto id = idOfKey.find(f.key);
    if (id == idOfKey.end()) continue;  // the game skips unresolved keys too
    f.rawId = id->second;
    f.left = raw[0][f.rawId];
    f.right = raw[1][f.rawId];
    for (const auto* side : {&f.left, &f.right})
      for (const auto& e : *side) f.durationSeconds = std::max(f.durationSeconds, e.endFrame * EVENT_FRAME_S);
    hs.fallbacks.push_back(std::move(f));
  }
  return hs;
}

std::vector<HandWeight> EvaluateFallback(const HandSequences& hs, int category, float t, bool right) {
  std::vector<const HandFallback*> cycle;
  float total = 0;
  for (const auto& f : hs.fallbacks)
    if (f.category == category && f.durationSeconds > 0) cycle.push_back(&f), total += f.durationSeconds;
  if (cycle.empty()) return {};
  float local = std::fmod(std::max(t, 0.0f), total);
  for (const auto* f : cycle) {
    if (local < f->durationSeconds) return EvaluateHand(right ? f->right : f->left, local);
    local -= f->durationSeconds;
  }
  return EvaluateHand(right ? cycle.back()->right : cycle.back()->left, cycle.back()->durationSeconds);
}

}  // namespace pes
