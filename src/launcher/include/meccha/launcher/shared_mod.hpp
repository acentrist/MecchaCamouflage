#pragma once

#include <meccha/launcher/owned_file_storage.hpp>
#include <meccha/launcher/preparation.hpp>
#include <meccha/launcher/runtime_storage.hpp>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <string>
#include <vector>

namespace meccha::launcher
{
enum class SharedModErrorCode : std::uint8_t
{
    Manifest,
    Payload,
    Path,
    Plan,
    Store,
};

struct SharedModError
{
    SharedModErrorCode code{};
    std::string detail{};

    auto operator==(const SharedModError&) const -> bool = default;
};

struct SharedModFileMaterial
{
    OwnedFileExpectation expectation{};
    std::vector<std::byte> bytes{};
};

struct SharedModMaterial
{
    std::vector<ManifestFile> compatibility_files{};
    std::vector<SharedModFileMaterial> files{};
};

struct SharedModApplyResult
{
    std::size_t reused_owned{};
    std::size_t reused_unowned{};
    std::size_t created{};
    std::size_t replaced{};
    std::size_t removed_stale{};
};

struct SharedModRemovalResult
{
    std::size_t removed{};
};

#ifdef _WIN32
[[nodiscard]] auto build_shared_mod_material(
    const PayloadManifest& manifest,
    const Sha256Digest& manifest_sha256,
    RuntimePayloadSource& payload_source)
    -> std::expected<SharedModMaterial, SharedModError>;

auto apply_shared_mod_plan(
    SharedModAction action,
    const std::filesystem::path& shared_runtime_directory,
    const std::filesystem::path& ownership_directory,
    const SharedModMaterial& material)
    -> std::expected<SharedModApplyResult, SharedModError>;

auto apply_shared_mod_removal(
    const RemovalPlan& plan,
    const std::filesystem::path& shared_runtime_directory,
    const std::filesystem::path& ownership_directory,
    const SharedModMaterial& material)
    -> std::expected<SharedModRemovalResult, SharedModError>;
#endif
} // namespace meccha::launcher
