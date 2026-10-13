#include "pesrandom.hpp"

#include <algorithm>
#include <cstdio>
#include <initializer_list>

namespace pes {
namespace {

// {VA, expected opcode bytes, immediate offset}: the opcode is checked before
// the immediate is trusted, so another build fails loudly.
struct Imm {
  uint64_t va;
  std::initializer_list<uint8_t> opcode;
  int offset;
};
constexpr Imm LCG_MUL{0x140a92491, {0x69, 0x44, 0x91, 0x08}, 4};  // imul eax,[rcx+rdx*4+8],mul
constexpr Imm LCG_SUB{0x140a92499, {0xb9}, 1};                    // mov ecx, sub
constexpr Imm WEAK_MUL{0x140a92478, {0x6b, 0x41, 0x14}, 3};       // imul eax,[rcx+0x14],mul3
constexpr Imm WEAK_INC{0x140a9247c, {0x83, 0xc0}, 2};             // add eax, inc3
constexpr Imm WEAK_MASK{0x140a9247f, {0x25}, 1};                  // and eax, mask3
// Constructor 0x140a923f0: mov dword [rcx+8+4i], seed_i.
constexpr Imm SEED[kRandomSlots] = {{0x140a923fd, {0xc7, 0x41, 0x08}, 3},
                                    {0x140a92404, {0xc7, 0x41, 0x0c}, 3},
                                    {0x140a9240b, {0xc7, 0x41, 0x10}, 3},
                                    {0x140a92412, {0xc7, 0x41, 0x14}, 3}};
constexpr uint64_t MODULUS_TABLE_VA = 0x1426548b0;  // {100, 5000, 32768, 32768}
constexpr uint64_t SCALE_VA = 0x1426550d8;          // 2^-31

constexpr uint32_t BIT31_CLEAR = 0x7FFFFFFFu;  // `btr ecx, 0x1f`
constexpr int MATCH_SLOT = 1;                  // Range0 draws slot 1

template <typename T>
bool ReadImm(const PeImage& exe, const Imm& imm, T* out, std::string* error) {
  uint8_t code[8] = {};
  if (!exe.ReadVa(imm.va, imm.opcode.size(), code) ||
      !std::equal(imm.opcode.begin(), imm.opcode.end(), code) ||
      !exe.ReadVa(imm.va + imm.offset, sizeof *out, reinterpret_cast<uint8_t*>(out))) {
    char va[32];
    std::snprintf(va, sizeof va, "%#llx", static_cast<unsigned long long>(imm.va));
    *error = std::string("rng constant: unexpected instruction at ") + va;
    return false;
  }
  return true;
}

}  // namespace

std::optional<RngConstants> LoadRngConstants(const PeImage& exe, std::string* error) {
  RngConstants k{};
  uint8_t weakMul = 0, weakInc = 0;
  if (!ReadImm(exe, LCG_MUL, &k.lcgMul, error) || !ReadImm(exe, LCG_SUB, &k.lcgSub, error) ||
      !ReadImm(exe, WEAK_MUL, &weakMul, error) || !ReadImm(exe, WEAK_INC, &weakInc, error) ||
      !ReadImm(exe, WEAK_MASK, &k.weakMask, error))
    return std::nullopt;
  k.weakMul = weakMul;
  k.weakInc = weakInc;
  for (int i = 0; i < kRandomSlots; ++i)
    if (!ReadImm(exe, SEED[i], &k.defaultSeeds[i], error)) return std::nullopt;
  if (!exe.ReadVa(MODULUS_TABLE_VA, sizeof k.modulus, reinterpret_cast<uint8_t*>(k.modulus.data())) ||
      !exe.ReadVa(SCALE_VA, sizeof k.scale, reinterpret_cast<uint8_t*>(&k.scale))) {
    *error = "rng constant: .rdata unreadable";
    return std::nullopt;
  }
  return k;
}

Random::Random(const RngConstants& k) : k_(k), slots_(k.defaultSeeds) {}

uint32_t Random::Step(int slot) {
  // imul (32-bit wrap), sub from the constant, clear bit 31.
  slots_[slot] = (k_.lcgSub - slots_[slot] * k_.lcgMul) & BIT31_CLEAR;
  return slots_[slot];
}

int Random::Range(int slot) {
  if (slot == kWeakSlot) {
    AdvanceWeak();
    return 0;  // the weak branch returns nothing; callers read the state
  }
  const uint32_t s = Step(slot);
  const int32_t m = k_.modulus[slot];
  // cvtsi2ss both, (float)s * 2^-31 * (float)M, cvttss2si, unsigned clamp to M-1.
  const auto r = static_cast<uint32_t>(static_cast<int64_t>(
      static_cast<float>(s) * k_.scale * static_cast<float>(m)));
  return static_cast<uint32_t>(m) <= r ? m - 1 : static_cast<int>(r);
}

float Random::Unit(int slot) { return static_cast<float>(Step(slot)) * k_.scale; }

int Random::Range0(int count) {
  if (count <= 0) return 0;
  const int r = static_cast<int>(static_cast<float>(Step(MATCH_SLOT)) * k_.scale *
                                 static_cast<float>(count));
  return std::min(count - 1, std::max(0, r));
}

void Random::AdvanceWeak() {
  slots_[kWeakSlot] = (k_.weakMul * slots_[kWeakSlot] + k_.weakInc) & k_.weakMask;
}

void SeedAtKickoff(Random& rng, uint32_t t) {
  for (int slot = 0; slot < kWeakSlot; ++slot) rng.SetSeed(slot, t + static_cast<uint32_t>(slot));
}

}  // namespace pes
