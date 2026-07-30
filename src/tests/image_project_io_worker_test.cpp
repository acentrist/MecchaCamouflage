#include <meccha/application/image_project_io_worker.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
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
using namespace meccha::application;
using namespace std::chrono_literals;

constexpr auto ProjectId =
    std::string_view{"0123456789abcdef0123456789abcdef"};

auto expect(bool condition, std::string_view message) -> bool
{
    if (!condition)
    {
        std::cerr << "FAIL image_project_io_worker: "
                  << message << '\n';
    }
    return condition;
}

class DeterministicHasher final : public PresetHasher
{
public:
    auto hash(std::span<const std::byte> bytes)
        -> std::expected<
            common::Sha256Digest,
            PresetHashError> override
    {
        auto digest = common::Sha256Digest{};
        for (auto index = std::size_t{};
             index < bytes.size();
             ++index)
        {
            digest.bytes[index % digest.bytes.size()] ^=
                bytes[index];
        }
        return digest;
    }
};

class FakeProjectStorage final : public AtomicProjectStorage
{
public:
    auto read(std::string_view name, std::size_t maximum_bytes)
        -> std::expected<
            std::optional<std::vector<std::byte>>,
            ProjectStorageError> override
    {
        const auto lock = std::scoped_lock{mutex};
        read_thread = std::this_thread::get_id();
        if (throw_read)
        {
            throw std::runtime_error{"injected read failure"};
        }
        const auto found = files.find(std::string{name});
        if (found == files.end())
        {
            return std::nullopt;
        }
        if (found->second.size() > maximum_bytes)
        {
            return std::unexpected(ProjectStorageError{
                ProjectStorageErrorCode::TooLarge,
                "oversized fake project",
            });
        }
        return found->second;
    }

    auto write_atomic(
        std::string_view name,
        std::span<const std::byte> bytes)
        -> std::expected<void, ProjectStorageError> override
    {
        const auto lock = std::scoped_lock{mutex};
        files.insert_or_assign(
            std::string{name},
            std::vector<std::byte>{bytes.begin(), bytes.end()});
        return {};
    }

    auto remove(std::string_view name)
        -> std::expected<bool, ProjectStorageError> override
    {
        const auto lock = std::scoped_lock{mutex};
        return files.erase(std::string{name}) == 1U;
    }

    std::mutex mutex{};
    std::unordered_map<std::string, std::vector<std::byte>> files{};
    std::thread::id read_thread{};
    bool throw_read{};
};

class FakeTextStorage final : public AtomicTextStorage
{
public:
    auto read_text(std::string_view name, std::size_t)
        -> std::expected<
            std::optional<std::string>,
            TextStorageError> override
    {
        const auto found = files.find(std::string{name});
        return found == files.end()
                   ? std::optional<std::string>{}
                   : std::optional<std::string>{found->second};
    }

    auto write_text_atomic(
        std::string_view name,
        std::string_view text)
        -> std::expected<void, TextStorageError> override
    {
        files.insert_or_assign(
            std::string{name},
            std::string{text});
        return {};
    }

    std::unordered_map<std::string, std::string> files{};
};

auto project(
    DeterministicHasher& hasher,
    std::uint64_t revision) -> core::ImageProject
{
    auto source =
        std::make_shared<const std::vector<std::byte>>(
            std::initializer_list<std::byte>{
                std::byte{0x11},
            });
    const auto asset_id =
        common::sha256_hex(hasher.hash(*source).value());
    return core::ImageProject{
        core::ImageProjectSchemaVersion,
        std::string{ProjectId},
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
            std::byte{0x44}),
    };
}

auto wait_for_completion(ImageProjectIoWorker& worker)
    -> std::optional<ImageProjectIoCompletion>
{
    for (auto attempt = 0; attempt < 1000; ++attempt)
    {
        if (auto completed = worker.poll())
        {
            return completed;
        }
        std::this_thread::sleep_for(1ms);
    }
    return std::nullopt;
}
} // namespace

auto main() -> int
{
    auto passed = true;
    auto hasher = DeterministicHasher{};
    auto project_storage = FakeProjectStorage{};
    auto text_storage = FakeTextStorage{};
    auto project_store =
        ImageProjectStore{project_storage, hasher};
    auto config_store = ConfigStore{text_storage};
    auto persistence = ImageProjectPersistenceCoordinator{
        project_store,
        config_store,
    };
    auto saved = project(hasher, 1U);
    passed &= expect(
        project_store.save_named(saved, 0U).has_value(),
        "the named project fixture did not save");

    const auto caller_thread = std::this_thread::get_id();
    auto worker =
        ImageProjectIoWorker{project_store, persistence};
    const auto started = worker.start(
        ImageProjectLoadRequest{
            101U,
            std::string{ProjectId},
            core::ApplicationConfig{},
        });
    passed &= expect(
        started.has_value() &&
            worker.start(ImageProjectLoadRequest{
                102U,
                std::string{ProjectId},
                core::ApplicationConfig{},
            }) ==
                std::unexpected(
                    ImageProjectIoStartError::Busy),
        "load admission did not preserve single I/O ownership");

    const auto completed = wait_for_completion(worker);
    const auto* loaded =
        completed && completed->result
            ? std::get_if<ActivatedImageProject>(
                  &*completed->result)
            : nullptr;
    passed &= expect(
        completed && completed->command_id == 101U &&
            completed->operation ==
                ImageProjectIoOperation::Load &&
            loaded && loaded->project.project_id == ProjectId &&
            loaded->config.active_image_project &&
            loaded->config.active_image_project->project_id ==
                ProjectId &&
            project_storage.read_thread != caller_thread,
        "named load did not execute and activate off-thread");

    auto next = std::make_shared<const core::ImageProject>(
        project(hasher, 2U));
    const auto active_config =
        loaded
            ? loaded->config
            : core::ApplicationConfig{};
    passed &= expect(
        worker.start(ImageProjectSaveRequest{
            201U,
            next,
            1U,
            active_config,
        }).has_value(),
        "the ready project did not enter off-thread save");
    const auto saved_completion = wait_for_completion(worker);
    const auto* published =
        saved_completion && saved_completion->result
            ? std::get_if<SavedImageProject>(
                  &*saved_completion->result)
            : nullptr;
    const auto stored_after_save =
        project_store.load_named(ProjectId);
    passed &= expect(
        saved_completion &&
            saved_completion->operation ==
                ImageProjectIoOperation::Save &&
            published && published->project == next &&
            published->config.active_image_project &&
            stored_after_save && *stored_after_save &&
            (*stored_after_save)->revision == 2U,
        "off-thread save did not publish project before config");

    passed &= expect(
        worker.start(ImageProjectRenameRequest{
            202U,
            next,
            2U,
            "Renamed",
        }).has_value(),
        "the named project did not enter off-thread rename");
    const auto renamed_completion = wait_for_completion(worker);
    const auto* renamed =
        renamed_completion && renamed_completion->result
            ? std::get_if<core::ImageProject>(
                  &*renamed_completion->result)
            : nullptr;
    passed &= expect(
        renamed && renamed->display_name == "Renamed" &&
            renamed->revision == 3U,
        "rename did not preserve identity and advance revision");

    passed &= expect(
        worker.start(ImageProjectDeleteRequest{
            203U,
            std::string{ProjectId},
            published
                ? published->config
                : core::ApplicationConfig{},
        }).has_value(),
        "the named project did not enter off-thread delete");
    const auto deleted_completion = wait_for_completion(worker);
    const auto* deleted =
        deleted_completion && deleted_completion->result
            ? std::get_if<DeletedImageProject>(
                  &*deleted_completion->result)
            : nullptr;
    passed &= expect(
        deleted && deleted->deleted &&
            !deleted->config.active_image_project &&
            !project_store.load_named(ProjectId).value(),
        "delete left a named project or active config reference");

    passed &= expect(
        worker.start(ImageProjectLoadRequest{
            204U,
            std::string{ProjectId},
            core::ApplicationConfig{},
        }).has_value(),
        "the missing-project load did not enter the worker");
    const auto missing = wait_for_completion(worker);
    passed &= expect(
        missing && !missing->result &&
            missing->result.error().kind ==
                ImageProjectIoFailureKind::Persistence &&
            missing->result.error().persistence &&
            missing->result.error().persistence->project &&
            missing->result.error().persistence->project->code ==
                ImageProjectStoreErrorCode::NotFound,
        "missing named project did not remain a typed failure");

    project_storage.throw_read = true;
    passed &= expect(
        worker.start(ImageProjectLoadRequest{
            205U,
            std::string{ProjectId},
            core::ApplicationConfig{},
        }).has_value(),
        "the exception fixture did not enter the worker");
    const auto failed = wait_for_completion(worker);
    passed &= expect(
        failed && !failed->result &&
            failed->result.error().kind ==
                ImageProjectIoFailureKind::WorkerException,
        "an exception escaped the project I/O worker");

    worker.shutdown();
    passed &= expect(
        worker.start(ImageProjectLoadRequest{
            103U,
            std::string{ProjectId},
            core::ApplicationConfig{},
        }) ==
            std::unexpected(ImageProjectIoStartError::Stopped),
        "the stopped I/O worker accepted new work");

    if (passed)
    {
        std::cout << "PASS image_project_io_worker\n";
    }
    return passed ? 0 : 1;
}
