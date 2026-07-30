#include <meccha/launcher/shared_mod.hpp>

#include <meccha/launcher/hash.hpp>

#include "owned_file_win32_io.hpp"

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

constexpr std::string_view ModPrefix{"Mods/MecchaCamouflage/"};
constexpr std::string_view MainPath{
    "Mods/MecchaCamouflage/dlls/main.dll"};
constexpr std::string_view EnabledPath{
    "Mods/MecchaCamouflage/enabled.txt"};

auto error(SharedModErrorCode code, std::string detail)
    -> std::unexpected<SharedModError>
{
    return std::unexpected(
        SharedModError{code, std::move(detail)});
}

auto store_error(
    std::string_view path,
    const OwnedFileStoreError& cause)
    -> std::unexpected<SharedModError>
{
    return error(
        SharedModErrorCode::Store,
        std::string{path} + ": " + cause.detail);
}

auto validate_roots(
    const fs::path& shared_runtime_directory,
    const fs::path& ownership_directory)
    -> std::expected<void, SharedModError>
{
    if (!shared_runtime_directory.is_absolute() ||
        shared_runtime_directory.lexically_normal() !=
            shared_runtime_directory ||
        !ownership_directory.is_absolute() ||
        ownership_directory.lexically_normal() !=
            ownership_directory)
    {
        return error(
            SharedModErrorCode::Path,
            "Shared runtime and ownership roots must be absolute "
            "normalized paths.");
    }
    auto runtime = detail::require_plain_directory_tree(
        shared_runtime_directory);
    if (!runtime)
    {
        return error(
            SharedModErrorCode::Path,
            "Shared runtime root: " + runtime.error().detail);
    }
    auto mods = detail::require_plain_directory_tree(
        shared_runtime_directory / "Mods");
    if (!mods)
    {
        return error(
            SharedModErrorCode::Path,
            "Shared Mods root: " + mods.error().detail);
    }
    auto ownership = detail::inspect_plain_directory_tree(
        ownership_directory);
    if (!ownership)
    {
        return error(
            SharedModErrorCode::Path,
            "Shared ownership root: " +
                ownership.error().detail);
    }
    return {};
}

auto target_path(
    const fs::path& shared_runtime_directory,
    std::string_view manifest_path) -> fs::path
{
    return shared_runtime_directory /
           fs::path{std::string{manifest_path}};
}

auto receipt_path(
    const fs::path& ownership_scope,
    std::string_view manifest_path) -> fs::path
{
    const auto suffix = manifest_path.substr(ModPrefix.size());
    auto result =
        ownership_scope /
        fs::path{std::string{suffix}};
    result += ".owner.json";
    return result;
}

auto ownership_scope(
    const fs::path& ownership_directory,
    const fs::path& shared_runtime_directory)
    -> std::expected<fs::path, SharedModError>
{
    const auto native = shared_runtime_directory.native();
    const auto digest = sha256_bytes(
        std::as_bytes(std::span{native}));
    if (!digest)
    {
        return error(
            SharedModErrorCode::Path,
            "Could not derive the shared runtime ownership scope.");
    }
    return ownership_directory / "shared-mod" /
           sha256_hex(*digest);
}

auto make_store(
    const fs::path& shared_runtime_directory,
    const fs::path& ownership_scope,
    const SharedModFileMaterial& file)
    -> Win32OwnedFileStore
{
    return Win32OwnedFileStore{
        target_path(
            shared_runtime_directory,
            file.expectation.file.path),
        receipt_path(
            ownership_scope,
            file.expectation.file.path),
        file.expectation.file.path,
        FileRole::Mod,
    };
}

auto is_installable(ArtifactState state) -> bool
{
    return state == ArtifactState::Missing ||
           state == ArtifactState::ExactOwned ||
           state == ArtifactState::OwnedPrevious;
}

auto read_verified_payload(
    const ManifestFile& file,
    RuntimePayloadSource& payload_source)
    -> std::expected<std::vector<std::byte>, SharedModError>
{
    auto bytes = payload_source.read_file(file.path);
    if (!bytes)
    {
        return error(
            SharedModErrorCode::Payload,
            "The payload source could not provide " +
                file.path + ": " + bytes.error().detail);
    }
    const auto digest = sha256_bytes(*bytes);
    if (!digest || bytes->size() != file.size ||
        *digest != file.sha256)
    {
        return error(
            SharedModErrorCode::Payload,
            "Payload does not match the manifest: " + file.path);
    }
    return std::move(*bytes);
}

auto validate_compatibility_files(
    const fs::path& shared_runtime_directory,
    const SharedModMaterial& material)
    -> std::expected<void, SharedModError>
{
    for (const auto& expected : material.compatibility_files)
    {
        auto current = detail::measure_plain_file(
            target_path(
                shared_runtime_directory,
                expected.path));
        if (!current)
        {
            return error(
                SharedModErrorCode::Path,
                expected.path + ": " + current.error().detail);
        }
        if (!*current || (**current).size != expected.size ||
            (**current).sha256 != expected.sha256)
        {
            return error(
                SharedModErrorCode::Plan,
                "The compatible shared runtime changed after "
                "planning: " +
                    expected.path);
        }
    }
    return {};
}
} // namespace

auto build_shared_mod_material(
    const PayloadManifest& manifest,
    const Sha256Digest& manifest_sha256,
    RuntimePayloadSource& payload_source)
    -> std::expected<SharedModMaterial, SharedModError>
{
    auto material = SharedModMaterial{};
    auto has_main = false;
    auto has_enabled = false;
    auto has_runtime = false;
    for (const auto& file : manifest.files)
    {
        const auto in_mod_directory =
            file.path.starts_with(ModPrefix);
        if (file.role == FileRole::Mod && !in_mod_directory)
        {
            return error(
                SharedModErrorCode::Manifest,
                "Mod payload is outside "
                "Mods/MecchaCamouflage: " +
                    file.path);
        }
        if (in_mod_directory && file.role != FileRole::Mod)
        {
            return error(
                SharedModErrorCode::Manifest,
                "Every packaged file in Mods/MecchaCamouflage "
                "must use the mod role: " +
                    file.path);
        }
        if (!in_mod_directory)
        {
            if (file.role != FileRole::Runtime &&
                file.role != FileRole::Config)
            {
                continue;
            }
            auto bytes = read_verified_payload(
                file,
                payload_source);
            if (!bytes)
            {
                return std::unexpected(bytes.error());
            }
            material.compatibility_files.push_back(file);
            if (file.role == FileRole::Runtime &&
                file.path == "UE4SS.dll")
            {
                has_runtime = true;
            }
            continue;
        }

        auto bytes = read_verified_payload(
            file,
            payload_source);
        if (!bytes)
        {
            return std::unexpected(bytes.error());
        }
        if (file.path == MainPath)
        {
            has_main = true;
        }
        if (file.path == EnabledPath)
        {
            if (!bytes->empty())
            {
                return error(
                    SharedModErrorCode::Manifest,
                    "The self-enabling enabled.txt must be empty.");
            }
            has_enabled = true;
        }
        material.files.push_back(SharedModFileMaterial{
            OwnedFileExpectation{
                manifest.product_version,
                manifest_sha256,
                file,
            },
            std::move(*bytes),
        });
    }
    if (!has_runtime || !has_main || !has_enabled)
    {
        return error(
            SharedModErrorCode::Manifest,
            "Shared mode requires canonical UE4SS.dll, "
            "dlls/main.dll, and empty enabled.txt entries.");
    }
    return material;
}

auto apply_shared_mod_plan(
    SharedModAction action,
    const fs::path& shared_runtime_directory,
    const fs::path& ownership_directory,
    const SharedModMaterial& material)
    -> std::expected<SharedModApplyResult, SharedModError>
{
    auto roots = validate_roots(
        shared_runtime_directory,
        ownership_directory);
    if (!roots)
    {
        return std::unexpected(roots.error());
    }
    auto compatible = validate_compatibility_files(
        shared_runtime_directory,
        material);
    if (!compatible)
    {
        return std::unexpected(compatible.error());
    }
    if (material.files.empty())
    {
        return error(
            SharedModErrorCode::Manifest,
            "The shared mod material is empty.");
    }
    auto scope = ownership_scope(
        ownership_directory,
        shared_runtime_directory);
    if (!scope)
    {
        return std::unexpected(scope.error());
    }

    std::vector<ArtifactState> states{};
    states.reserve(material.files.size());
    for (const auto& file : material.files)
    {
        auto store = make_store(
            shared_runtime_directory,
            *scope,
            file);
        auto recovered = store.recover();
        if (!recovered)
        {
            return store_error(
                file.expectation.file.path,
                recovered.error());
        }
        auto state = store.observe(file.expectation);
        if (!state)
        {
            return store_error(
                file.expectation.file.path,
                state.error());
        }
        const auto valid =
            action == SharedModAction::Reuse
                ? *state == ArtifactState::ExactOwned ||
                      *state == ArtifactState::ExactUnowned
                : is_installable(*state);
        if (!valid)
        {
            return error(
                SharedModErrorCode::Plan,
                "The shared mod filesystem changed after "
                "planning; no mod payload was installed: " +
                    file.expectation.file.path);
        }
        states.push_back(*state);
    }
    compatible = validate_compatibility_files(
        shared_runtime_directory,
        material);
    if (!compatible)
    {
        return std::unexpected(compatible.error());
    }

    auto result = SharedModApplyResult{};
    for (std::size_t index = 0;
         index < material.files.size();
         ++index)
    {
        const auto& file = material.files[index];
        const auto state = states[index];
        if (state == ArtifactState::ExactOwned)
        {
            ++result.reused_owned;
            continue;
        }
        if (state == ArtifactState::ExactUnowned)
        {
            ++result.reused_unowned;
            continue;
        }

        auto store = make_store(
            shared_runtime_directory,
            *scope,
            file);
        auto installed = store.install(
            file.expectation,
            file.bytes);
        if (!installed)
        {
            return store_error(
                file.expectation.file.path,
                installed.error());
        }
        if (*installed == OwnedFileInstallResult::Created)
        {
            ++result.created;
        }
        else if (*installed ==
                 OwnedFileInstallResult::Replaced)
        {
            ++result.replaced;
        }
        else
        {
            ++result.reused_owned;
        }
    }
    return result;
}

auto apply_shared_mod_removal(
    const RemovalPlan& plan,
    const fs::path& shared_runtime_directory,
    const fs::path& ownership_directory,
    const SharedModMaterial& material)
    -> std::expected<SharedModRemovalResult, SharedModError>
{
    if (plan.runtime_cache != RemovalAction::None ||
        plan.proxy != RemovalAction::None ||
        plan.override_file != RemovalAction::None ||
        plan.elevated_loader)
    {
        return error(
            SharedModErrorCode::Plan,
            "The shared mod remover received non-mod actions.");
    }
    auto roots = validate_roots(
        shared_runtime_directory,
        ownership_directory);
    if (!roots)
    {
        return std::unexpected(roots.error());
    }
    if (material.files.empty())
    {
        return error(
            SharedModErrorCode::Manifest,
            "The shared mod material is empty.");
    }
    auto scope = ownership_scope(
        ownership_directory,
        shared_runtime_directory);
    if (!scope)
    {
        return std::unexpected(scope.error());
    }

    std::vector<bool> removable{};
    removable.reserve(material.files.size());
    for (const auto& file : material.files)
    {
        auto store = make_store(
            shared_runtime_directory,
            *scope,
            file);
        auto recovered = store.recover();
        if (!recovered)
        {
            return store_error(
                file.expectation.file.path,
                recovered.error());
        }
        auto can_remove = store.removable();
        if (!can_remove)
        {
            return store_error(
                file.expectation.file.path,
                can_remove.error());
        }
        removable.push_back(*can_remove);
    }

    const auto requested =
        plan.mod == RemovalAction::RemoveOwned;
    const auto all_removable =
        std::ranges::all_of(removable, [](bool value) {
            return value;
        });
    const auto none_removable =
        std::ranges::none_of(removable, [](bool value) {
            return value;
        });
    if ((requested && !all_removable) ||
        (!requested && !none_removable))
    {
        return error(
            SharedModErrorCode::Plan,
            "The shared mod filesystem changed after removal "
            "planning; nothing was removed.");
    }
    if (!requested)
    {
        return SharedModRemovalResult{};
    }

    auto result = SharedModRemovalResult{};
    for (auto iterator = material.files.rbegin();
         iterator != material.files.rend();
         ++iterator)
    {
        auto store = make_store(
            shared_runtime_directory,
            *scope,
            *iterator);
        auto removed = store.remove_owned();
        if (!removed)
        {
            return store_error(
                iterator->expectation.file.path,
                removed.error());
        }
        if (*removed)
        {
            ++result.removed;
        }
    }
    return result;
}
} // namespace meccha::launcher
