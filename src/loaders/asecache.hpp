// A binary cache for geometry parsed out of .ase files.
//
// The ASE format is text: an imported stadium is tens of megabytes and
// hundreds of thousands of lines, and turning that into GeometryData costs a
// full tokenise-and-allocate pass every single time the scene loads. The
// result of that pass is, however, plain data - flat float arrays, index
// lists, and texture names - so it only has to be produced once.
//
// The cache sits next to its source as <file>.geomcache, stamped with the
// source's size and a content hash - not a modification time, so a cache
// stays valid across a copy or a checkout. Editing the .ase (they are meant
// to stay editable) silently invalidates it and the next load rebuilds it.
// The .ase remains the source of truth; the cache is derived and disposable,
// and is gitignored for that reason.
//
// What it is worth: across a match load, parsing every .ase costs about
// 2.65 s and reading them all back from cache about 0.58 s - the imported
// stadium alone goes from 633 ms to 287 ms. The caches for that same content
// weigh about 82 MB, which is why they are built on first load rather than
// shipped: the trade only makes sense on the machine that already has them.

#ifndef _HPP_LOADERS_ASECACHE
#define _HPP_LOADERS_ASECACHE

#include <array>
#include <string>
#include <vector>

#include "scene/resources/geometrydata.hpp"
#include "types/resource.hpp"

namespace blunted {

// Path of the cache belonging to an .ase file.
std::string GeometryCachePath(const std::string& aseFilename);

// Fills `resource` from the cache, if one exists and still matches the source.
// Returns false when the caller has to parse the .ase itself.
bool LoadGeometryCache(const std::string& aseFilename,
                       boost::intrusive_ptr<Resource<GeometryData>> resource);

// Writes the cache for geometry just parsed from `aseFilename`.
//
// `texturePaths` holds the four map paths of each mesh, in mesh order, as the
// parse saw them (ASELoader::GetTexturePaths). They cannot be recovered from
// the geometry itself: the resource manager registers surfaces under their
// basename, so a material only remembers 'ass.png', never the directory it
// came out of - and a cold start has to be able to open the file again.
//
// Failures are not fatal: a cache that cannot be written just means parsing
// again later.
void SaveGeometryCache(const std::string& aseFilename,
                       boost::intrusive_ptr<Resource<GeometryData>> resource,
                       const std::vector<std::array<std::string, 4>>& texturePaths);

}  // namespace blunted

#endif
