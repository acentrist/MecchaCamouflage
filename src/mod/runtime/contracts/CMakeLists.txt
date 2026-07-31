add_library(
    meccha_runtime_contracts
    STATIC
        src/reflection_contract.cpp
        src/paint_call_codec.cpp
        src/unreal_contracts.cpp
)
add_library(
    MecchaCamouflage::RuntimeContracts
    ALIAS meccha_runtime_contracts
)

target_include_directories(
    meccha_runtime_contracts
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
)
target_link_libraries(
    meccha_runtime_contracts
    PUBLIC
        MecchaCamouflage::Application
    PRIVATE
        meccha_build_options
)
