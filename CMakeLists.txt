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
    ${SRC_DIR}/biosfunc.c
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
    ${SRC_DIR}/egfileio.c
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
    ${SRC_DIR}/enworld.c
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
add_executable(f15se2 ${F15SE2_SOURCES})
add_executable(original_behavior_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/original_behavior_tests.cpp
    ${SRC_DIR}/eg3dmath.c
    ${SRC_DIR}/egmath.c
    ${SRC_DIR}/egflight.c
    ${SRC_DIR}/eghudm.c
    ${SRC_DIR}/egdata.c
)
add_executable(utility_behavior_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/utility_behavior_tests.cpp
    ${SRC_DIR}/shared/miscimpl.c
    ${SRC_DIR}/shared/filepic.c
    ${SRC_DIR}/shared/textfmt.c
    ${SRC_DIR}/shared/drawstr.c
    ${SRC_DIR}/eg3dload.c
    ${SRC_DIR}/enbrief.c
    ${SRC_DIR}/enworld.c
    ${SRC_DIR}/enmisc.c
    ${SRC_DIR}/endata.c
    ${SRC_DIR}/strand.c
    ${SRC_DIR}/stgen.c
    ${SRC_DIR}/stterr.c
)
add_executable(award_behavior_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/award_behavior_tests.cpp
    ${SRC_DIR}/enaward.c
    ${SRC_DIR}/endata.c
)
add_executable(end_file_behavior_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/end_file_behavior_tests.cpp
    ${SRC_DIR}/enfile.c
    ${SRC_DIR}/enrand.c
    ${SRC_DIR}/endata.c
)
add_executable(end_input_behavior_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/end_input_behavior_tests.cpp
    ${SRC_DIR}/eninput.c
    ${SRC_DIR}/endata.c
)
add_executable(end_runtime_behavior_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/end_runtime_behavior_tests.cpp
    ${SRC_DIR}/enmain.c
)
add_executable(end_debrief_behavior_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/end_debrief_behavior_tests.cpp
    ${SRC_DIR}/endbrf.c
    ${SRC_DIR}/endata.c
)
add_executable(geometry_behavior_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/geometry_behavior_tests.cpp
    ${SRC_DIR}/eg3dproj.c
    ${SRC_DIR}/egtacmap.c
    ${SRC_DIR}/egui.c
    ${SRC_DIR}/egtgt2.c
    ${SRC_DIR}/eg3dvp.c
    ${SRC_DIR}/eg3dgrid.c
    ${SRC_DIR}/eg3dmap.c
    ${SRC_DIR}/eg3dcam.c
    ${SRC_DIR}/eg3dmath.c
    ${SRC_DIR}/egmath.c
    ${SRC_DIR}/egdata.c
)
add_executable(file_io_behavior_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/file_io_behavior_tests.cpp
    ${SRC_DIR}/shared/file_io.c
)
add_executable(gameplay_behavior_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/gameplay_behavior_tests.cpp
    ${SRC_DIR}/egthreat.c
    ${SRC_DIR}/egcombat.c
    ${SRC_DIR}/egtarget.c
    ${SRC_DIR}/egframe.c
    ${SRC_DIR}/egkeys.c
    ${SRC_DIR}/egmath.c
    ${SRC_DIR}/egdata.c
)
add_executable(input_behavior_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/input_behavior_tests.cpp
    ${SRC_DIR}/eginput.c
    ${SRC_DIR}/egdata.c
)
add_executable(hud_raster_behavior_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/hud_raster_behavior_tests.cpp
    ${SRC_DIR}/eghudr.c
    ${SRC_DIR}/eghudm.c
    ${SRC_DIR}/egdata.c
)
add_executable(view_behavior_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/view_behavior_tests.cpp
    ${SRC_DIR}/eg3dview.c
    ${SRC_DIR}/egdata.c
)
add_executable(sys_behavior_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/sys_behavior_tests.cpp
    ${SRC_DIR}/egsys.c
    ${SRC_DIR}/egdata.c
)
add_executable(egsys_internal_behavior_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/egsys_internal_behavior_tests.cpp
    ${SRC_DIR}/egdata.c
)
add_executable(io_wrapper_behavior_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/io_wrapper_behavior_tests.cpp
    ${SRC_DIR}/egfileio.c
)
add_executable(farbuf_behavior_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/farbuf_behavior_tests.cpp
    ${SRC_DIR}/egfarbuf.c
)
add_executable(pic_behavior_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/pic_behavior_tests.cpp
    ${SRC_DIR}/egpic.c
)
add_executable(sphere_behavior_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/sphere_behavior_tests.cpp
    ${SRC_DIR}/egsphere.c
    ${SRC_DIR}/egdata.c
)
add_executable(load_behavior_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/load_behavior_tests.cpp
    ${SRC_DIR}/eg3dload.c
    ${SRC_DIR}/egmath.c
    ${SRC_DIR}/egdata.c
    ${SRC_DIR}/egfarbuf.c
)
add_executable(start_runtime_behavior_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/start_runtime_behavior_tests.cpp
    ${SRC_DIR}/stinit.c
    ${SRC_DIR}/stalloc.c
    ${SRC_DIR}/stsprit.c
    ${SRC_DIR}/stdata.c
)
add_executable(shared_timer_behavior_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/shared_timer_behavior_tests.cpp
    ${SRC_DIR}/shared/timer.c
)
add_executable(shared_runtime_behavior_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/shared_runtime_behavior_tests.cpp
    ${SRC_DIR}/shared/cleanup.c
    ${SRC_DIR}/shared/drawstr.c
)
add_executable(shared_overlay_behavior_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/shared_overlay_behavior_tests.cpp
    ${SRC_DIR}/shared/ovlimpl.c
)
add_executable(shared_misc_behavior_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/shared_misc_behavior_tests.cpp
)
add_executable(shared_picimpl_behavior_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/shared_picimpl_behavior_tests.cpp
    ${SRC_DIR}/shared/picimpl.c
)
add_executable(eg_stubs_behavior_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/eg_stubs_behavior_tests.cpp
    ${SRC_DIR}/egstubs.c
)
add_executable(eg_main_behavior_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/eg_main_behavior_tests.cpp
    ${SRC_DIR}/egmain.c
    ${SRC_DIR}/egdata.c
    ${SRC_DIR}/egfarbuf.c
)
add_executable(eg3drast_behavior_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/eg3drast_behavior_tests.cpp
    ${SRC_DIR}/eg3drast.c
    ${SRC_DIR}/egdata.c
)
add_executable(eg3drast_internal_behavior_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/eg3drast_internal_behavior_tests.cpp
    ${SRC_DIR}/egdata.c
)
add_executable(tick_behavior_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/tick_behavior_tests.cpp
    ${SRC_DIR}/egtick.c
    ${SRC_DIR}/egdata.c
)
add_executable(start_behavior_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/start_behavior_tests.cpp
    ${SRC_DIR}/stgen.c
    ${SRC_DIR}/strand.c
    ${SRC_DIR}/stparse.c
    ${SRC_DIR}/stterr.c
    ${SRC_DIR}/stmissn.c
    ${SRC_DIR}/stpilot.c
    ${SRC_DIR}/stdata.c
    ${SRC_DIR}/entext.c
    ${SRC_DIR}/shared/filepic.c
)
add_executable(start_parse_behavior_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/start_parse_behavior_tests.cpp
    ${SRC_DIR}/stparse.c
    ${SRC_DIR}/stdata.c
)
add_executable(launcher_runtime_behavior_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/launcher_runtime_behavior_tests.cpp
    ${SRC_DIR}/f15.c
)
add_executable(log_behavior_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/log_behavior_tests.cpp
    ${SRC_DIR}/log.c
)
add_executable(dosfunc_behavior_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/dosfunc_behavior_tests.cpp
)
add_executable(vgapal_behavior_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/vgapal_behavior_tests.cpp
    ${SRC_DIR}/vgapal.c
)
add_executable(vgapal_internal_behavior_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/vgapal_internal_behavior_tests.cpp
)
add_executable(start_main_autostart_behavior_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/start_main_autostart_behavior_tests.cpp
    ${SRC_DIR}/stmain.c
)
add_executable(start_main_behavior_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/start_main_behavior_tests.cpp
    ${SRC_DIR}/stmain.c
)
add_executable(gfx_impl_behavior_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/gfx_impl_behavior_tests.cpp
    ${SRC_DIR}/gfx_impl.c
)
add_executable(biosfunc_behavior_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/biosfunc_behavior_tests.cpp
)

set_target_properties(f15se2 PROPERTIES
    OUTPUT_NAME "f15se2-ex"
    PREFIX ""
)

# Compile C as C++ for extra linting.
set_source_files_properties(${F15SE2_SOURCES} PROPERTIES LANGUAGE CXX)

# Nuked-OPL3 stays plain C (not in the CXX-forced list above).
target_sources(f15se2 PRIVATE ${nuked_opl3_SOURCE_DIR}/opl3.c)

# Keep the new log.h diagnostics live (the Log* macros compile out without DEBUG).
target_compile_definitions(f15se2 PRIVATE DEBUG)

if(AUTOSTART)
    target_compile_definitions(f15se2 PRIVATE DEBUG_AUTOSTART)
endif()

target_include_directories(f15se2 BEFORE PRIVATE
    ${SRC_DIR}/compat64
    ${SRC_DIR}
    ${SRC_DIR}/shared
    ${SRC_DIR}/asound
    ${nuked_opl3_SOURCE_DIR}
)
target_include_directories(original_behavior_tests BEFORE PRIVATE
    ${SRC_DIR}/compat64
    ${SRC_DIR}
    ${SRC_DIR}/shared
)
target_include_directories(utility_behavior_tests BEFORE PRIVATE
    ${SRC_DIR}/compat64
    ${SRC_DIR}
    ${SRC_DIR}/shared
)
target_include_directories(award_behavior_tests BEFORE PRIVATE
    ${SRC_DIR}/compat64
    ${SRC_DIR}
    ${SRC_DIR}/shared
)
target_include_directories(end_file_behavior_tests BEFORE PRIVATE
    ${SRC_DIR}/compat64
    ${SRC_DIR}
    ${SRC_DIR}/shared
)
target_include_directories(end_input_behavior_tests BEFORE PRIVATE
    ${SRC_DIR}/compat64
    ${SRC_DIR}
    ${SRC_DIR}/shared
)
target_include_directories(end_runtime_behavior_tests BEFORE PRIVATE
    ${SRC_DIR}/compat64
    ${SRC_DIR}
    ${SRC_DIR}/shared
)
target_include_directories(end_debrief_behavior_tests BEFORE PRIVATE
    ${SRC_DIR}/compat64
    ${SRC_DIR}
    ${SRC_DIR}/shared
)
target_include_directories(geometry_behavior_tests BEFORE PRIVATE
    ${SRC_DIR}/compat64
    ${SRC_DIR}
    ${SRC_DIR}/shared
)
target_include_directories(file_io_behavior_tests BEFORE PRIVATE
    ${SRC_DIR}/compat64
    ${SRC_DIR}
    ${SRC_DIR}/shared
)
target_include_directories(gameplay_behavior_tests BEFORE PRIVATE
    ${SRC_DIR}/compat64
    ${SRC_DIR}
    ${SRC_DIR}/shared
)
target_include_directories(input_behavior_tests BEFORE PRIVATE
    ${SRC_DIR}/compat64
    ${SRC_DIR}
    ${SRC_DIR}/shared
)
target_include_directories(hud_raster_behavior_tests BEFORE PRIVATE
    ${SRC_DIR}/compat64
    ${SRC_DIR}
    ${SRC_DIR}/shared
)
target_include_directories(view_behavior_tests BEFORE PRIVATE
    ${SRC_DIR}/compat64
    ${SRC_DIR}
    ${SRC_DIR}/shared
)
target_include_directories(sys_behavior_tests BEFORE PRIVATE
    ${SRC_DIR}/compat64
    ${SRC_DIR}
    ${SRC_DIR}/shared
)
target_include_directories(egsys_internal_behavior_tests BEFORE PRIVATE
    ${SRC_DIR}/compat64
    ${SRC_DIR}
    ${SRC_DIR}/shared
)
target_include_directories(io_wrapper_behavior_tests BEFORE PRIVATE
    ${SRC_DIR}/compat64
    ${SRC_DIR}
    ${SRC_DIR}/shared
)
target_include_directories(farbuf_behavior_tests BEFORE PRIVATE
    ${SRC_DIR}/compat64
    ${SRC_DIR}
    ${SRC_DIR}/shared
)
target_include_directories(pic_behavior_tests BEFORE PRIVATE
    ${SRC_DIR}/compat64
    ${SRC_DIR}
    ${SRC_DIR}/shared
)
target_include_directories(sphere_behavior_tests BEFORE PRIVATE
    ${SRC_DIR}/compat64
    ${SRC_DIR}
    ${SRC_DIR}/shared
)
target_include_directories(load_behavior_tests BEFORE PRIVATE
    ${SRC_DIR}/compat64
    ${SRC_DIR}
    ${SRC_DIR}/shared
)
target_include_directories(start_runtime_behavior_tests BEFORE PRIVATE
    ${SRC_DIR}/compat64
    ${SRC_DIR}
    ${SRC_DIR}/shared
)
target_include_directories(shared_timer_behavior_tests BEFORE PRIVATE
    ${SRC_DIR}/compat64
    ${SRC_DIR}
    ${SRC_DIR}/shared
)
target_include_directories(shared_runtime_behavior_tests BEFORE PRIVATE
    ${SRC_DIR}/compat64
    ${SRC_DIR}
    ${SRC_DIR}/shared
)
target_include_directories(shared_overlay_behavior_tests BEFORE PRIVATE
    ${SRC_DIR}/compat64
    ${SRC_DIR}
    ${SRC_DIR}/shared
)
target_include_directories(shared_misc_behavior_tests BEFORE PRIVATE
    ${SRC_DIR}/compat64
    ${SRC_DIR}
    ${SRC_DIR}/shared
)
target_include_directories(shared_picimpl_behavior_tests BEFORE PRIVATE
    ${SRC_DIR}/compat64
    ${SRC_DIR}
    ${SRC_DIR}/shared
)
target_include_directories(eg_stubs_behavior_tests BEFORE PRIVATE
    ${SRC_DIR}/compat64
    ${SRC_DIR}
    ${SRC_DIR}/shared
)
target_include_directories(eg_main_behavior_tests BEFORE PRIVATE
    ${SRC_DIR}/compat64
    ${SRC_DIR}
    ${SRC_DIR}/shared
)
target_include_directories(eg3drast_behavior_tests BEFORE PRIVATE
    ${SRC_DIR}/compat64
    ${SRC_DIR}
    ${SRC_DIR}/shared
)
target_include_directories(eg3drast_internal_behavior_tests BEFORE PRIVATE
    ${SRC_DIR}/compat64
    ${SRC_DIR}
    ${SRC_DIR}/shared
)
target_include_directories(tick_behavior_tests BEFORE PRIVATE
    ${SRC_DIR}/compat64
    ${SRC_DIR}
    ${SRC_DIR}/shared
)
target_include_directories(start_behavior_tests BEFORE PRIVATE
    ${SRC_DIR}/compat64
    ${SRC_DIR}
    ${SRC_DIR}/shared
)
target_include_directories(start_parse_behavior_tests BEFORE PRIVATE
    ${SRC_DIR}/compat64
    ${SRC_DIR}
    ${SRC_DIR}/shared
)
target_include_directories(launcher_runtime_behavior_tests BEFORE PRIVATE
    ${SRC_DIR}/compat64
    ${SRC_DIR}
    ${SRC_DIR}/shared
)
target_include_directories(log_behavior_tests BEFORE PRIVATE
    ${SRC_DIR}/compat64
    ${SRC_DIR}
    ${SRC_DIR}/shared
)
target_include_directories(dosfunc_behavior_tests BEFORE PRIVATE
    ${SRC_DIR}/compat64
    ${SRC_DIR}
    ${SRC_DIR}/shared
)
target_include_directories(vgapal_behavior_tests BEFORE PRIVATE
    ${SRC_DIR}/compat64
    ${SRC_DIR}
    ${SRC_DIR}/shared
)
target_include_directories(vgapal_internal_behavior_tests BEFORE PRIVATE
    ${SRC_DIR}/compat64
    ${SRC_DIR}
    ${SRC_DIR}/shared
)
target_include_directories(start_main_autostart_behavior_tests BEFORE PRIVATE
    ${SRC_DIR}/compat64
    ${SRC_DIR}
    ${SRC_DIR}/shared
)
target_include_directories(start_main_behavior_tests BEFORE PRIVATE
    ${SRC_DIR}/compat64
    ${SRC_DIR}
    ${SRC_DIR}/shared
)
target_include_directories(gfx_impl_behavior_tests BEFORE PRIVATE
    ${SRC_DIR}/compat64
    ${SRC_DIR}
    ${SRC_DIR}/shared
)
target_include_directories(biosfunc_behavior_tests BEFORE PRIVATE
    ${SRC_DIR}/compat64
    ${SRC_DIR}
    ${SRC_DIR}/shared
)

# SDL3::SDL3 is exported by both the system package and the FetchContent build,
# so this works regardless of how SDL3 was provided.
target_link_libraries(f15se2 PRIVATE SDL3::SDL3)
target_link_libraries(utility_behavior_tests PRIVATE SDL3::SDL3)
target_link_libraries(award_behavior_tests PRIVATE SDL3::SDL3)
target_link_libraries(end_file_behavior_tests PRIVATE SDL3::SDL3)
target_link_libraries(end_input_behavior_tests PRIVATE SDL3::SDL3)
target_link_libraries(file_io_behavior_tests PRIVATE SDL3::SDL3)
target_link_libraries(input_behavior_tests PRIVATE SDL3::SDL3)
target_link_libraries(shared_timer_behavior_tests PRIVATE SDL3::SDL3)
target_link_libraries(shared_overlay_behavior_tests PRIVATE SDL3::SDL3)
target_link_libraries(shared_picimpl_behavior_tests PRIVATE SDL3::SDL3)
target_link_libraries(start_behavior_tests PRIVATE SDL3::SDL3)
target_link_libraries(launcher_runtime_behavior_tests PRIVATE SDL3::SDL3)
target_link_libraries(log_behavior_tests PRIVATE SDL3::SDL3)
target_link_libraries(start_main_autostart_behavior_tests PRIVATE SDL3::SDL3)
target_link_libraries(start_main_behavior_tests PRIVATE SDL3::SDL3)
target_link_libraries(gfx_impl_behavior_tests PRIVATE SDL3::SDL3)
set_source_files_properties(
    ${SRC_DIR}/eg3dmath.c
    ${SRC_DIR}/egmath.c
    ${SRC_DIR}/egflight.c
    ${SRC_DIR}/eghudm.c
    ${SRC_DIR}/egdata.c
    ${SRC_DIR}/shared/miscimpl.c
    ${SRC_DIR}/shared/filepic.c
    ${SRC_DIR}/shared/textfmt.c
    ${SRC_DIR}/shared/drawstr.c
    ${SRC_DIR}/eg3dload.c
    ${SRC_DIR}/enbrief.c
    ${SRC_DIR}/enworld.c
    ${SRC_DIR}/enmisc.c
    ${SRC_DIR}/endata.c
    ${SRC_DIR}/strand.c
    ${SRC_DIR}/stgen.c
    ${SRC_DIR}/stterr.c
    ${SRC_DIR}/enaward.c
    ${SRC_DIR}/enfile.c
    ${SRC_DIR}/enrand.c
    ${SRC_DIR}/eninput.c
    ${SRC_DIR}/enmain.c
    ${SRC_DIR}/endbrf.c
    ${SRC_DIR}/eg3dproj.c
    ${SRC_DIR}/egtacmap.c
    ${SRC_DIR}/egui.c
    ${SRC_DIR}/egtgt2.c
    ${SRC_DIR}/eg3dvp.c
    ${SRC_DIR}/eg3dgrid.c
    ${SRC_DIR}/eg3dmap.c
    ${SRC_DIR}/eg3dcam.c
    ${SRC_DIR}/shared/file_io.c
    ${SRC_DIR}/egthreat.c
    ${SRC_DIR}/egcombat.c
    ${SRC_DIR}/egtarget.c
    ${SRC_DIR}/egframe.c
    ${SRC_DIR}/egkeys.c
    ${SRC_DIR}/eginput.c
    ${SRC_DIR}/eghudr.c
    ${SRC_DIR}/eg3dview.c
    ${SRC_DIR}/egsys.c
    ${SRC_DIR}/egfileio.c
    ${SRC_DIR}/egfarbuf.c
    ${SRC_DIR}/egpic.c
    ${SRC_DIR}/egsphere.c
    ${SRC_DIR}/eg3dload.c
    ${SRC_DIR}/stalloc.c
    ${SRC_DIR}/stsprit.c
    ${SRC_DIR}/shared/timer.c
    ${SRC_DIR}/shared/cleanup.c
    ${SRC_DIR}/shared/drawstr.c
    ${SRC_DIR}/shared/ovlimpl.c
    ${SRC_DIR}/shared/miscimpl.c
    ${SRC_DIR}/shared/picimpl.c
    ${SRC_DIR}/egstubs.c
    ${SRC_DIR}/egmain.c
    ${SRC_DIR}/eg3drast.c
    ${SRC_DIR}/egtick.c
    ${SRC_DIR}/stpilot.c
    ${SRC_DIR}/stdata.c
    ${SRC_DIR}/entext.c
    ${SRC_DIR}/stparse.c
    ${SRC_DIR}/f15.c
    ${SRC_DIR}/log.c
    ${SRC_DIR}/dosfunc.c
    ${SRC_DIR}/vgapal.c
    ${SRC_DIR}/stmain.c
    ${SRC_DIR}/gfx_impl.c
    PROPERTIES LANGUAGE CXX
)
if(
    CMAKE_CXX_COMPILER_ID STREQUAL "GNU"
    OR CMAKE_CXX_COMPILER_ID MATCHES "Clang"
)
    target_compile_options(original_behavior_tests PRIVATE -ffunction-sections -fdata-sections -Wno-narrowing)
    target_link_options(original_behavior_tests PRIVATE -Wl,--gc-sections)
    target_compile_options(utility_behavior_tests PRIVATE -ffunction-sections -fdata-sections -Wno-narrowing)
    target_link_options(utility_behavior_tests PRIVATE -Wl,--gc-sections)
    target_compile_options(award_behavior_tests PRIVATE -ffunction-sections -fdata-sections -Wno-narrowing)
    target_link_options(award_behavior_tests PRIVATE -Wl,--gc-sections)
    target_compile_options(end_file_behavior_tests PRIVATE -ffunction-sections -fdata-sections -Wno-narrowing)
    target_link_options(end_file_behavior_tests PRIVATE -Wl,--gc-sections)
    target_compile_options(end_input_behavior_tests PRIVATE -ffunction-sections -fdata-sections -Wno-narrowing)
    target_link_options(end_input_behavior_tests PRIVATE -Wl,--gc-sections)
    target_compile_options(end_runtime_behavior_tests PRIVATE -ffunction-sections -fdata-sections -Wno-narrowing)
    target_link_options(end_runtime_behavior_tests PRIVATE -Wl,--gc-sections)
    target_compile_options(end_debrief_behavior_tests PRIVATE -ffunction-sections -fdata-sections -Wno-narrowing)
    target_link_options(end_debrief_behavior_tests PRIVATE -Wl,--gc-sections)
    target_compile_options(geometry_behavior_tests PRIVATE -ffunction-sections -fdata-sections -Wno-narrowing)
    target_link_options(geometry_behavior_tests PRIVATE -Wl,--gc-sections)
    target_compile_options(file_io_behavior_tests PRIVATE -ffunction-sections -fdata-sections -Wno-narrowing)
    target_link_options(file_io_behavior_tests PRIVATE -Wl,--gc-sections)
    target_compile_options(gameplay_behavior_tests PRIVATE -ffunction-sections -fdata-sections -Wno-narrowing)
    target_link_options(gameplay_behavior_tests PRIVATE -Wl,--gc-sections)
    target_compile_options(input_behavior_tests PRIVATE -ffunction-sections -fdata-sections -Wno-narrowing)
    target_link_options(input_behavior_tests PRIVATE -Wl,--gc-sections)
    target_compile_options(hud_raster_behavior_tests PRIVATE -ffunction-sections -fdata-sections -Wno-narrowing)
    target_link_options(hud_raster_behavior_tests PRIVATE -Wl,--gc-sections)
    target_compile_options(view_behavior_tests PRIVATE -ffunction-sections -fdata-sections -Wno-narrowing)
    target_link_options(view_behavior_tests PRIVATE -Wl,--gc-sections)
    target_compile_options(sys_behavior_tests PRIVATE -ffunction-sections -fdata-sections -Wno-narrowing)
    target_link_options(sys_behavior_tests PRIVATE -Wl,--gc-sections)
    target_compile_options(egsys_internal_behavior_tests PRIVATE -ffunction-sections -fdata-sections -Wno-narrowing)
    target_link_options(egsys_internal_behavior_tests PRIVATE -Wl,--gc-sections)
    target_compile_options(io_wrapper_behavior_tests PRIVATE -ffunction-sections -fdata-sections -Wno-narrowing)
    target_link_options(io_wrapper_behavior_tests PRIVATE -Wl,--gc-sections)
    target_compile_options(farbuf_behavior_tests PRIVATE -ffunction-sections -fdata-sections -Wno-narrowing)
    target_link_options(farbuf_behavior_tests PRIVATE -Wl,--gc-sections)
    target_compile_options(pic_behavior_tests PRIVATE -ffunction-sections -fdata-sections -Wno-narrowing)
    target_link_options(pic_behavior_tests PRIVATE -Wl,--gc-sections)
    target_compile_options(sphere_behavior_tests PRIVATE -ffunction-sections -fdata-sections -Wno-narrowing)
    target_link_options(sphere_behavior_tests PRIVATE -Wl,--gc-sections)
    target_compile_options(load_behavior_tests PRIVATE -ffunction-sections -fdata-sections -Wno-narrowing)
    target_link_options(load_behavior_tests PRIVATE -Wl,--gc-sections)
    target_compile_options(start_runtime_behavior_tests PRIVATE -ffunction-sections -fdata-sections -Wno-narrowing)
    target_link_options(start_runtime_behavior_tests PRIVATE -Wl,--gc-sections)
    target_compile_options(shared_timer_behavior_tests PRIVATE -ffunction-sections -fdata-sections -Wno-narrowing)
    target_link_options(shared_timer_behavior_tests PRIVATE -Wl,--gc-sections)
    target_compile_options(shared_runtime_behavior_tests PRIVATE -ffunction-sections -fdata-sections -Wno-narrowing)
    target_link_options(shared_runtime_behavior_tests PRIVATE -Wl,--gc-sections)
    target_compile_options(shared_overlay_behavior_tests PRIVATE -ffunction-sections -fdata-sections -Wno-narrowing)
    target_link_options(shared_overlay_behavior_tests PRIVATE -Wl,--gc-sections)
    target_compile_options(shared_misc_behavior_tests PRIVATE -ffunction-sections -fdata-sections -Wno-narrowing)
    target_link_options(shared_misc_behavior_tests PRIVATE -Wl,--gc-sections)
    target_compile_options(shared_picimpl_behavior_tests PRIVATE -ffunction-sections -fdata-sections -Wno-narrowing)
    target_link_options(shared_picimpl_behavior_tests PRIVATE -Wl,--gc-sections)
    target_compile_options(eg_stubs_behavior_tests PRIVATE -ffunction-sections -fdata-sections -Wno-narrowing)
    target_link_options(eg_stubs_behavior_tests PRIVATE -Wl,--gc-sections)
    target_compile_options(eg_main_behavior_tests PRIVATE -ffunction-sections -fdata-sections -Wno-narrowing)
    target_link_options(eg_main_behavior_tests PRIVATE -Wl,--gc-sections)
    target_compile_options(eg3drast_behavior_tests PRIVATE -ffunction-sections -fdata-sections -Wno-narrowing)
    target_link_options(eg3drast_behavior_tests PRIVATE -Wl,--gc-sections)
    target_compile_options(eg3drast_internal_behavior_tests PRIVATE -ffunction-sections -fdata-sections -Wno-narrowing)
    target_link_options(eg3drast_internal_behavior_tests PRIVATE -Wl,--gc-sections)
    target_compile_options(tick_behavior_tests PRIVATE -ffunction-sections -fdata-sections -Wno-narrowing)
    target_link_options(tick_behavior_tests PRIVATE -Wl,--gc-sections)
    target_compile_options(start_behavior_tests PRIVATE -ffunction-sections -fdata-sections -Wno-narrowing)
    target_link_options(start_behavior_tests PRIVATE -Wl,--gc-sections)
    target_compile_options(start_parse_behavior_tests PRIVATE -ffunction-sections -fdata-sections -Wno-narrowing)
    target_link_options(start_parse_behavior_tests PRIVATE -Wl,--gc-sections)
    target_compile_options(launcher_runtime_behavior_tests PRIVATE -ffunction-sections -fdata-sections -Wno-narrowing)
    target_link_options(launcher_runtime_behavior_tests PRIVATE -Wl,--gc-sections)
    target_compile_options(log_behavior_tests PRIVATE -ffunction-sections -fdata-sections -Wno-narrowing)
    target_link_options(log_behavior_tests PRIVATE -Wl,--gc-sections)
    target_compile_options(dosfunc_behavior_tests PRIVATE -ffunction-sections -fdata-sections -Wno-narrowing)
    target_link_options(dosfunc_behavior_tests PRIVATE -Wl,--gc-sections)
    target_compile_options(vgapal_behavior_tests PRIVATE -ffunction-sections -fdata-sections -Wno-narrowing)
    target_link_options(vgapal_behavior_tests PRIVATE -Wl,--gc-sections)
    target_compile_options(vgapal_internal_behavior_tests PRIVATE -ffunction-sections -fdata-sections -Wno-narrowing)
    target_link_options(vgapal_internal_behavior_tests PRIVATE -Wl,--gc-sections)
    target_compile_options(start_main_autostart_behavior_tests PRIVATE -ffunction-sections -fdata-sections -Wno-narrowing)
    target_link_options(start_main_autostart_behavior_tests PRIVATE -Wl,--gc-sections)
    target_compile_options(start_main_behavior_tests PRIVATE -ffunction-sections -fdata-sections -Wno-narrowing)
    target_link_options(start_main_behavior_tests PRIVATE -Wl,--gc-sections)
    target_compile_options(gfx_impl_behavior_tests PRIVATE -ffunction-sections -fdata-sections -Wno-narrowing)
    target_link_options(gfx_impl_behavior_tests PRIVATE -Wl,--gc-sections)
    target_compile_options(biosfunc_behavior_tests PRIVATE -ffunction-sections -fdata-sections -Wno-narrowing)
    target_link_options(biosfunc_behavior_tests PRIVATE -Wl,--gc-sections)
endif()

target_compile_definitions(launcher_runtime_behavior_tests PRIVATE main=f15_program_main)
target_compile_definitions(vgapal_behavior_tests PRIVATE main=vgapal_program_main)
target_compile_definitions(start_main_autostart_behavior_tests PRIVATE DEBUG_AUTOSTART)

enable_testing()
add_test(NAME original_behavior_tests COMMAND original_behavior_tests)
add_test(NAME utility_behavior_tests COMMAND utility_behavior_tests)
add_test(NAME award_behavior_tests COMMAND award_behavior_tests)
add_test(NAME end_file_behavior_tests COMMAND end_file_behavior_tests)
add_test(NAME end_input_behavior_tests COMMAND end_input_behavior_tests)
add_test(NAME end_runtime_behavior_tests COMMAND end_runtime_behavior_tests)
add_test(NAME end_debrief_behavior_tests COMMAND end_debrief_behavior_tests)
add_test(NAME geometry_behavior_tests COMMAND geometry_behavior_tests)
add_test(NAME file_io_behavior_tests COMMAND file_io_behavior_tests)
add_test(NAME gameplay_behavior_tests COMMAND gameplay_behavior_tests)
add_test(NAME input_behavior_tests COMMAND input_behavior_tests)
add_test(NAME hud_raster_behavior_tests COMMAND hud_raster_behavior_tests)
add_test(NAME view_behavior_tests COMMAND view_behavior_tests)
add_test(NAME sys_behavior_tests COMMAND sys_behavior_tests)
add_test(NAME egsys_internal_behavior_tests COMMAND egsys_internal_behavior_tests)
add_test(NAME io_wrapper_behavior_tests COMMAND io_wrapper_behavior_tests)
add_test(NAME farbuf_behavior_tests COMMAND farbuf_behavior_tests)
add_test(NAME pic_behavior_tests COMMAND pic_behavior_tests)
add_test(NAME sphere_behavior_tests COMMAND sphere_behavior_tests)
add_test(NAME load_behavior_tests COMMAND load_behavior_tests)
add_test(NAME start_runtime_behavior_tests COMMAND start_runtime_behavior_tests)
add_test(NAME shared_timer_behavior_tests COMMAND shared_timer_behavior_tests)
add_test(NAME shared_runtime_behavior_tests COMMAND shared_runtime_behavior_tests)
add_test(NAME shared_overlay_behavior_tests COMMAND shared_overlay_behavior_tests)
add_test(NAME shared_misc_behavior_tests COMMAND shared_misc_behavior_tests)
add_test(NAME shared_picimpl_behavior_tests COMMAND shared_picimpl_behavior_tests)
add_test(NAME eg_stubs_behavior_tests COMMAND eg_stubs_behavior_tests)
add_test(NAME eg_main_behavior_tests COMMAND eg_main_behavior_tests)
add_test(NAME eg3drast_behavior_tests COMMAND eg3drast_behavior_tests)
add_test(NAME eg3drast_internal_behavior_tests COMMAND eg3drast_internal_behavior_tests)
add_test(NAME tick_behavior_tests COMMAND tick_behavior_tests)
add_test(NAME start_behavior_tests COMMAND start_behavior_tests)
add_test(NAME start_parse_behavior_tests COMMAND start_parse_behavior_tests)
add_test(NAME launcher_runtime_behavior_tests COMMAND launcher_runtime_behavior_tests)
add_test(NAME log_behavior_tests COMMAND log_behavior_tests)
add_test(NAME dosfunc_behavior_tests COMMAND dosfunc_behavior_tests)
add_test(NAME vgapal_behavior_tests COMMAND vgapal_behavior_tests)
add_test(NAME vgapal_internal_behavior_tests COMMAND vgapal_internal_behavior_tests)
add_test(NAME start_main_autostart_behavior_tests COMMAND start_main_autostart_behavior_tests)
add_test(NAME start_main_behavior_tests COMMAND start_main_behavior_tests)
add_test(NAME gfx_impl_behavior_tests COMMAND gfx_impl_behavior_tests)
add_test(NAME biosfunc_behavior_tests COMMAND biosfunc_behavior_tests)
add_test(NAME original_binary_function_harness
    COMMAND python3 /home/xor/games/f15se2-re/mzretools/original_binary_function_harness.py
)
add_custom_target(function_coverage_audit
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
    COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/tests/function_coverage_audit.py ${CMAKE_BINARY_DIR}
    COMMENT "Running tests and auditing source function coverage from unit-test object files"
)
add_custom_target(source_line_coverage_audit
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
    COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/tests/function_coverage_audit.py ${CMAKE_BINARY_DIR}
    COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/tests/source_line_coverage_audit.py ${CMAKE_BINARY_DIR}
    COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/tests/cross_layer_function_inventory.py --build-dir ${CMAKE_BINARY_DIR}
    COMMENT "Running tests and auditing source line coverage and cross-layer function inventory"
)

# OpenGL for the GL 3D backend (r3d_gl.c). The immediate-mode 1.x path needs only
# the base GL library; SDL3 supplies the GL headers and context management.
find_package(OpenGL REQUIRED)
target_link_libraries(f15se2 PRIVATE OpenGL::GL)

#==============================================================================
# Compiler warnings
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

    # find same definiton/extern mismatches as the MSVC linker using gcc/LTO
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
      # only available with gcc
      target_compile_options(f15se2 PRIVATE -flto)
      # break linking on mismatch errors
      target_link_options(f15se2 PRIVATE -flto -Werror=lto-type-mismatch)
    endif()

    target_compile_options(f15se2 PRIVATE
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

    # Sanitizers must be passed to both the compiler and the linker.
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
        target_compile_options(f15se2 PRIVATE
            -fsanitize=${SANITIZER_LIST}
            -fno-omit-frame-pointer
            -g                      # DWARF line info so ASan/UBSan reports symbolize to file:line
        )
        target_link_options(f15se2 PRIVATE -fsanitize=${SANITIZER_LIST})
    endif()

elseif(
    MSVC
    OR
    CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC"
)
    target_compile_options(f15se2 PRIVATE
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
