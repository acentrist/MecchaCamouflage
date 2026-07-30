add_library(
    meccha_launcher_core
    STATIC
        src/hash.cpp
        src/loader.cpp
        src/manifest.cpp
        src/ownership.cpp
        src/policy.cpp
        src/transaction.cpp
)
add_library(MecchaCamouflage::LauncherCore ALIAS meccha_launcher_core)

target_include_directories(
    meccha_launcher_core
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
)
target_link_libraries(
    meccha_launcher_core
    PUBLIC
        meccha_build_identity
        meccha_core
    PRIVATE
        glaze::glaze
        meccha_build_options
)

if(WIN32)
    target_sources(meccha_launcher_core PRIVATE src/hash_win32.cpp)
    target_link_libraries(meccha_launcher_core PRIVATE bcrypt)
endif()
