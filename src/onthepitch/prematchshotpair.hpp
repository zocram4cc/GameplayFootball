// Keeping a pre-match shot's camera and its choreography together.
//
// PES authors both per stadium and per variant, and names them in pairs:
//
//     ent_009_st002_cmn_cam   the camerawork
//     ent_009_st002_cmn_pl    the players it films
//
// A beat that asks for the family alone ("ent_009") must not resolve the two
// independently: the staging came from stadium 000 and the camera from stadium
// 002, so the lens sat where stadium 002's tunnel mouth is and spent the whole
// walk-on inside a player's chest.

#ifndef _HPP_ONTHEPITCH_PREMATCHSHOTPAIR
#define _HPP_ONTHEPITCH_PREMATCHSHOTPAIR

#include <string>

namespace PrematchShotPair {

// The player pack that belongs to a camera track, or empty if the name is not a
// camera's. Any directory and extension are ignored.
std::string StagingForCamera(const std::string& cameraName);

}  // namespace PrematchShotPair

#endif
