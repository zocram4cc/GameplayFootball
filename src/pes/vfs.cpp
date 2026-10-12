#include "pes/vfs.hpp"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <tuple>

namespace fs = std::filesystem;

namespace pes {
namespace {

// DpFileList.bin: u32 0, u32 count, 8 bytes pad, then 48-byte records whose
// first bytes are a NUL-terminated cpk name.
constexpr size_t DPFILELIST_COUNT_OFFSET = 4;
constexpr size_t DPFILELIST_RECORDS_OFFSET = 16;
constexpr size_t DPFILELIST_RECORD_BYTES = 48;
// Data/ archives; skips "*.bak", "* - Copia.cpk" and other leftovers.
const std::regex DATA_CPK_NAME(R"(^dt(\d\d)_[A-Za-z0-9_]+\.cpk$)");
const std::regex WINDOWS_ABSOLUTE(R"(^[A-Za-z]:)");

std::optional<std::string> Slurp(const fs::path& p) {
  std::ifstream in(p, std::ios::binary);
  if (!in) return std::nullopt;
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

std::string Trim(std::string s) {
  const char* ws = " \t\r\n";
  s.erase(0, s.find_first_not_of(ws));
  s.erase(s.find_last_not_of(ws) + 1);
  return s;
}

}  // namespace

std::string PesVfs::Fold(const std::string& path) {
  std::string out;
  out.reserve(path.size());
  for (char c : path) out.push_back(c == '\\' ? '/' : char(std::tolower(static_cast<unsigned char>(c))));
  out.erase(0, out.find_first_not_of('/'));
  return out;
}

void PesVfs::Add(const std::string& path, Source source) { index_[Fold(path)] = source; }

void PesVfs::MountCpk(const std::string& file, const std::string& label) {
  CpkArchive cpk;
  std::string err;
  if (!cpk.Open(file, &err)) {
    report_ += label + "  FAILED: " + err + "\n";
    return;
  }
  const auto id = int32_t(archives_.size());
  for (uint32_t i = 0; i < cpk.Entries().size(); ++i) Add(cpk.Entries()[i].path, {id, i});
  report_ += label + "  " + std::to_string(cpk.Entries().size()) + " entries\n";
  archives_.push_back(std::move(cpk));
}

void PesVfs::MountLooseRoot(const std::string& dir) {
  const auto rootId = uint32_t(looseRoots_.size());
  looseRoots_.push_back(dir);
  size_t count = 0;
  std::error_code ec;
  for (auto it = fs::recursive_directory_iterator(dir, ec); !ec && it != fs::recursive_directory_iterator();
       it.increment(ec)) {
    if (!it->is_regular_file()) continue;
    const std::string rel = fs::relative(it->path(), dir).generic_string();
    Add(rel, {LOOSE, uint32_t(loose_.size())});
    loose_.push_back(rel);
    looseRootOf_.push_back(rootId);
    ++count;
  }
  report_ += "sider " + fs::relative(dir, root_).generic_string() + "  " + std::to_string(count) + " files\n";
}

bool PesVfs::Mount(const std::string& root, std::string* error) {
  root_ = root;
  const fs::path rootPath(root);
  if (!fs::is_directory(rootPath / "Data")) {
    *error = "no Data/ folder under the PES21 root";
    return false;
  }

  // 1. Data/: by number, then the _4K_ variant after its base so 4K wins, then name.
  std::vector<std::tuple<int, bool, std::string>> data;
  for (const auto& e : fs::directory_iterator(rootPath / "Data")) {
    const std::string name = e.path().filename().string();
    std::smatch m;
    if (e.is_regular_file() && std::regex_match(name, m, DATA_CPK_NAME))
      data.emplace_back(std::stoi(m[1]), name.find("_4K_") != std::string::npos, name);
  }
  std::sort(data.begin(), data.end());
  for (const auto& [num, is4k, name] : data) MountCpk((rootPath / "Data" / name).string(), "data " + name);

  // 2. download/ in DpFileList.bin order.
  const fs::path dl = rootPath / "download";
  if (auto list = Slurp(dl / "DpFileList.bin");
      list && list->size() >= DPFILELIST_RECORDS_OFFSET) {
    uint32_t count;
    std::memcpy(&count, list->data() + DPFILELIST_COUNT_OFFSET, 4);
    for (uint32_t i = 0; i < count; ++i) {
      const size_t at = DPFILELIST_RECORDS_OFFSET + size_t(i) * DPFILELIST_RECORD_BYTES;
      if (at + DPFILELIST_RECORD_BYTES > list->size()) {
        report_ += "download DpFileList.bin truncated at record " + std::to_string(i) + "\n";
        break;
      }
      const std::string name(list->c_str() + at, strnlen(list->c_str() + at, DPFILELIST_RECORD_BYTES));
      if (fs::is_regular_file(dl / name)) MountCpk((dl / name).string(), "download " + name);
      else report_ += "download " + name + "  listed but absent\n";
    }
  } else {
    report_ += "download  no DpFileList.bin, skipped\n";
  }

  // 3. sider live-cpk roots; the first listed root wins, so mount in reverse.
  if (auto ini = Slurp(rootPath / "sider.ini")) {
    std::istringstream in(*ini);
    std::string line;
    bool enabled = false;
    std::vector<std::string> roots;
    while (std::getline(in, line)) {
      line = Trim(line.substr(0, line.find(';')));
      const auto eq = line.find('=');
      if (eq == std::string::npos) continue;
      const std::string key = Trim(line.substr(0, eq));
      std::string value = Trim(line.substr(eq + 1));
      if (key == "livecpk.enabled") enabled = value == "1";
      if (key != "cpk.root") continue;
      value.erase(std::remove(value.begin(), value.end(), '"'), value.end());
      roots.push_back(value);
    }
    if (enabled) {
      for (auto it = roots.rbegin(); it != roots.rend(); ++it) {
        std::string r = *it;
        if (std::regex_search(r, WINDOWS_ABSOLUTE)) {
          report_ += "sider root is an absolute Windows path, skipped\n";
          continue;
        }
        std::replace(r.begin(), r.end(), '\\', '/');
        const fs::path dir = (rootPath / r).lexically_normal();
        if (fs::is_directory(dir)) MountLooseRoot(dir.string());
        else report_ += "sider root " + r + "  absent\n";
      }
    }
  }
  report_ += "total " + std::to_string(index_.size()) + " unique paths\n";
  return true;
}

bool PesVfs::Exists(const std::string& path) const { return index_.count(Fold(path)) != 0; }

std::optional<uint64_t> PesVfs::Size(const std::string& path) const {
  auto it = index_.find(Fold(path));
  if (it == index_.end()) return std::nullopt;
  const Source& s = it->second;
  if (s.archive != LOOSE) return archives_[s.archive].Entries()[s.entry].extractSize;
  std::error_code ec;
  const auto size = fs::file_size(fs::path(looseRoots_[looseRootOf_[s.entry]]) / loose_[s.entry], ec);
  return ec ? std::nullopt : std::optional<uint64_t>(size);
}

const std::string& PesVfs::OriginalPath(const Source& s) const {
  return s.archive == LOOSE ? loose_[s.entry] : archives_[s.archive].Entries()[s.entry].path;
}

std::optional<std::vector<uint8_t>> PesVfs::Read(const std::string& path, std::string* error) const {
  auto it = index_.find(Fold(path));
  if (it == index_.end()) {
    *error = "not in PES VFS: " + path;
    return std::nullopt;
  }
  const Source& s = it->second;
  if (s.archive != LOOSE) return archives_[s.archive].Read(archives_[s.archive].Entries()[s.entry], error);
  auto bytes = Slurp(fs::path(looseRoots_[looseRootOf_[s.entry]]) / loose_[s.entry]);
  if (!bytes) {
    *error = "cannot read live file " + path;
    return std::nullopt;
  }
  return std::vector<uint8_t>(bytes->begin(), bytes->end());
}

std::vector<std::string> PesVfs::List(const std::string& prefix) const {
  const std::string p = Fold(prefix);
  std::vector<std::string> out;
  for (const auto& [key, src] : index_)
    if (key.compare(0, p.size(), p) == 0) out.push_back(OriginalPath(src));
  std::sort(out.begin(), out.end());
  return out;
}

}  // namespace pes
