cmake_minimum_required(VERSION 3.22)

set(TARGET MecchaCamouflage)
project(${TARGET} LANGUAGES CXX)

add_library(${TARGET} SHARED
    src/dllmain.cpp
)

target_include_directories(${TARGET} PRIVATE include)
target_link_libraries(${TARGET} PUBLIC UE4SS)
set_target_properties(${TARGET} PROPERTIES OUTPUT_NAME "MecchaCamouflage")

if(MSVC)
    target_compile_options(${TARGET} PRIVATE /O2 /Oi /Ot /GL)
    set_source_files_properties(src/dllmain.cpp PROPERTIES COMPILE_OPTIONS /Ob3)
    target_link_options(${TARGET} PRIVATE /LTCG /OPT:REF /OPT:ICF)
endif()
