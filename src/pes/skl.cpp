// Port of GameplayFootball tools/pes21_import/pes_skl.py.
#include "pes/skl.hpp"

#include <cstring>

namespace pes {
namespace {

constexpr uint32_t SKL_VERSION = 12;
constexpr uint32_t SKL_RECORD_BYTES = 56;
constexpr size_t SKL_HEADER_BYTES = 12;
constexpr size_t SKL_MATRIX_FIELD = 8;

template <typename T>
T Load(const uint8_t* p) { T v; std::memcpy(&v, p, sizeof v); return v; }

}  // namespace

std::optional<std::vector<SklBone>> ParseSkl(std::span<const uint8_t> blob, std::string* error) {
  if (blob.size() < SKL_HEADER_BYTES || Load<uint32_t>(blob.data()) != SKL_VERSION ||
      Load<uint32_t>(blob.data() + 8) != SKL_RECORD_BYTES) {
    *error = "not a version-12 .skl with 56-byte records";
    return std::nullopt;
  }
  const uint32_t count = Load<uint32_t>(blob.data() + 4);
  if (SKL_HEADER_BYTES + size_t(count) * SKL_RECORD_BYTES > blob.size()) {
    *error = ".skl records exceed file";
    return std::nullopt;
  }
  std::vector<SklBone> bones(count);
  for (uint32_t i = 0; i < count; ++i) {
    const uint8_t* r = blob.data() + SKL_HEADER_BYTES + size_t(i) * SKL_RECORD_BYTES;
    const uint32_t nameAt = Load<uint32_t>(r);
    if (nameAt >= blob.size()) {
      *error = ".skl name offset out of bounds";
      return std::nullopt;
    }
    const char* name = reinterpret_cast<const char*>(blob.data() + nameAt);
    bones[i].name.assign(name, strnlen(name, blob.size() - nameAt));
    bones[i].parent = Load<int32_t>(r + 4);
    std::memcpy(bones[i].matrix.data(), r + SKL_MATRIX_FIELD, sizeof(float) * bones[i].matrix.size());
  }
  return bones;
}

}  // namespace pes
