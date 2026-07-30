add_library(
    meccha_core
    STATIC
        src/esp.cpp
        src/image_mapping.cpp
        src/image_project.cpp
        src/paint.cpp
)
add_library(MecchaCamouflage::Core ALIAS meccha_core)

target_include_directories(
    meccha_core
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
)
target_link_libraries(meccha_core PRIVATE meccha_build_options)
