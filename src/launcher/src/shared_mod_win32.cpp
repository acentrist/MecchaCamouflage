#include <meccha/launcher/shared_mod.hpp>

#include <meccha/launcher/hash.hpp>

#include "owned_file_win32_io.hpp"
#include "shared_mod_ledger.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_set>
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
constexpr std::string_view LedgerManifestPath{
    "shared-mod/installed-files.json"};
constexpr std::string_view LedgerFileName{
    "installed-files.json"};
constexpr std::string_view LedgerReceiptName{
    "installed-files.owner.json"};
constexpr std::size_t MaximumLedgerBytes = 4U * 1024U * 1024U;
constexpr std::size_t MaximumOwnedTreeEntries = 8192U;

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

auto same_windows_path(
    const fs::path& left,
    const fs::path& right) -> bool
{
    auto left_native = left.native();
    auto right_native = right.native();
    std::ranges::replace(left_native, L'/', L'\\');
    std::ranges::replace(right_native, L'/', L'\\');
    if (left_native.size() >
            static_cast<std::size_t>(
                std::numeric_limits<int>::max()) ||
        right_native.size() >
            static_cast<std::size_t>(
                std::numeric_limits<int>::max()))
    {
        return false;
    }
    return CompareStringOrdinal(
               left_native.data(),
               static_cast<int>(left_native.size()),
               right_native.data(),
               static_cast<int>(right_native.size()),
               TRUE) == CSTR_EQUAL;
}

auto contains_windows_path(
    std::span<const fs::path> paths,
    const fs::path& candidate) -> bool
{
    return std::ranges::any_of(
        paths,
        [&candidate](const fs::path& path) {
            return same_windows_path(path, candidate);
        });
}

auto expected_directories(
    const fs::path& root,
    std::span<const fs::path> files)
    -> std::expected<std::vector<fs::path>, SharedModError>
{
    std::vector<fs::path> result{root};
    for (const auto& file : files)
    {
        auto directory = file.parent_path();
        while (!same_windows_path(directory, root))
        {
            if (directory.empty() ||
                same_windows_path(
                    directory,
                    directory.parent_path()))
            {
                return error(
                    SharedModErrorCode::Path,
                    "An owned file is outside its expected tree: " +
                        file.string() + " (root " +
                        root.string() + ", reached " +
                        directory.string() + ").");
            }
            if (!contains_windows_path(result, directory))
            {
                result.push_back(directory);
            }
            directory = directory.parent_path();
        }
    }
    return result;
}

auto preflight_known_tree(
    const fs::path& root,
    std::span<const fs::path> expected_files,
    std::string_view label)
    -> std::expected<
        detail::PlainDirectoryTreeSnapshot,
        SharedModError>
{
    auto expected_dirs = expected_directories(
        root,
        expected_files);
    if (!expected_dirs)
    {
        return std::unexpected(expected_dirs.error());
    }
    auto snapshot = detail::snapshot_plain_directory_tree(
        root,
        MaximumOwnedTreeEntries);
    if (!snapshot)
    {
        return store_error(label, snapshot.error());
    }
    for (const auto& file : snapshot->files)
    {
        if (!contains_windows_path(expected_files, file))
        {
            return error(
                SharedModErrorCode::Plan,
                std::string{label} +
                    " contains an unknown file; nothing was "
                    "removed: " +
                    file.string());
        }
    }
    for (const auto& directory : snapshot->directories)
    {
        if (!contains_windows_path(*expected_dirs, directory))
        {
            return error(
                SharedModErrorCode::Plan,
                std::string{label} +
                    " contains an unknown directory; nothing was "
                    "removed: " +
                    directory.string());
        }
    }
    return std::move(*snapshot);
}

auto remove_snapshot_directories(
    const detail::PlainDirectoryTreeSnapshot& snapshot,
    std::string_view label)
    -> std::expected<void, SharedModError>
{
    for (const auto& directory : snapshot.directories)
    {
        auto removed =
            detail::delete_plain_empty_directory(directory);
        if (!removed)
        {
            return store_error(label, removed.error());
        }
        if (!*removed)
        {
            return error(
                SharedModErrorCode::Plan,
                std::string{label} +
                    " acquired unknown content during removal.");
        }
    }
    return {};
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

auto compatibility_state(
    const fs::path& shared_runtime_directory,
    const SharedModMaterial& material)
    -> std::expected<SettingsState, SharedModError>
{
    auto has_runtime = false;
    for (const auto& expected : material.compatibility_files)
    {
        if ((expected.role != FileRole::Runtime &&
             expected.role != FileRole::Config) ||
            !is_canonical_payload_path(expected.path))
        {
            return error(
                SharedModErrorCode::Manifest,
                "Shared runtime compatibility material is invalid.");
        }
        if (expected.role == FileRole::Runtime &&
            expected.path == "UE4SS.dll")
        {
            has_runtime = true;
        }
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
            return SettingsState::Incompatible;
        }
    }
    if (!has_runtime)
    {
        return error(
            SharedModErrorCode::Manifest,
            "Shared runtime compatibility material has no "
            "canonical UE4SS.dll.");
    }
    return SettingsState::Compatible;
}

auto validate_compatibility_files(
    const fs::path& shared_runtime_directory,
    const SharedModMaterial& material)
    -> std::expected<void, SharedModError>
{
    const auto state = compatibility_state(
        shared_runtime_directory,
        material);
    if (!state)
    {
        return std::unexpected(state.error());
    }
    if (*state != SettingsState::Compatible)
    {
        return error(
            SharedModErrorCode::Plan,
            "The compatible shared runtime changed after planning.");
    }
    return {};
}

auto make_ledger_store(const fs::path& ownership_scope)
    -> Win32OwnedFileStore
{
    return Win32OwnedFileStore{
        ownership_scope / LedgerFileName,
        ownership_scope / LedgerReceiptName,
        std::string{LedgerManifestPath},
        FileRole::Config,
    };
}

struct PreparedLedger
{
    OwnedFileExpectation expectation{};
    std::vector<std::byte> bytes{};
};

auto prepare_ledger(detail::SharedModLedger ledger)
    -> std::expected<PreparedLedger, SharedModError>
{
    auto serialized =
        detail::serialize_shared_mod_ledger(ledger);
    if (!serialized)
    {
        return std::unexpected(serialized.error());
    }
    const auto view =
        std::as_bytes(std::span{*serialized});
    std::vector<std::byte> bytes{
        view.begin(),
        view.end()};
    const auto digest = sha256_bytes(bytes);
    if (!digest)
    {
        return error(
            SharedModErrorCode::Payload,
            "Could not hash the shared mod ledger.");
    }
    return PreparedLedger{
        OwnedFileExpectation{
            ledger.product_version,
            ledger.manifest_sha256,
            ManifestFile{
                std::string{LedgerManifestPath},
                FileRole::Config,
                bytes.size(),
                *digest,
            },
        },
        std::move(bytes),
    };
}

auto final_ledger(const SharedModMaterial& material)
    -> std::expected<detail::SharedModLedger, SharedModError>
{
    if (material.files.empty())
    {
        return error(
            SharedModErrorCode::Manifest,
            "The shared mod material is empty.");
    }
    const auto& first = material.files.front().expectation;
    auto ledger = detail::SharedModLedger{
        first.product_version,
        first.manifest_sha256,
        {},
    };
    ledger.files.reserve(material.files.size());
    for (const auto& file : material.files)
    {
        if (file.expectation.product_version !=
                ledger.product_version ||
            file.expectation.manifest_sha256 !=
                ledger.manifest_sha256 ||
            file.expectation.file.role != FileRole::Mod)
        {
            return error(
                SharedModErrorCode::Manifest,
                "Shared mod material has inconsistent ownership "
                "identity.");
        }
        ledger.files.push_back(OwnershipRecord{
            file.expectation.product_version,
            file.expectation.manifest_sha256,
            file.expectation.file,
        });
    }
    return ledger;
}

struct LoadedLedger
{
    detail::SharedModLedger ledger{};
};

auto load_ledger(
    const fs::path& ownership_scope,
    bool recover_first)
    -> std::expected<std::optional<LoadedLedger>, SharedModError>
{
    auto store = make_ledger_store(ownership_scope);
    if (recover_first)
    {
        auto recovered = store.recover();
        if (!recovered)
        {
            return store_error(
                LedgerManifestPath,
                recovered.error());
        }
    }
    auto bytes = detail::read_plain_file_bytes(
        ownership_scope / LedgerFileName,
        MaximumLedgerBytes);
    if (!bytes)
    {
        return store_error(
            LedgerManifestPath,
            bytes.error());
    }
    if (!*bytes)
    {
        return std::nullopt;
    }
    const auto text = std::string_view{
        reinterpret_cast<const char*>((**bytes).data()),
        (**bytes).size()};
    auto ledger = detail::parse_shared_mod_ledger(text);
    if (!ledger)
    {
        return std::unexpected(ledger.error());
    }
    const auto digest = sha256_bytes(**bytes);
    if (!digest)
    {
        return error(
            SharedModErrorCode::Payload,
            "Could not hash the installed shared mod ledger.");
    }
    auto expectation = OwnedFileExpectation{
        ledger->product_version,
        ledger->manifest_sha256,
        ManifestFile{
            std::string{LedgerManifestPath},
            FileRole::Config,
            (**bytes).size(),
            *digest,
        },
    };
    auto state = store.observe(expectation);
    if (!state)
    {
        return store_error(
            LedgerManifestPath,
            state.error());
    }
    if (*state != ArtifactState::ExactOwned)
    {
        return error(
            SharedModErrorCode::Plan,
            "The installed shared mod ledger is not exactly owned.");
    }
    return LoadedLedger{
        std::move(*ledger),
    };
}

auto old_file_material(
    const OwnershipRecord& record) -> SharedModFileMaterial
{
    return SharedModFileMaterial{
        OwnedFileExpectation{
            record.product_version,
            record.manifest_sha256,
            record.file,
        },
        {},
    };
}

auto stale_files(
    const std::optional<LoadedLedger>& installed,
    const SharedModMaterial& current)
    -> std::vector<SharedModFileMaterial>
{
    if (!installed)
    {
        return {};
    }
    std::unordered_set<std::string> current_paths{};
    current_paths.reserve(current.files.size());
    for (const auto& file : current.files)
    {
        current_paths.insert(canonical_payload_path_key(
            file.expectation.file.path));
    }

    std::vector<SharedModFileMaterial> result{};
    for (const auto& record : installed->ledger.files)
    {
        if (!current_paths.contains(
                canonical_payload_path_key(record.file.path)))
        {
            result.push_back(old_file_material(record));
        }
    }
    return result;
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

auto observe_shared_runtime_settings(
    const fs::path& shared_runtime_directory,
    const SharedModMaterial& material)
    -> std::expected<SettingsState, SharedModError>
{
    if (!shared_runtime_directory.is_absolute() ||
        shared_runtime_directory.lexically_normal() !=
            shared_runtime_directory ||
        shared_runtime_directory.filename().empty())
    {
        return error(
            SharedModErrorCode::Path,
            "Shared runtime root must be an absolute normalized "
            "directory path.");
    }
    const auto tree = detail::require_plain_directory_tree(
        shared_runtime_directory);
    if (!tree)
    {
        return error(
            SharedModErrorCode::Path,
            "Shared runtime root: " + tree.error().detail);
    }
    return compatibility_state(
        shared_runtime_directory,
        material);
}

auto observe_shared_mod(
    const fs::path& shared_runtime_directory,
    const fs::path& ownership_directory,
    const SharedModMaterial& material)
    -> std::expected<ArtifactState, SharedModError>
{
    const auto roots = validate_roots(
        shared_runtime_directory,
        ownership_directory);
    if (!roots)
    {
        return std::unexpected(roots.error());
    }
    const auto compatible = validate_compatibility_files(
        shared_runtime_directory,
        material);
    if (!compatible)
    {
        return std::unexpected(compatible.error());
    }
    const auto current_ledger = final_ledger(material);
    if (!current_ledger)
    {
        return std::unexpected(current_ledger.error());
    }
    const auto scope = ownership_scope(
        ownership_directory,
        shared_runtime_directory);
    if (!scope)
    {
        return std::unexpected(scope.error());
    }
    const auto installed_ledger = load_ledger(*scope, false);
    if (!installed_ledger)
    {
        return ArtifactState::Conflict;
    }

    auto states = std::vector<ArtifactState>{};
    states.reserve(material.files.size());
    for (const auto& file : material.files)
    {
        auto store = make_store(
            shared_runtime_directory,
            *scope,
            file);
        const auto state = store.observe(file.expectation);
        if (!state)
        {
            if (state.error().code ==
                OwnedFileStoreErrorCode::Conflict)
            {
                return ArtifactState::Conflict;
            }
            return store_error(
                file.expectation.file.path,
                state.error());
        }
        states.push_back(*state);
    }

    if (!*installed_ledger)
    {
        if (std::ranges::all_of(
                states,
                [](ArtifactState state) {
                    return state == ArtifactState::Missing;
                }))
        {
            return ArtifactState::Missing;
        }
        if (std::ranges::all_of(
                states,
                [](ArtifactState state) {
                    return state == ArtifactState::ExactUnowned;
                }))
        {
            return ArtifactState::ExactUnowned;
        }
        return ArtifactState::Conflict;
    }

    if (std::ranges::any_of(
            states,
            [](ArtifactState state) {
                return state == ArtifactState::Conflict ||
                       state == ArtifactState::ExactUnowned;
            }))
    {
        return ArtifactState::Conflict;
    }
    const auto stale = stale_files(
        *installed_ledger,
        material);
    for (const auto& file : stale)
    {
        auto store = make_store(
            shared_runtime_directory,
            *scope,
            file);
        const auto removable = store.removable();
        if (!removable)
        {
            if (removable.error().code ==
                OwnedFileStoreErrorCode::Conflict)
            {
                return ArtifactState::Conflict;
            }
            return store_error(
                file.expectation.file.path,
                removable.error());
        }
        if (*removable)
        {
            continue;
        }
        const auto state = store.observe(file.expectation);
        if (!state)
        {
            if (state.error().code ==
                OwnedFileStoreErrorCode::Conflict)
            {
                return ArtifactState::Conflict;
            }
            return store_error(
                file.expectation.file.path,
                state.error());
        }
        if (*state != ArtifactState::Missing)
        {
            return ArtifactState::Conflict;
        }
    }

    const auto all_exact = std::ranges::all_of(
        states,
        [](ArtifactState state) {
            return state == ArtifactState::ExactOwned;
        });
    if (all_exact && stale.empty() &&
        (**installed_ledger).ledger == *current_ledger)
    {
        return ArtifactState::ExactOwned;
    }
    const auto safely_updatable = std::ranges::all_of(
        states,
        [](ArtifactState state) {
            return state == ArtifactState::Missing ||
                   state == ArtifactState::ExactOwned ||
                   state == ArtifactState::OwnedPrevious;
        });
    return safely_updatable
               ? ArtifactState::OwnedPrevious
               : ArtifactState::Conflict;
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
    auto current_ledger = final_ledger(material);
    if (!current_ledger)
    {
        return std::unexpected(current_ledger.error());
    }
    auto final_prepared = prepare_ledger(*current_ledger);
    if (!final_prepared)
    {
        return std::unexpected(final_prepared.error());
    }
    auto installed_ledger = load_ledger(*scope, true);
    if (!installed_ledger)
    {
        return std::unexpected(installed_ledger.error());
    }
    const auto previous_ledger =
        *installed_ledger
            ? std::optional{(**installed_ledger).ledger}
            : std::nullopt;
    auto transition_prepared = prepare_ledger(
        detail::make_shared_mod_transition_ledger(
            previous_ledger,
            *current_ledger));
    if (!transition_prepared)
    {
        return std::unexpected(transition_prepared.error());
    }
    auto ledger_store = make_ledger_store(*scope);
    const auto& planned_ledger =
        action == SharedModAction::Install
            ? *transition_prepared
            : *final_prepared;
    auto ledger_state = ledger_store.observe(
        planned_ledger.expectation);
    if (!ledger_state)
    {
        return store_error(
            LedgerManifestPath,
            ledger_state.error());
    }
    const auto ledger_valid =
        action == SharedModAction::Reuse
            ? (*installed_ledger
                   ? *ledger_state == ArtifactState::ExactOwned
                   : *ledger_state == ArtifactState::Missing)
            : is_installable(*ledger_state);
    if (!ledger_valid)
    {
        return error(
            SharedModErrorCode::Plan,
            "The shared mod ledger changed after planning.");
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
        const auto valid = action == SharedModAction::Reuse
                               ? *state ==
                                     (*installed_ledger
                                          ? ArtifactState::ExactOwned
                                          : ArtifactState::ExactUnowned)
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
    const auto stale = stale_files(
        *installed_ledger,
        material);
    std::vector<bool> stale_removable{};
    stale_removable.reserve(stale.size());
    if (action == SharedModAction::Install)
    {
        for (const auto& file : stale)
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
            auto removable = store.removable();
            if (!removable)
            {
                return store_error(
                    file.expectation.file.path,
                    removable.error());
            }
            if (!*removable)
            {
                auto state = store.observe(file.expectation);
                if (!state)
                {
                    return store_error(
                        file.expectation.file.path,
                        state.error());
                }
                if (*state != ArtifactState::Missing)
                {
                    return error(
                        SharedModErrorCode::Plan,
                        "A stale shared mod file is no longer "
                        "safely removable: " +
                            file.expectation.file.path);
                }
            }
            stale_removable.push_back(*removable);
        }
    }
    compatible = validate_compatibility_files(
        shared_runtime_directory,
        material);
    if (!compatible)
    {
        return std::unexpected(compatible.error());
    }
    if (action == SharedModAction::Install)
    {
        auto transition_installed = ledger_store.install(
            transition_prepared->expectation,
            transition_prepared->bytes);
        if (!transition_installed)
        {
            return store_error(
                LedgerManifestPath,
                transition_installed.error());
        }
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
    for (std::size_t reverse_index = stale.size();
         reverse_index > 0;
         --reverse_index)
    {
        const auto index = reverse_index - 1U;
        if (!stale_removable[index])
        {
            continue;
        }
        auto store = make_store(
            shared_runtime_directory,
            *scope,
            stale[index]);
        auto removed = store.remove_owned();
        if (!removed)
        {
            return store_error(
                stale[index].expectation.file.path,
                removed.error());
        }
        if (*removed)
        {
            ++result.removed_stale;
        }
    }
    if (action == SharedModAction::Install)
    {
        auto ledger_installed = ledger_store.install(
            final_prepared->expectation,
            final_prepared->bytes);
        if (!ledger_installed)
        {
            return store_error(
                LedgerManifestPath,
                ledger_installed.error());
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
    auto installed_ledger = load_ledger(*scope, true);
    if (!installed_ledger)
    {
        return std::unexpected(installed_ledger.error());
    }
    std::vector<SharedModFileMaterial> tracked = material.files;
    std::unordered_set<std::string> tracked_paths{};
    tracked_paths.reserve(
        material.files.size() +
        (*installed_ledger
             ? (**installed_ledger).ledger.files.size()
             : 0U));
    for (const auto& file : material.files)
    {
        tracked_paths.insert(canonical_payload_path_key(
            file.expectation.file.path));
    }
    if (*installed_ledger)
    {
        for (const auto& record :
             (**installed_ledger).ledger.files)
        {
            if (tracked_paths.insert(
                    canonical_payload_path_key(
                        record.file.path))
                    .second)
            {
                tracked.push_back(
                    old_file_material(record));
            }
        }
    }

    const auto requested =
        plan.mod == RemovalAction::RemoveOwned;
    std::vector<bool> removable{};
    removable.reserve(tracked.size());
    for (const auto& file : tracked)
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
        if (!*can_remove)
        {
            auto state = store.observe(file.expectation);
            if (!state)
            {
                return store_error(
                    file.expectation.file.path,
                    state.error());
            }
            const auto safely_absent =
                *state == ArtifactState::Missing;
            const auto safely_unowned =
                !*installed_ledger &&
                *state == ArtifactState::ExactUnowned;
            if (!safely_absent &&
                (!safely_unowned || requested))
            {
                return error(
                    SharedModErrorCode::Plan,
                    "A shared mod target is not safely removable: " +
                        file.expectation.file.path);
            }
        }
        removable.push_back(*can_remove);
    }

    auto ledger_store = make_ledger_store(*scope);
    auto ledger_removable = ledger_store.removable();
    if (!ledger_removable)
    {
        return store_error(
            LedgerManifestPath,
            ledger_removable.error());
    }
    const auto any_removable =
        std::ranges::any_of(removable, [](bool value) {
            return value;
        });
    if ((!requested &&
         (any_removable || *ledger_removable)) ||
        (requested && *installed_ledger &&
         !*ledger_removable))
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

    std::vector<fs::path> expected_mod_files{};
    std::vector<fs::path> expected_ownership_files{};
    expected_mod_files.reserve(tracked.size());
    expected_ownership_files.reserve(tracked.size() + 2U);
    for (const auto& file : tracked)
    {
        expected_mod_files.push_back(target_path(
            shared_runtime_directory,
            file.expectation.file.path));
        expected_ownership_files.push_back(receipt_path(
            *scope,
            file.expectation.file.path));
    }
    expected_ownership_files.push_back(*scope / LedgerFileName);
    expected_ownership_files.push_back(*scope / LedgerReceiptName);

    auto mod_snapshot = preflight_known_tree(
        shared_runtime_directory / "Mods" /
            "MecchaCamouflage",
        expected_mod_files,
        "Shared mod tree");
    if (!mod_snapshot)
    {
        return std::unexpected(mod_snapshot.error());
    }
    auto ownership_snapshot = preflight_known_tree(
        *scope,
        expected_ownership_files,
        "Shared mod ownership tree");
    if (!ownership_snapshot)
    {
        return std::unexpected(ownership_snapshot.error());
    }

    auto result = SharedModRemovalResult{};
    for (std::size_t reverse_index = tracked.size();
         reverse_index > 0;
         --reverse_index)
    {
        const auto index = reverse_index - 1U;
        if (!removable[index])
        {
            continue;
        }
        auto store = make_store(
            shared_runtime_directory,
            *scope,
            tracked[index]);
        auto removed = store.remove_owned();
        if (!removed)
        {
            return store_error(
                tracked[index].expectation.file.path,
                removed.error());
        }
        if (*removed)
        {
            ++result.removed;
        }
    }
    if (*installed_ledger)
    {
        auto removed = ledger_store.remove_owned();
        if (!removed)
        {
            return store_error(
                LedgerManifestPath,
                removed.error());
        }
        if (!*removed)
        {
            return error(
                SharedModErrorCode::Plan,
                "The shared mod ledger changed during removal.");
        }
    }
    auto mod_directories_removed = remove_snapshot_directories(
        *mod_snapshot,
        "Shared mod tree");
    if (!mod_directories_removed)
    {
        return std::unexpected(
            mod_directories_removed.error());
    }
    auto ownership_directories_removed =
        remove_snapshot_directories(
            *ownership_snapshot,
            "Shared mod ownership tree");
    if (!ownership_directories_removed)
    {
        return std::unexpected(
            ownership_directories_removed.error());
    }
    auto shared_ownership_parent_removed =
        detail::delete_plain_empty_directory(
            scope->parent_path());
    if (!shared_ownership_parent_removed)
    {
        return store_error(
            "Shared mod ownership parent",
            shared_ownership_parent_removed.error());
    }
    return result;
}
} // namespace meccha::launcher
