#ifndef _HPP_ONTHEPITCH_CUTSCENESEQUENCE
#define _HPP_ONTHEPITCH_CUTSCENESEQUENCE

#include <functional>
#include <string>
#include <vector>

// Cutscenes that run as several shots rather than one.
//
// PES does not close a match with a single camera: the whistle goes and the broadcast
// cuts between the crowd of that stadium, the winners celebrating, the losers standing
// about, the walk to the crowd, the team photo - all of it behind the result panel, all
// of it imported here and none of it ever played. Nor is a goal one shot: the scorer's
// run has shots after it that we were dropping on the floor.
//
// This decides the order; Match plays each stage as it would play any cutscene, so a
// stage is skippable and pausable like the rest.
namespace CutsceneSequence {

struct Stage {
  std::string pool;   // the cutscene pool to draw the shot from
  float seconds = 0;  // the cap; the track's own length wins when shorter
};

// Whether a pool has anything in it. Passed in rather than looked up, so the order can
// be decided and tested without a match.
using PoolTest = std::function<bool(const std::string&)>;

// The shots after the whistle, for a team that won, drew or lost. Names only pools the
// test says exist, and the stadium's own crowd shots when they were imported: PES
// exports these per stadium (end_audience_st011_ha_home), so a neutral ground does not
// get another ground's stands.
std::vector<Stage> ClosingStages(int goalDifference, const std::string& stadiumTag,
                                 const PoolTest& has);

// Which pool a closing camtrack belongs in. PES exports the end-of-match camerawork
// as one flat directory whose file names carry the family - end_joy_high_2,
// end_lose_sad_1, end_audience_st011_ha_home - so the loader has to read them to tell
// a celebration from a crowd shot. Returns "" for a name it does not recognise, which
// stays in the category pool and nothing more.
std::string ClosingPoolForFile(const std::string& filename);

// The ground a pack was authored for, read from its name ("change_stand_st041_home"
// -> "st041"), or "" for one authored for every ground. PES places a stand camera
// where that stadium has a stand: at any other ground the same coordinates are a
// seat in the sky or the inside of a roof, so a pack that names another ground is
// not installed there at all.
std::string GroundOfFile(const std::string& filename);

// How long the whole sequence runs, for a caller that has to reserve the time.
float TotalSeconds(const std::vector<Stage>& stages);

}  // namespace CutsceneSequence

#endif
