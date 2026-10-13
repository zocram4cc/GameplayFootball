#pragma once

// PES's in-play broadcast camera, Dynamic Wide (mode 5), as documented in
// docs/re/pes-re-stadium-camera.md 4.3-4.6 and docs/re/pes-re-camera-target.md
// (CameraTargetRE). Every number comes from the install: the far/near tables
// from cameraInplay.o (constant_match.bin), the slider presets, the smoothing
// constants and the mode-5 constants from PES2021.exe .rdata/.data.

#include <array>
#include <optional>
#include <string>

#include "pes/exe.hpp"
#include "pes/types.hpp"
#include "pes/vfs.hpp"

namespace pes {

// One camera record in code order (FUN_1408a1a70).
struct CameraRecord {
  float mani, fov, posY, lookY, posZ, zMin, zMax, xLimit, pan;
};

struct DynamicWideParams {
  CameraRecord far, near;                // newWideCamera.paramMax / paramMin
  float marginUnder, marginUpper;        // newWideCamera.ballZRangeMargin{Under,Upper}
  std::array<CameraRecord, 4> presets;   // exe records #0..#3: zoom x height bilinear (pan)
  float farFovZoomedOutDeg, farPosYHigh;
  float nearPosYBase, nearPosYHeightSpan, nearFovBaseDeg, nearFovZoomSpanDeg, nearPosZ;
  float zMin, zMax, panBlend;
  // CameraTargetRE constants, all read from the exe.
  float liftBaseM, liftMaxM;             // .rdata 0x1425656c0 / 0x14259222c
  float leadBlend;                       // .rdata 0x14257a194 (0.7)
  float clampXMarginM, clampZMarginM;    // .rdata 0x14256ae1c / 0x14258c264
  float smoothStepUp, smoothCap;         // .rdata 0x1425a3590 / 0x14259d634
  float smoothFloor;                     // the 1.0 clamp at 0x1408a3a39
  float kSpringGain, kSpringStepLimit;   // .rdata 0x1425c4894 / 0x1425b8708 (+-0.005)
  float fpsRef, liftNumerator;           // .data 0x14351ae54 (54), .rdata 0x14259a764 (60)
};

std::optional<DynamicWideParams> LoadDynamicWide(const PeImage& exe, const PesVfs& vfs, std::string* error);

struct CameraShot {
  CameraRecord record;  // blended far -> near
  float panEff;
  float liftBaseM, liftMaxM;      // the target.y ball-height term, from the exe
  float liftTimeScale = 1.0f;        // 60 / FpsRef, the lift's time scale
  float clampXMarginM, clampZMarginM;  // the k-blend's wide bounds, from the exe
};
// zoom/height are the user's camera sliders in [0, 1]; targetZ the smoothed
// target z; fieldHalfWidth FieldInfo +0x08 (to the touchline's outer edge);
// liftTimeScale the lift's 60/FpsRef, read from the exe (item 1). The shot
// carries both forward so FramePose needs no exe access.
CameraShot DynamicWideShot(const DynamicWideParams& p, float zoom, float height, float targetZ, float fieldHalfWidth,
                           float liftTimeScale);
struct CameraPose {
  float tx, ty, tz;  // look-at target, Fox space (Y up, metres)
  float cx, cy, cz;  // camera position, always on the +Z touchline side
  float vfovDeg;
};
// targetLift: the raw focus y, of which the ball-height term
// lift = clamp((targetLift + liftBaseM) * liftTimeScale, 0, liftMaxM) is built
// and added to LookY (report CameraTargetRE item 1); k the smoothed zoom
// scalar. fieldHalfLength: FieldInfo +0x00, for the k-blended x clamp
// (item 4); fieldHalfWidth: FieldInfo +0x08, for the z clamps.
CameraPose FramePose(const CameraShot& shot, float tx, float tz, float targetLift, float k, float fieldHalfLength,
                     float fieldHalfWidth);

// The smoothed zoom scalar k (FUN_1408a2180): per step
// v' = clamp((goal - k)*gain - v, +-limit*k); v += v'; k += v. gain and limit
// are read from the exe (report CameraTargetRE item 3).
struct ZoomSpring {
  ZoomSpring() = default;
  ZoomSpring(float k, float v, float gain, float limit);
  void Step(float goal);

  float k = 1.0f, v = 0.0f, gain_ = 0.02f, limit_ = 0.005f;
};

// PES steps the whole camera update once per rendered frame at FpsRef = 54
// (the .data holder 0x14351ae54, initialised from .rdata 0x14259d624; the value
// is carried in DynamicWideParams::fpsRef): the smoothing weight, the k spring
// and the ball-height lift are per frame. The live sim step was observed at
// 1/48 on the title screen and is unverified in-match
// (docs/re/pes-re-injury-fatigue.md 4b), so it is not used here.

// Target smoothing (report CameraTargetRE 3a, corrected against the raw
// asm 0x1408a3cd1-0x1408a3d41): tgt = (prev + w*raw)/(1 + w) per axis, whose
// fixed point is raw; with w = 1 the camera moves half the remaining distance
// per step. The weight gains smoothStepUp per step while margin time is
// active and decays back to smoothFloor otherwise, and the whole blend is
// skipped once the weight reaches its cap (10000 at .rdata 0x14259d634).
struct TargetSmoothing {
  TargetSmoothing() = default;
  explicit TargetSmoothing(const DynamicWideParams& p);
  void Reset(const pes::Vec3& raw);
  void Step(const pes::Vec3& raw, bool marginTimeActive);
  const pes::Vec3& target() const { return prev; }
  float weight() const { return w; }

 private:
  float w = 1.0f;              // image default, .data 0x1434a00b0
  float stepUp_ = 1.1f, cap_ = 10000.0f, floor_ = 1.0f;  // .rdata {1.1, 10000}, clamp 1
  pes::Vec3 prev{};
};

// Focus lead (report CameraTargetRE item 2): out = w*focus + (1-w)*ball with
// w = leadBlend * |focus-ball|_xyz / |focus-ball|_xz, leadBlend .rdata
// 0x14257a194. The zero-horizontal-distance case has no defined lead, so the
// focus is returned unchanged.
pes::Vec3 LeadFocus(const pes::Vec3& focus, const pes::Vec3& ball, float leadBlend);

}  // namespace pes
