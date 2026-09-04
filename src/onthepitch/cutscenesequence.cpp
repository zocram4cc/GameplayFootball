#include "cutscenesequence.hpp"

#include <cctype>

namespace CutsceneSequence {
namespace {

// How long each kind of shot is held. The tracks carry their own length and the
// shorter of the two wins, so these are ceilings: long enough that a full-length
// track is not cut off, short enough that the whole ceremony stays watchable.
constexpr float kAudienceSeconds = 6.0f;
constexpr float kReactionSeconds = 7.0f;
constexpr float kGreetSeconds = 7.0f;
constexpr float kPhotoSeconds = 6.0f;

void Add(std::vector<Stage>& stages, const PoolTest& has, const std::string& pool,
         float seconds) {
  if (has(pool))
    stages.push_back(Stage{pool, seconds});
}

}  // namespace

std::vector<Stage> ClosingStages(int goalDifference, const std::string& stadiumTag,
                                 const PoolTest& has) {
  std::vector<Stage> stages;
  // The ground first, as the broadcast does: the whistle goes and the crowd is what
  // is shown. Only this ground's own stands - PES exports them per stadium.
  if (!stadiumTag.empty())
    Add(stages, has, "end/audience_" + stadiumTag, kAudienceSeconds);
  // Then the players. A draw is neither celebrated nor mourned, so it goes straight
  // to the walk over to the crowd.
  if (goalDifference > 0)
    Add(stages, has, "end/joy", kReactionSeconds);
  else if (goalDifference < 0)
    Add(stages, has, "end/sad", kReactionSeconds);
  Add(stages, has, "end/greet", kGreetSeconds);
  Add(stages, has, "end/photo", kPhotoSeconds);
  return stages;
}

std::string ClosingPoolForFile(const std::string& filename) {
  // end_audience_st011_ha_home.camtrack -> end/audience_st011, so a neutral ground
  // cannot be given another ground's stands.
  const std::string audience = "end_audience_";
  if (filename.compare(0, audience.size(), audience) == 0) {
    const size_t begin = audience.size();
    size_t end = filename.find('_', begin);
    if (end == std::string::npos)
      end = filename.find('.', begin);
    if (end != std::string::npos && end > begin)
      return "end/audience_" + filename.substr(begin, end - begin);
    return "";
  }
  // The rest carry their family in the second word. joy_high and shareJoy are both
  // the winners celebrating; lose_sad is the other side of it. PES's "timeup"
  // directory is flat the same way and mixes three moments - the half-time
  // walk-off, the full-time reaction and the post-match pick-up - so the interval
  // gets its own pool too, or a 48-second "glad" camera turns up at half time.
  struct Family {
    const char* prefix;
    const char* pool;
  };
  static const Family kFamilies[] = {
      {"end_joy_high", "end/joy"},   {"end_shareJoy", "end/joy"},
      {"end_lose_sad", "end/sad"},   {"end_greet_audi", "end/greet"},
      {"end_photo", "end/photo"},    {"end_retire", "end/retire"},
      {"tu_half", "timeup/half"},    {"tu_full", "timeup/full"},
      {"tu_pickup", "timeup/pickup"},
  };
  for (const Family& family : kFamilies) {
    const std::string prefix(family.prefix);
    if (filename.compare(0, prefix.size(), prefix) != 0) continue;
    // The walk-off is staged in stadium coordinates - the primary stands at that
    // ground's tunnel mouth, the rest at fixed points on its pitch - so a pack
    // exported for st000 has nothing to say at st002 and is filed under its own
    // ground, like the crowd shots. One without a ground is for every ground.
    if (family.pool == std::string("timeup/half")) {
      const std::string ground = GroundOfFile(filename);
      if (!ground.empty()) return "timeup/half_" + ground;
    }
    return family.pool;
  }
  return "";
}

std::string GroundOfFile(const std::string& filename) {
  for (size_t at = filename.find("_st"); at != std::string::npos; at = filename.find("_st", at + 1)) {
    if (at + 6 > filename.size()) break;
    if (isdigit((unsigned char)filename[at + 3]) && isdigit((unsigned char)filename[at + 4]) &&
        isdigit((unsigned char)filename[at + 5]))
      return filename.substr(at + 1, 5);
  }
  return "";
}

float TotalSeconds(const std::vector<Stage>& stages) {
  float total = 0.0f;
  for (const Stage& stage : stages) total += stage.seconds;
  return total;
}

}  // namespace CutsceneSequence
