#include "pessetpiecetaker.hpp"

#include <cstdint>

namespace pes {
namespace {

// Plain .rdata floats, all four verified against the shipped exe (OBS,
// docs/re/pes-re-setpiece-choreography.md 9 item 2 and
// pes-re-setpiece-kicker.md 2.4).
constexpr uint64_t CORNER_GATE_VA = 0x14259bde8;         // 3.5
constexpr uint64_t FREEKICK_GATE_VA = 0x14258c264;       // 3.0
constexpr uint64_t ONE_VA = 0x14256ae1c;                 // 1.0
constexpr uint64_t SHORT_CORNER_WINDOW_VA = 0x142565604;  // 2.0
constexpr uint64_t SHORT_CORNER_RUNNING_VA = 0x14259bdf0;  // 7.0

bool ReadFloat(const PeImage& exe, uint64_t va, float* out) {
  return exe.ReadVa(va, sizeof *out, reinterpret_cast<uint8_t*>(out));
}

}  // namespace

std::optional<SetPieceTakerConstants> LoadSetPieceTakerConstants(const PesVfs& vfs,
                                                                 const PeImage& exe,
                                                                 std::string* error) {
  SetPieceTakerConstants k{};
  bool ok = true;
  for (auto [va, out] :
       {std::pair{CORNER_GATE_VA, &k.cornerGate_s}, std::pair{FREEKICK_GATE_VA, &k.freeKickGate_s},
        std::pair{ONE_VA, &k.penaltyGate_s}, std::pair{SHORT_CORNER_WINDOW_VA, &k.shortCornerWindow_s},
        std::pair{SHORT_CORNER_RUNNING_VA, &k.shortCornerRunningWindow_s}}) {
    if (!ReadFloat(exe, va, out)) {
      char buf[32];
      std::snprintf(buf, sizeof buf, "%#llx", static_cast<unsigned long long>(va));
      *error = std::string("set-piece taker constant: unreadable .rdata at ") + buf;
      ok = false;
    }
  }
  if (!ok) return std::nullopt;
  // A gate of zero or less would release the kicker instantly; the shipped
  // values are all positive and a failed read must not look like a rule.
  if (k.cornerGate_s <= 0.0f || k.freeKickGate_s <= 0.0f || k.penaltyGate_s <= 0.0f ||
      k.shortCornerWindow_s <= 0.0f || k.shortCornerRunningWindow_s <= 0.0f) {
    *error = "set-piece taker constant: non-positive gate in .rdata";
    return std::nullopt;
  }
  // The VFS is not consulted for these: they are image constants, not VFS
  // objects. It is taken so the loader has the same shape as the other PES
  // loaders, which do read constant_*.bin records out of the install.
  (void)vfs;
  return k;
}

}  // namespace pes
