// A binary cache for geometry parsed out of .ase files.
//
// The ASE format is text: an imported stadium is tens of megabytes and
// hundreds of thousands of lines, and turning that into GeometryData costs a
// full tokenise-and-allocate pass every single time the scene loads. The
// result of that pass is, however, plain data - flat float arrays, index
// lists, and texture names - so it only has to be produced once.
//
// The cache sits next to its source as <file>.geomcache and is stamped with
// the source's size and modification time, so editing the .ase (they are meant
// to stay editable) silently invalidates it and the next load rebuilds it. The
// .ase remains the source of truth; the cache is derived and disposable.

#ifndef _HPP_LOADERS_ASECACHE
#define _HPP_LOADERS_ASECACHE

#include <string>

#include "scene/resources/geometrydata.hpp"
#include "types/resource.hpp"

namespace blunted {

// Path of the cache belonging to an .ase file.
std::string GeometryCachePath(const std::string& aseFilename);

// Fills `resource` from the cache, if one exists and still matches the source.
// Returns false when the caller has to parse the .ase itself.
bool LoadGeometryCache(const std::string& aseFilename,
                       boost::intrusive_ptr<Resource<GeometryData>> resource);

// Writes the cache for geometry just parsed from `aseFilename`. Failures are
// not fatal: a cache that cannot be written just means parsing again later.
void SaveGeometryCache(const std::string& aseFilename,
                       boost::intrusive_ptr<Resource<GeometryData>> resource);

}  // namespace blunted

#endif
