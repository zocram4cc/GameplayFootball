// The 4cc replay wipe: which frame is on screen, and when the cut happens.
//
// The mod ships its replay transition as a movie - 4cc_20_swipe.cpk in PES19's
// download folder - and each wipe is a CRI USM holding two video streams: the
// colour, and a matte. The matte is the point. The picture is the /vg/ Football
// League crest on black, so drawn opaque it blacks the screen out; drawn through its
// own alpha it is a wipe, rising from nothing, holding at full cover while the
// picture underneath is switched, and falling away again.
//
// tools/pes21_import/import_wipe.py merges the two streams into RGBA frames and
// writes the timing beside them:
//
//     fps 60
//     frames 92
//     fadestart 8      <- the frame the switch belongs on, where cover is complete
//
// This is the timing alone. Whether a wipe is playing is the replay page's business
// (src/menu/ingame/replaymenu.cpp).

#ifndef _HPP_ONTHEPITCH_REPLAYWIPE
#define _HPP_ONTHEPITCH_REPLAYWIPE

#include <string>

namespace ReplayWipe {

// What FrameAt returns once there is nothing left to draw.
const int kFinished = -1;

struct Timing {
  float fps = 60.0f;
  int frames = 0;
  // PES's "fadestart": the frame under which the cut is hidden.
  int cutFrame = 0;
  bool valid = false;
};

// A wipe with no frames, or an impossible rate, is not valid - and a build with no
// wipe imported simply never draws one.
Timing Parse(const std::string& text);

// wipe.txt beside the frames.
std::string SidecarPath(const std::string& wipeDir);

// media/.../f_001.png, one-based the way the importer numbers them.
std::string FramePath(const std::string& wipeDir, int frame);

// The frame on screen this far into the wipe, or kFinished.
int FrameAt(const Timing& timing, unsigned long elapsed_ms);

// Whether the cut belongs behind the cover by now. Asked as "is it due" rather than
// "is this the frame" so a dropped frame cannot lose the switch; with no wipe at all
// it is due immediately, because the replay still has to start.
bool CutIsDue(const Timing& timing, unsigned long elapsed_ms);

}  // namespace ReplayWipe

#endif
