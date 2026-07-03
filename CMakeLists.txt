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
add_executable(f15se2 ${F15SE2_SOURCES})

set_target_properties(f15se2 PROPERTIES
    OUTPUT_NAME "f15se2-ex"
    PREFIX ""
)

# Compile C as C++ for extra linting.
set_source_files_properties(${F15SE2_SOURCES} PROPERTIES LANGUAGE CXX)

# Nuked-OPL3 stays plain C (not in the CXX-forced list above).
target_sources(f15se2 PRIVATE ${nuked_opl3_SOURCE_DIR}/opl3.c ${quick_digest5_SOURCE_DIR}/src/quickdigest5.cpp)

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
    ${quick_digest5_SOURCE_DIR}/include
)

# SDL3::SDL3 is exported by both the system package and the FetchContent build,
# so this works regardless of how SDL3 was provided.
target_link_libraries(f15se2 PRIVATE SDL3::SDL3)

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
