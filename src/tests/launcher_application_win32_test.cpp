#include <meccha/launcher/application_win32.hpp>
#include <meccha/launcher/hash.hpp>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
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
        std::cerr << "FAIL launcher_application_win32: "
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
               ("meccha-v2-application-win32-" +
                std::to_string(GetCurrentProcessId()) + "-" +
                std::to_string(GetTickCount64()) + "-" +
                std::to_string(sequence));
        fs::create_directories(root / "game");
        fs::create_directories(root / "local");
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

struct PackageFixture
{
    std::string manifest_json{};
    PayloadManifest manifest{};
    Sha256Digest manifest_sha256{};
    std::map<
        std::string,
        std::vector<std::byte>,
        std::less<>> files{};
};

auto make_package() -> PackageFixture
{
    auto package = PackageFixture{};
    package.files["dwmapi.dll"] = bytes("proxy");
    package.files["UE4SS.dll"] = bytes("runtime");
    package.files[
        "Mods/MecchaCamouflage/dlls/main.dll"] = bytes("main");
    package.files[
        "Mods/MecchaCamouflage/enabled.txt"] = {};

    const auto entry = [&package](
                           std::string_view path,
                           std::string_view role) {
        const auto& value = package.files.at(std::string{path});
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
    package.manifest =
        parse_payload_manifest(package.manifest_json).value();
    package.manifest_sha256 = sha256_bytes(
        std::as_bytes(std::span{package.manifest_json}))
                                  .value();
    return package;
}

class MemoryPayloadSource final : public RuntimePayloadSource
{
public:
    explicit MemoryPayloadSource(const PackageFixture& package)
        : package_{package}
    {
    }

    auto read_file(std::string_view relative_path)
        -> std::expected<
            std::vector<std::byte>,
            RuntimePayloadError> override
    {
        const auto found =
            package_.files.find(std::string{relative_path});
        if (found == package_.files.end())
        {
            return std::unexpected(RuntimePayloadError{
                "missing payload fixture",
            });
        }
        return found->second;
    }

private:
    const PackageFixture& package_;
};

class FakePackageSource final : public LauncherPackageSource
{
public:
    explicit FakePackageSource(const PackageFixture& package)
        : package_{package}
    {
    }

    auto load(
        const fs::path& scratch_parent,
        LauncherInvocationMode mode)
        -> std::expected<
            LoadedLauncherPackage,
            RuntimePayloadError> override
    {
        ++load_count;
        observed_scratch_parent = scratch_parent;
        observed_mode = mode;
        return LoadedLauncherPackage{
            package_.manifest_json,
            package_.manifest,
            package_.manifest_sha256,
            std::make_unique<MemoryPayloadSource>(package_),
        };
    }

    const PackageFixture& package_;
    std::size_t load_count{};
    fs::path observed_scratch_parent{};
    LauncherInvocationMode observed_mode{};
};

class FakeBootstrapPlatform final : public LauncherBootstrapPlatform
{
public:
    explicit FakeBootstrapPlatform(const TemporaryTree& tree)
        : tree_{tree}
    {
    }

    auto validate_explicit_game_directory(std::string_view value)
        -> std::expected<
            GameInstallation,
            GameDiscoveryError> override
    {
        ++explicit_calls;
        explicit_value = value;
        return installation();
    }

    auto discover_game_installation()
        -> std::expected<
            GameInstallation,
            GameDiscoveryError> override
    {
        ++discovery_calls;
        if (discovery_failure)
        {
            return std::unexpected(GameDiscoveryError{
                GameDiscoveryErrorCode::MissingGame,
                "automatic fixture miss",
            });
        }
        return installation();
    }

    auto pick_game_installation()
        -> std::expected<
            GameInstallation,
            GameDiscoveryError> override
    {
        ++picker_calls;
        return installation();
    }

    auto local_app_data_directory()
        -> std::expected<
            fs::path,
            LauncherBootstrapError> override
    {
        ++local_data_calls;
        return tree_.root / "local";
    }

    auto runtime_nonce()
        -> std::expected<
            std::string,
            LauncherBootstrapError> override
    {
        ++nonce_calls;
        return "0123456789abcdef0123456789abcdef";
    }

    auto installation() const -> GameInstallation
    {
        return GameInstallation{
            {},
            tree_.root / "game",
            tree_.root / "game",
            tree_.root / "game" /
                "PenguinHotel-Win64-Shipping.exe",
        };
    }

    const TemporaryTree& tree_;
    bool discovery_failure{};
    std::size_t explicit_calls{};
    std::size_t discovery_calls{};
    std::size_t picker_calls{};
    std::size_t local_data_calls{};
    std::size_t nonce_calls{};
    std::string explicit_value{};
};

class FakeObservationPlatform final
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
        return std::unexpected(
            LauncherEffectError{"unexpected UAC"});
    }

    auto remove(
        const RemovalPlan&,
        const fs::path&,
        const fs::path&)
        -> std::expected<void, LauncherEffectError> override
    {
        called = true;
        return std::unexpected(
            LauncherEffectError{"unexpected UAC"});
    }

    bool called{};
};

class UnexpectedSteam final : public SteamGameLauncher
{
public:
    auto launch()
        -> std::expected<void, LauncherEffectError> override
    {
        called = true;
        return std::unexpected(
            LauncherEffectError{"unexpected Steam launch"});
    }

    bool called{};
};

auto application_inputs(
    std::span<const std::string_view> arguments,
    LauncherBootstrapPlatform& bootstrap,
    LauncherPackageSource& package,
    OriginalUserObservationPlatform& observation,
    ElevatedLoaderBroker& broker,
    SteamGameLauncher& steam)
    -> Win32LauncherApplicationInputs
{
    return Win32LauncherApplicationInputs{
        arguments,
        bootstrap,
        package,
        observation,
        broker,
        steam,
    };
}
} // namespace

auto main() -> int
{
    using namespace meccha::launcher;

    auto passed = true;
    const auto package = make_package();
    TemporaryTree explicit_tree{};
    FakeBootstrapPlatform explicit_bootstrap{explicit_tree};
    FakePackageSource explicit_package{package};
    FakeObservationPlatform explicit_observation{};
    UnexpectedBroker broker{};
    UnexpectedSteam steam{};
    constexpr std::string_view explicit_arguments[]{
        "--game-dir",
        "C:\\Games\\MECCHA カメレオン",
        "--prepare-only",
    };
    const auto explicit_result =
        run_win32_launcher_application(application_inputs(
            explicit_arguments,
            explicit_bootstrap,
            explicit_package,
            explicit_observation,
            broker,
            steam));
    const auto expected_root =
        explicit_tree.root / "local" /
        "MecchaCamouflage" / "v2";
    passed &= expect(
        explicit_result &&
            explicit_result->arguments.mode ==
                LauncherInvocationMode::PrepareOnly &&
            explicit_result->paths.data_root == expected_root &&
            explicit_result->paths.runtime_root ==
                expected_root / "runtime" &&
            explicit_result->paths.ownership_directory ==
                expected_root / "ownership" &&
            explicit_bootstrap.explicit_calls == 1U &&
            explicit_bootstrap.discovery_calls == 0U &&
            explicit_bootstrap.picker_calls == 0U &&
            explicit_bootstrap.explicit_value ==
                explicit_arguments[1] &&
            explicit_bootstrap.local_data_calls == 1U &&
            explicit_bootstrap.nonce_calls == 1U &&
            explicit_package.load_count == 1U &&
            explicit_package.observed_scratch_parent ==
                expected_root / "runtime" &&
            explicit_package.observed_mode ==
                LauncherInvocationMode::PrepareOnly &&
            explicit_observation.game_checks == 3U &&
            explicit_observation.launch_option_reads == 1U &&
            fs::exists(
                expected_root / "runtime" / "active" /
                "UE4SS.dll") &&
            fs::exists(
                explicit_tree.root / "game" / "dwmapi.dll") &&
            !broker.called && !steam.called,
        "explicit prepare-only application flow was not composed");

    TemporaryTree fallback_tree{};
    FakeBootstrapPlatform fallback_bootstrap{fallback_tree};
    fallback_bootstrap.discovery_failure = true;
    const auto fallback = resolve_launcher_game_installation(
        LauncherArguments{},
        fallback_bootstrap);
    passed &= expect(
        fallback &&
            fallback->install_directory ==
                fallback_tree.root / "game" &&
            fallback_bootstrap.explicit_calls == 0U &&
            fallback_bootstrap.discovery_calls == 1U &&
            fallback_bootstrap.picker_calls == 1U,
        "automatic discovery failure did not use the picker fallback");

    TemporaryTree running_tree{};
    FakeBootstrapPlatform running_bootstrap{running_tree};
    FakePackageSource running_package{package};
    FakeObservationPlatform running_observation{};
    running_observation.running = true;
    constexpr std::string_view prepare_only[]{"--prepare-only"};
    const auto running = run_win32_launcher_application(
        application_inputs(
            prepare_only,
            running_bootstrap,
            running_package,
            running_observation,
            broker,
            steam));
    passed &= expect(
        !running &&
            std::holds_alternative<LauncherWorkflowError>(
                running.error()) &&
            running_bootstrap.nonce_calls == 0U &&
            running_package.load_count == 0U &&
            running_observation.game_checks == 1U &&
            !fs::exists(
                running_tree.root / "local" /
                "MecchaCamouflage"),
        "running-game preflight reached nonce, package, or storage effects");

    TemporaryTree blocked_tree{};
    FakeBootstrapPlatform blocked_bootstrap{blocked_tree};
    FakePackageSource blocked_package{package};
    FakeObservationPlatform blocked_observation{};
    const auto held = acquire_launcher_instance();
    passed &= expect(held.has_value(), "test could not hold launcher mutex");
    const auto blocked = run_win32_launcher_application(
        application_inputs(
            prepare_only,
            blocked_bootstrap,
            blocked_package,
            blocked_observation,
            broker,
            steam));
    passed &= expect(
        !blocked &&
            std::holds_alternative<SingleInstanceError>(
                blocked.error()) &&
            blocked_bootstrap.discovery_calls == 0U &&
            blocked_bootstrap.local_data_calls == 0U &&
            blocked_package.load_count == 0U &&
            blocked_observation.game_checks == 0U,
        "second instance reached launcher bootstrap effects");

    Win32LauncherBootstrapPlatform native_bootstrap{};
    const auto local_app_data =
        native_bootstrap.local_app_data_directory();
    const auto nonce = native_bootstrap.runtime_nonce();
    const auto invalid_utf8 =
        native_bootstrap.validate_explicit_game_directory(
            std::string_view{"\xC3\x28", 2U});
    passed &= expect(
        local_app_data &&
            local_app_data->is_absolute() &&
            fs::is_directory(*local_app_data) &&
            nonce &&
            nonce->size() == 32U &&
            std::ranges::all_of(
                *nonce,
                [](char value)
                {
                    return (value >= '0' && value <= '9') ||
                           (value >= 'a' && value <= 'f');
                }) &&
            !invalid_utf8 &&
            invalid_utf8.error().code ==
                GameDiscoveryErrorCode::InvalidInstallDirectory,
        "native LocalAppData, nonce, or UTF-8 boundary is invalid");

    if (passed)
    {
        std::cout << "PASS launcher_application_win32\n";
        return 0;
    }
    return 1;
}
