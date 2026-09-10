# SDL and the game share the browser thread; Asyncify preserves legacy waits.
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)

# Temporary backport: https://github.com/libsdl-org/SDL/issues/16289
function(f15_patch_sdl_missing_axes source_dir)
    set(source_file "${source_dir}/src/joystick/SDL_gamepad.c")
    file(READ "${source_file}" source)
    set(original "                    value = SDL_GetJoystickAxis(gamepad->joystick, binding->input.axis.axis);")
    set(replacement [=[                    const int input_axis = binding->input.axis.axis;
                    /* Missing axes would normalize to a half-pressed trigger. */
                    if (input_axis < 0 || input_axis >= gamepad->joystick->naxes) {
                        continue;
                    }
                    value = SDL_GetJoystickAxis(gamepad->joystick, input_axis);]=])
    string(FIND "${source}" "${replacement}" patched_position)
    if(NOT patched_position EQUAL -1)
        return()
    endif()
    string(FIND "${source}" "${original}" original_position)
    if(original_position EQUAL -1)
        message(FATAL_ERROR "SDL axis backport no longer matches; review or remove it for this SDL version")
    endif()
    string(REPLACE "${original}" "${replacement}" source "${source}")
    file(WRITE "${source_file}" "${source}")
endfunction()

function(f15_configure_browser target)
    set_target_properties(${target} PROPERTIES OUTPUT_NAME "f15se2-ex" SUFFIX ".js")
    target_link_options(${target} PRIVATE
        "-sASYNCIFY=1"
        "-sASYNCIFY_STACK_SIZE=131072"
        "-sSTACK_SIZE=1048576"
        "-sALLOW_MEMORY_GROWTH=1"
        # The GLES compatibility renderer uses client-side vertex arrays.
        "-sFULL_ES2=1"
        "-sFORCE_FILESYSTEM=1"
        "-sMODULARIZE=1"
        "-sEXPORT_NAME=createF15Game"
        "-sEXPORTED_RUNTIME_METHODS=['FS','callMain']"
        "-sEXIT_RUNTIME=1"
        "-lidbfs.js"
        "--pre-js=${PROJECT_SOURCE_DIR}/web/storage.js")
    foreach(file index.html launcher.js)
        configure_file("${PROJECT_SOURCE_DIR}/web/${file}"
            "${CMAKE_CURRENT_BINARY_DIR}/${file}" COPYONLY)
    endforeach()
endfunction()
