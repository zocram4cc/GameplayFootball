#include <gtest/gtest.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "pes/exe.hpp"
#include "pes/handseq.hpp"
#include "pes/vfs.hpp"

namespace {

template <typename T>
void Put(std::vector<uint8_t>& b, size_t at, T v) {
  if (b.size() < at + sizeof v) b.resize(at + sizeof v);
  std::memcpy(&b[at], &v, sizeof v);
}

// One 12-byte Handlbase/Handrbase record (docs/re/pes-re-animselect.md 11.2):
// id = w0 bits 0-12 (= motionNo + 216), clip = w1 bits 20-27,
// start = ((w1 >> 10) & 0x3ff) - 11, end = (w1 & 0x3ff) - 8 (left) / - 9 (right).
void Record(std::vector<uint8_t>& b, int motionNo, int clip, int start, int end, bool right) {
  const size_t at = b.size();
  const uint32_t w0 = uint32_t(motionNo + 216);
  const uint32_t w1 = (uint32_t(clip) << 20) | (uint32_t(start + 11) << 10) | uint32_t(end + (right ? 9 : 8));
  Put<uint32_t>(b, at, w0);
  Put<uint32_t>(b, at + 4, w1);
  Put<uint32_t>(b, at + 8, 0);
}

float WeightOf(const std::vector<pes::HandWeight>& ws, int clip) {
  for (const auto& w : ws)
    if (w.clip == clip) return w.weight;
  return 0;
}

constexpr float FRAME_S = 1.0f / 30.0f;  // hand events are body-clip frames at 30 Hz

}  // namespace

TEST(HandSequence, DecodesEventsPerMotionAndSide) {
  std::vector<uint8_t> left, right;
  Record(left, 5, 12, 0, 4, false);
  Record(left, 5, 30, 10, 12, false);
  Record(right, 5, 40, 2, 3, true);
  std::string err;
  auto l = pes::ParseHandEvents(left, false, &err), r = pes::ParseHandEvents(right, true, &err);
  ASSERT_TRUE(l && r) << err;
  ASSERT_EQ(l->at(5).size(), 2u);
  EXPECT_EQ(l->at(5)[1].clip, 30);
  EXPECT_EQ(l->at(5)[1].startFrame, 10);
  EXPECT_EQ(l->at(5)[1].endFrame, 12);
  ASSERT_EQ(r->at(5).size(), 1u);
  EXPECT_EQ(r->at(5)[0].clip, 40);
  EXPECT_EQ(r->at(5)[0].endFrame, 3);
}

// Inside an event its pose holds; the last pose holds past its end.
TEST(HandSequence, HoldsThePoseInsideAnEventAndAfterTheLast) {
  const std::vector<pes::HandEvent> ev = {{7, 0, 4}};
  EXPECT_FLOAT_EQ(WeightOf(pes::EvaluateHand(ev, 2 * FRAME_S), 7), 1.0f);
  EXPECT_FLOAT_EQ(WeightOf(pes::EvaluateHand(ev, 100 * FRAME_S), 7), 1.0f);
}

// Between two events, linear interpolation from the first to the next.
TEST(HandSequence, InterpolatesAcrossAGap) {
  const std::vector<pes::HandEvent> ev = {{1, 0, 4}, {2, 10, 12}};
  const auto mid = pes::EvaluateHand(ev, 7 * FRAME_S);  // 3 of 6 gap frames
  EXPECT_NEAR(WeightOf(mid, 1), 0.5f, 1e-5f);
  EXPECT_NEAR(WeightOf(mid, 2), 0.5f, 1e-5f);
}

// Overlapping events crossfade over the overlap.
TEST(HandSequence, CrossfadesOverlappingEvents) {
  const std::vector<pes::HandEvent> ev = {{1, 0, 8}, {2, 4, 12}};
  const auto mid = pes::EvaluateHand(ev, 6 * FRAME_S);  // halfway through the 4..8 overlap
  EXPECT_NEAR(WeightOf(mid, 2), 0.5f, 1e-5f);
  EXPECT_NEAR(WeightOf(mid, 1), 0.5f, 1e-5f);
}

TEST(HandSequence, PoseIndexIsTheUpperCaseSortOfTheHandClips) {
  // The exe's 162-name table equals the hand gani stems sorted case-insensitively
  // by upper case ('_' after letters), checked against the game image.
  const auto order = pes::HandPoseOrder({"normal", "amanigiri", "Open_full", "hatanigiri", "normal_yubimage", "normal3"});
  EXPECT_EQ(order, (std::vector<std::string>{"amanigiri", "hatanigiri", "normal", "normal3", "normal_yubimage", "Open_full"}));
}

// The flag grip lands only on linesman clips (pes-re-animselect.md 11.2; also
// on assistant_idle_0_point_f090, a linesman pointing with his flag, so not
// only on clips named *flag*).
TEST(HandSequenceInstall, FlagGripOnlyOnLinesmanFlagClips) {
  const char* root = std::getenv("PES21_ROOT");
  if (!root) GTEST_SKIP() << "PES21_ROOT not set";
  pes::PesVfs vfs;
  std::string err;
  ASSERT_TRUE(vfs.Mount(root, &err)) << err;
  auto pe = pes::PeImage::Open(std::string(root) + "/PES2021.exe", &err);
  ASSERT_TRUE(pe) << err;
  auto motions = pes::MotionNames(*pe, vfs, &err);
  ASSERT_TRUE(motions) << err;
  auto hands = pes::LoadHandSequences(vfs, &*pe, &err);
  ASSERT_TRUE(hands) << err;
  ASSERT_EQ(hands->poses.size(), 162u);
  const int grip = int(std::find(hands->poses.begin(), hands->poses.end(), "hatanigiri") - hands->poses.begin());
  ASSERT_LT(grip, 162);
  int gripped = 0;
  for (const auto* side : {&hands->left, &hands->right})
    for (const auto& [motion, events] : *side)
      for (const auto& e : events)
        if (e.clip == grip) {
          ++gripped;
          ASSERT_LT(size_t(motion), motions->size());
          const std::string& name = (*motions)[motion];
          EXPECT_NE(name.find("assistant"), std::string::npos) << name;  // incl. layer_assistant_*
        }
  EXPECT_GT(gripped, 0);
}

// docs/re/pes-re-animselect.md 11.3.1: 17 {key, category} fallback entries in
// the exe; each key resolves through Sequenceinfo.bin to a raw id below 216,
// whose own hand records are that fallback's timelines. The game's live
// durations equal each timeline's last end frame / 30.
TEST(HandSequenceInstall, FallbacksResolveThroughSequenceinfo) {
  const char* root = std::getenv("PES21_ROOT");
  if (!root) GTEST_SKIP() << "PES21_ROOT not set";
  pes::PesVfs vfs;
  std::string err;
  ASSERT_TRUE(vfs.Mount(root, &err)) << err;
  auto pe = pes::PeImage::Open(std::string(root) + "/PES2021.exe", &err);
  ASSERT_TRUE(pe) << err;
  auto hands = pes::LoadHandSequences(vfs, &*pe, &err);
  ASSERT_TRUE(hands) << err;
  constexpr size_t FALLBACK_ENTRIES = 17;
  ASSERT_EQ(hands->fallbacks.size(), FALLBACK_ENTRIES);
  const auto& first = hands->fallbacks[0];
  EXPECT_EQ(first.key, 2564);
  EXPECT_EQ(first.category, 0);
  // Live readout: entry 0 lasts 1.4 s (both hands).
  EXPECT_NEAR(first.durationSeconds, 1.4f, 1e-4f);
  const auto& linesman = hands->fallbacks[9];  // key 6, category 2: the flag grip
  EXPECT_EQ(linesman.category, 2);
  const int grip = int(std::find(hands->poses.begin(), hands->poses.end(), "hatanigiri") - hands->poses.begin());
  bool gripped = false;
  for (const auto* side : {&linesman.left, &linesman.right})
    for (const auto& e : *side) gripped |= e.clip == grip;
  EXPECT_TRUE(gripped) << "the linesman fallback holds the flag grip";
}

// Idle hands cycle through the variants of their category: after one entry's
// duration the next entry of the same category plays.
TEST(HandSequence, FallbackCyclesThroughItsCategory) {
  pes::HandSequences hs;
  hs.fallbacks = {{100, 0, 0, {{1, 0, 3}}, {{1, 0, 3}}, 0.1f},
                  {101, 1, 0, {{9, 0, 3}}, {{9, 0, 3}}, 0.1f},
                  {102, 0, 0, {{2, 0, 3}}, {{2, 0, 3}}, 0.1f}};
  constexpr float INSIDE_FIRST = 0.05f, INSIDE_SECOND = 0.15f, WRAPPED = 0.25f;
  auto clipAt = [&](float t) { return pes::EvaluateFallback(hs, 0, t, false)[0].clip; };
  EXPECT_EQ(clipAt(INSIDE_FIRST), 1);
  EXPECT_EQ(clipAt(INSIDE_SECOND), 2);
  EXPECT_EQ(clipAt(WRAPPED), 1);
}
