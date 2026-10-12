#include <gtest/gtest.h>
#include <zlib.h>

#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "pes/uniform.hpp"
#include "pes/vfs.hpp"

namespace {

template <typename T>
void Put(std::vector<uint8_t>& b, size_t at, T v) {
  if (b.size() < at + sizeof v) b.resize(at + sizeof v);
  std::memcpy(&b[at], &v, sizeof v);
}

void PutStr(std::vector<uint8_t>& b, size_t at, const std::string& s) {
  if (b.size() < at + s.size() + 1) b.resize(at + s.size() + 1);
  std::memcpy(&b[at], s.c_str(), s.size() + 1);
}

// u32 count, u32 tableOff, then {u32 dataOff, u32 size, u32 nameOff} records.
std::vector<uint8_t> Table(const std::vector<std::pair<std::string, std::vector<uint8_t>>>& files) {
  std::vector<uint8_t> b;
  const size_t table = 8, names = table + 12 * files.size();
  size_t at = names;
  std::vector<size_t> nameOff, dataOff;
  for (auto& [n, d] : files) nameOff.push_back(at), at += n.size() + 1;
  for (auto& [n, d] : files) dataOff.push_back(at), at += d.size();
  b.resize(at);
  Put<uint32_t>(b, 0, files.size());
  Put<uint32_t>(b, 4, table);
  for (size_t i = 0; i < files.size(); ++i) {
    Put<uint32_t>(b, table + 12 * i, dataOff[i]);
    Put<uint32_t>(b, table + 12 * i + 4, files[i].second.size());
    Put<uint32_t>(b, table + 12 * i + 8, nameOff[i]);
    PutStr(b, nameOff[i], files[i].first);
    std::memcpy(&b[dataOff[i]], files[i].second.data(), files[i].second.size());
  }
  return b;
}

constexpr size_t REAL_UNI_SIZE = 120;

// A realUni record: sleeve code A = (0,1) -> 1, B = (1,3) -> 1003, packed as
// A = b0 | (b1 & 0xF) << 8, B = (b1 >> 4) | b2 << 4.
std::vector<uint8_t> RealUni(int sleeveLimits, bool tight) {
  std::vector<uint8_t> r(REAL_UNI_SIZE, 0);
  const int a = 1, b = 1003;
  r[0] = a & 0xFF;
  r[1] = ((a >> 8) & 0xF) | ((b & 0xF) << 4);
  r[2] = b >> 4;
  r[3] = 16;     // ShortsModel
  r[0x14] = 105;  // Collar
  r[0x15] = 45;   // WinterCollar
  uint32_t d6 = (uint32_t(sleeveLimits) << 29) | (uint32_t(tight) << 31);
  Put<uint32_t>(r, 24, d6);
  Put<uint32_t>(r, 36, 6u << 5);  // ShirtPattern
  PutStr(r, 0x28, "u0767p1");
  PutStr(r, 0x38, "u0767p1_back");
  PutStr(r, 0x58, "u0767p1_leg");
  PutStr(r, 0x68, "u0767p1_name");
  return r;
}

}  // namespace

TEST(UniformTable, FindsEntryByName) {
  auto blob = Table({{"1_DEF_1st_realUni.bin", {1, 2, 3}}, {"2_DEF_1st.bin", {9}}});
  std::string err;
  auto t = pes::ParseUniformTable(blob, &err);
  ASSERT_TRUE(t) << err;
  auto e = t->Find("2_DEF_1st.bin");
  ASSERT_TRUE(e);
  EXPECT_EQ(std::vector<uint8_t>(e->begin(), e->end()), std::vector<uint8_t>{9});
  EXPECT_FALSE(t->Find("3_DEF_1st.bin"));
}

TEST(UniformTable, InflatesWesysContainer) {
  // Vanilla dt34 copy: "\x05\x10\xd1\x57WESYS" header, zlib stream from +0x10.
  auto raw = Table({{"1_DEF_1st_realUni.bin", {7, 7}}});
  uLongf zlen = compressBound(raw.size());
  std::vector<uint8_t> blob(16 + zlen);
  const uint8_t magic[] = {0x05, 0x10, 0xd1, 0x57, 'W', 'E', 'S', 'Y', 'S'};
  std::memcpy(blob.data(), magic, sizeof magic);
  ASSERT_EQ(compress(blob.data() + 16, &zlen, raw.data(), raw.size()), Z_OK);
  blob.resize(16 + zlen);
  std::string err;
  auto t = pes::ParseUniformTable(blob, &err);
  ASSERT_TRUE(t) << err;
  auto e = t->Find("1_DEF_1st_realUni.bin");
  ASSERT_TRUE(e);
  EXPECT_EQ(e->size(), 2u);
}

TEST(UniformTable, RejectsEntryPastEnd) {
  auto blob = Table({{"a.bin", {1, 2, 3}}});
  Put<uint32_t>(blob, 8 + 4, 1000);  // size beyond the blob
  std::string err;
  EXPECT_FALSE(pes::ParseUniformTable(blob, &err));
  EXPECT_FALSE(err.empty());
}

TEST(RealUni, DecodesPartFieldsAndFiles) {
  std::string err;
  auto k = pes::ParseRealUni(RealUni(0, true), &err);
  ASSERT_TRUE(k) << err;
  EXPECT_EQ(k->shortsModel, 16);
  EXPECT_EQ(k->collar, 105);
  EXPECT_EQ(k->winterCollar, 45);
  EXPECT_TRUE(k->tight);
  EXPECT_EQ(k->shirtPattern, 6);
  EXPECT_EQ(k->kitFile, "u0767p1");
  EXPECT_EQ(k->backNumbersFile, "u0767p1_back");
  EXPECT_EQ(k->chestNumbersFile, "");
  EXPECT_EQ(k->legNumbersFile, "u0767p1_leg");
  EXPECT_EQ(k->nameFontFile, "u0767p1_name");
}

// SleeveLimits (d6 bits 29-30) routes the two codes: 0 -> short slot A, long
// slot B; 1 -> both B; 2 -> both A (FUN_141ea3190).
TEST(RealUni, SleeveLimitsRoutesCodesToSlots) {
  const pes::SleeveCode A{0, 1}, B{1, 3};
  struct Case { int limits; pes::SleeveCode shortSlot, longSlot; };
  for (const Case& c : {Case{0, A, B}, Case{1, B, B}, Case{2, A, A}}) {
    std::string err;
    auto k = pes::ParseRealUni(RealUni(c.limits, false), &err);
    ASSERT_TRUE(k) << err;
    EXPECT_EQ(k->shortSleeves, c.shortSlot) << "limits " << c.limits;
    EXPECT_EQ(k->longSleeves, c.longSlot) << "limits " << c.limits;
  }
}

TEST(RealUni, ShortsModelZeroMeansOne) {
  auto r = RealUni(0, false);
  r[3] = 0;
  std::string err;
  auto k = pes::ParseRealUni(r, &err);
  ASSERT_TRUE(k) << err;
  EXPECT_EQ(k->shortsModel, 1);
}

TEST(RealUni, RejectsWrongSize) {
  std::vector<uint8_t> r(96, 0);
  std::string err;
  EXPECT_FALSE(pes::ParseRealUni(r, &err));
}

// Oracle: the kit struct read from the running game for team 767's 1st kit
// (UniformImpl +0x0C shorts 16, +0x14 pattern 6, +0x20 "u0767p1",
// +0x98/+0x9C collar 105/105, +0xA0..+0xAC sleeves (0,1),(1,3)).
TEST(UniformInstall, Team767FirstKitMatchesTheRunningGame) {
  const char* root = std::getenv("PES21_ROOT");
  if (!root) GTEST_SKIP() << "PES21_ROOT not set";
  pes::PesVfs vfs;
  std::string err;
  ASSERT_TRUE(vfs.Mount(root, &err)) << err;
  auto blob = vfs.Read(pes::kUniformParameterPath, &err);
  ASSERT_TRUE(blob) << err;
  auto t = pes::ParseUniformTable(*blob, &err);
  ASSERT_TRUE(t) << err;
  auto rec = t->Find(pes::RealUniName(767, 0, false));
  ASSERT_TRUE(rec);
  auto k = pes::ParseRealUni(*rec, &err);
  ASSERT_TRUE(k) << err;
  EXPECT_EQ(k->shortSleeves, (pes::SleeveCode{0, 1}));
  EXPECT_EQ(k->longSleeves, (pes::SleeveCode{1, 3}));
  EXPECT_EQ(k->shortsModel, 16);
  EXPECT_EQ(k->collar, 105);
  EXPECT_EQ(k->winterCollar, 105);
  EXPECT_EQ(k->shirtPattern, 6);
  EXPECT_FALSE(k->tight);
  EXPECT_EQ(k->kitFile, "u0767p1");
}

TEST(RealUniName, FollowsTheGamesNameBuilder) {
  // FUN_141ea2400: team + "_DEF_" + {"", "GK"} + {"1st".."9th"} + "_realUni.bin"
  EXPECT_EQ(pes::RealUniName(767, 0, false), "767_DEF_1st_realUni.bin");
  EXPECT_EQ(pes::RealUniName(767, 1, false), "767_DEF_2nd_realUni.bin");
  EXPECT_EQ(pes::RealUniName(767, 2, false), "767_DEF_3rd_realUni.bin");
  EXPECT_EQ(pes::RealUniName(767, 8, false), "767_DEF_9th_realUni.bin");
  EXPECT_EQ(pes::RealUniName(767, 0, true), "767_DEF_GK1st_realUni.bin");
}
