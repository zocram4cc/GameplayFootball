#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "pes/fpk.hpp"
#include "pes/stadium.hpp"
#include "pes/vfs.hpp"

namespace {

// A 392-byte Stadium.bin record: u16 id at +4, NUL-terminated folder at +370.
// Synthetic ids (2 and 5); no PES bytes anywhere in this test.
std::vector<uint8_t> MakeStadiumBinBlob() {
  auto rec = [&](uint16_t id, const char* folder) {
    std::vector<uint8_t> r(392, 0);
    // u16 id at +4, little-endian.
    r[4] = static_cast<uint8_t>(id & 0xFF);
    r[5] = static_cast<uint8_t>((id >> 8) & 0xFF);
    std::memcpy(r.data() + 370, folder, std::strlen(folder) + 1);
    return r;
  };
  std::vector<uint8_t> blob;
  for (auto& r : {rec(2, "st002"), rec(5, "st005")})
    blob.insert(blob.end(), r.begin(), r.end());
  return blob;
}

// The one installed PES VFS, mounted once for the whole test binary.
const pes::PesVfs* Vfs() {
  static pes::PesVfs vfs;
  static bool ready = [] {
    const char* root = std::getenv("PES21_ROOT");
    if (!root) return false;
    std::string err;
    return vfs.Mount(root, &err);
  }();
  return ready ? &vfs : nullptr;
}

bool Is180Y(const pes::Quat& q) {
  return std::fabs(std::fabs(q.y) - 1.0f) < 1e-4f && std::fabs(q.x) < 1e-4f &&
         std::fabs(q.z) < 1e-4f && std::fabs(q.w) < 1e-4f;
}
}  // namespace

TEST(Stadium, ParsesStadiumRecordsSynthetically) {
  const std::vector<uint8_t> blob = MakeStadiumBinBlob();
  std::string err;
  auto list = pes::ParseStadiumRecords({blob.data(), blob.size()}, &err);
  ASSERT_EQ(list.size(), 2u) << err;
  EXPECT_EQ(list[0].id, 2);
  EXPECT_EQ(list[0].folder, "st002");
  EXPECT_EQ(list[1].id, 5);
  EXPECT_EQ(list[1].folder, "st005");
}

TEST(Stadium, RejectsTruncatedStadiumRecords) {
  const std::vector<uint8_t> blob = MakeStadiumBinBlob();
  std::string err;
  auto bad = pes::ParseStadiumRecords({blob.data(), blob.size() - 1}, &err);
  EXPECT_TRUE(bad.empty());
  EXPECT_FALSE(err.empty());
}

// A model's transform is exactly the fox2 TransformEntity's scale/quat/translation;
// the goal's 180-degree rotation about Y is (0,1,0,0).
TEST(Stadium, TransformHoldsFoxValues) {
  pes::Transform t;
  t.scale = {2.0f, 1.0f, 1.0f};
  t.rotation = {0.0f, 1.0f, 0.0f, 0.0f};
  t.translation = {-52.5f, 0.0f, 0.0f};
  EXPECT_FLOAT_EQ(t.scale.x, 2.0f);
  EXPECT_FLOAT_EQ(t.rotation.y, 1.0f);
  EXPECT_FLOAT_EQ(t.rotation.w, 0.0f);
}

TEST(StadiumInstall, StadiumListNamesStadiumTwo) {
  const pes::PesVfs* vfs = Vfs();
  if (!vfs) GTEST_SKIP() << "PES21_ROOT not set or mount failed";
  std::string err;
  auto list = pes::ReadStadiumList(*vfs, &err);
  ASSERT_FALSE(list.empty()) << err;
  const pes::StadiumInfo* two = nullptr;
  for (const auto& s : list)
    if (s.id == 2) two = &s;
  ASSERT_NE(two, nullptr) << "stadium 2 missing among " << list.size() << " records";
  EXPECT_EQ(two->folder, "st002");
  // The list's own rule: folder = "st" + id zero-padded to 3 digits.
  for (const auto& s : list) {
    std::string num = std::to_string(s.id);
    while (num.size() < 3) num = "0" + num;
    EXPECT_EQ(s.folder, "st" + num);
  }
}

TEST(StadiumInstall, AssemblesStadiumTwoWithPitchAndTwoGoals) {
  const pes::PesVfs* vfs = Vfs();
  if (!vfs) GTEST_SKIP() << "PES21_ROOT not set or mount failed";
  std::string err;
  auto models = pes::LoadStadiumScene(*vfs, 2, &err);
  ASSERT_TRUE(models.has_value()) << err;
  const auto& ms = *models;

  // Every placed model's fmdl really is in its sibling .fpk.
  for (const auto& m : ms) {
    auto fpkBlob = vfs->Read(m.fpk, &err);
    ASSERT_TRUE(fpkBlob.has_value()) << m.fpk << ": " << err;
    auto fpk = pes::FpkArchive::Parse(*fpkBlob, &err);
    ASSERT_TRUE(fpk.has_value()) << m.fpk << ": " << err;
    const bool found = std::any_of(fpk->entries.begin(), fpk->entries.end(),
                                   [&](const auto& e) { return e.path == m.member; });
    EXPECT_TRUE(found) << "member " << m.member << " not in " << m.fpk;
  }

  // The scene: every fmdl the st002 scene pack ships is placed.
  {
    const std::string fpk = "Asset/model/bg/st002/#Win/st002.fpk";
    auto fpkBlob = vfs->Read(fpk, &err);
    ASSERT_TRUE(fpkBlob.has_value()) << err;
    auto pack = pes::FpkArchive::Parse(*fpkBlob, &err);
    ASSERT_TRUE(pack.has_value()) << err;
    int shipped = 0, placed = 0;
    for (const auto& e : pack->entries)
      if (e.path.size() > 5 && e.path.compare(e.path.size() - 5, 5, ".fmdl") == 0) {
        ++shipped;
        if (std::any_of(ms.begin(), ms.end(),
                        [&](const auto& m) { return m.fpk == fpk && m.member == e.path; }))
          ++placed;
      }
    EXPECT_GT(shipped, 0);
    EXPECT_EQ(placed, shipped) << "a shipped scene fmdl was not placed";
  }

  // The pitch is present.
  EXPECT_TRUE(std::any_of(ms.begin(), ms.end(), [](const auto& m) {
    return m.fpk == "Asset/model/bg/st002/pitch/#Win/pitch_st002.fpk";
  })) << "pitch missing";

  // Two goals: the common goal_post places goal.fmdl twice, one of them 180 deg
  // about Y (the mesh is authored at -X and mirrored for +X), plus two shadows.
  int goals = 0, shadow = 0;
  bool goal180 = false;
  for (const auto& m : ms)
    if (m.fpk == "Asset/model/bg/common/goal/#Win/goal_post.fpk") {
      if (m.member == "Assets/pes16/model/bg/common/goal/scenes/goal.fmdl") {
        ++goals;
        if (Is180Y(m.transform.rotation)) goal180 = true;
      } else if (m.member == "Assets/pes16/model/bg/common/goal/scenes/shadow.fmdl")
        ++shadow;
    }
  EXPECT_EQ(goals, 2) << "expected two goal posts";
  EXPECT_EQ(shadow, 2) << "expected two goal shadows";
  EXPECT_TRUE(goal180) << "one goal is authored at -X and mirrored 180 deg about Y";
}
