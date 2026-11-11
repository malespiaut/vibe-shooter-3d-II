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

# Sound effects build step: assets/src/sfx/*.json -> assets/gen/sfx/*.snd
# (tools/sfx.py through libbast.so).
file(GLOB SFX_PARAMS CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/assets/src/sfx/*.json)
set(SFX_OUTPUTS)
foreach(p ${SFX_PARAMS})
  get_filename_component(n ${p} NAME_WE)
  list(APPEND SFX_OUTPUTS ${ASSETS_GEN}/sfx/${n}.snd)
endforeach()
add_custom_command(
  OUTPUT ${SFX_OUTPUTS}
  COMMAND ${CMAKE_COMMAND} -E env BAST_LIB=$<TARGET_FILE:bast>
          ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/sfx.py -q
          ${SFX_PARAMS} -o ${ASSETS_GEN}/sfx
  DEPENDS bast ${SFX_PARAMS} ${CMAKE_SOURCE_DIR}/tools/sfx.py ${CMAKE_SOURCE_DIR}/tools/bast.py
  COMMENT "Building sound effects")
add_custom_target(sfx ALL DEPENDS ${SFX_OUTPUTS})

# Renderer test-pattern textures (test fixture, never shipped):
# tools/testpattern.py -> assets/gen/test/testNN.tex through libbast.so.
set(TESTPATTERN_TEX)
foreach(n 01 02 03 04 05 06 07 08)
  list(APPEND TESTPATTERN_TEX ${ASSETS_GEN}/test/test${n}.tex)
endforeach()
add_custom_command(
  OUTPUT ${TESTPATTERN_TEX}
  COMMAND ${CMAKE_COMMAND} -E env BAST_LIB=$<TARGET_FILE:bast>
          BAST_PALETTE=${ASSETS_GEN}/palette.bin
          ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/testpattern.py -q
          --count 8 -o ${ASSETS_GEN}/test
  DEPENDS bast ${ASSETS_GEN}/palette.bin ${CMAKE_SOURCE_DIR}/tools/testpattern.py
          ${CMAKE_SOURCE_DIR}/tools/bast.py
  COMMENT "Building renderer test-pattern textures")
add_custom_target(testpattern ALL DEPENDS ${TESTPATTERN_TEX})
add_dependencies(testpattern palette)

add_executable(game src/main.c)
target_link_libraries(game PRIVATE engine platform)
target_compile_definitions(game PRIVATE VS3D_ASSETS_DIR="${ASSETS_GEN}"
  VS3D_MAPS_DIR="${CMAKE_SOURCE_DIR}/maps")
add_dependencies(game colormap sfx testpattern)

enable_testing()
add_executable(test_smoke tests/test_smoke.c)
target_link_libraries(test_smoke PRIVATE engine)
add_test(NAME smoke COMMAND test_smoke)
# Tests are plain C asserts: keep them active in every build type.
target_compile_options(test_smoke PRIVATE -UNDEBUG)

# Core maths tests (fixed, angle, rng, hash).
foreach(t fixed angle rng hash player_move)
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

add_executable(test_pak_roundtrip tests/test_pak_roundtrip.c)
target_link_libraries(test_pak_roundtrip PRIVATE bast_static)
target_compile_options(test_pak_roundtrip PRIVATE -UNDEBUG)
add_test(NAME pak_roundtrip COMMAND test_pak_roundtrip ${CMAKE_BINARY_DIR})

add_executable(test_files tests/test_files.c)
target_link_libraries(test_files PRIVATE engine)
target_compile_options(test_files PRIVATE -UNDEBUG)
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/test_tmp/files)
add_test(NAME files COMMAND test_files ${CMAKE_BINARY_DIR}/test_tmp/files)

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
add_test(NAME editor_roundtrip
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/editor_roundtrip.py)
add_test(NAME texgen_tile
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_texgen.py)
add_test(NAME editor_entities
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/editor_entities.py)
add_test(NAME gibs_determinism
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_gibs.py determinism)
add_test(NAME gibs_pixels
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_gibs.py pixels)
add_test(NAME fonts
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_fonts.py)
add_test(NAME pak_py
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_pak.py)
# Python tests load the libbast.so built here, not a stale one.
set_tests_properties(palcheck bast_py sfx_determinism mkmap_py quantize editor_roundtrip
  texgen_tile editor_entities gibs_determinism gibs_pixels fonts pak_py PROPERTIES
  ENVIRONMENT "BAST_LIB=$<TARGET_FILE:bast>;BAST_PALETTE=${ASSETS_GEN}/palette.bin")

add_executable(test_colormap tests/test_colormap.c)
target_link_libraries(test_colormap PRIVATE engine m)
target_compile_options(test_colormap PRIVATE -UNDEBUG)
add_dependencies(test_colormap colormap)
add_test(NAME colormap COMMAND test_colormap ${ASSETS_GEN} ${PALETTE_GPL})

# Effect generator (tools/fx.py, GAME.md 6.3).
add_test(NAME fx_loop
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_fx.py loop)
add_test(NAME fx_determinism
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_fx.py determinism)
add_test(NAME fx_shapes
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_fx.py shapes)
set_tests_properties(fx_loop fx_determinism fx_shapes PROPERTIES
  ENVIRONMENT "BAST_LIB=$<TARGET_FILE:bast>;BAST_PALETTE=${ASSETS_GEN}/palette.bin")

# HUD art (hud.md Status bar): build step output, exact sizes, palcheck.
add_test(NAME hud_assets
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_hud.py)
set_tests_properties(hud_assets PROPERTIES
  ENVIRONMENT "BAST_LIB=$<TARGET_FILE:bast>;BAST_PALETTE=${ASSETS_GEN}/palette.bin")

# Mixer: unit tests on synthetic sounds, and a scripted mix of real effects
# into build/mixer_wav.wav (evidence).
add_executable(test_mixer tests/test_mixer.c)
target_link_libraries(test_mixer PRIVATE engine)
target_compile_options(test_mixer PRIVATE -UNDEBUG)
add_test(NAME mixer COMMAND test_mixer)

add_executable(test_mixer_wav tests/test_mixer_wav.c)
target_link_libraries(test_mixer_wav PRIVATE engine)
target_compile_options(test_mixer_wav PRIVATE -UNDEBUG)
add_dependencies(test_mixer_wav sfx)
add_test(NAME mixer_wav COMMAND test_mixer_wav ${ASSETS_GEN} ${CMAKE_BINARY_DIR}/mixer_wav.wav)

# Headless demos (roadmap.md Verification tooling contract).
add_executable(test_png_writer tests/test_png_writer.c)
target_link_libraries(test_png_writer PRIVATE engine)
target_compile_options(test_png_writer PRIVATE -UNDEBUG)
add_dependencies(test_png_writer palette)
add_test(NAME png_writer
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_png_writer.py
          $<TARGET_FILE:test_png_writer> ${ASSETS_GEN}/palette.bin
          ${CMAKE_BINARY_DIR}/test_tmp/png_writer)

add_executable(test_demo_parse tests/test_demo_parse.c)
target_link_libraries(test_demo_parse PRIVATE engine)
target_compile_options(test_demo_parse PRIVATE -UNDEBUG)
add_test(NAME demo_parse COMMAND test_demo_parse ${CMAKE_BINARY_DIR}/demo_parse_rec.demo)

add_test(NAME headless_smoke
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_headless.py
          $<TARGET_FILE:game> ${CMAKE_BINARY_DIR}/test_tmp/headless)

# Sprite pre-render pipeline (tools/blender_render.py). Exits 77 = skipped
# when `blender` is not installed.
add_test(NAME blender_render
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_blender_render.py)
set_tests_properties(blender_render PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 600
  ENVIRONMENT "BAST_LIB=$<TARGET_FILE:bast>;BAST_PALETTE=${ASSETS_GEN}/palette.bin")

# Music sequencer (docs/design/audio.md, "Music"): parser and render tests,
# and build/vmus_render <song.vmus> <out.wav> [seconds] for listening/evidence.
add_executable(vmus_render tools/vmus_render.c)
target_link_libraries(vmus_render PRIVATE engine)
set(TITLE_VMUS ${CMAKE_SOURCE_DIR}/assets/src/music/title.vmus)
foreach(t music_parse music_render)
  add_executable(test_${t} tests/test_${t}.c)
  target_link_libraries(test_${t} PRIVATE engine)
  target_compile_options(test_${t} PRIVATE -UNDEBUG)
  add_test(NAME ${t} COMMAND test_${t} ${TITLE_VMUS})
endforeach()

# Determinism (roadmap.md, Determinism rules): replay a registered demo twice
# with --hash-log and require byte-identical per-tic logs. Later milestones
# call add_demo_determinism(m3-arena) and add_demo_determinism(levelN-complete).
function(add_demo_determinism name)
  add_test(NAME determinism_${name}
    COMMAND ${CMAKE_COMMAND}
            -DGAME=$<TARGET_FILE:game>
            -DDEMO=${CMAKE_SOURCE_DIR}/demos/${name}.demo
            -DOUT=${CMAKE_BINARY_DIR}/test_tmp/determinism_${name}
            -P ${CMAKE_SOURCE_DIR}/tests/determinism.cmake)
endfunction()
add_demo_determinism(m1-walk)

# High-level demo commands (goto/face/use/fire/expect) through build/game.
foreach(c goto timeout expect)
  add_test(NAME demo_${c}
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_demo_cmds.py
            $<TARGET_FILE:game> ${CMAKE_BINARY_DIR}/test_tmp/demo_cmds ${c})
  set_tests_properties(demo_${c} PROPERTIES
    ENVIRONMENT "BAST_LIB=$<TARGET_FILE:bast>;BAST_PALETTE=${ASSETS_GEN}/palette.bin")
endforeach()

# Renderer: walls on maps/m1test.vmap from a fixed pose (issue #13).
add_executable(test_render_walls tests/test_render_walls.c)
target_link_libraries(test_render_walls PRIVATE engine m)
target_compile_options(test_render_walls PRIVATE -UNDEBUG)
add_dependencies(test_render_walls colormap testpattern)
add_test(NAME render_walls
  COMMAND test_render_walls ${CMAKE_SOURCE_DIR}/maps/m1test.vmap ${ASSETS_GEN})
