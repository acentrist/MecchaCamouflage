add_library(
    meccha_core
    STATIC
        src/config.cpp
        src/esp.cpp
        src/image_compositor.cpp
        src/image_guide.cpp
        src/image_mapping.cpp
        src/image_paint_plan.cpp
        src/image_profile_mapping.cpp
        src/image_project.cpp
        src/mesh_profile.cpp
        src/paint.cpp
        src/paint_appearance.cpp
        src/paint_appearance_fit.cpp
        src/paint_capture_geometry.cpp
        src/paint_capture_request.cpp
        src/paint_deformation.cpp
        src/paint_plan.cpp
        src/paint_preview.cpp
        src/paint_sampling_profile.cpp
        src/profile_paint_sampling.cpp
        src/png_encoder.cpp
        src/utf8.cpp
)
add_library(MecchaCamouflage::Core ALIAS meccha_core)

target_include_directories(
    meccha_core
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
)
target_link_libraries(meccha_core PRIVATE meccha_build_options)
