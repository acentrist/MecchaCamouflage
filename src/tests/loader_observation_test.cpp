#include <meccha/launcher/game_discovery.hpp>
#include <meccha/launcher/hash.hpp>
#include <meccha/launcher/loader_observation.hpp>

#include <array>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace
{
using namespace meccha::launcher;
namespace fs = std::filesystem;

auto expect(bool condition, std::string_view message) -> bool
{
    if (!condition)
    {
        std::cerr << "FAIL loader_observation: " << message << '\n';
    }
    return condition;
}

auto views(const std::vector<std::string>& values)
    -> std::vector<std::string_view>
{
    auto result = std::vector<std::string_view>{};
    result.reserve(values.size());
    for (const auto& value : values)
    {
        result.push_back(value);
    }
    return result;
}

class TemporaryTree
{
public:
    TemporaryTree()
    {
        const auto nonce = std::chrono::steady_clock::now()
                               .time_since_epoch()
                               .count();
        root = fs::temp_directory_path() /
               ("meccha-v2-loader-" + std::to_string(nonce));
        fs::create_directories(root);
    }

    TemporaryTree(const TemporaryTree&) = delete;
    auto operator=(const TemporaryTree&) -> TemporaryTree& = delete;

    ~TemporaryTree()
    {
        std::error_code ignored{};
        fs::remove_all(root, ignored);
    }

    fs::path root{};
};

#ifdef _WIN32
auto write_text(const fs::path& path, std::string_view value) -> void
{
    fs::create_directories(path.parent_path());
    std::ofstream output{path, std::ios::binary | std::ios::trunc};
    output.write(value.data(), static_cast<std::streamsize>(value.size()));
}
#endif
} // namespace

auto main() -> int
{
    using namespace meccha::launcher;

    bool passed = true;

    const auto steam_options = parse_steam_launch_options(
        R"vdf("UserLocalConfigStore"
{
    "Software" { "Valve" { "Steam" { "apps" {
        "4704690" { "LaunchOptions" "--ue4ss-path runtime\\UE4SS.dll" }
    } } } }
}
)vdf");
    passed &= expect(
        steam_options &&
            *steam_options ==
                std::optional<std::string>{
                    R"(--ue4ss-path runtime\UE4SS.dll)"},
        "Steam LaunchOptions were not extracted");

    const std::vector path_arguments{
        std::string{"-windowed"},
        std::string{"--ue4ss-path"},
        std::string{R"(runtime\UE4SS.dll)"},
    };
    const auto path_views = views(path_arguments);
    const auto path_argument = analyze_ue4ss_arguments(path_views);
    passed &= expect(
        path_argument && *path_argument &&
            (*path_argument)->path == R"(runtime\UE4SS.dll)",
        "the UE4SS launch path was not recognized");

    const std::vector<std::string> unrelated_arguments{
        "-windowed",
        "-ResX=1280",
    };
    const auto unrelated_views = views(unrelated_arguments);
    const auto unrelated = analyze_ue4ss_arguments(unrelated_views);
    passed &= expect(
        unrelated && !*unrelated,
        "unrelated launch options were treated as a UE4SS override");

    const std::vector<std::string> missing_value{"--ue4ss-path"};
    const auto missing_views = views(missing_value);
    const auto missing = analyze_ue4ss_arguments(missing_views);
    passed &= expect(
        !missing &&
            missing.error().code ==
                LoaderObservationErrorCode::Arguments,
        "a missing --ue4ss-path value was accepted");

    const std::vector<std::string> duplicate_values{
        "--ue4ss-path",
        "one.dll",
        "--ue4ss-path",
        "two.dll",
    };
    const auto duplicate_views = views(duplicate_values);
    passed &= expect(
        !analyze_ue4ss_arguments(duplicate_views),
        "duplicate --ue4ss-path values were accepted");

    const std::vector<std::string> disabled{"--disable-ue4ss"};
    const auto disabled_views = views(disabled);
    passed &= expect(
        !analyze_ue4ss_arguments(disabled_views),
        "--disable-ue4ss did not block preparation");

    TemporaryTree tree{};
    const auto game = tree.root / "game";
    const auto runtime = tree.root / "runtime";
    fs::create_directories(game);
    fs::create_directories(runtime);
    const auto relative_override =
        parse_override_target("../runtime\r\n", game);
    passed &= expect(
        relative_override &&
            *relative_override ==
                (game / ".." / "runtime" / "UE4SS.dll")
                    .lexically_normal(),
        "relative override.txt was not resolved like the pinned proxy");
    passed &= expect(
        !parse_override_target("../runtime\nignored", game),
        "override.txt trailing data was accepted");

#ifdef _WIN32
    write_text(runtime / "UE4SS.dll", "pinned-runtime");
    const auto runtime_hash = sha256_file(runtime / "UE4SS.dll");
    passed &= expect(
        runtime_hash.has_value(),
        "the pinned runtime fixture could not be hashed");
    if (runtime_hash)
    {
        write_text(game / "override.txt", "../runtime\r\n");
        const LoaderFilesystemRequest request{
            game,
            DirectiveState::Absent,
            std::nullopt,
            DirectiveState::Unowned,
            runtime_hash->sha256,
        };
        const auto observation = observe_loader_filesystem(request);
        const auto detailed =
            observe_loader_filesystem_details(request);
        passed &= expect(
            observation &&
                observation->override_target ==
                    CandidateIdentity::Pinned &&
                observation->conventional_subdirectory ==
                    CandidateIdentity::Missing &&
                observation->conventional_root ==
                    CandidateIdentity::Missing,
            "the exact override target was not observed");
        passed &= expect(
            detailed &&
                detailed->chain == *observation &&
                !detailed->command_line_target &&
                detailed->override_target ==
                    std::optional<fs::path>{
                        (game / ".." / "runtime" / "UE4SS.dll")
                            .lexically_normal()},
            "the loader observation did not retain its resolved target");

        write_text(game / "ue4ss" / "UE4SS.dll", "other-runtime");
        const auto conflicting = observe_loader_filesystem(request);
        passed &= expect(
            conflicting &&
                resolve_loader_chain(*conflicting).state ==
                    LoaderResolutionState::Conflict,
            "an incompatible conventional fallback was ignored");

        const auto command = analyze_windows_launch_options(
            R"(--ue4ss-path "..\runtime\UE4SS.dll")",
            game);
        passed &= expect(
            command &&
                *command ==
                    std::optional<fs::path>{
                        (game / ".." / "runtime" / "UE4SS.dll")
                            .lexically_normal()},
            "Windows launch options were not tokenized and resolved");
    }
#endif

    if (passed)
    {
        std::cout << "PASS loader_observation\n";
        return 0;
    }
    return 1;
}
