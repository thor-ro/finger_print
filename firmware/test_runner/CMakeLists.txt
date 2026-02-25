cmake_minimum_required(VERSION 3.16)

# Include the SDF components so the test runner can evaluate them
set(EXTRA_COMPONENT_DIRS "../components")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(sdf_test_runner)
