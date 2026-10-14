#pragma once

// PES's situation x angle clip table, Animetable.bin
// (docs/re/pes-re-animselect.md 2.3): every action class picks a row (a named
// situation), a relative angle picks one of the columns, the cell gives the
// motion number and a mirror bit. Read from the user's install; the layout
// numbers and the row labels come from the exe (parser FUN_1406aa440, lookup
// FUN_1406abb30, column picker FUN_1406de990, labels 0x1434997f0).

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "pes/exe.hpp"
#include "pes/vfs.hpp"

namespace pes {

struct AnimetableLayout {
  int rows = 0, cols = 0;
  uint32_t motionMask = 0;  // bits holding motionNo + 1 (0 = empty)
  int mirrorShift = 0;
  // FUN_1406abb30's one hard-coded cell: (row, col) -> motion, mirror off.
  int patchRow = -1, patchCol = -1, patchMotion = -1;
  // FUN_1406de990: column centre = k * step - origin, k = c < split ? c : c - 1.
  float columnStep_deg = 0.0f, columnOrigin_deg = 0.0f;
  float zeroLeft_deg = 0.0f, zeroRight_deg = 0.0f;  // the split 0 deg columns
  int splitCol = 8;      // columns past it shift down one bucket
  int defaultCol = 9;    // the answer when no column is populated
  float farthest_deg = 360.0f;  // the search's starting distance
  int noRow = -1;  // the "no situation" row id: every column counts as populated
};

struct AnimetableCell {
  int motionNo = -1;  // -1 = empty
  bool mirror = false;
};

class Animetable {
 public:
  const AnimetableLayout& Layout() const { return layout_; }
  const std::vector<std::string>& Labels() const { return labels_; }
  AnimetableCell Cell(int row, int col) const;
  bool Populated(int row, int col) const { return Cell(row, col).motionNo >= 0; }
  // The populated column nearest the relative angle (degrees), the middle
  // column when none is.
  int Column(int row, float angle_deg) const;
  int Row(std::string_view label) const;  // -1 when absent

 private:
  friend std::optional<Animetable> ParseAnimetable(const std::vector<uint8_t>&,
                                                   const AnimetableLayout&, std::string*);
  friend std::optional<Animetable> LoadAnimetable(const PeImage&, const PesVfs&, std::string*);
  AnimetableLayout layout_;
  std::vector<uint32_t> cells_;
  std::vector<std::string> labels_;
};

std::optional<Animetable> ParseAnimetable(const std::vector<uint8_t>& bytes,
                                          const AnimetableLayout& layout, std::string* error);
std::optional<Animetable> LoadAnimetable(const PeImage& exe, const PesVfs& vfs, std::string* error);

}  // namespace pes
