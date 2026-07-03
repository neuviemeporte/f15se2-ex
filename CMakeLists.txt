cmake_minimum_required(VERSION 3.22)

project(F15SE2-EX
    VERSION 0.1.0
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
    find_package(SDL3 3.2.0 QUIET CONFIG COMPONENTS SDL3)
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
            GIT_TAG "release-3.2.6"
            UPDATE_DISCONNECTED TRUE
        )
        FetchContent_MakeAvailable(SDL3)
    else()
        # DOWNLOAD_DEPENDENCIES is off: a system SDL3 is mandatory.
        find_package(SDL3 3.2.0 REQUIRED CONFIG COMPONENTS SDL3)
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
target_link_libraries(f15se2 PRIVATE f15se2_core)

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

# copy sdl3.dll into the build directory after build
if(WIN32)
    add_custom_command(TARGET f15se2 POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:SDL3::SDL3>
        $<TARGET_FILE_DIR:f15se2>
    )
endif()

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
    )
    if(T_LINK_CORE)
        target_link_libraries(${name} PRIVATE f15se2_core)
    else()
        target_link_libraries(${name} PRIVATE SDL3::SDL3)
    endif()
    if(T_DEFS)
        target_compile_definitions(${name} PRIVATE ${T_DEFS})
    endif()
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        target_compile_options(${name} PRIVATE -ffunction-sections -fdata-sections -Wno-narrowing)
        target_link_options(${name} PRIVATE -Wl,--gc-sections)
    endif()
    add_test(NAME ${name} COMMAND ${name})
endfunction()

# --- Active: build green and pass under `ctest` ---------------------------------
add_behavior_test(award_behavior_tests SOURCES
    ${SRC_DIR}/enaward.c ${SRC_DIR}/endata.c)
add_behavior_test(end_file_behavior_tests SOURCES
    ${SRC_DIR}/enfile.c ${SRC_DIR}/enrand.c ${SRC_DIR}/endata.c)
add_behavior_test(end_input_behavior_tests SOURCES
    ${SRC_DIR}/eninput.c ${SRC_DIR}/endata.c)
add_behavior_test(sys_behavior_tests SOURCES
    ${SRC_DIR}/egsys.c ${SRC_DIR}/egdata.c)
add_behavior_test(farbuf_behavior_tests LINK_CORE)
add_behavior_test(pic_behavior_tests SOURCES
    ${SRC_DIR}/egpic.c)
add_behavior_test(sphere_behavior_tests SOURCES
    ${SRC_DIR}/egsphere.c ${SRC_DIR}/egdata.c)
add_behavior_test(load_behavior_tests SOURCES
    ${SRC_DIR}/eg3dload.c ${SRC_DIR}/egmath.c ${SRC_DIR}/egdata.c
    ${SRC_DIR}/egfarbuf.c)
add_behavior_test(shared_timer_behavior_tests SOURCES
    ${SRC_DIR}/shared/timer.c)
add_behavior_test(shared_runtime_behavior_tests SOURCES
    ${SRC_DIR}/shared/cleanup.c ${SRC_DIR}/shared/drawstr.c)
add_behavior_test(eg3drast_behavior_tests SOURCES
    ${SRC_DIR}/eg3drast.c ${SRC_DIR}/egdata.c)
add_behavior_test(tick_behavior_tests SOURCES
    ${SRC_DIR}/egtick.c ${SRC_DIR}/egdata.c)
add_behavior_test(start_parse_behavior_tests SOURCES
    ${SRC_DIR}/stparse.c ${SRC_DIR}/stdata.c)

# --- Quarantined: diverged from current `main`; being migrated to LINK_CORE. ----
# See docs/test-migration.md. Uncomment each as it is migrated/salvaged (or delete
# if it tests a removed system). Original curated SOURCES kept for reference.
#
# add_behavior_test(original_behavior_tests SOURCES        # g_pageBack (gfx page rename)
#     ${SRC_DIR}/eg3dmath.c ${SRC_DIR}/egmath.c ${SRC_DIR}/egflight.c ${SRC_DIR}/eghudm.c ${SRC_DIR}/egdata.c)
# add_behavior_test(utility_behavior_tests SOURCES         # worldBuf/lastDrawX; enworld->worldxfer; gfx spies
#     ${SRC_DIR}/shared/miscimpl.c ${SRC_DIR}/shared/filepic.c ${SRC_DIR}/shared/textfmt.c
#     ${SRC_DIR}/shared/drawstr.c ${SRC_DIR}/eg3dload.c ${SRC_DIR}/enbrief.c ${SRC_DIR}/worldxfer.c
#     ${SRC_DIR}/enmisc.c ${SRC_DIR}/endata.c ${SRC_DIR}/strand.c ${SRC_DIR}/stgen.c ${SRC_DIR}/stterr.c
#     ${TEST_DIR}/r3d_render_stubs.cpp ${TEST_DIR}/utility_stubs.cpp)
# add_behavior_test(end_runtime_behavior_tests SOURCES ${SRC_DIR}/enmain.c)              # VGA_BUF_SIZE removed
# add_behavior_test(end_debrief_behavior_tests SOURCES ${SRC_DIR}/endbrf.c ${SRC_DIR}/endata.c)  # debrief render/data
# add_behavior_test(geometry_behavior_tests SOURCES       # g_drawPage; projection math may salvage
#     ${SRC_DIR}/eg3dproj.c ${SRC_DIR}/egtacmap.c ${SRC_DIR}/egui.c ${SRC_DIR}/egtgt2.c ${SRC_DIR}/eg3dvp.c
#     ${SRC_DIR}/eg3dgrid.c ${SRC_DIR}/eg3dmap.c ${SRC_DIR}/eg3dcam.c ${SRC_DIR}/eg3dmath.c
#     ${SRC_DIR}/egmath.c ${SRC_DIR}/egdata.c)
# add_behavior_test(file_io_behavior_tests SOURCES ${SRC_DIR}/shared/file_io.c)          # writeFileAtRaw renamed
# add_behavior_test(gameplay_behavior_tests SOURCES       # farPointer; huge, de-generate
#     ${SRC_DIR}/egthreat.c ${SRC_DIR}/egcombat.c ${SRC_DIR}/egtarget.c ${SRC_DIR}/egframe.c
#     ${SRC_DIR}/egkeys.c ${SRC_DIR}/egmath.c ${SRC_DIR}/egdata.c ${TEST_DIR}/r3d_render_stubs.cpp)
# add_behavior_test(input_behavior_tests SOURCES ${SRC_DIR}/eginput.c ${SRC_DIR}/egdata.c)
# add_behavior_test(hud_raster_behavior_tests SOURCES ${SRC_DIR}/eghudr.c ${SRC_DIR}/eghudm.c ${SRC_DIR}/egdata.c)  # render arch
# add_behavior_test(view_behavior_tests SOURCES ${SRC_DIR}/eg3dview.c ${SRC_DIR}/egdata.c)   # render arch
# add_behavior_test(egsys_internal_behavior_tests SOURCES ${SRC_DIR}/egdata.c)           # closeFile
# add_behavior_test(start_runtime_behavior_tests SOURCES                                  # page1Ptr (gfx page rename)
#     ${SRC_DIR}/stinit.c ${SRC_DIR}/stalloc.c ${SRC_DIR}/stsprit.c ${SRC_DIR}/stdata.c)
# add_behavior_test(shared_overlay_behavior_tests SOURCES ${SRC_DIR}/shared/ovlimpl.c)   # joystick reorg
# add_behavior_test(shared_misc_behavior_tests)                                           # pollJoystick (reorg)
# add_behavior_test(shared_picimpl_behavior_tests SOURCES ${SRC_DIR}/shared/picimpl.c)   # runtime assertion drift
# add_behavior_test(eg_stubs_behavior_tests SOURCES ${SRC_DIR}/egstubs.c)                 # loadF15DgtlBin
# add_behavior_test(eg_main_behavior_tests SOURCES ${SRC_DIR}/egmain.c ${SRC_DIR}/egdata.c ${SRC_DIR}/egfarbuf.c)  # gfxInit
# add_behavior_test(eg3drast_internal_behavior_tests SOURCES ${SRC_DIR}/egdata.c)         # installDivZeroHandler
# add_behavior_test(start_behavior_tests SOURCES          # moveDst; huge
#     ${SRC_DIR}/stgen.c ${SRC_DIR}/strand.c ${SRC_DIR}/stparse.c ${SRC_DIR}/stterr.c ${SRC_DIR}/stmissn.c
#     ${SRC_DIR}/stpilot.c ${SRC_DIR}/stdata.c ${SRC_DIR}/entext.c ${SRC_DIR}/shared/filepic.c)
# add_behavior_test(launcher_runtime_behavior_tests SOURCES ${SRC_DIR}/f15.c DEFS main=f15_program_main)
# add_behavior_test(log_behavior_tests SOURCES ${SRC_DIR}/log.c)
# add_behavior_test(dosfunc_behavior_tests)
# add_behavior_test(vgapal_behavior_tests SOURCES ${SRC_DIR}/vgapal.c DEFS main=vgapal_program_main)  # render arch
# add_behavior_test(vgapal_internal_behavior_tests)                                       # render arch
# add_behavior_test(start_main_autostart_behavior_tests SOURCES ${SRC_DIR}/stmain.c DEFS DEBUG_AUTOSTART)
# add_behavior_test(start_main_behavior_tests SOURCES ${SRC_DIR}/stmain.c)
# add_behavior_test(gfx_impl_behavior_tests SOURCES ${SRC_DIR}/gfx_impl.c ${TEST_DIR}/r3d_render_stubs.cpp)  # render arch


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
