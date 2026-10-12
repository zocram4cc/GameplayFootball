#pragma once

#include <cstdint>
#include <cstdio>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace pes {

struct CpkEntry {
  std::string path;  // DirName + '/' + FileName, no leading slash
  uint64_t offset = 0, size = 0, extractSize = 0;
};

// CRI CPK archive. Only the TOC is read at Open; entries are read in place.
class CpkArchive {
 public:
  // `at`/`length`: a cpk stored inside another file (Data/dt30_g4.cpk holds
  // whole stadium cpks); read in place, never extracted. 0/0 = the whole file.
  bool Open(const std::string& path, std::string* error, uint64_t at = 0, uint64_t length = 0);
  const std::vector<CpkEntry>& Entries() const { return entries_; }
  std::optional<std::vector<uint8_t>> Read(const CpkEntry& entry, std::string* error) const;
  const std::string& Path() const { return path_; }

 private:
  struct FileCloser { void operator()(std::FILE* f) const { std::fclose(f); } };
  std::string path_;
  std::unique_ptr<std::FILE, FileCloser> file_;
  std::vector<CpkEntry> entries_;
};

// CRILAYLA backwards-LZ decompression; returns input unchanged if not CRILAYLA.
std::optional<std::vector<uint8_t>> CrilaylaDecompress(const std::vector<uint8_t>& data, std::string* error);

}  // namespace pes
