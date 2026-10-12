#include "pes/ask.hpp"

#include <cstring>

namespace pes {
namespace {

constexpr uint32_t ASK_VERSION = 1;
constexpr size_t ASK_HEADER_BYTES = 8;
constexpr size_t ASK_RECORD_BYTES = 44;
constexpr size_t ASK_NAME_FIELD = 4;   // nameOffset is relative to its own position
constexpr size_t ASK_PARENT_FIELD = 8;
constexpr size_t ASK_TRANSLATION_FIELD = 12;
constexpr size_t ASK_ROTATION_FIELD = 28;

template <typename T>
T Load(const uint8_t* p) { T v; std::memcpy(&v, p, sizeof v); return v; }

}  // namespace

std::optional<std::vector<AskBone>> ParseAsk(std::span<const uint8_t> blob, std::string* error) {
  if (blob.size() < ASK_HEADER_BYTES || Load<uint32_t>(blob.data()) != ASK_VERSION) {
    *error = "not a version-1 .ask";
    return std::nullopt;
  }
  const uint32_t count = Load<uint32_t>(blob.data() + 4);
  if (ASK_HEADER_BYTES + size_t(count) * ASK_RECORD_BYTES > blob.size()) {
    *error = ".ask records exceed file";
    return std::nullopt;
  }
  std::vector<AskBone> bones(count);
  for (uint32_t i = 0; i < count; ++i) {
    const size_t at = ASK_HEADER_BYTES + size_t(i) * ASK_RECORD_BYTES;
    const uint8_t* r = blob.data() + at;
    const int64_t nameAt = int64_t(at + ASK_NAME_FIELD) + Load<int32_t>(r + ASK_NAME_FIELD);
    if (nameAt < 0 || size_t(nameAt) >= blob.size()) {
      *error = ".ask name offset out of bounds";
      return std::nullopt;
    }
    const char* name = reinterpret_cast<const char*>(blob.data() + nameAt);
    AskBone& b = bones[i];
    b.name.assign(name, strnlen(name, blob.size() - size_t(nameAt)));
    b.parent = Load<int32_t>(r + ASK_PARENT_FIELD);
    auto f = [&](size_t field, int k) { return Load<float>(r + field + 4 * k); };
    b.local = {f(ASK_TRANSLATION_FIELD, 0), f(ASK_TRANSLATION_FIELD, 1), f(ASK_TRANSLATION_FIELD, 2)};
    b.rotation = {f(ASK_ROTATION_FIELD, 0), f(ASK_ROTATION_FIELD, 1), f(ASK_ROTATION_FIELD, 2), f(ASK_ROTATION_FIELD, 3)};
    if (b.parent >= int(count)) {
      *error = ".ask parent out of range";
      return std::nullopt;
    }
  }
  return bones;
}

}  // namespace pes
