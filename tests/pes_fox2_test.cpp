#include <gtest/gtest.h>

#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "pes/fpk.hpp"
#include "pes/fox2.hpp"
#include "pes/strcode.hpp"
#include "pes/vfs.hpp"

namespace {

// ---- synthetic fox2 builder: layout per docs/re/pes-re-fox2-format.md ----
// File header (32 bytes): magic, u32 entity count, u32 string-table offset,
// u32 entity-data offset (= 32), 12 reserved bytes. Entities: 64-byte header
// (u32 address at +10, u64 class hash at +0x1c, counts at +0x24, block size at
// +0x30), then property blocks of 32-byte header + data, 16-byte aligned.
// String table: u64 hash + u32 length + bytes, ended by a zero hash.
constexpr uint64_t kMask = 0xFFFFFFFFFFFFULL;

template <typename T>
void Put(std::vector<uint8_t>& b, size_t at, T v) {
  if (b.size() < at + sizeof v) b.resize(at + sizeof v);
  std::memcpy(&b[at], &v, sizeof v);
}

uint64_t H(const char* s) { return pes::StrCode64(s) & kMask; }

// One property block: 16-byte header + 16 zero pad, then data, 0-padded to 16.
std::vector<uint8_t> PropBlock(uint64_t hash, uint8_t type, uint8_t container,
                               const std::vector<uint8_t>& data, uint16_t count) {
  std::vector<uint8_t> b(32, 0);
  Put<uint64_t>(b, 0, hash);
  b[8] = type;
  b[9] = container;
  Put<uint16_t>(b, 10, count);
  Put<uint16_t>(b, 12, 32);
  const size_t size = (32 + data.size() + 15) & ~15;
  Put<uint16_t>(b, 14, uint16_t(size));
  b.resize(size, 0);
  std::memcpy(&b[32], data.data(), data.size());
  return b;
}

void AppendU64(std::vector<uint8_t>& b, uint64_t v) {
  uint8_t tmp[8];
  std::memcpy(tmp, &v, 8);
  b.insert(b.end(), tmp, tmp + 8);
}
void AppendF32(std::vector<uint8_t>& b, float v) {
  uint8_t tmp[4];
  std::memcpy(tmp, &v, 4);
  b.insert(b.end(), tmp, tmp + 4);
}
void AppendI32(std::vector<uint8_t>& b, int32_t v) {
  uint8_t tmp[4];
  std::memcpy(tmp, &v, 4);
  b.insert(b.end(), tmp, tmp + 4);
}

// Two entities: a StadiumModel (modelFile FilePtr + transform EntityPtr +
// flags/color/drawMode StringMap) and a TransformEntity with non-identity
// scale/rotation/translation. Built with its own string table so no PES bytes
// appear anywhere in this test.
std::vector<uint8_t> SyntheticFox2() {
  constexpr uint64_t kModelAddr = 0x1000, kTransformAddr = 0x1100;
  std::vector<uint8_t> b(32, 0);
  // NOTE: never "\xf2box5" — \x eats the 'b' as a hex digit. Real files start
  // with F2 62 6F 78 35.
  const uint8_t kMagic[] = {0xF2, 'b', 'o', 'x', '5'};
  std::memcpy(b.data(), kMagic, sizeof kMagic);
  std::vector<uint8_t> entities;
  // --- StadiumModel @0x1000 ---
  std::vector<uint8_t> e0(64, 0);
  Put<uint32_t>(e0, 10, uint32_t(kModelAddr));
  Put<uint64_t>(e0, 0x1c, H("StadiumModel"));
  std::vector<std::vector<uint8_t>> props0;
  std::vector<uint8_t> modelFile;
  AppendU64(modelFile, H("/Assets/test/scenes/center1.fmdl"));
  props0.push_back(PropBlock(H("modelFile"), 20, 0, modelFile, 1));
  std::vector<uint8_t> transform;
  AppendU64(transform, kTransformAddr);
  props0.push_back(PropBlock(H("transform"), 13, 0, transform, 1));
  std::vector<uint8_t> flags;
  AppendI32(flags, 7);
  props0.push_back(PropBlock(H("flags"), 5, 0, flags, 1));
  std::vector<uint8_t> color;
  for (float f : {1.0f, 0.5f, 0.25f, 1.0f}) AppendF32(color, f);
  props0.push_back(PropBlock(H("color"), 19, 0, color, 1));
  std::vector<uint8_t> drawMode;
  AppendU64(drawMode, H("opaque"));
  AppendI32(drawMode, 0);
  AppendU64(drawMode, H("cutout"));
  AppendI32(drawMode, 1);
  props0.push_back(PropBlock(H("drawMode"), 5, 2, drawMode, 2));
  Put<uint16_t>(e0, 0x24, uint16_t(props0.size()));
  size_t e0size = 64;
  for (const auto& p : props0) e0size += p.size();
  Put<uint32_t>(e0, 0x30, uint32_t(e0size));
  entities.insert(entities.end(), e0.begin(), e0.end());
  for (const auto& p : props0) entities.insert(entities.end(), p.begin(), p.end());

  // --- TransformEntity @0x1100 ---
  std::vector<uint8_t> e1(64, 0);
  Put<uint32_t>(e1, 10, uint32_t(kTransformAddr));
  Put<uint64_t>(e1, 0x1c, H("TransformEntity"));
  std::vector<std::vector<uint8_t>> props1;
  std::vector<uint8_t> scale;
  for (float f : {2.0f, 3.0f, 4.0f, 0.0f}) AppendF32(scale, f);
  props1.push_back(PropBlock(H("transform_scale"), 14, 0, scale, 1));
  std::vector<uint8_t> rot;
  for (float f : {0.0f, 1.0f, 0.0f, 0.0f}) AppendF32(rot, f);
  props1.push_back(PropBlock(H("transform_rotation_quat"), 16, 0, rot, 1));
  std::vector<uint8_t> trans;
  for (float f : {10.0f, 20.0f, 30.0f, 0.0f}) AppendF32(trans, f);
  props1.push_back(PropBlock(H("transform_translation"), 14, 0, trans, 1));
  Put<uint16_t>(e1, 0x24, uint16_t(props1.size()));
  size_t e1size = 64;
  for (const auto& p : props1) e1size += p.size();
  Put<uint32_t>(e1, 0x30, uint32_t(e1size));
  entities.insert(entities.end(), e1.begin(), e1.end());
  for (const auto& p : props1) entities.insert(entities.end(), p.begin(), p.end());

  Put<uint32_t>(b, 8, 2);                       // entity count
  Put<uint32_t>(b, 12, uint32_t(32 + entities.size()));  // string table offset
  Put<uint32_t>(b, 16, 32);                     // entity-data offset
  b.insert(b.end(), entities.begin(), entities.end());
  // String table: every hash the blob references.
  for (const char* s :
       {"StadiumModel", "TransformEntity", "modelFile", "transform", "flags", "color", "drawMode",
        "transform_scale", "transform_rotation_quat", "transform_translation", "/Assets/test/scenes/center1.fmdl",
        "opaque", "cutout"}) {
    Put<uint64_t>(b, b.size(), H(s));
    Put<uint32_t>(b, b.size(), uint32_t(std::strlen(s)));
    b.insert(b.end(), s, s + std::strlen(s));
  }
  Put<uint64_t>(b, b.size(), 0);  // table terminator: zero hash + zero length
  Put<uint32_t>(b, b.size(), 0);
  return b;
}
}  // namespace

TEST(Fox2, SyntheticSceneParsesToExpectedValues) {
  const std::vector<uint8_t> blob = SyntheticFox2();
  std::string err;
  auto file = pes::ParseFox2(blob, &err);
  ASSERT_TRUE(file) << err;
  ASSERT_EQ(file->entities.size(), 2u);

  const pes::Fox2Entity& model = file->entities[0];
  EXPECT_EQ(model.className, "StadiumModel");
  EXPECT_EQ(model.address, 0x1000u);
  const pes::Fox2Property* modelFile = model.Find("modelFile");
  ASSERT_TRUE(modelFile != nullptr);
  EXPECT_EQ(modelFile->type, 20);
  ASSERT_EQ(modelFile->values.size(), 1u);
  EXPECT_EQ(modelFile->values[0].s, "/Assets/test/scenes/center1.fmdl");
  const pes::Fox2Property* transform = model.Find("transform");
  ASSERT_TRUE(transform != nullptr);
  ASSERT_EQ(transform->values.size(), 1u);
  EXPECT_EQ(transform->values[0].u, 0x1100u);  // EntityPtr kept as the address
  const pes::Fox2Property* flags = model.Find("flags");
  ASSERT_TRUE(flags != nullptr);
  EXPECT_EQ(flags->values[0].u, 7u);
  const pes::Fox2Property* color = model.Find("color");
  ASSERT_TRUE(color != nullptr);
  EXPECT_FLOAT_EQ(color->values[0].v4.x, 1.0f);
  EXPECT_FLOAT_EQ(color->values[0].v4.y, 0.5f);
  EXPECT_FLOAT_EQ(color->values[0].v4.z, 0.25f);
  EXPECT_FLOAT_EQ(color->values[0].v4.w, 1.0f);
  const pes::Fox2Property* drawMode = model.Find("drawMode");
  ASSERT_TRUE(drawMode != nullptr);
  EXPECT_EQ(drawMode->container, 2);
  ASSERT_EQ(drawMode->keys.size(), 2u);
  EXPECT_EQ(drawMode->keys[0], "opaque");
  EXPECT_EQ(drawMode->keys[1], "cutout");
  ASSERT_EQ(drawMode->values.size(), 2u);
  EXPECT_EQ(drawMode->values[0].u, 0u);
  EXPECT_EQ(drawMode->values[1].u, 1u);
  EXPECT_TRUE(model.Find("missing") == nullptr);

  const pes::Fox2Entity* target = file->ByAddress(transform->values[0].u);
  ASSERT_TRUE(target != nullptr);
  EXPECT_EQ(target->className, "TransformEntity");
  const pes::Fox2Property* scale = target->Find("transform_scale");
  ASSERT_TRUE(scale != nullptr);
  EXPECT_FLOAT_EQ(scale->values[0].v3.x, 2.0f);
  EXPECT_FLOAT_EQ(scale->values[0].v3.y, 3.0f);
  EXPECT_FLOAT_EQ(scale->values[0].v3.z, 4.0f);
  const pes::Fox2Property* quat = target->Find("transform_rotation_quat");
  ASSERT_TRUE(quat != nullptr);
  EXPECT_FLOAT_EQ(quat->values[0].q.x, 0.0f);
  EXPECT_FLOAT_EQ(quat->values[0].q.y, 1.0f);
  EXPECT_FLOAT_EQ(quat->values[0].q.z, 0.0f);
  EXPECT_FLOAT_EQ(quat->values[0].q.w, 0.0f);
  const pes::Fox2Property* translation = target->Find("transform_translation");
  ASSERT_TRUE(translation != nullptr);
  EXPECT_FLOAT_EQ(translation->values[0].v3.x, 10.0f);
  EXPECT_FLOAT_EQ(translation->values[0].v3.y, 20.0f);
  EXPECT_FLOAT_EQ(translation->values[0].v3.z, 30.0f);
  EXPECT_TRUE(file->ByAddress(0x9999) == nullptr);
}

TEST(Fox2, MalformedBlobsFailWithReason) {
  const std::vector<uint8_t> good = SyntheticFox2();
  std::string err;
  auto fails = [&](std::vector<uint8_t> blob) {
    err.clear();
    EXPECT_TRUE(!pes::ParseFox2(blob, &err)) << "blob of " << blob.size() << " bytes parsed";
    EXPECT_TRUE(!err.empty()) << "no reason for " << blob.size() << "-byte blob";
  };
  fails({});
  std::vector<uint8_t> badMagic = good;
  std::memcpy(badMagic.data(), "NOTFOX2!", 8);
  fails(badMagic);
  fails(std::vector<uint8_t>(good.begin(), good.begin() + 31));  // header cut
  fails(std::vector<uint8_t>(good.begin(), good.begin() + 100));  // entity cut
  std::vector<uint8_t> badCount = good;  // claims more entities than present
  const uint8_t kNine[] = {9, 0, 0, 0};
  std::memcpy(&badCount[8], kNine, sizeof kNine);
  fails(badCount);
  std::vector<uint8_t> badEsize = good;  // first entity runs past the file
  const uint8_t kHuge[] = {0xFF, 0xFF, 0xFF, 0x7F};
  std::memcpy(&badEsize[32 + 0x30], kHuge, sizeof kHuge);
  fails(badEsize);
  std::vector<uint8_t> badTable = good;  // string table starts out of range
  std::memcpy(&badTable[12], kHuge, sizeof kHuge);
  fails(badTable);
}

// Install-reading checks: observable facts only, never PES values as
// expectations (the string table carries the names at runtime).
TEST(Fox2Install, StadiumPackParsesWithStadiumModels) {
  const char* root = std::getenv("PES21_ROOT");
  if (!root) GTEST_SKIP() << "PES21_ROOT not set";
  pes::PesVfs vfs;
  std::string err;
  ASSERT_TRUE(vfs.Mount(root, &err)) << err;
  auto fpkd = vfs.Read("Asset/model/bg/st002/#Win/st002.fpkd", &err);
  ASSERT_TRUE(fpkd) << err;
  auto pack = pes::FpkArchive::Parse(*fpkd, &err);
  ASSERT_TRUE(pack) << err;
  ASSERT_EQ(pack->entries.size(), 1u);
  const pes::FpkEntry& entry = pack->entries[0];
  ASSERT_LE(size_t(entry.offset) + entry.size, fpkd->size());
  auto file = pes::ParseFox2(std::span<const uint8_t>(fpkd->data() + entry.offset, entry.size), &err);
  ASSERT_TRUE(file) << err;
  ASSERT_GT(file->entities.size(), 0u);
  bool sawStadiumModel = false, sawTransformRef = false;
  for (const auto& e : file->entities) {
    EXPECT_TRUE(e.className.empty() || e.className[0] != '#') << e.className;
    if (e.className == "StadiumModel") {
      sawStadiumModel = true;
      const pes::Fox2Property* modelFile = e.Find("modelFile");
      ASSERT_TRUE(modelFile != nullptr);
      EXPECT_TRUE(!modelFile->values.empty() && !modelFile->values[0].s.empty());
      const pes::Fox2Property* transform = e.Find("transform");
      ASSERT_TRUE(transform != nullptr);
      ASSERT_TRUE(!transform->values.empty());
      const pes::Fox2Entity* target = file->ByAddress(transform->values[0].u);
      ASSERT_TRUE(target != nullptr) << "transform target missing";
      EXPECT_EQ(target->className, "TransformEntity");
      sawTransformRef = true;
    }
  }
  EXPECT_TRUE(sawStadiumModel);
  EXPECT_TRUE(sawTransformRef);
}

TEST(Fox2Install, PitchGoalAndCornerflagPacksParse) {
  const char* root = std::getenv("PES21_ROOT");
  if (!root) GTEST_SKIP() << "PES21_ROOT not set";
  pes::PesVfs vfs;
  std::string err;
  ASSERT_TRUE(vfs.Mount(root, &err)) << err;
  for (const char* path : {"Asset/model/bg/st002/pitch/#Win/pitch_st002.fpkd",
                           "Asset/model/bg/common/goal/#Win/goal_post.fpkd",
                           "Asset/model/bg/st002/cornerflag/#Win/cornerflag_00108.fpkd"}) {
    auto fpkd = vfs.Read(path, &err);
    ASSERT_TRUE(fpkd) << err << " " << path;
    auto pack = pes::FpkArchive::Parse(*fpkd, &err);
    ASSERT_TRUE(pack) << err << " " << path;
    ASSERT_EQ(pack->entries.size(), 1u) << path;
    const pes::FpkEntry& entry = pack->entries[0];
    auto file = pes::ParseFox2(std::span<const uint8_t>(fpkd->data() + entry.offset, entry.size), &err);
    ASSERT_TRUE(file) << err << " " << path;
    EXPECT_GT(file->entities.size(), 0u) << path;
    for (const auto& e : file->entities)
      for (const auto& p : e.props)
        EXPECT_TRUE(e.className.empty() || e.className[0] != '#')
            << path << " " << e.className << "." << p.name;
  }
}
