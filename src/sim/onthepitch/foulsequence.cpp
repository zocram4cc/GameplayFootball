#include "foulsequence.hpp"

#include <cmath>

namespace FoulSequence {

namespace {

unsigned long ToMs(float seconds) {
  return static_cast<unsigned long>(std::lround(seconds * 1000.0f));
}

// The whole bracket: hold, plus PES's fixed beats between the hold and the
// kick. `hold_s` is table[1] for a foul or table[5] for an out-of-play stop.
unsigned long Tail_ms() { return kStopStamp_ms + kUiFrames_ms + kTeleport_ms; }

}  // namespace

unsigned long HoldEndsAt_ms(unsigned long stopTime_ms, const pes::StoppageConstants& sf,
                           bool downed, int holdKind) {
  // 9.3/9.2: the out-of-play hold is table[5], the foul hold table[1]; a
  // player down adds PES's +2.5 s (0x14259bde4) to either.
  const float hold_s =
      (holdKind == 1 ? sf.holdKind1_s : sf.holdKind5_s) + (downed ? sf.downedPlayerExtra_s : 0.0f);
  return stopTime_ms + ToMs(hold_s);
}

unsigned long RestartPrepareAt_ms(unsigned long stopTime_ms, const pes::StoppageConstants& sf,
                                 bool downed, int holdKind) {
  // The wipe (9.4, 0x142588200 = 0.1 s) is the last thing before the referee
  // moves the cast back to their marks, which is this engine's prepare.
  return HoldEndsAt_ms(stopTime_ms, sf, downed, holdKind) + ToMs(sf.wipe_s);
}

unsigned long RestartTakeAt_ms(unsigned long stopTime_ms, const pes::StoppageConstants& sf,
                              bool downed, int holdKind) {
  // The teleport, the fade-in and the tail (9.6). This engine's match clock
  // keeps running through all of them, so the kick lands after the whole of
  // them - 7.54 s after the whistle for an ordinary foul.
  return RestartPrepareAt_ms(stopTime_ms, sf, downed, holdKind) + Tail_ms() + ToMs(sf.fadeIn_s) +
         ToMs(sf.restartTail_s);
}

bool ScheduleIsConsistent() {
  return kStopStamp_ms < kUiFrames_ms;
}

}  // namespace FoulSequence
