// Measures the cutscene pools Match has loaded and reports what they hold.
//
// Kept apart from cutsceneviewer.hpp so the judgement in there stays testable
// without linking the engine's track and choreography loaders, and apart from
// match.cpp so the match's own code path carries none of it.

#ifndef _HPP_ONTHEPITCH_CUTSCENEREPORT
#define _HPP_ONTHEPITCH_CUTSCENEREPORT

#include <map>
#include <string>
#include <vector>

#include "cutsceneviewer.hpp"
#include "utils/camtrack.hpp"
#include "utils/entrancechoreo.hpp"

namespace CutsceneViewer {

// Where every frame of a track sits, so its coordinates can be classified.
TrackExtent MeasureTrack(const blunted::CamTrack& track);

// Where every key of every slot of a choreography sits, so the staging can be
// classified the same way the camerawork is. A foul's actors are authored about
// the incident; an entrance's walk out of a real tunnel in stadium coordinates.
TrackExtent MeasureChoreography(const blunted::EntranceChoreo& choreography);

// One line per pool: how much camerawork and choreography it holds, and whether
// each is authored about the incident or in stadium coordinates.
std::vector<std::string> Report(
    const std::map<std::string, std::vector<blunted::CamTrack>>& cameraPools,
    const std::map<std::string, std::vector<blunted::EntranceChoreo>>& choreographyPools);

}  // namespace CutsceneViewer

#endif
