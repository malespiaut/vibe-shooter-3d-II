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

# Engine + game: platform-independent, never includes SDL.
file(GLOB ENGINE_SOURCES CONFIGURE_DEPENDS src/engine/*.c src/game/*.c)
add_library(engine STATIC ${ENGINE_SOURCES} ${TRIG_TABLES_H})
target_include_directories(engine PUBLIC src PRIVATE ${CMAKE_BINARY_DIR}/gen)

# Platform layer: the only code allowed to talk to SDL3.
add_library(platform STATIC src/platform/platform_sdl.c)
target_include_directories(platform PUBLIC src)
target_link_libraries(platform PRIVATE SDL3::SDL3)

add_executable(game src/main.c)
target_link_libraries(game PRIVATE engine platform)

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

add_test(NAME palcheck
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_palette.py)
