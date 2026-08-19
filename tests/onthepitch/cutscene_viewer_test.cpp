// Tests for the cutscene bench's measurements.
//
// The classification is the part that matters: it is what tells camerawork
// authored about the incident apart from camerawork authored in stadium
// coordinates, which is the distinction the foul cutscenes got wrong. PES's card
// shot is a static camera 5.6 m from the origin; used as a world position it
// filmed every foul from the centre spot.

#include <gtest/gtest.h>

#include "onthepitch/cutsceneviewer.hpp"

namespace {

CutsceneViewer::TrackExtent Extent(int frames, float minX, float maxX, float minY, float maxY) {
  CutsceneViewer::TrackExtent e;
  e.frames = frames;
  e.minX = minX;
  e.maxX = maxX;
  e.minY = minY;
  e.maxY = maxY;
  e.isStatic = (minX == maxX && minY == maxY);
  return e;
}

}  // namespace

TEST(CutsceneViewer, MeasuresSpanAndRadius) {
  const CutsceneViewer::TrackExtent e = Extent(180, -4.4f, -4.4f, -3.1f, -3.1f);
  EXPECT_FLOAT_EQ(e.SpanX(), 0.0f);
  EXPECT_TRUE(e.isStatic);
  EXPECT_NEAR(e.MaxRadius(), 5.38f, 0.05f);
}

// PES's card shot: static, 5.6 m out. Authored about the incident.
TEST(CutsceneViewer, ACardShotIsIncidentLocal) {
  EXPECT_EQ(CutsceneViewer::ClassifyAnchoring(Extent(180, -4.4f, -4.4f, -3.1f, -3.1f)),
            CutsceneViewer::Anchoring::IncidentLocal);
}

// A substitution is staged at the touchline, 27 m off centre: stadium space.
TEST(CutsceneViewer, ASubstitutionShotIsStadiumWorld) {
  EXPECT_EQ(CutsceneViewer::ClassifyAnchoring(Extent(240, -2.2f, 0.4f, -27.2f, -9.0f)),
            CutsceneViewer::Anchoring::StadiumWorld);
}

// A shot of the stands spans most of the pitch.
TEST(CutsceneViewer, AResultShotIsStadiumWorld) {
  EXPECT_EQ(CutsceneViewer::ClassifyAnchoring(Extent(300, -72.0f, 56.0f, -39.5f, 0.0f)),
            CutsceneViewer::Anchoring::StadiumWorld);
}

TEST(CutsceneViewer, AnEmptyTrackCannotBeClassified) {
  EXPECT_EQ(CutsceneViewer::ClassifyAnchoring(Extent(0, 0, 0, 0, 0)),
            CutsceneViewer::Anchoring::Unknown);
}

// A track that stays close to the centre but does move is still incident-local:
// PES pans a little within the shot.
TEST(CutsceneViewer, AShortPanNearTheCentreIsStillIncidentLocal) {
  EXPECT_EQ(CutsceneViewer::ClassifyAnchoring(Extent(120, -6.0f, -2.0f, -4.0f, 1.0f)),
            CutsceneViewer::Anchoring::IncidentLocal);
}

TEST(CutsceneViewer, PackSelectionWalksThePacksInTurn) {
  CutsceneViewer::Settings s;
  s.seconds = 60.0f;
  s.packSeconds = 5.0f;
  EXPECT_EQ(CutsceneViewer::PackIndexAt(s, 0.0f, 3), 0);
  EXPECT_EQ(CutsceneViewer::PackIndexAt(s, 4.9f, 3), 0);
  EXPECT_EQ(CutsceneViewer::PackIndexAt(s, 5.1f, 3), 1);
  EXPECT_EQ(CutsceneViewer::PackIndexAt(s, 12.0f, 3), 2);
  // and wraps, so a short pool keeps cycling for the whole run
  EXPECT_EQ(CutsceneViewer::PackIndexAt(s, 17.0f, 3), 0);
}

TEST(CutsceneViewer, PackSelectionReportsNothingToShow) {
  CutsceneViewer::Settings s;
  s.seconds = 60.0f;
  EXPECT_EQ(CutsceneViewer::PackIndexAt(s, 1.0f, 0), -1);
}

TEST(CutsceneViewer, TheBenchStopsAfterItsRunTime) {
  CutsceneViewer::Settings s;
  s.seconds = 10.0f;
  EXPECT_TRUE(CutsceneViewer::IsRunning(s, 9.5f));
  EXPECT_FALSE(CutsceneViewer::IsRunning(s, 10.5f));
}

TEST(CutsceneViewer, AnEmptyFilterMatchesEveryPack) {
  CutsceneViewer::Settings s;
  EXPECT_TRUE(CutsceneViewer::PackMatches(s, "foul_injury_card_r01"));
}

TEST(CutsceneViewer, AFilterMatchesOnSubstring) {
  CutsceneViewer::Settings s;
  s.pack = "card_r";
  EXPECT_TRUE(CutsceneViewer::PackMatches(s, "foul_injury_card_r01"));
  EXPECT_FALSE(CutsceneViewer::PackMatches(s, "foul_injury_card_y01"));
}

// How far out a track sits is a poor test of whose space it is in. Measured over the
// 703 imported tracks, the 12 m rule calls only 32% of the goal camerawork
// incident-local when 90% of it aims at the origin, and none of the `result`
// camerawork - which is 88% aimed at the origin from a median 99 m out, long lenses
// on a group standing at the middle. Those are authored about their subject as
// plainly as a card shot is; they are just further away.
//
// So aim decides too. It cannot decide alone: PES's card shots sit within 12 m and do
// NOT aim at their origin (0 of 2), because they frame a referee standing beside the
// incident rather than the incident itself. Both signs together, either sufficient,
// is the rule that fits the data - and it is strictly wider than the radius alone, so
// nothing that is staged at the incident today stops being staged there.

TEST(CutsceneViewer, ALongLensAimedAtItsOriginIsIncidentLocal) {
  // result_*: 99 m out, pointed at the middle
  CutsceneViewer::TrackExtent e = Extent(300, -70.0f, -70.0f, -70.0f, -70.0f);
  e.aimsAtOrigin = true;
  EXPECT_EQ(CutsceneViewer::ClassifyAnchoring(e), CutsceneViewer::Anchoring::IncidentLocal);
}

TEST(CutsceneViewer, ACloseShotThatLooksElsewhereIsStillIncidentLocal) {
  // the card shot: 5.6 m out, aimed past the origin at the referee
  CutsceneViewer::TrackExtent e = Extent(180, -4.4f, -4.4f, -3.1f, -3.1f);
  e.aimsAtOrigin = false;
  EXPECT_EQ(CutsceneViewer::ClassifyAnchoring(e), CutsceneViewer::Anchoring::IncidentLocal);
}

TEST(CutsceneViewer, AFarShotThatLooksElsewhereIsStadiumWorld) {
  // neither sign: a shot of the stands, authored where it stands
  CutsceneViewer::TrackExtent e = Extent(300, -72.0f, 56.0f, -39.5f, 0.0f);
  e.aimsAtOrigin = false;
  EXPECT_EQ(CutsceneViewer::ClassifyAnchoring(e), CutsceneViewer::Anchoring::StadiumWorld);
}

TEST(CutsceneViewer, AnEmptyTrackIsStillUnclassifiable) {
  CutsceneViewer::TrackExtent e = Extent(0, 0, 0, 0, 0);
  e.aimsAtOrigin = true;   // nothing to aim with, so it cannot rescue an empty track
  EXPECT_EQ(CutsceneViewer::ClassifyAnchoring(e), CutsceneViewer::Anchoring::Unknown);
}

// Which categories are about an incident, and so must be staged at one.
//
// Measured off a match with debug_cutscene_report on:
//
//   category foul/card_yellow  shot incident-local: incident at 0,-33, camera 6 m from it
//   category change            shot stadium-world:  incident at -61,-14, camera 117 m from it
//
// The foul is right and the substitution is not, and the reason is not the track
// measurement - it is that a substitution's camerawork is authored in PES's own
// stadium (78 tracks, every one beyond 12 m, out to 86.6 m for the change_stand_*
// family) and used here as a world position. On PES's ground a stand camera looking
// at PES's touchline is a fine shot; on ours it films the sky over the stand.
//
// A category either has a subject on the pitch or it does not, and that is known
// from the category rather than from coordinates. A foul, a substitution and an
// offside all have one. The post-match and entrance presentations do not - they are
// authored to show the stadium, and anchoring them at the ball would wreck them.

TEST(CutsceneViewer, AnIncidentCategoryIsStagedAtTheIncident) {
  EXPECT_TRUE(CutsceneViewer::AnchorsAtIncident("foul"));
  EXPECT_TRUE(CutsceneViewer::AnchorsAtIncident("change"));
  EXPECT_TRUE(CutsceneViewer::AnchorsAtIncident("offside"));
}

TEST(CutsceneViewer, ASubpoolIsTheSameCategory) {
  // the engine asks for "foul/card_yellow" and falls back to "foul"
  EXPECT_TRUE(CutsceneViewer::AnchorsAtIncident("foul/card_yellow"));
  EXPECT_TRUE(CutsceneViewer::AnchorsAtIncident("goal/offside"));
}

TEST(CutsceneViewer, APresentationCategoryKeepsItsOwnCoordinates) {
  // these are shots of a stadium, and there is no incident in them to move to
  EXPECT_FALSE(CutsceneViewer::AnchorsAtIncident("result"));
  EXPECT_FALSE(CutsceneViewer::AnchorsAtIncident("result/003"));
  EXPECT_FALSE(CutsceneViewer::AnchorsAtIncident("end"));
  EXPECT_FALSE(CutsceneViewer::AnchorsAtIncident("ent"));
  EXPECT_FALSE(CutsceneViewer::AnchorsAtIncident("timeup"));
  EXPECT_FALSE(CutsceneViewer::AnchorsAtIncident("mode"));
}

TEST(CutsceneViewer, SomethingUnknownIsLeftWhereItIs) {
  EXPECT_FALSE(CutsceneViewer::AnchorsAtIncident(""));
  EXPECT_FALSE(CutsceneViewer::AnchorsAtIncident("something_new"));
}

// Where a substitution happens: at the touchline beside the man coming off, not
// where he was standing and not where the ball rolled out. The ball is the wrong
// answer badly - it put the anchor at -61,-14, six metres beyond the goal line.
TEST(CutsceneViewer, ASubstitutionIsStagedAtTheNearestTouchline) {
  // a player in his own half, nearer the -y touchline
  const auto mark = CutsceneViewer::TouchlineMark(20.0f, -12.0f, 36.0f);
  EXPECT_FLOAT_EQ(mark.first, 20.0f);
  EXPECT_FLOAT_EQ(mark.second, -36.0f);
}

TEST(CutsceneViewer, AndTheOtherTouchlineWhenHeIsNearerThat) {
  const auto mark = CutsceneViewer::TouchlineMark(-8.0f, 21.0f, 36.0f);
  EXPECT_FLOAT_EQ(mark.first, -8.0f);
  EXPECT_FLOAT_EQ(mark.second, 36.0f);
}

TEST(CutsceneViewer, ATouchlineMarkStaysBetweenTheGoalLines) {
  // a man out by the corner does not drag the change behind the goal
  EXPECT_FLOAT_EQ(CutsceneViewer::TouchlineMark(70.0f, 2.0f, 36.0f).first, 45.0f);
  EXPECT_FLOAT_EQ(CutsceneViewer::TouchlineMark(-70.0f, 2.0f, 36.0f).first, -45.0f);
}
