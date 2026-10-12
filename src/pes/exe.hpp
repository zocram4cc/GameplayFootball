#pragma once

// The user's PES2021.exe, read as a PE image: virtual address -> file bytes
// through the section table. Tables the game keeps in code are read from it at
// run time (docs/re: addresses), never transcribed.

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "pes/vfs.hpp"

namespace pes {

class PeImage {
 public:
  static std::optional<PeImage> Parse(std::vector<uint8_t> file, std::string* error);
  static std::optional<PeImage> Open(const std::string& path, std::string* error);
  std::optional<uint64_t> ReadU64(uint64_t va) const;
  std::string ReadCString(uint64_t va) const;  // empty when unmapped

 private:
  struct Section { uint64_t va, size, raw; };
  // Reads n bytes at a file offset; a buffer in tests, the file on disk otherwise
  // (the exe is ~460 MB; a table needs a few KB of it).
  using Reader = std::function<bool(uint64_t offset, size_t n, uint8_t* out)>;
  static std::optional<PeImage> FromReader(Reader read, std::string* error);
  bool ReadVa(uint64_t va, size_t n, uint8_t* out) const;
  Reader read_;
  std::vector<Section> sections_;
};

// The motion name table: entry motionNo names that body clip. Its length is the
// number of body clips in the install's mtars.
std::optional<std::vector<std::string>> MotionNames(const PeImage& exe, const PesVfs& vfs, std::string* error);

}  // namespace pes
