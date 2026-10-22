# The Valor engine: an engine-agnostic C++ core with MoH's rules (core/), its test tools (tools/),
# and the Godot host (godot/, a GDExtension). Built on its own:
#   cmake -S engine -B build/gd/cmake -G Ninja && cmake --build build/gd/cmake
cmake_minimum_required(VERSION 3.22)
project(valor_engine CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

option(VALOR_GODOT "Build the Godot GDExtension (needs third_party/godot-cpp)" ON)

add_library(valor_core STATIC
    core/src/fixed.cpp
    core/src/io.cpp
    core/src/world.cpp
    core/src/movement.cpp
    core/src/trace.cpp
    core/src/level.cpp
    core/src/render_data.cpp
    core/src/player.cpp
    core/src/controls.cpp
    core/src/records.cpp
    core/src/session.cpp
    core/src/entities.cpp
    core/src/gte.cpp
    core/src/model.cpp
    core/src/anim.cpp
    core/src/camera.cpp
    core/src/aim.cpp
)
target_include_directories(valor_core PUBLIC core/include)
target_compile_options(valor_core PRIVATE -Wall -Wextra -fno-exceptions)

# The closed-loop engine (tools/sim/engine*.py ported), on the original's memory layout.
add_library(valor_sim STATIC
    sim/src/feed.cpp
    sim/src/engine.cpp
    sim/src/level.cpp
    sim/src/audio.cpp
    sim/src/objects.cpp
    sim/src/planes.cpp
    sim/src/attach.cpp
    sim/src/projectile.cpp
    sim/src/grenade.cpp
    sim/src/texts.cpp
    sim/src/alarm.cpp
    sim/src/views.cpp
    sim/src/combat.cpp
    sim/src/sense.cpp
    sim/src/scripts.cpp
    sim/src/brain.cpp
    sim/src/anim.cpp
    sim/src/camera.cpp
    sim/src/player.cpp
    sim/src/enemy.cpp
    sim/src/dispatch.cpp
    sim/src/load.cpp
    sim/src/campaign.cpp
)
target_include_directories(valor_sim PUBLIC sim/include)
target_link_libraries(valor_sim PUBLIC valor_core)
target_compile_options(valor_sim PRIVATE -Wall -Wextra -fno-exceptions)

add_executable(coretest tools/coretest/coretest.cpp tools/coretest/pad_mode.cpp tools/coretest/pose_mode.cpp
    tools/coretest/engine_mode.cpp tools/coretest/build_mode.cpp
    tools/coretest/live_mode.cpp
    tools/coretest/campaign_mode.cpp)
target_link_libraries(coretest PRIVATE valor_core valor_sim)

if(VALOR_GODOT)
    add_subdirectory(godot)
endif()
