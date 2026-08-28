# Version generation from git describe
# Runs at CMake configure time

# Because the describe below is evaluated at configure time only, CMake has to
# re-configure whenever HEAD, the index or the reflog moves. Without this an
# incremental `idf.py build` into an existing build directory keeps stamping the
# version captured when that directory was first configured, and the stale
# string flows into esp_app_desc and into sdf_ota_version_compare()'s
# upgrade/downgrade gate - i.e. the image lies about which firmware it is.
# (An unstaged edit that merely flips clean -> dirty does not touch any of these
# files, so the -dirty suffix alone can still lag by one build.)
get_filename_component(SDF_GIT_DIR "${CMAKE_CURRENT_LIST_DIR}/../../.git" ABSOLUTE)
foreach(sdf_git_watch HEAD index logs/HEAD)
    if(EXISTS "${SDF_GIT_DIR}/${sdf_git_watch}")
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
                     "${SDF_GIT_DIR}/${sdf_git_watch}")
    endif()
endforeach()

find_package(Git QUIET)

if(GIT_FOUND)
    execute_process(
        COMMAND git describe --tags --always --dirty
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE SDF_GIT_VERSION
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE GIT_RESULT
    )

    if(GIT_RESULT EQUAL 0)
        message(STATUS "Git version: ${SDF_GIT_VERSION}")
        set(PROJECT_VER "${SDF_GIT_VERSION}" CACHE STRING "Firmware version from git" FORCE)
    else()
        message(WARNING "git describe failed, using fallback version")
        string(TIMESTAMP SDF_FALLBACK_VERSION "%Y%m%d%H%M%S" UTC)
        set(PROJECT_VER "v0.0.0-${SDF_FALLBACK_VERSION}" CACHE STRING "Firmware version fallback" FORCE)
    endif()
else()
    message(WARNING "Git not found, using fallback version")
    string(TIMESTAMP SDF_FALLBACK_VERSION "%Y%m%d%H%M%S" UTC)
    set(PROJECT_VER "v0.0.0-${SDF_FALLBACK_VERSION}" CACHE STRING "Firmware version fallback" FORCE)
endif()

# Generate version.c with the version string
configure_file(
    ${CMAKE_CURRENT_LIST_DIR}/version.c.in
    ${CMAKE_CURRENT_BINARY_DIR}/version.c
    @ONLY
)

# Add the generated version.c to the build
set(SDF_VERSION_C "${CMAKE_CURRENT_BINARY_DIR}/version.c")