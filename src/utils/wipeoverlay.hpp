// A pre-rendered transition "video with alpha": a numbered sequence of RGBA
// PNG frames played fullscreen over the live scene, used to wipe into and out
// of replays and cutscenes (see docs/PRESENTATION_SPEC.md section 2).
//
// The engine has no video decoder and packs hold simple editable formats, so
// the wipe ships as frames plus a plain-text manifest:
//
//   data/media/cutscenes/wipes/<name>/wipe.txt
//     fps 30
//     frames wipe_%03d.png
//     count 48
//
// Frame files sit next to the manifest.

#ifndef _HPP_UTILS_WIPEOVERLAY
#define _HPP_UTILS_WIPEOVERLAY

#include <istream>
#include <string>

namespace blunted {

struct WipeManifest {
  float fps = 30.0f;
  std::string framePattern;  // printf-style, one %d/%0Nd for the frame number
  int frameCount = 0;
  int firstFrame = 0;

  bool IsValid() const { return frameCount > 0 && !framePattern.empty() && fps > 0.0f; }
  float GetDurationSeconds() const { return frameCount / fps; }
};

// Parses a wipe.txt manifest. Unknown keys are ignored, so manifests can grow.
bool LoadWipeManifest(std::istream& in, WipeManifest& manifest);

// Frame index on screen at `elapsedSeconds`, or -1 once the wipe has finished.
int GetWipeFrameIndex(const WipeManifest& manifest, float elapsedSeconds);

// Builds the file name for a frame index ("wipe_%03d.png" + 7 -> wipe_007.png).
std::string GetWipeFrameFilename(const WipeManifest& manifest, int frameIndex);

}  // namespace blunted

#endif
