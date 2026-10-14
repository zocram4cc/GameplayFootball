// PES's Mbinfo bins (pes/mbinfo.hpp): synthetic records check each layout's
// bit fields; one install test reads the user's three files.
#include <gtest/gtest.h>

#include <cstdlib>
#include <cstring>

#include "pes/mbinfo.hpp"

namespace {

template <typename T>
void Put(std::vector<uint8_t>& b, size_t at, T v) {
  if (b.size() < at + sizeof v) b.resize(at + sizeof v);
  std::memcpy(&b[at], &v, sizeof v);
}

TEST(PesMbinfo, DecodesTheThreeLayouts) {
  std::vector<uint8_t> anim(36, 0), hit(108, 0), cancel(12, 0);
  Put<uint32_t>(anim, 0, 5u | (63u << 13));  // motion 5, end frame 64
  Put<uint32_t>(anim, 4, 360u | (50u << 10));
  Put<float>(anim, 28, 1.5f);  // cancel_speed
  Put<uint32_t>(hit, 0, 5u | (12u << 13) | (9u << 27));
  Put<float>(hit, 0x24, 106.0f);
  Put<float>(hit, 0x28, 168.5f);
  Put<float>(hit, 0x2c, 60.0f);
  Put<float>(hit, 0x44, -4.0f);
  Put<uint32_t>(cancel, 0, 5u | (40u << 13));
  std::string err;
  auto m = pes::ParseMbinfo(anim, hit, cancel, &err);
  ASSERT_TRUE(m.has_value()) << err;
  ASSERT_NE(m->Timing(5), nullptr);
  EXPECT_EQ(m->Timing(5)->endFrame, 64);
  EXPECT_EQ(m->Timing(5)->blendStart, 50);
  EXPECT_FLOAT_EQ(m->Timing(5)->cancelSpeed, 1.5f);
  ASSERT_EQ(m->Hits(5).size(), 1u);
  EXPECT_EQ(m->Hits(5)[0].frame, 12);
  EXPECT_EQ(m->Hits(5)[0].contactPart, 10);
  EXPECT_FLOAT_EQ(m->Hits(5)[0].ballY, 168.5f);
  EXPECT_FLOAT_EQ(m->Hits(5)[0].ballX, 60.0f);
  EXPECT_FLOAT_EQ(m->Hits(5)[0].yaw_deg, -4.0f);
  ASSERT_NE(m->Cancel(5), nullptr);
  EXPECT_EQ(m->Cancel(5)->frame, 40);
  EXPECT_EQ(m->Timing(4), nullptr);
  EXPECT_TRUE(m->Hits(4).empty());
  EXPECT_TRUE(m->Hits(99999).empty());
}

TEST(PesMbinfo, RejectsTruncatedFiles) {
  std::string err;
  EXPECT_FALSE(pes::ParseMbinfo(std::vector<uint8_t>(35), {}, {}, &err).has_value());
  EXPECT_FALSE(pes::ParseMbinfo({}, std::vector<uint8_t>(100), {}, &err).has_value());
}

TEST(PesMbinfoInstall, TheUsersBins) {
  const char* root = std::getenv("PES21_ROOT");
  if (!root) GTEST_SKIP() << "PES21_ROOT not set";
  std::string err;
  pes::PesVfs vfs;
  ASSERT_TRUE(vfs.Mount(root, &err)) << err;
  auto m = pes::LoadMbinfo(vfs, &err);
  ASSERT_TRUE(m.has_value()) << err;
  int hits = 0, timed = 0, inside = 0, judged = 0;
  for (size_t i = 0; i < m->MotionCount(); ++i) {
    hits += int(m->Hits(int(i)).size());
    const auto* t = m->Timing(int(i));
    if (t) {
      ++timed;
      EXPECT_GE(t->endFrame, 0);
    }
    for (const auto& h : m->Hits(int(i)))
      if (t) ++judged, inside += h.frame <= t->endFrame;
  }
  EXPECT_GT(hits, 1000);
  EXPECT_GT(timed, 1000);
  // The hit frame lies inside its clip for all but a handful of records (26 of
  // 2,896 in the 2021 data hit past the stored end frame).
  EXPECT_GT(inside * 100, judged * 98);
}

}  // namespace
