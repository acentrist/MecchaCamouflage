add_library(
    meccha_common
    STATIC
        src/hash.cpp
)
add_library(MecchaCamouflage::Common ALIAS meccha_common)

target_include_directories(
    meccha_common
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
)
target_link_libraries(meccha_common PRIVATE meccha_build_options)

if(WIN32)
    target_sources(meccha_common PRIVATE src/hash_win32.cpp)
    target_link_libraries(meccha_common PRIVATE bcrypt)
endif()
