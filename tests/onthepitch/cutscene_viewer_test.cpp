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
