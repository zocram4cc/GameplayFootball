// Loader for the .faceanim text format (imported PES facial expressions,
// exported by tools/pes21_import/face_to_anim.py):
//   <bone>,<frame>,<qx>,<qy>,<qz>,<qw>[,...]   rotation keys
//   <bone>_pos,<frame>,<x>,<y>,<z>[,...]       translation keys (metres)
//   <frames> N </frames>                       tail

#ifndef _HPP_UTILS_FACEANIM
#define _HPP_UTILS_FACEANIM

#include <array>
#include <istream>
#include <map>
#include <string>
#include <vector>

namespace blunted {

struct FaceKey {
  int frame = 0;
  std::array<float, 4> values{};  // qx,qy,qz,qw or x,y,z,unused
};

class FaceAnim {
public:
  // false when the stream holds no valid track lines
  bool Load(std::istream& in);

  int GetFrameCount() const { return frameCount; }
  const std::map<std::string, std::vector<FaceKey>>& GetRotationTracks() const {
    return rotations;
  }
  const std::map<std::string, std::vector<FaceKey>>& GetTranslationTracks() const {
    return translations;
  }

private:
  int frameCount = 0;
  std::map<std::string, std::vector<FaceKey>> rotations;
  std::map<std::string, std::vector<FaceKey>> translations;
};

}  // namespace blunted

#endif
