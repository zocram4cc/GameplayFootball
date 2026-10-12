#pragma once

// PES's in-play broadcast camera, Dynamic Wide (mode 5), as documented in
// docs/re/pes-re-stadium-camera.md 4.3-4.6. Every number comes from the
// install: the far/near tables from cameraInplay.o (constant_match.bin), the
// slider presets and mode-5 constants from PES2021.exe .rdata.

#include <array>
#include <optional>
#include <string>

#include "pes/exe.hpp"
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
};

std::optional<DynamicWideParams> LoadDynamicWide(const PeImage& exe, const PesVfs& vfs, std::string* error);

struct CameraShot {
  CameraRecord record;  // blended far -> near
  float panEff;
};
// zoom/height are the user's camera sliders in [0, 1]; targetZ the smoothed
// target z; fieldHalfWidth FieldInfo +0x08 (to the touchline's outer edge).
CameraShot DynamicWideShot(const DynamicWideParams& p, float zoom, float height, float targetZ, float fieldHalfWidth);

struct CameraPose {
  float tx, ty, tz;  // look-at target, Fox space (Y up, metres)
  float cx, cy, cz;  // camera position, always on the +Z touchline side
  float vfovDeg;
};
// targetLift: the ball-height term added to LookY (clamped to [0, 5]).
CameraPose FramePose(const CameraShot& shot, float tx, float tz, float targetLift, float k);

// The smoothed zoom scalar k (FUN_1408a2180): per step
// v' = clamp((goal - k)*0.02 - v, +-0.005k); v += v'; k += v.
struct ZoomSpring {
  float k, v;
  void Step(float goal);
};

}  // namespace pes
