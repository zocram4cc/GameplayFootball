// written by bastiaan konings schuiling 2008 - 2014
// this work is public domain. the code is undocumented, scruffy, untested, and should generally not
// be used for anything important. i do not offer support, so don't ask. to be used for inspiration
// :)

#ifndef _hpp_bluntmath
#define _hpp_bluntmath

#include <limits>

#include "defines.hpp"

// todo: function names should probably start with caps

namespace blunted {

real clamp(const real value, const real min, const real max);
real NormalizedClamp(const real value, const real min, const real max);
real invsqrt(real fvalue);

// you can never be too specific ;)
const real pi = 3.1415926535897932384626433832795028841972f;  // last decimal rounded ;)

using radian = real;

void normalize(real v[3]);
bool sign(real n);
signed int signSide(real n);  // returns -1 or 1
bool is_odd(int n);
// Every draw comes from PES 2021's generator (onthepitch/pesrandom.hpp), which
// the Match seeds at kick-off from its seed; never from the clock. The Match
// installs the source for its lifetime. `stream` selects a generator slot.
struct RandomSource {
  virtual ~RandomSource() = default;
  // A uniform draw in [0, 1) from `stream`.
  virtual float Unit(int stream) = 0;
};
void SetRandomSource(RandomSource* source);
RandomSource* GetRandomSource();
// GF's two generators, kept as two streams of PES's.
inline constexpr int kRandomStream = 0;
inline constexpr int kFastRandomStream = 1;
real random(real min, real max);
real fastrandom(real min, real max);

inline float curve(float source, float bias = 1.0f) {  // make linear / into sined _/-
  return (sin((source - 0.5f) * pi) * 0.5f + 0.5f) * bias + source * (1.0f - bias);
}

int pot(int x);
real ModulateIntoRange(real min, real max, real value);

}  // namespace blunted

#endif
