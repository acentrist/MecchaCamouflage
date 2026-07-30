add_library(
    meccha_application
    STATIC
        src/application_snapshot.cpp
        src/callback_barrier.cpp
        src/game_thread_scheduler.cpp
        src/job_state.cpp
        src/runtime_lifecycle.cpp
)
add_library(MecchaCamouflage::Application ALIAS meccha_application)

target_include_directories(
    meccha_application
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
)
target_link_libraries(
    meccha_application
    PUBLIC
        MecchaCamouflage::Core
    PRIVATE
        meccha_build_options
)
