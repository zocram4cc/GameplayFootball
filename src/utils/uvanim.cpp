#include "utils/uvanim.hpp"

#include <cmath>
#include <sstream>

namespace blunted {

float UvAnimTilesUsed(const UvAnimParams& p) {
  // Tiles_Used, or the full grid when it is 0 (mirrors the fmdl convention).
  const float tcu = p.tileU != 0.0f ? p.tileU : 1.0f;
  const float tcv = p.tileV != 0.0f ? p.tileV : 1.0f;
  return p.tilesUsed != 0.0f ? p.tilesUsed : tcu * tcv;
}

void UvAnimBaseScale(const UvAnimParams& p, float& su, float& sv) {
  if (p.scaleUvToTiles != 0.0f) {
    // _apply_uvstep divides the base UV by the grid: uvs authored in
    // per-tile space land back in the full texture.
    su = 1.0f / (p.tileU != 0.0f ? p.tileU : 1.0f);
    sv = 1.0f / (p.tileV != 0.0f ? p.tileV : 1.0f);
  } else {
    su = 1.0f;
    sv = 1.0f;
  }
}

UvAnimParams UvAnimParseAseLine(const std::string& line) {
  UvAnimParams p;
  std::istringstream in(line);
  std::string family;
  if (!(in >> family))
    return p;  // empty: a static material
  bool scroll = (family == "uvscroll");
  bool step = (family == "uvstep");
  if (!scroll && !step)
    return p;  // unknown: stays static rather than animating wrong
  p.family = scroll ? UvAnimParams::Family::Scroll
                    : UvAnimParams::Family::Step;
  // The line carries the importer's values in order; a truncated line is a
  // static material, not a half-animated one.
  float values[8] = {0};
  const int want = scroll ? 3 : 8;
  for (int i = 0; i < want; i++)
    if (!(in >> values[i]))
      return UvAnimParams();
  if (scroll) {
    p.speedU = values[0];
    p.speedV = values[1];
    p.offsetS = values[2];
  } else {
    p.tileU = values[0];
    p.tileV = values[1];
    p.tilesUsed = values[2];
    p.scaleUvToTiles = values[3];
    p.secCycle = values[4];
    p.useTiming = (values[5] != 0.0f);
    p.secTimingU = values[6];
    p.secTimingV = values[7];
  }
  return p;
}

void UvAnimScrollShift(const UvAnimParams& p, float timeS,
                       float& du, float& dv) {
  // A pure base-UV translation: (speedU, speedV) per second, plus an authoring
  // offset. Continuous - float32 matches the GPU in the last ULP only.
  const float t = timeS + p.offsetS;
  du = p.speedU * t;
  dv = p.speedV * t;
}

void UvAnimStepShift(const UvAnimParams& p, float timeS,
                     float& du, float& dv) {
  // Mirrors vertex-shader.asm lines 195-243 (and the addon's float32 port).
  // The division and the ratio-floor run in float32 so the hardware's
  // round-to-nearest-even lands the tile boundary on the integer the user
  // authored (2.4 stored as 2.4000000953674316 -> ratio exactly 1.0 at t=2.4).
  const float tcu = p.tileU != 0.0f ? p.tileU : 1.0f;
  const float tcv = p.tileV != 0.0f ? p.tileV : 1.0f;
  const float used = UvAnimTilesUsed(p);

  float offset;
  if (p.secCycle != 0.0f) {
    const float ratio = timeS / p.secCycle;  // float32 division
    offset = ratio - static_cast<float>(std::floor(ratio));
  } else {
    offset = 0.0f;
  }
  const int tile = static_cast<int>(std::floor(offset * used));
  const float tv = static_cast<float>(std::floor(static_cast<float>(tile) / tcu));
  const float tu = static_cast<float>(tile) - tv * tcu;
  du = tu / tcu;
  dv = tv / tcv;
}

void UvAnimStepTimingSweep(const UvAnimParams& p, float timeS,
                           float& duTime, float& dvTime) {
  // The per-material timing sweep, in UV cycles (0..1 = one full sweep). 0
  // cycle means no motion. These become the uvanim_dTime/dTimeV uniforms.
  duTime = p.secTimingU != 0.0f ? timeS / p.secTimingU : 0.0f;
  dvTime = p.secTimingV != 0.0f ? timeS / p.secTimingV : 0.0f;
}

int UvAnimTimingTile(float red, float tilesUsed) {
  // Line 188 of vertex-shader.asm: animationOffset = R * 255/256, then floor
  // (offset * tilesUsed). The 255/256 keeps R=1.0 inside the last tile rather
  // than stepping onto a phantom tile.
  const float offset = red * (255.0f / 256.0f);
  return static_cast<int>(std::floor(offset * tilesUsed));
}

void UvAnimTimingUv(float timingU, float timingV, float duTime, float dvTime,
                    float& tu, float& tv) {
  // The timing sample UV after the sweep, wrapped to [0,1). In the engine this
  // runs in the fragment shader (GL 1.50 core has no vertex texture fetch); the
  // C++ copy is what the test pins and what a CPU fallback would use. PES V
  // convention: both components are added (the engine uploads textures V=0 at
  // the top, matching the fmdl's V).
  tu = timingU + duTime;
  tu -= std::floor(tu);
  tv = timingV + dvTime;
  tv -= std::floor(tv);
}

}  // namespace blunted
