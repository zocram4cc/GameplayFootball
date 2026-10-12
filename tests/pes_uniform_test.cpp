#include <gtest/gtest.h>
#include <zlib.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "pes/fpk.hpp"
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

// FUN_1419637c0: "sleeve" ("s" for type 3) + type suffix unless long + glove,
// then _glove (never for types 0/2), _cap, _tight, then _%03d unless long + glove.
TEST(SleeveModelName, FollowsTheGamesNameBuilder) {
  using pes::SleeveCode;
  using pes::SleeveModelName;
  EXPECT_EQ(SleeveModelName({0, 1}, false, false, false), "sleeve_short_001");
  EXPECT_EQ(SleeveModelName({1, 2}, false, true, true), "sleeve_long_cap_tight_002");
  EXPECT_EQ(SleeveModelName({3, 1}, true, true, false), "s_inner_glove_cap_001");
  EXPECT_EQ(SleeveModelName({1, 2}, true, true, true), "sleeve_glove_cap_tight");
  EXPECT_EQ(SleeveModelName({0, 1}, true, false, false), "sleeve_short_001");  // glove off for short
  EXPECT_EQ(SleeveModelName({2, 3}, true, false, false), "sleeve_half_003");   // and for half
}

namespace {
std::vector<std::string> Paths(const std::vector<pes::UniformPart>& parts) {
  std::vector<std::string> out;
  for (const auto& p : parts) out.push_back((p.inCommonPackage ? "fpk:" : "") + p.path);
  std::sort(out.begin(), out.end());
  return out;
}
pes::KitConfig Kit767() {
  pes::KitConfig k;
  k.shortSleeves = {0, 1};
  k.longSleeves = {1, 3};
  k.shortsModel = 16;
  k.collar = 105;
  k.winterCollar = 45;
  k.kitFile = "u0767p1";
  return k;
}
const std::string NOCLOTH = "Asset/model/character/uniform/nocloth/#Win/";
}  // namespace

// Oracle: player 76711's uniform/body draw list read from the running game
// (team 767 1st kit; short sleeves, socks type 0, shirt tail out).
TEST(SelectUniformParts, MatchesTheLiveDrawListOf76711) {
  pes::PlayerLook look;
  look.longSleeves = false;
  look.socks = 0;
  look.shirtTailOut = true;
  std::vector<std::string> want = {
      NOCLOTH + "pants_016.fmdl",       NOCLOTH + "collar_105.fmdl",
      NOCLOTH + "shirt_out.fmdl",       NOCLOTH + "sleeve_short_001.fmdl",
      "fpk:common/socks_middle.fmdl",   "fpk:common/pants_out_sub.fmdl",
      "fpk:common/thigh_middle.fmdl",   "fpk:common/arm.fmdl",
      "fpk:common/arm_cap.fmdl",        "fpk:common/hand_l.fmdl",
      "fpk:common/hand_r.fmdl",         "fpk:common/eye_new.fmdl",
      "fpk:common/mouth.fmdl"};
  std::sort(want.begin(), want.end());
  EXPECT_EQ(Paths(pes::SelectUniformParts(Kit767(), look)), want);
}

TEST(SelectUniformParts, LongSleevesUseTheLongSlotAndWinterCollar) {
  pes::PlayerLook look;
  look.longSleeves = true;
  auto p = Paths(pes::SelectUniformParts(Kit767(), look));
  auto has = [&](const std::string& s) { return std::find(p.begin(), p.end(), s) != p.end(); };
  EXPECT_TRUE(has(NOCLOTH + "sleeve_long_003.fmdl"));
  EXPECT_TRUE(has(NOCLOTH + "collar_045.fmdl"));
  // The arm slot is skipped by sleeve type, not by file existence (pes-kit-assembly.md 8.9).
  EXPECT_FALSE(has("fpk:common/arm_cap.fmdl"));
  EXPECT_FALSE(has("fpk:common/arm.fmdl"));
  EXPECT_TRUE(has("fpk:common/hand_l.fmdl"));
}

TEST(SelectUniformParts, TailInWearsShirtInOverTheInnerShorts) {
  pes::PlayerLook look;
  look.shirtTailOut = false;
  auto p = Paths(pes::SelectUniformParts(Kit767(), look));
  auto has = [&](const std::string& s) { return std::find(p.begin(), p.end(), s) != p.end(); };
  EXPECT_TRUE(has(NOCLOTH + "shirt_in.fmdl"));
  EXPECT_FALSE(has(NOCLOTH + "shirt_out.fmdl"));
  EXPECT_TRUE(has("fpk:common/pants_in_sub.fmdl"));
}

TEST(SelectUniformParts, SocksTypePicksSocksAndThighs) {
  struct Case { int socks; const char *sock, *thigh; };
  for (const Case& c : {Case{0, "socks_middle", "thigh_middle"}, Case{1, "socks_long", "thigh_long"},
                        Case{2, "socks_short", "thigh_short"}, Case{3, "socks_noguard", "thigh_middle"}}) {
    pes::PlayerLook look;
    look.socks = c.socks;
    auto p = Paths(pes::SelectUniformParts(Kit767(), look));
    auto has = [&](const std::string& s) { return std::find(p.begin(), p.end(), s) != p.end(); };
    EXPECT_TRUE(has(std::string("fpk:common/") + c.sock + ".fmdl")) << c.socks;
    EXPECT_TRUE(has(std::string("fpk:common/") + c.thigh + ".fmdl")) << c.socks;
  }
}

TEST(SelectUniformParts, TightKitUsesTightShirtSleeveAndShorts) {
  auto k = Kit767();
  k.tight = true;
  auto p = Paths(pes::SelectUniformParts(k, pes::PlayerLook{}));
  auto has = [&](const std::string& s) { return std::find(p.begin(), p.end(), s) != p.end(); };
  EXPECT_TRUE(has(NOCLOTH + "shirt_out_tight.fmdl"));
  EXPECT_TRUE(has(NOCLOTH + "sleeve_short_tight_001.fmdl"));
  EXPECT_TRUE(has("fpk:common/pants_out_sub_tight.fmdl"));
}

// Every part chosen for a real kit, under every look, exists in this install:
// loose parts in the VFS, common parts as a common_package.fpk member whose
// path ends in "/" + part path.
TEST(UniformInstall, EverySelectedPartExists) {
  const char* root = std::getenv("PES21_ROOT");
  if (!root) GTEST_SKIP() << "PES21_ROOT not set";
  pes::PesVfs vfs;
  std::string err;
  ASSERT_TRUE(vfs.Mount(root, &err)) << err;
  auto blob = vfs.Read(pes::kUniformParameterPath, &err);
  ASSERT_TRUE(blob) << err;
  auto t = pes::ParseUniformTable(*blob, &err);
  ASSERT_TRUE(t) << err;
  auto kit = pes::ParseRealUni(*t->Find(pes::RealUniName(767, 0, false)), &err);
  ASSERT_TRUE(kit) << err;
  auto pack = vfs.Read(pes::kCommonPackagePath, &err);
  ASSERT_TRUE(pack) << err;
  auto fpk = pes::FpkArchive::Parse(*pack, &err);
  ASSERT_TRUE(fpk) << err;
  auto inPack = [&](const std::string& p) {
    return std::any_of(fpk->entries.begin(), fpk->entries.end(),
                       [&](const pes::FpkEntry& e) { return e.path.ends_with("/" + p); });
  };
  // Open: tight kits with long model 3 (team 714) name sleeve_long_tight_003,
  // which no install file provides; PES's handling is not yet known.
  for (bool longSleeves : {false, true})
    for (bool tailOut : {false, true})
      for (bool glove : {false, true})
        for (int socks = 0; socks < 4; ++socks) {
          pes::PlayerLook look{longSleeves, socks, tailOut, glove ? 1 : 0};
          for (const auto& part : pes::SelectUniformParts(*kit, look))
            EXPECT_TRUE(part.inCommonPackage ? inPack(part.path) : vfs.Exists(part.path))
                << part.path << " long=" << longSleeves << " out=" << tailOut << " glove=" << glove
                << " socks=" << socks;
        }
}

TEST(SkinTone, PicksTheMatchingArmAndThighTextures) {
  EXPECT_EQ(pes::ArmSkinPath(1), "Asset/model/character/common/sourceimages/#windx11/arm_1_bsm.ftex");
  EXPECT_EQ(pes::ThighSkinPath(6), "Asset/model/character/common/sourceimages/#windx11/thigh_6_bsm.ftex");
}

// FUN_1414e03c0: a boots id of 0 means "look the player up in BootsList.bin",
// whose default is 62 when the player is absent.
TEST(Boots, ZeroIdFallsBackToTheListThenItsDefault) {
  std::vector<uint8_t> blob;
  Put<uint32_t>(blob, 0, 100);   // player 100 -> boots 7011
  Put<uint32_t>(blob, 4, 7011);
  Put<uint32_t>(blob, 8, 200);   // player 200 -> boots 1386
  Put<uint32_t>(blob, 12, 1386);
  std::string err;
  auto list = pes::ParseBootsList(blob, &err);
  ASSERT_TRUE(list) << err;
  EXPECT_EQ(pes::ResolveBootsId(1052, 100, *list), 1052);  // the appearance id wins
  EXPECT_EQ(pes::ResolveBootsId(0, 200, *list), 1386);
  EXPECT_EQ(pes::ResolveBootsId(0, 300, *list), pes::kBootsListDefaultId);
  EXPECT_EQ(pes::BootsPackPath(62), "Asset/model/character/boots/k0062/#Win/boots.fpk");
  EXPECT_EQ(pes::GlovePackPath(1), "Asset/model/character/glove/g0001/#Win/glove.fpk");
}

TEST(Boots, RejectsAListThatIsNotWholePairs) {
  std::vector<uint8_t> blob(12, 0);
  std::string err;
  EXPECT_FALSE(pes::ParseBootsList(blob, &err));
}

// Live: 76711 and 75403 (boots id 0, not listed) wear k0062.
TEST(UniformInstall, BootsListResolvesUnlistedPlayersToTheDefaultPack) {
  const char* root = std::getenv("PES21_ROOT");
  if (!root) GTEST_SKIP() << "PES21_ROOT not set";
  pes::PesVfs vfs;
  std::string err;
  ASSERT_TRUE(vfs.Mount(root, &err)) << err;
  auto blob = vfs.Read(pes::kBootsListPath, &err);
  ASSERT_TRUE(blob) << err;
  auto list = pes::ParseBootsList(*blob, &err);
  ASSERT_TRUE(list) << err;
  for (int player : {76711, 75403}) {
    const int id = pes::ResolveBootsId(0, player, *list);
    EXPECT_EQ(id, 62) << player;
    EXPECT_TRUE(vfs.Exists(pes::BootsPackPath(id))) << player;
  }
}
