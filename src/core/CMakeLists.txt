add_library(meccha_core INTERFACE)
add_library(MecchaCamouflage::Core ALIAS meccha_core)

target_include_directories(
    meccha_core
    INTERFACE
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
)
target_link_libraries(meccha_core INTERFACE meccha_build_options)
