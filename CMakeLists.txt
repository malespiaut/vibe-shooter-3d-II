cmake_minimum_required(VERSION 3.20)
project(vibe_shooter_3d_ii LANGUAGES C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

add_compile_options(-Wall -Wextra -Werror)

find_package(SDL3 REQUIRED CONFIG)
find_package(Python3 REQUIRED COMPONENTS Interpreter)

# Palette build step: catlantis.gpl -> assets/gen/palette.bin + palette_flags.bin.
set(PALETTE_GPL ${CMAKE_SOURCE_DIR}/assets/src/palette/catlantis.gpl)
set(ASSETS_GEN ${CMAKE_SOURCE_DIR}/assets/gen)
add_custom_command(
  OUTPUT ${ASSETS_GEN}/palette.bin ${ASSETS_GEN}/palette_flags.bin
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/palette.py
          --gpl ${PALETTE_GPL} --out-dir ${ASSETS_GEN}
  DEPENDS ${PALETTE_GPL} ${CMAKE_SOURCE_DIR}/tools/palette.py
  COMMENT "Building palette.bin from catlantis.gpl")
add_custom_target(palette ALL
  DEPENDS ${ASSETS_GEN}/palette.bin ${ASSETS_GEN}/palette_flags.bin)

# Host tool: generates the trig tables (floats allowed here only).
# Output lives in the build tree and is never committed.
add_executable(gen_trig tools/gen_trig.c)
target_link_libraries(gen_trig PRIVATE m)
set(TRIG_TABLES_H ${CMAKE_BINARY_DIR}/gen/trig_tables.h)
add_custom_command(
  OUTPUT ${TRIG_TABLES_H}
  COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/gen
  COMMAND gen_trig ${TRIG_TABLES_H}
  DEPENDS gen_trig
  COMMENT "Generating trig_tables.h")

# libbast: the single implementation of the shared formats (palette, Oklab,
# TEX). One set of objects, built position-independent, goes both into
# libbast.so (for the Python tools, via tools/bast.py) and statically into the
# engine. -ffp-contract=off keeps the Oklab maths bit-identical to the
# palette generator's expectations (no FMA contraction).
file(GLOB BAST_SOURCES CONFIGURE_DEPENDS src/bast/*.c)
add_library(bast_obj OBJECT ${BAST_SOURCES})
set_target_properties(bast_obj PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(bast_obj PUBLIC src/bast)
target_compile_options(bast_obj PRIVATE -ffp-contract=off)
add_library(bast SHARED $<TARGET_OBJECTS:bast_obj>)
target_link_libraries(bast PRIVATE m)
add_library(bast_static STATIC $<TARGET_OBJECTS:bast_obj>)
target_include_directories(bast_static PUBLIC src/bast)
target_link_libraries(bast_static PUBLIC m)

# Engine + game: platform-independent, never includes SDL.
file(GLOB ENGINE_SOURCES CONFIGURE_DEPENDS src/engine/*.c src/game/*.c)
add_library(engine STATIC ${ENGINE_SOURCES} ${TRIG_TABLES_H})
target_include_directories(engine PUBLIC src PRIVATE ${CMAKE_BINARY_DIR}/gen)
target_link_libraries(engine PUBLIC bast_static)

# Platform layer: the only code allowed to talk to SDL3.
add_library(platform STATIC src/platform/platform_sdl.c)
target_include_directories(platform PUBLIC src)
target_link_libraries(platform PRIVATE SDL3::SDL3)

# Colormap build step: palette.bin + libbast.so -> assets/gen/colormap.bin.
add_custom_command(
  OUTPUT ${ASSETS_GEN}/colormap.bin
  COMMAND ${CMAKE_COMMAND} -E env BAST_LIB=$<TARGET_FILE:bast>
          ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/colormap.py
          --palette ${ASSETS_GEN}/palette.bin --out ${ASSETS_GEN}/colormap.bin
  DEPENDS bast ${ASSETS_GEN}/palette.bin ${ASSETS_GEN}/palette_flags.bin
          ${CMAKE_SOURCE_DIR}/tools/colormap.py ${CMAKE_SOURCE_DIR}/tools/bast.py
  COMMENT "Building colormap.bin")
add_custom_target(colormap ALL DEPENDS ${ASSETS_GEN}/colormap.bin)
add_dependencies(colormap palette)

add_executable(game src/main.c)
target_link_libraries(game PRIVATE engine platform)
target_compile_definitions(game PRIVATE VS3D_ASSETS_DIR="${ASSETS_GEN}")
add_dependencies(game colormap)

enable_testing()
add_executable(test_smoke tests/test_smoke.c)
target_link_libraries(test_smoke PRIVATE engine)
add_test(NAME smoke COMMAND test_smoke)
# Tests are plain C asserts: keep them active in every build type.
target_compile_options(test_smoke PRIVATE -UNDEBUG)

# Core maths tests (fixed, angle, rng, hash).
foreach(t fixed angle rng hash)
  add_executable(test_${t} tests/test_${t}.c)
  target_link_libraries(test_${t} PRIVATE engine)
  target_compile_options(test_${t} PRIVATE -UNDEBUG)
  add_test(NAME ${t} COMMAND test_${t})
endforeach()
target_link_libraries(test_angle PRIVATE m)

add_executable(test_platform_audio tests/test_platform_audio.c)
target_link_libraries(test_platform_audio PRIVATE platform)
target_compile_options(test_platform_audio PRIVATE -UNDEBUG)
add_test(NAME platform_audio COMMAND test_platform_audio)
set_tests_properties(platform_audio PROPERTIES ENVIRONMENT "SDL_AUDIODRIVER=dummy")

# libbast tests. The palette files come from the palette build step.
foreach(t tex_roundtrip oklab_nearest)
  add_executable(test_${t} tests/test_${t}.c)
  target_link_libraries(test_${t} PRIVATE bast_static)
  target_compile_options(test_${t} PRIVATE -UNDEBUG)
  add_dependencies(test_${t} palette)
  add_test(NAME ${t} COMMAND test_${t} ${ASSETS_GEN}/palette.bin)
endforeach()

add_executable(test_snd_roundtrip tests/test_snd_roundtrip.c)
target_link_libraries(test_snd_roundtrip PRIVATE bast_static)
target_compile_options(test_snd_roundtrip PRIVATE -UNDEBUG)
add_test(NAME snd_roundtrip COMMAND test_snd_roundtrip)

add_executable(test_map_roundtrip tests/test_map_roundtrip.c)
target_link_libraries(test_map_roundtrip PRIVATE bast_static)
target_compile_options(test_map_roundtrip PRIVATE -UNDEBUG)
add_test(NAME map_roundtrip COMMAND test_map_roundtrip)

add_test(NAME palcheck
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_palette.py)
add_test(NAME bast_py
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_bast.py)
add_test(NAME sfx_determinism
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_sfx.py)
add_test(NAME mkmap_py
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_mkmap.py)
add_test(NAME quantize
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_quantize.py)
# Python tests load the libbast.so built here, not a stale one.
set_tests_properties(palcheck bast_py sfx_determinism mkmap_py quantize PROPERTIES
  ENVIRONMENT "BAST_LIB=$<TARGET_FILE:bast>;BAST_PALETTE=${ASSETS_GEN}/palette.bin")

add_executable(test_colormap tests/test_colormap.c)
target_link_libraries(test_colormap PRIVATE engine m)
target_compile_options(test_colormap PRIVATE -UNDEBUG)
add_dependencies(test_colormap colormap)
add_test(NAME colormap COMMAND test_colormap ${ASSETS_GEN} ${PALETTE_GPL})
