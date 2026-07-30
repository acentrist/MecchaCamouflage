add_library(
    meccha_ui
    STATIC
        src/canvas.cpp
        src/input_lease.cpp
        src/interaction.cpp
        src/layout.cpp
        src/scroll.cpp
        src/text_edit.cpp
        src/widgets.cpp
)
add_library(MecchaCamouflage::Ui ALIAS meccha_ui)

target_include_directories(
    meccha_ui
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
)
target_link_libraries(
    meccha_ui
    PUBLIC
        MecchaCamouflage::Core
    PRIVATE
        meccha_build_options
)
