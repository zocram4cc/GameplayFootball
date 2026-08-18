// Pre-match presentation timeline (docs/PRESENTATION_SPEC.md section 1):
// parsing a competition's beat list, and resolving where the sequence is at a
// given moment. Pure logic - see src/onthepitch/prematchtimeline.hpp.

#include <gtest/gtest.h>

#include <sstream>
#include <string>

#include "onthepitch/prematchtimeline.hpp"

using PrematchTimeline::At;
using PrematchTimeline::Beat;
using PrematchTimeline::Camera;
using PrematchTimeline::Default;
using PrematchTimeline::Overlay;
using PrematchTimeline::Parse;
using PrematchTimeline::Rescale;
using PrematchTimeline::Timeline;

namespace {

Timeline ParseText(const std::string& text) {
  std::istringstream in(text);
  Timeline timeline;
  Parse(in, timeline);
  return timeline;
}

// A three-beat stand-in for a competition file.
Timeline Sample() {
  return ParseText(
      "# a competition's pre-match\n"
      "beat stadium 4 camera=orbit\n"
      "\n"
      "beat walkout 10 camera=entrance\n"
      "beat lineup_home 6 camera=hold overlay=formation_home\n");
}

}  // namespace

// --- Parsing ---

TEST(PrematchTimelineParseTest, ReadsBeatsInOrderWithTheirDurations) {
  const Timeline timeline = Sample();
  ASSERT_EQ(timeline.beats.size(), 3u);
  EXPECT_EQ(timeline.beats[0].name, "stadium");
  EXPECT_NEAR(timeline.beats[0].seconds, 4.0f, 0.001f);
  EXPECT_EQ(timeline.beats[1].name, "walkout");
  EXPECT_EQ(timeline.beats[2].name, "lineup_home");
  EXPECT_NEAR(timeline.TotalSeconds(), 20.0f, 0.001f);
}

TEST(PrematchTimelineParseTest, ReadsCameraAndOverlayKeys) {
  const Timeline timeline = Sample();
  EXPECT_EQ(timeline.beats[0].camera, Camera::Orbit);
  EXPECT_EQ(timeline.beats[1].camera, Camera::Entrance);
  EXPECT_EQ(timeline.beats[2].camera, Camera::Hold);
  EXPECT_EQ(timeline.beats[0].overlay, Overlay::None);
  EXPECT_EQ(timeline.beats[2].overlay, Overlay::FormationHome);
}

TEST(PrematchTimelineParseTest, CommentsAndBlankLinesAreSkipped) {
  const Timeline timeline = ParseText(
      "\n"
      "# nothing here\n"
      "   # nor here\n"
      "beat only 3\n");
  ASSERT_EQ(timeline.beats.size(), 1u);
  EXPECT_EQ(timeline.beats[0].name, "only");
}

TEST(PrematchTimelineParseTest, UnknownKeysAndValuesLeaveTheDefaults) {
  // A competition file may carry keys a later version understands; reading it
  // with an older build must not throw the beat away.
  const Timeline timeline = ParseText("beat odd 5 camera=telescope sponsor=acme overlay=hologram\n");
  ASSERT_EQ(timeline.beats.size(), 1u);
  EXPECT_EQ(timeline.beats[0].camera, Camera::Entrance);
  EXPECT_EQ(timeline.beats[0].overlay, Overlay::None);
  EXPECT_NEAR(timeline.beats[0].seconds, 5.0f, 0.001f);
}

TEST(PrematchTimelineParseTest, MalformedLinesAreDroppedNotFatal) {
  const Timeline timeline = ParseText(
      "beat good 4\n"
      "beat\n"                 // no name, no duration
      "beat nameonly\n"        // no duration
      "beat zero 0\n"          // a beat that holds for no time is not a beat
      "beat negative -3\n"
      "nonsense here\n"
      "beat alsogood 2\n");
  ASSERT_EQ(timeline.beats.size(), 2u);
  EXPECT_EQ(timeline.beats[0].name, "good");
  EXPECT_EQ(timeline.beats[1].name, "alsogood");
}

TEST(PrematchTimelineParseTest, AnEmptyFileParsesToNothingAndSaysSo) {
  std::istringstream in("# only a comment\n");
  Timeline timeline;
  EXPECT_FALSE(Parse(in, timeline));
  EXPECT_TRUE(timeline.beats.empty());
}

// --- The built-in default ---

TEST(PrematchTimelineDefaultTest, CoversTheWholeShotListIncludingBothLineups) {
  const Timeline timeline = Default();
  EXPECT_GE(timeline.beats.size(), 6u);

  bool home = false, away = false;
  for (const auto& beat : timeline.beats) {
    if (beat.overlay == Overlay::FormationHome) home = true;
    if (beat.overlay == Overlay::FormationAway) away = true;
  }
  EXPECT_TRUE(home);
  EXPECT_TRUE(away);
}

TEST(PrematchTimelineDefaultTest, RunsLongEnoughToReadAsABroadcastOpening) {
  // The reference pre-match runs about two minutes from the stadium reveal to
  // kickoff; the complaint about the old entrance was that it was over in
  // seconds.
  EXPECT_GT(Default().TotalSeconds(), 60.0f);
}

TEST(PrematchTimelineDefaultTest, FollowsTheReferenceRunningOrder) {
  // VGL 26 Day 3 from 14:35: a stadium establishing wide under the
  // competition card, the walk-on from outside the touchline, a hold on the
  // line, the anthems, the wides the lineup graphics sit over, the team
  // picture, and away to kickoff.
  const Timeline timeline = Default();
  ASSERT_GE(timeline.beats.size(), 8u);

  auto indexOf = [&](const std::string& name) {
    for (size_t i = 0; i < timeline.beats.size(); i++)
      if (timeline.beats[i].name == name) return (int)i;
    return -1;
  };
  const int card = indexOf("stadium_card");
  const int walkOn = indexOf("walk_on");
  // The hold on the line is the anthem beat: it has the pack that stands the
  // squads in a row. Giving it a beat of its own on the walk-on pack restarted
  // that pack, which walked the whole column in a second time.
  const int line = indexOf("anthems");
  const int wideHome = indexOf("wide_home");
  const int picture = indexOf("team_picture");

  ASSERT_GE(card, 0);
  ASSERT_GE(walkOn, 0);
  ASSERT_GE(line, 0);
  ASSERT_GE(wideHome, 0);
  ASSERT_GE(picture, 0);

  EXPECT_EQ(card, 0);
  EXPECT_LT(walkOn, line);
  EXPECT_LT(line, wideHome);
  EXPECT_LT(wideHome, picture);

  // The reference holds on the line far longer than on any establishing
  // shot; that hold is the heart of the sequence.
  EXPECT_GT(timeline.beats[line].seconds, timeline.beats[card].seconds);
}

TEST(PrematchTimelineDefaultTest, EndsOnTheCameraTheMatchItselfStartsOn) {
  // Kickoff should arrive as a continuation, not a cut. The last beat used to
  // be PES's own "aerial" entrance shot, which flies the camera outside the
  // ground looking down, so the first whistle teleported the viewer back to the
  // broadcast angle. Ending on the live camera makes the handover invisible.
  const Timeline timeline = Default();
  ASSERT_FALSE(timeline.beats.empty());
  const Beat& last = timeline.beats.back();
  EXPECT_EQ(last.camera, Camera::Aerial);
  EXPECT_TRUE(last.shot.empty());
}

TEST(PrematchTimelineDefaultTest, StagedBeatsAreFilmedByPessOwnCamerawork) {
  // PES's framing and focal lengths are the thing being imported, so a beat that
  // stages PES's choreography is filmed by PES's camera for it. The tracks are
  // authored in the same coordinates as the choreography, so Match moves both by
  // the same offset to put them on our pitch; computing a camera off the cast
  // instead - which this default used to do - looked nothing like the broadcast.
  for (const Beat& beat : Default().beats) {
    if (beat.shot.empty()) continue;
    EXPECT_EQ(beat.camera, Camera::Entrance) << "beat " << beat.name;
  }
}

TEST(PrematchTimelineParseTest, ReadsTheCastFramingCameras) {
  const Timeline timeline = ParseText(
      "beat walkout 8 camera=walkout\n"
      "beat anthems 6 camera=lineup\n");
  ASSERT_EQ(timeline.beats.size(), 2u);
  EXPECT_EQ(timeline.beats[0].camera, Camera::Walkout);
  EXPECT_EQ(timeline.beats[1].camera, Camera::Lineup);
}

TEST(PrematchTimelineDefaultTest, ShowsTheHomeSideBeforeTheAwaySide) {
  const Timeline timeline = Default();
  int homeAt = -1, awayAt = -1;
  for (size_t i = 0; i < timeline.beats.size(); i++) {
    if (timeline.beats[i].overlay == Overlay::FormationHome && homeAt < 0) homeAt = (int)i;
    if (timeline.beats[i].overlay == Overlay::FormationAway && awayAt < 0) awayAt = (int)i;
  }
  ASSERT_GE(homeAt, 0);
  ASSERT_GE(awayAt, 0);
  EXPECT_LT(homeAt, awayAt);
}

// --- Resolving a moment ---

TEST(PrematchTimelineAtTest, PicksTheBeatThatIsOnAir) {
  const Timeline timeline = Sample();  // 4 + 10 + 6
  EXPECT_EQ(At(timeline, 0.0f).beatIndex, 0);
  EXPECT_EQ(At(timeline, 3.9f).beatIndex, 0);
  EXPECT_EQ(At(timeline, 4.1f).beatIndex, 1);
  EXPECT_EQ(At(timeline, 13.9f).beatIndex, 1);
  EXPECT_EQ(At(timeline, 14.1f).beatIndex, 2);
}

TEST(PrematchTimelineAtTest, ReportsHowFarThroughABeatItIs) {
  const Timeline timeline = Sample();
  EXPECT_NEAR(At(timeline, 2.0f).beatT, 0.5f, 0.01f);
  EXPECT_NEAR(At(timeline, 9.0f).beatT, 0.5f, 0.01f);  // 5s into the 10s beat
}

TEST(PrematchTimelineAtTest, CarriesTheBeatsCameraAndOverlay) {
  const Timeline timeline = Sample();
  EXPECT_EQ(At(timeline, 2.0f).camera, Camera::Orbit);
  EXPECT_EQ(At(timeline, 9.0f).camera, Camera::Entrance);
  const auto lineup = At(timeline, 17.0f);
  EXPECT_EQ(lineup.camera, Camera::Hold);
  EXPECT_EQ(lineup.overlay, Overlay::FormationHome);
}

TEST(PrematchTimelineAtTest, PastTheEndIsFinished) {
  const Timeline timeline = Sample();
  const auto state = At(timeline, 21.0f);
  EXPECT_TRUE(state.finished);
  EXPECT_EQ(state.overlay, Overlay::None);
  EXPECT_NEAR(state.overlayAlpha, 0.0f, 0.001f);
}

TEST(PrematchTimelineAtTest, AnEmptyTimelineIsImmediatelyFinished) {
  Timeline empty;
  EXPECT_TRUE(At(empty, 0.0f).finished);
}

TEST(PrematchTimelineAtTest, OverlayFadesInAndOutWithinItsOwnBeat) {
  const Timeline timeline = Sample();  // lineup_home spans 14..20
  const float in = At(timeline, 14.0f + PrematchTimeline::kOverlayFadeSeconds * 0.5f).overlayAlpha;
  const float middle = At(timeline, 17.0f).overlayAlpha;
  const float out = At(timeline, 20.0f - PrematchTimeline::kOverlayFadeSeconds * 0.5f).overlayAlpha;
  EXPECT_LT(in, middle);
  EXPECT_LT(out, middle);
  EXPECT_NEAR(middle, 1.0f, 0.001f);
  EXPECT_GT(in, 0.0f);
}

TEST(PrematchTimelineAtTest, ABeatWithNoOverlayHasNoAlpha) {
  EXPECT_NEAR(At(Sample(), 9.0f).overlayAlpha, 0.0f, 0.001f);
}

TEST(PrematchTimelineAtTest, AnOverlayBeatShorterThanItsFadesStillPeaksVisible) {
  const Timeline timeline = ParseText("beat blink 0.4 overlay=formation_home\n");
  const float peak = At(timeline, 0.2f).overlayAlpha;
  EXPECT_GT(peak, 0.0f);
  EXPECT_LE(peak, 1.0f);
}

// --- Rescaling to a configured duration ---

TEST(PrematchTimelineRescaleTest, TotalMatchesTheTargetAndProportionsSurvive) {
  const Timeline scaled = Rescale(Sample(), 40.0f);  // twice the natural 20s
  EXPECT_NEAR(scaled.TotalSeconds(), 40.0f, 0.001f);
  ASSERT_EQ(scaled.beats.size(), 3u);
  EXPECT_NEAR(scaled.beats[0].seconds, 8.0f, 0.001f);
  EXPECT_NEAR(scaled.beats[1].seconds, 20.0f, 0.001f);
  EXPECT_NEAR(scaled.beats[2].seconds, 12.0f, 0.001f);
}

TEST(PrematchTimelineRescaleTest, CameraAndOverlayAreCarriedThrough) {
  const Timeline scaled = Rescale(Sample(), 40.0f);
  EXPECT_EQ(scaled.beats[0].camera, Camera::Orbit);
  EXPECT_EQ(scaled.beats[2].overlay, Overlay::FormationHome);
}

TEST(PrematchTimelineRescaleTest, ANonPositiveOrEmptyTargetChangesNothing) {
  EXPECT_NEAR(Rescale(Sample(), 0.0f).TotalSeconds(), 20.0f, 0.001f);
  EXPECT_NEAR(Rescale(Sample(), -5.0f).TotalSeconds(), 20.0f, 0.001f);
  Timeline empty;
  EXPECT_TRUE(Rescale(empty, 30.0f).beats.empty());
}

// --- Spreading the imported camerawork across the entrance beats only ---
//
// The walkout camerawork has to play across the beats that asked for it and
// nothing else: a lineup graphic holding the picture for eight seconds must
// not consume eight seconds of camera track it is not showing.

TEST(PrematchEntranceProgressTest, StaysAtZeroBeforeAnyEntranceBeat) {
  const Timeline timeline = ParseText(
      "beat stadium 4 camera=orbit\n"
      "beat walkout 10 camera=entrance\n");
  EXPECT_NEAR(PrematchTimeline::EntranceProgress(timeline, At(timeline, 2.0f)), 0.0f, 0.001f);
}

TEST(PrematchEntranceProgressTest, AdvancesOnlyWhileAnEntranceBeatIsOnAir) {
  const Timeline timeline = ParseText(
      "beat stadium 4 camera=orbit\n"
      "beat walkout 10 camera=entrance\n"
      "beat lineup 6 camera=aerial overlay=formation_home\n"
      "beat closeups 10 camera=entrance\n");
  // Half way through the first entrance beat: 5s of the 20s of entrance beats.
  EXPECT_NEAR(PrematchTimeline::EntranceProgress(timeline, At(timeline, 9.0f)), 0.25f, 0.01f);
  // Through the aerial beat, progress must not have moved on.
  EXPECT_NEAR(PrematchTimeline::EntranceProgress(timeline, At(timeline, 17.0f)), 0.5f, 0.01f);
  // Half way through the second entrance beat: 15s of 20s.
  EXPECT_NEAR(PrematchTimeline::EntranceProgress(timeline, At(timeline, 25.0f)), 0.75f, 0.01f);
}

TEST(PrematchEntranceProgressTest, ReachesOneByTheEndOfTheLastEntranceBeat) {
  const Timeline timeline = ParseText("beat walkout 10 camera=entrance\n");
  EXPECT_NEAR(PrematchTimeline::EntranceProgress(timeline, At(timeline, 9.999f)), 1.0f, 0.01f);
}

TEST(PrematchEntranceProgressTest, ATimelineWithNoEntranceBeatsIsZero) {
  const Timeline timeline = ParseText("beat stadium 4 camera=orbit\n");
  EXPECT_NEAR(PrematchTimeline::EntranceProgress(timeline, At(timeline, 2.0f)), 0.0f, 0.001f);
}

TEST(PrematchEntranceProgressTest, AFinishedOrEmptyStateIsClampedNotUndefined) {
  const Timeline timeline = ParseText("beat walkout 10 camera=entrance\n");
  const float past = PrematchTimeline::EntranceProgress(timeline, At(timeline, 30.0f));
  EXPECT_GE(past, 0.0f);
  EXPECT_LE(past, 1.0f);
  Timeline empty;
  EXPECT_NEAR(PrematchTimeline::EntranceProgress(empty, At(empty, 1.0f)), 0.0f, 0.001f);
}

TEST(PrematchTimelineParseTest, ABeatCanNameTheAuthoredShotItWants) {
  const Timeline timeline = ParseText(
      "beat walkout 16 camera=entrance shot=passage01\n"
      "beat wide 6 camera=aerial\n");
  ASSERT_EQ(timeline.beats.size(), 2u);
  EXPECT_EQ(timeline.beats[0].shot, "passage01");
  EXPECT_TRUE(timeline.beats[1].shot.empty());
}

TEST(PrematchTimelineDefaultTest, EachStagedBeatAsksForItsOwnPesShot) {
  // Each ent_<id> family is a different shot, not a variant of one entrance:
  // the walk-on, the anthems and the team picture each name theirs.
  const Timeline timeline = Default();
  std::string walkOnShot, anthemShot, pictureShot;
  for (const auto& beat : timeline.beats) {
    if (beat.name == "walk_on") walkOnShot = beat.shot;
    if (beat.name == "anthems") anthemShot = beat.shot;
    if (beat.name == "team_picture") pictureShot = beat.shot;
  }
  // ent_009's packs start the squads outside the touchline; the tunnel packs
  // need geometry no venue here provides, so they are deliberately unused.
  EXPECT_EQ(walkOnShot, "ent_009");
  EXPECT_EQ(anthemShot, "anth");
  EXPECT_EQ(pictureShot, "circle_home");
}
