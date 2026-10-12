#include <gdextension_interface.h>

#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/godot.hpp>

using namespace godot;

class GFInfo : public Object {
  GDCLASS(GFInfo, Object)

 protected:
  static void _bind_methods() { ClassDB::bind_static_method("GFInfo", D_METHOD("version"), &GFInfo::version); }

 public:
  static String version() { return "0.1.0"; }
};

static void initialize_module(ModuleInitializationLevel level) {
  if (level != MODULE_INITIALIZATION_LEVEL_SCENE) return;
  GDREGISTER_CLASS(GFInfo);
}

static void uninitialize_module(ModuleInitializationLevel) {}

extern "C" GDExtensionBool GDE_EXPORT godotfootball_library_init(GDExtensionInterfaceGetProcAddress get_proc_address,
                                                                  GDExtensionClassLibraryPtr library,
                                                                  GDExtensionInitialization* initialization) {
  GDExtensionBinding::InitObject init(get_proc_address, library, initialization);
  init.register_initializer(initialize_module);
  init.register_terminator(uninitialize_module);
  init.set_minimum_library_initialization_level(MODULE_INITIALIZATION_LEVEL_SCENE);
  return init.init();
}
