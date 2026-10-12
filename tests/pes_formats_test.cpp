#include <gtest/gtest.h>
#include <zlib.h>

#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "pes/fmdl.hpp"
#include "pes/fpk.hpp"
#include <algorithm>

#include "pes/ftex.hpp"
#include "pes/pack_rules.hpp"
#include "pes/vfs.hpp"

namespace {

// ---- synthetic ftex: header layout from GF ftex.py ----
constexpr size_t FTEX_HEADER = 64, MIP_INFO = 16;
constexpr uint16_t PF_RGBA8 = 0;

template <typename T>
void Put(std::vector<uint8_t>& b, size_t at, T v) {
  if (b.size() < at + sizeof v) b.resize(at + sizeof v);
  std::memcpy(&b[at], &v, sizeof v);
}

std::vector<uint8_t> FtexHeader(uint16_t w, uint16_t h, uint8_t mips) {
  std::vector<uint8_t> b(FTEX_HEADER, 0);
  std::memcpy(b.data(), "FTEX", 4);
  Put<uint16_t>(b, 8, PF_RGBA8);
  Put<uint16_t>(b, 10, w);  // width before height
  Put<uint16_t>(b, 12, h);
  b[16] = mips;
  return b;
}

const std::vector<uint8_t> kTexels = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24};

}  // namespace

TEST(Ftex, InlineRawMipKeepsWidthBeforeHeight) {
  auto b = FtexHeader(3, 2, 1);
  const uint32_t dataAt = FTEX_HEADER + MIP_INFO;
  Put<uint32_t>(b, FTEX_HEADER, dataAt);
  Put<uint32_t>(b, FTEX_HEADER + 4, uint32_t(kTexels.size()));
  b.resize(dataAt);  // the mip info record is 16 bytes; data follows it
  b.insert(b.end(), kTexels.begin(), kTexels.end());
  std::string err;
  auto img = pes::ParseFtex(b, &err);
  ASSERT_TRUE(img) << err;
  EXPECT_EQ(img->width, 3);
  EXPECT_EQ(img->height, 2);
  ASSERT_EQ(img->mips.size(), 1u);
  EXPECT_EQ(img->mips[0], kTexels);
}

// Chunked mip: one chunk flagged stored (bit 31), one zlib-compressed.
TEST(Ftex, ChunkedMipJoinsStoredAndDeflatedChunks) {
  auto b = FtexHeader(3, 2, 1);
  const std::vector<uint8_t> first(kTexels.begin(), kTexels.begin() + 8), second(kTexels.begin() + 8, kTexels.end());
  std::vector<uint8_t> deflated(compressBound(second.size()));
  uLongf n = deflated.size();
  ASSERT_EQ(compress(deflated.data(), &n, second.data(), second.size()), Z_OK);
  deflated.resize(n);

  const uint32_t mipAt = FTEX_HEADER + MIP_INFO;
  constexpr uint32_t CHUNK_TABLE = 2 * 8, STORED = 0x80000000;
  Put<uint32_t>(b, FTEX_HEADER, mipAt);
  Put<uint16_t>(b, FTEX_HEADER + 14, 2);  // chunk count
  Put<uint16_t>(b, mipAt, uint16_t(first.size()));
  Put<uint16_t>(b, mipAt + 2, uint16_t(first.size()));
  Put<uint32_t>(b, mipAt + 4, CHUNK_TABLE | STORED);
  Put<uint16_t>(b, mipAt + 8, uint16_t(deflated.size()));
  Put<uint16_t>(b, mipAt + 10, uint16_t(second.size()));
  Put<uint32_t>(b, mipAt + 12, CHUNK_TABLE + uint32_t(first.size()));
  b.insert(b.end(), first.begin(), first.end());
  b.insert(b.end(), deflated.begin(), deflated.end());

  std::string err;
  auto img = pes::ParseFtex(b, &err);
  ASSERT_TRUE(img) << err;
  EXPECT_EQ(img->mips[0], kTexels);
}

TEST(Ftex, RejectsSidecarFtexs) {
  auto b = FtexHeader(4, 4, 1);
  b[0x20] = 1;
  std::string err;
  EXPECT_FALSE(pes::ParseFtex(b, &err));
  EXPECT_NE(err.find("ftexs"), std::string::npos);
}

// docs/re/pes-re-ftex.md 2.2: only the bit-31 flag marks a stored chunk. On
// official files every unflagged chunk is zlib, even when its two size fields
// are equal (997 kit textures).
TEST(Ftex, EqualSizedUnflaggedChunkIsZlib) {
  auto b = FtexHeader(4, 4, 1);
  const std::vector<uint8_t> plain(64, 7);
  std::vector<uint8_t> z(compressBound(plain.size()));
  uLongf n = z.size();
  ASSERT_EQ(compress(z.data(), &n, plain.data(), plain.size()), Z_OK);
  z.resize(plain.size(), 0);  // the zlib stream, zero-padded to the plain size
  const uint32_t mipAt = FTEX_HEADER + MIP_INFO;
  Put<uint32_t>(b, FTEX_HEADER, mipAt);
  Put<uint16_t>(b, FTEX_HEADER + 14, 1);
  Put<uint16_t>(b, mipAt, uint16_t(z.size()));
  Put<uint16_t>(b, mipAt + 2, uint16_t(plain.size()));
  Put<uint32_t>(b, mipAt + 4, 8);
  b.insert(b.end(), z.begin(), z.end());
  std::string err;
  auto img = pes::ParseFtex(b, &err);
  ASSERT_TRUE(img) << err;
  EXPECT_EQ(img->mips[0], plain);
}

// A chunk size field of 0 means 65,536 bytes.
TEST(Ftex, ZeroSizeFieldsMean64KiB) {
  constexpr size_t FULL_CHUNK = 65536;
  auto b = FtexHeader(128, 128, 1);
  const uint32_t mipAt = FTEX_HEADER + MIP_INFO;
  Put<uint32_t>(b, FTEX_HEADER, mipAt);
  Put<uint16_t>(b, FTEX_HEADER + 14, 1);
  Put<uint16_t>(b, mipAt, 0);
  Put<uint16_t>(b, mipAt + 2, 0);
  Put<uint32_t>(b, mipAt + 4, 8u | 0x80000000u);
  b.resize(mipAt + 8 + FULL_CHUNK, 3);
  std::string err;
  auto img = pes::ParseFtex(b, &err);
  ASSERT_TRUE(img) << err;
  EXPECT_EQ(img->mips[0].size(), FULL_CHUNK);
}

// textureType is the u32 at +0x1C (the one at +0x18 is always 0); arrays carry
// their slice count in the u16 at +0x22; entries are slice-major.
TEST(Ftex, TypeAndSlicesComeFromTheirOwnFields) {
  constexpr uint32_t TYPE_CUBE = 4, TYPE_ARRAY = 0x10;
  constexpr int SLICES = 3;
  auto withEntries = [&](uint32_t type, int entries) {
    auto b = FtexHeader(1, 1, 1);
    Put<uint32_t>(b, 0x1C, type);
    Put<uint16_t>(b, 0x22, uint16_t(SLICES));
    const uint32_t dataAt = uint32_t(FTEX_HEADER + MIP_INFO * entries);
    b.resize(dataAt, 0);  // whole mip table, zeroed: chunkCount 0 = data inline
    for (int e = 0; e < entries; ++e) {
      Put<uint32_t>(b, FTEX_HEADER + MIP_INFO * e, dataAt + 4 * e);
      Put<uint32_t>(b, FTEX_HEADER + MIP_INFO * e + 4, 4);
    }
    b.resize(dataAt + 4 * entries, uint8_t(9));
    return b;
  };
  std::string err;
  auto array = pes::ParseFtex(withEntries(TYPE_ARRAY, SLICES), &err);
  ASSERT_TRUE(array) << err;
  EXPECT_EQ(array->layers, SLICES);
  EXPECT_EQ(array->mips.size(), size_t(SLICES));
  auto cube = pes::ParseFtex(withEntries(TYPE_CUBE, 6), &err);
  ASSERT_TRUE(cube) << err;
  EXPECT_TRUE(cube->cube);
  EXPECT_EQ(cube->mips.size(), 6u);
  auto flat = withEntries(0, 1);
  Put<uint32_t>(flat, 0x18, TYPE_CUBE);  // the old, wrong offset: must be ignored
  auto plain = pes::ParseFtex(flat, &err);
  ASSERT_TRUE(plain) << err;
  EXPECT_FALSE(plain->cube);
  EXPECT_EQ(plain->layers, 1);
}

// ---- synthetic fmdl ----
TEST(Fmdl, ModelWithoutMeshInfoIsEmptyNotAnError) {
  std::vector<uint8_t> b(64, 0);
  std::memcpy(b.data(), "FMDL", 4);
  Put<uint32_t>(b, 4, 0x4001eb85);  // version 2.03
  Put<uint64_t>(b, 8, 56);          // section info at 56, zero blocks
  std::string err;
  auto m = pes::ParseFmdl(b, &err);
  ASSERT_TRUE(m) << err;
  EXPECT_TRUE(m->meshes.empty());
}

TEST(Fmdl, RejectsOtherVersions) {
  std::vector<uint8_t> b(64, 0);
  std::memcpy(b.data(), "FMDL", 4);
  Put<float>(b, 4, 2.04f);
  std::string err;
  EXPECT_FALSE(pes::ParseFmdl(b, &err));
}

// ---- pack rules (GF fmdl_to_fullbody.py conventions) ----
TEST(PackRules, NonRenderPassesMatchWholeWords) {
  EXPECT_TRUE(pes::IsNonRenderPass("outline", ""));
  EXPECT_TRUE(pes::IsNonRenderPass("player antiblur", "nico"));
  EXPECT_TRUE(pes::IsNonRenderPass("player", "body_outline"));
  EXPECT_FALSE(pes::IsNonRenderPass("player", "outlined_crest"));
}

TEST(PackRules, KitSlotIsDummyKitOrBareUniformSlot) {
  EXPECT_TRUE(pes::IsKitSlotTexture("dummy_kit"));
  EXPECT_TRUE(pes::IsKitSlotTexture("u0XXXp0"));
  EXPECT_FALSE(pes::IsKitSlotTexture("u0XXXp1"));
  EXPECT_FALSE(pes::IsKitSlotTexture("dummy_nrm"));
  EXPECT_TRUE(pes::IsPlaceholderTexture("dummy_nrm"));
}

TEST(PackRules, EffectTextures) {
  EXPECT_TRUE(pes::IsEffectTexture("goku_aura_02"));
  EXPECT_TRUE(pes::IsEffectTexture("FX_glow"));
  EXPECT_FALSE(pes::IsEffectTexture("face_bsm_alp"));
}

TEST(PackRules, TextureStemDropsDirectoryAndExtension) {
  EXPECT_EQ(pes::TextureStem("/Assets/pes16/model/x/face.dds"), "face");
  EXPECT_EQ(pes::TextureStem("dummy_nrm.tga"), "dummy_nrm");
}

// ---- against the user's install ----
namespace {

constexpr int NICO_FACE = 70922;  // a head pack: face_high.fmdl with a kit-slot cap

const pes::PesVfs* Install() {
  static pes::PesVfs vfs;
  static int state = 0;  // 0 untried, 1 mounted, -1 absent
  if (state == 0) {
    const char* root = std::getenv("PES21_ROOT");
    std::string err;
    state = root && vfs.Mount(root, &err) ? 1 : -1;
  }
  return state == 1 ? &vfs : nullptr;
}

#define REQUIRE_INSTALL()                     \
  const pes::PesVfs* vfs = Install();         \
  if (!vfs) GTEST_SKIP() << "PES21_ROOT not set"

std::optional<pes::Fmdl> FaceHigh(const pes::PesVfs& vfs, int face) {
  std::string err;
  auto pack = vfs.Read(pes::FacePackPath(face), &err);
  if (!pack) return std::nullopt;
  auto fpk = pes::FpkArchive::Parse(*pack, &err);
  for (const auto& e : fpk->entries)
    if (e.path == "face_high.fmdl") return pes::ParseFmdl(std::span(*pack).subspan(e.offset, e.size), &err);
  return std::nullopt;
}

}  // namespace

TEST(FmdlInstall, MeshesAreConsistent) {
  REQUIRE_INSTALL();
  auto m = FaceHigh(*vfs, NICO_FACE);
  ASSERT_TRUE(m);
  ASSERT_EQ(m->meshes.size(), 4u);
  constexpr float UNORM8_WEIGHT_SUM_TOLERANCE = 4.0f / 255.0f;  // four rounded unorm8 weights
  for (const auto& mesh : m->meshes) {
    ASSERT_FALSE(mesh.pos.empty());
    EXPECT_EQ(mesh.indices.size() % 3, 0u);
    for (uint32_t i : mesh.indices) ASSERT_LT(i, mesh.pos.size());
    ASSERT_EQ(mesh.weights.size(), mesh.pos.size());
    for (const auto& w : mesh.weights) EXPECT_NEAR(w.x + w.y + w.z + w.w, 1.0f, UNORM8_WEIGHT_SUM_TOLERANCE);
    for (const auto& j : mesh.joints)
      for (uint16_t k : j) ASSERT_LT(k, mesh.boneGroup.size());
  }
}

// The pack is a head: every vertex sits between neck and crown height.
TEST(FmdlInstall, HeadPackSpansNeckToCrown) {
  REQUIRE_INSTALL();
  auto m = FaceHigh(*vfs, NICO_FACE);
  ASSERT_TRUE(m);
  constexpr float NECK_Y_M = 1.4f, CROWN_Y_M = 2.0f;
  for (const auto& mesh : m->meshes)
    for (const auto& p : mesh.pos) {
      ASSERT_GT(p.y, NECK_Y_M);
      ASSERT_LT(p.y, CROWN_Y_M);
    }
  EXPECT_EQ(m->materials[0].name, "player");
  EXPECT_NE(m->materials[0].shader.find("constant"), std::string::npos);
}

TEST(TextureResolverInstall, StoredDirectoryMapsIntoVfs) {
  REQUIRE_INSTALL();
  pes::TextureResolver r(*vfs, NICO_FACE);
  EXPECT_EQ(r.Resolve({"/Assets/pes16/model/character/face/real/70922/sourceimages/", "nico.dds"}),
            "Asset/model/character/face/real/70922/sourceimages/#windx11/nico.ftex");
  EXPECT_EQ(r.Resolve({"/Assets/pes16/nowhere/", "no_such_texture_anywhere.dds"}), "");
}

TEST(FtexInstall, DecodesFullMipChain) {
  REQUIRE_INSTALL();
  std::string err;
  auto bytes = vfs->Read("Asset/model/character/face/real/70922/sourceimages/#windx11/nico.ftex", &err);
  ASSERT_TRUE(bytes) << err;
  auto img = pes::ParseFtex(*bytes, &err);
  ASSERT_TRUE(img) << err;
  EXPECT_GT(img->width, 0);
  EXPECT_GT(img->height, 0);
  ASSERT_EQ(int(img->mips.size()), img->mipCount);
  for (size_t i = 1; i < img->mips.size(); ++i) EXPECT_LE(img->mips[i].size(), img->mips[i - 1].size());
}

namespace {
// D3D size of one mip (docs/re/pes-re-ftex.md 2.3).
size_t MipBytes(int pf, int w, int h) {
  const bool bc8 = pf == 2 || pf == 8, bc16 = pf == 3 || pf == 4 || pf == 9 || pf == 10 || pf == 11;
  if (bc8 || bc16) return size_t((w + 3) / 4) * ((h + 3) / 4) * (bc8 ? 8 : 16);
  const int bpp = pf == 0 ? 4 : pf == 1 ? 1 : pf == 12 ? 8 : pf == 13 ? 16 : 4;
  return size_t(w) * h * bpp;
}
}  // namespace

// Every mip of every official kit texture decodes to its exact size (the
// equal-size chunk bug corrupted 997 of them), and cubes and arrays are
// recognised. Official kits are teams below 701; the 4cc teams (701+) ship
// mod-tool files whose small mips are short, which the strict census also
// rejects (pes-re-ftex.md 3.1) and PES tolerates.
TEST(FtexInstall, KitTexturesCubeAndArrayDecodeExactly) {
  REQUIRE_INSTALL();
  constexpr int UNI_PATTERN_SLICES = 18;  // docs/re/pes-re-ftex.md 3.2
  std::string err;
  auto check = [&](const std::string& path) {
    auto bytes = vfs->Read(path, &err);
    if (!bytes) return ADD_FAILURE() << path << ": " << err, std::optional<pes::FtexImage>{};
    auto img = pes::ParseFtex(*bytes, &err);
    if (!img) return ADD_FAILURE() << path << ": " << err, std::optional<pes::FtexImage>{};
    const int images = img->cube ? 6 : img->layers;
    EXPECT_EQ(img->mips.size(), size_t(images * img->mipCount)) << path;
    for (int i = 0; i < images && size_t(i * img->mipCount) < img->mips.size(); ++i)
      for (int m = 0; m < img->mipCount; ++m)
        EXPECT_EQ(img->mips[i * img->mipCount + m].size(),
                  MipBytes(img->pixelFormat, std::max(1, img->width >> m), std::max(1, img->height >> m)))
            << path << " image " << i << " mip " << m;
    return img;
  };
  int kits = 0;
  for (const auto& path : vfs->List("Asset/model/character/uniform/texture/#windx11/"))
    if (path.ends_with(".ftex")) {
      constexpr int FIRST_4CC_TEAM = 701;
      // <letter><team %04d>[pg]N...: u0001p1, a0150g1_back, ...
      const std::string file = path.substr(path.rfind('/') + 1);
      if (file.size() < 5 || !std::all_of(file.begin() + 1, file.begin() + 5, ::isdigit)) continue;
      if (std::stoi(file.substr(1, 4)) >= FIRST_4CC_TEAM) continue;
      ++kits, check(path);
    }
  EXPECT_GT(kits, 0);
  // An official face normal map whose 64 KiB chunks have size fields of 0.
  check("Asset/model/character/face/real/100393/sourceimages/#windx11/face_nrm.ftex");
  auto cube = check("Asset/model/character/common/sourceimages/#windx11/eye_catch_cbm.ftex");
  ASSERT_TRUE(cube);
  EXPECT_TRUE(cube->cube);
  auto array = check("Asset/model/character/common/sourceimages/#windx11/uni_pattern.ftex");
  ASSERT_TRUE(array);
  EXPECT_EQ(array->layers, UNI_PATTERN_SLICES);
  // Slices are distinct images (15 of 18 differ; 0, 2 and 12 are identical).
  EXPECT_NE(array->mips[0], array->mips[size_t(array->mipCount)]);
}

// Data/dt30_g4.cpk holds whole cpks (Asset/model/bg/stNNN.cpk, common.cpk).
// Their contents are part of the file system the game reads: stadiums, goals,
// pitch (docs/re/pes-re-stadium-camera.md 1.1). They mount at their parent's
// precedence, so download overrides still win, as the 4cc packs rely on.
TEST(VfsInstall, SeesInsideNestedCpksBelowDownloadOverrides) {
  REQUIRE_INSTALL();
  constexpr const char* NESTED_ONLY = "Asset/model/bg/st002/audi/sourceimages/au_seat.png";
  ASSERT_TRUE(vfs->Exists(NESTED_ONLY)) << "content inside st002.cpk is not mounted";
  std::string err;
  auto blob = vfs->Read(NESTED_ONLY, &err);
  ASSERT_TRUE(blob) << err;
  constexpr uint8_t PNG_MAGIC[] = {0x89, 'P', 'N', 'G'};
  ASSERT_GE(blob->size(), sizeof PNG_MAGIC);
  EXPECT_EQ(std::memcmp(blob->data(), PNG_MAGIC, sizeof PNG_MAGIC), 0);
  // In both: nested original 48,342,208 B; the 4cc download override wins.
  constexpr uint64_t NESTED_ORIGINAL_BYTES = 48342208;
  auto size = vfs->Size("Asset/model/bg/st002/#Win/st002.fpk");
  ASSERT_TRUE(size);
  EXPECT_NE(*size, NESTED_ORIGINAL_BYTES);
}
