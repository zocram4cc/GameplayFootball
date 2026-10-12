#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "pes/types.hpp"

namespace pes {

// Fox engine binary FoxDataFile ("fox2"), as stored inside .fpkd packs
// (docs: docs/re/pes-re-fox2-format.md). The blob passed to ParseFox2 is the
// inner .fox2 payload, not the fpkd wrapper — see FpkArchive for the wrapper.
struct Fox2Value {
  // Variant, discriminated by the property's type id:
  //   bool -> b; int64 -> i; uint64 (EntityPtr/EntityHandle addresses,
  //     unresolved String/Path/FilePtr hashes) -> u; double -> d;
  //   std::string (resolved String/Path/FilePtr) -> s;
  //   Vec3 (WideVec3, xyz of 4 floats) -> v3; Quat -> q; Vec4/Color -> v4.
  //   Raw (undecoded type ids such as EntityLink): the element bytes.
  bool b = false;
  int64_t i = 0;
  uint64_t u = 0;
  double d = 0;
  std::string s;
  Vec3 v3;
  Quat q;
  Vec4 v4;
  std::vector<uint8_t> raw;
};

struct Fox2Property {
  std::string name;
  uint8_t type = 0;
  uint8_t container = 0;
  std::vector<Fox2Value> values;
  std::vector<std::string> keys;  // StringMap keys, file order; else empty
};

struct Fox2Entity {
  uint64_t address = 0;
  std::string className;
  std::vector<Fox2Property> props;
  const Fox2Property* Find(std::string_view name) const;
};

struct Fox2File {
  std::vector<Fox2Entity> entities;
  const Fox2Entity* ByAddress(uint64_t address) const;
};

std::optional<Fox2File> ParseFox2(std::span<const uint8_t> blob, std::string* error);

}  // namespace pes
