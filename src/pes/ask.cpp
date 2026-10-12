#include "pes/ask.hpp"

#include <cstring>

namespace pes {
namespace {

// Record layouts by version. nameOffset is relative to its own position.
// Version 2 (Hand/hand_anim_skel.ask) stores positions only: its frames are
// world-aligned, so the rotation is identity.
struct AskLayout {
  uint32_t version;
  size_t header, record, name, parent, translation;
  int rotation;  // field offset, -1 = none
};
constexpr AskLayout ASK_LAYOUTS[] = {
    {1, 8, 44, 4, 8, 12, 28},
    {2, 0x14, 28, 0, 4, 8, -1},
};

template <typename T>
T Load(const uint8_t* p) { T v; std::memcpy(&v, p, sizeof v); return v; }

}  // namespace

std::optional<std::vector<AskBone>> ParseAsk(std::span<const uint8_t> blob, std::string* error) {
  const AskLayout* L = nullptr;
  for (const auto& layout : ASK_LAYOUTS)
    if (blob.size() >= layout.header && Load<uint32_t>(blob.data()) == layout.version) L = &layout;
  if (!L) {
    *error = "not a version-1 or version-2 .ask";
    return std::nullopt;
  }
  const uint32_t count = Load<uint32_t>(blob.data() + 4);
  if (L->header + size_t(count) * L->record > blob.size()) {
    *error = ".ask records exceed file";
    return std::nullopt;
  }
  std::vector<AskBone> bones(count);
  for (uint32_t i = 0; i < count; ++i) {
    const size_t at = L->header + size_t(i) * L->record;
    const uint8_t* r = blob.data() + at;
    const int64_t nameAt = int64_t(at + L->name) + Load<int32_t>(r + L->name);
    if (nameAt < 0 || size_t(nameAt) >= blob.size()) {
      *error = ".ask name offset out of bounds";
      return std::nullopt;
    }
    const char* name = reinterpret_cast<const char*>(blob.data() + nameAt);
    AskBone& b = bones[i];
    b.name.assign(name, strnlen(name, blob.size() - size_t(nameAt)));
    b.parent = Load<int32_t>(r + L->parent);
    auto f = [&](size_t field, int k) { return Load<float>(r + field + 4 * k); };
    b.local = {f(L->translation, 0), f(L->translation, 1), f(L->translation, 2)};
    b.rotation = L->rotation < 0 ? Quat{0, 0, 0, 1}
                                 : Quat{f(L->rotation, 0), f(L->rotation, 1), f(L->rotation, 2), f(L->rotation, 3)};
    if (b.parent >= int(count)) {
      *error = ".ask parent out of range";
      return std::nullopt;
    }
  }
  return bones;
}

}  // namespace pes
