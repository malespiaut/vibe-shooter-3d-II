# Determinism test (docs/design/roadmap.md, Determinism rules; GAME.md 1, 12).
# Replays one demo twice with --hash-log and requires the per-tic hash logs to
# be byte-identical. On mismatch, names the first differing tic.
#
#   cmake -DGAME=<game exe> -DDEMO=<file.demo> -DOUT=<dir> -P determinism.cmake
#
# Registered from CMakeLists.txt through add_demo_determinism(<name>).
#
# The rules also require gcc and clang builds to agree. Without CI, check by
# hand that the per-tic logs (and so the STATS hash) are identical:
#   CC=clang cmake -S . -B build-clang -G Ninja && cmake --build build-clang
#   build/game --headless --demo demos/m1-walk.demo --hash-log /tmp/gcc.log
#   build-clang/game --headless --demo demos/m1-walk.demo --hash-log /tmp/clang.log
#   cmp /tmp/gcc.log /tmp/clang.log
foreach(v GAME DEMO OUT)
  if(NOT DEFINED ${v})
    message(FATAL_ERROR "determinism: -D${v}=... is required")
  endif()
endforeach()

file(REMOVE_RECURSE ${OUT})
file(MAKE_DIRECTORY ${OUT})

foreach(run a b)
  execute_process(
    COMMAND ${GAME} --headless --demo ${DEMO} --hash-log ${OUT}/hash_${run}.log
    RESULT_VARIABLE rc
    OUTPUT_VARIABLE out
    ERROR_VARIABLE err)
  if(NOT rc EQUAL 0)
    message(FATAL_ERROR "determinism: run ${run} exited ${rc}\n${out}${err}")
  endif()
  string(STRIP "${out}" out)
  string(REGEX MATCH "STATS [^\n]*$" stats_${run} "${out}")
  message(STATUS "run ${run}: ${stats_${run}}")
endforeach()

file(STRINGS ${OUT}/hash_a.log a)
file(STRINGS ${OUT}/hash_b.log b)
list(LENGTH a na)
list(LENGTH b nb)
if(na EQUAL 0)
  message(FATAL_ERROR "determinism: ${OUT}/hash_a.log is empty")
endif()

# Line i (1-based) of a hash log is the state hash after tic i.
set(n ${na})
if(nb LESS n)
  set(n ${nb})
endif()
math(EXPR last "${n} - 1")
foreach(i RANGE 0 ${last})
  list(GET a ${i} ha)
  list(GET b ${i} hb)
  if(NOT ha STREQUAL hb)
    math(EXPR tic "${i} + 1")
    message(FATAL_ERROR
      "determinism: ${DEMO}: first differing tic ${tic}: run a ${ha}, run b ${hb}")
  endif()
endforeach()
if(NOT na EQUAL nb)
  math(EXPR tic "${n} + 1")
  message(FATAL_ERROR
    "determinism: ${DEMO}: first differing tic ${tic}: run a logged ${na} tics, run b ${nb}")
endif()

# Lines match; also require byte identity (line endings, trailing data).
file(SHA256 ${OUT}/hash_a.log sa)
file(SHA256 ${OUT}/hash_b.log sb)
if(NOT sa STREQUAL sb)
  message(FATAL_ERROR "determinism: ${DEMO}: hash logs differ in bytes but not in lines")
endif()
message(STATUS "determinism: ${DEMO}: ${na} tics identical")
