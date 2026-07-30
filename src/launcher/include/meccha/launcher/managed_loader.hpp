#pragma once

#include <meccha/launcher/owned_file_storage.hpp>
#include <meccha/launcher/preparation.hpp>
#include <meccha/launcher/runtime_storage.hpp>

#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace meccha::launcher
{
enum class ManagedLoaderErrorCode : std::uint8_t
{
    Manifest,
    Payload,
    PathEncoding,
    Plan,
    ElevationRequired,
    Store,
};

struct ManagedLoaderError
{
    ManagedLoaderErrorCode code{};
    std::string detail{};

    auto operator==(const ManagedLoaderError&) const -> bool = default;
};

struct ManagedLoaderMaterial
{
    OwnedFileExpectation proxy{};
    std::vector<std::byte> proxy_bytes{};
    OwnedFileExpectation override_file{};
    std::vector<std::byte> override_bytes{};
};

struct ManagedLoaderApplyResult
{
    std::optional<OwnedFileInstallResult> proxy{};
    std::optional<OwnedFileInstallResult> override_file{};
};

#ifdef _WIN32
[[nodiscard]] auto build_managed_loader_material(
    const PayloadManifest& manifest,
    const Sha256Digest& manifest_sha256,
    const std::filesystem::path& active_runtime_directory,
    RuntimePayloadSource& payload_source)
    -> std::expected<ManagedLoaderMaterial, ManagedLoaderError>;

auto apply_managed_loader_plan(
    const ManagedLoaderPlan& plan,
    const std::filesystem::path& game_directory,
    const std::filesystem::path& ownership_directory,
    const ManagedLoaderMaterial& material)
    -> std::expected<ManagedLoaderApplyResult, ManagedLoaderError>;
#endif
} // namespace meccha::launcher
