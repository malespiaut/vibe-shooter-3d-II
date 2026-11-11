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
          --palette ${ASSETS_GEN}/palette.bin --gpl ${PALETTE_GPL}
          --out ${ASSETS_GEN}/colormap.bin
  DEPENDS bast ${ASSETS_GEN}/palette.bin ${ASSETS_GEN}/palette_flags.bin ${PALETTE_GPL}
          ${CMAKE_SOURCE_DIR}/tools/colormap.py ${CMAKE_SOURCE_DIR}/tools/bast.py
          ${CMAKE_SOURCE_DIR}/tools/palette.py
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
list(APPEND TESTPATTERN_TEX ${ASSETS_GEN}/test/testsky.tex)
# Test-pattern sprite (TEST_SPRITE entity, issue #15): 8 directions.
foreach(d 0 1 2 3 4 5 6 7)
  list(APPEND TESTPATTERN_TEX ${ASSETS_GEN}/test/testspr_idle_d${d}_f0.tex)
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

# HUD art (hud.md Status bar, Cat face): assets/src/hud/ -> assets/gen/hud/
# (same as tools/build.d/50-hud.sh), so the game draws the status bar after
# a plain build. A stamp stands for the ~60 outputs.
file(GLOB_RECURSE HUD_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/assets/src/hud/*)
add_custom_command(
  OUTPUT ${ASSETS_GEN}/hud/.stamp
  COMMAND ${CMAKE_COMMAND} -E env BAST_LIB=$<TARGET_FILE:bast>
          BAST_PALETTE=${ASSETS_GEN}/palette.bin
          ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/hud.py
          --src ${CMAKE_SOURCE_DIR}/assets/src/hud -o ${ASSETS_GEN}/hud --clean
  COMMAND ${CMAKE_COMMAND} -E touch ${ASSETS_GEN}/hud/.stamp
  DEPENDS bast ${ASSETS_GEN}/palette.bin ${HUD_SOURCES} ${CMAKE_SOURCE_DIR}/tools/hud.py
          ${CMAKE_SOURCE_DIR}/tools/bast.py
  COMMENT "Building HUD art")
add_custom_target(hud_tex ALL DEPENDS ${ASSETS_GEN}/hud/.stamp)
add_dependencies(hud_tex palette)

add_executable(game src/main.c)
target_link_libraries(game PRIVATE engine platform)
target_compile_definitions(game PRIVATE VS3D_ASSETS_DIR="${ASSETS_GEN}"
  VS3D_MAPS_DIR="${CMAKE_SOURCE_DIR}/maps")
add_dependencies(game colormap sfx testpattern hud_tex fonts_tex)

enable_testing()
add_executable(test_smoke tests/test_smoke.c)
target_link_libraries(test_smoke PRIVATE engine)
add_test(NAME smoke COMMAND test_smoke)
# Tests are plain C asserts: keep them active in every build type.
target_compile_options(test_smoke PRIVATE -UNDEBUG)

# Core maths tests (fixed, angle, rng, hash).
foreach(t fixed angle rng hash player_move weapon sprite_dir)
  add_executable(test_${t} tests/test_${t}.c)
  target_link_libraries(test_${t} PRIVATE engine)
  target_compile_options(test_${t} PRIVATE -UNDEBUG)
  add_test(NAME ${t} COMMAND test_${t})
endforeach()
target_link_libraries(test_angle PRIVATE m)

# Pickups, inventory, player damage (items.md): one ctest per table group.
add_executable(test_items tests/test_items.c)
target_link_libraries(test_items PRIVATE engine)
target_compile_options(test_items PRIVATE -UNDEBUG)
foreach(g health armour ammo weapons keys powerups damage ninth_life spawn touch)
  add_test(NAME items_${g} COMMAND test_items ${g})
endforeach()

add_executable(test_ai tests/test_ai.c)
target_link_libraries(test_ai PRIVATE engine)
target_compile_options(test_ai PRIVATE -UNDEBUG)
foreach(g sight ambush windup chomper pillar drop block pushwall
          pom_speed pom_pair pom_dodge pom_still pom_windup
          dach_mortar dach_retreat dach_windup dach_headbutt)
  add_test(NAME ai_${g} COMMAND test_ai ${g})
endforeach()
# Projectiles, splash, Scarab Launcher, kibble barrels (issue #93).
add_executable(test_projectile tests/test_projectile.c)
target_link_libraries(test_projectile PRIVATE engine)
target_compile_options(test_projectile PRIVATE -UNDEBUG)
foreach(g travel wall splash splash_wall self direct barrel_chain barrel_solid awake alive)
  add_test(NAME proj_${g} COMMAND test_projectile ${g})
endforeach()
# Gameplay code is fixed point only (CLAUDE.md): no float/double in src/game.
add_test(NAME proj_no_floats
  COMMAND sh -c "! grep -rnE 'float|double' ${CMAKE_SOURCE_DIR}/src/game")

# Weapons 6-7: Eye of Ra Lance and Sphinx Howler.
add_executable(test_weapon67 tests/test_weapon67.c)
target_link_libraries(test_weapon67 PRIVATE engine)
target_compile_options(test_weapon67 PRIVATE -UNDEBUG)
foreach(g pierce wall cone knockback pain self barrel_lance barrel_howler sound)
  add_test(NAME weapon67_${g} COMMAND test_weapon67 ${g})
endforeach()

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
add_test(NAME sfx_levels
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_sfx_levels.py)
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
add_test(NAME editor_launch
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/editor_launch.py)
add_test(NAME gibs_determinism
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_gibs.py determinism)
add_test(NAME gibs_pixels
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_gibs.py pixels)
add_test(NAME fonts
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_fonts.py)
add_test(NAME pak_py
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_pak.py)
# Python tests load the libbast.so built here, not a stale one.
set_tests_properties(palcheck bast_py sfx_determinism sfx_levels mkmap_py quantize editor_roundtrip
  texgen_tile editor_entities editor_launch gibs_determinism gibs_pixels fonts pak_py PROPERTIES
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
add_test(NAME title_screen
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_title.py)
set_tests_properties(title_screen PROPERTIES
  ENVIRONMENT "BAST_LIB=$<TARGET_FILE:bast>;BAST_PALETTE=${ASSETS_GEN}/palette.bin")

# Status-bar cat faces, tiers 1-2 + dead + god (hud.md Cat face): names, 32x32, palcheck.
add_test(NAME hud_faces_t12
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_hud_faces.py)
set_tests_properties(hud_faces_t12 PROPERTIES
  ENVIRONMENT "BAST_LIB=$<TARGET_FILE:bast>;BAST_PALETTE=${ASSETS_GEN}/palette.bin")

# Pickup sprites (items.md): build step output, sizes, origin, ramp budget, palcheck.
add_test(NAME sprites_items
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_sprites_items.py)
set_tests_properties(sprites_items PROPERTIES
  ENVIRONMENT "BAST_LIB=$<TARGET_FILE:bast>;BAST_PALETTE=${ASSETS_GEN}/palette.bin")

# Status-bar cat faces, tiers 3-5 (hud.md Cat face): names, 32x32, damage build-up,
# all 52 faces present, palcheck.
add_test(NAME hud_faces_t345
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_hud_faces_t345.py)
set_tests_properties(hud_faces_t345 PROPERTIES
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

add_test(NAME items_demo
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_items_demo.py
          $<TARGET_FILE:game> ${CMAKE_SOURCE_DIR})

# Sprite pre-render pipeline (tools/blender_render.py). Exits 77 = skipped
# when `blender` is not installed.
add_test(NAME blender_render
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_blender_render.py)
set_tests_properties(blender_render PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 600
  ENVIRONMENT "BAST_LIB=$<TARGET_FILE:bast>;BAST_PALETTE=${ASSETS_GEN}/palette.bin")
# Mutt Trooper sprite set (assets/src/sprites/mutt): renders it and checks
# the frame set and art-bible pixel rules. Exits 77 = skipped without blender.
add_test(NAME sprite_mutt
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_sprite_mutt.py)
set_tests_properties(sprite_mutt PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 600
  ENVIRONMENT "BAST_LIB=$<TARGET_FILE:bast>;BAST_PALETTE=${ASSETS_GEN}/palette.bin")

# Chihuahua Chomper sprite set (enemies.md, art-bible.md): renders the model
# into a temp dir and checks frames, size, ramps and outline. 77 = skipped
# without blender.
add_test(NAME sprite_chomp
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_sprite_chomp.py)
set_tests_properties(sprite_chomp PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 600
  ENVIRONMENT "BAST_LIB=$<TARGET_FILE:bast>;BAST_PALETTE=${ASSETS_GEN}/palette.bin")
# Dachshund Howitzer sprite set: same checks at 72x48, plus the muzzle glow.
add_test(NAME sprite_dach
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_sprite_dach.py)
set_tests_properties(sprite_dach PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 600
  ENVIRONMENT "BAST_LIB=$<TARGET_FILE:bast>;BAST_PALETTE=${ASSETS_GEN}/palette.bin")

# Mastiff Juggernaut sprite set (enemies.md, art-bible.md): renders the model
# into a temp dir and checks frames, size, ramps and outline. 77 = skipped
# without blender.
add_test(NAME sprite_mastiff
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_sprite_mastiff.py)
set_tests_properties(sprite_mastiff PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 600
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
add_demo_determinism(door-test)
add_demo_determinism(m1-sprites)
add_demo_determinism(tags-test)
add_demo_determinism(ai-test)
add_demo_determinism(combat-test)
add_demo_determinism(lance-howler-test)

# High-level demo commands (goto/face/use/fire/expect) through build/game.
foreach(c goto timeout expect)
  add_test(NAME demo_${c}
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_demo_cmds.py
            $<TARGET_FILE:game> ${CMAKE_BINARY_DIR}/test_tmp/demo_cmds ${c})
  set_tests_properties(demo_${c} PROPERTIES
    ENVIRONMENT "BAST_LIB=$<TARGET_FILE:bast>;BAST_PALETTE=${ASSETS_GEN}/palette.bin")
endforeach()

# Combat demo commands (#98): face/fire nearest, clear.
add_executable(test_demo_combat tests/test_demo_combat.c)
target_link_libraries(test_demo_combat PRIVATE engine)
target_compile_options(test_demo_combat PRIVATE -UNDEBUG)
foreach(g face tie aim parse)
  add_test(NAME demo_combat_${g} COMMAND test_demo_combat ${g})
endforeach()
foreach(c fire clear notarget)
  add_test(NAME demo_combat_${c}
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_demo_combat.py
            $<TARGET_FILE:game> ${CMAKE_BINARY_DIR}/test_tmp/demo_combat ${c})
  set_tests_properties(demo_combat_${c} PROPERTIES
    ENVIRONMENT "BAST_LIB=$<TARGET_FILE:bast>;BAST_PALETTE=${ASSETS_GEN}/palette.bin")
endforeach()

# Renderer: walls on maps/m1test.vmap from a fixed pose (issue #13).
add_executable(test_render_walls tests/test_render_walls.c)
target_link_libraries(test_render_walls PRIVATE engine m)
target_compile_options(test_render_walls PRIVATE -UNDEBUG)
add_dependencies(test_render_walls colormap testpattern)
add_test(NAME render_walls
  COMMAND test_render_walls ${CMAKE_SOURCE_DIR}/maps/m1test.vmap ${ASSETS_GEN})

# Renderer: doors drawn from live door state on maps/doortest.vmap (#116).
add_executable(test_render_doors tests/test_render_doors.c)
target_link_libraries(test_render_doors PRIVATE engine m)
target_compile_options(test_render_doors PRIVATE -UNDEBUG)
add_dependencies(test_render_doors colormap testpattern)
add_test(NAME render_doors
  COMMAND test_render_doors ${CMAKE_SOURCE_DIR}/maps/doortest.vmap ${ASSETS_GEN})

# Fonts (docs/design/hud.md Fonts): the committed indexed sheets ->
# assets/gen/fonts/*.tex (same conversion as tools/build.d/50-fonts.sh), so
# the 2D drawing test and tools have them after a plain build.
set(FONT_TEXES)
foreach(f small menu bigdigits)
  add_custom_command(
    OUTPUT ${ASSETS_GEN}/fonts/${f}.tex
    COMMAND ${CMAKE_COMMAND} -E make_directory ${ASSETS_GEN}/fonts
    COMMAND ${CMAKE_COMMAND} -E env BAST_LIB=$<TARGET_FILE:bast>
            BAST_PALETTE=${ASSETS_GEN}/palette.bin
            ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/png2tex.py
            ${CMAKE_SOURCE_DIR}/assets/src/fonts/${f}.png ${ASSETS_GEN}/fonts/${f}.tex
    DEPENDS bast ${ASSETS_GEN}/palette.bin ${CMAKE_SOURCE_DIR}/assets/src/fonts/${f}.png
            ${CMAKE_SOURCE_DIR}/tools/png2tex.py ${CMAKE_SOURCE_DIR}/tools/bast.py
    COMMENT "Building font ${f}.tex")
  list(APPEND FONT_TEXES ${ASSETS_GEN}/fonts/${f}.tex)
endforeach()
add_custom_target(fonts_tex ALL DEPENDS ${FONT_TEXES})
add_dependencies(fonts_tex palette)

# In-game HUD (issue #99): face selection logic, and the status-bar layout
# rendered for a fixed state.
add_executable(test_hud_face tests/test_hud_face.c)
target_link_libraries(test_hud_face PRIVATE engine)
target_compile_options(test_hud_face PRIVATE -UNDEBUG)
add_test(NAME hud_face COMMAND test_hud_face)
add_executable(test_hud_layout tests/test_hud_layout.c)
target_link_libraries(test_hud_layout PRIVATE engine)
target_compile_options(test_hud_layout PRIVATE -UNDEBUG)
add_dependencies(test_hud_layout fonts_tex hud_tex colormap)
add_test(NAME hud_layout COMMAND test_hud_layout ${ASSETS_GEN} ${PALETTE_GPL}
  ${CMAKE_BINARY_DIR}/test_tmp/hud_layout.png)

# 2D drawing (issue #44): ctest draw2d, and
# build/draw2d_specimen <assets/gen dir> <out.png> for the specimen screen.
add_executable(test_draw2d tests/test_draw2d.c)
target_link_libraries(test_draw2d PRIVATE engine)
target_compile_options(test_draw2d PRIVATE -UNDEBUG)
add_dependencies(test_draw2d fonts_tex)
add_test(NAME draw2d COMMAND test_draw2d ${ASSETS_GEN} ${PALETTE_GPL})

add_executable(draw2d_specimen tools/draw2d_specimen.c)
target_link_libraries(draw2d_specimen PRIVATE engine)
add_dependencies(draw2d_specimen fonts_tex colormap)

# Weapons 1-4 and thinkers: the weapon test demo, run twice.
add_test(NAME weapon_demo
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_weapon_demo.py
          $<TARGET_FILE:game> ${CMAKE_BINARY_DIR}/test_tmp/weapon_demo)
set_tests_properties(weapon_demo PROPERTIES
  ENVIRONMENT "BAST_LIB=$<TARGET_FILE:bast>;BAST_PALETTE=${ASSETS_GEN}/palette.bin")

# Launcher test map and demo (issue #93): scarab splash and barrel kills.
add_test(NAME launcher_demo
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_launcher_demo.py
          $<TARGET_FILE:game> ${CMAKE_BINARY_DIR}/test_tmp/launcher_demo)
set_tests_properties(launcher_demo PROPERTIES
  ENVIRONMENT "BAST_LIB=$<TARGET_FILE:bast>;BAST_PALETTE=${ASSETS_GEN}/palette.bin")
add_demo_determinism(launcher-test)

# Weapons 6-7: the Lance / Howler test demo, run twice.
add_test(NAME lance_demo
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_lance_demo.py
          $<TARGET_FILE:game> ${CMAKE_BINARY_DIR}/test_tmp/lance_demo)
set_tests_properties(lance_demo PROPERTIES
  ENVIRONMENT "BAST_LIB=$<TARGET_FILE:bast>;BAST_PALETTE=${ASSETS_GEN}/palette.bin")

# Renderer: floors, ceilings and sky on maps/m1test.vmap (issue #14).
add_executable(test_render_floor tests/test_render_floor.c)
target_link_libraries(test_render_floor PRIVATE engine m)
target_compile_options(test_render_floor PRIVATE -UNDEBUG)
add_dependencies(test_render_floor colormap testpattern)
add_test(NAME render_floor
  COMMAND test_render_floor ${CMAKE_SOURCE_DIR}/maps/m1test.vmap ${ASSETS_GEN})

# Doors and keys on maps/doortest.vmap, plus the door-test demo replay.
add_executable(test_door tests/test_door.c)
target_link_libraries(test_door PRIVATE engine)
target_compile_options(test_door PRIVATE -UNDEBUG)
add_test(NAME door COMMAND test_door ${CMAKE_SOURCE_DIR}/maps/doortest.vmap
  ${CMAKE_SOURCE_DIR}/demos/door-test.demo)
# Renderer: billboard sprites, occlusion, size, shading (issue #15).
add_executable(test_render_sprites tests/test_render_sprites.c)
target_link_libraries(test_render_sprites PRIVATE engine m)
target_compile_options(test_render_sprites PRIVATE -UNDEBUG)
add_dependencies(test_render_sprites colormap testpattern)
add_test(NAME render_sprites COMMAND test_render_sprites ${ASSETS_GEN})
# Pickups as world sprites from live state (issue #120). Needs the item
# sprites from tools/build_assets.sh (tools/build.d/47-items.sh).
add_executable(test_world_sprites_items tests/test_world_sprites_items.c)
target_link_libraries(test_world_sprites_items PRIVATE engine m)
target_compile_options(test_world_sprites_items PRIVATE -UNDEBUG)
add_dependencies(test_world_sprites_items colormap testpattern)
add_test(NAME world_sprites_items COMMAND test_world_sprites_items ${ASSETS_GEN})

# Map logic: tags, switches, exit, pushwalls, triggers, secrets, lights,
# damage floors on maps/tagstest.vmap (issue #66), one ctest per case.
add_executable(test_tags tests/test_tags.c)
target_link_libraries(test_tags PRIVATE engine)
target_compile_options(test_tags PRIVATE -UNDEBUG)
foreach(c switch start_open timed pushwall trigger secret lights damage exit)
  add_test(NAME tags_${c} COMMAND test_tags ${CMAKE_SOURCE_DIR}/maps/tagstest.vmap ${c})
endforeach()

# Sound events (issue #118): the sound-ID table matches assets/src/sfx, the
# queue and door/use sounds (unit), and demos with --sound-log.
add_test(NAME sfx_names
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_sfx_names.py ${CMAKE_SOURCE_DIR})
add_executable(test_sound tests/test_sound.c)
target_link_libraries(test_sound PRIVATE engine)
target_compile_options(test_sound PRIVATE -UNDEBUG)
add_test(NAME sound_unit COMMAND test_sound unit ${CMAKE_SOURCE_DIR}/maps)
add_test(NAME sound_events
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_sound_events.py
          $<TARGET_FILE:game> $<TARGET_FILE:test_sound> ${CMAKE_SOURCE_DIR}
          ${CMAKE_BINARY_DIR}/test_tmp/sound_events)
add_demo_determinism(door-locked)
