cmake_minimum_required(VERSION 3.22)

project(MecchaCamouflage VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(UE4SS_ROOT "" CACHE PATH "Path to a local RE-UE4SS checkout")
if(NOT UE4SS_ROOT AND DEFINED ENV{UE4SS_ROOT})
    set(UE4SS_ROOT "$ENV{UE4SS_ROOT}")
endif()
if(NOT UE4SS_ROOT)
    set(UE4SS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/external/RE-UE4SS")
endif()
if(NOT EXISTS "${UE4SS_ROOT}/CMakeLists.txt")
    message(FATAL_ERROR "RE-UE4SS was not found. Clone it to external/RE-UE4SS or pass -DUE4SS_ROOT=/path/to/RE-UE4SS.")
endif()

add_subdirectory("${UE4SS_ROOT}" "${CMAKE_BINARY_DIR}/RE-UE4SS")
add_subdirectory(cppmods/MecchaCamouflage)
