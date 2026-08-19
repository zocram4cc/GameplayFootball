// The 4cc replay wipe: which frame is on screen, and when the cut happens.
//
// The mod ships its transition as a movie (4cc_20_swipe.cpk), and
// tools/pes21_import/import_wipe.py turns it into RGBA frames plus a sidecar:
//
//     fps 60
//     frames 92
//     fadestart 8
//
// The alpha is PES's own second video stream, which is what makes it a wipe rather
// than a black frame: it rises from nothing, holds at full cover while the picture
// underneath is switched, and falls away again. fadestart is the frame on which that
// switch belongs - where the cover is complete and nothing of either shot shows
// through.
//
// This is the timing alone. Whether a wipe is playing at all is the replay page's
// business (src/menu/ingame/replaymenu.cpp).

#include <gtest/gtest.h>

#include "onthepitch/replaywipe.hpp"

namespace {
const char* kSidecar =
    "# a comment\nfps 60\nframes 92\nfadestart 8\ncover 9\ncut 15\n";
}  // namespace

TEST(ReplayWipe, ASidecarIsRead) {
  const ReplayWipe::Timing timing = ReplayWipe::Parse(kSidecar);
  EXPECT_TRUE(timing.valid);
  EXPECT_FLOAT_EQ(timing.fps, 60.0f);
  EXPECT_EQ(timing.frames, 92);
  EXPECT_EQ(timing.cutFrame, 15);
}

// PES's fadestart is not the frame a cut can hide behind: at frame 6 the matte still
// has pixels at zero, so the cut shows through the gaps. The importer measures where
// it actually covers and writes that as "cover"; fadestart is kept for reference and
// used only by a sidecar too old to carry one.
TEST(ReplayWipe, TheCutWaitsForRealCoverRatherThanPesFadestart) {
  EXPECT_EQ(ReplayWipe::Parse("fps 60\nframes 92\nfadestart 6\ncover 9\n").cutFrame, 9);
}

// And "cut" beats both: covered is not the same as safely covered, so the importer
// holds it a quarter of a second into the hold.
TEST(ReplayWipe, TheHeldCutFrameWinsOverBoth) {
  EXPECT_EQ(ReplayWipe::Parse("fps 60\nframes 92\nfadestart 6\ncover 9\ncut 15\n").cutFrame,
            15);
}

TEST(ReplayWipe, WithoutACoverFrameFadestartIsAllThereIs) {
  EXPECT_EQ(ReplayWipe::Parse("fps 60\nframes 92\nfadestart 6\n").cutFrame, 6);
}

TEST(ReplayWipe, NothingIsNotAWipe) {
  EXPECT_FALSE(ReplayWipe::Parse("").valid);
  EXPECT_FALSE(ReplayWipe::Parse("fps 60\n").valid);          // no frames
  EXPECT_FALSE(ReplayWipe::Parse("frames 0\nfps 60\n").valid);
}

TEST(ReplayWipe, ARubbishFpsIsRefusedRatherThanDividedBy) {
  EXPECT_FALSE(ReplayWipe::Parse("fps 0\nframes 92\n").valid);
  EXPECT_FALSE(ReplayWipe::Parse("fps -30\nframes 92\n").valid);
}

TEST(ReplayWipe, TheFirstFrameIsOnScreenAtOnce) {
  const ReplayWipe::Timing timing = ReplayWipe::Parse(kSidecar);
  EXPECT_EQ(ReplayWipe::FrameAt(timing, 0), 0);
}

TEST(ReplayWipe, ItRunsAtTheRateItWasAuthoredAt) {
  const ReplayWipe::Timing timing = ReplayWipe::Parse(kSidecar);
  // 60 fps: a frame every 16.67 ms
  EXPECT_EQ(ReplayWipe::FrameAt(timing, 17), 1);
  EXPECT_EQ(ReplayWipe::FrameAt(timing, 500), 30);
}

TEST(ReplayWipe, PastTheEndItIsOver) {
  const ReplayWipe::Timing timing = ReplayWipe::Parse(kSidecar);
  // 92 frames at 60 fps run out at 1533.3 ms, so 1533 is still the last of them
  EXPECT_EQ(ReplayWipe::FrameAt(timing, 1533), 91);
  EXPECT_EQ(ReplayWipe::FrameAt(timing, 1534), ReplayWipe::kFinished);
  EXPECT_EQ(ReplayWipe::FrameAt(timing, 99999), ReplayWipe::kFinished);
}

TEST(ReplayWipe, AnInvalidTimingIsAlwaysOver) {
  // so a build with no wipe imported simply never draws one
  EXPECT_EQ(ReplayWipe::FrameAt(ReplayWipe::Parse(""), 0), ReplayWipe::kFinished);
}

// The cut goes under full cover. Asking "has it happened yet" rather than "is this
// the frame" means a dropped frame cannot lose the switch entirely.
TEST(ReplayWipe, TheCutHasNotHappenedBeforeItsFrame) {
  const ReplayWipe::Timing timing = ReplayWipe::Parse(kSidecar);
  EXPECT_FALSE(ReplayWipe::CutIsDue(timing, 0));
  EXPECT_FALSE(ReplayWipe::CutIsDue(timing, 234));   // frame 14, covered but not yet held
}

TEST(ReplayWipe, TheCutIsDueFromItsFrameOnward) {
  const ReplayWipe::Timing timing = ReplayWipe::Parse(kSidecar);
  EXPECT_TRUE(ReplayWipe::CutIsDue(timing, 250));    // frame 15
  EXPECT_TRUE(ReplayWipe::CutIsDue(timing, 800));
  EXPECT_TRUE(ReplayWipe::CutIsDue(timing, 99999));  // even once it is over
}

TEST(ReplayWipe, WithoutAWipeTheCutIsDueImmediately) {
  // no transition to hide it behind, so the replay must still start
  EXPECT_TRUE(ReplayWipe::CutIsDue(ReplayWipe::Parse(""), 0));
}

TEST(ReplayWipe, AFrameHasAPath) {
  EXPECT_EQ(ReplayWipe::FramePath("media/textures/wipe/acl", 0),
            "media/textures/wipe/acl/f_001.png");
  EXPECT_EQ(ReplayWipe::FramePath("media/textures/wipe/acl", 91),
            "media/textures/wipe/acl/f_092.png");
}

TEST(ReplayWipe, ATrailingSlashDoesNotDoubleUp) {
  EXPECT_EQ(ReplayWipe::FramePath("media/textures/wipe/acl/", 0),
            "media/textures/wipe/acl/f_001.png");
}

TEST(ReplayWipe, TheSidecarSitsInTheWipesOwnDirectory) {
  EXPECT_EQ(ReplayWipe::SidecarPath("media/textures/wipe/acl"),
            "media/textures/wipe/acl/wipe.txt");
}
