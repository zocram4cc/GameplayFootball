#include "onthepitch/pestackle.hpp"

#include <array>
#include <cstring>

#include "pes/exe.hpp"

namespace pes {
namespace {

// FUN_14078fe90's dispatch: `DEC ECX; CMP ECX,7; JA default` then a jump table
// at 0x14078ff4c of 8 x uint32 offsets from the image base. Each case is
// `MOV EAX,ECX; LEA RCX,[RIP+disp32] -> the kind's table`.
constexpr uint64_t JUMP_TABLE_VA = 0x14078ff4c;
constexpr uint64_t KIND7_TABLE_VA = 0x1425e9b50;
constexpr uint64_t GETTER_VA = 0x14078fe90;
// FUN_14078b550's `MOV dword ptr [RCX + 0x44],0x7` (the default sub-kind).
constexpr uint64_t DEFAULT_VA = 0x14078b572;
constexpr int kGroups = 3;  // FUN_14078f4f0's speed class 0..2

bool ReadRipTarget(const PeImage& exe, uint64_t lea, uint64_t* target) {
  // MOV EAX,ECX (41 8b c1); LEA RCX,[RIP+disp32] (48 8d 0d)
  uint8_t code[3] = {};
  if (!exe.ReadVa(lea, 3, code) || code[0] != 0x41 || code[1] != 0x8b || code[2] != 0xc1)
    return false;
  uint8_t leaCode[3] = {};
  if (!exe.ReadVa(lea + 3, 3, leaCode) || leaCode[0] != 0x48 || leaCode[1] != 0x8d ||
      leaCode[2] != 0x0d)
    return false;
  int32_t disp = 0;
  if (!exe.ReadVa(lea + 6, 4, reinterpret_cast<uint8_t*>(&disp))) return false;
  *target = lea + 10 + uint64_t(int64_t(disp));
  return true;
}

// One table slot: {entries, ?, count}, where entries is an array of
// {row, bit} pairs (FUN_14078b550's `plVar3[1]` is the count and `*plVar3` the
// array). A zero count means the slot carries no group at all.
//
// ponytail: kinds 5 and 6's tables are laid out back to back and share their
// boundary (kind 5's second table at a slot is kind 6's first), so the last
// slot's second table of one kind runs into the next kind's data. A slot whose
// entries pointer is not in the image or whose count is implausible is read as
// carrying no group, which is what FUN_14078b550's own bit test ends up doing
// there anyway (its loop finds no live entry and moves on). The ceiling is
// that a genuinely corrupt exe would be read as a missing group rather than
// failing the load; the install test's per-kind non-empty check catches that.
bool ReadGroup(const PeImage& exe, uint64_t slotVa, std::vector<TackleGroupEntry>* out,
               std::string* error) {
  uint64_t entries = 0;
  int32_t count = 0;
  if (!exe.ReadVa(slotVa, 8, reinterpret_cast<uint8_t*>(&entries))) {
    *error = "tackle: a group slot is unreadable";
    return false;
  }
  if (!exe.ReadVa(slotVa + 8, 4, reinterpret_cast<uint8_t*>(&count))) {
    *error = "tackle: a group count is unreadable";
    return false;
  }
  constexpr uint64_t kImageLo = 0x140000000, kImageHi = 0x144000000;
  constexpr int32_t kMaxEntries = 64;
  if (entries < kImageLo || entries >= kImageHi || count <= 0 || count > kMaxEntries) {
    out->clear();  // not a group: FUN_14078b550 skips the slot
    return true;
  }
  out->clear();
  out->reserve(size_t(count));
  for (int32_t i = 0; i < count; ++i) {
    uint8_t buf[8] = {};
    if (!exe.ReadVa(entries + 8 * uint64_t(i), 8, buf)) {
      *error = "tackle: a group entry is unreadable";
      return false;
    }
    int32_t row = 0, bit = 0;
    std::memcpy(&row, buf, 4);
    std::memcpy(&bit, buf + 4, 4);
    if (row < 0 || bit < 0 || bit > 31) {
      *error = "tackle: a group entry is out of range";
      return false;
    }
    out->push_back({row, bit});
  }
  return true;
}

// FUN_14078e5a0's constants, each through the instruction that references it.
// Every one of them is a RIP-relative load, so the reader resolves the target
// and then reads the float: MOVSS (f3 0f 10 modrm disp32), MULSS
// (f3 0f 59 modrm disp32) and COMISS (0f 2f modrm disp32), each with an
// optional REX prefix (f3 44 0f ...).
struct Imm {
  uint64_t va;
  std::array<uint8_t, 4> opcode;  // the prefix, opcode and ModRM's leading byte
  size_t opcodeLen, dispOffset;   // where the disp32 starts
  std::array<uint8_t, 4> modRm;   // the ModRM byte the instruction uses
};

bool ReadRipF32(const PeImage& exe, const Imm& imm, float* out) {
  uint8_t code[8] = {};
  if (!exe.ReadVa(imm.va, imm.opcodeLen, code) ||
      std::memcmp(code, imm.opcode.data(), imm.opcodeLen) != 0)
    return false;
  if (!exe.ReadVa(imm.va + imm.opcodeLen, 1, code) || code[0] != imm.modRm[0]) return false;
  int32_t disp = 0;
  if (!exe.ReadVa(imm.va + imm.dispOffset, 4, reinterpret_cast<uint8_t*>(&disp))) return false;
  const uint64_t target = imm.va + imm.dispOffset + 4 + uint64_t(int64_t(disp));
  return exe.ReadVa(target, 4, reinterpret_cast<uint8_t*>(out));
}

// MOVSS XMMn,[RIP+disp32]: f3 0f 10 <modrm> <disp32>, the next instruction at
// va + 8. COMISS XMMn,[RIP+disp32]: 0f 2f <modrm> <disp32>, next at va + 7.
// MULSS XMMn,[RIP+disp32]: f3 0f 59 <modrm> <disp32>, next at va + 8; with a
// REX prefix (f3 44 0f ...) the next is va + 9.
constexpr Imm OPPOSED_DIST = {0x14078eb13, {0xf3, 0x0f, 0x10}, 3, 4, {0x35}};
constexpr Imm NEAR_SQ = {0x14078ece9, {0xf3, 0x0f, 0x10}, 3, 4, {0x3d}};
constexpr Imm KEEPER_SQ = {0x14078ed10, {0xf3, 0x0f, 0x10}, 3, 4, {0x3d}};
constexpr Imm KEEPER_GATE = {0x14078ed07, {0x0f, 0x2f}, 2, 3, {0x05}};
constexpr Imm ANGLE_NEAR = {0x14078ed36, {0xf3, 0x0f, 0x10}, 3, 4, {0x0d}};
constexpr Imm ANGLE_MID = {0x14078ed6a, {0xf3, 0x0f, 0x10}, 3, 4, {0x35}};
constexpr Imm DELAY_WINDOW = {0x14078e7d7, {0xf3, 0x0f, 0x59}, 3, 4, {0x3d}};
constexpr Imm DELAY_S2 = {0x14078ea34, {0xf3, 0x0f, 0x10}, 3, 4, {0x35}};
constexpr Imm DELAY_S1 = {0x14078ea64, {0xf3, 0x0f, 0x59}, 3, 4, {0x05}};
// f3 44 0f 10 <modrm> <disp32>, the next instruction at va + 9.
constexpr Imm DELAY_FPS = {0x14078e7c1, {0xf3, 0x44, 0x0f, 0x10}, 4, 5, {0x0d}};
constexpr Imm DELAY_FLOOR = {0x14078e7ce, {0xf3, 0x44, 0x0f, 0x10}, 4, 5, {0x15}};

}  // namespace

std::optional<TackleConstants> LoadTackleConstants(const PeImage& exe, std::string* error) {
  TackleConstants k;
  // The dispatch is FUN_14078fe90's own: verify the DEC/CMP/JA head and the
  // `MOV [RCX+0x44],7` default so a different executable fails the load.
  uint8_t head[6] = {};
  if (!exe.ReadVa(GETTER_VA, 6, head) || head[0] != 0xff || head[1] != 0xc9 || head[2] != 0x83 ||
      head[3] != 0xf9 || head[4] != 0x07 || head[5] != 0x0f) {
    *error = "tackle: FUN_14078fe90's dispatch is not the PES 2021 instruction";
    return std::nullopt;
  }
  uint8_t def[7] = {};
  if (!exe.ReadVa(DEFAULT_VA, 7, def) || def[0] != 0xc7 || def[1] != 0x41 || def[2] != 0x44) {
    *error = "tackle: FUN_14078b550's default store is not the PES 2021 instruction";
    return std::nullopt;
  }
  int32_t defKind = 0;
  std::memcpy(&defKind, def + 3, 4);
  k.defaultSubKind = defKind;

  for (int kind = 1; kind <= 8; ++kind) {
    // Each case: its LEA's target is the kind's table base. Kind 7's is the
    // fixed 0x1425e9b50 (its case builds the address arithmetically instead).
    uint64_t base = 0;
    if (kind == 7) {
      base = KIND7_TABLE_VA;
    } else {
      uint32_t off = 0;
      if (!exe.ReadVa(JUMP_TABLE_VA + 4 * uint64_t(kind - 1), 4,
                      reinterpret_cast<uint8_t*>(&off))) {
        *error = "tackle: the kind dispatch table is unreadable";
        return std::nullopt;
      }
      if (!ReadRipTarget(exe, 0x140000000 + off, &base)) {
        *error = "tackle: a kind case is not MOV EAX,ECX; LEA RCX,[RIP+disp32]";
        return std::nullopt;
      }
    }
    // Stride: kinds 1, 2, 3, 7, 8 index one slot per class; kinds 4, 5, 6
    // carry two (the second at +0x10, stride 0x20).
    const bool twoPerSlot = kind == 4 || kind == 5 || kind == 6;
    const int stride = twoPerSlot ? 0x20 : 0x10;
    for (int slot = 0; slot < kGroups; ++slot) {
      if (kind == 7) {
        // Kind 7's index is (param_2 + slot * 2), so slot s at param_2 = 0 is
        // (2s) and at param_2 = 1 it is (1 + 2s).
        if (!ReadGroup(exe, base + size_t(2 * slot) * stride, &k.group7[0][slot], error))
          return std::nullopt;
        if (!ReadGroup(exe, base + size_t(1 + 2 * slot) * stride, &k.group7[1][slot], error))
          return std::nullopt;
      } else {
        if (!ReadGroup(exe, base + size_t(slot) * stride, &k.groups[kind - 1][slot], error))
          return std::nullopt;
      }
      if (twoPerSlot) {
        // The second table of the slot, 0x10 into it.
        if (!ReadGroup(exe, base + size_t(slot) * stride + 0x10, &k.groups[kind - 1][slot], error))
          return std::nullopt;
      }
    }
  }
  // FUN_14078e5a0's bit-4 constants, opcode-checked through its own
  // referencing instructions.
  if (!ReadRipF32(exe, OPPOSED_DIST, &k.opposedBody_m) ||
      !ReadRipF32(exe, NEAR_SQ, &k.pairNearSq_m) || !ReadRipF32(exe, KEEPER_SQ, &k.pairKeeperSq_m) ||
      !ReadRipF32(exe, KEEPER_GATE, &k.keeperGate_m) ||
      !ReadRipF32(exe, ANGLE_NEAR, &k.headingToBall_deg) ||
      !ReadRipF32(exe, ANGLE_MID, &k.carrierToPoint_deg) ||
      !ReadRipF32(exe, DELAY_WINDOW, &k.delayWindow_s) ||
      !ReadRipF32(exe, DELAY_FPS, &k.delayFramesPerSecond) ||
      !ReadRipF32(exe, DELAY_S2, &k.delayStream2_s) ||
      !ReadRipF32(exe, DELAY_S1, &k.delayStream1_s) ||
      !ReadRipF32(exe, DELAY_FLOOR, &k.delayFloor_s)) {
    *error = "tackle: an exe readiness constant is unmapped or unexpected instruction";
    return std::nullopt;
  }
  k.carrierToStep_deg = k.carrierToPoint_deg;  // the third test reuses the bound
  return k;
}

uint32_t ReadinessMask(const TackleReadiness& r, const TackleConstants& k) {
  // FUN_14078e5a0's uVar17, bit by bit. Bits 1..8 are kinds 1..8: the pick
  // tests `(mask >> kind) & 1`.
  uint32_t mask = 0;
  // bit 1 (kind 1): may play, and the post-tackle cooldown has cleared.
  if (r.mayPlay && r.cooldownClear) mask |= 0x2;
  // bit 2 (kind 2): he is touching, and the opposed body is inside 5 m.
  const bool opposed = r.opposedBody_m >= 0.0f && r.opposedBody_m < k.opposedBody_m;
  if (r.touching && opposed) mask |= 0x4;
  // bit 3 (kind 3): the second-man flag, and no other tackle already running.
  if (r.secondMan && !r.otherTackleCommitted) mask |= 0x8;
  // bit 4 (kind 4): the pair's squared separation clears the near bound (0.09
  // m^2 when the ball is lifted), the three angular tests pass, and the
  // tackle's frame-delay roll does.
  const float bound = r.ballLifted ? k.pairKeeperSq_m : k.pairNearSq_m;
  if (r.pairDistSq_m > bound && r.headingToBall_deg < k.headingToBall_deg &&
      r.carrierToPoint_deg < k.carrierToPoint_deg && r.carrierToStep_deg < k.carrierToStep_deg &&
      r.delayRollOk)
    mask |= 0x10;
  // bits 5-6 (kinds 5, 6): the same touching gate, set as one pair (OR EDI,0x60).
  if (r.touching && opposed) mask |= 0x60;
  // bit 7 (kind 7): the fallback, when neither contact flag is live.
  if (!r.touching && !r.secondMan) mask |= 0x80;
  // bit 8 (kind 8): the "not in play" early-out.
  if (!r.mayPlay) mask |= 0x100;
  return mask;
}

TackleSubKind PickSubKind(const TackleConstants& k, uint32_t readiness,
                          const std::array<uint32_t, 8>& clipMask, const Animetable& table,
                          const std::array<bool, 8>& solverOk) {
  // FUN_14078b550's loop: kinds 1..8 in order; the first kind whose readiness
  // bit is set AND whose group table has an entry whose clip-mask bit is set
  // AND whose row the solver solved wins.
  for (int kind = 1; kind <= 8; ++kind) {
    if (!((readiness >> kind) & 1u)) continue;
    const auto& slots = kind == 7 ? k.group7[0] : k.groups[kind - 1];
    for (int slot = 0; slot < kGroups; ++slot) {
      for (const auto& entry : slots[slot]) {
        if (entry.bit > 31) continue;
        if (!((clipMask[slot] >> entry.bit) & 1u)) continue;
        (void)table;  // the row -> clip step is ResolveClip's, once a kind wins
        if (solverOk[kind - 1]) return static_cast<TackleSubKind>(kind);
      }
    }
  }
  return static_cast<TackleSubKind>(k.defaultSubKind);
}

TackleSubKind PickSubKind(const TackleConstants& k, const TackleReadiness& ready,
                          const std::array<uint32_t, 8>& clipMask, const Animetable& table,
                          const std::array<bool, 8>& solverOk) {
  return PickSubKind(k, ReadinessMask(ready, k), clipMask, table, solverOk);
}

std::optional<TackleClip> ResolveClip(const Animetable& t, const TackleConstants& k, int row,
                                      float relativeAngle_deg) {
  (void)k;
  if (row < 0 || row >= t.Layout().rows) return std::nullopt;
  // FUN_1406de990: the populated column nearest the angle, the middle column
  // when none is (the Animetable's own picker, as the dodge selectors use it).
  const int col = t.Column(row, relativeAngle_deg);
  const AnimetableCell cell = t.Cell(row, col);
  if (cell.motionNo < 0) return std::nullopt;
  return TackleClip{row, col, cell.motionNo, cell.mirror};
}

uint32_t ClipMask(const TackleClipMaskInput& in) {
  // FUN_14078f290's uVar13. 0xffffffff when the record is not live, or the kind
  // is 5 or 6 - both are gated off entirely, so their group tables are never
  // walked (and the install's tables for them are the shared boundary the
  // reader treats as empty anyway).
  if (!in.recordLive || in.kind == 5 || in.kind == 6) return 0xffffffffu;
  uint32_t mask = in.standing ? 1u : 0u;
  if (in.keeperBallHands) {
    // The keeper's-hands branch (his action class is 0x34/0x35): +4, and +2
    // with it when the ball's resolved point is in front.
    mask |= 4u;
    if (in.ballInFront) mask |= 2u;
  } else if (in.ballAheadOfSelf) {
    // The record's own +0x15 flag: +2, and +6 when the ball is in front.
    mask |= 2u;
    if (in.ballInFront) mask |= 4u;
  } else if (in.committed && in.ballInFront) {
    // The committed branch: +2 only.
    mask |= 2u;
  }
  return mask;
}

TackleChoice ChooseTackle(const TackleConstants& k, const Animetable& t, const TackleSituation& s) {
  // ponytail: GF carries no per-player RNG draw on PES's streams, so the delay
  // roll is answered as passed. It gates bit 4 alone, and bit 4 is not the
  // sub-kind a standing tackle usually takes (kinds 5/6 are), so answering it
  // optimistically costs nothing today and is named here.
  TackleReadiness ready;
  ready.mayPlay = s.mayPlay;
  ready.touching = s.touching;
  ready.secondMan = s.secondMan;
  ready.opposedBody_m = s.opposedBody_m;
  ready.cooldownClear = true;
  ready.otherTackleCommitted = s.otherTackleCommitted;
  ready.pairDistSq_m = s.pairDistSq_m;
  ready.ballLifted = s.ballHeight_m > k.keeperGate_m;
  ready.headingToBall_deg = s.bearingToCarrier_deg;
  ready.carrierToPoint_deg = s.bearingToCarrier_deg;
  ready.carrierToStep_deg = s.bearingToCarrier_deg;
  ready.delayRollOk = true;
  const uint32_t readiness = ReadinessMask(ready, k);
  // Every kind's solver is answered "yes": see the header's ponytail.
  const std::array<bool, 8> solverOk{true, true, true, true, true, true, true, true};
  // FUN_14078b550 asks FUN_14078f290 for each kind's clip mask as it goes, so
  // the pick walks a per-kind mask rather than one shared word.
  const auto maskFor = [&s, &k](int kind) {
    TackleClipMaskInput in;
    in.recordLive = true;  // the caller only asks for kinds the readiness admits
    in.kind = kind;
    in.standing = true;
    in.committed = s.touching;
    in.ballInFront = true;
    in.ballAheadOfSelf = s.secondMan;
    in.keeperBallHands = s.ballHeight_m > k.keeperGate_m;
    return ClipMask(in);
  };
  // The slot indexed by the mask word is FUN_14078f4f0's speed class, so the
  // array carries the same word at the class the situation names.
  const int cls = s.speedClass < 0 ? 0 : (s.speedClass > 2 ? 2 : s.speedClass);
  std::array<uint32_t, 3> classMask{};
  for (int c = 0; c < 3; ++c) classMask[c] = maskFor(c + 1);
  // The pick reads one word per slot, so hand it the class's word everywhere.
  std::array<uint32_t, 8> clipMask{};
  for (int i = 0; i < 8; ++i) clipMask[i] = classMask[cls];
  const TackleSubKind kind = PickSubKind(k, readiness, clipMask, t, solverOk);
  TackleChoice choice;
  choice.kind = kind;
  // The winning kind's first live entry names the row, whose column for the
  // defender's bearing gives the clip.
  const auto& slots = int(kind) == 7 ? k.group7[0] : k.groups[int(kind) - 1];
  for (int slot = 0; slot < 3 && choice.motionNo < 0; ++slot) {
    for (const auto& entry : slots[slot]) {
      if (entry.bit > 31 || !((clipMask[slot] >> entry.bit) & 1u)) continue;
      auto clip = ResolveClip(t, k, entry.row, s.bearingToCarrier_deg);
      if (!clip) continue;
      choice.row = clip->row;
      choice.motionNo = clip->motionNo;
      choice.mirror = clip->mirror;
      break;
    }
  }
  return choice;
}

}  // namespace pes
