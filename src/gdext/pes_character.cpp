#include "gdext/pes_character.hpp"

#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/skeleton3d.hpp>
#include <godot_cpp/classes/skin.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <algorithm>
#include <unordered_map>

#include "gdext/pes_texture.hpp"
#include "pes/fmdl.hpp"
#include "pes/fpk.hpp"
#include "pes/pack_rules.hpp"

namespace godot {
namespace {

using pes::TextureStem;

// Fox names shaders after their BRDF; "Constant" is the unlit one (the flat
// look of the 4cc anime models), so it renders unshaded.
constexpr const char* FOX_UNLIT_SHADER_MARK = "constant";
// The kit slot wears the team's uniform at run time; kits are the next plan.
const Color kKitPlaceholderAlbedo(0.5f, 0.5f, 0.5f);
constexpr float kAlphaScissorThreshold = 0.5f;
constexpr int MAX_INFLUENCES = 4;  // PES and Godot both skin with four

std::string Lower(std::string s) {
  for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

bool EndsWith(const std::string& s, const std::string& tail) {
  return s.size() >= tail.size() && s.compare(s.size() - tail.size(), tail.size(), tail) == 0;
}

struct Texture {
  Ref<ImageTexture> gpu;
  Ref<Image> rgba;  // decompressed copy, for alpha rules
  bool hasAlpha = false;
  int maxAlpha = 0;
};

class Build {
 public:
  Build(const pes::PesVfs& vfs, const pesrig::Rig& rig, int faceId)
      : vfs_(vfs), rig_(rig), faceId_(faceId), resolver_(vfs, faceId) {}

  Node3D* Run();

 private:
  const Texture* LoadTexture(const pes::FmdlTexture& t);
  Ref<StandardMaterial3D> MakeMaterial(const pes::Fmdl& m, int index, bool kitSlot);
  bool FacesSampleOpaque(const pes::FmdlMesh& mesh, const Texture& tex) const;
  Ref<ArrayMesh> MakeMesh(const pes::FmdlMesh& mesh, const std::vector<int>& jointOfBone, int* unbound);

  const pes::PesVfs& vfs_;
  const pesrig::Rig& rig_;
  int faceId_;
  pes::TextureResolver resolver_;
  std::unordered_map<std::string, std::optional<Texture>> textures_;
  int unresolved_ = 0;
};

const Texture* Build::LoadTexture(const pes::FmdlTexture& t) {
  const std::string key = Lower(t.directory + t.filename);
  auto it = textures_.find(key);
  if (it != textures_.end()) return it->second ? &*it->second : nullptr;
  auto& slot = textures_[key];
  const std::string path = resolver_.Resolve(t);
  if (path.empty()) {
    UtilityFunctions::push_warning("PES texture not found: ", ToGd(t.directory + t.filename));
    ++unresolved_;
    return nullptr;
  }
  std::string err;
  Ref<Image> img = LoadPesImage(vfs_, path, &err);
  if (img.is_null() || img->is_empty()) {
    UtilityFunctions::push_warning("PES texture unreadable: ", ToGd(path), " (", ToGd(err), ")");
    ++unresolved_;
    return nullptr;
  }
  Texture tex;
  tex.gpu = ImageTexture::create_from_image(img);
  tex.rgba = Image::create_from_data(img->get_width(), img->get_height(), img->has_mipmaps(), img->get_format(),
                                     img->get_data());
  tex.rgba->decompress();
  tex.rgba->clear_mipmaps();
  tex.rgba->convert(Image::FORMAT_RGBA8);
  const PackedByteArray px = tex.rgba->get_data();
  constexpr uint8_t OPAQUE = 255;
  for (int64_t i = 3; i < px.size(); i += 4) tex.maxAlpha = std::max<int>(tex.maxAlpha, px[i]), tex.hasAlpha |= px[i] < OPAQUE;
  slot = std::move(tex);
  return &*slot;
}

// Face-centroid UVs decide, as in GF's mesh_faces_sample_opaque.
bool Build::FacesSampleOpaque(const pes::FmdlMesh& mesh, const Texture& tex) const {
  if (mesh.uv0.size() != mesh.pos.size()) return true;
  const int w = tex.rgba->get_width(), h = tex.rgba->get_height();
  const PackedByteArray px = tex.rgba->get_data();
  for (size_t f = 0; f + 2 < mesh.indices.size(); f += 3) {
    float u = 0, v = 0;
    for (int k = 0; k < 3; ++k) u += mesh.uv0[mesh.indices[f + k]].x / 3, v += mesh.uv0[mesh.indices[f + k]].y / 3;
    const int x = std::clamp(int(u * (w - 1)), 0, w - 1), y = std::clamp(int(v * (h - 1)), 0, h - 1);
    if (px[(int64_t(y) * w + x) * 4 + 3] != 0) return true;
  }
  return false;
}

Ref<StandardMaterial3D> Build::MakeMaterial(const pes::Fmdl& m, int index, bool kitSlot) {
  Ref<StandardMaterial3D> mat;
  mat.instantiate();
  if (index < 0 || index >= int(m.materials.size())) return mat;
  const auto& fm = m.materials[index];
  if (Lower(fm.shader).find(FOX_UNLIT_SHADER_MARK) != std::string::npos)
    mat->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
  if (kitSlot) {
    mat->set_albedo(kKitPlaceholderAlbedo);
    return mat;
  }
  for (const auto& [role, texIndex] : fm.textures) {
    if (texIndex < 0 || texIndex >= int(m.textures.size())) continue;
    const std::string stem = Lower(TextureStem(m.textures[texIndex].filename));
    if (pes::IsPlaceholderTexture(stem)) continue;  // dummy_nrm / dummy_srm stand-ins
    const bool base = role.find("Base_Tex") != std::string::npos || EndsWith(role, "_SRGB");
    const bool normal = role.find("NormalMap") != std::string::npos || EndsWith(stem, "_nrm");
    const bool spec = role.find("SpecularMap") != std::string::npos || EndsWith(stem, "_srm");
    if (!base && !normal && !spec) continue;
    const Texture* tex = LoadTexture(m.textures[texIndex]);
    if (!tex) continue;
    if (base) {
      mat->set_texture(BaseMaterial3D::TEXTURE_ALBEDO, tex->gpu);
      if (tex->hasAlpha) {
        mat->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA_SCISSOR);
        mat->set_alpha_scissor_threshold(kAlphaScissorThreshold);
      }
    } else if (normal) {
      mat->set_feature(BaseMaterial3D::FEATURE_NORMAL_MAPPING, true);
      mat->set_texture(BaseMaterial3D::TEXTURE_NORMAL, tex->gpu);
    } else {
      // SRM channel layout unverified: G = roughness, B = metalness.
      mat->set_texture(BaseMaterial3D::TEXTURE_ROUGHNESS, tex->gpu);
      mat->set_roughness_texture_channel(BaseMaterial3D::TEXTURE_CHANNEL_GREEN);
      mat->set_texture(BaseMaterial3D::TEXTURE_METALLIC, tex->gpu);
      mat->set_metallic_texture_channel(BaseMaterial3D::TEXTURE_CHANNEL_BLUE);
    }
  }
  return mat;
}

Ref<ArrayMesh> Build::MakeMesh(const pes::FmdlMesh& mesh, const std::vector<int>& jointOfBone, int* unbound) {
  const int64_t n = int64_t(mesh.pos.size());
  PackedVector3Array pos, nrm;
  PackedVector2Array uv;
  PackedFloat32Array tan, weights;
  PackedInt32Array bones, indices;
  pos.resize(n);
  bones.resize(n * MAX_INFLUENCES);
  weights.resize(n * MAX_INFLUENCES);
  for (int64_t i = 0; i < n; ++i) {
    pos.set(i, Vector3(mesh.pos[i].x, mesh.pos[i].y, mesh.pos[i].z));
    // Merge influences that resolve to the same rig joint, strongest first.
    std::vector<std::pair<int, float>> inf;
    if (size_t(i) < mesh.joints.size() && size_t(i) < mesh.weights.size()) {
      const float w[MAX_INFLUENCES] = {mesh.weights[i].x, mesh.weights[i].y, mesh.weights[i].z, mesh.weights[i].w};
      for (int k = 0; k < MAX_INFLUENCES; ++k) {
        const int local = mesh.joints[i][k];
        if (w[k] <= 0 || local >= int(mesh.boneGroup.size())) continue;
        const int bone = mesh.boneGroup[local];
        const int joint = bone < int(jointOfBone.size()) ? jointOfBone[bone] : -1;
        if (joint < 0) continue;
        auto hit = std::find_if(inf.begin(), inf.end(), [&](const auto& p) { return p.first == joint; });
        if (hit != inf.end()) hit->second += w[k];
        else inf.emplace_back(joint, w[k]);
      }
    }
    if (inf.empty()) {
      inf.emplace_back(0, 1.0f);  // the body joint
      ++*unbound;
    }
    std::sort(inf.begin(), inf.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
    float total = 0;
    for (const auto& p : inf) total += p.second;
    for (int k = 0; k < MAX_INFLUENCES; ++k) {
      const bool has = k < int(inf.size());
      bones.set(i * MAX_INFLUENCES + k, has ? inf[k].first : 0);
      weights.set(i * MAX_INFLUENCES + k, has ? inf[k].second / total : 0.0f);
    }
  }
  if (mesh.nrm.size() == size_t(n)) {
    nrm.resize(n);
    for (int64_t i = 0; i < n; ++i) nrm.set(i, Vector3(mesh.nrm[i].x, mesh.nrm[i].y, mesh.nrm[i].z).normalized());
  }
  if (mesh.tan.size() == size_t(n) && !nrm.is_empty()) {
    tan.resize(n * 4);
    for (int64_t i = 0; i < n; ++i) {
      const Vector3 t = Vector3(mesh.tan[i].x, mesh.tan[i].y, mesh.tan[i].z).normalized();
      tan.set(i * 4, t.x), tan.set(i * 4 + 1, t.y), tan.set(i * 4 + 2, t.z);
      tan.set(i * 4 + 3, mesh.tan[i].w < 0 ? -1.0f : 1.0f);
    }
  }
  if (mesh.uv0.size() == size_t(n)) {
    uv.resize(n);
    for (int64_t i = 0; i < n; ++i) uv.set(i, Vector2(mesh.uv0[i].x, mesh.uv0[i].y));
  }
  // Fox and Godot agree on front-face winding; indices pass through as stored.
  indices.resize(int64_t(mesh.indices.size()));
  for (size_t k = 0; k < mesh.indices.size(); ++k) indices.set(int64_t(k), int32_t(mesh.indices[k]));
  Array arrays;
  arrays.resize(Mesh::ARRAY_MAX);
  arrays[Mesh::ARRAY_VERTEX] = pos;
  if (!nrm.is_empty()) arrays[Mesh::ARRAY_NORMAL] = nrm;
  if (!tan.is_empty()) arrays[Mesh::ARRAY_TANGENT] = tan;
  if (!uv.is_empty()) arrays[Mesh::ARRAY_TEX_UV] = uv;
  arrays[Mesh::ARRAY_BONES] = bones;
  arrays[Mesh::ARRAY_WEIGHTS] = weights;
  arrays[Mesh::ARRAY_INDEX] = indices;
  Ref<ArrayMesh> am;
  am.instantiate();
  am->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays);
  return am;
}

Node3D* Build::Run() {
  const std::string packPath = pes::FacePackPath(faceId_);
  std::string err;
  auto pack = vfs_.Read(packPath, &err);
  if (!pack) {
    UtilityFunctions::push_error("PES face pack missing: ", ToGd(packPath));
    return nullptr;
  }
  auto fpk = pes::FpkArchive::Parse(*pack, &err);
  if (!fpk) {
    UtilityFunctions::push_error("PES face pack unreadable: ", ToGd(packPath), " (", ToGd(err), ")");
    return nullptr;
  }

  Node3D* root = memnew(Node3D);
  root->set_name("Character");
  Skeleton3D* skeleton = memnew(Skeleton3D);
  skeleton->set_name("Skeleton");
  root->add_child(skeleton);
  Ref<Skin> skin;
  skin.instantiate();
  for (size_t i = 0; i < rig_.bones.size(); ++i) {
    const auto& b = rig_.bones[i];
    const int id = skeleton->add_bone(ToGd(b.gfName));
    skeleton->set_bone_parent(id, b.parent);
    skeleton->set_bone_rest(id, Transform3D(Basis(), Vector3(b.animLocalPos.x, b.animLocalPos.y, b.animLocalPos.z)));
    // Render pose -> anim pose: v_anim = anim_b + W_b (v_render - render_b).
    const Quaternion w(b.align.x, b.align.y, b.align.z, b.align.w);
    const Vector3 render(b.renderGlobalPos.x, b.renderGlobalPos.y, b.renderGlobalPos.z);
    skin->add_bind(id, Transform3D(Basis(w.inverse()), render).affine_inverse());
  }
  skeleton->reset_bone_poses();

  int kept = 0, dropped = 0, unbound = 0;
  for (const auto& entry : fpk->entries) {
    if (!EndsWith(Lower(entry.path), ".fmdl")) continue;
    auto model = pes::ParseFmdl(std::span(*pack).subspan(entry.offset, entry.size), &err);
    if (!model) {
      UtilityFunctions::push_warning("fmdl unreadable: ", ToGd(entry.path), " (", ToGd(err), ")");
      continue;
    }
    std::vector<int> jointOfBone;
    for (const auto& bone : model->bones) {
      auto id = pesrig::ResolveBone(rig_, bone.name, &bone.worldPos);
      jointOfBone.push_back(id ? *id : -1);
    }
    std::unordered_map<int, Ref<StandardMaterial3D>> materials;
    for (size_t mi = 0; mi < model->meshes.size(); ++mi) {
      const auto& mesh = model->meshes[mi];
      const pes::FmdlMaterial* fm =
          mesh.materialIndex >= 0 && mesh.materialIndex < int(model->materials.size()) ? &model->materials[mesh.materialIndex] : nullptr;
      const pes::FmdlTexture* base = nullptr;
      if (fm)
        for (const auto& [role, ti] : fm->textures)
          if ((role.find("Base_Tex") != std::string::npos || EndsWith(role, "_SRGB")) && ti >= 0 && ti < int(model->textures.size()))
            base = &model->textures[ti];
      const std::string baseStem = base ? TextureStem(base->filename) : "";
      const std::string label = entry.path + "#" + std::to_string(mi) + " (" + (fm ? fm->name : "?") + ", " + baseStem + ")";
      std::string reason;
      const bool kitSlot = pes::IsKitSlotTexture(baseStem);
      if (pes::IsNonRenderPass(fm ? fm->name : "", baseStem)) reason = "non-render pass";
      else if (pes::IsEffectTexture(baseStem)) reason = "effect texture";
      else if (base && !kitSlot) {
        if (const Texture* tex = LoadTexture(*base)) {
          if (tex->maxAlpha <= pes::HIDER_MAX_ALPHA) reason = "kit hider (transparent texture)";
          else if (pes::IsPlaceholderTexture(baseStem) && !FacesSampleOpaque(mesh, *tex))
            reason = "unresolved swap mesh (no opaque face)";
        }
      }
      if (!reason.empty()) {
        UtilityFunctions::print("  drop ", ToGd(label), ": ", ToGd(reason));
        ++dropped;
        continue;
      }
      auto& mat = materials[mesh.materialIndex];
      if (mat.is_null()) mat = MakeMaterial(*model, mesh.materialIndex, kitSlot);
      Ref<ArrayMesh> am = MakeMesh(mesh, jointOfBone, &unbound);
      am->surface_set_material(0, mat);
      MeshInstance3D* mi3d = memnew(MeshInstance3D);
      mi3d->set_name(ToGd("mesh_" + TextureStem(entry.path) + "_" + std::to_string(mi)));
      mi3d->set_mesh(am);
      mi3d->set_skin(skin);
      skeleton->add_child(mi3d);
      mi3d->set_skeleton_path(NodePath(".."));
      UtilityFunctions::print("  keep ", ToGd(label), ": ", int64_t(mesh.pos.size()), " vertices");
      ++kept;
    }
  }
  UtilityFunctions::print("face ", faceId_, ": ", kept, " meshes kept, ", dropped, " dropped, ", unbound,
                          " vertices without a resolvable joint (bound to body), ", unresolved_, " textures unresolved");
  return root;
}

}  // namespace

void PesCharacterBuilder::_bind_methods() {
  ClassDB::bind_method(D_METHOD("build_face_model", "library", "face_id"), &PesCharacterBuilder::build_face_model);
}

Node3D* PesCharacterBuilder::build_face_model(const Ref<PesLibrary>& library, int face_id) {
  if (library.is_null() || !library->rig()) {
    UtilityFunctions::push_error("PesCharacterBuilder: library is not open");
    return nullptr;
  }
  return Build(library->vfs(), *library->rig(), face_id).Run();
}

}  // namespace godot
