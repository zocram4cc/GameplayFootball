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

  // The same track read as the montage it is. PES's entrance camerawork changes
  // shot every hundred frames (the cut table in its .fdc), and the export
  // concatenates those cuts with each one keeping its own frame numbering - so
  // the first column is where that cut starts in the demo's timeline. Sampling
  // by row plays the cuts back to back at ten seconds apiece; sampling by
  // timeline cuts when PES cuts, and holds a cut's last frame until the next one
  // starts.
  CamTrackFrame SampleTimeline(float timelineFrame) const;

  // Where the last cut's clip runs out, in timeline frames.
  int GetTimelineFrameCount() const;

private:
  // Row ranges of the concatenated cuts, and where each begins in the timeline.
  struct Cut {
    int timelineStart = 0;
    int firstRow = 0;
    int rowCount = 0;
  };

  std::vector<CamTrackFrame> frames;
  std::vector<Cut> cuts;
};

}  // namespace blunted

#endif
