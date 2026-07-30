find_package(Python3 REQUIRED COMPONENTS Interpreter)

add_executable(meccha_build_identity_test build_identity_test.cpp)
target_link_libraries(
    meccha_build_identity_test
    PRIVATE
        meccha_build_identity
        meccha_build_options
)

add_test(NAME build_identity COMMAND meccha_build_identity_test)

add_executable(meccha_launcher_tests launcher_manifest_test.cpp)
target_link_libraries(
    meccha_launcher_tests
    PRIVATE
        MecchaCamouflage::LauncherCore
        meccha_build_options
)
add_test(NAME launcher_manifest COMMAND meccha_launcher_tests)

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
