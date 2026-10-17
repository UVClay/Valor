cmake_minimum_required(VERSION 3.28)
project(Valor C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

find_package(Python3 REQUIRED COMPONENTS Interpreter)
find_package(SDL3 REQUIRED CONFIG)
find_package(OpenGL REQUIRED)

set(GAME moh CACHE STRING "Game config under games/")
set(GEN_DIR ${CMAKE_BINARY_DIR}/gen)

# Generate C for a project at configure time; re-run when the recompiler or config changes.
function(recompile project config)
    set(out ${GEN_DIR}/${project})
    execute_process(
        COMMAND ${Python3_EXECUTABLE} tools/recomp/recomp.py ${config} generate ${out}
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        RESULT_VARIABLE result)
    if(result)
        message(FATAL_ERROR "recompiling ${config} failed")
    endif()
    file(STRINGS ${out}/sources.txt names)
    list(TRANSFORM names PREPEND ${out}/)
    file(GLOB recomp_py CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/tools/recomp/*.py)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${config} ${recomp_py})

    add_library(${project}_recomp STATIC ${names})
    target_include_directories(${project}_recomp PUBLIC ${CMAKE_SOURCE_DIR}/runtime/include ${out})
    # Generated code is large and mechanical: keep warnings off and optimization moderate.
    target_compile_options(${project}_recomp PRIVATE -O1 -w -fno-strict-aliasing -fwrapv)
endfunction()

# OpenBIOS is built once with the MIPS cross compiler.
set(OPENBIOS_ELF ${CMAKE_SOURCE_DIR}/build/openbios/openbios.elf)
if(NOT EXISTS ${OPENBIOS_ELF})
    execute_process(COMMAND ${CMAKE_SOURCE_DIR}/tools/build_openbios.sh RESULT_VARIABLE result)
    if(result)
        message(FATAL_ERROR "building OpenBIOS failed")
    endif()
endif()

recompile(bios bios/openbios.toml)
recompile(game games/${GAME}/recomp.toml)

# Mednafen's SPU (vendored from Beetle PSX, GPL-2.0-or-later), built against small shims.
add_library(beetle_spu STATIC third_party/beetle-psx/psx/spu.c)
target_include_directories(beetle_spu PUBLIC third_party/beetle-psx third_party/beetle-psx/psx)
target_compile_options(beetle_spu PRIVATE -O2 -w)

file(GLOB runtime_src CONFIGURE_DEPENDS runtime/src/*.cpp)
add_executable(valor ${runtime_src})
target_include_directories(valor PRIVATE runtime/include)
target_compile_options(valor PRIVATE -Wall -Wextra)
target_link_libraries(valor PRIVATE game_recomp bios_recomp beetle_spu SDL3::SDL3 OpenGL::GL)
