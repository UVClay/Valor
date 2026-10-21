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
    core/src/records.cpp
    core/src/session.cpp
)
target_include_directories(valor_core PUBLIC core/include)
target_compile_options(valor_core PRIVATE -Wall -Wextra -fno-exceptions)

add_executable(coretest tools/coretest/coretest.cpp)
target_link_libraries(coretest PRIVATE valor_core)

if(VALOR_GODOT)
    add_subdirectory(godot)
endif()
