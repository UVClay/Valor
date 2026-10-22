# The presentation extension (ValorAssets): the level's textures, sounds, music and the player's
# weapon models as Godot resources, for the HUD, the view model, the sky and the audio drawn natively
# (engine/godot/project/present/). A project of its own, so it builds apart from the game's
# extension:
#   ./dev cmake -S engine/godot/present -B build/gdp/cmake -G Ninja && ./dev cmake --build build/gdp/cmake
cmake_minimum_required(VERSION 3.22)
project(valor_present CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

set(GODOTCPP_API_VERSION "4.7" CACHE STRING "")
set(GODOTCPP_TARGET "template_debug" CACHE STRING "")
set(GODOTCPP_USE_STATIC_CPP OFF CACHE BOOL "")
set(root ${CMAKE_CURRENT_SOURCE_DIR}/../../..)
add_subdirectory(${root}/third_party/godot-cpp ${CMAKE_BINARY_DIR}/godot-cpp)

# The parts of the core the loaders need (models, skinning, the VRAM image).
add_library(valor_present_core STATIC
    ${root}/engine/core/src/fixed.cpp
    ${root}/engine/core/src/io.cpp
    ${root}/engine/core/src/disc.cpp
    ${root}/engine/core/src/level.cpp
    ${root}/engine/core/src/render_data.cpp
    ${root}/engine/core/src/gte.cpp
    ${root}/engine/core/src/model.cpp
    ${root}/engine/core/src/anim.cpp
    ${root}/engine/core/src/controls.cpp
)
target_include_directories(valor_present_core PUBLIC ${root}/engine/core/include)
target_compile_options(valor_present_core PRIVATE -fno-exceptions)

add_library(valor_present SHARED
    src/register_types.cpp
    src/valor_assets.cpp
    src/adpcm.cpp
)
target_link_libraries(valor_present PRIVATE valor_present_core godot-cpp)
get_target_property(GODOTCPP_SUFFIX godot-cpp GODOTCPP_SUFFIX)
set_target_properties(valor_present PROPERTIES
    PREFIX "lib"
    OUTPUT_NAME "valor_present${GODOTCPP_SUFFIX}"
    LIBRARY_OUTPUT_DIRECTORY "${root}/engine/godot/project/bin"
    CXX_VISIBILITY_PRESET hidden
)
