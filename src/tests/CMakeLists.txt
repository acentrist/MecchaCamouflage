find_package(Python3 REQUIRED COMPONENTS Interpreter)

add_executable(meccha_build_identity_test build_identity_test.cpp)
target_link_libraries(
    meccha_build_identity_test
    PRIVATE
        meccha_build_identity
        meccha_build_options
)

add_test(NAME build_identity COMMAND meccha_build_identity_test)

add_executable(
    meccha_application_runtime_tests
    application_runtime_test.cpp
)
target_link_libraries(
    meccha_application_runtime_tests
    PRIVATE
        MecchaCamouflage::Application
        meccha_build_options
)
add_test(NAME application_runtime COMMAND meccha_application_runtime_tests)

add_executable(
    meccha_application_state_tests
    application_state_test.cpp
)
target_link_libraries(
    meccha_application_state_tests
    PRIVATE
        MecchaCamouflage::Application
        meccha_build_options
)
add_test(NAME application_state COMMAND meccha_application_state_tests)

add_executable(meccha_core_contract_tests core_contract_test.cpp)
target_link_libraries(
    meccha_core_contract_tests
    PRIVATE
        MecchaCamouflage::Core
        meccha_build_options
)
add_test(NAME core_contract COMMAND meccha_core_contract_tests)

add_executable(meccha_launcher_tests launcher_manifest_test.cpp)
target_link_libraries(
    meccha_launcher_tests
    PRIVATE
        MecchaCamouflage::LauncherCore
        meccha_build_options
)
add_test(NAME launcher_manifest COMMAND meccha_launcher_tests)

add_executable(meccha_launcher_policy_tests launcher_policy_test.cpp)
target_link_libraries(
    meccha_launcher_policy_tests
    PRIVATE
        MecchaCamouflage::LauncherCore
        meccha_build_options
)
add_test(NAME launcher_policy COMMAND meccha_launcher_policy_tests)

add_executable(meccha_loader_resolution_tests loader_resolution_test.cpp)
target_link_libraries(
    meccha_loader_resolution_tests
    PRIVATE
        MecchaCamouflage::LauncherCore
        meccha_build_options
)
add_test(NAME loader_resolution COMMAND meccha_loader_resolution_tests)

add_executable(
    meccha_loader_observation_tests
    loader_observation_test.cpp
)
target_link_libraries(
    meccha_loader_observation_tests
    PRIVATE
        MecchaCamouflage::LauncherCore
        meccha_build_options
)
add_test(NAME loader_observation COMMAND meccha_loader_observation_tests)

add_executable(meccha_game_discovery_tests game_discovery_test.cpp)
target_link_libraries(
    meccha_game_discovery_tests
    PRIVATE
        MecchaCamouflage::LauncherCore
        meccha_build_options
)
add_test(NAME game_discovery COMMAND meccha_game_discovery_tests)

add_executable(meccha_runtime_transaction_tests runtime_transaction_test.cpp)
target_link_libraries(
    meccha_runtime_transaction_tests
    PRIVATE
        MecchaCamouflage::LauncherCore
        meccha_build_options
)
add_test(NAME runtime_transaction COMMAND meccha_runtime_transaction_tests)

if(WIN32)
    add_executable(
        meccha_runtime_storage_tests
        runtime_storage_win32_test.cpp
    )
    target_link_libraries(
        meccha_runtime_storage_tests
        PRIVATE
            MecchaCamouflage::LauncherCore
            meccha_build_options
    )
    add_test(NAME runtime_storage COMMAND meccha_runtime_storage_tests)
endif()

add_test(
    NAME phase1_contracts
    COMMAND
        "${Python3_EXECUTABLE}"
        "${PROJECT_SOURCE_DIR}/tools/v2/verify_phase1.py"
)
add_test(
    NAME source_graph
    COMMAND
        "${Python3_EXECUTABLE}"
        "${PROJECT_SOURCE_DIR}/tools/v2/verify_source_graph.py"
)
add_test(
    NAME ci_policy
    COMMAND
        "${Python3_EXECUTABLE}"
        "${PROJECT_SOURCE_DIR}/tools/v2/verify_ci_policy.py"
)
