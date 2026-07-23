cmake_minimum_required(VERSION 3.22)

project(F15SE2-EX
    LANGUAGES C CXX
)

#==============================================================================
# Options
#==============================================================================
# When ON (the default), any third-party dependency not found on the system is
# downloaded and built from source, so the project builds out of the box. Pass
# -DDOWNLOAD_DEPENDENCIES=OFF to require system-provided dependencies instead
# (configuration fails if one is missing).
option(DOWNLOAD_DEPENDENCIES "Download and build missing third-party dependencies from source" ON)

# Sanitizers (GCC/Clang only). Off by default; enable for debugging.
option(ASAN "Build with AddressSanitizer" OFF)
option(UBSAN "Build with UndefinedBehaviorSanitizer" OFF)

# Code coverage (GCC/Clang only). Instruments the game code + tests with gcov so
# `ctest` produces .gcda data that gcovr turns into a report (see the `coverage`
# target). Used by CI to upload to Codecov.
option(COVERAGE "Build with gcov code-coverage instrumentation" OFF)

# When ON, skip the interactive menus and drop straight into a hardcoded
# mission (egame). Handy for iterating on the flight code without clicking
# through the front end each run.
option(AUTOSTART "Skip menus and start a mission directly (DEBUG_AUTOSTART)" OFF)

#==============================================================================
# Global settings
#==============================================================================
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

#==============================================================================
# Dependencies
#==============================================================================
if(DOWNLOAD_DEPENDENCIES)
    # Use a system SDL3 if one is available, otherwise build it from source.
    find_package(SDL3 3.4.0 QUIET CONFIG COMPONENTS SDL3)
endif()

if(SDL3_FOUND)
    message(STATUS "Found system SDL3 ${SDL3_VERSION}")
else()
    if(DOWNLOAD_DEPENDENCIES)
        message(STATUS "System SDL3 not found, building it from source")
        include(FetchContent)
        FetchContent_Declare(
            SDL3
            GIT_REPOSITORY "https://github.com/libsdl-org/SDL.git"
            GIT_TAG "release-3.4.12"
            UPDATE_DISCONNECTED TRUE
        )
        FetchContent_MakeAvailable(SDL3)
    else()
        # DOWNLOAD_DEPENDENCIES is off: a system SDL3 is mandatory.
        find_package(SDL3 3.4.0 REQUIRED CONFIG COMPONENTS SDL3)
    endif()
endif()

# Nuked-OPL3: the YM3812/OPL2 emulator that turns the ported AdLib driver's
# register state into PCM. Header-light (opl3.c/.h), no CMake of its own, so we
# just pull the sources and compile opl3.c into the target (as C).
include(FetchContent)
FetchContent_Declare(
    nuked_opl3
    GIT_REPOSITORY "https://github.com/nukeykt/Nuked-OPL3.git"
    GIT_TAG "cfedb09efc03f1d7b5fc1f04dd449d77d8c49d50"  # master has no release tags
    UPDATE_DISCONNECTED TRUE
)
FetchContent_MakeAvailable(nuked_opl3)
FetchContent_Declare(
    quick_digest5
    GIT_REPOSITORY "https://github.com/nthnn/QuickDigest5.git"
    GIT_TAG "1d61aece0e023e4c41cf24547fa1ac2f8028219d"
    UPDATE_DISCONNECTED TRUE
)
FetchContent_MakeAvailable(quick_digest5)


#==============================================================================
# Sources
#==============================================================================
set(SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src")

# The four original DOS programs (f15 / start / egame / end) are being merged
# into a single SDL executable.
set(F15SE2_SOURCES

    # Shared gfx-slot / timer / misc / pic / overlay basics
    ${SRC_DIR}/input.c
    ${SRC_DIR}/shared/miscimpl.c
    ${SRC_DIR}/shared/ovlimpl.c
    ${SRC_DIR}/shared/picimpl.c
    ${SRC_DIR}/shared/timer.c

    # Shared file + string helpers
    ${SRC_DIR}/shared/file_io.c
    ${SRC_DIR}/shared/asset_path.c
    ${SRC_DIR}/shared/png_asset.c
    ${SRC_DIR}/shared/cleanup.c
    ${SRC_DIR}/shared/drawstr.c
    ${SRC_DIR}/shared/textfmt.c
    ${SRC_DIR}/shared/filepic.c

    # f15 launcher
    ${SRC_DIR}/f15.c
    ${SRC_DIR}/dosfunc.c
    ${SRC_DIR}/log.c
    ${SRC_DIR}/gfx_impl.c
    ${SRC_DIR}/joystick.c
    ${SRC_DIR}/hdsprite.c

    # start
    ${SRC_DIR}/stmain.c
    ${SRC_DIR}/stinit.c
    ${SRC_DIR}/stmissn.c
    ${SRC_DIR}/stsprit.c
    ${SRC_DIR}/strand.c
    ${SRC_DIR}/stpilot.c
    ${SRC_DIR}/stalloc.c
    ${SRC_DIR}/stterr.c
    ${SRC_DIR}/stparse.c
    ${SRC_DIR}/stgen.c
    ${SRC_DIR}/stdata.c
)

# egame (in-flight) module
set(EGAME_SOURCES
    ${SRC_DIR}/egmain.c
    ${SRC_DIR}/egsphere.c
    ${SRC_DIR}/egframe.c
    ${SRC_DIR}/worldxfer.c
    ${SRC_DIR}/eg3dview.c
    ${SRC_DIR}/eg3dproj.c
    ${SRC_DIR}/eg3dgrid.c
    ${SRC_DIR}/eg3dload.c
    ${SRC_DIR}/eg3dmap.c
    ${SRC_DIR}/eg3dvp.c
    ${SRC_DIR}/eg3dcam.c
    ${SRC_DIR}/egflight.c
    ${SRC_DIR}/egthreat.c
    ${SRC_DIR}/egcombat.c
    ${SRC_DIR}/egtacmap.c
    ${SRC_DIR}/egui.c
    ${SRC_DIR}/egtarget.c
    ${SRC_DIR}/egtgt2.c
    ${SRC_DIR}/egmath.c
    ${SRC_DIR}/egkeys.c
    ${SRC_DIR}/egpic.c
    ${SRC_DIR}/egdata.c
    ${SRC_DIR}/egfarbuf.c
    ${SRC_DIR}/eg3dmath.c
    ${SRC_DIR}/eghudm.c
    ${SRC_DIR}/eghudr.c
    ${SRC_DIR}/eg3drast.c
    ${SRC_DIR}/r3d.c
    ${SRC_DIR}/r3d_sw.c
    ${SRC_DIR}/r3d_gl.c
    ${SRC_DIR}/r3dmesh.c
    ${SRC_DIR}/r2d.c
    ${SRC_DIR}/egsys.c
    ${SRC_DIR}/egtick.c
    ${SRC_DIR}/eginput.c
    ${SRC_DIR}/egstubs.c

    # audio: ported ASOUND AdLib driver on SDL3
    ${SRC_DIR}/asound/asound_model.c
    ${SRC_DIR}/asound/asopl.c
    ${SRC_DIR}/asound/asound_sdl.c
)

# end (debrief) module
set(END_SOURCES
    ${SRC_DIR}/enmain.c
    ${SRC_DIR}/enmisc.c
    ${SRC_DIR}/eninput.c
    ${SRC_DIR}/entext.c
    ${SRC_DIR}/enrand.c
    ${SRC_DIR}/enaward.c
    ${SRC_DIR}/enbrief.c
    ${SRC_DIR}/endbrf.c
    ${SRC_DIR}/enfile.c
    ${SRC_DIR}/endata.c
)

list(APPEND F15SE2_SOURCES ${EGAME_SOURCES})
list(APPEND F15SE2_SOURCES ${END_SOURCES})

#==============================================================================
# Target
#==============================================================================
# The .c files are compiled as C++ (const linkage rules, overloads, etc.).
# vgapal.c is a standalone tool pulled in only by its own tests.
set_source_files_properties(
    ${F15SE2_SOURCES}
    ${SRC_DIR}/vgapal.c
    PROPERTIES LANGUAGE CXX
)

# OpenGL for the GL 3D backend (r3d_gl.c). The immediate-mode 1.x path needs only
# the base GL library; SDL3 supplies the GL headers and context management.
find_package(OpenGL REQUIRED)

# Some tests use std::thread; link a threads library explicitly so it resolves
# across toolchains (not all fold pthread into libc).
find_package(Threads REQUIRED)

#------------------------------------------------------------------------------
# Sanitizers (GCC/Clang, opt-in). Applied at directory scope so the executable
# and the tests (which each compile the game code they exercise) are instrumented
# consistently.
#------------------------------------------------------------------------------
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    set(SANITIZERS "")
    if(ASAN)
        list(APPEND SANITIZERS address)
    endif()
    if(UBSAN)
        list(APPEND SANITIZERS undefined)
    endif()
    if(SANITIZERS)
        list(JOIN SANITIZERS "," SANITIZER_LIST)
        message(STATUS "Building with sanitizers: ${SANITIZER_LIST}")
        add_compile_options(-fsanitize=${SANITIZER_LIST} -fno-omit-frame-pointer -g)
        add_link_options(-fsanitize=${SANITIZER_LIST})
    endif()

    # Coverage instrumentation (directory scope, so the core library and every
    # test object are instrumented consistently). --coverage implies -g and
    # disables optimisation-driven inlining that would confuse line mapping.
    if(COVERAGE)
        message(STATUS "Building with gcov coverage instrumentation")
        add_compile_options(--coverage -O0 -g)
        # GCC emits absolute .gcno paths with this, so gcovr resolves sources
        # regardless of cwd; Clang doesn't recognise the flag.
        add_compile_options($<$<CXX_COMPILER_ID:GNU>:-fprofile-abs-path>)
        add_link_options(--coverage)
    endif()
endif()

# The whole game except the launcher main() is a static core library. The
# executable is just f15.c linked against it; migrated behavior tests link the
# same library and exercise the real implementations headless (SDL dummy
# drivers), so the game code compiles once instead of per-test.
set(F15SE2_CORE_SOURCES ${F15SE2_SOURCES})
list(REMOVE_ITEM F15SE2_CORE_SOURCES ${SRC_DIR}/f15.c)

add_library(f15se2_core STATIC
    ${F15SE2_CORE_SOURCES}
    ${nuked_opl3_SOURCE_DIR}/opl3.c
    ${quick_digest5_SOURCE_DIR}/src/quickdigest5.cpp
)
target_include_directories(f15se2_core BEFORE PUBLIC
    ${SRC_DIR}/compat64
    ${SRC_DIR}
    ${SRC_DIR}/shared
    ${SRC_DIR}/generated
)
target_include_directories(f15se2_core PRIVATE
    ${SRC_DIR}/asound
    ${nuked_opl3_SOURCE_DIR}
    ${quick_digest5_SOURCE_DIR}/include
)
# Keep the log.h diagnostics live (the Log* macros compile out without DEBUG).
target_compile_definitions(f15se2_core PUBLIC DEBUG)
if(AUTOSTART)
    target_compile_definitions(f15se2_core PUBLIC DEBUG_AUTOSTART)
endif()
target_link_libraries(f15se2_core PUBLIC SDL3::SDL3 OpenGL::GL)

add_executable(f15se2 ${SRC_DIR}/f15.c)
set_target_properties(f15se2 PROPERTIES OUTPUT_NAME "f15se2-ex")
target_link_libraries(f15se2 PRIVATE f15se2_core)

# Autogenerate header file with version string
add_custom_target(GenerateVersionHeader ALL
    COMMAND ${CMAKE_COMMAND}
        -D SRC_DIR=${SRC_DIR}
        -P "GenerateVersion.cmake"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    BYPRODUCTS "${SRC_DIR}/generated/version.h"
)
add_dependencies(f15se2_core GenerateVersionHeader)

# Strip symbols from Release builds to shrink the binary (GNU/Clang only; MSVC
# already keeps debug info in a separate .pdb rather than the exe itself).
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_link_options(f15se2 PRIVATE $<$<CONFIG:Release>:-s>)
endif()

#==============================================================================
# Compiler warnings (game code, compiled once into the core library)
#==============================================================================
if(
    CMAKE_CXX_COMPILER_ID STREQUAL "GNU"
    OR
    (
        CMAKE_CXX_COMPILER_ID MATCHES "Clang"
        AND
        NOT CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC"
    )
)
    target_compile_options(f15se2_core PRIVATE
        -Wall
        -Wno-unused
        -Wno-unused-variable
        -Wno-unused-parameter
        -Wno-unused-function
        -Wno-unused-but-set-variable
        -Wno-deprecated-declarations
        -Wno-deprecated
        $<$<CXX_COMPILER_ID:Clang>:-ferror-limit=1000>
        $<$<CXX_COMPILER_ID:AppleClang>:-ferror-limit=1000>
        $<$<CXX_COMPILER_ID:GNU>:-fmax-errors=1000>
        -Wno-narrowing
    )
elseif(
    MSVC
    OR
    CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC"
)
    if(false)
        add_compile_options(
      $<$<CONFIG:Debug>:/fsanitize=address>)

        # https://learn.microsoft.com/en-us/cpp/sanitizers/error-container-overflow?view=msvc-170
        add_compile_definitions($<$<CONFIG:Debug>:_DISABLE_VECTOR_ANNOTATION>)
        add_compile_definitions($<$<CONFIG:Debug>:_DISABLE_STRING_ANNOTATION>)
		add_compile_definitions($<$<CONFIG:Debug>:_DISABLE_OPTIONAL_ANNOTATION>)
    endif()

    target_compile_options(f15se2_core PRIVATE
        /nologo
        /TP
        /W3
        /wd4100  # unreferenced formal parameter
        /wd4101  # unreferenced local variable
        /wd4189  # local variable initialized but not referenced
        /wd4996  # deprecated / unsafe CRT
        /wd4244  # conversion, possible loss of data
        /wd4267  # size_t -> smaller type
        /wd4305  # truncation
        /wd4311  # pointer truncation
        /wd4312  # conversion to pointer of greater size
    )
else()
    message(WARNING "Unknown C++ compiler: ${CMAKE_CXX_COMPILER_ID}")
endif()

# copy necessary DLLs into the build directory after build on Windows
if(WIN32)
    # sdl3.dll
    add_custom_command(TARGET f15se2 POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:SDL3::SDL3>
        $<TARGET_FILE_DIR:f15se2>
    )
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        # libc++.dll and libunwind.dll
        get_filename_component(COMPILER_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)
        add_custom_command(TARGET f15se2 POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${COMPILER_DIR}/libc++.dll"
            "${COMPILER_DIR}/libunwind.dll"
            $<TARGET_FILE_DIR:f15se2>
        )
    endif()
endif()

# HD assets (optional high-res sprite art, GPU backends only) live in assets/ and are
# mirrored next to the binary so the runtime finds them relative to the working dir.
# Driven by the asset files themselves (not the f15se2 target) so editing or adding a
# PNG re-copies it on the next build without a relink or `make clean`. CONFIGURE_DEPENDS
# re-globs at build time, so new files are picked up too; the stamp makes it incremental
# (the copy runs only when an asset actually changed).
file(GLOB_RECURSE ASSET_FILES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/assets/*)
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/assets.stamp
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/assets $<TARGET_FILE_DIR:f15se2>/assets
    COMMAND ${CMAKE_COMMAND} -E touch ${CMAKE_CURRENT_BINARY_DIR}/assets.stamp
    DEPENDS ${ASSET_FILES}
    COMMENT "Mirroring assets to the build dir"
)
add_custom_target(mirror_assets ALL DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/assets.stamp)

#==============================================================================
# Behavior tests
#==============================================================================
# Two linking models, chosen per test:
#   * LINK_CORE  - the test links the whole f15se2_core library and exercises
#                  real implementations (headless via SDL dummy drivers). This
#                  is the target model: the game compiles once, no inline stubs.
#                  Migrated/new tests use this.
#   * SOURCES    - the legacy isolation model: the test compiles only the few
#                  translation units it exercises and inline-stubs the rest.
#                  Not-yet-migrated tests still use this.
#
#   add_behavior_test(<name> [LINK_CORE] [SOURCES <.c/.cpp ...>] [DEFS <defs...>])
enable_testing()

set(TEST_DIR ${CMAKE_CURRENT_SOURCE_DIR}/tests)

function(add_behavior_test name)
    cmake_parse_arguments(T "LINK_CORE" "" "SOURCES;DEFS" ${ARGN})
    add_executable(${name} ${TEST_DIR}/${name}.cpp ${T_SOURCES})
    target_include_directories(${name} BEFORE PRIVATE
        ${SRC_DIR}/compat64
        ${SRC_DIR}
        ${SRC_DIR}/shared
        ${SRC_DIR}/generated
    )
    # Absolute path to the committed golden-image references (tests/golden.h).
    target_compile_definitions(${name} PRIVATE F15_GOLDEN_DIR="${TEST_DIR}/goldens")
    if(T_LINK_CORE)
        target_link_libraries(${name} PRIVATE f15se2_core Threads::Threads)
    else()
        target_link_libraries(${name} PRIVATE SDL3::SDL3 Threads::Threads)
    endif()
    if(T_DEFS)
        target_compile_definitions(${name} PRIVATE ${T_DEFS})
    endif()
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        target_compile_options(${name} PRIVATE -ffunction-sections -fdata-sections -Wno-narrowing)
        # Dead-strip unused sections: Apple ld spells it -dead_strip, GNU ld/lld --gc-sections.
        if(APPLE)
            target_link_options(${name} PRIVATE -Wl,-dead_strip)
        else()
            target_link_options(${name} PRIVATE -Wl,--gc-sections)
        endif()
    endif()
    add_test(NAME ${name} COMMAND ${name})
endfunction()

# --- Active: build green and pass under `ctest` ---------------------------------
add_behavior_test(award_behavior_tests SOURCES
    ${SRC_DIR}/enaward.c ${SRC_DIR}/endata.c)
add_behavior_test(end_file_behavior_tests LINK_CORE)
add_behavior_test(end_input_behavior_tests SOURCES
    ${SRC_DIR}/eninput.c ${SRC_DIR}/endata.c)
add_behavior_test(sys_behavior_tests SOURCES
    ${SRC_DIR}/egsys.c ${SRC_DIR}/egdata.c)
add_behavior_test(sphere_behavior_tests LINK_CORE)
add_behavior_test(load_behavior_tests SOURCES
    ${SRC_DIR}/eg3dload.c ${SRC_DIR}/egmath.c ${SRC_DIR}/egdata.c
    ${SRC_DIR}/egfarbuf.c)
add_behavior_test(shared_timer_behavior_tests LINK_CORE)
add_behavior_test(shared_runtime_behavior_tests SOURCES
    ${SRC_DIR}/shared/cleanup.c ${SRC_DIR}/shared/drawstr.c)
add_behavior_test(eg3drast_behavior_tests LINK_CORE)
add_behavior_test(tick_behavior_tests SOURCES
    ${SRC_DIR}/egtick.c ${SRC_DIR}/egdata.c)
add_behavior_test(start_parse_behavior_tests SOURCES
    ${SRC_DIR}/stparse.c ${SRC_DIR}/stdata.c)
add_behavior_test(original_behavior_tests LINK_CORE)
add_behavior_test(utility_behavior_tests LINK_CORE)
add_behavior_test(end_runtime_behavior_tests LINK_CORE)
add_behavior_test(geometry_behavior_tests LINK_CORE)
add_behavior_test(file_io_behavior_tests LINK_CORE)
add_behavior_test(asset_path_behavior_tests LINK_CORE)
add_behavior_test(png_asset_behavior_tests LINK_CORE)
if(F15SE2_ORIGINAL_ASSETS AND F15SE2_CONVERTED_ASSETS
   AND IS_DIRECTORY "${F15SE2_ORIGINAL_ASSETS}"
   AND IS_DIRECTORY "${F15SE2_CONVERTED_ASSETS}")
    add_behavior_test(png_asset_full_validation_tests LINK_CORE
        DEFS
            F15_ORIGINAL_ASSETS="${F15SE2_ORIGINAL_ASSETS}"
            F15_CONVERTED_ASSETS="${F15SE2_CONVERTED_ASSETS}")
    set_tests_properties(png_asset_full_validation_tests PROPERTIES
        LABELS "assets;integration"
        TIMEOUT 120)
else()
    message(STATUS
        "Skipping PNG full validation: set F15SE2_ORIGINAL_ASSETS and "
        "F15SE2_CONVERTED_ASSETS to existing directories")
endif()
add_behavior_test(gameplay_behavior_tests LINK_CORE)
add_behavior_test(input_behavior_tests LINK_CORE)
add_behavior_test(gfx_render_behavior_tests LINK_CORE)
add_behavior_test(view_behavior_tests LINK_CORE)
add_behavior_test(egsys_internal_behavior_tests LINK_CORE)
add_behavior_test(start_runtime_behavior_tests SOURCES
    ${SRC_DIR}/stinit.c ${SRC_DIR}/stalloc.c ${SRC_DIR}/stsprit.c ${SRC_DIR}/stdata.c)
add_behavior_test(shared_overlay_behavior_tests LINK_CORE)
add_behavior_test(shared_misc_behavior_tests)
add_behavior_test(shared_picimpl_behavior_tests SOURCES ${SRC_DIR}/shared/picimpl.c)
add_behavior_test(eg_stubs_behavior_tests LINK_CORE)
add_behavior_test(eg3drast_internal_behavior_tests SOURCES ${SRC_DIR}/egdata.c)
add_behavior_test(start_behavior_tests LINK_CORE)
add_behavior_test(log_behavior_tests LINK_CORE)
add_behavior_test(vgapal_behavior_tests SOURCES ${SRC_DIR}/vgapal.c DEFS main=vgapal_program_main)


#------------------------------------------------------------------------------
# Coverage/inventory audits (opt-in custom targets; not part of `ctest`).
#------------------------------------------------------------------------------
add_custom_target(function_coverage_audit
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
    COMMAND python3 ${TEST_DIR}/function_coverage_audit.py ${CMAKE_BINARY_DIR}
    COMMENT "Running tests and auditing source function coverage from unit-test object files"
)
add_custom_target(source_line_coverage_audit
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
    COMMAND python3 ${TEST_DIR}/function_coverage_audit.py ${CMAKE_BINARY_DIR}
    COMMAND python3 ${TEST_DIR}/source_line_coverage_audit.py ${CMAKE_BINARY_DIR}
    COMMAND python3 ${TEST_DIR}/cross_layer_function_inventory.py --build-dir ${CMAKE_BINARY_DIR}
    COMMENT "Running tests and auditing source line coverage and cross-layer function inventory"
)

#------------------------------------------------------------------------------
# gcov/gcovr coverage report (requires -DCOVERAGE=ON and gcovr on PATH).
# `cmake --build build --target coverage` runs the tests and writes:
#   build/coverage.xml  - Cobertura XML for Codecov / CI
#   build/coverage.txt  - human-readable per-file summary
# Only src/ is measured; vendored SDL / OPL / digest and the tests themselves
# are excluded so the percentage reflects the F-15 rewrite.
#------------------------------------------------------------------------------
if(COVERAGE)
    find_program(GCOVR_EXECUTABLE gcovr)
    if(GCOVR_EXECUTABLE)
        add_custom_target(coverage
            COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
            COMMAND ${GCOVR_EXECUTABLE}
                    --root ${CMAKE_CURRENT_SOURCE_DIR}
                    --filter ${SRC_DIR}
                    --exclude ${TEST_DIR}
                    --gcov-ignore-parse-errors=negative_hits.warn
                    --print-summary
                    --xml-pretty --output ${CMAKE_BINARY_DIR}/coverage.xml
                    --txt ${CMAKE_BINARY_DIR}/coverage.txt
                    ${CMAKE_BINARY_DIR}
            WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
            COMMENT "Running tests and generating gcovr coverage report (coverage.xml / coverage.txt)"
            USES_TERMINAL
        )
    else()
        message(WARNING "COVERAGE=ON but gcovr not found; the `coverage` target is unavailable")
    endif()
endif()
