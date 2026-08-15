#include "wipeoverlay.hpp"

#include <cstdio>
#include <sstream>
#include <string>
#include <vector>

namespace blunted {

bool LoadWipeManifest(std::istream& in, WipeManifest& manifest) {
  std::string line;
  while (std::getline(in, line)) {
    if (line.empty() || line[0] == '#') continue;
    std::istringstream fields(line);
    std::string key;
    fields >> key;
    if (key == "fps") {
      fields >> manifest.fps;
    } else if (key == "frames") {
      fields >> manifest.framePattern;
    } else if (key == "count") {
      fields >> manifest.frameCount;
    } else if (key == "first") {
      fields >> manifest.firstFrame;
    }
    // anything else is a comment as far as the engine is concerned
  }
  return manifest.IsValid();
}

int GetWipeFrameIndex(const WipeManifest& manifest, float elapsedSeconds) {
  if (!manifest.IsValid() || elapsedSeconds < 0.0f) return -1;
  const int index = static_cast<int>(elapsedSeconds * manifest.fps);
  return index < manifest.frameCount ? index : -1;
}

std::string GetWipeFrameFilename(const WipeManifest& manifest, int frameIndex) {
  std::vector<char> name(manifest.framePattern.size() + 32);
  const int written = snprintf(name.data(), name.size(), manifest.framePattern.c_str(),
                               manifest.firstFrame + frameIndex);
  if (written <= 0) return std::string();
  return std::string(name.data());
}

}  // namespace blunted
