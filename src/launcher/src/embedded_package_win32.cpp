#include <meccha/launcher/embedded_package.hpp>

#include <meccha/core/utf8.hpp>
#include <meccha/launcher/hash.hpp>

#include <algorithm>
#include <cstddef>
#include <expected>
#include <memory>
#include <span>
#include <string>
#include <utility>

namespace meccha::launcher
{
namespace
{
constexpr std::size_t MaximumManifestBytes{
    1024U * 1024U};

auto package_error(std::string detail)
    -> std::unexpected<RuntimePayloadError>
{
    return std::unexpected(
        RuntimePayloadError{std::move(detail)});
}
} // namespace

auto load_embedded_launcher_package(
    std::span<const std::byte> manifest_bytes,
    std::span<const std::byte> cabinet_bytes,
    const std::filesystem::path& scratch_parent)
    -> std::expected<
        LoadedLauncherPackage,
        RuntimePayloadError>
{
    if (manifest_bytes.empty() ||
        manifest_bytes.size() > MaximumManifestBytes)
    {
        return package_error(
            "The embedded payload manifest size is invalid.");
    }
    const auto manifest_characters =
        std::span{
            reinterpret_cast<const char*>(
                manifest_bytes.data()),
            manifest_bytes.size()};
    auto manifest_json = std::string{
        manifest_characters.begin(),
        manifest_characters.end()};
    if (!core::valid_utf8(manifest_json) ||
        manifest_json.find('\0') != std::string::npos)
    {
        return package_error(
            "The embedded payload manifest is not strict UTF-8.");
    }
    const auto manifest =
        parse_payload_manifest(manifest_json);
    if (!manifest)
    {
        return package_error(
            "The embedded payload manifest is invalid: " +
            manifest.error().detail);
    }
    const auto manifest_sha256 =
        sha256_bytes(manifest_bytes);
    if (!manifest_sha256)
    {
        return package_error(
            "The embedded payload manifest could not be hashed.");
    }
    auto payload_source = Win32CabPayloadSource::open(
        cabinet_bytes,
        *manifest,
        scratch_parent);
    if (!payload_source)
    {
        return std::unexpected(payload_source.error());
    }
    return LoadedLauncherPackage{
        std::move(manifest_json),
        *manifest,
        *manifest_sha256,
        std::make_unique<Win32CabPayloadSource>(
            std::move(*payload_source)),
    };
}

Win32EmbeddedLauncherPackageSource::
    Win32EmbeddedLauncherPackageSource(
        std::uint16_t manifest_resource_id,
        std::uint16_t cabinet_resource_id)
    : manifest_resource_id_(manifest_resource_id),
      cabinet_resource_id_(cabinet_resource_id)
{
}

auto Win32EmbeddedLauncherPackageSource::load(
    const std::filesystem::path& scratch_parent,
    LauncherInvocationMode mode)
    -> std::expected<
        LoadedLauncherPackage,
        RuntimePayloadError>
{
    static_cast<void>(mode);
    const auto manifest =
        read_current_module_rcdata(
            manifest_resource_id_);
    if (!manifest)
    {
        return std::unexpected(manifest.error());
    }
    const auto cabinet =
        read_current_module_rcdata(
            cabinet_resource_id_);
    if (!cabinet)
    {
        return std::unexpected(cabinet.error());
    }
    return load_embedded_launcher_package(
        *manifest,
        *cabinet,
        scratch_parent);
}
} // namespace meccha::launcher
