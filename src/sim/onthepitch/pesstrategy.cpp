#include "pesstrategy.hpp"

#include <algorithm>
#include <cstdint>
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

template <typename T>
bool ReadImm(const PeImage& exe, const Imm& imm, T* out, std::string* error) {
  uint8_t code[8] = {};
  if (!exe.ReadVa(imm.va, imm.opcode.size(), code) ||
      !std::equal(imm.opcode.begin(), imm.opcode.end(), code) ||
      !exe.ReadVa(imm.va + imm.offset, sizeof *out, reinterpret_cast<uint8_t*>(out))) {
    char va[32];
    std::snprintf(va, sizeof va, "%#llx", static_cast<unsigned long long>(imm.va));
    *error = std::string("strategy constant: unexpected instruction at ") + va;
    return false;
  }
  return true;
}

// FUN_14051e970's minute-0 block (0x14051ea0c..0x14051eb94).
constexpr uint64_t STRENGTH_MARGIN_VA = 0x14258c260;                     // 0.9
constexpr Imm SLOT{0x14051ea0c, {0xbe}, 1};                              // mov esi, 2
constexpr Imm STRONGER_ATTACK{0x14051eac1, {0x41, 0xbf}, 2};             // mov r15d, 7
constexpr Imm STRONGER_BALANCE_DELTA{0x14051eac7, {0x45, 0x8d, 0x77}, 3};  // lea r14d,[r15-4]
constexpr Imm WEAKER_ATTACK{0x14051ead6, {0x41, 0xbf}, 2};               // mov r15d, 4 (= r12d)
constexpr Imm EVEN_ATTACK{0x14051eae4, {0x41, 0xbf}, 2};                 // mov r15d, 3
constexpr Imm EVEN_BALANCE{0x14051eaed, {0x41, 0xbe}, 2};                // mov r14d, 5
constexpr Imm ATTACK_HOLD_FROM{0x14051eb14, {0xbb}, 1};                  // mov ebx, 10
constexpr Imm ATTACK_HOLD_SPAN{0x14051eb19, {0x8d, 0x53}, 2};            // lea edx,[rbx+0x24]
constexpr Imm DEFEND_HOLD_FROM{0x14051eb2d, {0xbb}, 1};                  // mov ebx, 20
constexpr Imm HOLD_NEVER{0x14051eb71, {0xc7, 0x47, 0x14}, 3};            // mov [rdi+0x14], 100
constexpr Imm HOLD_CAP{0x14051eb6b, {0x41, 0x83, 0xf8}, 3};              // cmp r8d, 0x2e
constexpr Imm SECOND_HALF_SPAN{0x14051eb8a, {0xba}, 1};                  // mov edx, 10
constexpr Imm SECOND_HALF_FROM{0x14051eb94, {0x83, 0xc0}, 2};            // add eax, 0x1c

enum Weight { kAttack, kDefend, kBalance };

}  // namespace

std::optional<StrategyConstants> LoadStrategyConstants(const PeImage& exe, std::string* error) {
  StrategyConstants k{};
  int32_t slot = 0, strongAttack = 0, weakAttack = 0, evenAttack = 0, evenBalance = 0;
  int32_t attackFrom = 0, defendFrom = 0, never = 0, span = 0;
  int8_t strongDelta = 0, attackSpan = 0, cap = 0, from = 0;
  if (!exe.ReadVa(STRENGTH_MARGIN_VA, sizeof k.strengthMargin,
                  reinterpret_cast<uint8_t*>(&k.strengthMargin))) {
    *error = "strategy constant: .rdata unreadable";
    return std::nullopt;
  }
  if (!ReadImm(exe, SLOT, &slot, error) || !ReadImm(exe, STRONGER_ATTACK, &strongAttack, error) ||
      !ReadImm(exe, STRONGER_BALANCE_DELTA, &strongDelta, error) ||
      !ReadImm(exe, WEAKER_ATTACK, &weakAttack, error) ||
      !ReadImm(exe, EVEN_ATTACK, &evenAttack, error) ||
      !ReadImm(exe, EVEN_BALANCE, &evenBalance, error) ||
      !ReadImm(exe, ATTACK_HOLD_FROM, &attackFrom, error) ||
      !ReadImm(exe, ATTACK_HOLD_SPAN, &attackSpan, error) ||
      !ReadImm(exe, DEFEND_HOLD_FROM, &defendFrom, error) ||
      !ReadImm(exe, HOLD_NEVER, &never, error) || !ReadImm(exe, HOLD_CAP, &cap, error) ||
      !ReadImm(exe, SECOND_HALF_SPAN, &span, error) ||
      !ReadImm(exe, SECOND_HALF_FROM, &from, error))
    return std::nullopt;
  // The three weight rows {attacking, defensive, balanced} are r15d/r12d/r14d.
  // Where an instruction copies a register instead of loading a literal the
  // row takes that register's value: ebx is zeroed at 0x14051ea40, esi is the
  // slot's 2.
  k.rngSlot = slot;
  k.weightsStronger[kAttack] = strongAttack;
  k.weightsStronger[kDefend] = 0;  // mov r12d, ebx (ebx = 0)
  k.weightsStronger[kBalance] = strongAttack + strongDelta;
  k.weightsWeaker[kAttack] = weakAttack;
  k.weightsWeaker[kDefend] = weakAttack;  // mov r12d, r15d
  k.weightsWeaker[kBalance] = slot;       // mov r14d, esi
  k.weightsEven[kAttack] = evenAttack;
  k.weightsEven[kDefend] = slot;  // mov r12d, esi
  k.weightsEven[kBalance] = evenBalance;
  k.attackHoldFrom = attackFrom;
  k.holdTo = attackFrom + attackSpan;  // both holds share the 46 upper end
  k.defendHoldFrom = defendFrom;
  k.holdNever = never;
  k.secondHalfFrom = from;
  k.secondHalfSpan = span;
  if (k.holdTo != cap) {
    *error = "strategy constant: hold span and its cap disagree";
    return std::nullopt;
  }
  return k;
}

void StrategyStep(const StrategyConstants& k, const StrategyInputs& in, Random& rng,
                  StrategyState* s) {
  const int g = in.ownGoals - in.oppGoals;
  if (in.period == StrategyPeriod::ExtraTime) {
    s->strategy = g < 1 ? kStrategyAttacking : kStrategyDefensive;
    return;
  }
  if (in.period == StrategyPeriod::SecondHalf) {
    if (in.minute >= s->secondHalfMinute) {
      s->strategy = g > 0 ? kStrategyDefensive : kStrategyAttacking;
      return;
    }
    switch (s->strategy) {
      case 0:
        if (g <= 0) s->strategy = kStrategyBalanced;
        return;
      case kStrategyDefensive:
        if (g < 1) s->strategy = kStrategyAttacking;
        return;
      case kStrategyAttacking:
        if (g > 0) s->strategy = kStrategyDefensive;
        return;
      case kStrategyAllOut:
        if (g >= 0) s->strategy = kStrategyBalanced;
        return;
      default:  // balanced, or out of range (forced to balanced first)
        s->strategy = kStrategyBalanced;
        if (g > 1) s->strategy = kStrategyDefensive;
        else if (g < -1) s->strategy = kStrategyAttacking;
        return;
    }
  }
  // First half.
  if (in.minute == 0) {
    const int* w = in.strengthDiff > k.strengthMargin    ? k.weightsStronger
                   : -in.strengthDiff > k.strengthMargin ? k.weightsWeaker
                                                         : k.weightsEven;
    const int r = rng.RangeRaw(k.rngSlot, w[kAttack] + w[kDefend] + w[kBalance]);
    int from = 0, to = 0;
    if (r <= w[kAttack]) {
      s->strategy = kStrategyAttacking;
      from = k.attackHoldFrom, to = k.holdTo;
    } else if (r <= w[kAttack] + w[kDefend]) {
      s->strategy = kStrategyDefensive;
      from = k.defendHoldFrom, to = k.holdTo;
    } else {
      s->strategy = kStrategyBalanced;
    }
    s->holdUntilMinute = to == from ? from : rng.RangeRaw(k.rngSlot, to - from) + from;
    if (s->holdUntilMinute >= k.holdTo) s->holdUntilMinute = k.holdNever;
    s->secondHalfMinute = rng.RangeRaw(k.rngSlot, k.secondHalfSpan) + k.secondHalfFrom;
    return;
  }
  if (in.minute >= s->holdUntilMinute) {
    s->strategy = kStrategyBalanced;
    return;
  }
  // Inside the hold: a defensive pick gives up when behind, an attacking one
  // when ahead.
  if ((s->strategy <= kStrategyDefensive && g < 0) ||
      (s->strategy >= kStrategyAttacking && s->strategy <= kStrategyAllOut && g > 0) ||
      s->strategy < 0 || s->strategy > kStrategyAllOut)
    s->strategy = kStrategyBalanced;
}

int StrategyAttackLevel(int strategy) {
  switch (strategy) {
    case 0:
    case kStrategyDefensive:
    case kStrategyAttacking:
    case kStrategyAllOut:
      return strategy;
    default:
      return kStrategyBalanced;
  }
}

}  // namespace pes
