// Runtime port of GameplayFootball tools/pes21_import/mine_anim_names.py
// (sources json and exe; the synthetic "mutate" source is not used).
#include "pes/anim_names.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>

#include "pes/mtar.hpp"
#include "pes/strcode.hpp"

namespace pes {
namespace {

constexpr const char* ANIM_INFOS = "common/anime/Mbinfo/json/anim_infos.json";
constexpr const char* GAME_EXE = "PES2021.exe";
constexpr int BODY_ARCHIVE_COUNT = 10;
constexpr const char* BODY_ARCHIVE = "common/anime/FoxAnim/Body/body_anime_file";
constexpr std::string_view FILE_NAME_KEY = "\"file_name\"";
constexpr size_t IDENT_MIN = 3, IDENT_MAX = 90;

bool IsIdent(uint8_t c) { return std::isalnum(c) || c == '_'; }

std::set<std::string> JsonNames(std::string_view json) {
  std::set<std::string> out;
  for (size_t at = json.find(FILE_NAME_KEY); at != std::string_view::npos; at = json.find(FILE_NAME_KEY, at + 1)) {
    size_t p = at + FILE_NAME_KEY.size();
    while (p < json.size() && std::isspace(static_cast<unsigned char>(json[p]))) ++p;
    if (p >= json.size() || json[p] != ':') continue;
    ++p;
    while (p < json.size() && std::isspace(static_cast<unsigned char>(json[p]))) ++p;
    if (p >= json.size() || json[p] != '"') continue;
    const size_t end = json.find('"', p + 1);
    if (end == std::string_view::npos) break;
    if (end > p + 1) out.emplace(json.substr(p + 1, end - p - 1));
  }
  return out;
}

// Calls f(candidate) for each identifier run, as GF's [A-Za-z0-9_]{3,90}: a
// longer run splits into consecutive 90-character candidates.
template <typename F>
void ForEachIdentifierRun(std::span<const uint8_t> bytes, F&& f) {
  size_t i = 0;
  while (i < bytes.size()) {
    if (!IsIdent(bytes[i])) { ++i; continue; }
    size_t end = i;
    while (end < bytes.size() && IsIdent(bytes[end])) ++end;
    for (size_t s = i; s < end && end - s >= IDENT_MIN; s += IDENT_MAX)
      f(std::string_view(reinterpret_cast<const char*>(bytes.data() + s), std::min(IDENT_MAX, end - s)));
    i = end;
  }
}

}  // namespace

AnimNameIndex AnimNameIndex::FromSources(std::string_view json, std::span<const uint8_t> exe,
                                         const std::set<uint32_t>& targets) {
  // Trust order json > exe; within a source the alphabetically first name wins a hash.
  AnimNameIndex idx;
  std::unordered_map<uint32_t, std::string> found;
  auto offer = [&](std::string_view name) {
    const uint32_t h = StrCode32(name);
    if (!targets.count(h) || idx.names_.count(h)) return;
    auto [it, fresh] = found.emplace(h, name);
    if (!fresh && name < it->second) it->second = name;
  };
  for (const auto& name : JsonNames(json)) offer(name);
  idx.names_.merge(found);
  found.clear();
  ForEachIdentifierRun(exe, offer);
  idx.names_.merge(found);
  return idx;
}

std::optional<AnimNameIndex> AnimNameIndex::Build(const PesVfs& vfs, const std::string& installRoot,
                                                  std::string* error) {
  std::set<uint32_t> targets;
  for (int i = 0; i < BODY_ARCHIVE_COUNT; ++i) {
    auto bytes = vfs.Read(BODY_ARCHIVE + std::to_string(i) + ".mtar", error);
    if (!bytes) return std::nullopt;
    auto m = ParseMtar(*bytes, error);
    if (!m) return std::nullopt;
    for (const auto& e : m->entries) targets.insert(e.nameHash);
  }
  std::string json;
  if (auto j = vfs.Read(ANIM_INFOS, error)) json.assign(j->begin(), j->end());
  std::vector<uint8_t> exe;
  if (std::ifstream in{std::filesystem::path(installRoot) / GAME_EXE, std::ios::binary | std::ios::ate}) {
    exe.resize(size_t(in.tellg()));
    in.seekg(0);
    in.read(reinterpret_cast<char*>(exe.data()), std::streamsize(exe.size()));
  }
  return FromSources(json, exe, targets);
}

std::string AnimNameIndex::Name(uint32_t hash) const {
  if (auto it = names_.find(hash); it != names_.end()) return it->second;
  char buf[16];
  std::snprintf(buf, sizeof buf, "0x%08x", hash);
  return buf;
}

}  // namespace pes
