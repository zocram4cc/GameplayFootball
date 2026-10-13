#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <vector>

#include "pes/fmdl.hpp"

namespace {

// Synthetic FMDL 2.03, laid out as the real files are (pitch_st002 and
// goal_post on any install): a section table at +56, section-0 blocks after it,
// then one section-1 BUFFER block holding all the vertex streams.
//
// BUFFER_OFFSETS (block 14) carries one 16-byte entry per vertex-buffer slot:
// the slot's byte length at +4 and its start offset inside the vertex buffer at
// +8. The reader has to add that offset; without it the normal/tangent/UV
// stream is read out of the position stream, which is what produced a UV of
// ~29328 on the pitch.
constexpr uint32_t kVersion2_03 = 0x4001eb85;
constexpr size_t kSectionInfo = 56, kS0RecordBytes = 8, kS1RecordBytes = 12;
constexpr size_t kMeshInfoBytes = 48, kMeshFormatInfoBytes = 8, kMeshFormatBytes = 8;
constexpr size_t kVertexFormatBytes = 4, kBufferOffsetBytes = 16;

// Vertex-attribute format codes at vfAt+1 (OBS: the pitch and goal packs).
constexpr uint8_t kFmtF32x3 = 1;  // position
constexpr uint8_t kFmtHalf = 6;   // normal, tangent
constexpr uint8_t kFmtF32x2 = 2;  // two float32s: the pitch mesh's UV0
constexpr uint8_t kFmtHalf2 = 7;  // two half floats: every other pack's UV0

// Vertex-attribute types at vfAt.
constexpr uint8_t kAttrPosition = 0, kAttrNormal = 2, kAttrTangent = 0xE, kAttrUv0 = 8;

void Put16(std::vector<uint8_t>& b, size_t at, uint16_t v) {
  if (b.size() < at + sizeof v) b.resize(at + sizeof v, 0);
  std::memcpy(&b[at], &v, sizeof v);
}
void Put32(std::vector<uint8_t>& b, size_t at, uint32_t v) {
  if (b.size() < at + sizeof v) b.resize(at + sizeof v, 0);
  std::memcpy(&b[at], &v, sizeof v);
}
void Put64(std::vector<uint8_t>& b, size_t at, uint64_t v) {
  if (b.size() < at + sizeof v) b.resize(at + sizeof v, 0);
  std::memcpy(&b[at], &v, sizeof v);
}

uint16_t AsHalf(float f) {
  uint32_t bits;
  std::memcpy(&bits, &f, 4);
  const uint32_t sign = (bits >> 16) & 0x8000;
  int32_t exp = int32_t(bits >> 23) & 0xFF;
  const uint32_t mant = bits & 0x7FFFFF;
  if (exp == 255) return uint16_t(sign | 0x7BFF);
  if (exp == 0) return uint16_t(sign);
  exp += -127 + 15;  // float32 bias 127 -> half bias 15
  if (exp >= 0x1F) return uint16_t(sign | 0x7BFF);
  if (exp <= 0) return uint16_t(sign | mant >> (13 - exp + 1));
  return uint16_t(sign | (uint32_t(exp) << 10) | (mant >> 13));
}

// One mesh: a f32x3 position stream (slot 0), a half normal + half tangent and
// a `uvFormat` UV0 at attribute offset 16 (slot 1, stride 24), and a u16 index
// list (slot 2). All three slots live in one vertex buffer, positions first,
// so slot 1's start offset is non-zero exactly as the real files have it.
struct Spec {
  uint8_t uvFormat = kFmtF32x2;
  int verts = 3;
  float u = 0.25f, v = 0.75f;
};

std::vector<uint8_t> SyntheticFmdl(const Spec& spec) {
  constexpr size_t kStride = 24;
  const size_t slot0Bytes = 12 * size_t(spec.verts);
  const size_t slot1Bytes = kStride * size_t(spec.verts);
  constexpr uint32_t kIndexCount = 3;
  // The slots are packed into one vertex buffer: positions, then the
  // normal/tangent/UV stream, then the u16 index list.
  const uint32_t slot0Offset = 0;
  const uint32_t slot1Offset = uint32_t(slot0Bytes);

  // ---- section 0 blocks, in ascending type order as the files keep them ----
  struct Block { uint16_t type; uint16_t count; std::vector<uint8_t> data; };
  std::vector<Block> blocks;
  {
    std::vector<uint8_t> d(kMeshInfoBytes, 0);
    Put16(d, 10, uint16_t(spec.verts));
    Put32(d, 20, kIndexCount);
    blocks.push_back({3, 1, d});  // MESH_INFO
  }
  {
    std::vector<uint8_t> d(kMeshFormatInfoBytes, 0);
    d[0] = 3;             // format count: one interleaved stream per slot
    Put16(d, 4, 0);      // first format
    Put16(d, 6, 0);     // first vertex format
    blocks.push_back({9, 1, d});  // MESH_FORMAT_INFO
  }
  {
    std::vector<uint8_t> d;
    // Slot 0 holds the position alone; slot 1 holds the normal and the tangent;
    // slot 2 holds the UV. Each is an interleaved stream of its own stride.
    for (int s = 0; s < 3; ++s) {
      std::vector<uint8_t> e(kMeshFormatBytes, 0);
      e[0] = uint8_t(s);      // buffer slot
      e[1] = s == 1 ? 3 : (s == 2 ? 0 : 1);  // slot 1 holds all three attributes
      e[2] = s == 0 ? 12 : 24;
      Put32(e, 4, 0);
      d.insert(d.end(), e.begin(), e.end());
    }
    blocks.push_back({10, 3, d});  // MESH_FORMATS
  }
  {
    std::vector<uint8_t> d;
    const uint8_t type[] = {kAttrPosition, kAttrNormal, kAttrTangent, kAttrUv0};
    const uint8_t fmt[] = {kFmtF32x3, kFmtHalf, kFmtHalf, spec.uvFormat};
    const uint16_t off[] = {0, 0, 8, 16};  // the UV sits at 16, inside slot 1
    for (int a = 0; a < 4; ++a) {
      std::vector<uint8_t> e(kVertexFormatBytes, 0);
      e[0] = type[a];
      e[1] = fmt[a];
      std::memcpy(&e[2], &off[a], 2);
      d.insert(d.end(), e.begin(), e.end());
    }
    blocks.push_back({11, 4, d});  // VERTEX_FORMATS
  }
  {
    const uint32_t start[] = {slot0Offset, slot1Offset, slot1Offset + uint32_t(slot1Bytes)};
    const uint32_t len[] = {uint32_t(slot0Bytes), uint32_t(slot1Bytes), kIndexCount * 2};
    // Slot 1 holds the normal, the tangent and the UV, exactly as the pitch
    // pack does: three attributes in one 24-byte interleaved stream.
    std::vector<uint8_t> d;
    for (int s = 0; s < 3; ++s) {
      std::vector<uint8_t> e(kBufferOffsetBytes, 0);
      Put32(e, 4, len[s]);
      Put32(e, 8, start[s]);
      d.insert(d.end(), e.begin(), e.end());
    }
    blocks.push_back({14, 3, d});  // BUFFER_OFFSETS
  }

  // Header, section table (section-0 records, then the section-1 records),
  // then section-0 data (16-byte aligned), then the vertex buffer.
  std::vector<uint8_t> b(kSectionInfo, 0);
  std::memcpy(b.data(), "FMDL", 4);
  Put32(b, 4, kVersion2_03);
  Put64(b, 8, kSectionInfo);
  Put32(b, 32, uint32_t(blocks.size()));
  Put32(b, 36, 1);  // one section-1 block: the vertex buffer
  const size_t s0 = kSectionInfo + (blocks.size() + 1) * kS0RecordBytes;
  Put32(b, 40, uint32_t(s0));
  Put32(b, 48, uint32_t(s0));
  while (b.size() < s0) b.push_back(0);
  for (size_t i = 0; i < blocks.size(); ++i) {
    const size_t tableAt = kSectionInfo + i * kS0RecordBytes;
    Put16(b, tableAt, blocks[i].type);
    Put16(b, tableAt + 2, blocks[i].count);
    std::vector<uint8_t> data = blocks[i].data;
    while (data.size() % 16 != 0) data.push_back(0);
    Put32(b, tableAt + 4, uint32_t(b.size() - s0));
    b.insert(b.end(), data.begin(), data.end());
  }
  // The section-1 record, then the vertex buffer it points at.
  const size_t s1Table = kSectionInfo + blocks.size() * kS0RecordBytes;
  while (b.size() < s1Table) b.push_back(0);
  const size_t buffer = (b.size() + kS1RecordBytes + 15) / 16 * 16;
  Put32(b, s1Table, 2);                 // block type BUFFER
  Put32(b, s1Table + 4, uint32_t(buffer - s0));  // offset relative to section 1
  Put32(b, s1Table + 8, 0);             // byte length, filled after the data
  while (b.size() < buffer) b.push_back(0);
  for (int v = 0; v < spec.verts; ++v) {
    // Slot 0: position, f32x3, 12 bytes.
    const float x = 1.0f * float(v), y = 2.0f * float(v), z = 3.0f * float(v);
    uint32_t px, py, pz;
    std::memcpy(&px, &x, 4); std::memcpy(&py, &y, 4); std::memcpy(&pz, &z, 4);
    Put32(b, buffer + slot0Offset + size_t(v) * 12 + 0, px);
    Put32(b, buffer + slot0Offset + size_t(v) * 12 + 4, py);
    Put32(b, buffer + slot0Offset + size_t(v) * 12 + 8, pz);
    // Slot 1: a half normal at 0, a half tangent at 8, then the UV at 16. The
    // slot starts after the position stream, so a reader that ignores the slot
    // offset reads positions here.
    const size_t at = buffer + slot1Offset + size_t(v) * kStride;
    Put16(b, at + 0, AsHalf(0.0f));
    Put16(b, at + 2, AsHalf(1.0f));
    Put16(b, at + 4, AsHalf(0.0f));
    Put16(b, at + 8, AsHalf(1.0f));
    Put16(b, at + 10, AsHalf(0.0f));
    Put16(b, at + 12, AsHalf(0.0f));
    if (spec.uvFormat == kFmtHalf2) {
      Put16(b, at + 16, AsHalf(spec.u));
      Put16(b, at + 18, AsHalf(spec.v));
    } else {
      Put32(b, at + 16, *reinterpret_cast<const uint32_t*>(&spec.u));
      Put32(b, at + 20, *reinterpret_cast<const uint32_t*>(&spec.v));
    }
  }
  const size_t indexAt = buffer + slot1Offset + kStride * size_t(spec.verts);
  Put16(b, indexAt + 0, 0);
  Put16(b, indexAt + 2, 1);
  Put16(b, indexAt + 4, 2);
  Put32(b, s1Table + 8, uint32_t(b.size() - buffer));

  return b;
}

}  // namespace

// The pitch stores UV0 as two float32s at attribute offset 16 in a separate
// buffer slot; decoding it as half floats reads the position stream instead.
TEST(Fmdl, Float32Uv0DecodesFromItsBufferSlot) {
  Spec spec;
  spec.u = 0.25f;
  spec.v = 0.75f;
  std::string err;
  auto m = pes::ParseFmdl(SyntheticFmdl(spec), &err);
  ASSERT_TRUE(m) << err;
  ASSERT_EQ(m->meshes.size(), 1u);
  const auto& mesh = m->meshes[0];
  ASSERT_EQ(mesh.uv0.size(), mesh.pos.size());
  for (size_t i = 0; i < mesh.uv0.size(); ++i) {
    EXPECT_NEAR(mesh.uv0[i].x, 0.25f, 1e-6f) << "vertex " << i;
    EXPECT_NEAR(mesh.uv0[i].y, 0.75f, 1e-6f) << "vertex " << i;
  }
  // The position stream is unaffected by the UV slot's offset.
  for (size_t i = 0; i < mesh.pos.size(); ++i)
    EXPECT_FLOAT_EQ(mesh.pos[i].x, float(i));
}

// A mesh whose UV0 is two half floats in four bytes — every pack but the pitch:
// face.fpk's face_high.fmdl, the goal pack, and the st002 scene meshes. Reading
// those four bytes as a float32 gives 0.0002 where the UV is 0.871/0.666, which
// blanks a kit's UV atlas and flattens a stadium's textures.
TEST(Fmdl, Half2Uv0DecodesFromFourBytes) {
  Spec spec;
  spec.uvFormat = kFmtHalf2;
  spec.u = 0.87109375f;   // 0x3af8 as a half
  spec.v = 0.666015625f;  // 0x3954 as a half
  std::string err;
  auto m = pes::ParseFmdl(SyntheticFmdl(spec), &err);
  ASSERT_TRUE(m) << err;
  ASSERT_EQ(m->meshes.size(), 1u);
  const auto& mesh = m->meshes[0];
  ASSERT_EQ(mesh.uv0.size(), mesh.pos.size());
  for (size_t i = 0; i < mesh.uv0.size(); ++i) {
    EXPECT_NEAR(mesh.uv0[i].x, 0.871094f, 1e-6f) << "vertex " << i;
    EXPECT_NEAR(mesh.uv0[i].y, 0.666016f, 1e-6f) << "vertex " << i;
  }
}
