#pragma once

// PES 2021's restart timings: how long a whistle stoppage holds, and how far
// after it the restart is prepared and taken. docs/re/pes-re-foul-choreography.md
// 9 ("Stoppage timings and reaction roles"), which is where each address and
// each measured live duration comes from.
//
// PES runs its own situation sequencer (`Flow`, report 1): a stop is
// `3 STOP` -> `5 FOUL` (or `0xb OFFSIDE/OUT-OF-PLAY`, or `6 LATE-CARD`) ->
// `0x45` -> `0x11 WIPE` -> UI windows -> `0x14/0x15 teleport` -> `0xe FADE-IN` ->
// `0x34, 0x19, 0x1a` (restart armed). Every timer in it is seconds x 54 frames
// (9.1), so a number read from the image is the PES value in seconds and this
// engine takes it as milliseconds - its tick is 10 ms.
//
// Every value is read from the user's own PES2021.exe through
// pes::PeImage::ReadVa. Nothing is transcribed.

#include <optional>
#include <string>

#include "pes/exe.hpp"

namespace pes {

struct StoppageConstants {
  // FUN_14044e460's hold table (9.3), a stack array built from two
  // `movaps` reads at 0x14259ce60. Index = the "kind" the caller passes.
  float holdKind1_s = 0.0f;  // table[1] @0x14259ce60 = 5.0: the ordinary foul hold
  float holdKind5_s = 0.0f;  // table[5] @0x14259ce54 = 2.5: every 0xb out-of-play hold
  // 0x14259bde4 (9.3): added to the 0xb hold while a player is down.
  float downedPlayerExtra_s = 0.0f;
  // The offside variant of the 0xb hold: an immediate, `mov dword [rdi+0x868],
  // 0x40400000` @0x140447a25, not a float in .rdata - which is why it cannot be
  // read at runtime and is carried as a named constant with its opcode checked
  // first (LoadOffsideHold_checksOpcode).
  float offsideHold_s = 0.0f;
  // 0x142588200 (9.4): the post-foul wipe (step 0x11/0x10).
  float wipe_s = 0.0f;
  // 0x14256ae1c (9.4): the fade-in (step 0xe).
  float fadeIn_s = 0.0f;
  // 0x142592228 (9.4): the last two steps of the restart tail (0x34 + 0x19).
  float restartTail_s = 0.0f;
  // 0x142565600 (9.4): the 0.5 s fade used by the abandon path and by the
  // downed-player branch of 0xb.
  float fadeHalf_s = 0.0f;
  // 0x14259222c (9.2): the scene start gate opens when the referee is this
  // close to the fouler; EXCITE scenes use 0x142565604 (2.0 m) instead.
  float sceneRefereeRange_m = 0.0f;
  float sceneExciteRange_m = 0.0f;
  // 0x14259be10 squared (9.7): the referee approaches the judged player until
  // within 4 m (d^2 < 16.0) before playing his JUDGE action, no closer.
  float judgeApproachSq_m2 = 0.0f;
  // 9.2: the scene start gate waits `fps*3/env+0x5c` frames before opening
  // (x6 for injury scenes) - the 3x/6x are `lea` address arithmetic
  // (@0x14047ff23/@0x14047ff57), so this is a named constant with PES
  // provenance rather than a runtime read.
  // ponytail: compiled into address arithmetic; no loader can read it. The
  // gate only moves the card scenes this engine does not play, so the
  // multiplier is recorded here and nowhere else uses it yet.
  float sceneGate_s = 0.0f;
};

// Reads the stoppage constants out of the install's PES2021.exe. The offside
// hold is an instruction immediate: its opcode bytes are verified before the
// value is trusted, so a different build fails loudly instead of yielding a
// wrong duration.
std::optional<StoppageConstants> LoadStoppageConstants(const PeImage& exe, std::string* error);

}  // namespace pes
