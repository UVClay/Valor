# The GDExtension that hosts the core in Godot (godot-cpp 10, API 4.7). The library lands in
# project/bin/, where valor.gdextension finds it.
set(GODOTCPP_API_VERSION "4.7" CACHE STRING "")
set(GODOTCPP_TARGET "template_debug" CACHE STRING "")
# The dev container has no static libstdc++; the library links the system one.
set(GODOTCPP_USE_STATIC_CPP OFF CACHE BOOL "")
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/godot-cpp ${CMAKE_BINARY_DIR}/godot-cpp)

add_library(valor_godot SHARED
    src/register_types.cpp
    src/valor_game.cpp
    src/valor_shell.cpp
)
target_link_libraries(valor_godot PRIVATE valor_core valor_sim godot-cpp)
get_target_property(GODOTCPP_SUFFIX godot-cpp GODOTCPP_SUFFIX)
set_target_properties(valor_godot PROPERTIES
    PREFIX "lib"
    OUTPUT_NAME "valor${GODOTCPP_SUFFIX}"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/project/bin"
    CXX_VISIBILITY_PRESET hidden
)
