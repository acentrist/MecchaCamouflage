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

add_executable(
    meccha_application_command_queue_tests
    application_command_queue_test.cpp
)
target_link_libraries(
    meccha_application_command_queue_tests
    PRIVATE
        MecchaCamouflage::Application
        meccha_build_options
)
add_test(
    NAME application_command_queue
    COMMAND meccha_application_command_queue_tests
)

add_executable(
    meccha_application_root_tests
    application_root_test.cpp
)
target_link_libraries(
    meccha_application_root_tests
    PRIVATE
        MecchaCamouflage::Application
        meccha_build_options
)
add_test(NAME application_root COMMAND meccha_application_root_tests)

add_executable(
    meccha_application_root_paint_tests
    application_root_paint_test.cpp
)
target_link_libraries(
    meccha_application_root_paint_tests
    PRIVATE
        MecchaCamouflage::Application
        meccha_build_options
)
add_test(
    NAME application_root_paint
    COMMAND meccha_application_root_paint_tests
)

add_executable(
    meccha_application_root_image_paint_tests
    application_root_image_paint_test.cpp
)
target_link_libraries(
    meccha_application_root_image_paint_tests
    PRIVATE
        MecchaCamouflage::Application
        meccha_build_options
)
add_test(
    NAME application_root_image_paint
    COMMAND meccha_application_root_image_paint_tests
)

add_executable(
    meccha_runtime_operation_executor_tests
    runtime_operation_executor_test.cpp
)
target_link_libraries(
    meccha_runtime_operation_executor_tests
    PRIVATE
        MecchaCamouflage::Application
        meccha_build_options
)
add_test(
    NAME runtime_operation_executor
    COMMAND meccha_runtime_operation_executor_tests
)

add_executable(
    meccha_mesh_profile_codec_tests
    mesh_profile_codec_test.cpp
)
target_link_libraries(
    meccha_mesh_profile_codec_tests
    PRIVATE
        MecchaCamouflage::Application
        meccha_build_options
)
add_test(
    NAME mesh_profile_codec
    COMMAND
        meccha_mesh_profile_codec_tests
        "${PROJECT_SOURCE_DIR}/resources/mesh-profiles"
)

add_executable(
    meccha_image_project_codec_tests
    image_project_codec_test.cpp
)
target_link_libraries(
    meccha_image_project_codec_tests
    PRIVATE
        MecchaCamouflage::Application
        meccha_build_options
)
add_test(
    NAME image_project_codec
    COMMAND meccha_image_project_codec_tests
)

add_executable(
    meccha_image_project_store_tests
    image_project_store_test.cpp
)
target_link_libraries(
    meccha_image_project_store_tests
    PRIVATE
        MecchaCamouflage::Application
        meccha_build_options
)
add_test(
    NAME image_project_store
    COMMAND meccha_image_project_store_tests
)

add_executable(
    meccha_image_project_persistence_tests
    image_project_persistence_test.cpp
)
target_link_libraries(
    meccha_image_project_persistence_tests
    PRIVATE
        MecchaCamouflage::Application
        meccha_build_options
)
add_test(
    NAME image_project_persistence
    COMMAND meccha_image_project_persistence_tests
)

add_executable(
    meccha_image_project_io_worker_tests
    image_project_io_worker_test.cpp
)
target_link_libraries(
    meccha_image_project_io_worker_tests
    PRIVATE
        MecchaCamouflage::Application
        meccha_build_options
)
add_test(
    NAME image_project_io_worker
    COMMAND meccha_image_project_io_worker_tests
)

add_executable(
    meccha_image_editor_session_tests
    image_editor_session_test.cpp
)
target_link_libraries(
    meccha_image_editor_session_tests
    PRIVATE
        MecchaCamouflage::Application
        meccha_build_options
)
add_test(
    NAME image_editor_session
    COMMAND meccha_image_editor_session_tests
)

add_executable(
    meccha_config_persistence_tests
    config_persistence_test.cpp
)
target_link_libraries(
    meccha_config_persistence_tests
    PRIVATE
        MecchaCamouflage::Application
        meccha_build_options
)
add_test(NAME config_persistence COMMAND meccha_config_persistence_tests)

add_executable(
    meccha_localization_tests
    localization_test.cpp
)
target_link_libraries(
    meccha_localization_tests
    PRIVATE
        MecchaCamouflage::Application
        meccha_build_options
)
add_test(
    NAME localization
    COMMAND
        meccha_localization_tests
        "${PROJECT_SOURCE_DIR}/resources/localization/catalog.json"
)

add_executable(meccha_core_contract_tests core_contract_test.cpp)
target_link_libraries(
    meccha_core_contract_tests
    PRIVATE
        MecchaCamouflage::Core
        meccha_build_options
)
add_test(NAME core_contract COMMAND meccha_core_contract_tests)

add_executable(meccha_esp_frame_tests esp_frame_test.cpp)
target_link_libraries(
    meccha_esp_frame_tests
    PRIVATE
        MecchaCamouflage::Core
        meccha_build_options
)
add_test(NAME esp_frame COMMAND meccha_esp_frame_tests)

add_executable(
    meccha_esp_frame_coordinator_tests
    esp_frame_coordinator_test.cpp
)
target_link_libraries(
    meccha_esp_frame_coordinator_tests
    PRIVATE
        MecchaCamouflage::Application
        meccha_build_options
)
add_test(
    NAME esp_frame_coordinator
    COMMAND meccha_esp_frame_coordinator_tests
)

add_executable(
    meccha_image_compositor_tests
    image_compositor_test.cpp
)
target_link_libraries(
    meccha_image_compositor_tests
    PRIVATE
        MecchaCamouflage::Core
        meccha_build_options
)
add_test(NAME image_compositor COMMAND meccha_image_compositor_tests)

add_executable(
    meccha_image_composition_worker_tests
    image_composition_worker_test.cpp
)
target_link_libraries(
    meccha_image_composition_worker_tests
    PRIVATE
        MecchaCamouflage::Application
        meccha_build_options
)
add_test(
    NAME image_composition_worker
    COMMAND meccha_image_composition_worker_tests
)

add_executable(
    meccha_image_decoder_tests
    image_decoder_test.cpp
)
target_link_libraries(
    meccha_image_decoder_tests
    PRIVATE
        MecchaCamouflage::Application
        meccha_build_options
)
add_test(
    NAME image_decoder
    COMMAND meccha_image_decoder_tests
)

add_executable(
    meccha_image_decode_worker_tests
    image_decode_worker_test.cpp
)
target_link_libraries(
    meccha_image_decode_worker_tests
    PRIVATE
        MecchaCamouflage::Application
        meccha_build_options
)
add_test(
    NAME image_decode_worker
    COMMAND meccha_image_decode_worker_tests
)

add_executable(
    meccha_image_editor_pipeline_tests
    image_editor_pipeline_test.cpp
)
target_link_libraries(
    meccha_image_editor_pipeline_tests
    PRIVATE
        MecchaCamouflage::Application
        meccha_build_options
)
add_test(
    NAME image_editor_pipeline
    COMMAND meccha_image_editor_pipeline_tests
)

add_executable(
    meccha_image_paint_planner_tests
    image_paint_planner_test.cpp
)
target_link_libraries(
    meccha_image_paint_planner_tests
    PRIVATE
        MecchaCamouflage::Core
        meccha_build_options
)
add_test(
    NAME image_paint_planner
    COMMAND meccha_image_paint_planner_tests
)

add_executable(
    meccha_image_profile_mapping_tests
    image_profile_mapping_test.cpp
)
target_link_libraries(
    meccha_image_profile_mapping_tests
    PRIVATE
        MecchaCamouflage::Core
        meccha_build_options
)
add_test(
    NAME image_profile_mapping
    COMMAND meccha_image_profile_mapping_tests
)

add_executable(
    meccha_image_paint_planning_worker_tests
    image_paint_planning_worker_test.cpp
)
target_link_libraries(
    meccha_image_paint_planning_worker_tests
    PRIVATE
        MecchaCamouflage::Application
        meccha_build_options
)
add_test(
    NAME image_paint_planning_worker
    COMMAND meccha_image_paint_planning_worker_tests
)

add_executable(
    meccha_image_paint_job_coordinator_tests
    image_paint_job_coordinator_test.cpp
)
target_link_libraries(
    meccha_image_paint_job_coordinator_tests
    PRIVATE
        MecchaCamouflage::Application
        meccha_build_options
)
add_test(
    NAME image_paint_job_coordinator
    COMMAND meccha_image_paint_job_coordinator_tests
)

add_executable(meccha_paint_planner_tests paint_planner_test.cpp)
target_link_libraries(
    meccha_paint_planner_tests
    PRIVATE
        MecchaCamouflage::Core
        meccha_build_options
)
add_test(NAME paint_planner COMMAND meccha_paint_planner_tests)

add_executable(
    meccha_paint_preview_composer_tests
    paint_preview_composer_test.cpp
)
target_link_libraries(
    meccha_paint_preview_composer_tests
    PRIVATE
        MecchaCamouflage::Core
        meccha_build_options
)
add_test(
    NAME paint_preview_composer
    COMMAND meccha_paint_preview_composer_tests
)

add_executable(
    meccha_paint_preview_build_worker_tests
    paint_preview_build_worker_test.cpp
)
target_link_libraries(
    meccha_paint_preview_build_worker_tests
    PRIVATE
        MecchaCamouflage::Application
        meccha_build_options
)
add_test(
    NAME paint_preview_build_worker
    COMMAND meccha_paint_preview_build_worker_tests
)

add_executable(meccha_paint_dispatch_tests paint_dispatch_test.cpp)
target_link_libraries(
    meccha_paint_dispatch_tests
    PRIVATE
        MecchaCamouflage::Application
        meccha_build_options
)
add_test(NAME paint_dispatch COMMAND meccha_paint_dispatch_tests)

add_executable(
    meccha_paint_planning_worker_tests
    paint_planning_worker_test.cpp
)
target_link_libraries(
    meccha_paint_planning_worker_tests
    PRIVATE
        MecchaCamouflage::Application
        meccha_build_options
)
add_test(
    NAME paint_planning_worker
    COMMAND meccha_paint_planning_worker_tests
)

add_executable(
    meccha_paint_job_coordinator_tests
    paint_job_coordinator_test.cpp
)
target_link_libraries(
    meccha_paint_job_coordinator_tests
    PRIVATE
        MecchaCamouflage::Application
        meccha_build_options
)
add_test(
    NAME paint_job_coordinator
    COMMAND meccha_paint_job_coordinator_tests
)

add_executable(
    meccha_paint_preview_controller_tests
    paint_preview_controller_test.cpp
)
target_link_libraries(
    meccha_paint_preview_controller_tests
    PRIVATE
        MecchaCamouflage::Application
        meccha_build_options
)
add_test(
    NAME paint_preview_controller
    COMMAND meccha_paint_preview_controller_tests
)

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

add_executable(
    meccha_preparation_plan_tests
    preparation_plan_test.cpp
)
target_link_libraries(
    meccha_preparation_plan_tests
    PRIVATE
        MecchaCamouflage::LauncherCore
        meccha_build_options
)
add_test(NAME preparation_plan COMMAND meccha_preparation_plan_tests)

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

add_executable(
    meccha_shared_mod_ledger_tests
    shared_mod_ledger_test.cpp
)
target_include_directories(
    meccha_shared_mod_ledger_tests
    PRIVATE
        "${PROJECT_SOURCE_DIR}/src/launcher/src"
)
target_link_libraries(
    meccha_shared_mod_ledger_tests
    PRIVATE
        MecchaCamouflage::LauncherCore
        meccha_build_options
)
add_test(
    NAME shared_mod_ledger
    COMMAND meccha_shared_mod_ledger_tests
)

add_executable(
    meccha_launcher_execution_tests
    launcher_execution_test.cpp
)
target_link_libraries(
    meccha_launcher_execution_tests
    PRIVATE
        MecchaCamouflage::LauncherCore
        meccha_build_options
)
add_test(
    NAME launcher_execution
    COMMAND meccha_launcher_execution_tests
)

add_executable(
    meccha_launcher_command_line_tests
    launcher_command_line_test.cpp
)
target_link_libraries(
    meccha_launcher_command_line_tests
    PRIVATE
        MecchaCamouflage::LauncherCore
        meccha_build_options
)
add_test(
    NAME launcher_command_line
    COMMAND meccha_launcher_command_line_tests
)

if(WIN32)
    add_executable(
        meccha_single_instance_tests
        single_instance_win32_test.cpp
    )
    target_link_libraries(
        meccha_single_instance_tests
        PRIVATE
            MecchaCamouflage::LauncherCore
            meccha_build_options
    )
    add_test(
        NAME single_instance
        COMMAND meccha_single_instance_tests
    )

    add_executable(
        meccha_launcher_execution_win32_tests
        launcher_execution_win32_test.cpp
    )
    target_link_libraries(
        meccha_launcher_execution_win32_tests
        PRIVATE
            MecchaCamouflage::LauncherCore
            meccha_build_options
    )
    add_test(
        NAME launcher_execution_win32
        COMMAND meccha_launcher_execution_win32_tests
    )

    add_executable(
        meccha_managed_loader_tests
        managed_loader_win32_test.cpp
    )
    target_link_libraries(
        meccha_managed_loader_tests
        PRIVATE
            MecchaCamouflage::LauncherCore
            meccha_build_options
    )
    add_test(NAME managed_loader COMMAND meccha_managed_loader_tests)

    add_executable(
        meccha_owned_file_storage_tests
        owned_file_storage_win32_test.cpp
    )
    target_link_libraries(
        meccha_owned_file_storage_tests
        PRIVATE
            MecchaCamouflage::LauncherCore
            meccha_build_options
    )
    add_test(
        NAME owned_file_storage
        COMMAND meccha_owned_file_storage_tests
    )

    add_executable(
        meccha_shared_mod_tests
        shared_mod_win32_test.cpp
    )
    target_link_libraries(
        meccha_shared_mod_tests
        PRIVATE
            MecchaCamouflage::LauncherCore
            meccha_build_options
    )
    add_test(NAME shared_mod COMMAND meccha_shared_mod_tests)

    add_executable(
        meccha_config_storage_tests
        config_storage_win32_test.cpp
    )
    target_link_libraries(
        meccha_config_storage_tests
        PRIVATE
            MecchaCamouflage::Application
            meccha_build_options
    )
    add_test(NAME config_storage COMMAND meccha_config_storage_tests)

    add_executable(
        meccha_image_project_storage_tests
        image_project_storage_win32_test.cpp
    )
    target_link_libraries(
        meccha_image_project_storage_tests
        PRIVATE
            MecchaCamouflage::Application
            meccha_build_options
    )
    add_test(
        NAME image_project_storage
        COMMAND meccha_image_project_storage_tests
    )

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
