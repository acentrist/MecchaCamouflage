#include <meccha/application/image_project_store.hpp>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace
{
using namespace meccha;

auto expect(bool condition, std::string_view message) -> bool
{
    if (!condition)
    {
        std::cerr << "FAIL image_project_store: " << message << '\n';
    }
    return condition;
}

class DeterministicTestHasher final
    : public application::PresetHasher
{
public:
    auto hash(std::span<const std::byte> bytes)
        -> std::expected<
            common::Sha256Digest,
            application::PresetHashError> override
    {
        auto digest = common::Sha256Digest{};
        for (auto index = std::size_t{};
             index < bytes.size();
             ++index)
        {
            const auto slot = index % digest.bytes.size();
            const auto previous = std::to_integer<std::uint8_t>(
                digest.bytes[slot]);
            digest.bytes[slot] = static_cast<std::byte>(
                static_cast<std::uint8_t>(
                    previous * 33U +
                    std::to_integer<std::uint8_t>(bytes[index]) +
                    static_cast<std::uint8_t>(index)));
        }
        return digest;
    }
};

class FakeProjectStorage final
    : public application::AtomicProjectStorage
{
public:
    auto read(std::string_view name, std::size_t maximum_bytes)
        -> std::expected<
            std::optional<std::vector<std::byte>>,
            application::ProjectStorageError> override
    {
        reads.emplace_back(name);
        if (fail_read)
        {
            return std::unexpected(application::ProjectStorageError{
                application::ProjectStorageErrorCode::Io,
                "injected read failure",
            });
        }
        const auto found = files.find(std::string{name});
        if (found == files.end())
        {
            return std::nullopt;
        }
        if (found->second.size() > maximum_bytes)
        {
            return std::unexpected(application::ProjectStorageError{
                application::ProjectStorageErrorCode::TooLarge,
                "fake oversized project",
            });
        }
        return found->second;
    }

    auto write_atomic(
        std::string_view name,
        std::span<const std::byte> bytes)
        -> std::expected<
            void,
            application::ProjectStorageError> override
    {
        writes.emplace_back(name);
        if (fail_write)
        {
            return std::unexpected(application::ProjectStorageError{
                application::ProjectStorageErrorCode::Io,
                "injected write failure",
            });
        }
        files.insert_or_assign(
            std::string{name},
            std::vector<std::byte>{bytes.begin(), bytes.end()});
        return {};
    }

    auto remove(std::string_view name)
        -> std::expected<
            bool,
            application::ProjectStorageError> override
    {
        removals.emplace_back(name);
        if (fail_remove)
        {
            return std::unexpected(application::ProjectStorageError{
                application::ProjectStorageErrorCode::Io,
                "injected remove failure",
            });
        }
        return files.erase(std::string{name}) == 1U;
    }

    std::unordered_map<std::string, std::vector<std::byte>> files{};
    std::vector<std::string> reads{};
    std::vector<std::string> writes{};
    std::vector<std::string> removals{};
    bool fail_read{};
    bool fail_write{};
    bool fail_remove{};
};

auto make_project(
    DeterministicTestHasher& hasher,
    std::string project_id =
        "0123456789abcdef0123456789abcdef")
    -> core::ImageProject
{
    const auto source =
        std::make_shared<const std::vector<std::byte>>(
            std::initializer_list<std::byte>{
                std::byte{1},
                std::byte{2},
                std::byte{3},
            });
    const auto asset_id =
        common::sha256_hex(hasher.hash(*source).value());
    return core::ImageProject{
        core::ImageProjectSchemaVersion,
        std::move(project_id),
        "Project 日本語",
        1U,
        {},
        {core::ImageLayer{
            asset_id,
            "source.png",
            core::ImageMime::Png,
            source->size(),
        }},
        {core::ImageSourceAsset{
            asset_id,
            core::ImageMime::Png,
            source,
        }},
        std::make_shared<const std::vector<std::byte>>(
            core::CanonicalAtlasByteLength,
            std::byte{0x7F}),
    };
}
} // namespace

auto main() -> int
{
    using namespace meccha;

    constexpr auto project_id =
        std::string_view{"0123456789abcdef0123456789abcdef"};
    constexpr auto project_file =
        std::string_view{
            "0123456789abcdef0123456789abcdef.mcpreset"};
    constexpr auto active_file =
        std::string_view{"active-draft.mcpreset"};

    auto passed = true;
    auto hasher = DeterministicTestHasher{};
    auto storage = FakeProjectStorage{};
    auto store = application::ImageProjectStore{storage, hasher};
    const auto project = make_project(hasher);

    const auto missing = store.load_named(project_id);
    passed &= expect(
        missing && !*missing &&
            storage.reads ==
                std::vector<std::string>{
                    std::string{project_file}},
        "missing named project did not remain missing");

    const auto invalid_id = store.load_named("../escape");
    passed &= expect(
        !invalid_id &&
            invalid_id.error().code ==
                application::ImageProjectStoreErrorCode::InvalidProjectId &&
            storage.reads.size() == 1U,
        "an unsafe project ID reached storage");

    const auto initial_save = store.save_named(project, 0U);
    passed &= expect(
        initial_save &&
            storage.writes ==
                std::vector<std::string>{
                    std::string{project_file}},
        "initial revision was not published");
    const auto loaded = store.load_named(project_id);
    passed &= expect(
        loaded && *loaded && **loaded == project,
        "published named project did not round trip");

    const auto stale_initial = store.save_named(project, 0U);
    passed &= expect(
        !stale_initial &&
            stale_initial.error().code ==
                application::ImageProjectStoreErrorCode::RevisionConflict &&
            storage.writes.size() == 1U,
        "a stale expected revision replaced a named project");

    auto revision_two = project;
    revision_two.revision = 2U;
    revision_two.settings.brush_size_texels = 6.0;
    const auto update = store.save_named(revision_two, 1U);
    passed &= expect(
        update && storage.writes.size() == 2U,
        "the next named-project revision was not published");

    auto skipped_revision = revision_two;
    skipped_revision.revision = 4U;
    const auto skipped =
        store.save_named(skipped_revision, 2U);
    passed &= expect(
        !skipped &&
            skipped.error().code ==
                application::ImageProjectStoreErrorCode::RevisionConflict &&
            storage.writes.size() == 2U,
        "a non-consecutive project revision was accepted");

    const auto renamed =
        store.rename_named(project_id, "Renamed 日本語");
    passed &= expect(
        renamed && renamed->display_name == "Renamed 日本語" &&
            renamed->revision == 3U,
        "rename did not atomically advance the project revision");
    const auto loaded_renamed = store.load_named(project_id);
    passed &= expect(
        loaded_renamed && *loaded_renamed &&
            (*loaded_renamed)->display_name == "Renamed 日本語" &&
            (*loaded_renamed)->revision == 3U,
        "renamed project was not persisted");

    const auto writes_before_invalid_name = storage.writes.size();
    const auto invalid_name =
        store.rename_named(project_id, std::string{"bad\nname"});
    passed &= expect(
        !invalid_name &&
            invalid_name.error().code ==
                application::ImageProjectStoreErrorCode::Codec &&
            invalid_name.error().codec &&
            invalid_name.error().codec->code ==
                application::ImageProjectCodecErrorCode::InvalidProject &&
            storage.writes.size() == writes_before_invalid_name,
        "an invalid project name reached storage");

    const auto draft_save = store.save_active_draft(*renamed);
    const auto draft_load = store.load_active_draft();
    passed &= expect(
        draft_save && draft_load && *draft_load &&
            **draft_load == *renamed &&
            storage.files.contains(std::string{active_file}),
        "active draft did not round trip independently");
    const auto draft_clear = store.clear_active_draft();
    passed &= expect(
        draft_clear && *draft_clear &&
            !storage.files.contains(std::string{active_file}),
        "active draft was not cleared");
    const auto draft_clear_again = store.clear_active_draft();
    passed &= expect(
        draft_clear_again && !*draft_clear_again,
        "clearing a missing active draft was not idempotent");

    const auto other =
        make_project(
            hasher,
            "fedcba9876543210fedcba9876543210");
    const auto wrong_bytes =
        application::encode_image_project(other, hasher);
    if (wrong_bytes)
    {
        storage.files.insert_or_assign(
            std::string{project_file},
            *wrong_bytes);
    }
    const auto identity_mismatch = store.load_named(project_id);
    passed &= expect(
        wrong_bytes && !identity_mismatch &&
            identity_mismatch.error().code ==
                application::ImageProjectStoreErrorCode::IdentityMismatch,
        "a project stored under the wrong ID was published");

    storage.files[std::string{project_file}] =
        std::vector<std::byte>{
            std::byte{'M'},
            std::byte{'C'},
            std::byte{'I'},
            std::byte{'P'},
            std::byte{'R'},
            std::byte{'S'},
            std::byte{'T'},
            std::byte{'1'},
        };
    const auto legacy = store.load_named(project_id);
    passed &= expect(
        !legacy &&
            legacy.error().code ==
                application::ImageProjectStoreErrorCode::Codec &&
            legacy.error().codec &&
            legacy.error().codec->code ==
                application::ImageProjectCodecErrorCode::LegacyFormat,
        "legacy preset rejection was lost at the store boundary");

    storage.files.erase(std::string{project_file});
    const auto rename_missing =
        store.rename_named(project_id, "Missing");
    passed &= expect(
        !rename_missing &&
            rename_missing.error().code ==
                application::ImageProjectStoreErrorCode::NotFound,
        "renaming a missing project did not report not-found");

    storage.files.insert_or_assign(
        std::string{project_file},
        application::encode_image_project(project, hasher).value());
    const auto deleted = store.delete_named(project_id);
    const auto deleted_again = store.delete_named(project_id);
    passed &= expect(
        deleted && *deleted && deleted_again && !*deleted_again,
        "named project deletion was not idempotent");

    storage.fail_read = true;
    const auto failed_read = store.load_named(project_id);
    passed &= expect(
        !failed_read &&
            failed_read.error().code ==
                application::ImageProjectStoreErrorCode::Storage &&
            failed_read.error().storage &&
            failed_read.error().storage->code ==
                application::ProjectStorageErrorCode::Io,
        "storage read failure lost its structured cause");

    storage.fail_read = false;
    auto overflow_project = project;
    overflow_project.revision =
        std::numeric_limits<std::uint64_t>::max();
    const auto overflow = store.save_named(
        overflow_project,
        std::numeric_limits<std::uint64_t>::max());
    passed &= expect(
        !overflow &&
            overflow.error().code ==
                application::ImageProjectStoreErrorCode::RevisionOverflow,
        "revision overflow was not rejected before storage");

    const auto other_file =
        std::string{
            "fedcba9876543210fedcba9876543210.mcpreset"};
    storage.fail_write = true;
    const auto failed_write = store.save_named(other, 0U);
    passed &= expect(
        !failed_write &&
            failed_write.error().code ==
                application::ImageProjectStoreErrorCode::Storage &&
            failed_write.error().storage &&
            failed_write.error().storage->code ==
                application::ProjectStorageErrorCode::Io &&
            !storage.files.contains(other_file),
        "failed atomic publication changed project storage");

    storage.fail_write = false;
    storage.files.insert_or_assign(
        other_file,
        application::encode_image_project(other, hasher).value());
    storage.fail_remove = true;
    const auto failed_remove =
        store.delete_named(other.project_id);
    passed &= expect(
        !failed_remove &&
            failed_remove.error().code ==
                application::ImageProjectStoreErrorCode::Storage &&
            storage.files.contains(other_file),
        "failed project deletion removed the destination");

    if (passed)
    {
        std::cout << "PASS image_project_store\n";
        return 0;
    }
    return 1;
}
