#include "gdext/pes_library.hpp"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

namespace godot {

void PesLibrary::_bind_methods() {
  ClassDB::bind_method(D_METHOD("open", "root"), &PesLibrary::open);
  ClassDB::bind_method(D_METHOD("mount_report"), &PesLibrary::mount_report);
  ClassDB::bind_method(D_METHOD("list", "prefix"), &PesLibrary::list);
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
  return OK;
}

String PesLibrary::mount_report() const { return ToGd(vfs_.MountReport()); }

PackedStringArray PesLibrary::list(const String& prefix) const {
  PackedStringArray out;
  for (const auto& p : vfs_.List(FromGd(prefix))) out.push_back(ToGd(p));
  return out;
}

}  // namespace godot
