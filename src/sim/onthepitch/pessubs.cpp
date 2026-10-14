#include "pessubs.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <initializer_list>

namespace pes {
namespace {

struct Imm {
  uint64_t va;
  std::initializer_list<uint8_t> opcode;
  int offset;
};

template <typename T>
bool ReadImm(const PeImage& exe, const Imm& imm, T* out, std::string* error) {
  uint8_t code[8] = {};
  if (!exe.ReadVa(imm.va, imm.opcode.size(), code) ||
      !std::equal(imm.opcode.begin(), imm.opcode.end(), code) ||
      !exe.ReadVa(imm.va + imm.offset, sizeof *out, reinterpret_cast<uint8_t*>(out))) {
    char va[32];
    std::snprintf(va, sizeof va, "%#llx", static_cast<unsigned long long>(imm.va));
    *error = std::string("substitution constant: unexpected instruction at ") + va;
    return false;
  }
  return true;
}

constexpr uint64_t IMAGE_BASE = 0x140000000;

// The cap schedule: `cmp esi, imm8` on the minute.
constexpr Imm MODE1_TWO_THIRDS{0x140523cca, {0x83, 0xfe}, 2};  // 25
constexpr Imm MODE2_TWO_THIRDS{0x140523ca6, {0x83, 0xfe}, 2};  // 25
constexpr Imm MODE2_THIRD{0x140523cab, {0x83, 0xfe}, 2};       // 15
constexpr Imm MODE3_FULL{0x140523c91, {0x83, 0xfe}, 2};        // 20
constexpr Imm MODE3_TWO_THIRDS{0x140523c96, {0x83, 0xfe}, 2};  // 10

// The class switch `cmp ecx(class-2), 7; jmp [table]` and its four blocks.
// Each block sets ebx = threshold and esi = base for the mode in eax: mode 1
// by `lea ebx, [rax + d8]` (rax = 1), mode 3 by a `cmove` immediate, any other
// mode by the block's first immediate.
constexpr uint64_t CLASS_TABLE_VA = 0x140524280;
constexpr int FIRST_SWITCHED_CLASS = 2;
struct Block {
  uint64_t entry;
  Imm mode1Lea;          // lea ebx, [rax + d8]
  Imm mode2;             // mov ebx, imm32 (absent: 0)
  Imm mode3;             // mov eax, imm32 (the cmove source)
  Imm base;              // mov esi, imm32 (absent: 0)
  bool hasMode1, hasMode2, hasBase;
};
constexpr Block BLOCKS[] = {
    {0x140523fb6, {0x140523fbb, {0x8d, 0x58}, 2}, {0x140523fce, {0xbb}, 1},
     {0x140523fd3, {0xb8}, 1}, {0x140523fbe, {0xbe}, 1}, true, true, true},  // DMF, CMF
    {0x140523fe5, {0x140523fef, {0x8d, 0x58}, 2}, {0x140523fe5, {0xbb}, 1},
     {0x140524004, {0xb8}, 1}, {0x140523ff2, {0xbe}, 1}, true, true, true},  // FB, side mid
    {0x140524011, {0x14052401e, {0x8d, 0x58}, 2}, {0x140524011, {0xbb}, 1},
     {0x140524029, {0xb8}, 1}, {0, {}, 0}, true, true, false},  // attackers (base r12d = 0)
    {0x140524039, {0, {}, 0}, {0, {}, 0}, {0x140524047, {0xb8}, 1},
     {0x14052404f, {0xbe}, 1}, false, false, true},  // default (ebx = r12d = 0)
};

constexpr uint64_t TIER_GAP_VA = 0x14256ae1c;  // 1.0
constexpr uint64_t FIT_WIDE_VA = 0x14259be0c;  // 15.0
constexpr uint64_t FIT_NEAR_VA = 0x14259bdf4;  // 8.0

bool ReadBlock(const PeImage& exe, const Block& b, int* thr, int* base, std::string* error) {
  int8_t d8 = 0;
  int32_t m2 = 0, m3 = 0, bs = 0;
  if ((b.hasMode1 && !ReadImm(exe, b.mode1Lea, &d8, error)) ||
      (b.hasMode2 && !ReadImm(exe, b.mode2, &m2, error)) || !ReadImm(exe, b.mode3, &m3, error) ||
      (b.hasBase && !ReadImm(exe, b.base, &bs, error)))
    return false;
  constexpr int kMode1Rax = 1;
  thr[0] = b.hasMode1 ? kMode1Rax + d8 : 0;
  thr[1] = m2;
  thr[2] = m3;
  *base = bs;
  return true;
}

// Position groups of FUN_140522970: GK, DEF, MID, FWD.
enum Group { GK, DEF, MID, FWD };
Group GroupOf(e_PlayerRole r) {
  switch (r) {
    case e_PlayerRole_GK: return GK;
    case e_PlayerRole_CB: case e_PlayerRole_LB: case e_PlayerRole_RB: return DEF;
    case e_PlayerRole_DM: case e_PlayerRole_CM: case e_PlayerRole_LM: case e_PlayerRole_RM:
    case e_PlayerRole_AM: return MID;
    default: return FWD;
  }
}

// +6: the main-position tier, by the needed group (rows) and his main
// position's group (columns), as FUN_140522970 branches.
int MainTier(Group need, Group main) {
  static const int kTier[4][4] = {{4, 3, 2, 1}, {0, 3, 2, 1}, {0, 1, 2, 1}, {0, 1, 2, 3}};
  return kTier[need][main];
}

bool HasAny(const std::vector<e_PlayerRole>& regs, std::initializer_list<int> lo_hi) {
  const int lo = *lo_hi.begin(), hi = *(lo_hi.begin() + 1);
  for (e_PlayerRole r : regs)
    if (r >= lo && r <= hi) return true;
  return false;
}

// +7: the secondary tier, any registered flag in the listed ranges.
int SecondaryTier(Group need, const std::vector<e_PlayerRole>& regs) {
  switch (need) {
    case GK: return HasAny(regs, {0, 0}) ? 3 : HasAny(regs, {1, 3}) ? 2 : HasAny(regs, {4, 7}) ? 1 : 0;
    case DEF: return HasAny(regs, {1, 3}) ? 2 : HasAny(regs, {4, 7}) ? 1 : 0;
    case MID: return HasAny(regs, {4, 8}) ? 2 : HasAny(regs, {9, 12}) ? 1 : 0;
    default: return HasAny(regs, {9, 12}) ? 2 : HasAny(regs, {5, 8}) ? 1 : 0;
  }
}

struct Rated {
  int index, mainTier, secTier;
  bool isMain, canPlay, superSub;
  float fit;
};

// FUN_140520390: a better than b.
bool Better(const SubstitutionConstants& k, const Rated& a, const Rated& b) {
  if (a.mainTier > b.mainTier + k.tierGap) return true;
  if (std::abs(a.mainTier - b.mainTier) > k.tierGap) return false;
  if (a.secTier > b.secTier + k.tierGap) return true;
  if (std::abs(a.secTier - b.secTier) > k.tierGap) return false;
  if (a.canPlay != b.canPlay) return a.canPlay;
  const int d = static_cast<int>(a.fit - b.fit);
  if (d > k.fitWide) return true;
  if (std::abs(d) > k.fitWide) return false;
  if (a.superSub > b.superSub || a.secTier > b.secTier) return true;
  if (a.secTier < b.secTier) return false;
  if (d > k.fitNear) return true;
  if (std::abs(d) > k.fitNear) return false;
  if (a.isMain != b.isMain) return a.isMain;
  return a.fit > b.fit;
}

}  // namespace

std::optional<SubstitutionConstants> LoadSubstitutionConstants(const PeImage& exe,
                                                               std::string* error) {
  SubstitutionConstants k{};
  int8_t m1 = 0, m2a = 0, m2b = 0, m3a = 0, m3b = 0;
  if (!ReadImm(exe, MODE1_TWO_THIRDS, &m1, error) || !ReadImm(exe, MODE2_TWO_THIRDS, &m2a, error) ||
      !ReadImm(exe, MODE2_THIRD, &m2b, error) || !ReadImm(exe, MODE3_FULL, &m3a, error) ||
      !ReadImm(exe, MODE3_TWO_THIRDS, &m3b, error))
    return std::nullopt;
  k.mode1TwoThirdsFrom = m1;
  k.mode2TwoThirdsFrom = m2a;
  k.mode2ThirdFrom = m2b;
  k.mode3FullFrom = m3a;
  k.mode3TwoThirdsFrom = m3b;
  // GK and CB: `class <= 1 -> continue` ahead of the switch.
  for (int c = 0; c < FIRST_SWITCHED_CLASS; ++c) k.neverSubbed[c] = true;
  for (int c = FIRST_SWITCHED_CLASS; c < kSubRoleClasses; ++c) {
    int32_t rel = 0;
    if (!exe.ReadVa(CLASS_TABLE_VA + 4 * (c - FIRST_SWITCHED_CLASS), sizeof rel,
                    reinterpret_cast<uint8_t*>(&rel))) {
      *error = "substitution constant: class jump table unreadable";
      return std::nullopt;
    }
    const uint64_t target = IMAGE_BASE + static_cast<uint32_t>(rel);
    const Block* block = nullptr;
    for (const Block& b : BLOCKS)
      if (b.entry == target) block = &b;
    if (!block) {
      *error = "substitution constant: class jump lands outside the four known blocks";
      return std::nullopt;
    }
    if (!ReadBlock(exe, *block, k.threshold[c], &k.base[c], error)) return std::nullopt;
  }
  if (!exe.ReadVa(TIER_GAP_VA, sizeof k.tierGap, reinterpret_cast<uint8_t*>(&k.tierGap)) ||
      !exe.ReadVa(FIT_WIDE_VA, sizeof k.fitWide, reinterpret_cast<uint8_t*>(&k.fitWide)) ||
      !exe.ReadVa(FIT_NEAR_VA, sizeof k.fitNear, reinterpret_cast<uint8_t*>(&k.fitNear))) {
    *error = "substitution constant: .rdata unreadable";
    return std::nullopt;
  }
  return k;
}

int SubRoleClass(e_PlayerRole role) {
  switch (role) {
    case e_PlayerRole_GK: return 0;
    case e_PlayerRole_CB: return 1;
    case e_PlayerRole_LB: case e_PlayerRole_RB: return 2;
    case e_PlayerRole_DM: return 3;
    case e_PlayerRole_CM: return 4;
    case e_PlayerRole_LM: case e_PlayerRole_RM: return 5;
    case e_PlayerRole_AM: return 6;
    case e_PlayerRole_SS: return 7;
    case e_PlayerRole_LW: case e_PlayerRole_RW: return 8;
    default: return 9;  // CF
  }
}

int StaminaSubCap(const SubstitutionConstants& k, int mode, bool firstHalf, bool secondHalf,
                  int m, int cap) {
  if (firstHalf || mode < 1 || mode > kSubModes) return 0;
  if (!secondHalf) return cap;
  // Integer thirds, as PES's multiply-high divisions compute them.
  const int twoThirds = cap * 2 / 3, third = cap / 3;
  switch (mode) {
    case 1: return m >= k.mode1TwoThirdsFrom ? twoThirds : 0;
    // PES also stores the full cap from minute 35, but the >= 25 store right
    // after it overwrites it: mode 2 never reaches the full cap.
    case 2: return m >= k.mode2TwoThirdsFrom ? twoThirds : m >= k.mode2ThirdFrom ? third : 0;
    default: return m >= k.mode3FullFrom ? cap : m >= k.mode3TwoThirdsFrom ? twoThirds : 0;
  }
}

std::vector<int> PickStaminaSubs(const SubstitutionConstants& k, int mode,
                                 const std::vector<StaminaSubCandidate>& onPitch) {
  std::vector<std::pair<float, int>> cands;
  if (mode < 1 || mode > kSubModes) return {};
  for (const StaminaSubCandidate& c : onPitch) {
    const int cls = SubRoleClass(c.role);
    if (k.neverSubbed[cls]) continue;
    // ponytail: PES also keeps a player with half his team's goals or assists
    // on, and halves the threshold for the pad user's player and a player in
    // form; GF records no per-player goals, has no pad user, and the form
    // gate's reference byte (obj+0x14) has no known writer.
    const int thr = k.threshold[cls][mode - 1];
    if (c.staminaPct <= thr) cands.push_back({static_cast<float>(k.base[cls] + c.staminaPct), c.index});
  }
  std::stable_sort(cands.begin(), cands.end(),
                   [](const auto& a, const auto& b) { return a.first < b.first; });
  std::vector<int> out;
  for (const auto& c : cands) out.push_back(c.second);
  return out;
}

int PickReplacement(const SubstitutionConstants& k, e_PlayerRole role, SubKind kind,
                    const std::vector<BenchCandidate>& bench) {
  const Group need = GroupOf(role);
  const Rated* best = nullptr;
  std::vector<Rated> rated;
  rated.reserve(bench.size());
  for (const BenchCandidate& c : bench) {
    if (c.registered.empty()) continue;
    Rated r{c.index,
            MainTier(need, GroupOf(c.registered.front())),
            SecondaryTier(need, c.registered),
            c.registered.front() == role,
            std::find(c.registered.begin(), c.registered.end(), role) != c.registered.end(),
            c.superSub,
            c.fit};
    if (kind == SubKind::Stamina && !r.canPlay && r.secTier == 0) continue;
    rated.push_back(r);
  }
  for (const Rated& r : rated)
    if (!best || Better(k, r, *best)) best = &r;
  return best ? best->index : -1;
}

}  // namespace pes
