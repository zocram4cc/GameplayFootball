// written by bastiaan konings schuiling 2008 - 2014
// this work is public domain. the code is undocumented, scruffy, untested, and should generally not
// be used for anything important. i do not offer support, so don't ask. to be used for inspiration
// :)

#ifndef _HPP_MATERIAL
#define _HPP_MATERIAL

#include "scene/resources/surface.hpp"
#include "utils/uvanim.hpp"
namespace blunted {

// Per-material PES UV animation (uvscroll / uvstep, e.g. the k2017 LED
// boots). The importer bakes it into the .object's MATERIAL block; the
// renderer replays it against a per-frame clock so PES's animated boots
// stay animated. family None (0) means no animation.

struct Material {
  boost::intrusive_ptr<Resource<Surface>> diffuseTexture;
  boost::intrusive_ptr<Resource<Surface>> normalTexture;
  boost::intrusive_ptr<Resource<Surface>> specularTexture;
  boost::intrusive_ptr<Resource<Surface>> illuminationTexture;
  boost::intrusive_ptr<Resource<Surface>> timingTexture;  // uvanim: tile-select grid
  UvAnimParams uvanim;
  float shininess;
  float specular_amount;
  Vector3 self_illumination;
};

}  // namespace blunted

#endif
