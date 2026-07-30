#include <meccha/launcher/managed_loader.hpp>

#include <meccha/launcher/hash.hpp>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace meccha::launcher
{
namespace
{
namespace fs = std::filesystem;

auto error(ManagedLoaderErrorCode code, std::string detail)
    -> std::unexpected<ManagedLoaderError>
{
    return std::unexpected(
        ManagedLoaderError{code, std::move(detail)});
}

auto byte_copy(std::string_view text) -> std::vector<std::byte>
{
    const auto bytes = std::as_bytes(std::span{text});
    return {bytes.begin(), bytes.end()};
}

auto ascii_path(const fs::path& path) -> std::optional<std::string>
{
    const auto native = path.native();
    if (!std::ranges::all_of(native, [](wchar_t character) {
            return character >= 0x20 && character <= 0x7e;
        }))
    {
        return std::nullopt;
    }
    std::string result{};
    result.reserve(native.size());
    std::ranges::transform(
        native,
        std::back_inserter(result),
        [](wchar_t character) {
            return static_cast<char>(character);
        });
    return result;
}

auto override_text(const fs::path& active_runtime_directory)
    -> std::expected<std::string, ManagedLoaderError>
{
    if (!active_runtime_directory.is_absolute() ||
        active_runtime_directory.lexically_normal() !=
            active_runtime_directory)
    {
        return error(
            ManagedLoaderErrorCode::PathEncoding,
            "The managed runtime path is not absolute and "
            "normalized.");
    }
    const auto attributes =
        GetFileAttributesW(active_runtime_directory.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES ||
        !(attributes & FILE_ATTRIBUTE_DIRECTORY) ||
        (attributes & FILE_ATTRIBUTE_REPARSE_POINT))
    {
        return error(
            ManagedLoaderErrorCode::PathEncoding,
            "The managed runtime path is unavailable or a reparse "
            "point.");
    }

    auto preferred = active_runtime_directory;
    preferred.make_preferred();
    if (auto direct = ascii_path(preferred); direct)
    {
        return *direct;
    }

    const auto required = GetShortPathNameW(
        preferred.c_str(),
        nullptr,
        0);
    if (required == 0)
    {
        return error(
            ManagedLoaderErrorCode::PathEncoding,
            "The pinned UE4SS proxy cannot represent the managed "
            "runtime path and no short path is available.");
    }
    std::vector<wchar_t> buffer(required);
    const auto written = GetShortPathNameW(
        preferred.c_str(),
        buffer.data(),
        static_cast<DWORD>(buffer.size()));
    if (written == 0 || written >= buffer.size())
    {
        return error(
            ManagedLoaderErrorCode::PathEncoding,
            "Windows could not produce a stable short runtime "
            "path.");
    }
    const auto short_path =
        fs::path{std::wstring_view{buffer.data(), written}};
    auto ascii = ascii_path(short_path);
    if (!ascii)
    {
        return error(
            ManagedLoaderErrorCode::PathEncoding,
            "The available short runtime path is not ASCII-safe for "
            "the pinned UE4SS proxy.");
    }
    return *ascii;
}

auto proxy_entry(const PayloadManifest& manifest)
    -> std::expected<ManifestFile, ManagedLoaderError>
{
    std::optional<ManifestFile> proxy{};
    for (const auto& file : manifest.files)
    {
        if (file.role != FileRole::Proxy)
        {
            continue;
        }
        if (proxy)
        {
            return error(
                ManagedLoaderErrorCode::Manifest,
                "The payload contains multiple proxy files.");
        }
        proxy = file;
    }
    if (!proxy || proxy->path != "dwmapi.dll")
    {
        return error(
            ManagedLoaderErrorCode::Manifest,
            "The payload must contain exactly one canonical "
            "dwmapi.dll proxy.");
    }
    return *proxy;
}

auto state_matches(
    ArtifactDisposition disposition,
    ArtifactState state) -> bool
{
    switch (disposition)
    {
    case ArtifactDisposition::CreateOwned:
        return state == ArtifactState::Missing;
    case ArtifactDisposition::ReuseOwned:
        return state == ArtifactState::ExactOwned;
    case ArtifactDisposition::ReuseUnowned:
        return state == ArtifactState::ExactUnowned;
    case ArtifactDisposition::ReplaceOwned:
        return state == ArtifactState::OwnedPrevious;
    case ArtifactDisposition::None:
        return false;
    }
    return false;
}

auto store_error(
    std::string_view artifact,
    const OwnedFileStoreError& cause)
    -> std::unexpected<ManagedLoaderError>
{
    return error(
        ManagedLoaderErrorCode::Store,
        std::string{artifact} + ": " + cause.detail);
}

auto install_if_needed(
    ArtifactDisposition disposition,
    Win32OwnedFileStore& store,
    const OwnedFileExpectation& expectation,
    std::span<const std::byte> bytes)
    -> std::expected<
        std::optional<OwnedFileInstallResult>,
        ManagedLoaderError>
{
    if (disposition != ArtifactDisposition::CreateOwned &&
        disposition != ArtifactDisposition::ReplaceOwned)
    {
        return std::nullopt;
    }
    auto installed = store.install(expectation, bytes);
    if (!installed)
    {
        return store_error(expectation.file.path, installed.error());
    }
    return *installed;
}
} // namespace

auto build_managed_loader_material(
    const PayloadManifest& manifest,
    const Sha256Digest& manifest_sha256,
    const fs::path& active_runtime_directory,
    RuntimePayloadSource& payload_source)
    -> std::expected<ManagedLoaderMaterial, ManagedLoaderError>
{
    auto proxy = proxy_entry(manifest);
    if (!proxy)
    {
        return std::unexpected(proxy.error());
    }
    auto proxy_bytes = payload_source.read_file(proxy->path);
    if (!proxy_bytes)
    {
        return error(
            ManagedLoaderErrorCode::Payload,
            "The payload source could not provide dwmapi.dll: " +
                proxy_bytes.error().detail);
    }
    const auto proxy_hash = sha256_bytes(*proxy_bytes);
    if (!proxy_hash || proxy_bytes->size() != proxy->size ||
        *proxy_hash != proxy->sha256)
    {
        return error(
            ManagedLoaderErrorCode::Payload,
            "The embedded dwmapi.dll does not match the manifest.");
    }

    auto override = override_text(active_runtime_directory);
    if (!override)
    {
        return std::unexpected(override.error());
    }
    auto override_bytes = byte_copy(*override);
    const auto override_hash = sha256_bytes(override_bytes);
    if (!override_hash)
    {
        return error(
            ManagedLoaderErrorCode::Payload,
            "Could not hash generated override.txt.");
    }
    const auto override_file = ManifestFile{
        "override.txt",
        FileRole::Override,
        override_bytes.size(),
        *override_hash,
    };
    return ManagedLoaderMaterial{
        OwnedFileExpectation{
            manifest.product_version,
            manifest_sha256,
            *proxy,
        },
        std::move(*proxy_bytes),
        OwnedFileExpectation{
            manifest.product_version,
            manifest_sha256,
            override_file,
        },
        std::move(override_bytes),
    };
}

auto apply_managed_loader_plan(
    const ManagedLoaderPlan& plan,
    const fs::path& game_directory,
    const fs::path& ownership_directory,
    const ManagedLoaderMaterial& material)
    -> std::expected<ManagedLoaderApplyResult, ManagedLoaderError>
{
    if (plan.elevated)
    {
        return error(
            ManagedLoaderErrorCode::ElevationRequired,
            "The managed loader plan requires the minimal elevated "
            "broker.");
    }

    Win32OwnedFileStore proxy_store{
        game_directory / "dwmapi.dll",
        ownership_directory / "dwmapi.owner.json",
        "dwmapi.dll",
        FileRole::Proxy};
    Win32OwnedFileStore override_store{
        game_directory / "override.txt",
        ownership_directory / "override.owner.json",
        "override.txt",
        FileRole::Override};

    auto proxy_recovered = proxy_store.recover();
    if (!proxy_recovered)
    {
        return store_error("dwmapi.dll", proxy_recovered.error());
    }
    auto override_recovered = override_store.recover();
    if (!override_recovered)
    {
        return store_error(
            "override.txt",
            override_recovered.error());
    }
    const auto proxy_state = proxy_store.observe(material.proxy);
    if (!proxy_state)
    {
        return store_error("dwmapi.dll", proxy_state.error());
    }
    const auto override_state =
        override_store.observe(material.override_file);
    if (!override_state)
    {
        return store_error(
            "override.txt",
            override_state.error());
    }
    if (!state_matches(plan.proxy, *proxy_state) ||
        !state_matches(
            plan.override_file,
            *override_state))
    {
        return error(
            ManagedLoaderErrorCode::Plan,
            "The loader filesystem changed after planning; no "
            "loader mutation was performed.");
    }

    auto proxy_result = install_if_needed(
        plan.proxy,
        proxy_store,
        material.proxy,
        material.proxy_bytes);
    if (!proxy_result)
    {
        return std::unexpected(proxy_result.error());
    }
    auto override_result = install_if_needed(
        plan.override_file,
        override_store,
        material.override_file,
        material.override_bytes);
    if (!override_result)
    {
        return std::unexpected(override_result.error());
    }
    return ManagedLoaderApplyResult{
        *proxy_result,
        *override_result,
    };
}

auto apply_managed_loader_removal(
    const RemovalPlan& plan,
    const fs::path& game_directory,
    const fs::path& ownership_directory)
    -> std::expected<ManagedLoaderRemovalResult, ManagedLoaderError>
{
    if (plan.runtime_cache != RemovalAction::None ||
        plan.mod != RemovalAction::None)
    {
        return error(
            ManagedLoaderErrorCode::Plan,
            "The loader remover received non-loader actions.");
    }
    if (plan.elevated_loader)
    {
        return error(
            ManagedLoaderErrorCode::ElevationRequired,
            "The managed loader removal requires the minimal "
            "elevated broker.");
    }

    Win32OwnedFileStore proxy_store{
        game_directory / "dwmapi.dll",
        ownership_directory / "dwmapi.owner.json",
        "dwmapi.dll",
        FileRole::Proxy};
    Win32OwnedFileStore override_store{
        game_directory / "override.txt",
        ownership_directory / "override.owner.json",
        "override.txt",
        FileRole::Override};
    auto proxy_recovered = proxy_store.recover();
    if (!proxy_recovered)
    {
        return store_error("dwmapi.dll", proxy_recovered.error());
    }
    auto override_recovered = override_store.recover();
    if (!override_recovered)
    {
        return store_error(
            "override.txt",
            override_recovered.error());
    }
    const auto proxy_removable = proxy_store.removable();
    if (!proxy_removable)
    {
        return store_error(
            "dwmapi.dll",
            proxy_removable.error());
    }
    const auto override_removable = override_store.removable();
    if (!override_removable)
    {
        return store_error(
            "override.txt",
            override_removable.error());
    }
    const auto remove_proxy =
        plan.proxy == RemovalAction::RemoveOwned;
    const auto remove_override =
        plan.override_file == RemovalAction::RemoveOwned;
    if (remove_proxy != *proxy_removable ||
        remove_override != *override_removable)
    {
        return error(
            ManagedLoaderErrorCode::Plan,
            "The loader filesystem changed after removal planning; "
            "nothing was removed.");
    }

    auto result = ManagedLoaderRemovalResult{};
    if (remove_proxy)
    {
        auto removed = proxy_store.remove_owned();
        if (!removed)
        {
            return store_error("dwmapi.dll", removed.error());
        }
        result.proxy_removed = *removed;
    }
    if (remove_override)
    {
        auto removed = override_store.remove_owned();
        if (!removed)
        {
            return store_error(
                "override.txt",
                removed.error());
        }
        result.override_removed = *removed;
    }
    return result;
}
} // namespace meccha::launcher
