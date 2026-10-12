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
#include <cstring>
#include <unordered_map>

#include "gdext/pes_texture.hpp"
#include "pes/fmdl.hpp"
#include "pes/fpk.hpp"
#include "pes/pack_rules.hpp"
#include "pes/skl.hpp"
#include "pes/uniform.hpp"

namespace godot {
namespace {

using pes::TextureStem;

// Fox names shaders after their BRDF; "Constant" is the unlit one (the flat
// look of the 4cc anime models), so it renders unshaded.
constexpr const char* FOX_UNLIT_SHADER_MARK = "constant";
// The kit slot wears the team's uniform at run time; kits are the next plan.
const Color kKitPlaceholderAlbedo(0.5f, 0.5f, 0.5f);
constexpr float kAlphaScissorThreshold = 0.5f;
// Fox material names, as bound by the running game (pes-kit-assembly.md 7.2).
constexpr const char* UNIFORM_MATERIAL_PREFIX = "uni_";
constexpr const char* ARM_MATERIAL = "arm_mat";
constexpr const char* KIT_TEXTURE_DIR = "Asset/model/character/uniform/texture/#windx11/";
constexpr const char* FACE_ARM_SKIN = "arm_bsm.ftex";  // a face folder's own arm skin
constexpr const char* THIGH_MATERIAL = "thigh_mat";
constexpr const char* SOCKS_MATERIAL = "uni_socks";
constexpr const char* TEXTURE_EXT = ".ftex";
constexpr const char* KIT_SRM_SUFFIX = "_srm.ftex";
// Shirt, collar, sleeve and pants shaders draw an opaque kit at 85% of its
// texel colour; socks at full (pes-re-shaders.md 3.1, 3.2 / section 7).
constexpr float kOpaqueKitAlbedoScale = 0.85f;
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

// FaceOnly: a face pack alone, cleaned with GF's 4cc pack rules (hiders,
// effect textures, grey kit slot). Player: the parts PES selects, drawn as PES
// draws them, with the kit atlas on the uniform.
enum class Mode { FaceOnly, Player };

class Build {
 public:
  Build(const pes::PesVfs& vfs, const pesrig::Rig& rig, int faceId, Mode mode)
      : vfs_(vfs), rig_(rig), faceId_(faceId), mode_(mode), resolver_(vfs, faceId) {}

  Node3D* RunFace();
  Node3D* RunPlayer(int teamId, int kitIndex);

 private:
  void Begin();
  bool AddPack(const std::string& packPath);
  void AddModel(std::span<const uint8_t> blob, const std::string& entryPath, const Ref<Skin>& skin);
  Node3D* Finish();
  const Texture* LoadTexture(const pes::FmdlTexture& t);
  const Texture* LoadTexturePath(const std::string& path);
  Ref<StandardMaterial3D> MakeMaterial(const pes::Fmdl& m, int index, bool kitSlot);
  Ref<StandardMaterial3D> MakeUniformMaterial(Ref<StandardMaterial3D> mat, const std::string& name);
  bool FacesSampleOpaque(const pes::FmdlMesh& mesh, const Texture& tex) const;
  Ref<ArrayMesh> MakeMesh(const pes::FmdlMesh& mesh, const std::vector<int>& jointOfBone, int* unbound);

  const pes::PesVfs& vfs_;
  const pesrig::Rig& rig_;
  int faceId_;
  Mode mode_;
  pes::TextureResolver resolver_;
  std::unordered_map<std::string, std::optional<Texture>> textures_;  // by VFS path
  std::unordered_map<std::string, std::string> resolved_;             // fmdl reference -> VFS path
  int unresolved_ = 0;
  std::string kitAtlasPath_, armSkinPath_, thighSkinPath_;  // Player mode overrides
  Node3D* root_ = nullptr;
  Skeleton3D* skeleton_ = nullptr;
  Ref<Skin> skin_;      // parts authored on the render pose (body.skl): aligned by W
  Ref<Skin> animSkin_;  // parts authored on the anim pose (some boots.skl): no alignment
  int kept_ = 0, dropped_ = 0, unbound_ = 0;
};

const Texture* Build::LoadTexture(const pes::FmdlTexture& t) {
  const std::string key = Lower(t.directory + t.filename);
  auto it = resolved_.find(key);
  if (it == resolved_.end()) {
    it = resolved_.emplace(key, resolver_.Resolve(t)).first;
    if (it->second.empty()) {
      UtilityFunctions::push_warning("PES texture not found: ", ToGd(t.directory + t.filename));
      ++unresolved_;
    }
  }
  return it->second.empty() ? nullptr : LoadTexturePath(it->second);
}

// Cached by VFS path; the GPU texture is named after it.
const Texture* Build::LoadTexturePath(const std::string& path) {
  auto it = textures_.find(path);
  if (it != textures_.end()) return it->second ? &*it->second : nullptr;
  auto& slot = textures_[path];
  std::string err;
  Ref<Image> img = LoadPesImage(vfs_, path, &err);
  if (img.is_null() || img->is_empty()) {
    UtilityFunctions::push_warning("PES texture unreadable: ", ToGd(path), " (", ToGd(err), ")");
    ++unresolved_;
    return nullptr;
  }
  Texture tex;
  tex.gpu = ImageTexture::create_from_image(img);
  tex.gpu->set_name(ToGd(path));
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
  mat->set_name(ToGd(fm.name));
  if (Lower(fm.shader).find(FOX_UNLIT_SHADER_MARK) != std::string::npos)
    mat->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
  if (kitSlot) {
    mat->set_albedo(kKitPlaceholderAlbedo);
    return mat;
  }
  // Player mode, as bound live: the engine binds the uniform's kit maps at run
  // time (the fmdl carries only Pattern_Tex_LIN), and a face folder's arm_bsm
  // replaces the arm skin. Arm and thigh fmdls carry a dummy Base or none; the
  // engine binds the skin, opaque (arm_mat.001 on hand_r is the same material).
  if (mode_ == Mode::Player && fm.name.starts_with(UNIFORM_MATERIAL_PREFIX)) return MakeUniformMaterial(mat, fm.name);
  std::string skin;
  if (mode_ == Mode::Player && fm.name.starts_with(ARM_MATERIAL)) skin = armSkinPath_;
  else if (mode_ == Mode::Player && fm.name.starts_with(THIGH_MATERIAL)) skin = thighSkinPath_;
  if (!skin.empty())
    if (const Texture* tex = LoadTexturePath(skin)) mat->set_texture(BaseMaterial3D::TEXTURE_ALBEDO, tex->gpu);
  for (const auto& [role, texIndex] : fm.textures) {
    if (texIndex < 0 || texIndex >= int(m.textures.size())) continue;
    const std::string stem = Lower(TextureStem(m.textures[texIndex].filename));
    const bool base = role.find("Base_Tex") != std::string::npos || EndsWith(role, "_SRGB");
    if ((base && !skin.empty()) || pes::IsPlaceholderTexture(stem)) continue;  // dummy_nrm / dummy_srm
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
      // SRM: R specular intensity, G roughness, B unused (pes-re-shaders.md 3).
      mat->set_texture(BaseMaterial3D::TEXTURE_ROUGHNESS, tex->gpu);
      mat->set_roughness_texture_channel(BaseMaterial3D::TEXTURE_CHANNEL_GREEN);
    }
  }
  return mat;
}

// pes3ddf_{shirt_nb,collar,sleeve,pants_nb}_nc and pes3DDF_Uniform: kit atlas
// on UV0, opaque (its alpha masks kit against fabric weave, it is not
// transparency), the kit's _srm when it has one. ponytail: wrinkle normals and
// the weave are Texture2DArrays the ftex reader does not decode yet; until it
// does, cloth shades flat.
Ref<StandardMaterial3D> Build::MakeUniformMaterial(Ref<StandardMaterial3D> mat, const std::string& name) {
  if (const Texture* tex = LoadTexturePath(kitAtlasPath_)) mat->set_texture(BaseMaterial3D::TEXTURE_ALBEDO, tex->gpu);
  if (name != SOCKS_MATERIAL) mat->set_albedo(Color(kOpaqueKitAlbedoScale, kOpaqueKitAlbedoScale, kOpaqueKitAlbedoScale));
  const std::string srm = kitAtlasPath_.substr(0, kitAtlasPath_.size() - std::strlen(TEXTURE_EXT)) + KIT_SRM_SUFFIX;
  if (vfs_.Exists(srm))
    if (const Texture* tex = LoadTexturePath(srm)) {
      mat->set_texture(BaseMaterial3D::TEXTURE_ROUGHNESS, tex->gpu);
      mat->set_roughness_texture_channel(BaseMaterial3D::TEXTURE_CHANNEL_GREEN);
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

void Build::Begin() {
  root_ = memnew(Node3D);
  root_->set_name("Character");
  skeleton_ = memnew(Skeleton3D);
  skeleton_->set_name("Skeleton");
  root_->add_child(skeleton_);
  skin_.instantiate();
  animSkin_.instantiate();
  for (size_t i = 0; i < rig_.bones.size(); ++i) {
    const auto& b = rig_.bones[i];
    const int id = skeleton_->add_bone(ToGd(b.gfName));
    skeleton_->set_bone_parent(id, b.parent);
    skeleton_->set_bone_rest(id, Transform3D(Basis(), Vector3(b.animLocalPos.x, b.animLocalPos.y, b.animLocalPos.z)));
    // Render pose -> anim pose: v_anim = anim_b + W_b (v_render - render_b).
    const Quaternion w(b.align.x, b.align.y, b.align.z, b.align.w);
    const Vector3 render(b.renderGlobalPos.x, b.renderGlobalPos.y, b.renderGlobalPos.z);
    skin_->add_bind(id, Transform3D(Basis(w.inverse()), render).affine_inverse());
    const Vector3 anim(b.animGlobalPos.x, b.animGlobalPos.y, b.animGlobalPos.z);
    animSkin_->add_bind(id, Transform3D(Basis(), anim).affine_inverse());
  }
  skeleton_->reset_bone_poses();
}

bool Build::AddPack(const std::string& packPath) {
  std::string err;
  auto pack = vfs_.Read(packPath, &err);
  if (!pack) {
    UtilityFunctions::push_error("PES pack missing: ", ToGd(packPath));
    return false;
  }
  auto fpk = pes::FpkArchive::Parse(*pack, &err);
  if (!fpk) {
    UtilityFunctions::push_error("PES pack unreadable: ", ToGd(packPath), " (", ToGd(err), ")");
    return false;
  }
  // A pack that ships its own skeleton says which PES skeleton its meshes were
  // authored on (boots: 883 render pose, 65 anim pose in this install).
  Ref<Skin> skin = skin_;
  for (const auto& entry : fpk->entries)
    if (EndsWith(Lower(entry.path), ".skl")) {
      auto skl = pes::ParseSkl(std::span(*pack).subspan(entry.offset, entry.size), &err);
      const pesrig::PartPose pose = skl ? pesrig::ClassifyPartPose(rig_, *skl) : pesrig::PartPose::Unknown;
      if (pose == pesrig::PartPose::Anim) skin = animSkin_;
      if (pose == pesrig::PartPose::Unknown)
        UtilityFunctions::push_warning("PES pack skeleton on neither pose, bound as render pose: ", ToGd(packPath));
    }
  for (const auto& entry : fpk->entries)
    if (EndsWith(Lower(entry.path), ".fmdl")) AddModel(std::span(*pack).subspan(entry.offset, entry.size), entry.path, skin);
  return true;
}

void Build::AddModel(std::span<const uint8_t> blob, const std::string& entryPath, const Ref<Skin>& skin) {
  std::string err;
  auto model = pes::ParseFmdl(blob, &err);
  if (!model) {
    UtilityFunctions::push_warning("fmdl unreadable: ", ToGd(entryPath), " (", ToGd(err), ")");
    return;
  }
  const std::string part = TextureStem(entryPath);
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
    const std::string label = entryPath + "#" + std::to_string(mi) + " (" + (fm ? fm->name : "?") + ", " + baseStem + ")";
    std::string reason;
    // Outline/antiblur passes need their own shaders: a known gap in both modes.
    const bool kitSlot = mode_ == Mode::FaceOnly && pes::IsKitSlotTexture(baseStem);
    if (pes::IsNonRenderPass(fm ? fm->name : "", baseStem)) reason = "non-render pass";
    else if (mode_ == Mode::FaceOnly && pes::IsEffectTexture(baseStem)) reason = "effect texture";
    else if (mode_ == Mode::FaceOnly && base && !kitSlot) {
      if (const Texture* tex = LoadTexture(*base)) {
        if (tex->maxAlpha <= pes::HIDER_MAX_ALPHA) reason = "kit hider (transparent texture)";
        else if (pes::IsPlaceholderTexture(baseStem) && !FacesSampleOpaque(mesh, *tex))
          reason = "unresolved swap mesh (no opaque face)";
      }
    }
    if (!reason.empty()) {
      UtilityFunctions::print("  drop ", ToGd(label), ": ", ToGd(reason));
      ++dropped_;
      continue;
    }
    auto& mat = materials[mesh.materialIndex];
    if (mat.is_null()) mat = MakeMaterial(*model, mesh.materialIndex, kitSlot);
    Ref<ArrayMesh> am = MakeMesh(mesh, jointOfBone, &unbound_);
    am->surface_set_material(0, mat);
    MeshInstance3D* mi3d = memnew(MeshInstance3D);
    mi3d->set_name(ToGd("mesh_" + part + "_" + std::to_string(mi)));
    mi3d->set_meta("part", ToGd(part));
    mi3d->set_mesh(am);
    mi3d->set_skin(skin);
    skeleton_->add_child(mi3d);
    mi3d->set_skeleton_path(NodePath(".."));
    UtilityFunctions::print("  keep ", ToGd(label), ": ", int64_t(mesh.pos.size()), " vertices");
    ++kept_;
  }
}

Node3D* Build::Finish() {
  UtilityFunctions::print("character ", faceId_, ": ", kept_, " meshes kept, ", dropped_, " dropped, ", unbound_,
                          " vertices without a resolvable joint (bound to body), ", unresolved_, " textures unresolved");
  return root_;
}

Node3D* Build::RunFace() {
  Begin();
  if (!AddPack(pes::FacePackPath(faceId_))) {
    memdelete(root_);
    return nullptr;
  }
  return Finish();
}

// The kit config comes from UniformParameter.bin, the parts from the PES
// selection rules, the head from the face pack.
Node3D* Build::RunPlayer(int teamId, int kitIndex) {
  std::string err;
  auto table = vfs_.Read(pes::kUniformParameterPath, &err);
  std::optional<pes::UniformTable> uniforms;
  if (table) uniforms = pes::ParseUniformTable(*table, &err);
  const std::string kitName = pes::RealUniName(teamId, kitIndex, false);
  auto record = uniforms ? uniforms->Find(kitName) : std::nullopt;
  auto kit = record ? pes::ParseRealUni(*record, &err) : std::nullopt;
  if (!kit) {
    UtilityFunctions::push_error("PES kit config missing: ", ToGd(kitName), " (", ToGd(err), ")");
    return nullptr;
  }
  kitAtlasPath_ = std::string(KIT_TEXTURE_DIR) + kit->kitFile + TEXTURE_EXT;
  // ponytail: the 4cc placeholder appearance for everyone until the EDIT save
  // is read (pes-kit-assembly.md 8.1).
  const pes::PlayerLook look;
  const std::string faceArm = pes::FaceTextureDir(faceId_) + FACE_ARM_SKIN;
  armSkinPath_ = vfs_.Exists(faceArm) ? faceArm : pes::ArmSkinPath(look.skin);
  thighSkinPath_ = pes::ThighSkinPath(look.skin);
  auto bootsBlob = vfs_.Read(pes::kBootsListPath, &err);
  auto bootsList = bootsBlob ? pes::ParseBootsList(*bootsBlob, &err) : std::nullopt;
  // 4cc players' ids equal their face ids (pes-kit-assembly.md 7.1).
  const int bootsId = pes::ResolveBootsId(look.bootsId, faceId_, bootsList ? *bootsList : pes::BootsList{});

  auto common = vfs_.Read(pes::kCommonPackagePath, &err);
  auto commonFpk = common ? pes::FpkArchive::Parse(*common, &err) : std::nullopt;
  if (!commonFpk) {
    UtilityFunctions::push_error("PES common package unreadable: ", ToGd(err));
    return nullptr;
  }
  Begin();
  if (!AddPack(pes::FacePackPath(faceId_))) {
    memdelete(root_);
    return nullptr;
  }
  for (const auto& part : pes::SelectUniformParts(*kit, look)) {
    if (part.inCommonPackage) {
      auto it = std::find_if(commonFpk->entries.begin(), commonFpk->entries.end(),
                             [&](const pes::FpkEntry& e) { return e.path.ends_with("/" + part.path); });
      if (it == commonFpk->entries.end()) {
        UtilityFunctions::push_warning("PES part missing from common package: ", ToGd(part.path));
        continue;
      }
      AddModel(std::span(*common).subspan(it->offset, it->size), it->path, skin_);
    } else if (auto blob = vfs_.Read(part.path, &err)) {
      AddModel(*blob, part.path, skin_);
    } else {
      // PES leaves the slot empty when a part file is missing (8.9).
      UtilityFunctions::print("  empty slot, no file: ", ToGd(part.path));
    }
  }
  // A missing boots or glove folder leaves the slot empty (pes-kit-assembly.md 8.4).
  for (const std::string& pack : {pes::BootsPackPath(bootsId), look.gloveId ? pes::GlovePackPath(look.gloveId) : ""})
    if (!pack.empty()) {
      if (vfs_.Exists(pack)) AddPack(pack);
      else UtilityFunctions::print("  empty slot, no pack: ", ToGd(pack));
    }
  return Finish();
}

}  // namespace

void PesCharacterBuilder::_bind_methods() {
  ClassDB::bind_method(D_METHOD("build_face_model", "library", "face_id"), &PesCharacterBuilder::build_face_model);
  ClassDB::bind_method(D_METHOD("build_player", "library", "team_id", "kit_index", "face_id"),
                       &PesCharacterBuilder::build_player);
}

Node3D* PesCharacterBuilder::build_face_model(const Ref<PesLibrary>& library, int face_id) {
  if (library.is_null() || !library->rig()) {
    UtilityFunctions::push_error("PesCharacterBuilder: library is not open");
    return nullptr;
  }
  return Build(library->vfs(), *library->rig(), face_id, Mode::FaceOnly).RunFace();
}

Node3D* PesCharacterBuilder::build_player(const Ref<PesLibrary>& library, int team_id, int kit_index, int face_id) {
  if (library.is_null() || !library->rig()) {
    UtilityFunctions::push_error("PesCharacterBuilder: library is not open");
    return nullptr;
  }
  return Build(library->vfs(), *library->rig(), face_id, Mode::Player).RunPlayer(team_id, kit_index);
}

}  // namespace godot
