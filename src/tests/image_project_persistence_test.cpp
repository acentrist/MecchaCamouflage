#include <meccha/application/image_project_persistence.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
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
        std::cerr << "FAIL image_project_persistence: "
                  << message << '\n';
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
        const auto lock = std::scoped_lock{mutex};
        const auto found = files.find(std::string{name});
        if (found == files.end())
        {
            return std::nullopt;
        }
        if (found->second.size() > maximum_bytes)
        {
            return std::unexpected(application::ProjectStorageError{
                application::ProjectStorageErrorCode::TooLarge,
                "oversized fake project",
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
        const auto lock = std::scoped_lock{mutex};
        write_thread = std::this_thread::get_id();
        if (fail_write)
        {
            return std::unexpected(application::ProjectStorageError{
                application::ProjectStorageErrorCode::Io,
                "injected project write failure",
            });
        }
        files.insert_or_assign(
            std::string{name},
            std::vector<std::byte>{bytes.begin(), bytes.end()});
        ++write_count;
        return {};
    }

    auto remove(std::string_view name)
        -> std::expected<
            bool,
            application::ProjectStorageError> override
    {
        const auto lock = std::scoped_lock{mutex};
        return files.erase(std::string{name}) == 1U;
    }

    auto corrupt_active_draft() -> void
    {
        const auto lock = std::scoped_lock{mutex};
        files["active-draft.mcpreset"] = {
            std::byte{'b'},
            std::byte{'a'},
            std::byte{'d'},
        };
    }

    mutable std::mutex mutex{};
    std::unordered_map<std::string, std::vector<std::byte>> files{};
    std::thread::id write_thread{};
    std::size_t write_count{};
    bool fail_write{};
};

class FakeTextStorage final
    : public application::AtomicTextStorage
{
public:
    auto read_text(std::string_view name, std::size_t)
        -> std::expected<
            std::optional<std::string>,
            application::TextStorageError> override
    {
        const auto found = files.find(std::string{name});
        if (found == files.end())
        {
            return std::nullopt;
        }
        return found->second;
    }

    auto write_text_atomic(
        std::string_view name,
        std::string_view text)
        -> std::expected<void, application::TextStorageError> override
    {
        if (fail_write)
        {
            return std::unexpected(application::TextStorageError{
                application::TextStorageErrorCode::Io,
                "injected config write failure",
            });
        }
        files.insert_or_assign(
            std::string{name},
            std::string{text});
        return {};
    }

    std::unordered_map<std::string, std::string> files{};
    bool fail_write{};
};

auto make_project(
    DeterministicTestHasher& hasher,
    std::string project_id,
    std::uint64_t revision) -> core::ImageProject
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
        "Project",
        revision,
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
            std::byte{0x31}),
    };
}
} // namespace

auto main() -> int
{
    using namespace meccha;
    using namespace std::chrono_literals;

    constexpr auto first_id =
        std::string_view{"0123456789abcdef0123456789abcdef"};
    constexpr auto second_id =
        std::string_view{"fedcba9876543210fedcba9876543210"};
    auto passed = true;
    auto hasher = DeterministicTestHasher{};
    auto project_storage = FakeProjectStorage{};
    auto text_storage = FakeTextStorage{};
    auto project_store =
        application::ImageProjectStore{project_storage, hasher};
    auto config_store = application::ConfigStore{text_storage};
    auto coordinator =
        application::ImageProjectPersistenceCoordinator{
            project_store,
            config_store,
        };

    auto named = make_project(hasher, std::string{first_id}, 1U);
    passed &= expect(
        project_store.save_named(named, 0U).has_value(),
        "named recovery fixture did not save");
    auto config = core::ApplicationConfig{};
    config.active_image_project =
        core::ActiveImageProjectReference{
            core::ImageProjectReferenceKind::NamedProject,
            std::string{first_id},
        };

    const auto named_recovery = coordinator.recover(config);
    passed &= expect(
        named_recovery.project &&
            named_recovery.source ==
                application::RecoveredImageProjectSource::NamedProject &&
            named_recovery.diagnostics.empty(),
        "a valid named project was not recovered");

    auto newer_draft = named;
    newer_draft.revision = 4U;
    newer_draft.display_name = "Unsaved";
    passed &= expect(
        project_store.save_active_draft(newer_draft).has_value(),
        "newer active draft fixture did not save");
    const auto draft_recovery = coordinator.recover(config);
    passed &= expect(
        draft_recovery.project &&
            draft_recovery.project->revision == 4U &&
            draft_recovery.source ==
                application::RecoveredImageProjectSource::ActiveDraft,
        "a newer matching draft did not supersede the named project");

    auto missing_config = config;
    missing_config.active_image_project->project_id =
        std::string{second_id};
    const auto fallback = coordinator.recover(missing_config);
    passed &= expect(
        fallback.project &&
            fallback.project->project_id == first_id &&
            fallback.source ==
                application::RecoveredImageProjectSource::ActiveDraft &&
            fallback.diagnostics.size() == 1U &&
            fallback.diagnostics.front().code ==
                application::ImageProjectRecoveryDiagnosticCode::
                    MissingNamedProject,
        "a missing named reference did not fall back non-destructively");

    auto published = make_project(
        hasher,
        std::string{second_id},
        7U);
    text_storage.fail_write = true;
    const auto partial = coordinator.save_named_and_activate(
        published,
        0U,
        config);
    passed &= expect(
        !partial &&
            partial.error().code ==
                application::ImageProjectPersistenceErrorCode::
                    Configuration &&
            project_store.load_named(second_id).value().has_value() &&
            config.active_image_project->project_id == first_id,
        "config-reference failure lost the published project or old config");

    text_storage.fail_write = false;
    const auto activated = coordinator.save_named_and_activate(
        published,
        7U,
        config);
    passed &= expect(
        !activated &&
            activated.error().code ==
                application::ImageProjectPersistenceErrorCode::Project,
        "a stale named publication unexpectedly rewrote the project");

    auto next = published;
    next.revision = 9U;
    const auto activation = coordinator.save_named_and_activate(
        next,
        7U,
        config);
    passed &= expect(
        activation &&
            activation->active_image_project &&
            activation->active_image_project->kind ==
                core::ImageProjectReferenceKind::NamedProject &&
            activation->active_image_project->project_id == second_id,
        "named publication did not atomically follow with its reference");

    const auto loaded_and_activated =
        coordinator.load_named_and_activate(
            second_id,
            config);
    passed &= expect(
        loaded_and_activated &&
            loaded_and_activated->project.project_id ==
                second_id &&
            loaded_and_activated->config.active_image_project &&
            loaded_and_activated->config.active_image_project->kind ==
                core::ImageProjectReferenceKind::NamedProject &&
            loaded_and_activated->config.active_image_project->project_id ==
                second_id,
        "named load did not validate before activating its reference");

    constexpr auto imported_id =
        std::string_view{"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"};
    const auto imported_project = make_project(
        hasher,
        std::string{imported_id},
        3U);
    const auto imported_bytes =
        application::encode_image_project(
            imported_project,
            hasher);
    const auto imported_and_activated =
        coordinator.import_named_and_activate(
            imported_bytes.value(),
            config);
    passed &= expect(
        imported_and_activated &&
            imported_and_activated->project ==
                imported_project &&
            imported_and_activated->config
                    .active_image_project &&
            imported_and_activated->config
                    .active_image_project->project_id ==
                imported_id &&
            project_store.load_named(imported_id)
                .value()
                .has_value(),
        "preset import did not publish before activating its reference");

    auto conflicting_import = imported_project;
    conflicting_import.display_name = "Conflict";
    const auto conflicting_import_bytes =
        application::encode_image_project(
            conflicting_import,
            hasher);
    const auto conflict =
        coordinator.import_named_and_activate(
            conflicting_import_bytes.value(),
            config);
    passed &= expect(
        !conflict &&
            conflict.error().code ==
                application::ImageProjectPersistenceErrorCode::
                    Project &&
            conflict.error().project &&
            conflict.error().project->code ==
                application::ImageProjectStoreErrorCode::
                    ImportConflict &&
            project_store.load_named(imported_id).value() ==
                std::optional{imported_project},
        "preset activation overwrote a conflicting stored project");

    const auto deleted_and_deactivated =
        coordinator.delete_named_and_deactivate(
            second_id,
            loaded_and_activated->config);
    passed &= expect(
        deleted_and_deactivated &&
            deleted_and_deactivated->deleted &&
            !deleted_and_deactivated->config.active_image_project &&
            !project_store.load_named(second_id).value(),
        "active named deletion left a dangling reference or project");

    passed &= expect(
        project_store.save_named(published, 0U).has_value(),
        "delete failure fixture could not restore the named project");
    text_storage.fail_write = true;
    const auto refused_delete =
        coordinator.delete_named_and_deactivate(
            second_id,
            loaded_and_activated->config);
    passed &= expect(
        !refused_delete &&
            refused_delete.error().code ==
                application::ImageProjectPersistenceErrorCode::
                    Configuration &&
            project_store.load_named(second_id).value().has_value(),
        "config failure deleted a still-referenced named project");
    text_storage.fail_write = false;

    project_storage.corrupt_active_draft();
    const auto corrupt_draft = coordinator.recover(
        core::ApplicationConfig{});
    passed &= expect(
        !corrupt_draft.project &&
            corrupt_draft.source ==
                application::RecoveredImageProjectSource::Blank &&
            corrupt_draft.diagnostics.size() == 1U &&
            corrupt_draft.diagnostics.front().code ==
                application::ImageProjectRecoveryDiagnosticCode::
                    InvalidActiveDraft,
        "a corrupt active draft partially published editor state");

    auto worker_storage = FakeProjectStorage{};
    auto worker_store =
        application::ImageProjectStore{worker_storage, hasher};
    const auto caller_thread = std::this_thread::get_id();
    auto worker = application::ActiveDraftPersistenceWorker{
        worker_store,
        40ms,
    };
    auto first_draft =
        std::make_shared<const core::ImageProject>(
            make_project(hasher, std::string{first_id}, 1U));
    auto latest_project =
        make_project(hasher, std::string{first_id}, 6U);
    auto latest_draft =
        std::make_shared<const core::ImageProject>(
            std::move(latest_project));
    passed &= expect(
        worker.schedule(first_draft).has_value() &&
            worker.schedule(latest_draft).has_value() &&
            worker.wait_until_idle(5s),
        "debounced draft worker did not become idle");
    const auto persisted_draft = worker_store.load_active_draft();
    const auto worker_snapshot = worker.snapshot();
    passed &= expect(
        persisted_draft && *persisted_draft &&
            (*persisted_draft)->revision == 6U &&
            worker_storage.write_count == 1U &&
            worker_storage.write_thread != caller_thread &&
            worker_snapshot.completed_generation ==
                worker_snapshot.scheduled_generation &&
            !worker_snapshot.pending &&
            !worker_snapshot.in_flight &&
            !worker_snapshot.last_error,
        "draft debounce did not coalesce and publish off-thread");

    worker_storage.fail_write = true;
    auto failing_draft =
        std::make_shared<const core::ImageProject>(
            make_project(hasher, std::string{first_id}, 8U));
    passed &= expect(
        worker.schedule(failing_draft).has_value() &&
            worker.wait_until_idle(5s) &&
            worker.snapshot().last_error.has_value(),
        "draft worker did not expose its failed publication");
    worker.shutdown(true);
    passed &= expect(
        !worker.schedule(failing_draft) &&
            worker.snapshot().stopped,
        "draft worker accepted work after shutdown");

    if (passed)
    {
        std::cout << "PASS image_project_persistence\n";
    }
    return passed ? 0 : 1;
}
