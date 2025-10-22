cmake_minimum_required(VERSION 3.26)
project(ladybird_windows LANGUAGES CXX)

# ---- General build setup ----
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# ---- Optional Ladybird submodule integration ----
option(LBW_WITH_UPSTREAM "Enable integration with Ladybird submodule (experimental)" OFF)

if(LBW_WITH_UPSTREAM)
    set(LBW_LADYBIRD_PATH "${CMAKE_SOURCE_DIR}/external/ladybird")
    if(EXISTS "${LBW_LADYBIRD_PATH}/CMakeLists.txt")
        message(STATUS "Ladybird submodule detected at: ${LBW_LADYBIRD_PATH}")
        # We only register it for IDE awareness, not to actually build it yet
        add_subdirectory(${LBW_LADYBIRD_PATH} EXCLUDE_FROM_ALL)
    else()
        message(WARNING "LBW_WITH_UPSTREAM=ON but no Ladybird source found at ${LBW_LADYBIRD_PATH}")
    endif()
endif()

# ---- Our own subdirectories ----
add_subdirectory(platform)
add_subdirectory(bootstrap)
