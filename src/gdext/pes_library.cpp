#include "gdext/pes_library.hpp"

#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include "gdext/pes_texture.hpp"
#include "pes/exe.hpp"

namespace godot {
namespace {
constexpr const char* EXE_NAME = "PES2021.exe";
}  // namespace

void PesLibrary::_bind_methods() {
  ClassDB::bind_method(D_METHOD("open", "root"), &PesLibrary::open);
  ClassDB::bind_method(D_METHOD("mount_report"), &PesLibrary::mount_report);
  ClassDB::bind_method(D_METHOD("list", "prefix"), &PesLibrary::list);
  ClassDB::bind_method(D_METHOD("load_edit_save", "path"), &PesLibrary::load_edit_save);
  ClassDB::bind_method(D_METHOD("load_texture", "path"), &PesLibrary::load_texture);
}

Error PesLibrary::open(const String& root) {
  std::string err;
  if (!vfs_.Mount(FromGd(root), &err)) {
    UtilityFunctions::push_error("PES21 mount failed: ", ToGd(err));
    return ERR_FILE_NOT_FOUND;
  }
  rig_ = pesrig::BuildRig(vfs_, &err);
  if (!rig_) {
    UtilityFunctions::push_error("PES21 rig failed: ", ToGd(err));
    return ERR_PARSE_ERROR;
  }
  auto file = vfs_.Read(pes::kPlayerAppearancePath, &err);
  auto table = file ? pes::ParsePlayerAppearanceFile(*file, &err) : std::nullopt;
  if (table) appearances_ = std::move(*table);
  else UtilityFunctions::push_warning("PES21 PlayerAppearance.bin unreadable: ", ToGd(err));
  // Hand rig. The motion name table lives in the game's executable.
  auto exe = pes::PeImage::Open(FromGd(root) + "/" + EXE_NAME, &err);
  if (exe) motionNames_ = pes::MotionNames(*exe, vfs_, &err);
  if (motionNames_) handSequences_ = pes::LoadHandSequences(vfs_, &*exe, &err);
  if (handSequences_) handPoses_ = pesrig::LoadHandPoses(vfs_, handSequences_->poses, &err);
  if (!handPoses_) UtilityFunctions::push_warning("PES21 hand rig unavailable, hands stay at rest: ", ToGd(err));
  auto glyphs = exe ? pes::ReadNameGlyphs(*exe, &err) : std::nullopt;
  auto nameFont = pes::LoadNameFontMap(vfs_, &err);
  if (glyphs && nameFont) markingFonts_ = MarkingFonts{std::move(*glyphs), std::move(*nameFont)};
  else UtilityFunctions::push_warning("PES21 name glyphs unavailable, names not drawn: ", ToGd(err));
  return OK;
}

Error PesLibrary::load_edit_save(const String& path) {
  const PackedByteArray bytes = FileAccess::get_file_as_bytes(path);
  if (bytes.is_empty()) {
    UtilityFunctions::push_error("EDIT save unreadable: ", path);
    return ERR_FILE_CANT_OPEN;
  }
  std::string err;
  auto save = pes::ReadEditSave(std::span(bytes.ptr(), size_t(bytes.size())), &err);
  if (!save) {
    UtilityFunctions::push_error("EDIT save rejected: ", ToGd(err));
    return ERR_PARSE_ERROR;
  }
  for (auto& [id, record] : save->appearances) appearances_[id] = record;  // the save wins
  return OK;
}

String PesLibrary::mount_report() const { return ToGd(vfs_.MountReport()); }

PackedStringArray PesLibrary::list(const String& prefix) const {
  PackedStringArray out;
  for (const auto& p : vfs_.List(FromGd(prefix))) out.push_back(ToGd(p));
  return out;
}

Ref<Texture> PesLibrary::load_texture(const String& path) const {
  std::string err;
  Ref<Texture> t = LoadPesTexture(vfs_, FromGd(path), &err);
  if (t.is_valid()) t->set_name(path);
  return t;
}

}  // namespace godot
