# Get version from Git. If not a git repo or no tags, fallback gracefully.
execute_process(
    COMMAND git describe --tags --always --dirty
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    OUTPUT_VARIABLE GIT_VERSION
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)

if(NOT GIT_VERSION)
    set(GIT_VERSION "v0.0.0-unknown")
endif()
message(STATUS "Using version number ${GIT_VERSION}")

# Configure the file. configure_file is smart: it only writes
# the output file if the content actually changed, preventing useless rebuilds.
configure_file(
    "${SRC_DIR}/version.h.in"
    "${SRC_DIR}/generated/version.h"
    @ONLY
)