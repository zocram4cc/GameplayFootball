// Pure float32-exact math for PES vertex-shader UV animation (the "uvscroll"
// and "uvstep" material families, e.g. the k2017 LED boots). The engine's GL
// 1.50 core has no vertex texture fetch, so this is the CPU side of the shader:
// the renderer calls these per material each frame to build the uvanim_*
// uniforms, and simple.vert/simple.frag mirror the per-vertex/per-fragment
// parts (timing-V is the third texcoord channel). Every op is float32 and in
#ifndef _HPP_UTILS_UVANIM
#define _HPP_UTILS_UVANIM

#include <string>
#include <utility>

namespace blunted {

struct UvAnimParams {
  enum class Family : int { None = 0, Scroll = 1, Step = 2 };
  Family family = Family::None;
  // uvscroll: a base-UV translation (per-second), plus an authoring offset.
  float speedU = 0.0f, speedV = 0.0f, offsetS = 0.0f;
  // uvstep: the tile grid and how long one full grid cycle takes. The base
  // UV is divided by (tileU, tileV) only when scaleUvToTiles is set -
  // Scale_UVs_To_Tiles, 1.0 in every shipped pack.
  float tileU = 1.0f, tileV = 1.0f;     // Tile_Count_U / Tile_Count_V
  float tilesUsed = 0.0f;               // Tiles_Used; 0 means tileU*tileV
  float scaleUvToTiles = 0.0f;          // Scale_UVs_To_Tiles; 0 = full-texture UVs
  float secCycle = 0.0f;                // Seconds_Per_Animation_Cycle; 0 = no cycle
  float secTimingU = 0.0f, secTimingV = 0.0f;  // timing sweep seconds; 0 = off
  bool useTiming = false;               // Use_Timing_Texture
};

// The resolved tile count (Tiles_Used, or tileU*tileV when 0).
float UvAnimTilesUsed(const UvAnimParams& p);

// uvscroll: the base-UV translation at time t seconds. (du, dv) in PES UV.
void UvAnimScrollShift(const UvAnimParams& p, float timeS,
                       float& du, float& dv);

// uvstep, no timing texture: the uniform base-UV tile offset at time t,
// normalized so the albedo samples (tu/tileU, tv/tileV) of the grid.
void UvAnimStepShift(const UvAnimParams& p, float timeS,
                     float& du, float& dv);

// uvstep timing sweep offset at time t (seconds -> UV cycles), per material.
// These are the shader's per-material uniforms; the per-vertex part (timingV +
// this) is computed in the fragment shader from the third texcoord channel.
void UvAnimStepTimingSweep(const UvAnimParams& p, float timeS,
                           float& duTime, float& dvTime);

// uvstep, per-fragment: which grid tile the timing texture's R channel selects
// from the [0,1] red value (mirrors _compute_animation_offset_from_timing).
int UvAnimTimingTile(float red, float tilesUsed);

// uvstep, per-fragment: the timing sample UV after the sweep, wrapped to [0,1).
void UvAnimTimingUv(float timingU, float timingV, float duTime, float dvTime,
                    float& tu, float& tv);

// The base-UV scale into per-tile space: (1/tileU, 1/tileV) when
// Scale_UVs_To_Tiles is set, (1, 1) when it is 0 (the addon's _apply_uvstep
// divides the base UV by the grid exactly in that case).
void UvAnimBaseScale(const UvAnimParams& p, float& su, float& sv);

// The ASE wire form: "uvstep <8 floats>" / "uvscroll <3 floats>", in the
// order the importer's *MATERIAL_UVANIM line carries them. "" (or anything
// unparseable) is a static material.
UvAnimParams UvAnimParseAseLine(const std::string& line);

}  // namespace blunted

#endif
