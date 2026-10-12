#include <gtest/gtest.h>

#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_set>
#include <vector>

#include "pes/exe.hpp"
#include "pes/mtar.hpp"
#include "pes/strcode.hpp"
#include "pes/vfs.hpp"

namespace {

template <typename T>
void Put(std::vector<uint8_t>& b, size_t at, T v) {
  if (b.size() < at + sizeof v) b.resize(at + sizeof v);
  std::memcpy(&b[at], &v, sizeof v);
}

// A PE with one section: file offset 0x400 holds virtual address base+0x1000.
std::vector<uint8_t> OneSectionPe(uint64_t imageBase) {
  std::vector<uint8_t> b(0x600, 0);
  b[0] = 'M', b[1] = 'Z';
  constexpr uint32_t PE_AT = 0x80;
  Put<uint32_t>(b, 0x3C, PE_AT);
  Put<uint32_t>(b, PE_AT, 0x00004550);           // "PE\0\0"
  Put<uint16_t>(b, PE_AT + 6, 1);                // one section
  Put<uint16_t>(b, PE_AT + 20, 0xF0);            // optional header size (PE32+)
  Put<uint16_t>(b, PE_AT + 24, 0x20B);           // PE32+ magic
  Put<uint64_t>(b, PE_AT + 24 + 24, imageBase);  // ImageBase
  const uint32_t sec = PE_AT + 24 + 0xF0;
  std::memcpy(&b[sec], ".rdata\0\0", 8);
  Put<uint32_t>(b, sec + 8, 0x200);   // virtual size
  Put<uint32_t>(b, sec + 12, 0x1000); // virtual address (RVA)
  Put<uint32_t>(b, sec + 16, 0x200);  // raw size
  Put<uint32_t>(b, sec + 20, 0x400);  // raw offset
  return b;
}

}  // namespace

TEST(PeImage, MapsVirtualAddressesThroughSections) {
  constexpr uint64_t BASE = 0x140000000;
  auto b = OneSectionPe(BASE);
  const uint64_t strVa = BASE + 0x1000 + 0x40;
  Put<uint64_t>(b, 0x400, strVa);  // a pointer at base+0x1000 to a string
  std::memcpy(&b[0x440], "run_3_3", 8);
  std::string err;
  auto pe = pes::PeImage::Parse(std::move(b), &err);
  ASSERT_TRUE(pe) << err;
  EXPECT_EQ(pe->ReadU64(BASE + 0x1000), strVa);
  EXPECT_EQ(pe->ReadCString(strVa), "run_3_3");
  EXPECT_FALSE(pe->ReadU64(BASE + 0x5000));  // outside every section
}

TEST(PeImage, RejectsNonPe) {
  std::string err;
  EXPECT_FALSE(pes::PeImage::Parse(std::vector<uint8_t>(64, 0), &err));
}

// motionNo indexes the game's motion name table (docs/re/pes-re-animselect.md
// 1), read from the user's own PES2021.exe.
TEST(ExeInstall, MotionNamesNameEveryBodyClip) {
  const char* root = std::getenv("PES21_ROOT");
  if (!root) GTEST_SKIP() << "PES21_ROOT not set";
  pes::PesVfs vfs;
  std::string err;
  ASSERT_TRUE(vfs.Mount(root, &err)) << err;
  auto pe = pes::PeImage::Open(std::string(root) + "/PES2021.exe", &err);
  ASSERT_TRUE(pe) << err;
  auto names = pes::MotionNames(*pe, vfs, &err);
  ASSERT_TRUE(names) << err;
  ASSERT_FALSE(names->empty());
  EXPECT_EQ(names->front(), "assistant_3_0_090_flag_gk");
  std::unordered_set<uint32_t> clips;
  for (const auto& path : vfs.List("common/anime/FoxAnim/Body/"))
    if (path.ends_with(".mtar")) {
      auto blob = vfs.Read(path, &err);
      auto mtar = pes::ParseMtar(*blob, &err);
      for (const auto& e : mtar->entries) clips.insert(e.nameHash);
    }
  EXPECT_EQ(names->size(), clips.size());
  for (const auto& n : *names) EXPECT_TRUE(clips.count(pes::StrCode32(n))) << n;
}
