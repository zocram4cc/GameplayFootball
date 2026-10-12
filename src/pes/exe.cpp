#include "pes/exe.hpp"

#include <cstring>
#include <fstream>
#include <memory>

#include "pes/mtar.hpp"

namespace pes {
namespace {

constexpr size_t PE_HEADER_POINTER = 0x3C;
constexpr uint32_t PE_SIGNATURE = 0x00004550;  // "PE\0\0"
constexpr size_t COFF_SECTION_COUNT = 6, COFF_OPTIONAL_SIZE = 20, COFF_BYTES = 24;
constexpr size_t OPTIONAL_IMAGE_BASE = 24;  // PE32+
constexpr size_t SECTION_BYTES = 40, SECTION_VSIZE = 8, SECTION_RVA = 12, SECTION_RAW_SIZE = 16, SECTION_RAW = 20;
constexpr size_t MAX_CSTRING = 256;

// docs/re/pes-re-animselect.md 1: the motion name table, one char* per motionNo.
constexpr uint64_t MOTION_NAME_TABLE_VA = 0x14351e590;
constexpr const char* BODY_CLIP_DIR = "common/anime/FoxAnim/Body/";

template <typename T>
T Load(const uint8_t* p) { T v; std::memcpy(&v, p, sizeof v); return v; }

}  // namespace

std::optional<PeImage> PeImage::FromReader(Reader read, std::string* error) {
  uint8_t head[0x400];
  if (!read(0, sizeof head, head) || head[0] != 'M' || head[1] != 'Z') {
    *error = "not a PE image (no MZ header)";
    return std::nullopt;
  }
  const uint32_t pe = Load<uint32_t>(head + PE_HEADER_POINTER);
  uint8_t coff[COFF_BYTES + OPTIONAL_IMAGE_BASE + 8];
  if (!read(pe, sizeof coff, coff) || Load<uint32_t>(coff) != PE_SIGNATURE) {
    *error = "not a PE image (no PE signature)";
    return std::nullopt;
  }
  const uint16_t count = Load<uint16_t>(coff + COFF_SECTION_COUNT), optional = Load<uint16_t>(coff + COFF_OPTIONAL_SIZE);
  const uint64_t base = Load<uint64_t>(coff + COFF_BYTES + OPTIONAL_IMAGE_BASE);
  std::vector<uint8_t> table(size_t(count) * SECTION_BYTES);
  if (!read(pe + COFF_BYTES + optional, table.size(), table.data())) {
    *error = "PE section table truncated";
    return std::nullopt;
  }
  PeImage img;
  img.read_ = std::move(read);
  for (uint16_t i = 0; i < count; ++i) {
    const uint8_t* s = &table[size_t(i) * SECTION_BYTES];
    img.sections_.push_back({base + Load<uint32_t>(s + SECTION_RVA), Load<uint32_t>(s + SECTION_RAW_SIZE),
                             Load<uint32_t>(s + SECTION_RAW)});
  }
  return img;
}

std::optional<PeImage> PeImage::Parse(std::vector<uint8_t> file, std::string* error) {
  auto data = std::make_shared<std::vector<uint8_t>>(std::move(file));
  return FromReader(
      [data](uint64_t off, size_t n, uint8_t* out) {
        if (off > data->size() || n > data->size() - off) return false;
        std::memcpy(out, data->data() + off, n);
        return true;
      },
      error);
}

std::optional<PeImage> PeImage::Open(const std::string& path, std::string* error) {
  auto f = std::make_shared<std::ifstream>(path, std::ios::binary);
  if (!*f) {
    *error = "cannot open " + path;
    return std::nullopt;
  }
  return FromReader(
      [f](uint64_t off, size_t n, uint8_t* out) {
        f->clear();
        f->seekg(std::streamoff(off));
        f->read(reinterpret_cast<char*>(out), std::streamsize(n));
        return f->gcount() == std::streamsize(n);
      },
      error);
}

bool PeImage::ReadVa(uint64_t va, size_t n, uint8_t* out) const {
  for (const auto& s : sections_)
    if (va >= s.va && va - s.va + n <= s.size) return read_(s.raw + (va - s.va), n, out);
  return false;
}

std::optional<uint64_t> PeImage::ReadU64(uint64_t va) const {
  uint8_t b[8];
  if (!ReadVa(va, sizeof b, b)) return std::nullopt;
  return Load<uint64_t>(b);
}

std::string PeImage::ReadCString(uint64_t va) const {
  std::string out;
  for (char c; out.size() < MAX_CSTRING && ReadVa(va + out.size(), 1, reinterpret_cast<uint8_t*>(&c)) && c; ) out += c;
  return out;
}

std::optional<std::vector<std::string>> MotionNames(const PeImage& exe, const PesVfs& vfs, std::string* error) {
  size_t clips = 0;
  for (const auto& path : vfs.List(BODY_CLIP_DIR))
    if (path.ends_with(".mtar")) {
      auto blob = vfs.Read(path, error);
      auto mtar = blob ? ParseMtar(*blob, error) : std::nullopt;
      if (!mtar) return std::nullopt;
      clips += mtar->entries.size();
    }
  std::vector<std::string> names;
  names.reserve(clips);
  for (size_t i = 0; i < clips; ++i) {
    auto p = exe.ReadU64(MOTION_NAME_TABLE_VA + 8 * i);
    std::string name = p ? exe.ReadCString(*p) : "";
    if (name.empty()) {
      *error = "motion name " + std::to_string(i) + " unreadable: not the PES 2021 executable this table was found in";
      return std::nullopt;
    }
    names.push_back(std::move(name));
  }
  return names;
}

}  // namespace pes
