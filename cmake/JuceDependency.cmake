# Resolves JUCE, in this order:
#
#   1. -DJUCE_SOURCE_DIR=/path/to/JUCE      an existing local checkout
#   2. ${CMAKE_SOURCE_DIR}/external/JUCE    a submodule or manual clone
#   3. FetchContent                         downloaded and cached in the build dir
#
# The first two keep configure offline and fast, which matters when iterating;
# the third is there so a fresh clone builds with nothing but CMake installed.

include(FetchContent)

set(MOE_JUCE_VERSION "8.0.4" CACHE STRING "JUCE version to fetch when no local copy is found")
set(JUCE_SOURCE_DIR "" CACHE PATH "Path to an existing JUCE checkout")

if(JUCE_SOURCE_DIR AND EXISTS "${JUCE_SOURCE_DIR}/CMakeLists.txt")
    message(STATUS "JUCE: using ${JUCE_SOURCE_DIR}")
    add_subdirectory(${JUCE_SOURCE_DIR} ${CMAKE_BINARY_DIR}/juce EXCLUDE_FROM_ALL)

elseif(EXISTS "${CMAKE_SOURCE_DIR}/external/JUCE/CMakeLists.txt")
    message(STATUS "JUCE: using external/JUCE")
    add_subdirectory(${CMAKE_SOURCE_DIR}/external/JUCE ${CMAKE_BINARY_DIR}/juce EXCLUDE_FROM_ALL)

else()
    message(STATUS "JUCE: fetching ${MOE_JUCE_VERSION}")
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        ${MOE_JUCE_VERSION}
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(JUCE)
endif()
