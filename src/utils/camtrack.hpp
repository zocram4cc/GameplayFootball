// Loader for the .camtrack text format: imported PES cutscene camerawork
// (tools/pes21_import/canm_to_camtrack.py), one 30fps frame per line,
// already in engine space (metres, Z up, vertical FOV degrees):
//   <frame>,<px>,<py>,<pz>,<qx>,<qy>,<qz>,<qw>,<fov>,<near>,<far>

#ifndef _HPP_UTILS_CAMTRACK
#define _HPP_UTILS_CAMTRACK

#include <array>
#include <istream>
#include <vector>

namespace blunted {

struct CamTrackFrame {
  std::array<float, 3> position{};
  std::array<float, 4> rotation{};  // x, y, z, w
  float fov = 35.0f;
  float near = 0.5f;
  float far = 400.0f;
};

// The view axis (local -Z) of a camera rotation quaternion (x, y, z, w).
std::array<float, 3> CamTrackForward(const std::array<float, 4>& rotation);

// Re-aims an authored frame at a live world-space target (the celebrating
// player's head): the goal camtracks are authored against PES's celebration
// staging, but in GF the player is wherever the goal happened, so the frame
// keeps its authored camera position, lens curve and clip planes where they
// are usable and gets:
//   - a roll-free look-at rotation towards the target,
//   - a push straight back along the aim line when the camera is closer than
//     minDistance (prevents camera-inside-player),
//   - the authored FOV widened just enough that subjectHalfHeight metres
//     around the target fit in frame at the actual distance (PES's 0.9-degree
//     super-telephoto shots assume a subject much farther away),
//   - the near plane pulled in so the subject is never near-clipped (some
//     tracks ship a 35m near override tuned for PES's subject distance).
CamTrackFrame RetargetCamTrackFrame(const CamTrackFrame& frame,
                                    const std::array<float, 3>& target,
                                    float minDistance,
                                    float subjectHalfHeight);

class CamTrack {
public:
  bool Load(std::istream& in);

  int GetFrameCount() const { return (int)frames.size(); }
  float GetDurationSeconds() const { return frames.size() / 30.0f; }

  // linear interpolation between frames; clamps outside the range
  CamTrackFrame Sample(float frame) const;

private:
  std::vector<CamTrackFrame> frames;
};

}  // namespace blunted

#endif
