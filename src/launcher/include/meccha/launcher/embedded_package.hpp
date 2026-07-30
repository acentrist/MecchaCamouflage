#pragma once

#include <meccha/launcher/application_win32.hpp>
#include <meccha/launcher/embedded_payload.hpp>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <span>

namespace meccha::launcher
{
#ifdef _WIN32
inline constexpr std::uint16_t
    LauncherManifestResourceId{101U};
inline constexpr std::uint16_t
    LauncherCabResourceId{102U};

[[nodiscard]] auto load_embedded_launcher_package(
    std::span<const std::byte> manifest_bytes,
    std::span<const std::byte> cabinet_bytes,
    const std::filesystem::path& scratch_parent)
    -> std::expected<
        LoadedLauncherPackage,
        RuntimePayloadError>;

class Win32EmbeddedLauncherPackageSource final
    : public LauncherPackageSource
{
public:
    explicit Win32EmbeddedLauncherPackageSource(
        std::uint16_t manifest_resource_id =
            LauncherManifestResourceId,
        std::uint16_t cabinet_resource_id =
            LauncherCabResourceId);

    auto load(
        const std::filesystem::path& scratch_parent,
        LauncherInvocationMode mode)
        -> std::expected<
            LoadedLauncherPackage,
            RuntimePayloadError> override;

private:
    std::uint16_t manifest_resource_id_{};
    std::uint16_t cabinet_resource_id_{};
};
#endif
} // namespace meccha::launcher
