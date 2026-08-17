// written by bastiaan konings schuiling 2008 - 2014
// this work is public domain. the code is undocumented, scruffy, untested, and should generally not
// be used for anything important. i do not offer support, so don't ask. to be used for inspiration
// :)

#ifndef _HPP_LOADERS_ASE
#define _HPP_LOADERS_ASE

#include <array>
#include <string>
#include <vector>

#include "base/utils.hpp"
#include "defines.hpp"
#include "managers/resourcemanager.hpp"
#include "scene/objects/geometry.hpp"
#include "scene/resources/geometrydata.hpp"

namespace blunted {

struct s_Material {
  std::string maps[4];
  std::string shininess;
  std::string specular_amount;
  Vector3 self_illumination;
};

class ASELoader : public Loader<GeometryData> {
public:
  ASELoader();
  virtual ~ASELoader();

  // ----- encapsulating load function
  virtual void Load(const std::string& filename, boost::intrusive_ptr<Resource<GeometryData>> resource);

  // ----- interpreter for the .ase treedata
  void Build(const s_tree* data, boost::intrusive_ptr<Resource<GeometryData>> resource);

  // ----- per-object interpreters
  void BuildTriangleMesh(const s_tree* data, boost::intrusive_ptr<Resource<GeometryData>> resource,
                         const std::vector<s_Material>& materialList);

  // The four map paths of each mesh, in the order the meshes were added.
  //
  // A material's texture resource cannot say where its file lives: the
  // resource manager registers surfaces under their BASENAME (see
  // ResourceManager::Fetch), so a stadium's 'ass.png' is all that survives of
  // 'media/objects/stadiums/pes_st060/textures/ass.png'. The geometry cache
  // has to be able to fetch the same file again on a cold start, so the paths
  // the parse saw are kept here for it.
  const std::vector<std::array<std::string, 4>>& GetTexturePaths() const {
    return texturePaths;
  }

protected:
  std::vector<std::array<std::string, 4>> texturePaths;
  int triangleCount;
};

}  // namespace blunted

#endif
