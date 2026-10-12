#include "gdext/pes_model.hpp"

#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/shader.hpp>
#include <godot_cpp/classes/shader_material.hpp>
#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <string_view>
#include <unordered_map>

#include "gdext/pes_mesh.hpp"
#include "gdext/pes_texture.hpp"
#include "pes/fmdl.hpp"
#include "pes/fpk.hpp"
#include "pes/pack_rules.hpp"

namespace godot {
namespace {

constexpr std::string_view FMDL_EXT = ".fmdl";
constexpr const char* BLIN_SHADER = "res://shaders/pes_blin.gdshader";
constexpr const char* CONSTANT_SHADER = "res://shaders/pes_constant.gdshader";
constexpr const char* CONSTANT_BLEND_SHADER = "res://shaders/pes_constant_blend.gdshader";
constexpr const char* PITCH_SHADER = "res://shaders/pes_pitch.gdshader";
constexpr const char* BASE_ROLE = "Base_Tex_SRGB";
constexpr int NO_FACE = 0;  // static models have no face folder to fall back on

// Fox technique -> shader. ConstantSRGB is unlit; "_Solid" is the opaque
// variant. ponytail: the fox3DD* family (Blin, Blin_LNM/NrmUV, GGX, GGX_TexAnim,
// TempolaryAlbedoView) all draw as Blin until their disassembly is mapped.
const char* ShaderFor(const std::string& technique) {
  if (technique == "pes3DDF_Pitch2020") return PITCH_SHADER;
  if (technique == "fox3DFW_ConstantSRGB_NDR_Solid") return CONSTANT_SHADER;
  if (technique == "fox3DFW_ConstantSRGB_NDR") return CONSTANT_BLEND_SHADER;
  if (technique.rfind("fox3DD", 0) == 0) return BLIN_SHADER;
  return nullptr;
}

bool EndsWith(std::string_view s, std::string_view tail) {
  return s.size() >= tail.size() && s.compare(s.size() - tail.size(), tail.size(), tail) == 0;
}

class StaticBuild {
 public:
  explicit StaticBuild(const pes::PesVfs& vfs) : vfs_(vfs), resolver_(vfs, NO_FACE) {}

  Ref<ShaderMaterial> Material(const pes::Fmdl& m, const pes::FmdlMaterial& fm) {
    const char* shaderPath = ShaderFor(fm.technique);
    if (!shaderPath) {
      UtilityFunctions::push_warning("PES technique without a shader, drawn as Blin: ", ToGd(fm.technique));
      shaderPath = BLIN_SHADER;
    }
    Ref<ShaderMaterial> mat;
    mat.instantiate();
    mat->set_name(ToGd(fm.name));
    mat->set_shader(ResourceLoader::get_singleton()->load(shaderPath));  // cached by the loader
    for (const auto& [name, v] : fm.params) mat->set_shader_parameter(ToGd(name), Vector4(v.x, v.y, v.z, v.w));
    for (const auto& [role, ti] : fm.textures) {
      if (ti < 0 || ti >= int(m.textures.size())) continue;
      const auto* t = Texture(m.textures[ti]);
      if (!t) continue;
      mat->set_shader_parameter(ToGd(role), t->tex);
      mat->set_shader_parameter(ToGd("has_" + role), true);
      if (role == BASE_ROLE) mat->set_shader_parameter("alpha_tested", t->hasAlpha);
    }
    return mat;
  }

 private:
  struct Loaded {
    Ref<godot::Texture> tex;
    bool hasAlpha;
  };
  const Loaded* Texture(const pes::FmdlTexture& t) {
    const std::string path = resolver_.Resolve(t);
    if (path.empty()) {
      UtilityFunctions::push_warning("PES texture not found: ", ToGd(t.directory + t.filename));
      return nullptr;
    }
    auto it = cache_.find(path);
    if (it != cache_.end()) return it->second ? &*it->second : nullptr;
    auto& slot = cache_[path];
    std::string err;
    Ref<godot::Texture> tex = LoadPesTexture(vfs_, path, &err);
    if (tex.is_null()) {
      UtilityFunctions::push_warning("PES texture unreadable: ", ToGd(path), " (", ToGd(err), ")");
      return nullptr;
    }
    bool hasAlpha = false;
    if (Ref<Texture2D> t2 = tex; t2.is_valid()) {
      Ref<Image> img = t2->get_image();
      if (img.is_valid()) {
        if (img->is_compressed()) img->decompress();
        hasAlpha = img->detect_alpha() != Image::ALPHA_NONE;
      }
    }
    slot = Loaded{tex, hasAlpha};
    return &*slot;
  }

  const pes::PesVfs& vfs_;
  pes::TextureResolver resolver_;
  std::unordered_map<std::string, std::optional<Loaded>> cache_;  // by VFS path
};

}  // namespace

void PesModelBuilder::_bind_methods() {
  ClassDB::bind_method(D_METHOD("build_model", "library", "fpk_path", "member"), &PesModelBuilder::build_model);
}

Node3D* PesModelBuilder::build_model(const Ref<PesLibrary>& library, const String& fpk_path, const String& member) const {
  if (library.is_null()) return nullptr;
  const auto& vfs = library->vfs();
  std::string err;
  const std::string packPath = FromGd(fpk_path), want = FromGd(member);
  auto pack = vfs.Read(packPath, &err);
  if (!pack) return nullptr;
  auto fpk = pes::FpkArchive::Parse(*pack, &err);
  if (!fpk) {
    UtilityFunctions::push_error("PES pack unreadable: ", fpk_path, " (", ToGd(err), ")");
    return nullptr;
  }
  StaticBuild build(vfs);
  Node3D* root = memnew(Node3D);
  root->set_name(ToGd(packPath.substr(packPath.find_last_of('/') + 1)));
  int built = 0;
  for (const auto& e : fpk->entries) {
    if (!EndsWith(e.path, FMDL_EXT) || (!want.empty() && !EndsWith(e.path, want))) continue;
    auto m = pes::ParseFmdl(std::span(*pack).subspan(e.offset, e.size), &err);
    if (!m) {
      UtilityFunctions::push_warning("PES model unreadable: ", ToGd(e.path), " (", ToGd(err), ")");
      continue;
    }
    std::vector<Ref<ShaderMaterial>> materials;
    for (const auto& fm : m->materials) materials.push_back(build.Material(*m, fm));
    for (const auto& mesh : m->meshes) {
      if (mesh.indices.empty()) continue;
      Ref<ArrayMesh> am;
      am.instantiate();
      am->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, FmdlSurfaceArrays(mesh));
      if (mesh.materialIndex >= 0 && mesh.materialIndex < int(materials.size()))
        am->surface_set_material(0, materials[mesh.materialIndex]);
      MeshInstance3D* mi = memnew(MeshInstance3D);
      mi->set_mesh(am);
      root->add_child(mi);
      ++built;
    }
  }
  if (built == 0) {
    memdelete(root);
    return nullptr;
  }
  return root;
}

}  // namespace godot
