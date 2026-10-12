#pragma once

#include <cstdint>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "pes/vfs.hpp"

namespace pes {

// Body-clip names, recovered at runtime from the user's install (they are not
// stored in the archives, which hold only StrCode32 hashes). Never committed.
class AnimNameIndex {
 public:
  // Sources in trust order: "file_name" values of anim_infos.json, then
  // identifier runs in PES2021.exe. Only names hashing to `targets` are kept.
  static AnimNameIndex FromSources(std::string_view animInfosJson, std::span<const uint8_t> exe,
                                   const std::set<uint32_t>& targets);
  static std::optional<AnimNameIndex> Build(const PesVfs& vfs, const std::string& installRoot, std::string* error);

  std::string Name(uint32_t hash) const;  // "0x%08x" when unresolved
  size_t Resolved() const { return names_.size(); }
  const std::unordered_map<uint32_t, std::string>& All() const { return names_; }

 private:
  std::unordered_map<uint32_t, std::string> names_;
};

}  // namespace pes
