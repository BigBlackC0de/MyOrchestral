cmake_minimum_required(VERSION 3.22)

project(MyOrchestral
    VERSION 0.1.0
    DESCRIPTION "Symphonic orchestra sampler plugin"
    LANGUAGES C CXX
)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# Universal binary by default on macOS. Override with -DCMAKE_OSX_ARCHITECTURES
# when a single-architecture build is faster to iterate on.
if(APPLE AND NOT CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
endif()

if(APPLE AND NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "" FORCE)
endif()

include(MoeWarnings)

option(MOE_BUILD_PLUGIN "Build the JUCE plugin targets" ON)
option(MOE_BUILD_TESTS  "Build the engine unit tests"   ON)

# ---------------------------------------------------------------------------
# The engine: no JUCE, no plugin SDK, buildable and testable on its own.
# ---------------------------------------------------------------------------
add_subdirectory(engine)

if(MOE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(MOE_BUILD_PLUGIN)
    include(JuceDependency)
    add_subdirectory(plugin)
endif()

message(STATUS "")
message(STATUS "MyOrchestral ${PROJECT_VERSION}")
message(STATUS "  build type ....... ${CMAKE_BUILD_TYPE}")
message(STATUS "  engine ........... always")
message(STATUS "  plugin ........... ${MOE_BUILD_PLUGIN}")
message(STATUS "  tests ............ ${MOE_BUILD_TESTS}")
if(APPLE)
    message(STATUS "  architectures .... ${CMAKE_OSX_ARCHITECTURES}")
    message(STATUS "  deployment target  ${CMAKE_OSX_DEPLOYMENT_TARGET}")
endif()
message(STATUS "")
