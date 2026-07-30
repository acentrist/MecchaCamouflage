if(NOT TARGET UE4SS)
    message(FATAL_ERROR "meccha_mod must be built from the pinned UE4SS graph.")
endif()

add_library(meccha_mod SHARED entry.cpp)
add_library(MecchaCamouflage::Mod ALIAS meccha_mod)

target_link_libraries(
    meccha_mod
    PRIVATE
        meccha_build_identity
        meccha_build_options
    PUBLIC
        UE4SS
)
target_compile_definitions(
    meccha_mod
    PRIVATE
        MECCHA_PRODUCT_VERSION="${PROJECT_VERSION}"
)
set_target_properties(
    meccha_mod
    PROPERTIES
        OUTPUT_NAME "main"
        PREFIX ""
)
