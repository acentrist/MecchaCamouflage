#include <meccha/launcher/execution_win32.hpp>
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
#include <iterator>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace
{
using namespace meccha::launcher;
namespace fs = std::filesystem;

auto digest(std::byte value) -> Sha256Digest
{
    Sha256Digest result{};
    result.bytes.fill(value);
    return result;
}

auto bytes(std::string_view text) -> std::vector<std::byte>
{
    const auto view = std::as_bytes(std::span{text});
    return {view.begin(), view.end()};
}

auto read_text(const fs::path& path) -> std::string
{
    std::ifstream input{path, std::ios::binary};
    return {
        std::istreambuf_iterator<char>{input},
        std::istreambuf_iterator<char>{},
    };
}

auto write_text(const fs::path& path, std::string_view text) -> void
{
    fs::create_directories(path.parent_path());
    std::ofstream output{
        path,
        std::ios::binary | std::ios::trunc};
    output.write(
        text.data(),
        static_cast<std::streamsize>(text.size()));
}

class TemporaryTree
{
public:
    TemporaryTree()
    {
        static std::uint64_t sequence{};
        ++sequence;
        root = fs::temp_directory_path() /
               ("meccha-v2-execution-win32-" +
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

auto expect(bool condition, std::string_view message) -> bool
{
    if (!condition)
    {
        std::cerr << "FAIL launcher_execution_win32: "
                  << message << '\n';
    }
    return condition;
}

class ReusableRuntimeStorage final : public RuntimeStorage
{
public:
    ReusableRuntimeStorage(
        Sha256Digest manifest,
        std::vector<std::string>& sequence)
        : manifest_(manifest),
          sequence_(sequence)
    {
    }

    auto identify_generation(std::string_view name)
        -> std::expected<
            GenerationIdentity,
            RuntimeStorageError> override
    {
        if (name == "active" && active_)
        {
            return GenerationIdentity{
                GenerationState::OwnedExact,
                manifest_,
            };
        }
        return GenerationIdentity{GenerationState::Missing, {}};
    }

    auto list_staging_generations()
        -> std::expected<
            std::vector<std::string>,
            RuntimeStorageError> override
    {
        return std::vector<std::string>{};
    }

    auto read_journal()
        -> std::expected<
            std::optional<RuntimeTransactionJournal>,
            RuntimeStorageError> override
    {
        return std::nullopt;
    }

    auto stage_generation(
        std::string_view,
        const Sha256Digest&)
        -> std::expected<void, RuntimeStorageError> override
    {
        return unexpected_mutation();
    }

    auto write_journal(const RuntimeTransactionJournal&)
        -> std::expected<void, RuntimeStorageError> override
    {
        return unexpected_mutation();
    }

    auto rename_generation(std::string_view, std::string_view)
        -> std::expected<void, RuntimeStorageError> override
    {
        return unexpected_mutation();
    }

    auto remove_generation(
        std::string_view name,
        const Sha256Digest& manifest)
        -> std::expected<void, RuntimeStorageError> override
    {
        if (name != "active" || !active_ ||
            manifest != manifest_)
        {
            return unexpected_mutation();
        }
        active_ = false;
        sequence_.emplace_back("runtime remove");
        return {};
    }

    auto remove_journal()
        -> std::expected<void, RuntimeStorageError> override
    {
        return unexpected_mutation();
    }

private:
    static auto unexpected_mutation()
        -> std::unexpected<RuntimeStorageError>
    {
        return std::unexpected(RuntimeStorageError{
            RuntimeStorageErrorCode::Conflict,
            "reuse attempted a runtime mutation",
        });
    }

    Sha256Digest manifest_{};
    std::vector<std::string>& sequence_;
    bool active_{true};
};

class RecordingBroker final : public ElevatedLoaderBroker
{
public:
    explicit RecordingBroker(std::vector<std::string>& sequence)
        : sequence_(sequence)
    {
    }

    auto apply(
        const ManagedLoaderPlan& plan,
        const fs::path&,
        const fs::path&,
        const ManagedLoaderMaterial&)
        -> std::expected<void, LauncherEffectError> override
    {
        const auto event =
            plan.elevated ? "broker apply elevated" : "broker apply";
        events.emplace_back(event);
        sequence_.emplace_back(event);
        return {};
    }

    auto remove(
        const RemovalPlan& plan,
        const fs::path&,
        const fs::path&)
        -> std::expected<void, LauncherEffectError> override
    {
        events.emplace_back("broker remove");
        sequence_.emplace_back("broker remove");
        removed_runtime_action = plan.runtime_cache;
        removed_mod_action = plan.mod;
        return {};
    }

    std::vector<std::string> events{};
    RemovalAction removed_runtime_action{RemovalAction::RemoveOwned};
    RemovalAction removed_mod_action{RemovalAction::RemoveOwned};

private:
    std::vector<std::string>& sequence_;
};

class RecordingSteamLauncher final : public SteamGameLauncher
{
public:
    explicit RecordingSteamLauncher(
        std::vector<std::string>& sequence)
        : sequence_(sequence)
    {
    }

    auto launch()
        -> std::expected<void, LauncherEffectError> override
    {
        events.emplace_back("steam");
        sequence_.emplace_back("steam");
        return {};
    }

    std::vector<std::string> events{};

private:
    std::vector<std::string>& sequence_;
};

class StaticMaterialProvider final : public LauncherMaterialProvider
{
public:
    StaticMaterialProvider(
        const ManagedLoaderMaterial& managed,
        const SharedModMaterial& shared,
        std::vector<std::string>* sequence = nullptr)
        : managed_{managed},
          shared_{shared},
          sequence_{sequence}
    {
    }

    auto managed_loader()
        -> std::expected<
            const ManagedLoaderMaterial*,
            LauncherEffectError> override
    {
        if (sequence_ != nullptr)
        {
            sequence_->emplace_back("managed material");
        }
        return &managed_;
    }

    auto shared_mod()
        -> std::expected<
            const SharedModMaterial*,
            LauncherEffectError> override
    {
        if (sequence_ != nullptr)
        {
            sequence_->emplace_back("shared material");
        }
        return &shared_;
    }

private:
    const ManagedLoaderMaterial& managed_;
    const SharedModMaterial& shared_;
    std::vector<std::string>* sequence_{};
};

class MapPayloadSource final : public RuntimePayloadSource
{
public:
    auto read_file(std::string_view relative_path)
        -> std::expected<
            std::vector<std::byte>,
            RuntimePayloadError> override
    {
        ++reads;
        const auto found = files.find(std::string{relative_path});
        if (found == files.end())
        {
            return std::unexpected(RuntimePayloadError{
                "missing payload fixture",
            });
        }
        return found->second;
    }

    std::unordered_map<std::string, std::vector<std::byte>> files{};
    std::size_t reads{};
};
} // namespace

auto main() -> int
{
    using namespace meccha::launcher;

    const auto manifest = digest(std::byte{0x42});
    std::vector<std::string> sequence{};
    ReusableRuntimeStorage storage{manifest, sequence};
    RecordingBroker broker{sequence};
    RecordingSteamLauncher steam{sequence};
    const auto root =
        (fs::temp_directory_path() /
         "meccha-v2-execution-adapter")
            .lexically_normal();
    const auto managed_material = ManagedLoaderMaterial{};
    const auto shared_material = SharedModMaterial{};
    StaticMaterialProvider material_provider{
        managed_material,
        shared_material,
        &sequence};
    Win32LauncherExecutionBackend backend{
        storage,
        manifest,
        "0123456789abcdef0123456789abcdef",
        root / "game",
        root / "ownership",
        root / "shared",
        material_provider,
        broker,
        steam,
    };

    const auto result = execute_preparation(
        PreparationPlan{
            DeploymentMode::Managed,
            RuntimeCacheAction::Reuse,
            ManagedLoaderPlan{
                ArtifactDisposition::CreateOwned,
                ArtifactDisposition::CreateOwned,
                true,
            },
            std::nullopt,
            true,
        },
        backend);
    const auto passed = expect(
        result &&
            result->runtime_cache == RuntimeCacheAction::Reuse &&
            result->managed_loader_applied &&
            result->steam_launched &&
            broker.events ==
                std::vector<std::string>{
                    "broker apply elevated",
                } &&
            steam.events ==
                std::vector<std::string>{"steam"} &&
            sequence ==
                std::vector<std::string>{
                    "managed material",
                    "broker apply elevated",
                    "steam",
                },
        "exact runtime did not route elevated loader before Steam");

    sequence.clear();
    const auto removal_result = execute_removal(
        RemovalMode::Managed,
        RemovalPlan{
            RemovalAction::RemoveOwned,
            RemovalAction::RemoveOwned,
            RemovalAction::RemoveOwned,
            RemovalAction::None,
            true,
        },
        backend);
    const auto all_passed =
        passed &&
        expect(
            removal_result &&
                removal_result->managed_loader_removed &&
                removal_result->runtime_cache_removed &&
                broker.removed_runtime_action ==
                    RemovalAction::None &&
                broker.removed_mod_action ==
                    RemovalAction::None &&
                sequence ==
                    std::vector<std::string>{
                        "broker remove",
                        "runtime remove",
                    },
            "elevated removal was not reduced to loader-only input "
            "before cache cleanup");

    TemporaryTree normal{};
    const auto proxy_bytes = bytes("proxy");
    const auto override_bytes = bytes("runtime-active");
    const auto proxy_hash = sha256_bytes(proxy_bytes);
    const auto override_hash = sha256_bytes(override_bytes);
    auto normal_passed = expect(
        proxy_hash && override_hash,
        "normal loader fixture could not be hashed");
    auto normal_sequence = std::vector<std::string>{};
    ReusableRuntimeStorage normal_storage{
        manifest,
        normal_sequence};
    RecordingBroker normal_broker{normal_sequence};
    RecordingSteamLauncher normal_steam{normal_sequence};
    const auto normal_material = ManagedLoaderMaterial{
        OwnedFileExpectation{
            "2.0.0",
            manifest,
            ManifestFile{
                "dwmapi.dll",
                FileRole::Proxy,
                proxy_bytes.size(),
                proxy_hash ? *proxy_hash : Sha256Digest{},
            },
        },
        proxy_bytes,
        OwnedFileExpectation{
            "2.0.0",
            manifest,
            ManifestFile{
                "override.txt",
                FileRole::Override,
                override_bytes.size(),
                override_hash ? *override_hash : Sha256Digest{},
            },
        },
        override_bytes,
    };
    StaticMaterialProvider normal_material_provider{
        normal_material,
        shared_material};
    Win32LauncherExecutionBackend normal_backend{
        normal_storage,
        manifest,
        "0123456789abcdef0123456789abcdef",
        normal.root / "game",
        normal.root / "ownership",
        normal.root / "shared",
        normal_material_provider,
        normal_broker,
        normal_steam,
    };
    const auto normal_result = execute_preparation(
        PreparationPlan{
            DeploymentMode::Managed,
            RuntimeCacheAction::Reuse,
            ManagedLoaderPlan{
                ArtifactDisposition::CreateOwned,
                ArtifactDisposition::CreateOwned,
                false,
            },
            std::nullopt,
            false,
        },
        normal_backend);
    normal_passed &= expect(
        normal_result &&
            normal_result->managed_loader_applied &&
            !normal_result->steam_launched &&
            read_text(normal.root / "game" / "dwmapi.dll") ==
                "proxy" &&
            read_text(normal.root / "game" / "override.txt") ==
                "runtime-active" &&
            normal_sequence.empty() &&
            normal_broker.events.empty(),
        "normal managed execution did not use the owned-file adapter");

    TemporaryTree shared{};
    write_text(shared.root / "shared" / "UE4SS.dll", "runtime");
    fs::create_directories(shared.root / "shared" / "Mods");
    const auto runtime_bytes = bytes("runtime");
    const auto main_bytes = bytes("main");
    const auto enabled_bytes = bytes("");
    const auto runtime_hash = sha256_bytes(runtime_bytes);
    const auto main_hash = sha256_bytes(main_bytes);
    const auto enabled_hash = sha256_bytes(enabled_bytes);
    auto shared_passed = expect(
        runtime_hash && main_hash && enabled_hash,
        "shared fixture could not be hashed");
    const auto shared_material_fixture = SharedModMaterial{
        {
            ManifestFile{
                "UE4SS.dll",
                FileRole::Runtime,
                runtime_bytes.size(),
                runtime_hash ? *runtime_hash : Sha256Digest{},
            },
        },
        {
            SharedModFileMaterial{
                OwnedFileExpectation{
                    "2.0.0",
                    manifest,
                    ManifestFile{
                        "Mods/MecchaCamouflage/dlls/main.dll",
                        FileRole::Mod,
                        main_bytes.size(),
                        main_hash ? *main_hash : Sha256Digest{},
                    },
                },
                main_bytes,
            },
            SharedModFileMaterial{
                OwnedFileExpectation{
                    "2.0.0",
                    manifest,
                    ManifestFile{
                        "Mods/MecchaCamouflage/enabled.txt",
                        FileRole::Mod,
                        enabled_bytes.size(),
                        enabled_hash ? *enabled_hash : Sha256Digest{},
                    },
                },
                enabled_bytes,
            },
        },
    };
    auto shared_sequence = std::vector<std::string>{};
    ReusableRuntimeStorage shared_storage{
        manifest,
        shared_sequence};
    RecordingBroker shared_broker{shared_sequence};
    RecordingSteamLauncher shared_steam{shared_sequence};
    StaticMaterialProvider shared_material_provider{
        managed_material,
        shared_material_fixture};
    Win32LauncherExecutionBackend shared_backend{
        shared_storage,
        manifest,
        "0123456789abcdef0123456789abcdef",
        shared.root / "game",
        shared.root / "ownership",
        shared.root / "shared",
        shared_material_provider,
        shared_broker,
        shared_steam,
    };
    const auto shared_result = execute_preparation(
        PreparationPlan{
            DeploymentMode::Shared,
            RuntimeCacheAction::None,
            std::nullopt,
            SharedModAction::Install,
            false,
        },
        shared_backend);
    shared_passed &= expect(
        shared_result && shared_result->shared_mod_applied &&
            read_text(
                shared.root / "shared" / "Mods" /
                "MecchaCamouflage" / "dlls" / "main.dll") ==
                "main" &&
            shared_sequence.empty() &&
            shared_broker.events.empty(),
        "shared execution did not use the isolated mod adapter");
    const auto shared_remove = execute_removal(
        RemovalMode::Shared,
        RemovalPlan{
            RemovalAction::None,
            RemovalAction::None,
            RemovalAction::None,
            RemovalAction::RemoveOwned,
            false,
        },
        shared_backend);
    shared_passed &= expect(
        shared_remove && shared_remove->shared_mod_removed &&
            !fs::exists(
                shared.root / "shared" / "Mods" /
                "MecchaCamouflage") &&
            read_text(shared.root / "shared" / "UE4SS.dll") ==
                "runtime",
        "shared execution removal touched the runtime or left the mod");

    TemporaryTree provider_tree{};
    const auto provider_proxy = bytes("provider-proxy");
    const auto provider_proxy_hash = sha256_bytes(provider_proxy);
    auto provider_passed = expect(
        provider_proxy_hash.has_value(),
        "lazy material fixture could not be hashed");
    auto provider_manifest = PayloadManifest{
        1,
        "2.0.0",
        "6c26f038751b3d96059d4a9148f5d093012d55ad",
        {
            ManifestFile{
                "dwmapi.dll",
                FileRole::Proxy,
                provider_proxy.size(),
                provider_proxy_hash.value_or(Sha256Digest{}),
            },
        },
        {},
        provider_proxy.size(),
    };
    MapPayloadSource provider_source{};
    provider_source.files.emplace(
        "dwmapi.dll",
        provider_proxy);
    const auto active =
        (provider_tree.root / "runtime/active").lexically_normal();
    Win32LauncherMaterialProvider lazy_provider{
        provider_manifest,
        manifest,
        active,
        provider_source};
    provider_passed &= expect(
        !lazy_provider.managed_loader(),
        "managed material was built before runtime publication");
    fs::create_directories(active);
    const auto built_material = lazy_provider.managed_loader();
    const auto cached_material = lazy_provider.managed_loader();
    provider_passed &= expect(
        built_material &&
            cached_material &&
            *built_material == *cached_material &&
            (**built_material).proxy_bytes == provider_proxy &&
            !(**built_material).override_bytes.empty() &&
            provider_source.reads == 2U,
        "managed material was not retried once and cached after "
        "runtime publication");

    if (all_passed && normal_passed && shared_passed &&
        provider_passed)
    {
        std::cout << "PASS launcher_execution_win32\n";
        return 0;
    }
    return 1;
}
