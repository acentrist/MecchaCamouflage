#include <meccha/application/image_editor_session.hpp>

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
using namespace meccha::application;
using namespace std::chrono_literals;

constexpr auto ProjectId =
    std::string_view{"0123456789abcdef0123456789abcdef"};

auto expect(bool condition, std::string_view message) -> bool
{
    if (!condition)
    {
        std::cerr << "FAIL image_editor_session: "
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
        const auto lock = std::scoped_lock{mutex_};
        const auto found = files_.find(std::string{name});
        if (found == files_.end())
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
        const auto lock = std::scoped_lock{mutex_};
        files_.insert_or_assign(
            std::string{name},
            std::vector<std::byte>{bytes.begin(), bytes.end()});
        return {};
    }

    auto remove(std::string_view name)
        -> std::expected<bool, ProjectStorageError> override
    {
        const auto lock = std::scoped_lock{mutex_};
        return files_.erase(std::string{name}) == 1U;
    }

private:
    std::mutex mutex_{};
    std::unordered_map<std::string, std::vector<std::byte>> files_{};
};

class FakeTextStorage final : public AtomicTextStorage
{
public:
    auto read_text(std::string_view name, std::size_t)
        -> std::expected<
            std::optional<std::string>,
            TextStorageError> override
    {
        const auto lock = std::scoped_lock{mutex_};
        const auto found = files_.find(std::string{name});
        return found == files_.end()
                   ? std::optional<std::string>{}
                   : std::optional<std::string>{found->second};
    }

    auto write_text_atomic(
        std::string_view name,
        std::string_view text)
        -> std::expected<void, TextStorageError> override
    {
        const auto lock = std::scoped_lock{mutex_};
        files_.insert_or_assign(
            std::string{name},
            std::string{text});
        return {};
    }

private:
    std::mutex mutex_{};
    std::unordered_map<std::string, std::string> files_{};
};

class TestDecoder final : public ImageSourceDecoder
{
public:
    auto decode(
        std::string_view asset_id,
        core::ImageMime,
        std::span<const std::byte>,
        std::stop_token)
        -> std::expected<
            core::DecodedImageSource,
            ImageDecodeError> override
    {
        return core::DecodedImageSource{
            std::string{asset_id},
            1U,
            1U,
            std::make_shared<const std::vector<std::byte>>(
                4U,
                std::byte{0x7F}),
        };
    }
};

class TestComposer final : public ImageAtlasComposer
{
public:
    auto compose(
        const core::ImageProjectSettings&,
        std::span<const core::ImageLayer>,
        std::span<const core::DecodedImageSource>,
        std::stop_token)
        -> std::expected<
            core::ImageAtlasComposition,
            core::ImageComposeError> override
    {
        return core::ImageAtlasComposition{
            std::vector<std::byte>(
                core::CanonicalAtlasByteLength,
                std::byte{0x5A}),
            1U,
            1U,
            1U,
            1U,
        };
    }
};

auto project(
    DeterministicHasher& hasher,
    std::uint64_t revision) -> core::ImageProject
{
    auto bytes =
        std::make_shared<const std::vector<std::byte>>(
            std::initializer_list<std::byte>{
                std::byte{0x11},
            });
    const auto asset_id =
        common::sha256_hex(hasher.hash(*bytes).value());
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
            bytes->size(),
        }},
        {core::ImageSourceAsset{
            asset_id,
            core::ImageMime::Png,
            bytes,
        }},
        std::make_shared<const std::vector<std::byte>>(
            core::CanonicalAtlasByteLength,
            std::byte{0x00}),
    };
}

auto wait_for_completion(ImageEditorSession& session)
    -> std::optional<ImageEditorSessionCompletion>
{
    for (auto attempt = 0; attempt < 2000; ++attempt)
    {
        session.update();
        if (auto completed = session.poll_completion())
        {
            return completed;
        }
        std::this_thread::sleep_for(1ms);
    }
    return std::nullopt;
}

auto wait_until_ready(
    ImageEditorSession& session,
    std::uint64_t revision) -> bool
{
    for (auto attempt = 0; attempt < 2000; ++attempt)
    {
        session.update();
        const auto snapshot = session.session_snapshot();
        if (snapshot.pipeline.phase ==
                ImageEditorPipelinePhase::Ready &&
            snapshot.pipeline.project_revision == revision &&
            !snapshot.active_draft.pending &&
            !snapshot.active_draft.in_flight)
        {
            return true;
        }
        std::this_thread::sleep_for(1ms);
    }
    return false;
}
} // namespace

auto main() -> int
{
    auto passed = true;
    auto hasher = DeterministicHasher{};
    auto project_storage = FakeProjectStorage{};
    auto text_storage = FakeTextStorage{};
    auto projects = ImageProjectStore{
        project_storage,
        hasher,
    };
    auto configs = ConfigStore{text_storage};
    auto persistence = ImageProjectPersistenceCoordinator{
        projects,
        configs,
    };
    auto decoder = TestDecoder{};
    auto composer = TestComposer{};
    auto original = project(hasher, 1U);
    passed &= expect(
        projects.save_named(original, 0U).has_value(),
        "the named fixture did not save");

    auto startup_config = core::ApplicationConfig{};
    startup_config.active_image_project =
        core::ActiveImageProjectReference{
            core::ImageProjectReferenceKind::NamedProject,
            std::string{ProjectId},
        };
    {
        auto recovered_session = ImageEditorSession{
            decoder,
            composer,
            projects,
            persistence,
            1ms,
        };
        const auto recovered =
            recovered_session.recover_startup(startup_config);
        passed &= expect(
            recovered &&
                recovered->attempted &&
                recovered->source ==
                    RecoveredImageProjectSource::NamedProject &&
                recovered->pipeline_generation &&
                recovered->diagnostics.empty() &&
                wait_until_ready(recovered_session, 1U) &&
                recovered_session.recover_startup(startup_config) ==
                    std::unexpected(
                        ImageEditorStartupError::AlreadyAttempted),
            "startup recovery did not publish the named project exactly once");
        recovered_session.shutdown(false);
    }

    auto session = ImageEditorSession{
        decoder,
        composer,
        projects,
        persistence,
        1ms,
    };
    passed &= expect(
        session.save(
            1U,
            ProjectId,
            0U,
            core::ApplicationConfig{}) ==
                std::unexpected(
                    ImageEditorSessionStartError::NotReady),
        "save accepted a project that was not editor-ready");

    passed &= expect(
        session.load(
            101U,
            std::string{ProjectId},
            core::ApplicationConfig{}).has_value() &&
            session.load(
                102U,
                std::string{ProjectId},
                core::ApplicationConfig{}) ==
                std::unexpected(
                    ImageEditorSessionStartError::Busy),
        "load admission did not retain one persistence owner");
    const auto loaded = wait_for_completion(session);
    const auto loaded_config =
        loaded && loaded->result &&
                loaded->result->config
            ? *loaded->result->config
            : core::ApplicationConfig{};
    passed &= expect(
        loaded && loaded->command_id == 101U &&
            loaded->operation ==
                ImageProjectIoOperation::Load &&
            loaded->result &&
            loaded->result->project_id == ProjectId &&
            loaded->result->project_revision == 1U &&
            loaded->result->pipeline_generation &&
            loaded_config.active_image_project &&
            wait_until_ready(session, 1U),
        "named load did not activate and prepare the project");

    auto edited = *session.ready_project(ProjectId, 1U);
    edited.revision = 2U;
    edited.settings.brush_size_texels = 9.0;
    passed &= expect(
        session.submit_edit(std::move(edited)).has_value() &&
            wait_until_ready(session, 2U),
        "an editor revision did not compose and debounce");
    const auto draft = projects.load_active_draft();
    passed &= expect(
        draft && *draft && (*draft)->revision == 2U &&
            (*draft)->settings.brush_size_texels == 9.0,
        "the latest ready revision was not persisted as active draft");

    passed &= expect(
        session.save(
            201U,
            ProjectId,
            1U,
            loaded_config).has_value(),
        "the ready project did not enter named save");
    const auto saved = wait_for_completion(session);
    const auto saved_config =
        saved && saved->result &&
                saved->result->config
            ? *saved->result->config
            : core::ApplicationConfig{};
    const auto named_after_save =
        projects.load_named(ProjectId);
    passed &= expect(
        saved && saved->result &&
            saved->result->project_revision == 2U &&
            saved_config.active_image_project &&
            named_after_save && *named_after_save &&
            (*named_after_save)->revision == 2U,
        "named save did not publish the exact ready revision");

    passed &= expect(
        session.rename(
            301U,
            ProjectId,
            2U,
            "Renamed").has_value(),
        "rename did not accept the ready project");
    const auto renamed = wait_for_completion(session);
    passed &= expect(
        renamed && renamed->result &&
            renamed->result->project_revision == 3U &&
            renamed->result->pipeline_generation &&
            wait_until_ready(session, 3U),
        "rename did not advance and re-publish editor state");
    const auto named_after_rename =
        projects.load_named(ProjectId);
    passed &= expect(
        named_after_rename && *named_after_rename &&
            (*named_after_rename)->revision == 3U &&
            (*named_after_rename)->display_name == "Renamed" &&
            (*named_after_rename)->settings.brush_size_texels ==
                9.0,
        "rename discarded unsaved editor metadata");

    passed &= expect(
        session.remove(
            401U,
            std::string{ProjectId},
            saved_config).has_value(),
        "delete did not enter draft-drain coordination");
    const auto deleted = wait_for_completion(session);
    const auto named_after_delete =
        projects.load_named(ProjectId);
    const auto draft_after_delete =
        projects.load_active_draft();
    passed &= expect(
        deleted && deleted->result &&
            deleted->result->deleted &&
            deleted->result->config &&
            !deleted->result->config->active_image_project &&
            named_after_delete && !*named_after_delete &&
            draft_after_delete && !*draft_after_delete &&
            session.snapshot().phase ==
                ImageEditorPipelinePhase::Empty,
        "delete raced active draft persistence or retained editor state");

    session.shutdown(true);
    passed &= expect(
        session.session_snapshot().stopped &&
            session.submit_edit(project(hasher, 4U)) ==
                std::unexpected(
                    ImageEditorSubmitError::Stopped) &&
            session.load(
                501U,
                std::string{ProjectId},
                core::ApplicationConfig{}) ==
                std::unexpected(
                    ImageEditorSessionStartError::Stopped),
        "terminal shutdown retained work admission");

    if (passed)
    {
        std::cout << "PASS image_editor_session\n";
    }
    return passed ? 0 : 1;
}
