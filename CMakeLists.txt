cmake_minimum_required(VERSION 3.28)

project(
    MecchaCamouflage
    VERSION 2.0.0
    DESCRIPTION "In-game Paint, Image Paint, and ESP for MECCHA CHAMELEON"
    LANGUAGES CXX
)

# The protected dependency-evidence gate consumes the resolved target closure
# from the official CMake File API. Requesting it here keeps all configure
# presets on the same codemodel-v2 contract.
cmake_file_api(QUERY API_VERSION 1 CODEMODEL 2)

if(WIN32)
    enable_language(RC)
endif()

option(MECCHA_WITH_UE4SS "Build the UE4SS runtime and MecchaCamouflage mod" OFF)
option(MECCHA_BUILD_TESTS "Build the secret-free v2 test suite" ON)
option(MECCHA_WARNINGS_AS_ERRORS "Treat project warnings as errors" ON)
set(
    MECCHA_PAYLOAD_MANIFEST
    ""
    CACHE FILEPATH
    "Canonical payload manifest to embed in the Windows launcher"
)
set(
    MECCHA_PAYLOAD_CAB
    ""
    CACHE FILEPATH
    "Verified payload CAB to embed in the Windows launcher"
)
option(
    MECCHA_ENABLE_MSVC_ADDRESS_SANITIZER
    "Enable the Windows MSVC AddressSanitizer gate"
    OFF
)
option(
    MECCHA_ENABLE_CLANG_CL_UBSAN
    "Enable the Windows clang-cl UndefinedBehaviorSanitizer gate"
    OFF
)
option(
    MECCHA_ENABLE_MSVC_CODE_ANALYSIS
    "Enable the Windows MSVC native code-analysis gate"
    OFF
)

set(MECCHA_UE4SS_COMMIT
    "6c26f038751b3d96059d4a9148f5d093012d55ad"
    CACHE STRING
    "UE4SS source commit accepted by the v2 architecture gate"
)
set(
    MECCHA_UE4SS_SOURCE_ROOT
    ""
    CACHE PATH
    "Verified immutable build-only UE4SS source stage"
)
set(
    MECCHA_UE4SS_SOURCE_MANIFEST
    ""
    CACHE FILEPATH
    "Manifest for the verified build-only UE4SS source stage"
)
set(
    MECCHA_UE4SS_CANONICAL_LOCK_SHA256
    "88c3718c03492cdc2650217a9d8bb2a8dbdecdbde1b4ea79e3e529e838b49bbe"
)
set(MECCHA_CONFIG_SCHEMA_VERSION 1 CACHE STRING "v2 configuration schema")
set(MECCHA_PRESET_SCHEMA_VERSION 1 CACHE STRING "v2 preset schema")
set(MECCHA_PAYLOAD_MANIFEST_SCHEMA_VERSION 1 CACHE STRING "v2 payload manifest schema")
set(MECCHA_RUNTIME_OWNERSHIP_SCHEMA_VERSION 1 CACHE STRING "v2 runtime ownership schema")
set(MECCHA_RUNTIME_TRANSACTION_SCHEMA_VERSION 1 CACHE STRING "v2 runtime transaction schema")
set(MECCHA_SHARED_MOD_LEDGER_SCHEMA_VERSION 1 CACHE STRING "v2 shared mod ledger schema")

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(MECCHA_ENABLE_CLANG_CL_UBSAN)
    # LLVM's supported Windows UBSan runtime is a static /MT library.
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
else()
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

include(cmake/ProjectDependencies.cmake)

add_library(meccha_build_options INTERFACE)
target_compile_features(meccha_build_options INTERFACE cxx_std_23)

if(MSVC)
    target_compile_options(
        meccha_build_options
        INTERFACE
            /permissive-
            /W4
            /utf-8
            /Zc:__cplusplus
            $<$<BOOL:${MECCHA_WARNINGS_AS_ERRORS}>:/WX>
    )
else()
    target_compile_options(
        meccha_build_options
        INTERFACE
            -Wall
            -Wextra
            -Wpedantic
            $<$<BOOL:${MECCHA_WARNINGS_AS_ERRORS}>:-Werror>
    )
endif()

set(_meccha_deep_validation_modes)
foreach(
    _meccha_mode
    IN ITEMS
        MECCHA_ENABLE_MSVC_ADDRESS_SANITIZER
        MECCHA_ENABLE_CLANG_CL_UBSAN
        MECCHA_ENABLE_MSVC_CODE_ANALYSIS
)
    if(${_meccha_mode})
        list(APPEND _meccha_deep_validation_modes "${_meccha_mode}")
    endif()
endforeach()
list(LENGTH _meccha_deep_validation_modes _meccha_deep_validation_mode_count)
if(_meccha_deep_validation_mode_count GREATER 1)
    message(
        FATAL_ERROR
        "Use a separate build tree for each Windows deep-validation mode."
    )
endif()

if(MECCHA_ENABLE_MSVC_ADDRESS_SANITIZER)
    if(NOT WIN32 OR NOT CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
        message(
            FATAL_ERROR
            "MECCHA_ENABLE_MSVC_ADDRESS_SANITIZER requires Windows MSVC."
        )
    endif()
    target_compile_options(
        meccha_build_options
        INTERFACE
            /fsanitize=address
            /Zi
    )
    target_link_options(
        meccha_build_options
        INTERFACE
            /DEBUG
            /INCREMENTAL:NO
    )
endif()

if(MECCHA_ENABLE_CLANG_CL_UBSAN)
    if(NOT WIN32 OR
       NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang" OR
       NOT CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
        message(
            FATAL_ERROR
            "MECCHA_ENABLE_CLANG_CL_UBSAN requires clang-cl on Windows."
        )
    endif()
    target_compile_options(
        meccha_build_options
        INTERFACE
            -fsanitize=undefined
            -fno-sanitize-recover=undefined
            "-fsanitize-ignorelist=${CMAKE_CURRENT_SOURCE_DIR}/cmake/clang-cl-ubsan-ignorelist.txt"
            /Oy-
            /Zi
    )
    target_link_options(
        meccha_build_options
        INTERFACE
            /DEBUG
            /INCREMENTAL:NO
    )
endif()

if(MECCHA_ENABLE_MSVC_CODE_ANALYSIS)
    if(NOT WIN32 OR NOT CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
        message(
            FATAL_ERROR
            "MECCHA_ENABLE_MSVC_CODE_ANALYSIS requires Windows MSVC."
        )
    endif()
    target_compile_options(
        meccha_build_options
        INTERFACE
            /analyze
            /analyze:external-
    )
endif()

configure_file(
    cmake/build_identity.hpp.in
    "${CMAKE_CURRENT_BINARY_DIR}/generated/meccha/build_identity.hpp"
    @ONLY
)

add_library(meccha_build_identity INTERFACE)
target_include_directories(
    meccha_build_identity
    INTERFACE
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/generated>"
)
target_link_libraries(meccha_build_identity INTERFACE meccha_build_options)

add_subdirectory(src/common)
add_subdirectory(src/core)
add_subdirectory(src/mod/application)
add_subdirectory(src/mod/runtime/contracts)
add_subdirectory(src/mod/ui)
add_subdirectory(src/launcher)

if(MECCHA_WITH_UE4SS)
    if(NOT WIN32 OR NOT MSVC)
        message(FATAL_ERROR "MECCHA_WITH_UE4SS requires Windows x64 with MSVC.")
    endif()
    if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
        message(FATAL_ERROR "MECCHA_WITH_UE4SS supports x64 only.")
    endif()

    if(MECCHA_UE4SS_SOURCE_ROOT STREQUAL "" OR
       MECCHA_UE4SS_SOURCE_MANIFEST STREQUAL "")
        message(
            FATAL_ERROR
            "The full build requires the verified immutable UE4SS source "
            "stage and manifest. Run prepare_ue4ss_source_stage.py first."
        )
    endif()
    file(
        REAL_PATH
        "${MECCHA_UE4SS_SOURCE_ROOT}"
        _meccha_ue4ss_root
        BASE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    )
    file(
        REAL_PATH
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/RE-UE4SS"
        _meccha_upstream_ue4ss_root
    )
    if(_meccha_ue4ss_root STREQUAL _meccha_upstream_ue4ss_root)
        message(
            FATAL_ERROR
            "The accepted UE4SS gitlink cannot be used as the mutable "
            "full-build source directory."
        )
    endif()
    if(NOT EXISTS "${_meccha_ue4ss_root}/CMakeLists.txt")
        message(
            FATAL_ERROR
            "The verified UE4SS source stage is missing."
        )
    endif()
    if(NOT EXISTS "${MECCHA_UE4SS_SOURCE_MANIFEST}")
        message(FATAL_ERROR "The UE4SS source-stage manifest is missing.")
    endif()
    set(_meccha_ue4ss_lock
        "${_meccha_ue4ss_root}/deps/first/patternsleuth_bind/Cargo.lock"
    )
    if(NOT EXISTS "${_meccha_ue4ss_lock}")
        message(FATAL_ERROR "The canonical staged Cargo.lock is missing.")
    endif()
    file(SHA256 "${_meccha_ue4ss_lock}" _meccha_ue4ss_lock_sha256)
    if(NOT _meccha_ue4ss_lock_sha256
       STREQUAL MECCHA_UE4SS_CANONICAL_LOCK_SHA256)
        message(
            FATAL_ERROR
            "The staged Cargo.lock does not match the approved canonical "
            "overlay."
        )
    endif()
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    execute_process(
        COMMAND
            "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/v2/prepare_ue4ss_source_stage.py"
            --verify-only
            --policy
            "${CMAKE_CURRENT_SOURCE_DIR}/cmake/ue4ss-source-overlay.json"
            --output-root
            "${_meccha_ue4ss_root}"
            --manifest
            "${MECCHA_UE4SS_SOURCE_MANIFEST}"
        RESULT_VARIABLE _meccha_ue4ss_stage_result
        OUTPUT_VARIABLE _meccha_ue4ss_stage_output
        ERROR_VARIABLE _meccha_ue4ss_stage_error
    )
    if(NOT _meccha_ue4ss_stage_result EQUAL 0)
        message(
            FATAL_ERROR
            "The UE4SS source-stage verifier rejected the build input:\n"
            "${_meccha_ue4ss_stage_output}"
            "${_meccha_ue4ss_stage_error}"
        )
    endif()
    if(NOT EXISTS "${_meccha_ue4ss_root}/deps/first/Unreal/CMakeLists.txt")
        message(
            FATAL_ERROR
            "The restricted UEPseudo dependency is missing. A complete build "
            "requires maintainer-approved Epic-linked GitHub access."
        )
    endif()
    if(NOT EXISTS "${_meccha_ue4ss_root}/deps/first/patternsleuth/Cargo.toml" OR
       NOT EXISTS "${_meccha_ue4ss_root}/deps/first/patternsleuth_bind/Cargo.toml")
        message(
            FATAL_ERROR
            "The pinned patternsleuth source or binding crate is missing. "
            "Initialize the RE-UE4SS submodules recursively in the trusted "
            "build."
        )
    endif()

    set(UE4SS_PROJECTS "UE4SS" CACHE STRING "Minimal v2 UE4SS target set" FORCE)
    set(UE4SS_PROFILERS OFF CACHE BOOL "Disable UE4SS profilers" FORCE)
    set(RC_PROFILER_FLAVOR "None" CACHE STRING "Disable UE4SS profiler integration" FORCE)
    set(MAKE_DEPENDENCIES_SHARED OFF CACHE BOOL "Keep one matched source graph" FORCE)
    set(UE4SS_CONSOLE_COLORS_ENABLED OFF CACHE BOOL "Disable console-only output" FORCE)
    set(UE4SS_INPUT_ENABLED ON CACHE BOOL "Required for configurable product hotkeys" FORCE)

    include(cmake/Ue4ssDependencyPins.cmake)
    add_subdirectory(
        "${_meccha_ue4ss_root}"
        "${CMAKE_CURRENT_BINARY_DIR}/_ue4ss"
        EXCLUDE_FROM_ALL
    )
    # UE4SS publishes its own /W3 policy and headers to consumers. Keep that
    # policy for the pinned dependency itself, but let MecchaCamouflage compile
    # its production composition root under the project-owned /W4 /WX policy.
    # The mod target marks the dependency's public headers as external without
    # changing their include-search order.
    if(MSVC)
        get_target_property(
            _meccha_ue4ss_consumer_options
            UE4SS
            INTERFACE_COMPILE_OPTIONS
        )
        if(_meccha_ue4ss_consumer_options)
            string(
                REPLACE "/W3" ""
                _meccha_ue4ss_consumer_options
                "${_meccha_ue4ss_consumer_options}"
            )
            set_property(
                TARGET UE4SS
                PROPERTY INTERFACE_COMPILE_OPTIONS
                    "${_meccha_ue4ss_consumer_options}"
            )
        endif()
    endif()
    if(NOT COMMAND corrosion_set_cargo_flags OR
       NOT TARGET patternsleuth_bind)
        message(
            FATAL_ERROR
            "The pinned Corrosion patternsleuth target is unavailable."
        )
    endif()
    corrosion_set_cargo_flags(patternsleuth_bind --locked)
    add_subdirectory(src/mod)
    if(NOT TARGET proxy)
        message(FATAL_ERROR "The pinned UE4SS proxy target is unavailable.")
    endif()
    add_dependencies(meccha_mod proxy)
endif()

if(MECCHA_BUILD_TESTS)
    include(CTest)
    enable_testing()
    add_subdirectory(src/tests)
endif()
