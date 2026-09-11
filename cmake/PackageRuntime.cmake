# Install only the runnable game and its redistributed dependencies.
# The Runtime component avoids installing SDL development files and test tools.
if(APPLE)
    set_property(TARGET f15se2 PROPERTY INSTALL_RPATH "@loader_path")
elseif(UNIX)
    set_property(TARGET f15se2 PROPERTY INSTALL_RPATH "$ORIGIN")
endif()

install(TARGETS f15se2 RUNTIME DESTINATION . COMPONENT Runtime)
get_target_property(SDL_RUNTIME_TYPE SDL3::SDL3 TYPE)
if(SDL_RUNTIME_TYPE STREQUAL "SHARED_LIBRARY")
    install(FILES $<TARGET_FILE:SDL3::SDL3> DESTINATION . COMPONENT Runtime)
    if(UNIX)
        install(FILES $<TARGET_SONAME_FILE:SDL3::SDL3> DESTINATION . COMPONENT Runtime)
    endif()
endif()
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/assets/" DESTINATION assets COMPONENT Runtime)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE" DESTINATION . COMPONENT Runtime)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/docs/nightly.md"
        DESTINATION . RENAME README.md COMPONENT Runtime)
install(FILES "${nuked_opl3_SOURCE_DIR}/LICENSE"
        DESTINATION licenses RENAME Nuked-OPL3.txt COMPONENT Runtime)
install(FILES "${quick_digest5_SOURCE_DIR}/LICENSE"
        DESTINATION licenses RENAME QuickDigest5.txt COMPONENT Runtime)
if(SDL3_SOURCE_DIR)
    install(FILES "${SDL3_SOURCE_DIR}/LICENSE.txt"
            DESTINATION licenses RENAME SDL3.txt COMPONENT Runtime)
endif()
