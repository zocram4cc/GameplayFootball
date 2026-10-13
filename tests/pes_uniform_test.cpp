#include <gtest/gtest.h>
#include <zlib.h>

#include <algorithm>
#include <cctype>
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

// WESYS container: u8 type, 0x10, u8 flags, "WESYS" at +3, u32 zlib size at
// +8, u32 raw size at +12, zlib from +0x10. Headers as shipped: vanilla dt34
// UniformParameter.bin "\x05\x10\xd1WESYS", constant_match.bin "\xff\x10\x51WESYS".
TEST(UniformTable, InflatesWesysContainer) {
  auto raw = Table({{"1_DEF_1st_realUni.bin", {7, 7}}});
  for (const auto& magic : {std::vector<uint8_t>{0x05, 0x10, 0xd1, 'W', 'E', 'S', 'Y', 'S'},
                            std::vector<uint8_t>{0xff, 0x10, 0x51, 'W', 'E', 'S', 'Y', 'S'}}) {
    uLongf zlen = compressBound(raw.size());
    std::vector<uint8_t> blob(16 + zlen);
    std::memcpy(blob.data(), magic.data(), magic.size());
    ASSERT_EQ(compress(blob.data() + 16, &zlen, raw.data(), raw.size()), Z_OK);
    blob.resize(16 + zlen);
    std::string err;
    auto t = pes::ParseUniformTable(blob, &err);
    ASSERT_TRUE(t) << err;
    auto e = t->Find("1_DEF_1st_realUni.bin");
    ASSERT_TRUE(e);
    EXPECT_EQ(e->size(), 2u);
  }
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

// A kit record read from THIS install, on data every install ships: the team
// whose record this install's table resolves, with the values the file itself
// carries. Nothing is pinned from a 4cc pack or from the running game: the
// record is re-read and re-decoded here, so the same assertions hold on a
// vanilla-only install and on any modded one (a mod that replaces the table
// changes the numbers, and this test follows it).
//
// dt*.cpk ships every official team (id < 701, e.g. 1..5619 on this install)
// inside common/character0/.../UniformParameter.bin; download/4cc_*_uniform
// replaces that whole file with its own table, which holds 7xx teams only.
TEST(UniformInstall, RealUniOfThisInstallDecodesAsTheFileHoldsIt) {
  const char* root = std::getenv("PES21_ROOT");
  if (!root) GTEST_SKIP() << "PES21_ROOT not set";
  pes::PesVfs vfs;
  std::string err;
  ASSERT_TRUE(vfs.Mount(root, &err)) << err;
  auto blob = vfs.Read(pes::kUniformParameterPath, &err);
  ASSERT_TRUE(blob) << err;
  auto t = pes::ParseUniformTable(*blob, &err);
  ASSERT_TRUE(t) << err;
  // The official-team band every PES 2021 install ships (dt*.cpk): team 1 is
  // present in dt00/dt12 era cpks on any install, modded or vanilla.
  constexpr int VANILLA_TEAM = 1;
  for (int kitIndex = 0; kitIndex < 2; ++kitIndex) {
    auto rec = t->Find(pes::RealUniName(VANILLA_TEAM, kitIndex, false));
    if (!rec) continue;  // a pack that lists no official teams: nothing to decode
    auto k = pes::ParseRealUni(*rec, &err);
    ASSERT_TRUE(k) << err << " (kit " << kitIndex << ')';
    // What a realUni record always holds (docs/re/pes-kit-assembly.md 1).
    EXPECT_EQ(k->kitFile.size(), 7u) << "kit " << kitIndex;  // u####p#
    EXPECT_EQ(k->kitFile.substr(0, 2), "u0");
    EXPECT_EQ(k->kitFile.substr(5), "p" + std::to_string(kitIndex + 1));
    EXPECT_GE(k->shortsModel, 1);
    EXPECT_GE(k->collar, 0);
    // The two sleeve codes are always whole 12-bit type/model pairs.
    EXPECT_GE(k->shortSleeves.type, 0);
    EXPECT_LT(k->shortSleeves.type, 4);
    EXPECT_GE(k->shortSleeves.model, 1);
    EXPECT_GE(k->longSleeves.model, 1);
    // What the parts the rules pick actually name: every selected loose part of
    // the subject's look resolves in the VFS. A wrong shortsModel, collar or
    // sleeve code names a file that is not there, and this fails.
    for (int longSleeves = 0; longSleeves < 2; ++longSleeves)
      for (int socks = 0; socks < 4; ++socks)
        for (int glove = 0; glove < 2; ++glove) {
          pes::PlayerLook look{longSleeves == 1, socks, true, glove};
          for (const auto& part : pes::SelectUniformParts(*k, look)) {
            if (part.inCommonPackage) continue;
            EXPECT_TRUE(vfs.Exists(part.path))
                << part.path << " (kit " << kitIndex << ", long " << longSleeves << ", socks " << socks
                << ", glove " << glove << ')';
          }
        }
    break;  // the first resolved kit is enough: the table is the pack's, not ours
  }
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
pes::KitConfig ShortSleeveKit() {  // a synthetic kit config with PES-plausible fields
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

// The part list the selection rules build for a short-sleeve, socks-0,
// tail-out look (pes-kit-assembly.md 7.3). Built entirely from a synthetic kit
// config: no install file is read, so no 4cc or live value is pinned here.
TEST(SelectUniformParts, BuildsTheShortSleeveDrawList) {
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
  EXPECT_EQ(Paths(pes::SelectUniformParts(ShortSleeveKit(), look)), want);
}

TEST(SelectUniformParts, LongSleevesUseTheLongSlotAndWinterCollar) {
  pes::PlayerLook look;
  look.longSleeves = true;
  auto p = Paths(pes::SelectUniformParts(ShortSleeveKit(), look));
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
  auto p = Paths(pes::SelectUniformParts(ShortSleeveKit(), look));
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
    auto p = Paths(pes::SelectUniformParts(ShortSleeveKit(), look));
    auto has = [&](const std::string& s) { return std::find(p.begin(), p.end(), s) != p.end(); };
    EXPECT_TRUE(has(std::string("fpk:common/") + c.sock + ".fmdl")) << c.socks;
    EXPECT_TRUE(has(std::string("fpk:common/") + c.thigh + ".fmdl")) << c.socks;
  }
}

TEST(SelectUniformParts, TightKitUsesTightShirtSleeveAndShorts) {
  auto k = ShortSleeveKit();
  k.tight = true;
  auto p = Paths(pes::SelectUniformParts(k, pes::PlayerLook{}));
  auto has = [&](const std::string& s) { return std::find(p.begin(), p.end(), s) != p.end(); };
  EXPECT_TRUE(has(NOCLOTH + "shirt_out_tight.fmdl"));
  EXPECT_TRUE(has(NOCLOTH + "sleeve_short_tight_001.fmdl"));
  EXPECT_TRUE(has("fpk:common/pants_out_sub_tight.fmdl"));
}

// Every part chosen for the kit this install's table resolves, under every
// look, exists in this install: loose parts in the VFS, common parts as a
// common_package.fpk member whose path ends in "/" + part path. The team id is
// whatever record the installed table holds first, so it is not pinned to a
// modded team.
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
  // The lowest team id the installed table lists: whichever band it comes from.
  int team = 0;
  for (const auto& e : t->entries) {
    const std::string& n = e.name;
    const size_t us = n.find('_');
    if (us == std::string::npos || us == 0) continue;
    int id = 0;
    for (size_t i = 0; i < us; ++i) {
      if (!std::isdigit(static_cast<unsigned char>(n[i]))) { id = 0; break; }
      id = id * 10 + (n[i] - '0');
    }
    if (id > 0 && (team == 0 || id < team)) team = id;
  }
  ASSERT_GT(team, 0) << "no team record in UniformParameter.bin";
  auto rec = t->Find(pes::RealUniName(team, 0, false));
  ASSERT_TRUE(rec);
  auto kit = pes::ParseRealUni(*rec, &err);
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

// The same list as above wrapped in the WESYS container vanilla BootsList.bin
// ships: u8 type, 0x10, u8 flags, "WESYS" at +3, u32 zlib size at +8, u32 raw
// size at +12, zlib from +0x10 (header "\x05\x10\x01WESYS"). 4cc packs ship
// the list raw, which is why the 4cc copy masks the vanilla one.
TEST(Boots, InflatesWesysWrappedList) {
  std::vector<uint8_t> raw;
  Put<uint32_t>(raw, 0, 100);
  Put<uint32_t>(raw, 4, 7011);
  Put<uint32_t>(raw, 8, 200);
  Put<uint32_t>(raw, 12, 1386);
  for (const auto& magic : {std::vector<uint8_t>{0x05, 0x10, 0x01, 'W', 'E', 'S', 'Y', 'S'},
                            std::vector<uint8_t>{0xff, 0x10, 0x51, 'W', 'E', 'S', 'Y', 'S'}}) {
    uLongf zlen = compressBound(raw.size());
    std::vector<uint8_t> blob(16 + zlen);
    std::memcpy(blob.data(), magic.data(), magic.size());
    ASSERT_EQ(compress(blob.data() + 16, &zlen, raw.data(), raw.size()), Z_OK);
    blob.resize(16 + zlen);
    std::string err;
    auto list = pes::ParseBootsList(blob, &err);
    ASSERT_TRUE(list) << err;
    ASSERT_EQ(list->size(), 2u);
    EXPECT_EQ((*list)[0], std::make_pair(uint32_t(100), uint32_t(7011)));
    EXPECT_EQ((*list)[1], std::make_pair(uint32_t(200), uint32_t(1386)));
    EXPECT_EQ(pes::ResolveBootsId(0, 200, *list), 1386);
  }
}

TEST(Boots, RejectsAListThatIsNotWholePairs) {
  std::vector<uint8_t> blob(12, 0);
  std::string err;
  EXPECT_FALSE(pes::ParseBootsList(blob, &err));
}

// A boots id of 0 resolves through the installed BootsList.bin, and the pack it
// names is in this install. Which player is used is not pinned: the list is
// read, one player id taken from it, and the resolution re-derived here, so the
// check holds on a vanilla-only install and on any modded one that replaces
// the list (4cc_03_stockkit ships its own, raw, copy).
TEST(UniformInstall, BootsListResolvesAListedPlayerToItsPack) {
  const char* root = std::getenv("PES21_ROOT");
  if (!root) GTEST_SKIP() << "PES21_ROOT not set";
  pes::PesVfs vfs;
  std::string err;
  ASSERT_TRUE(vfs.Mount(root, &err)) << err;
  auto blob = vfs.Read(pes::kBootsListPath, &err);
  if (!blob) {
    // The game tolerates a missing list (FUN_1414e03c0 falls to the default).
    EXPECT_EQ(pes::ResolveBootsId(0, 12345, pes::BootsList{}), pes::kBootsListDefaultId);
    return;
  }
  auto list = pes::ParseBootsList(*blob, &err);
  if (!list) {
    // Both shapes the game ships are read (vanilla wraps the list in a WESYS
    // container, 4cc ships raw pairs), so reaching here is a real defect.
    GTEST_SKIP() << err;
  }
  ASSERT_FALSE(list->empty());
  // The first listed player whose pack this install also ships: a modded list
  // can name boots whose pack it does not carry, and PES leaves that slot empty.
  const pes::BootsList::value_type* listed = nullptr;
  for (const auto& pair : *list)
    if (vfs.Exists(pes::BootsPackPath(int(pair.second)))) { listed = &pair; break; }
  if (!listed) GTEST_SKIP() << "none of the installed BootsList packs is mounted";
  const int id = pes::ResolveBootsId(0, int(listed->first), *list);
  EXPECT_EQ(id, int(listed->second)) << "player " << listed->first;
  EXPECT_TRUE(vfs.Exists(pes::BootsPackPath(id))) << pes::BootsPackPath(id);
  // A player the installed list does not name gets the default pack.
  EXPECT_EQ(pes::ResolveBootsId(0, 99999999, *list), pes::kBootsListDefaultId);
}

// Number/name placement on the garment (pes-kit-assembly.md 8.7). Oracles are
// the shader vectors read from live players' materials mid-match.
namespace {
void ExpectVec(const pes::Vec4& got, float x, float y, float z, float w, const char* what) {
  constexpr float LIVE_READ_TOLERANCE = 2e-3f;  // live values were printed to 4-5 digits
  EXPECT_NEAR(got.x, x, LIVE_READ_TOLERANCE) << what;
  EXPECT_NEAR(got.y, y, LIVE_READ_TOLERANCE) << what;
  EXPECT_NEAR(got.z, z, LIVE_READ_TOLERANCE) << what;
  EXPECT_NEAR(got.w, w, LIVE_READ_TOLERANCE) << what;
}
}  // namespace

TEST(MarkingPlacement, BackNumberMatchesLiveKits) {
  pes::KitConfig k;
  k.backNumberSize = 21, k.backNumberY = 21;  // team 752
  ExpectVec(pes::BackNumberScale(k), -5.5f, -11.f, 2.6672f, 2.6672f, "752");
  k.backNumberSize = 24, k.backNumberY = 21;  // team 754 2nd
  ExpectVec(pes::BackNumberScale(k), -4.8571f, -9.7143f, 2.3554f, 2.3554f, "754 2nd");
  k.backNumberSize = 26, k.backNumberY = 20;  // team 754 GK
  ExpectVec(pes::BackNumberScale(k), -4.4286f, -8.8571f, 2.1109f, 2.1109f, "754 GK");
}

TEST(MarkingPlacement, ChestNumberMatchesLiveKits) {
  pes::KitConfig k;
  k.chestNumberSize = 0, k.chestNumberX = 0, k.chestNumberY = 3;
  ExpectVec(pes::ChestNumberScale(k), 26.667f, 26.667f, -10.167f, -19.638f, "Y 3");
  k.chestNumberY = 2;
  EXPECT_NEAR(pes::ChestNumberScale(k).w, -19.859f, 2e-3f);
}

TEST(MarkingPlacement, LegNumberMatchesLiveKits) {
  pes::KitConfig k;
  k.legNumberSide = 0, k.legNumberH = 4, k.legNumberV = 15, k.legNumberSize = 10;  // 754 2nd
  ExpectVec(pes::LegNumberScale(k), -15.556f, -15.556f, 15.026f, 13.374f, "754 2nd");
  k.legNumberH = 5, k.legNumberV = 19, k.legNumberSize = 11;  // 752
  ExpectVec(pes::LegNumberScale(k), -14.933f, -14.933f, 14.347f, 12.570f, "752");
}

TEST(MarkingPlacement, NameMatchesLiveKits) {
  pes::KitConfig k;
  k.nameY = 31, k.nameSize = 14, k.nameShape = 1;  // 752
  pes::Vec4 v = pes::NameScale(k, 0);
  EXPECT_NEAR(v.y, 8.2051f, 2e-3f);
  EXPECT_NEAR(v.z, -2.307f, 2e-3f);
  k.nameY = 32, k.nameSize = 7, k.nameShape = 0;  // 754
  v = pes::NameScale(k, 0);
  EXPECT_NEAR(v.y, 11.228f, 2e-3f);
  EXPECT_NEAR(v.z, -3.470f, 2e-3f);
}

// The realUni bits behind those fields (pes-kit-assembly.md 1, 8.7; the game
// reads d5 16-20 as the horizontal leg position, the reverse of sider).
TEST(RealUni, DecodesMarkingFields) {
  auto r = RealUni(0, false);
  const uint32_t d5 = (4u << 16) | (15u << 21) | (10u << 26) | (1u << 31);
  const uint32_t d6 = (21u << 0) | (21u << 5) | (7u << 10) | (3u << 14) | (5u << 19) | (9u << 24);
  const uint32_t d7 = (1u << 2) | (31u << 3) | (14u << 9) | (1u << 14);
  const uint32_t d9 = (6u << 5) | (2u << 8);
  std::memcpy(&r[20], &d5, 4);
  std::memcpy(&r[24], &d6, 4);
  std::memcpy(&r[28], &d7, 4);
  std::memcpy(&r[36], &d9, 4);
  std::string err;
  auto k = pes::ParseRealUni(r, &err);
  ASSERT_TRUE(k) << err;
  EXPECT_EQ(k->legNumberH, 4);
  EXPECT_EQ(k->legNumberV, 15);
  EXPECT_EQ(k->legNumberSize, 10);
  EXPECT_EQ(k->legNumberSide, 1);
  EXPECT_EQ(k->backNumberY, 21);
  EXPECT_EQ(k->backNumberSize, 21);
  EXPECT_EQ(k->backNumberSpacing, 7);
  EXPECT_EQ(k->chestNumberY, 3);
  EXPECT_EQ(k->chestNumberX, 5);
  EXPECT_EQ(k->chestNumberSize, 9);
  EXPECT_EQ(k->backNumberType, 1);
  EXPECT_EQ(k->nameY, 31);
  EXPECT_EQ(k->nameSize, 14);
  EXPECT_EQ(k->nameShape, 1);
  EXPECT_EQ(k->nameStretch, 2);
  EXPECT_EQ(k->shirtPattern, 6);
}
