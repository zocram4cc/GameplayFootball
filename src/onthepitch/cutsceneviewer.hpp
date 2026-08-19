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
// Measuring the track is not enough to decide it, and a substitution is the proof.
// PES authors its change camerawork in its own stadium - all 78 tracks sit beyond
// 12 m, the change_stand_* family out to 86.6 m - so on PES's ground a stand camera
// looking at PES's touchline is a fine shot, and on ours the same coordinates film
// the sky over the stand. Measured with debug_cutscene_report: a foul was shot from
// 6 m of an incident at (0, -33), and a substitution from 117 m of one at (-61, -14).
//
// So the question is answered by what the category *is*. A foul, a substitution and
// an offside each have a subject standing on the grass, and their shots are placed at
// it. The entrance and the post-match presentations do not: they are authored to show
// a stadium, and dragging them to the ball would wreck them.
//
// Accepts a subpool ("foul/card_yellow") as its category.
bool AnchorsAtIncident(const std::string& category);

// Where a substitution happens: the touchline beside the man coming off, clamped
// between the goal lines so a change never stages itself behind a goal.
std::pair<float, float> TouchlineMark(float x, float y, float pitchHalfY);

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

}  // namespace CutsceneViewer

#endif
