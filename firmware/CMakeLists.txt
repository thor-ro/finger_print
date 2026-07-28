cmake_minimum_required(VERSION 3.16)

# Include version generation before project() so PROJECT_VER is available
include(cmake/version.cmake)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(sdf)