#pragma once

// PES 2021's random number generator, math::Random (docs/re/pes-re-rng.md 1).
// Four 32-bit slots: slots 0..2 step a 31-bit LCG,
//   s = (sub - s*mul) & 0x7FFFFFFF,
// and map through a per-slot modulus table; slot 3 is a separate 15-bit LCG,
//   s = (mul3*s + inc3) & mask3.
// Every constant is read from the user's PES2021.exe (stock bytes) through
// pes::PeImage::ReadVa: the instruction immediates of the draw function and
// the constructor, and the modulus table and 2^-31 scale from .rdata.
//
// Seeding (report 2.1): the match-context constructor commits one value t at
// kick-off as slots 0, 1, 2 = t, t+1, t+2; slot 3 keeps its constructor
// default. MatchSetup::seed is that t.

#include <array>
#include <cstdint>
#include <optional>
#include <string>

#include "pes/exe.hpp"

namespace pes {

inline constexpr int kRandomSlots = 4;
inline constexpr int kWeakSlot = 3;  // the 15-bit LCG slot
// The slot Range0 draws (0x140a92530's slot path, pesrandom.cpp's MATCH_SLOT):
// PES's per-system draw sites take their own slot (report 3), and this is the one
// Range0 always uses.
inline constexpr int kRange0Slot = 1;

struct RngConstants {
  uint32_t lcgMul;
  uint32_t lcgSub;
  std::array<int32_t, kRandomSlots> modulus;
  float scale;  // 2^-31
  uint32_t weakMul, weakInc, weakMask;
  std::array<uint32_t, kRandomSlots> defaultSeeds;
};

std::optional<RngConstants> LoadRngConstants(const PeImage& exe, std::string* error);

class Random {
 public:
  explicit Random(const RngConstants& k);

  void SetSeed(int slot, uint32_t value) { slots_[slot] = value; }
  uint32_t State(int slot) const { return slots_[slot]; }

  // The 31-bit step of slots 0..2 (0x140a92470's slot path); returns the new state.
  uint32_t Step(int slot);
  // Range(rng, slot) 0x140a92470: the step mapped to [0, modulus[slot]).
  int Range(int slot);
  // RangeRaw 0x140a924e0: the step's new state as a float in [0, 1).
  float Unit(int slot);
  // RangeRaw's other half, 0x140a924e0: `trunc(u * M)` on an explicit M, not
  // the slot's modulus entry. This is the injury roll's draw: PES passes
  // `Range(rng, 100, 2)` through it, so the modulus is 100, not modulus[2].
  int RangeRaw(int slot, int modulus);
  // Range0(rng, count) 0x140a92530: slot 1 mapped to [0, count).
  int Range0(int count);
  // Slot 3's bare advance (the 0x140a92470 idx == 3 branch).
  void AdvanceWeak();

 private:
  const RngConstants& k_;
  std::array<uint32_t, kRandomSlots> slots_;
};

// The kick-off seeding of the match generator (report 2.1).
void SeedAtKickoff(Random& rng, uint32_t t);

}  // namespace pes
