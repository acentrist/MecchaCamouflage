include_guard(GLOBAL)

include(FetchContent)

# UE4SS commit 6c26f038 pins two gitlinks, but several direct FetchContent
# dependencies by tag and IconFontCppHeaders by a moving branch. Declare every
# direct fetch first so CMake's first-declaration-wins rule produces one
# immutable source graph without patching UE4SS.
set(FETCHCONTENT_UPDATES_DISCONNECTED ON)
FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG e2c92645460f680fd272fd2eed591efb2be7dc31
)
FetchContent_Declare(
    ImGui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG 5d4126876bc10396d4c6511853ff10964414c776
)
FetchContent_Declare(
    ImGuiTextEdit
    GIT_REPOSITORY https://github.com/UE4SS-RE/ImGuiColorTextEdit.git
    GIT_TAG 6d943aba9f7cef05da80b86dbb0253b63818f95c
)
FetchContent_Declare(
    IconFontCppHeaders
    GIT_REPOSITORY https://github.com/juliettef/IconFontCppHeaders.git
    GIT_TAG 210b5a399a64270674560d633638952d1e8d804d
)
FetchContent_Declare(
    zydis
    GIT_REPOSITORY https://github.com/zyantific/zydis.git
    GIT_TAG a2278f1d254e492f6a6b39f6cb5d1f5d515659dc
)
FetchContent_Declare(
    PolyHook2
    GIT_REPOSITORY https://github.com/stevemk14ebr/PolyHook_2_0.git
    GIT_TAG 298d56210b9d9e66cde8f96481d6053925c6ae15
)
FetchContent_Declare(
    raw_pdb
    GIT_REPOSITORY https://github.com/MolecularMatters/raw_pdb.git
    GIT_TAG 8c6a7146393c83d27fa101e8bc8017f2a7f151df
)
FetchContent_Declare(
    Corrosion
    GIT_REPOSITORY https://github.com/UE4SS-RE/corrosion.git
    GIT_TAG 52844733e14f095c947577627e367ee5f6458af7
)
FetchContent_Declare(
    fmt
    GIT_REPOSITORY https://github.com/fmtlib/fmt.git
    GIT_TAG 40626af88bd7df9a5fb80be7b25ac85b122d6c21
    PATCH_COMMAND
        "${CMAKE_COMMAND}"
        "-DPATCH_SCRIPT_DIR=${CMAKE_CURRENT_SOURCE_DIR}/third_party/RE-UE4SS/deps/third/fmt"
        -P
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/RE-UE4SS/deps/third/fmt/patch_fmt.cmake"
)
FetchContent_Declare(
    tracy
    GIT_REPOSITORY https://github.com/wolfpld/tracy.git
    GIT_TAG 37aff70dfa50cf6307b3fee6074d627dc2929143
)
