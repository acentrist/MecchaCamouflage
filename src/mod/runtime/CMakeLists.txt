add_library(
    meccha_runtime
    STATIC
        src/unreal_runtime_adapter.cpp
)
add_library(MecchaCamouflage::Runtime ALIAS meccha_runtime)

target_include_directories(
    meccha_runtime
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
)
target_include_directories(
    meccha_runtime
    SYSTEM PRIVATE
        "${_meccha_ue4ss_root}/UE4SS/include"
        "${_meccha_ue4ss_root}/UE4SS/generated_include"
        "${_meccha_ue4ss_root}/deps/first/Unreal/include"
        "${_meccha_ue4ss_root}/deps/first/Unreal/generated_include"
)
target_link_libraries(
    meccha_runtime
    PUBLIC
        MecchaCamouflage::Application
        MecchaCamouflage::RuntimeContracts
    PRIVATE
        UE4SS
        meccha_build_options
)
