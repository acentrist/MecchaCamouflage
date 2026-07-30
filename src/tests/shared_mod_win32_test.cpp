#include <meccha/launcher/hash.hpp>
#include <meccha/launcher/shared_mod.hpp>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <winioctl.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace
{
using namespace meccha::launcher;
namespace fs = std::filesystem;

class TemporaryTree
{
public:
    TemporaryTree()
    {
        static auto sequence = std::uint64_t{};
        ++sequence;
        root = fs::temp_directory_path() /
               ("meccha-v2-shared-mod-" +
                std::to_string(GetCurrentProcessId()) + "-" +
                std::to_string(GetTickCount64()) + "-" +
                std::to_string(sequence));
        fs::create_directories(root / "shared" / "Mods");
        write_text(root / "shared" / "UE4SS.dll", "shared-runtime");
        write_text(root / "shared" / "Mods" / "mods.txt", "user-mods");
    }

    TemporaryTree(const TemporaryTree&) = delete;
    auto operator=(const TemporaryTree&) -> TemporaryTree& = delete;

    ~TemporaryTree()
    {
        std::error_code ignored{};
        fs::remove_all(root, ignored);
    }

    static auto write_text(
        const fs::path& path,
        std::string_view value) -> void
    {
        std::ofstream output{
            path,
            std::ios::binary | std::ios::trunc};
        output.write(
            value.data(),
            static_cast<std::streamsize>(value.size()));
    }

    fs::path root{};
};

struct JunctionBuffer
{
    DWORD reparse_tag{};
    WORD reparse_data_length{};
    WORD reserved{};
    WORD substitute_name_offset{};
    WORD substitute_name_length{};
    WORD print_name_offset{};
    WORD print_name_length{};
    WCHAR path_buffer[4096]{};
};

auto create_junction(
    const fs::path& link,
    const fs::path& target) -> bool
{
    if (!fs::create_directory(link))
    {
        return false;
    }
    const auto absolute_target =
        fs::absolute(target).lexically_normal().wstring();
    const auto substitute = L"\\??\\" + absolute_target;
    if (substitute.size() + absolute_target.size() + 2U >
        std::size(JunctionBuffer{}.path_buffer))
    {
        return false;
    }

    JunctionBuffer buffer{};
    buffer.reparse_tag = IO_REPARSE_TAG_MOUNT_POINT;
    buffer.substitute_name_length = static_cast<WORD>(
        substitute.size() * sizeof(WCHAR));
    buffer.print_name_offset = static_cast<WORD>(
        buffer.substitute_name_length + sizeof(WCHAR));
    buffer.print_name_length = static_cast<WORD>(
        absolute_target.size() * sizeof(WCHAR));
    buffer.reparse_data_length = static_cast<WORD>(
        8U + buffer.substitute_name_length + sizeof(WCHAR) +
        buffer.print_name_length + sizeof(WCHAR));
    std::ranges::copy(substitute, buffer.path_buffer);
    std::ranges::copy(
        absolute_target,
        buffer.path_buffer + substitute.size() + 1U);

    const auto handle = CreateFileW(
        link.c_str(),
        GENERIC_WRITE,
        0,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT |
            FILE_FLAG_BACKUP_SEMANTICS,
        nullptr);
    if (handle == INVALID_HANDLE_VALUE)
    {
        fs::remove(link);
        return false;
    }
    DWORD returned{};
    const auto input_size = 8U + buffer.reparse_data_length;
    const auto created = DeviceIoControl(
        handle,
        FSCTL_SET_REPARSE_POINT,
        &buffer,
        input_size,
        nullptr,
        0,
        &returned,
        nullptr);
    static_cast<void>(CloseHandle(handle));
    if (!created)
    {
        fs::remove(link);
        return false;
    }
    return true;
}

auto bytes(std::string_view value) -> std::vector<std::byte>
{
    const auto view = std::as_bytes(std::span{value});
    return {view.begin(), view.end()};
}

class MemoryPayload final : public RuntimePayloadSource
{
public:
    auto read_file(std::string_view relative_path)
        -> std::expected<
            std::vector<std::byte>,
            RuntimePayloadError> override
    {
        const auto found = files.find(std::string{relative_path});
        if (found == files.end())
        {
            return std::unexpected(
                RuntimePayloadError{"missing payload file"});
        }
        return found->second;
    }

    std::map<
        std::string,
        std::vector<std::byte>,
        std::less<>> files{};
};

struct Package
{
    PayloadManifest manifest{};
    Sha256Digest manifest_sha256{};
    MemoryPayload payload{};
};

auto add_mod_file(
    Package& package,
    std::string path,
    std::string_view content) -> void
{
    auto content_bytes = bytes(content);
    package.manifest.files.push_back(ManifestFile{
        path,
        FileRole::Mod,
        content_bytes.size(),
        sha256_bytes(content_bytes).value(),
    });
    package.payload.files.emplace(
        std::move(path),
        std::move(content_bytes));
}

auto make_package(
    std::string version = "2.0.0",
    std::string_view main_content = "main-v2",
    bool include_resource = true) -> Package
{
    Package package{};
    package.manifest.schema_version = 1;
    package.manifest.product_version = std::move(version);
    package.manifest.ue4ss_commit =
        "6c26f038751b3d96059d4a9148f5d093012d55ad";
    auto runtime_bytes = bytes("shared-runtime");
    package.manifest.files.push_back(ManifestFile{
        "UE4SS.dll",
        FileRole::Runtime,
        runtime_bytes.size(),
        sha256_bytes(runtime_bytes).value(),
    });
    package.payload.files.emplace(
        "UE4SS.dll",
        std::move(runtime_bytes));
    add_mod_file(
        package,
        "Mods/MecchaCamouflage/dlls/main.dll",
        main_content);
    add_mod_file(
        package,
        "Mods/MecchaCamouflage/enabled.txt",
        "");
    if (include_resource)
    {
        add_mod_file(
            package,
            "Mods/MecchaCamouflage/resources/catalog.bin",
            "catalog");
    }
    package.manifest_sha256 = sha256_bytes(
        std::as_bytes(
            std::span{package.manifest.product_version}))
                                  .value();
    return package;
}

auto write_payload_files(
    const fs::path& root,
    const Package& package) -> void
{
    for (const auto& [relative_path, content] :
         package.payload.files)
    {
        const auto target = root / fs::path{relative_path};
        fs::create_directories(target.parent_path());
        std::ofstream output{
            target,
            std::ios::binary | std::ios::trunc};
        output.write(
            reinterpret_cast<const char*>(content.data()),
            static_cast<std::streamsize>(content.size()));
    }
}

auto read_text(const fs::path& path) -> std::string
{
    std::ifstream input{path, std::ios::binary};
    return {
        std::istreambuf_iterator<char>{input},
        std::istreambuf_iterator<char>{}};
}

auto expect(bool condition, std::string_view message) -> bool
{
    if (!condition)
    {
        std::cerr << "FAIL shared_mod: " << message << '\n';
    }
    return condition;
}
} // namespace

auto main() -> int
{
    bool passed = true;
    auto package = make_package();
    const auto material = build_shared_mod_material(
        package.manifest,
        package.manifest_sha256,
        package.payload);
    passed &= expect(
        material && material->compatibility_files.size() == 1 &&
            material->files.size() == 3,
        "shared mod material did not include the complete mod");

    TemporaryTree lifecycle{};
    const auto missing = material
                             ? observe_shared_mod(
                                   lifecycle.root / "shared",
                                   lifecycle.root / "ownership",
                                   *material)
                             : std::expected<
                                   ArtifactState,
                                   SharedModError>{
                                   std::unexpected(SharedModError{
                                       SharedModErrorCode::Payload,
                                       "missing material",
                                   })};
    passed &= expect(
        missing && *missing == ArtifactState::Missing &&
            !fs::exists(lifecycle.root / "ownership"),
        "shared mod observation mutated or misclassified a clean tree");

    const auto installed = material
                               ? apply_shared_mod_plan(
                                     SharedModAction::Install,
                                     lifecycle.root / "shared",
                                     lifecycle.root / "ownership",
                                     *material)
                               : std::expected<
                                     SharedModApplyResult,
                                     SharedModError>{
                                     std::unexpected(SharedModError{
                                         SharedModErrorCode::Payload,
                                         "missing material"})};
    passed &= expect(
        installed && installed->created == 3 &&
            installed->replaced == 0 &&
            installed->reused_owned == 0 &&
            installed->reused_unowned == 0 &&
            read_text(
                lifecycle.root / "shared" / "Mods" /
                "MecchaCamouflage" / "dlls" / "main.dll") ==
                "main-v2" &&
            fs::exists(
                lifecycle.root / "shared" / "Mods" /
                "MecchaCamouflage" / "enabled.txt") &&
            read_text(
                lifecycle.root / "shared" / "Mods" /
                "mods.txt") == "user-mods",
        "fresh shared install changed unrelated content or missed files");
    const auto exact_owned = observe_shared_mod(
        lifecycle.root / "shared",
        lifecycle.root / "ownership",
        *material);
    passed &= expect(
        exact_owned &&
            *exact_owned == ArtifactState::ExactOwned,
        "shared mod observation missed the exact owned installation");

    const auto reused_owned = apply_shared_mod_plan(
        SharedModAction::Reuse,
        lifecycle.root / "shared",
        lifecycle.root / "ownership",
        *material);
    passed &= expect(
        reused_owned && reused_owned->reused_owned == 3 &&
            reused_owned->created == 0 &&
            read_text(
                lifecycle.root / "shared" / "Mods" /
                "mods.txt") == "user-mods",
        "exact owned shared mod reuse performed a mutation");

    TemporaryTree exact_unowned{};
    write_payload_files(
        exact_unowned.root / "shared",
        package);
    const auto exact_unowned_observation = observe_shared_mod(
        exact_unowned.root / "shared",
        exact_unowned.root / "ownership",
        *material);
    passed &= expect(
        exact_unowned_observation &&
            *exact_unowned_observation ==
                ArtifactState::ExactUnowned &&
            !fs::exists(exact_unowned.root / "ownership"),
        "shared mod observation claimed or misclassified exact unowned "
        "content");
    const auto reused_unowned = apply_shared_mod_plan(
        SharedModAction::Reuse,
        exact_unowned.root / "shared",
        exact_unowned.root / "ownership",
        *material);
    passed &= expect(
        reused_unowned &&
            reused_unowned->reused_unowned == 3 &&
            !fs::exists(exact_unowned.root / "ownership") &&
            read_text(
                exact_unowned.root / "shared" / "Mods" /
                "mods.txt") == "user-mods",
        "exact unowned shared mod was claimed or changed");

    TemporaryTree scoped_owner{};
    TemporaryTree other_exact_root{};
    const auto scoped_install = apply_shared_mod_plan(
        SharedModAction::Install,
        scoped_owner.root / "shared",
        scoped_owner.root / "ownership",
        *material);
    write_payload_files(
        other_exact_root.root / "shared",
        package);
    const auto refused_wrong_root_removal =
        apply_shared_mod_removal(
            RemovalPlan{
                RemovalAction::None,
                RemovalAction::None,
                RemovalAction::None,
                RemovalAction::RemoveOwned,
                false,
            },
            other_exact_root.root / "shared",
            scoped_owner.root / "ownership",
            *material);
    passed &= expect(
        scoped_install && !refused_wrong_root_removal &&
            refused_wrong_root_removal.error().code ==
                SharedModErrorCode::Plan &&
            fs::exists(
                other_exact_root.root / "shared" / "Mods" /
                "MecchaCamouflage" / "dlls" / "main.dll"),
        "ownership receipts were reusable against a different "
        "shared runtime root");

    TemporaryTree preflight{};
    const auto conflicting_main =
        preflight.root / "shared" / "Mods" /
        "MecchaCamouflage" / "dlls" / "main.dll";
    fs::create_directories(conflicting_main.parent_path());
    TemporaryTree::write_text(conflicting_main, "unknown");
    const auto refused_conflict = apply_shared_mod_plan(
        SharedModAction::Install,
        preflight.root / "shared",
        preflight.root / "ownership",
        *material);
    passed &= expect(
        !refused_conflict &&
            refused_conflict.error().code ==
                SharedModErrorCode::Plan &&
            read_text(conflicting_main) == "unknown" &&
            !fs::exists(
                preflight.root / "shared" / "Mods" /
                "MecchaCamouflage" / "enabled.txt") &&
            read_text(
                preflight.root / "shared" / "Mods" /
                "mods.txt") == "user-mods",
        "shared preflight conflict allowed a partial mutation");

    TemporaryTree stale_runtime{};
    TemporaryTree::write_text(
        stale_runtime.root / "shared" / "UE4SS.dll",
        "changed-runtime");
    const auto refused_stale_runtime = apply_shared_mod_plan(
        SharedModAction::Install,
        stale_runtime.root / "shared",
        stale_runtime.root / "ownership",
        *material);
    passed &= expect(
        !refused_stale_runtime &&
            refused_stale_runtime.error().code ==
                SharedModErrorCode::Plan &&
            !fs::exists(
                stale_runtime.root / "shared" / "Mods" /
                "MecchaCamouflage") &&
            read_text(
                stale_runtime.root / "shared" / "Mods" /
                "mods.txt") == "user-mods",
        "changed shared UE4SS runtime was not revalidated");

    auto updated_package = make_package("2.1.0", "main-v2.1");
    const auto updated_material = build_shared_mod_material(
        updated_package.manifest,
        updated_package.manifest_sha256,
        updated_package.payload);
    const auto pending_update = observe_shared_mod(
        lifecycle.root / "shared",
        lifecycle.root / "ownership",
        *updated_material);
    passed &= expect(
        pending_update &&
            *pending_update == ArtifactState::OwnedPrevious &&
            read_text(
                lifecycle.root / "shared" / "Mods" /
                "MecchaCamouflage" / "dlls" / "main.dll") ==
                "main-v2",
        "shared mod observation missed or mutated an owned update");
    const auto updated = apply_shared_mod_plan(
        SharedModAction::Install,
        lifecycle.root / "shared",
        lifecycle.root / "ownership",
        *updated_material);
    passed &= expect(
        updated && updated->replaced == 1 &&
            updated->reused_owned == 2 &&
            read_text(
                lifecycle.root / "shared" / "Mods" /
                "MecchaCamouflage" / "dlls" / "main.dll") ==
                "main-v2.1",
        "owned shared mod update was not applied safely");

    TemporaryTree stale_cleanup{};
    auto package_without_resource =
        make_package("2.1.0", "main-v2.1", false);
    const auto material_without_resource =
        build_shared_mod_material(
            package_without_resource.manifest,
            package_without_resource.manifest_sha256,
            package_without_resource.payload);
    const auto stale_fixture_install = apply_shared_mod_plan(
        SharedModAction::Install,
        stale_cleanup.root / "shared",
        stale_cleanup.root / "ownership",
        *material);
    const auto stale_cleanup_update = apply_shared_mod_plan(
        SharedModAction::Install,
        stale_cleanup.root / "shared",
        stale_cleanup.root / "ownership",
        *material_without_resource);
    passed &= expect(
        stale_fixture_install && stale_cleanup_update &&
            stale_cleanup_update->removed_stale == 1 &&
            !fs::exists(
                stale_cleanup.root / "shared" / "Mods" /
                "MecchaCamouflage" / "resources" /
                "catalog.bin") &&
            read_text(
                stale_cleanup.root / "shared" / "Mods" /
                "mods.txt") == "user-mods",
        "owned file removed from the new manifest was left behind");

    TemporaryTree stale_conflict{};
    const auto stale_conflict_install = apply_shared_mod_plan(
        SharedModAction::Install,
        stale_conflict.root / "shared",
        stale_conflict.root / "ownership",
        *material);
    TemporaryTree::write_text(
        stale_conflict.root / "shared" / "Mods" /
            "MecchaCamouflage" / "resources" / "catalog.bin",
        "user-change");
    const auto observed_stale_conflict = observe_shared_mod(
        stale_conflict.root / "shared",
        stale_conflict.root / "ownership",
        *material_without_resource);
    const auto refused_stale_conflict =
        apply_shared_mod_plan(
            SharedModAction::Install,
            stale_conflict.root / "shared",
            stale_conflict.root / "ownership",
            *material_without_resource);
    passed &= expect(
        stale_conflict_install && observed_stale_conflict &&
            *observed_stale_conflict == ArtifactState::Conflict &&
            !refused_stale_conflict &&
            refused_stale_conflict.error().code ==
                SharedModErrorCode::Store &&
            read_text(
                stale_conflict.root / "shared" / "Mods" /
                "MecchaCamouflage" / "dlls" / "main.dll") ==
                "main-v2" &&
            read_text(
                stale_conflict.root / "shared" / "Mods" /
                "MecchaCamouflage" / "resources" /
                "catalog.bin") == "user-change",
        "changed stale file allowed a partial shared mod update");

    TemporaryTree cross_version_removal{};
    const auto old_version_install = apply_shared_mod_plan(
        SharedModAction::Install,
        cross_version_removal.root / "shared",
        cross_version_removal.root / "ownership",
        *material);
    const auto old_version_removed_by_new =
        apply_shared_mod_removal(
            RemovalPlan{
                RemovalAction::None,
                RemovalAction::None,
                RemovalAction::None,
                RemovalAction::RemoveOwned,
                false,
            },
            cross_version_removal.root / "shared",
            cross_version_removal.root / "ownership",
            *material_without_resource);
    passed &= expect(
        old_version_install && old_version_removed_by_new &&
            old_version_removed_by_new->removed == 3 &&
            !fs::exists(
                cross_version_removal.root / "shared" / "Mods" /
                "MecchaCamouflage" / "resources" /
                "catalog.bin"),
        "new launcher material could not remove an older owned "
        "shared mod");

    TemporaryTree tampered_ledger{};
    const auto tampered_ledger_install =
        apply_shared_mod_plan(
            SharedModAction::Install,
            tampered_ledger.root / "shared",
            tampered_ledger.root / "ownership",
            *material);
    std::optional<fs::path> ledger_path{};
    for (const auto& entry : fs::recursive_directory_iterator{
             tampered_ledger.root / "ownership"})
    {
        if (entry.path().filename() == "installed-files.json")
        {
            ledger_path = entry.path();
            break;
        }
    }
    if (ledger_path)
    {
        TemporaryTree::write_text(*ledger_path, "{}");
    }
    const auto observed_tampered_ledger = observe_shared_mod(
        tampered_ledger.root / "shared",
        tampered_ledger.root / "ownership",
        *updated_material);
    const auto refused_tampered_ledger =
        apply_shared_mod_plan(
            SharedModAction::Install,
            tampered_ledger.root / "shared",
            tampered_ledger.root / "ownership",
            *updated_material);
    passed &= expect(
        tampered_ledger_install && ledger_path &&
            observed_tampered_ledger &&
            *observed_tampered_ledger ==
                ArtifactState::Conflict &&
            !refused_tampered_ledger &&
            refused_tampered_ledger.error().code ==
                SharedModErrorCode::Store &&
            read_text(
                tampered_ledger.root / "shared" / "Mods" /
                "MecchaCamouflage" / "dlls" / "main.dll") ==
                "main-v2",
        "tampered shared mod ledger allowed a payload mutation");

    const auto removed = apply_shared_mod_removal(
        RemovalPlan{
            RemovalAction::None,
            RemovalAction::None,
            RemovalAction::None,
            RemovalAction::RemoveOwned,
            false,
        },
        lifecycle.root / "shared",
        lifecycle.root / "ownership",
        *updated_material);
    passed &= expect(
        removed && removed->removed == 3 &&
            !fs::exists(
                lifecycle.root / "shared" / "Mods" /
                "MecchaCamouflage" / "dlls" / "main.dll") &&
            !fs::exists(
                lifecycle.root / "shared" / "Mods" /
                "MecchaCamouflage") &&
            fs::is_empty(lifecycle.root / "ownership") &&
            fs::exists(
                lifecycle.root / "shared" / "UE4SS.dll") &&
            read_text(
                lifecycle.root / "shared" / "Mods" /
                "mods.txt") == "user-mods",
        "shared removal touched unrelated runtime content");

    TemporaryTree unknown_mod_content{};
    const auto unknown_content_install =
        apply_shared_mod_plan(
            SharedModAction::Install,
            unknown_mod_content.root / "shared",
            unknown_mod_content.root / "ownership",
            *material);
    TemporaryTree::write_text(
        unknown_mod_content.root / "shared" / "Mods" /
            "MecchaCamouflage" / "user-file.txt",
        "preserve");
    const auto refused_unknown_content_removal =
        apply_shared_mod_removal(
            RemovalPlan{
                RemovalAction::None,
                RemovalAction::None,
                RemovalAction::None,
                RemovalAction::RemoveOwned,
                false,
            },
            unknown_mod_content.root / "shared",
            unknown_mod_content.root / "ownership",
            *material);
    passed &= expect(
        unknown_content_install &&
            !refused_unknown_content_removal &&
            refused_unknown_content_removal.error().code ==
                SharedModErrorCode::Plan &&
            read_text(
                unknown_mod_content.root / "shared" / "Mods" /
                "MecchaCamouflage" / "user-file.txt") ==
                "preserve" &&
            fs::exists(
                unknown_mod_content.root / "shared" / "Mods" /
                "MecchaCamouflage" / "dlls" / "main.dll"),
        "unknown mod content did not abort removal before mutation");

    TemporaryTree unknown_ownership_content{};
    const auto unknown_ownership_install =
        apply_shared_mod_plan(
            SharedModAction::Install,
            unknown_ownership_content.root / "shared",
            unknown_ownership_content.root / "ownership",
            *material);
    std::optional<fs::path> ownership_scope_path{};
    for (const auto& entry : fs::recursive_directory_iterator{
             unknown_ownership_content.root / "ownership"})
    {
        if (entry.path().filename() == "installed-files.json")
        {
            ownership_scope_path = entry.path().parent_path();
            break;
        }
    }
    if (ownership_scope_path)
    {
        TemporaryTree::write_text(
            *ownership_scope_path / "foreign.json",
            "preserve");
    }
    const auto refused_unknown_ownership_removal =
        apply_shared_mod_removal(
            RemovalPlan{
                RemovalAction::None,
                RemovalAction::None,
                RemovalAction::None,
                RemovalAction::RemoveOwned,
                false,
            },
            unknown_ownership_content.root / "shared",
            unknown_ownership_content.root / "ownership",
            *material);
    passed &= expect(
        unknown_ownership_install && ownership_scope_path &&
            !refused_unknown_ownership_removal &&
            refused_unknown_ownership_removal.error().code ==
                SharedModErrorCode::Plan &&
            read_text(*ownership_scope_path / "foreign.json") ==
                "preserve" &&
            fs::exists(
                unknown_ownership_content.root / "shared" / "Mods" /
                "MecchaCamouflage" / "dlls" / "main.dll"),
        "unknown ownership metadata did not abort removal before "
        "mutation");

    TemporaryTree changed_runtime_removal{};
    const auto changed_removal_install =
        apply_shared_mod_plan(
            SharedModAction::Install,
            changed_runtime_removal.root / "shared",
            changed_runtime_removal.root / "ownership",
            *material);
    TemporaryTree::write_text(
        changed_runtime_removal.root / "shared" / "UE4SS.dll",
        "runtime-updated-by-owner");
    const auto changed_runtime_removed =
        apply_shared_mod_removal(
            RemovalPlan{
                RemovalAction::None,
                RemovalAction::None,
                RemovalAction::None,
                RemovalAction::RemoveOwned,
                false,
            },
            changed_runtime_removal.root / "shared",
            changed_runtime_removal.root / "ownership",
            *material);
    passed &= expect(
        changed_removal_install && changed_runtime_removed &&
            changed_runtime_removed->removed == 3 &&
            read_text(
                changed_runtime_removal.root / "shared" /
                "UE4SS.dll") == "runtime-updated-by-owner" &&
            read_text(
                changed_runtime_removal.root / "shared" / "Mods" /
                "mods.txt") == "user-mods",
        "shared runtime owner changes blocked mod-only removal");

    package.payload.files[
        "Mods/MecchaCamouflage/dlls/main.dll"] =
        bytes("tampered");
    const auto bad_payload = build_shared_mod_material(
        package.manifest,
        package.manifest_sha256,
        package.payload);
    passed &= expect(
        !bad_payload &&
            bad_payload.error().code ==
                SharedModErrorCode::Payload,
        "shared payload hash mismatch was accepted");

    auto invalid_package = make_package();
    add_mod_file(
        invalid_package,
        "Mods/AnotherMod/dlls/main.dll",
        "foreign");
    const auto invalid_material = build_shared_mod_material(
        invalid_package.manifest,
        invalid_package.manifest_sha256,
        invalid_package.payload);
    passed &= expect(
        !invalid_material &&
            invalid_material.error().code ==
                SharedModErrorCode::Manifest,
        "a mod payload outside MecchaCamouflage was accepted");

    TemporaryTree junction_tree{};
    const auto external_mod =
        junction_tree.root / "external-mod";
    fs::create_directory(external_mod);
    const auto mod_junction =
        junction_tree.root / "shared" / "Mods" /
        "MecchaCamouflage";
    const auto junction_created =
        create_junction(mod_junction, external_mod);
    const auto refused_junction =
        junction_created
            ? apply_shared_mod_plan(
                  SharedModAction::Install,
                  junction_tree.root / "shared",
                  junction_tree.root / "ownership",
                  *material)
            : std::expected<
                  SharedModApplyResult,
                  SharedModError>{
                  std::unexpected(SharedModError{
                      SharedModErrorCode::Path,
                      "junction fixture creation failed"})};
    passed &= expect(
        junction_created && !refused_junction &&
            refused_junction.error().code ==
                SharedModErrorCode::Store &&
            fs::is_empty(external_mod) &&
            read_text(
                junction_tree.root / "shared" / "Mods" /
                "mods.txt") == "user-mods",
        "shared mod target junction was followed or accepted");

    if (passed)
    {
        std::cout << "PASS shared_mod\n";
    }
    return passed ? 0 : 1;
}
