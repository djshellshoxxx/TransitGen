cmake_minimum_required(VERSION 3.28)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version (07 §1)")
project(TransitGen VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)   # core is linked into the plugin shared libraries

option(TG_BUILD_TESTS  "Build the core unit tests" ON)
option(TG_BUILD_PLUGIN "Build the JUCE plugin wrapper (VST3 / AU / Standalone)" OFF)
option(TG_SANITIZE     "Build with -fsanitize=address,undefined" OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

enable_testing()
add_subdirectory(core)

if(TG_BUILD_PLUGIN)
    add_subdirectory(plugin)
endif()
