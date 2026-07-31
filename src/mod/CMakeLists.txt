if(NOT TARGET UE4SS)
    message(FATAL_ERROR "meccha_mod must be built from the pinned UE4SS graph.")
endif()

add_subdirectory(runtime)

add_library(meccha_mod SHARED entry.cpp)
add_library(MecchaCamouflage::Mod ALIAS meccha_mod)

target_link_libraries(
    meccha_mod
    PUBLIC
        UE4SS
    PRIVATE
        meccha_build_identity
        meccha_build_options
        MecchaCamouflage::Runtime
)
target_compile_definitions(
    meccha_mod
    PRIVATE
        MECCHA_PRODUCT_VERSION="${PROJECT_VERSION}"
)
if(MSVC)
    # Preserve UE4SS's required include order (notably its unqualified
    # <Common.hpp>) while excluding pinned dependency diagnostics from the
    # project-owned /W4 /WX boundary.
    target_compile_options(
        meccha_mod
        PRIVATE
            "/external:I$<TARGET_PROPERTY:UE4SS,SOURCE_DIR>/include"
            "/external:I$<TARGET_PROPERTY:UE4SS,SOURCE_DIR>/generated_include"
            /external:W0
    )
endif()
set_target_properties(
    meccha_mod
    PROPERTIES
        OUTPUT_NAME "main"
        PREFIX ""
)
