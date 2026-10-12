#include <gtest/gtest.h>

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "pes/anim_names.hpp"
#include "pes/clips.hpp"
#include "pes/gani.hpp"
#include "pes/mtar.hpp"
#include "pes/strcode.hpp"
#include "pes/vfs.hpp"

namespace {

template <typename T>
void Put(std::vector<uint8_t>& b, size_t at, T v) {
  if (b.size() < at + sizeof v) b.resize(at + sizeof v);
  std::memcpy(&b[at], &v, sizeof v);
}

constexpr uint32_t MTAR_MAGIC = 0x0BFFA89C;  // GF mtar.py
constexpr size_t MTAR_TABLE = 0x20, MTAR_ENTRY = 16;

}  // namespace

TEST(Mtar, EntryTableReadsHashOffsetAndSize) {
  std::vector<uint8_t> b;
  Put<uint32_t>(b, 0, MTAR_MAGIC);
  Put<uint32_t>(b, 4, 2);
  Put<uint32_t>(b, MTAR_TABLE, 0xAABBCCDD);
  Put<uint32_t>(b, MTAR_TABLE + 4, 0);
  Put<uint32_t>(b, MTAR_TABLE + 8, 0x40);
  Put<uint32_t>(b, MTAR_TABLE + 12, 8);
  Put<uint32_t>(b, MTAR_TABLE + MTAR_ENTRY, 0x11223344);
  Put<uint32_t>(b, MTAR_TABLE + MTAR_ENTRY + 8, 0x48);
  Put<uint32_t>(b, MTAR_TABLE + MTAR_ENTRY + 12, 4);
  b.resize(0x4C);
  std::string err;
  auto m = pes::ParseMtar(b, &err);
  ASSERT_TRUE(m) << err;
  ASSERT_EQ(m->entries.size(), 2u);
  EXPECT_EQ(m->entries[0].nameHash, 0xAABBCCDDu);
  EXPECT_EQ(m->entries[0].offset, 0x40u);
  EXPECT_EQ(m->entries[0].size, 8u);
  EXPECT_EQ(m->entries[1].nameHash, 0x11223344u);
}

TEST(Mtar, RejectsWrongMagic) {
  std::vector<uint8_t> b(0x40, 0);
  std::string err;
  EXPECT_FALSE(pes::ParseMtar(b, &err));
}

TEST(Mtar, RejectsEntryPastEnd) {
  std::vector<uint8_t> b;
  Put<uint32_t>(b, 0, MTAR_MAGIC);
  Put<uint32_t>(b, 4, 1);
  Put<uint32_t>(b, MTAR_TABLE + 8, 0x30);
  Put<uint32_t>(b, MTAR_TABLE + 12, 0x100);
  std::string err;
  EXPECT_FALSE(pes::ParseMtar(b, &err));
}

namespace {

const pes::PesVfs* Install() {
  static pes::PesVfs vfs;
  static int state = 0;
  if (state == 0) {
    const char* root = std::getenv("PES21_ROOT");
    std::string err;
    state = root && vfs.Mount(root, &err) ? 1 : -1;
  }
  return state == 1 ? &vfs : nullptr;
}

#define REQUIRE_INSTALL()             \
  const pes::PesVfs* vfs = Install(); \
  if (!vfs) GTEST_SKIP() << "PES21_ROOT not set"

constexpr int BODY_MTAR_COUNT = 10;          // body_anime_file0..9.mtar (dt13)
constexpr size_t BODY_CLIP_COUNT = 4389;     // GF counted every body clip across them
constexpr uint32_t GANI_MAGIC = 0x0BFCA2D2;  // GF mtar.py

}  // namespace

TEST(MtarInstall, BodyArchivesHoldEveryClipAsGani) {
  REQUIRE_INSTALL();
  size_t clips = 0;
  for (int i = 0; i < BODY_MTAR_COUNT; ++i) {
    std::string err;
    auto bytes = vfs->Read("common/anime/FoxAnim/Body/body_anime_file" + std::to_string(i) + ".mtar", &err);
    ASSERT_TRUE(bytes) << err;
    auto m = pes::ParseMtar(*bytes, &err);
    ASSERT_TRUE(m) << err;
    for (const auto& e : m->entries) {
      uint32_t magic;
      std::memcpy(&magic, bytes->data() + e.offset, 4);
      if (magic == GANI_MAGIC) ++clips;
    }
  }
  EXPECT_EQ(clips, BODY_CLIP_COUNT);
}

// The engine's half float rebases the exponent by +0x1DC00 (GF gani.py), i.e.
// x128 against IEEE half: 0x3C00 (half 1.0) decodes to 128.
TEST(Gani, AnimHalfRebasesExponent) {
  EXPECT_FLOAT_EQ(pes::AnimHalf(0x3C00), 128.0f);
  EXPECT_FLOAT_EQ(pes::AnimHalf(0xBC00), -128.0f);
  EXPECT_FLOAT_EQ(pes::AnimHalf(0x0000), 0.0f);
  EXPECT_FLOAT_EQ(pes::AnimHalf(0x3E00), 192.0f);  // mantissa half-way
}

// theta = a/mask * pi/2 is the HALF angle; x = b/mask, y = c/mask, z = 1-x-y,
// (x,y,z) normalised and scaled by sin(theta); three sign bits flip x, y, z.
TEST(Gani, QuaternionDequantisation) {
  constexpr int BITS = 12;
  constexpr uint32_t MASK = (1u << BITS) - 1;
  const pes::Quat q = pes::DequantQuat(MASK, MASK, 0, 0, BITS);  // half angle 90 deg about +X
  EXPECT_NEAR(q.x, 1.0f, 1e-6f);
  EXPECT_NEAR(q.y, 0.0f, 1e-6f);
  EXPECT_NEAR(q.z, 0.0f, 1e-6f);
  EXPECT_NEAR(q.w, 0.0f, 1e-6f);
  const pes::Quat zNeg = pes::DequantQuat(MASK, 0, 0, 4, BITS);   // z = 1, sign bit 2 flips it
  EXPECT_NEAR(zNeg.z, -1.0f, 1e-6f);
  const pes::Quat identity = pes::DequantQuat(0, 0, 0, 0, BITS);
  EXPECT_NEAR(identity.w, 1.0f, 1e-6f);
}

TEST(Gani, RejectsWrongMagic) {
  std::vector<uint8_t> b(0x100, 0);
  std::string err;
  EXPECT_FALSE(pes::ParseGani(b, &err));
}

namespace {
pes::Gani OneChannel(pes::Quat q, uint32_t lastKeyFrame, uint32_t frameCount) {
  pes::GaniSegment seg;
  seg.frames = {0, lastKeyFrame};
  seg.quats = {pes::Quat{}, q};
  pes::GaniUnit unit;
  unit.segments = {seg};
  pes::Gani g;
  g.frameCount = frameCount;
  g.units = {unit};
  return g;
}
}  // namespace

TEST(Gani, ValidationFlagsBrokenDecodes) {
  EXPECT_TRUE(pes::ValidateGani(OneChannel(pes::Quat{}, 10, 10)).empty());
  EXPECT_FALSE(pes::ValidateGani(OneChannel(pes::Quat{0, 0, 0, 2}, 10, 10)).empty());  // non-unit quat
  EXPECT_FALSE(pes::ValidateGani(OneChannel(pes::Quat{}, 9, 10)).empty());             // keys stop early
  pes::Gani nan = OneChannel(pes::Quat{}, 10, 10);
  nan.units[0].segments[0].vecs = {pes::Vec4{NAN, 0, 0, 0}};
  EXPECT_FALSE(pes::ValidateGani(nan).empty());
}

// GF's extra heuristics are contradicted by PES's own data and are not
// invariants: root turns span > 120 deg between sparse keys (262 clips), and
// long runs travel > 1e5 raw units (= 4.9 m; max seen 13.15 m).
TEST(Gani, LargeTurnsAndLongTravelAreValid) {
  const pes::Quat halfTurn{0, 1, 0, 0};
  EXPECT_TRUE(pes::ValidateGani(OneChannel(halfTurn, 10, 10)).empty());
  pes::Gani run = OneChannel(pes::Quat{}, 10, 10);
  run.units[0].segments[0].vecs = {pes::Vec4{269312.0f, 0, 0, 0}};
  EXPECT_TRUE(pes::ValidateGani(run).empty());
}

// Every body clip in the install decodes and satisfies the measured invariants.
TEST(GaniInstall, EveryBodyClipDecodesAndValidates) {
  REQUIRE_INSTALL();
  size_t decoded = 0;
  for (int i = 0; i < BODY_MTAR_COUNT; ++i) {
    std::string err;
    auto bytes = vfs->Read("common/anime/FoxAnim/Body/body_anime_file" + std::to_string(i) + ".mtar", &err);
    ASSERT_TRUE(bytes) << err;
    auto m = pes::ParseMtar(*bytes, &err);
    ASSERT_TRUE(m) << err;
    for (const auto& e : m->entries) {
      auto blob = std::span<const uint8_t>(*bytes).subspan(e.offset, e.size);
      uint32_t magic;
      std::memcpy(&magic, blob.data(), 4);
      if (magic != GANI_MAGIC) continue;
      auto g = pes::ParseGani(blob, &err);
      ASSERT_TRUE(g) << std::hex << e.nameHash << ": " << err;
      const auto problems = pes::ValidateGani(*g);
      EXPECT_TRUE(problems.empty()) << std::hex << e.nameHash << ": " << problems.front();
      ++decoded;
    }
  }
  EXPECT_EQ(decoded, BODY_CLIP_COUNT);
}

TEST(Clips, IdParsesHexHashOrHashesName) {
  EXPECT_EQ(pes::ClipId("0x2c5e8a2e"), 0x2c5e8a2eu);
  EXPECT_EQ(pes::ClipId("0X2C5E8A2E"), 0x2c5e8a2eu);
  EXPECT_EQ(pes::ClipId("MOTION"), 0x08908348u);  // StrCode32 of a name
}

TEST(ClipsInstall, FindsBodyClipByHash) {
  REQUIRE_INSTALL();
  std::string err;
  auto blob = pes::FindBodyClip(*vfs, 0x2c5e8a2e, &err);
  ASSERT_TRUE(blob) << err;
  EXPECT_TRUE(pes::ParseGani(*blob, &err)) << err;
  EXPECT_FALSE(pes::FindBodyClip(*vfs, 0xDEADBEEF, &err));
}

// Candidates come from "file_name" values (json) and identifier runs in the
// exe; only those hashing to an archive entry are kept, json winning ties.
TEST(AnimNames, KeepsOnlyCandidatesThatHashToArchiveEntries) {
  const std::string json = R"({"animations": {"a": {"file_name": "sliding_0_0_000"}, "b": {"file_name": "unused_clip"}}})";
  using namespace std::string_literals;
  const std::string exeText = "junk\0run_0_0_000\0zz\0sliding_0_0_000"s;  // NULs kept
  const std::vector<uint8_t> exe(exeText.begin(), exeText.end());
  const std::set<uint32_t> targets = {pes::StrCode32("sliding_0_0_000"), pes::StrCode32("run_0_0_000"), 0x12345678};
  const auto idx = pes::AnimNameIndex::FromSources(json, exe, targets);
  EXPECT_EQ(idx.Name(pes::StrCode32("sliding_0_0_000")), "sliding_0_0_000");
  EXPECT_EQ(idx.Name(pes::StrCode32("run_0_0_000")), "run_0_0_000");
  EXPECT_EQ(idx.Name(0x12345678), "0x12345678");  // unresolved stays addressable
  EXPECT_EQ(idx.Resolved(), 2u);
}

TEST(AnimNamesInstall, ResolvesEveryBodyClip) {
  REQUIRE_INSTALL();
  std::string err;
  auto idx = pes::AnimNameIndex::Build(*vfs, std::getenv("PES21_ROOT"), &err);
  ASSERT_TRUE(idx) << err;
  EXPECT_EQ(idx->Resolved(), BODY_CLIP_COUNT);
  EXPECT_EQ(idx->Name(pes::StrCode32("sliding_0_0_000")), "sliding_0_0_000");
}
