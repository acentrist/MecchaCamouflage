#pragma once

#include <meccha/launcher/deployment_paths.hpp>
#include <meccha/launcher/manifest.hpp>
#include <meccha/launcher/observation_assembly.hpp>

#include <expected>
#include <filesystem>
#include <optional>
#include <string>

namespace meccha::launcher
{
#ifdef _WIN32
class RuntimeStorage;
class LauncherMaterialProvider;

struct Win32LauncherObservationPaths
{
    std::filesystem::path game_directory{};
    std::filesystem::path runtime_root{};
    std::filesystem::path active_runtime_directory{};
    std::filesystem::path ownership_directory{};

    auto operator==(const Win32LauncherObservationPaths&) const
        -> bool = default;
};

class OriginalUserObservationPlatform
{
public:
    OriginalUserObservationPlatform() = default;
    OriginalUserObservationPlatform(
        const OriginalUserObservationPlatform&) = delete;
    auto operator=(const OriginalUserObservationPlatform&)
        -> OriginalUserObservationPlatform& = delete;
    OriginalUserObservationPlatform(
        OriginalUserObservationPlatform&&) = delete;
    auto operator=(OriginalUserObservationPlatform&&)
        -> OriginalUserObservationPlatform& = delete;
    virtual ~OriginalUserObservationPlatform() = default;

    virtual auto game_running()
        -> std::expected<
            bool,
            LauncherObservationError> = 0;

    virtual auto steam_launch_options()
        -> std::expected<
            std::optional<std::string>,
            LauncherObservationError> = 0;

    virtual auto directory_writable(
        const std::filesystem::path& path)
        -> std::expected<
            bool,
            LauncherObservationError> = 0;
};

class Win32OriginalUserObservationPlatform final
    : public OriginalUserObservationPlatform
{
public:
    auto game_running()
        -> std::expected<
            bool,
            LauncherObservationError> override;

    auto steam_launch_options()
        -> std::expected<
            std::optional<std::string>,
            LauncherObservationError> override;

    auto directory_writable(
        const std::filesystem::path& path)
        -> std::expected<
            bool,
            LauncherObservationError> override;
};

class Win32LauncherObservationSource final
    : public LauncherObservationSource,
      public SharedRuntimeDirectorySource
{
public:
    Win32LauncherObservationSource(
        const PayloadManifest& manifest,
        Sha256Digest manifest_sha256,
        RuntimeStorage& runtime_storage,
        Win32LauncherObservationPaths paths,
        LauncherMaterialProvider& material_provider,
        OriginalUserObservationPlatform& platform);

    auto observe_preparation()
        -> std::expected<
            PreparationEnvironment,
            LauncherObservationError> override;

    auto observe_removal()
        -> std::expected<
            RemovalObservation,
            LauncherObservationError> override;

    [[nodiscard]] auto shared_runtime_directory() const
        -> const std::optional<std::filesystem::path>&;

    auto selected_shared_runtime_directory() const
        -> std::expected<
            std::filesystem::path,
            LauncherEffectError> override;

private:
    auto observe_evidence()
        -> std::expected<
            LauncherObservationEvidence,
            LauncherObservationError>;

    const PayloadManifest& manifest_;
    Sha256Digest manifest_sha256_{};
    RuntimeStorage& runtime_storage_;
    Win32LauncherObservationPaths paths_{};
    LauncherMaterialProvider& material_provider_;
    OriginalUserObservationPlatform& platform_;
    std::optional<std::filesystem::path>
        shared_runtime_directory_{};
};
#endif
} // namespace meccha::launcher
