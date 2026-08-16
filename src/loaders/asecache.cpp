#include "asecache.hpp"

#include <sys/stat.h>

#include <cstdio>
#include <cstring>
#include <vector>

#include "base/log.hpp"
#include "base/utils.hpp"
#include "managers/resourcemanagerpool.hpp"

namespace blunted {

namespace {

const char kMagic[4] = {'G', 'F', 'G', 'C'};
const unsigned int kVersion = 1;

// vertices, normals, texture coords, tangents, bitangents
const int kFloatsPerVertex = 3 * 5;

struct SourceStamp {
  unsigned long long size = 0;
  unsigned long long hash = 0;
};

// Caches are built during import and SHIPPED with the content, so the stamp
// has to survive being copied, archived and checked out - a modification time
// does not. Hashing the source costs milliseconds against the seconds that
// parsing it costs, and it is what makes a prebuilt cache trustworthy on a
// machine that has never parsed the .ase itself.
bool StatSource(const std::string& filename, SourceStamp& stamp) {
  FILE* file = fopen(filename.c_str(), "rb");
  if (!file) return false;

  unsigned long long hash = 14695981039346656037ull;  // FNV-1a
  unsigned long long size = 0;
  unsigned char buffer[64 * 1024];
  size_t got;
  while ((got = fread(buffer, 1, sizeof(buffer), file)) > 0) {
    size += got;
    for (size_t i = 0; i < got; i++) {
      hash ^= buffer[i];
      hash *= 1099511628211ull;
    }
  }
  fclose(file);

  stamp.size = size;
  stamp.hash = hash;
  return true;
}

bool ReadString(FILE* file, std::string& out) {
  unsigned int length = 0;
  if (fread(&length, sizeof(length), 1, file) != 1) return false;
  if (length > (1u << 20)) return false;  // a texture path, not a novel
  out.resize(length);
  return length == 0 || fread(&out[0], 1, length, file) == length;
}

void WriteString(FILE* file, const std::string& text) {
  const unsigned int length = (unsigned int)text.size();
  fwrite(&length, sizeof(length), 1, file);
  if (length) fwrite(text.data(), 1, length, file);
}

// Not every surface is a file. A player's kit, for one, is generated at
// runtime and its ident is a key rather than a path - asking the image loader
// for it is fatal. Only geometry whose textures all come off disk can be
// reconstructed from a cache, so both ends check before trusting one.
bool TextureIsOnDisk(const std::string& name) {
  if (name.empty()) return true;   // no texture is fine
  FILE* file = fopen(name.c_str(), "rb");
  if (!file) return false;
  fclose(file);
  return true;
}

boost::intrusive_ptr<Resource<Surface>> FetchTexture(const std::string& name) {
  if (name.empty()) return boost::intrusive_ptr<Resource<Surface>>();
  return ResourceManagerPool::GetInstance()
      .GetManager<Surface>(e_ResourceType_Surface)
      ->Fetch(name, true, true);
}

std::string TextureName(const boost::intrusive_ptr<Resource<Surface>>& texture) {
  if (!texture) return "";
  return texture->GetIdentString();
}

}  // namespace

std::string GeometryCachePath(const std::string& aseFilename) {
  return aseFilename + ".geomcache";
}

bool LoadGeometryCache(const std::string& aseFilename,
                       boost::intrusive_ptr<Resource<GeometryData>> resource) {
  // Open the cache before hashing the source: with no cache present, hashing
  // tens of megabytes would be pure waste on every single load.
  FILE* file = fopen(GeometryCachePath(aseFilename).c_str(), "rb");
  if (!file) return false;

  SourceStamp source;
  if (!StatSource(aseFilename, source)) {
    fclose(file);
    return false;
  }

  bool ok = false;
  do {
    char magic[4];
    unsigned int version = 0;
    SourceStamp stamped;
    unsigned int meshCount = 0;
    if (fread(magic, 1, 4, file) != 4 || memcmp(magic, kMagic, 4) != 0) break;
    if (fread(&version, sizeof(version), 1, file) != 1 || version != kVersion) break;
    if (fread(&stamped, sizeof(stamped), 1, file) != 1) break;
    // the .ase is editable; a changed one invalidates whatever we cached
    if (stamped.size != source.size || stamped.hash != source.hash) break;
    if (fread(&meshCount, sizeof(meshCount), 1, file) != 1) break;

    std::vector<std::pair<Material, std::pair<float*, int>>> meshes;
    std::vector<std::vector<unsigned int>> indexLists;
    bool failed = false;
    for (unsigned int m = 0; m < meshCount && !failed; m++) {
      Material material;
      std::string diffuse, normal, specular, illumination;
      if (!ReadString(file, diffuse) || !ReadString(file, normal) ||
          !ReadString(file, specular) || !ReadString(file, illumination)) {
        failed = true;
        break;
      }
      if (!TextureIsOnDisk(diffuse) || !TextureIsOnDisk(normal) ||
          !TextureIsOnDisk(specular) || !TextureIsOnDisk(illumination)) {
        failed = true;
        break;
      }
      material.diffuseTexture = FetchTexture(diffuse);
      material.normalTexture = FetchTexture(normal);
      material.specularTexture = FetchTexture(specular);
      material.illuminationTexture = FetchTexture(illumination);

      float selfIllumination[3] = {0, 0, 0};
      if (fread(&material.shininess, sizeof(float), 1, file) != 1 ||
          fread(&material.specular_amount, sizeof(float), 1, file) != 1 ||
          fread(selfIllumination, sizeof(float), 3, file) != 3) {
        failed = true;
        break;
      }
      material.self_illumination.Set(selfIllumination[0], selfIllumination[1],
                                     selfIllumination[2]);

      int verticesDataSize = 0;
      unsigned int indexCount = 0;
      if (fread(&verticesDataSize, sizeof(verticesDataSize), 1, file) != 1 ||
          verticesDataSize <= 0) {
        failed = true;
        break;
      }
      const size_t floatCount = (size_t)verticesDataSize * kFloatsPerVertex;
      float* vertices = new float[floatCount];
      if (fread(vertices, sizeof(float), floatCount, file) != floatCount ||
          fread(&indexCount, sizeof(indexCount), 1, file) != 1 || indexCount % 3 != 0) {
        delete[] vertices;
        failed = true;
        break;
      }
      std::vector<unsigned int> indices(indexCount);
      if (indexCount &&
          fread(&indices[0], sizeof(unsigned int), indexCount, file) != indexCount) {
        delete[] vertices;
        failed = true;
        break;
      }
      meshes.push_back({material, {vertices, verticesDataSize}});
      indexLists.push_back(std::move(indices));
    }

    if (failed) {
      for (auto& mesh : meshes) delete[] mesh.second.first;
      break;
    }

    resource->resourceMutex.lock();
    for (size_t m = 0; m < meshes.size(); m++) {
      resource->GetResource()->AddTriangleMesh(meshes[m].first, meshes[m].second.first,
                                               meshes[m].second.second, indexLists[m]);
    }
    resource->resourceMutex.unlock();
    ok = true;
  } while (false);

  fclose(file);
  return ok;
}

void SaveGeometryCache(const std::string& aseFilename,
                       boost::intrusive_ptr<Resource<GeometryData>> resource) {
  SourceStamp source;
  if (!StatSource(aseFilename, source)) return;

  const std::string path = GeometryCachePath(aseFilename);
  const std::string temporary = path + ".tmp";
  FILE* file = fopen(temporary.c_str(), "wb");
  if (!file) return;  // read-only install: parsing again is the only cost

  resource->resourceMutex.lock();
  std::vector<MaterializedTriangleMesh>& meshes =
      resource->GetResource()->GetTriangleMeshesRef();

  for (MaterializedTriangleMesh& mesh : meshes) {
    if (!TextureIsOnDisk(TextureName(mesh.material.diffuseTexture))) {
      Log(e_Notice, "asecache", "SaveGeometryCache",
          "no cache for " + aseFilename + ": texture not on disk: '" +
              TextureName(mesh.material.diffuseTexture) + "'");
      resource->resourceMutex.unlock();
      fclose(file);
      remove(temporary.c_str());
      return;
    }
    if (!TextureIsOnDisk(TextureName(mesh.material.diffuseTexture)) ||
        !TextureIsOnDisk(TextureName(mesh.material.normalTexture)) ||
        !TextureIsOnDisk(TextureName(mesh.material.specularTexture)) ||
        !TextureIsOnDisk(TextureName(mesh.material.illuminationTexture))) {
      resource->resourceMutex.unlock();
      fclose(file);
      remove(temporary.c_str());
      return;
    }
  }

  fwrite(kMagic, 1, 4, file);
  fwrite(&kVersion, sizeof(kVersion), 1, file);
  fwrite(&source, sizeof(source), 1, file);
  const unsigned int meshCount = (unsigned int)meshes.size();
  fwrite(&meshCount, sizeof(meshCount), 1, file);

  for (MaterializedTriangleMesh& mesh : meshes) {
    WriteString(file, TextureName(mesh.material.diffuseTexture));
    WriteString(file, TextureName(mesh.material.normalTexture));
    WriteString(file, TextureName(mesh.material.specularTexture));
    WriteString(file, TextureName(mesh.material.illuminationTexture));
    fwrite(&mesh.material.shininess, sizeof(float), 1, file);
    fwrite(&mesh.material.specular_amount, sizeof(float), 1, file);
    fwrite(mesh.material.self_illumination.coords, sizeof(float), 3, file);

    fwrite(&mesh.verticesDataSize, sizeof(mesh.verticesDataSize), 1, file);
    fwrite(mesh.vertices, sizeof(float),
           (size_t)mesh.verticesDataSize * kFloatsPerVertex, file);
    const unsigned int indexCount = (unsigned int)mesh.indices.size();
    fwrite(&indexCount, sizeof(indexCount), 1, file);
    if (indexCount) fwrite(&mesh.indices[0], sizeof(unsigned int), indexCount, file);
  }
  resource->resourceMutex.unlock();

  const bool wrote = (ferror(file) == 0);
  fclose(file);
  if (wrote) {
    rename(temporary.c_str(), path.c_str());
    Log(e_Notice, "asecache", "SaveGeometryCache",
        "wrote " + path + " (" + int_to_str((int)meshCount) + " meshes)");
  } else {
    remove(temporary.c_str());
    Log(e_Warning, "asecache", "SaveGeometryCache", "could not write " + path);
  }
}

}  // namespace blunted
