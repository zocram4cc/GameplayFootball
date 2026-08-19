#include "replaywipe.hpp"

#include <cstdio>
#include <sstream>

namespace ReplayWipe {

Timing Parse(const std::string& text) {
  Timing timing;
  std::istringstream lines(text);
  std::string line;
  bool haveFrames = false;
  bool haveCover = false;
  bool haveCut = false;
  while (std::getline(lines, line)) {
    std::istringstream fields(line);
    std::string key;
    if (!(fields >> key) || key.empty() || key[0] == '#') continue;
    if (key == "fps") {
      float fps = 0.0f;
      if (fields >> fps) timing.fps = fps;
    } else if (key == "frames") {
      int frames = 0;
      if (fields >> frames && frames > 0) {
        timing.frames = frames;
        haveFrames = true;
      }
    } else if (key == "fadestart") {
      int cut = 0;
      // Only if nothing better arrives: PES's fadestart still has gaps in the matte.
      if (fields >> cut && !haveCover) timing.cutFrame = cut < 0 ? 0 : cut;
    } else if (key == "cover") {
      int cover = 0;
      if (fields >> cover && !haveCut) {
        timing.cutFrame = cover < 0 ? 0 : cover;
        haveCover = true;
      }
    } else if (key == "cut") {
      int cut = 0;
      if (fields >> cut) {
        timing.cutFrame = cut < 0 ? 0 : cut;
        haveCover = haveCut = true;
      }
    }
  }
  timing.valid = haveFrames && timing.fps > 0.0f;
  if (timing.valid && timing.cutFrame > timing.frames - 1) timing.cutFrame = timing.frames - 1;
  return timing;
}

std::string SidecarPath(const std::string& wipeDir) {
  if (wipeDir.empty()) return "";
  const bool slashed = wipeDir[wipeDir.size() - 1] == '/' || wipeDir[wipeDir.size() - 1] == '\\';
  return wipeDir + (slashed ? "" : "/") + "wipe.txt";
}

std::string FramePath(const std::string& wipeDir, int frame) {
  char name[32];
  std::snprintf(name, sizeof(name), "f_%03d.png", frame + 1);
  const bool slashed = !wipeDir.empty() &&
                       (wipeDir[wipeDir.size() - 1] == '/' || wipeDir[wipeDir.size() - 1] == '\\');
  return wipeDir + (slashed ? "" : "/") + name;
}

int FrameAt(const Timing& timing, unsigned long elapsed_ms) {
  if (!timing.valid) return kFinished;
  const int frame = static_cast<int>(elapsed_ms * timing.fps / 1000.0f);
  if (frame >= timing.frames) return kFinished;
  return frame;
}

bool CutIsDue(const Timing& timing, unsigned long elapsed_ms) {
  if (!timing.valid) return true;
  return static_cast<int>(elapsed_ms * timing.fps / 1000.0f) >= timing.cutFrame;
}

}  // namespace ReplayWipe
