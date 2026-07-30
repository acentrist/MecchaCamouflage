#include <meccha/launcher/composition_win32.hpp>
#include <meccha/launcher/hash.hpp>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace
{
using namespace meccha::launcher;
namespace fs = std::filesystem;

auto expect(bool condition, std::string_view message) -> bool
{
    if (!condition)
    {
        std::cerr << "FAIL launcher_composition_win32: "
                  << message << '\n';
    }
    return condition;
}

auto bytes(std::string_view text) -> std::vector<std::byte>
{
    const auto view = std::as_bytes(std::span{text});
    return {view.begin(), view.end()};
}

class TemporaryTree
{
public:
    TemporaryTree()
    {
        static std::uint64_t sequence{};
        ++sequence;
        root = fs::temp_directory_path() /
               ("meccha-v2-composition-win32-" +
                std::to_string(GetCurrentProcessId()) + "-" +
                std::to_string(GetTickCount64()) + "-" +
                std::to_string(sequence));
        fs::create_directories(root / "game");
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

class MemoryPayloadSource final : public RuntimePayloadSource
{
public:
    auto read_file(std::string_view relative_path)
        -> std::expected<
            std::vector<std::byte>,
            RuntimePayloadError> override
    {
        ++read_count;
        const auto found = files.find(std::string{relative_path});
        if (found == files.end())
        {
            return std::unexpected(RuntimePayloadError{
                "missing payload fixture",
            });
        }
        return found->second;
    }

    std::map<
        std::string,
        std::vector<std::byte>,
        std::less<>> files{};
    std::size_t read_count{};
};

struct Package
{
    std::string manifest_json{};
    PayloadManifest manifest{};
    Sha256Digest manifest_sha256{};
    MemoryPayloadSource payload{};
};

auto make_package() -> Package
{
    auto package = Package{};
    package.payload.files["dwmapi.dll"] = bytes("proxy");
    package.payload.files["UE4SS.dll"] = bytes("runtime");
    package.payload.files[
        "Mods/MecchaCamouflage/dlls/main.dll"] = bytes("main");
    package.payload.files[
        "Mods/MecchaCamouflage/enabled.txt"] = {};

    const auto entry = [&package](
                           std::string_view path,
                           std::string_view role) {
        const auto& value =
            package.payload.files.at(std::string{path});
        return std::string{R"json({"path":")json"} +
               std::string{path} +
               R"json(","role":")json" +
               std::string{role} +
               R"json(","size":)json" +
               std::to_string(value.size()) +
               R"json(,"sha256":")json" +
               sha256_hex(sha256_bytes(value).value()) +
               R"json("})json";
    };
    package.manifest_json =
        std::string{
            R"json({"schema_version":1,"product_version":"2.0.0",)json"} +
        R"json("ue4ss_commit":"6c26f038751b3d96059d4a9148f5d093012d55ad",)json" +
        R"json("generated_paths":["Logs"],"files":[)json" +
        entry("dwmapi.dll", "proxy") + "," +
        entry("UE4SS.dll", "runtime") + "," +
        entry(
            "Mods/MecchaCamouflage/dlls/main.dll",
            "mod") +
        "," +
        entry(
            "Mods/MecchaCamouflage/enabled.txt",
            "mod") +
        "]}";
    const auto manifest =
        parse_payload_manifest(package.manifest_json);
    if (!manifest)
    {
        std::cerr << "invalid composition fixture: "
                  << manifest.error().detail << '\n';
        return package;
    }
    package.manifest = *manifest;
    package.manifest_sha256 = sha256_bytes(
        std::as_bytes(std::span{package.manifest_json}))
                                  .value();
    return package;
}

class FakeOriginalUserPlatform final
    : public OriginalUserObservationPlatform
{
public:
    auto game_running()
        -> std::expected<
            bool,
            LauncherObservationError> override
    {
        ++game_checks;
        return running;
    }

    auto steam_launch_options()
        -> std::expected<
            std::optional<std::string>,
            LauncherObservationError> override
    {
        ++launch_option_reads;
        return std::optional<std::string>{};
    }

    auto directory_writable(const fs::path&)
        -> std::expected<
            bool,
            LauncherObservationError> override
    {
        return true;
    }

    bool running{};
    std::size_t game_checks{};
    std::size_t launch_option_reads{};
};

class UnexpectedBroker final : public ElevatedLoaderBroker
{
public:
    auto apply(
        const ManagedLoaderPlan&,
        const fs::path&,
        const fs::path&,
        const ManagedLoaderMaterial&)
        -> std::expected<void, LauncherEffectError> override
    {
        called = true;
        return std::unexpected(LauncherEffectError{
            "writable managed execution reached UAC",
        });
    }

    auto remove(
        const RemovalPlan&,
        const fs::path&,
        const fs::path&)
        -> std::expected<void, LauncherEffectError> override
    {
        called = true;
        return std::unexpected(LauncherEffectError{
            "writable managed execution reached UAC",
        });
    }

    bool called{};
};

class UnexpectedSteamLauncher final : public SteamGameLauncher
{
public:
    auto launch()
        -> std::expected<void, LauncherEffectError> override
    {
        called = true;
        return std::unexpected(LauncherEffectError{
            "prepare-only reached Steam",
        });
    }

    bool called{};
};

auto inputs(
    LauncherInvocationMode mode,
    const Package& package,
    MemoryPayloadSource& payload,
    const TemporaryTree& tree,
    OriginalUserObservationPlatform& platform,
    ElevatedLoaderBroker& broker,
    SteamGameLauncher& steam)
    -> Win32LauncherCompositionInputs
{
    return Win32LauncherCompositionInputs{
        mode,
        package.manifest,
        package.manifest_json,
        package.manifest_sha256,
        payload,
        tree.root / "game",
        tree.root / "data" / "runtime",
        tree.root / "data" / "ownership",
        "0123456789abcdef0123456789abcdef",
        platform,
        broker,
        steam,
    };
}
} // namespace

auto main() -> int
{
    using namespace meccha::launcher;

    auto package = make_package();
    TemporaryTree managed_tree{};
    FakeOriginalUserPlatform platform{};
    UnexpectedBroker broker{};
    UnexpectedSteamLauncher steam{};
    const auto first = run_win32_launcher_composition(inputs(
        LauncherInvocationMode::PrepareOnly,
        package,
        package.payload,
        managed_tree,
        platform,
        broker,
        steam));
    auto passed = expect(
        first &&
            first->recovery == RuntimeRecoveryResult::Clean &&
            std::holds_alternative<PreparationExecutionResult>(
                first->workflow) &&
            fs::exists(
                managed_tree.root / "data" / "runtime" /
                "active" / "UE4SS.dll") &&
            fs::exists(
                managed_tree.root / "data" / "runtime" /
                "active" / "Mods" / "MecchaCamouflage" /
                "dlls" / "main.dll") &&
            fs::exists(
                managed_tree.root / "game" / "dwmapi.dll") &&
            fs::exists(
                managed_tree.root / "game" / "override.txt") &&
            !broker.called && !steam.called,
        "clean managed composition did not prepare the complete runtime");

    const auto second = run_win32_launcher_composition(inputs(
        LauncherInvocationMode::PrepareOnly,
        package,
        package.payload,
        managed_tree,
        platform,
        broker,
        steam));
    std::size_t runtime_entries{};
    for (const auto& ignored : fs::directory_iterator{
             managed_tree.root / "data" / "runtime"})
    {
        static_cast<void>(ignored);
        ++runtime_entries;
    }
    passed &= expect(
        second &&
            second->recovery == RuntimeRecoveryResult::Clean &&
            runtime_entries == 1U &&
            platform.game_checks == 4U &&
            platform.launch_option_reads == 2U,
        "unchanged composition was not idempotent");

    const auto removed = run_win32_launcher_composition(inputs(
        LauncherInvocationMode::Remove,
        package,
        package.payload,
        managed_tree,
        platform,
        broker,
        steam));
    passed &= expect(
        removed &&
            std::holds_alternative<RemovalExecutionResult>(
                removed->workflow) &&
            !fs::exists(
                managed_tree.root / "data" / "runtime" /
                "active") &&
            !fs::exists(
                managed_tree.root / "game" / "dwmapi.dll") &&
            !fs::exists(
                managed_tree.root / "game" / "override.txt") &&
            !broker.called && !steam.called,
        "managed composition did not remove only its owned deployment");

    TemporaryTree running_tree{};
    FakeOriginalUserPlatform running_platform{};
    running_platform.running = true;
    MemoryPayloadSource running_payload{};
    running_payload.files = package.payload.files;
    const auto blocked = run_win32_launcher_composition(inputs(
        LauncherInvocationMode::PrepareOnly,
        package,
        running_payload,
        running_tree,
        running_platform,
        broker,
        steam));
    passed &= expect(
        !blocked &&
            std::holds_alternative<LauncherWorkflowError>(
                blocked.error()) &&
            !fs::exists(running_tree.root / "data") &&
            running_payload.read_count == 0U &&
            running_platform.game_checks == 1U &&
            running_platform.launch_option_reads == 0U,
        "running-game preflight reached recovery, payload, or observation");

    auto invalid_package = make_package();
    invalid_package.manifest_json.push_back(' ');
    TemporaryTree invalid_tree{};
    FakeOriginalUserPlatform invalid_platform{};
    const auto invalid = run_win32_launcher_composition(inputs(
        LauncherInvocationMode::PrepareOnly,
        invalid_package,
        invalid_package.payload,
        invalid_tree,
        invalid_platform,
        broker,
        steam));
    passed &= expect(
        !invalid &&
            std::holds_alternative<LauncherObservationError>(
                invalid.error()) &&
            !fs::exists(invalid_tree.root / "data") &&
            invalid_package.payload.read_count == 0U &&
            invalid_platform.game_checks == 1U &&
            invalid_platform.launch_option_reads == 0U,
        "invalid manifest identity reached recovery, payload, or observation");

    if (passed)
    {
        std::cout << "PASS launcher_composition_win32\n";
        return 0;
    }
    return 1;
}
