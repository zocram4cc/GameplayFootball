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
