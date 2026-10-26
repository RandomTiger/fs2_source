#include <gdextension_interface.h>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/defs.hpp>
#include <godot_cpp/godot.hpp>

#include "fs2_node.h"

using namespace godot;

static void fs2_initialize(ModuleInitializationLevel level)
{
	if (level == MODULE_INITIALIZATION_LEVEL_SCENE) {
		GDREGISTER_CLASS(Fs2Game);
	}
}

static void fs2_uninitialize(ModuleInitializationLevel) {}

extern "C" {
GDExtensionBool GDE_EXPORT fs2_library_init(GDExtensionInterfaceGetProcAddress get_proc_address,
	GDExtensionClassLibraryPtr library, GDExtensionInitialization *init)
{
	GDExtensionBinding::InitObject init_obj(get_proc_address, library, init);
	init_obj.register_initializer(fs2_initialize);
	init_obj.register_terminator(fs2_uninitialize);
	init_obj.set_minimum_library_initialization_level(MODULE_INITIALIZATION_LEVEL_SCENE);
	return init_obj.init();
}
}
