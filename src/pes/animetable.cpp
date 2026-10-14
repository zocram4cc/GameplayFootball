#include "pes/animetable.hpp"

#include <array>
#include <cmath>
#include <cstring>

namespace pes {
namespace {

constexpr const char* ANIMETABLE_PATH = "common/anime/AnimeTable/bin/Animetable.bin";
constexpr uint64_t ROW_LABELS_VA = 0x1434997f0;

// Instruction immediates, opcode-checked (parser FUN_1406aa440, lookup
// FUN_1406abb30, column picker FUN_1406de990).
struct Imm {
  uint64_t va;
  std::array<uint8_t, 4> opcode;
  size_t opcodeLen, offset, width;
};
constexpr Imm ROWS = {0x1406aa4df, {0x41, 0xbb}, 2, 2, 4};          // mov r11d, 0xbe5
constexpr Imm MOTION_MASK = {0x1406aa4e5, {0xbf}, 1, 1, 4};         // mov edi, 0x1fff
constexpr Imm MIRROR_SHIFT = {0x1406aa50a, {0xc1, 0xe9}, 2, 2, 1};  // shr ecx, 0xd
constexpr Imm COLS = {0x1406aa53a, {0x48, 0x83, 0xf8}, 3, 3, 1};    // cmp rax, 0x12
constexpr Imm PATCH_ROW = {0x1406abb3a, {0x41, 0x81, 0xf9}, 3, 3, 4};  // cmp r9d, 0x7c5
constexpr Imm PATCH_COL = {0x1406abb43, {0x41, 0x83, 0x38}, 3, 3, 1};  // cmp [r8], 2
constexpr Imm PATCH_MOTION = {0x1406abb49, {0xb8}, 1, 1, 4};           // mov eax, 0x9b4
constexpr Imm DEFAULT_COL = {0x1406de9b1, {0xbe}, 1, 1, 4};            // mov esi, 9
constexpr Imm SPLIT_COL = {0x1406dea47, {0x83, 0xfb}, 2, 2, 1};        // cmp ebx, 8
constexpr uint64_t FARTHEST_VA = 0x14259858c, ZERO_LEFT_VA = 0x1425de6fc,
                   ZERO_RIGHT_VA = 0x1425a9668, STEP_VA = 0x14259d618, ORIGIN_VA = 0x142598580;

bool ReadImm(const PeImage& exe, const Imm& imm, int64_t* out) {
  uint8_t code[4] = {};
  if (!exe.ReadVa(imm.va, imm.opcodeLen, code) || std::memcmp(code, imm.opcode.data(), imm.opcodeLen))
    return false;
  uint8_t v[4] = {};
  if (!exe.ReadVa(imm.va + imm.offset, imm.width, v)) return false;
  int32_t x = 0;
  std::memcpy(&x, v, imm.width);
  *out = imm.width == 1 ? int8_t(v[0]) : x;
  return true;
}

}  // namespace

AnimetableCell Animetable::Cell(int row, int col) const {
  const auto& l = layout_;
  if (row == l.patchRow && col == l.patchCol) return {l.patchMotion, false};
  if (row < 0 || row >= l.rows || col < 0 || col >= l.cols) return {};
  const uint32_t v = cells_[size_t(row) * l.cols + col];
  const int m = int(v & l.motionMask);
  if (m == 0) return {};
  return {m - 1, bool(v >> l.mirrorShift & 1)};
}

int Animetable::Column(int row, float angle) const {
  const auto& l = layout_;
  int best = l.defaultCol;
  float bestDist = l.farthest_deg;
  for (int c = 0; c < l.cols; ++c) {
    if (row != l.noRow && !Populated(row, c)) continue;
    const int k = c <= l.splitCol ? c : c - 1;
    float centre = k * l.columnStep_deg - l.columnOrigin_deg;
    if (centre == 0.0f) centre = c == l.splitCol ? l.zeroLeft_deg : l.zeroRight_deg;
    const float d = std::fabs(angle - centre);
    if (d < bestDist) bestDist = d, best = c;
  }
  return best;
}

int Animetable::Row(std::string_view label) const {
  for (size_t r = 0; r < labels_.size(); ++r)
    if (labels_[r] == label) return int(r);
  return -1;
}

std::optional<Animetable> ParseAnimetable(const std::vector<uint8_t>& bytes,
                                          const AnimetableLayout& layout, std::string* error) {
  const size_t want = size_t(layout.rows) * layout.cols * sizeof(uint32_t);
  if (bytes.size() != want) {
    if (error) *error = "Animetable.bin: " + std::to_string(bytes.size()) + " bytes, layout wants " +
                        std::to_string(want);
    return std::nullopt;
  }
  Animetable t;
  t.layout_ = layout;
  t.cells_.resize(size_t(layout.rows) * layout.cols);
  std::memcpy(t.cells_.data(), bytes.data(), want);
  return t;
}

std::optional<Animetable> LoadAnimetable(const PeImage& exe, const PesVfs& vfs, std::string* error) {
  AnimetableLayout l;
  int64_t rows, cols, mask, shift, prow, pcol, pmot, dcol, scol;
  float f[5];
  const uint64_t fva[5] = {FARTHEST_VA, ZERO_LEFT_VA, ZERO_RIGHT_VA, STEP_VA, ORIGIN_VA};
  bool ok = ReadImm(exe, ROWS, &rows) && ReadImm(exe, COLS, &cols) &&
            ReadImm(exe, MOTION_MASK, &mask) && ReadImm(exe, MIRROR_SHIFT, &shift) &&
            ReadImm(exe, PATCH_ROW, &prow) && ReadImm(exe, PATCH_COL, &pcol) &&
            ReadImm(exe, PATCH_MOTION, &pmot) && ReadImm(exe, DEFAULT_COL, &dcol) &&
            ReadImm(exe, SPLIT_COL, &scol);
  for (int i = 0; i < 5; ++i) ok &= exe.ReadVa(fva[i], 4, reinterpret_cast<uint8_t*>(&f[i]));
  if (!ok) {
    *error = "animetable: PES2021.exe layout constants unmapped or unexpected instruction";
    return std::nullopt;
  }
  l.rows = int(rows), l.cols = int(cols), l.motionMask = uint32_t(mask), l.mirrorShift = int(shift);
  l.patchRow = int(prow), l.patchCol = int(pcol), l.patchMotion = int(pmot);
  l.defaultCol = int(dcol), l.splitCol = int(scol), l.noRow = l.rows;
  l.farthest_deg = f[0], l.zeroLeft_deg = f[1], l.zeroRight_deg = f[2];
  l.columnStep_deg = f[3], l.columnOrigin_deg = f[4];
  auto bytes = vfs.Read(ANIMETABLE_PATH, error);
  if (!bytes) return std::nullopt;
  auto t = ParseAnimetable(*bytes, l, error);
  if (!t) return std::nullopt;
  t->labels_.reserve(l.rows);
  for (int r = 0; r < l.rows; ++r) {
    auto p = exe.ReadU64(ROW_LABELS_VA + 8 * uint64_t(r));
    std::string name = p ? exe.ReadCString(*p) : "";
    if (name.empty()) {
      *error = "animetable: row label " + std::to_string(r) + " unreadable";
      return std::nullopt;
    }
    t->labels_.push_back(std::move(name));
  }
  return t;
}

}  // namespace pes
