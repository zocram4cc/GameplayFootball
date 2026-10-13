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

// A face id present in vanilla dt data (dt36_g4), and the ids this install's
// download packs add on top of it: content no download folder ships.
constexpr int VANILLA_FACE = 100117;  // 303.0K pack, 7 mesh materials, all skins
constexpr int DOWNLOAD_FACE = 70922;  // a 4cc head: face_high.fmdl with a kit-slot cap

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

// The head pack's own skins, checked on data every install ships. The 4cc face
// packs replace the whole material set (player/kit/outline, fox_3ddf_constant),
// so what they cover is checked separately below.
const std::vector<std::string>& HeadPackMaterials() {
  static const std::vector<std::string> kMaterials = {"fox_skin_mat", "eyeline_SG", "collar_ao",
                                                      "fox_eyeOcclusion_mat", "fox_eyelash_mat"};
  return kMaterials;
}

// The mount report names each source "download <name>  <n> entries" for a cpk
// that mounted and "download  no DpFileList.bin, skipped" when the install has
// no download folder, so the marker "download " followed by a file name tells
// the two apart.
bool HasDownloadMounts(const std::string& report) {
  const std::string skipped = "download  no DpFileList.bin";
  std::string::size_type at = report.find("download ");
  while (at != std::string::npos) {
    const std::string::size_type end = report.find('\n', at);
    const std::string line = report.substr(at, end == std::string::npos ? std::string::npos : end - at);
    if (line.rfind(skipped, 0) != 0) return true;
    at = report.find("download ", end == std::string::npos ? std::string::npos : end);
  }
  return false;
}

}  // namespace

TEST(FmdlInstall, MeshesAreConsistent) {
  REQUIRE_INSTALL();
  auto m = FaceHigh(*vfs, VANILLA_FACE);
  ASSERT_TRUE(m) << pes::FacePackPath(VANILLA_FACE);
  ASSERT_EQ(m->materials.size(), HeadPackMaterials().size());
  for (size_t i = 0; i < m->materials.size(); ++i)
    EXPECT_EQ(m->materials[i].name, HeadPackMaterials()[i]) << "material " << i;
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

// The pack is a head: every vertex sits between the collar shell and the
// crown. The collar AO mesh (collar_ao, a neck-blend decal) is the lowest
// part of the pack, measured at y = 1.38537 on face 100117; the crown limit
// is generous enough for long hair (skin itself stops at 1.80).
TEST(FmdlInstall, HeadPackSpansNeckToCrown) {
  REQUIRE_INSTALL();
  auto m = FaceHigh(*vfs, VANILLA_FACE);
  ASSERT_TRUE(m) << pes::FacePackPath(VANILLA_FACE);
  constexpr float COLLAR_Y_M = 1.38f, CROWN_Y_M = 2.0f;
  for (const auto& mesh : m->meshes)
    for (const auto& p : mesh.pos) {
      ASSERT_GT(p.y, COLLAR_Y_M);
      ASSERT_LT(p.y, CROWN_Y_M);
    }
  EXPECT_EQ(m->materials[0].name, "fox_skin_mat");
  EXPECT_EQ(m->materials[0].technique, "pes3DDF_Skin_Face");
  EXPECT_NE(m->materials[0].shader.find("skin"), std::string::npos);
}

// A 4cc head pack: cubemap-free constant-shader skin, and the kit slot the
// material table names but the pack never textures. Only a download/ install
// ships this content, so it skips when the pack is absent.
TEST(FmdlInstall, DownloadHeadPackUsesTheConstantShader) {
  REQUIRE_INSTALL();
  auto m = FaceHigh(*vfs, DOWNLOAD_FACE);
  if (!m) GTEST_SKIP() << "no download face pack " << pes::FacePackPath(DOWNLOAD_FACE);
  ASSERT_EQ(m->materials[0].name, "player");
  EXPECT_EQ(m->materials[0].technique, "fox3DFW_ConstantSRGB_Plain");
  EXPECT_NE(m->materials[0].shader.find("constant"), std::string::npos);
}

// The stored directory of every official face texture maps into the VFS:
// /Assets/pes16/ -> Asset/, #windx11/ before the file name, .dds -> .ftex.
// This is a vanilla mapping: dt data stores the same /Assets/pes16/ prefix.
TEST(TextureResolverInstall, StoredDirectoryMapsIntoVfs) {
  REQUIRE_INSTALL();
  pes::TextureResolver r(*vfs, VANILLA_FACE);
  const std::string dir = "/Assets/pes16/model/character/face/real/" + std::to_string(VANILLA_FACE) + "/sourceimages/";
  EXPECT_EQ(r.Resolve({dir, "face_nrm.dds"}),
            "Asset/model/character/face/real/" + std::to_string(VANILLA_FACE) + "/sourceimages/#windx11/face_nrm.ftex");
  EXPECT_EQ(r.Resolve({"/Assets/pes16/nowhere/", "no_such_texture_anywhere.dds"}), "");
}

// A texture stored in a directory that has no #windx11 twin still resolves from
// its own pack folder: common/sourceimages holds .tga paths in vanilla. The kit
// atlas a0150p1.ftex lives in uniform/texture/#windx11/ and its stem appears
// nowhere else, so the stem-scan fallback cannot stand in: only the mapping
// produces this exact path.
TEST(TextureResolverInstall, PlainDirectoryResolvesThroughThePlatformSuffix) {
  REQUIRE_INSTALL();
  pes::TextureResolver r(*vfs, VANILLA_FACE);
  EXPECT_EQ(r.Resolve({"/Assets/pes16/model/character/common/sourceimages/", "face_eyelash_nrm.tga"}),
            "Asset/model/character/common/sourceimages/#windx11/face_eyelash_nrm.ftex");
  EXPECT_EQ(r.Resolve({"/Assets/pes16/model/character/uniform/texture/", "a0150p1.dds"}),
            "Asset/model/character/uniform/texture/#windx11/a0150p1.ftex");
}

TEST(FtexInstall, DecodesFullMipChain) {
  REQUIRE_INSTALL();
  std::string err;
  const std::string path = pes::FaceTextureDir(VANILLA_FACE) + "face_nrm.ftex";
  auto bytes = vfs->Read(path, &err);
  ASSERT_TRUE(bytes) << err;
  auto img = pes::ParseFtex(*bytes, &err);
  ASSERT_TRUE(img) << err;
  EXPECT_EQ(img->width, 1024);
  EXPECT_EQ(img->height, 1024);
  EXPECT_GT(img->mipCount, 1) << path;
  ASSERT_EQ(int(img->mips.size()), img->mipCount) << path;
  for (size_t i = 1; i < img->mips.size(); ++i) EXPECT_LE(img->mips[i].size(), img->mips[i - 1].size()) << path;
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
// pitch (docs/re/pes-re-stadium-camera.md 1.1).
TEST(VfsInstall, SeesInsideNestedCpksBelowDownloadOverrides) {
  REQUIRE_INSTALL();
  // Content that exists only inside the nested cpk: not in dt30's own TOC.
  constexpr const char* NESTED_ONLY = "Asset/model/bg/st002/audi/sourceimages/au_seat.png";
  ASSERT_TRUE(vfs->Exists(NESTED_ONLY)) << "content inside st002.cpk is not mounted";
  std::string err;
  auto blob = vfs->Read(NESTED_ONLY, &err);
  ASSERT_TRUE(blob) << err;
  constexpr uint8_t PNG_MAGIC[] = {0x89, 'P', 'N', 'G'};
  ASSERT_GE(blob->size(), sizeof PNG_MAGIC);
  EXPECT_EQ(std::memcmp(blob->data(), PNG_MAGIC, sizeof PNG_MAGIC), 0);
  // The nested cpk mounts below its parent, so a same-named nested entry never
  // shadows one the outer cpk has, and below every download/ mount, so an
  // override still wins. Without download/ cpks there is nothing to override
  // with, so that half skips cleanly.
  constexpr uint64_t NESTED_ORIGINAL_BYTES = 48342208;
  auto size = vfs->Size("Asset/model/bg/st002/#Win/st002.fpk");
  ASSERT_TRUE(size) << "the outer dt30 entry lost to its own nested cpk";
  if (!HasDownloadMounts(vfs->MountReport())) GTEST_SKIP() << "no download/ cpks: no override to test";
  EXPECT_NE(*size, NESTED_ORIGINAL_BYTES) << "a download/ cpk must override the nested original";
}

// Without download/ the nested original is what the game reads: the same path,
// the outer cpk's entry, and the nested cpk's own contents under it.
TEST(VfsInstall, NestedOriginalsSurviveWithoutDownloads) {
  REQUIRE_INSTALL();
  if (HasDownloadMounts(vfs->MountReport()))
    GTEST_SKIP() << "this install has download/ cpks: override precedence is tested above";
  constexpr uint64_t NESTED_ORIGINAL_BYTES = 48342208;
  auto size = vfs->Size("Asset/model/bg/st002/#Win/st002.fpk");
  ASSERT_TRUE(size);
  EXPECT_EQ(*size, NESTED_ORIGINAL_BYTES) << "the outer dt30 entry must win with no download/ cpks";
}

// Uniform shaders sample wrinkle normals and the fabric weave on UV1
// (pes-re-shaders.md 3.1), so every uniform mesh carries a second UV set.
TEST(FmdlInstall, UniformMeshesCarryUv1) {
  REQUIRE_INSTALL();
  std::string err;
  auto blob = vfs->Read("Asset/model/character/uniform/nocloth/#Win/sleeve_short_001.fmdl", &err);
  ASSERT_TRUE(blob) << err;
  auto m = pes::ParseFmdl(*blob, &err);
  ASSERT_TRUE(m) << err;
  ASSERT_FALSE(m->meshes.empty());
  for (const auto& mesh : m->meshes) {
    ASSERT_EQ(mesh.uv1.size(), mesh.pos.size());
    EXPECT_NE(mesh.uv1, mesh.uv0) << "UV1 is its own set, not a copy of UV0";
  }
}
