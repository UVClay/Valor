#include <gdextension_interface.h>

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/defs.hpp>
#include <godot_cpp/godot.hpp>

#include "valor_game.h"

using namespace godot;

static void initialize_valor(ModuleInitializationLevel level) {
    if (level != MODULE_INITIALIZATION_LEVEL_SCENE) return;
    GDREGISTER_CLASS(ValorGame);
}

static void uninitialize_valor(ModuleInitializationLevel) {}

extern "C" {
GDExtensionBool GDE_EXPORT valor_library_init(GDExtensionInterfaceGetProcAddress get_proc_address,
                                              GDExtensionClassLibraryPtr library,
                                              GDExtensionInitialization* init) {
    GDExtensionBinding::InitObject obj(get_proc_address, library, init);
    obj.register_initializer(initialize_valor);
    obj.register_terminator(uninitialize_valor);
    obj.set_minimum_library_initialization_level(MODULE_INITIALIZATION_LEVEL_SCENE);
    return obj.init();
}
}
