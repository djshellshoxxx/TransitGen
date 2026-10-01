cmake_minimum_required(VERSION 3.28)
project(TransitGen VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(TG_BUILD_TESTS  "Build the core unit tests" ON)
option(TG_BUILD_PLUGIN "Build the JUCE plugin wrapper (placeholder, not yet available)" OFF)
option(TG_SANITIZE     "Build with -fsanitize=address,undefined" OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

enable_testing()
add_subdirectory(core)

if(TG_BUILD_PLUGIN)
    # The JUCE wrapper lives in plugin/ (spec 07). Placeholder until it exists.
    message(WARNING "TG_BUILD_PLUGIN is ON but plugin/ is not implemented yet; skipping.")
endif()
