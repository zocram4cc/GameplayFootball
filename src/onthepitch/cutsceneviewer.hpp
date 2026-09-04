// A bench for the imported cutscenes, deliberately outside the match's own
// code path.
//
// Camerawork and actor choreography have been conflated more than once in this
// import - the entrance shots were blamed on stale clips when the real fault was
// per-stadium camerawork, and the offside "camera" turned out to be an actor
// pack with no camera stream at all. Both times the mistake was inferring what a
// pack contained instead of looking. This measures each pack and says what it
// actually holds, and plays one back so it can be seen.
//
// Config ("debug_cutscene_seconds" > 0 turns the bench on):
//   debug_cutscene_seconds     how long the bench runs
//   debug_cutscene_category    which pool, e.g. "foul/card_red" or "offside"
//   debug_cutscene_pack        substring of the pack name, empty for all
//   debug_cutscene_pack_secs   seconds per pack before moving to the next
//   debug_cutscene_report      log a measurement of every installed pool
//
// The maths is free functions over plain data so it can be tested without an
// engine, in the same spirit as modelviewer.hpp.

#ifndef _HPP_ONTHEPITCH_CUTSCENEVIEWER
#define _HPP_ONTHEPITCH_CUTSCENEVIEWER

#include <string>
#include <vector>

namespace CutsceneViewer {

struct Settings {
  float seconds = 0.0f;
  std::string category;
  std::string pack;
  float packSeconds = 6.0f;
  bool report = false;

  bool IsEnabled() const { return seconds > 0.0f; }
};

// What a camera track occupies in space, and whether it moves at all.
struct TrackExtent {
  int frames = 0;
  float minX = 0.0f, maxX = 0.0f;
  float minY = 0.0f, maxY = 0.0f;
  float minZ = 0.0f, maxZ = 0.0f;
  bool isStatic = false;  // the camera never moves over the whole track
  // Does the camera look at the origin? The other half of the anchoring question,
  // and the discriminating half: a shot authored about its subject aims at where
  // that subject stands, however far out the camera itself is.
  bool aimsAtOrigin = false;

  float SpanX() const { return maxX - minX; }
  float SpanY() const { return maxY - minY; }
  // Furthest any frame sits from the pitch centre, on the ground plane.
  float MaxRadius() const;
};

// Where a track's coordinates are expressed. The distinction decides whether
// the engine may use them as world positions or has to place them at the
// incident first: a shot authored about the incident sits a few metres from the
// origin, and used as-is it films the centre spot whatever corner the incident
// happened in.
enum class Anchoring {
  Unknown,         // no frames to judge
  IncidentLocal,   // authored about the origin: must be placed at the incident
  StadiumWorld,    // authored in pitch coordinates: use as-is
};

// A track whose every frame lies within this radius of the centre is taken to
// be authored about the incident rather than the stadium. The pitch is 110x72,
// so nothing genuinely staged in stadium coordinates - a touchline
// substitution, a shot of the stands - lives this close to the centre spot.
constexpr float kIncidentLocalRadius = 12.0f;

// How near the origin a camera has to be pointed for the shot to count as authored
// about it. Measured over the 703 imported tracks: at fifteen degrees, 90% of the
// goal camerawork and 88% of the `result` camerawork qualify, while none of the
// substitution or card shots do - they frame a person standing beside the incident.
constexpr float kAimsAtOriginDegrees = 15.0f;

// Is this category about something that happened on the pitch?
//
// Measuring the track is not enough to decide it: a card shot sits five metres out
// and frames the referee rather than the incident, so it has to be placed at the
// incident by what the category *is*. A foul and an offside each have a subject
// standing on the grass, and their shots are placed at it. The entrance and the
// post-match presentations do not: they are authored to show a stadium, and
// dragging them to the ball would wreck them. Neither does a substitution - PES
// authors those in stadium coordinates at the halfway line on the bench side, all
// 78 tracks and 30 stagings, and they play where they were written.
//
// Accepts a subpool ("foul/card_yellow") as its category.
bool AnchorsAtIncident(const std::string& category);

// Where a substitution happens: the halfway line on the bench side (-y), which is
// where PES's change staging and camerawork are authored, in stadium coordinates.
std::pair<float, float> SubstitutionMark(float pitchHalfY);

// Which assistant gives an offside, and where he stands to give it.
struct AssistantMark {
  int linesman = 0;  // 0 selects linesman0Y's man below, 1 selects linesman1Y's
  float x = 0.0f;
  float y = 0.0f;
};

// PES ships no offside camerawork or authored assistant position in any
// generation (16 through 21): every offside pack carries zero camera frames, and
// where a cut record names a clip at all it names an empty one. So the placement
// is the engine's to get right rather than to import.
//
// Each assistant runs one touchline for the whole match and gives the offsides
// he is positioned to see. Which one that is comes from his own LIVE y
// (``linesman0Y``/``linesman1Y``), not an assumed spawn constant or accessor
// name - that exact assumption (index 0 is always the -y man) was the bug:
// officials.cpp spawns linesman 0 on the -y touchline but names the accessor
// GetLinesmanNorth(), and the code this replaces picked by that name instead
// of by where he actually stands.
AssistantMark OffsideAssistantMark(float incidentX, float incidentY, float linesman0Y,
                                   float linesman1Y, float pitchHalfX, float pitchHalfY);

Anchoring ClassifyAnchoring(const TrackExtent& extent);
const char* AnchoringName(Anchoring anchoring);

// Which pack the bench should be showing, given how long it has been running.
// Returns -1 when there is nothing to show.
int PackIndexAt(const Settings& settings, float elapsedSeconds, int packCount);

// Still within the bench's run time?
bool IsRunning(const Settings& settings, float elapsedSeconds);

// Does this pack name match the settings' filter?
bool PackMatches(const Settings& settings, const std::string& packName);

// One line describing a pool, for the report.
std::string DescribePool(const std::string& category, int cameraPacks, int choreographyPacks,
                         const TrackExtent& firstTrack);

// Whether the legacy referee-follow may take the camera during a booking.
//
// It takes it by switching auto camera updates off - and that is the very function the
// imported PES foul camerawork plays through, so the shot was chosen, started, logged
// and never seen: what the viewer got was the 2008 follow-the-referee camera. PES
// ships two foul camera packs of its seventy-eight and both are imported, so when one
// is playing it wins; the follow is for a stoppage with nothing else to show.
bool RefereeFollowMayTakeCamera(bool cutscenePlaying);

}  // namespace CutsceneViewer

#endif
