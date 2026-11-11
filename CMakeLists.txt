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

# Engine + game: platform-independent, never includes SDL.
file(GLOB ENGINE_SOURCES CONFIGURE_DEPENDS src/engine/*.c src/game/*.c)
add_library(engine STATIC ${ENGINE_SOURCES})
target_include_directories(engine PUBLIC src)

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

add_executable(test_platform_audio tests/test_platform_audio.c)
target_link_libraries(test_platform_audio PRIVATE platform)
target_compile_options(test_platform_audio PRIVATE -UNDEBUG)
add_test(NAME platform_audio COMMAND test_platform_audio)
set_tests_properties(platform_audio PROPERTIES ENVIRONMENT "SDL_AUDIODRIVER=dummy")
