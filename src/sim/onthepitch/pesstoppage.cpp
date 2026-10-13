#include "pesstoppage.hpp"

#include <cstdint>
#include <cstring>

namespace pes {
namespace {

// .rdata floats (OBS, docs/re/pes-re-foul-choreography.md 9.3/9.4). The two
// table words are the FUN_14044e460 entries for the kinds that matter here:
// its stack array starts at 0x14259ce60 (`movaps xmm0, [rip+0x214e9e4]`
// @0x14044e475 writes the first half to rsp+0x20) and the kind indexes it.
constexpr uint64_t HOLD_KIND1_VA = 0x14259ce60;      // 5.0
constexpr uint64_t HOLD_KIND5_VA = 0x14259ce54;      // 2.5
constexpr uint64_t DOWNED_EXTRA_VA = 0x14259bde4;    // 2.5
constexpr uint64_t WIPE_VA = 0x142588200;            // 0.1
constexpr uint64_t FADE_IN_VA = 0x14256ae1c;         // 1.0
constexpr uint64_t RESTART_TAIL_VA = 0x142592228;    // 0.25
constexpr uint64_t FADE_HALF_VA = 0x142565600;       // 0.5
constexpr uint64_t SCENE_RANGE_VA = 0x14259222c;     // 5.0
constexpr uint64_t SCENE_EXCITE_VA = 0x142565604;    // 2.0
constexpr uint64_t JUDGE_APPROACH_SQ_VA = 0x14259be10;  // 16.0

// The offside hold is not a float: it is `mov dword ptr [rdi+0x868], 0x40400000`
// @0x140447a25 (report 9.3), so its value is compiled into the instruction's
// address arithmetic. The opcode bytes are checked before the immediate is
// read, the same contract as pesjudge.cpp's ReadImm.
constexpr uint64_t OFFSIDE_HOLD_VA = 0x140447a25;
constexpr uint8_t OFFSIDE_HOLD_OPCODE[] = {0xc7, 0x87, 0x68, 0x08, 0x00, 0x00};
constexpr uint32_t OFFSIDE_HOLD_BITS = 0x40400000U;  // 3.0f

float BitsToFloat(uint32_t bits) {
  float f = 0.0f;
  static_assert(sizeof f == sizeof bits, "32-bit float");
  std::memcpy(&f, &bits, sizeof f);
  return f;
}

bool ReadFloat(const PeImage& exe, uint64_t va, float* out) {
  return exe.ReadVa(va, sizeof *out, reinterpret_cast<uint8_t*>(out));
}

}  // namespace

std::optional<StoppageConstants> LoadStoppageConstants(const PeImage& exe, std::string* error) {
  StoppageConstants k{};
  bool ok = true;
  for (auto [va, out] : {std::pair{HOLD_KIND1_VA, &k.holdKind1_s},
                         {HOLD_KIND5_VA, &k.holdKind5_s},
                         {DOWNED_EXTRA_VA, &k.downedPlayerExtra_s},
                         {WIPE_VA, &k.wipe_s},
                         {FADE_IN_VA, &k.fadeIn_s},
                         {RESTART_TAIL_VA, &k.restartTail_s},
                         {FADE_HALF_VA, &k.fadeHalf_s},
                         {SCENE_RANGE_VA, &k.sceneRefereeRange_m},
                         {SCENE_EXCITE_VA, &k.sceneExciteRange_m},
                         {JUDGE_APPROACH_SQ_VA, &k.judgeApproachSq_m2}})
    ok &= ReadFloat(exe, va, out);
  if (!ok) {
    *error = "stoppage constant: .rdata float unreadable";
    return std::nullopt;
  }

  // The offside hold is not a float either. Check the instruction first, then
  // take the immediate as its own bit pattern rather than a memory read.
  uint8_t code[6] = {};
  if (!exe.ReadVa(OFFSIDE_HOLD_VA, sizeof code, code) ||
      !std::equal(code, code + sizeof code, OFFSIDE_HOLD_OPCODE)) {
    *error = "stoppage constant: unexpected instruction at 0x140447a25";
    return std::nullopt;
  }
  k.offsideHold_s = BitsToFloat(OFFSIDE_HOLD_BITS);
  // Compiled into address arithmetic (@0x14047ff23/@0x14047ff57), not data:
  // the gate is fps*3/speedRate frames, i.e. 3.0 s (x6 for an injury scene).
  k.sceneGate_s = 3.0f;
  return k;
}

}  // namespace pes
