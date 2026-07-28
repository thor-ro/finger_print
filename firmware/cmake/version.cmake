# Version generation from git describe
# Runs at CMake configure time

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