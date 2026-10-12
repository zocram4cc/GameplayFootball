#pragma once

// PES 2021 EDIT save (EDIT00000000): decryption and the per-player appearance
// records. Read-only; layout and cipher in docs/re/pes-kit-assembly.md 8.1.
// The save path arrives at run time only (PES21_EDIT_SAVE), never as a default.

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include "pes/uniform.hpp"

namespace pes {

std::array<uint8_t, 64> Sha512(std::span<const uint8_t> data);

// MT19937 seeded with init_by_array, as the game's FUN_1414136a0.
class Mt19937 {
 public:
  explicit Mt19937(const std::vector<uint32_t>& key);
  uint32_t Next();

 private:
  std::array<uint32_t, 624> state_{};
  size_t index_ = 624;
};

// FUN_14140e0f0: MT keystream mixed with four rotated earlier draws; its own
// inverse.
std::vector<uint8_t> CryptStream(const std::array<uint8_t, 64>& key, std::span<const uint8_t> in);

inline constexpr size_t kAppearanceRecordBytes = 72;
using AppearanceRecord = std::array<uint8_t, kAppearanceRecordBytes>;
using AppearanceTable = std::unordered_map<uint32_t, AppearanceRecord>;

struct EditSave {
  AppearanceTable appearances;  // by player id
};
// Decrypts the file, checks every block's SHA-512 against the header as the
// game does, and collects the appearance records.
std::optional<EditSave> ReadEditSave(std::span<const uint8_t> file, std::string* error);

// PlayerAppearance.bin, the game's fallback for players the save lacks:
// 60-byte {u32 id, 56-byte body} records, held as 72-byte records with zero
// boots/glove word and alternate id (pes-kit-assembly.md 8.1).
inline constexpr const char* kPlayerAppearancePath = "common/character0/model/character/appearance/PlayerAppearance.bin";
inline constexpr size_t kAppearanceFileRecordBytes = 60;
std::optional<AppearanceTable> ParsePlayerAppearanceFile(std::span<const uint8_t> blob, std::string* error);

// The fields of a 72-byte appearance record the part choice uses.
PlayerLook DecodeLook(const AppearanceRecord& record);

}  // namespace pes
