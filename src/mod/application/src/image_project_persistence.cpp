#include <meccha/application/image_project_persistence.hpp>

#include <algorithm>
#include <chrono>
#include <exception>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>

namespace meccha::application
{
namespace
{
constexpr auto MaximumDiagnosticBytes = std::size_t{512U};

auto bounded_detail(std::string detail) -> std::string
{
    if (detail.size() > MaximumDiagnosticBytes)
    {
        detail.resize(MaximumDiagnosticBytes);
    }
    return detail;
}

auto recovery_diagnostic(
    ImageProjectRecoveryDiagnosticCode code,
    std::string detail) -> ImageProjectRecoveryDiagnostic
{
    return ImageProjectRecoveryDiagnostic{
        code,
        bounded_detail(std::move(detail)),
    };
}

auto project_failure(ImageProjectStoreError error)
    -> ImageProjectPersistenceError
{
    return ImageProjectPersistenceError{
        ImageProjectPersistenceErrorCode::Project,
        std::move(error),
        std::nullopt,
        "Image project publication failed.",
    };
}

auto configuration_failure(ConfigStoreError error)
    -> ImageProjectPersistenceError
{
    return ImageProjectPersistenceError{
        ImageProjectPersistenceErrorCode::Configuration,
        std::nullopt,
        std::move(error),
        "The project was published, but its active reference was not.",
    };
}

auto unexpected_worker_failure() -> ImageProjectStoreError
{
    return ImageProjectStoreError{
        ImageProjectStoreErrorCode::Storage,
        ProjectStorageError{
            ProjectStorageErrorCode::Io,
            "Unexpected active-draft worker failure.",
        },
        std::nullopt,
        "Unexpected active-draft worker failure.",
    };
}
} // namespace

ImageProjectPersistenceCoordinator::
    ImageProjectPersistenceCoordinator(
        ImageProjectStore& projects,
        ConfigStore& configuration)
    : projects_{projects},
      configuration_{configuration}
{
}

auto ImageProjectPersistenceCoordinator::recover(
    const core::ApplicationConfig& config)
    -> ImageProjectRecoveryResult
{
    auto result = ImageProjectRecoveryResult{};
    auto draft = std::optional<core::ImageProject>{};
    const auto loaded_draft = projects_.load_active_draft();
    if (!loaded_draft)
    {
        result.diagnostics.push_back(recovery_diagnostic(
            ImageProjectRecoveryDiagnosticCode::InvalidActiveDraft,
            loaded_draft.error().detail));
    }
    else
    {
        draft = std::move(*loaded_draft);
    }

    if (!config.active_image_project)
    {
        if (draft)
        {
            result.project = std::move(draft);
            result.source =
                RecoveredImageProjectSource::ActiveDraft;
        }
        return result;
    }

    const auto& reference = *config.active_image_project;
    if (reference.kind ==
        core::ImageProjectReferenceKind::ActiveDraft)
    {
        if (!draft)
        {
            if (loaded_draft)
            {
                result.diagnostics.push_back(recovery_diagnostic(
                    ImageProjectRecoveryDiagnosticCode::
                        MissingActiveDraft,
                    "The active draft referenced by config is missing."));
            }
            return result;
        }
        if (draft->project_id != reference.project_id)
        {
            result.diagnostics.push_back(recovery_diagnostic(
                ImageProjectRecoveryDiagnosticCode::
                    ActiveDraftIdentityMismatch,
                "The active draft ID does not match config."));
        }
        result.project = std::move(draft);
        result.source = RecoveredImageProjectSource::ActiveDraft;
        return result;
    }

    auto named = std::optional<core::ImageProject>{};
    const auto loaded_named =
        projects_.load_named(reference.project_id);
    if (!loaded_named)
    {
        result.diagnostics.push_back(recovery_diagnostic(
            ImageProjectRecoveryDiagnosticCode::InvalidNamedProject,
            loaded_named.error().detail));
    }
    else if (!*loaded_named)
    {
        result.diagnostics.push_back(recovery_diagnostic(
            ImageProjectRecoveryDiagnosticCode::MissingNamedProject,
            "The named project referenced by config is missing."));
    }
    else
    {
        named = std::move(**loaded_named);
    }

    if (named)
    {
        if (draft &&
            draft->project_id == named->project_id &&
            draft->revision >= named->revision)
        {
            result.project = std::move(draft);
            result.source =
                RecoveredImageProjectSource::ActiveDraft;
        }
        else
        {
            result.project = std::move(named);
            result.source =
                RecoveredImageProjectSource::NamedProject;
        }
        return result;
    }

    if (draft)
    {
        result.project = std::move(draft);
        result.source = RecoveredImageProjectSource::ActiveDraft;
    }
    return result;
}

auto ImageProjectPersistenceCoordinator::save_named_and_activate(
    const core::ImageProject& project,
    std::uint64_t expected_revision,
    const core::ApplicationConfig& current_config)
    -> std::expected<
        core::ApplicationConfig,
        ImageProjectPersistenceError>
{
    const auto saved =
        projects_.save_named(project, expected_revision);
    if (!saved)
    {
        return std::unexpected(project_failure(saved.error()));
    }

    auto updated = current_config;
    updated.active_image_project =
        core::ActiveImageProjectReference{
            core::ImageProjectReferenceKind::NamedProject,
            project.project_id,
        };
    const auto configured = configuration_.save(updated);
    if (!configured)
    {
        return std::unexpected(
            configuration_failure(configured.error()));
    }
    return updated;
}

auto ImageProjectPersistenceCoordinator::load_named_and_activate(
    std::string_view project_id,
    const core::ApplicationConfig& current_config)
    -> std::expected<
        ActivatedImageProject,
        ImageProjectPersistenceError>
{
    auto loaded = projects_.load_named(project_id);
    if (!loaded)
    {
        return std::unexpected(project_failure(loaded.error()));
    }
    if (!*loaded)
    {
        return std::unexpected(project_failure(
            ImageProjectStoreError{
                ImageProjectStoreErrorCode::NotFound,
                std::nullopt,
                std::nullopt,
                "The named image project does not exist.",
            }));
    }

    auto updated = current_config;
    updated.active_image_project =
        core::ActiveImageProjectReference{
            core::ImageProjectReferenceKind::NamedProject,
            std::string{project_id},
        };
    const auto configured = configuration_.save(updated);
    if (!configured)
    {
        return std::unexpected(
            configuration_failure(configured.error()));
    }
    return ActivatedImageProject{
        std::move(**loaded),
        std::move(updated),
    };
}

auto ImageProjectPersistenceCoordinator::import_named_and_activate(
    std::span<const std::byte> bytes,
    const core::ApplicationConfig& current_config)
    -> std::expected<
        ActivatedImageProject,
        ImageProjectPersistenceError>
{
    auto imported = projects_.import_named(bytes);
    if (!imported)
    {
        return std::unexpected(
            project_failure(imported.error()));
    }

    auto updated = current_config;
    updated.active_image_project =
        core::ActiveImageProjectReference{
            core::ImageProjectReferenceKind::NamedProject,
            imported->project.project_id,
        };
    const auto configured = configuration_.save(updated);
    if (!configured)
    {
        return std::unexpected(
            configuration_failure(configured.error()));
    }
    return ActivatedImageProject{
        std::move(imported->project),
        std::move(updated),
    };
}

auto ImageProjectPersistenceCoordinator::
    delete_named_and_deactivate(
        std::string_view project_id,
        const core::ApplicationConfig& current_config)
    -> std::expected<
        DeletedImageProject,
        ImageProjectPersistenceError>
{
    auto updated = current_config;
    if (updated.active_image_project &&
        updated.active_image_project->project_id == project_id)
    {
        updated.active_image_project.reset();
        const auto configured = configuration_.save(updated);
        if (!configured)
        {
            return std::unexpected(
                configuration_failure(configured.error()));
        }
    }

    auto draft = projects_.load_active_draft();
    if (!draft)
    {
        return std::unexpected(project_failure(draft.error()));
    }
    if (*draft && (*draft)->project_id == project_id)
    {
        const auto cleared = projects_.clear_active_draft();
        if (!cleared)
        {
            return std::unexpected(
                project_failure(cleared.error()));
        }
    }

    const auto deleted = projects_.delete_named(project_id);
    if (!deleted)
    {
        return std::unexpected(project_failure(deleted.error()));
    }
    return DeletedImageProject{
        *deleted,
        std::move(updated),
    };
}

auto ImageProjectPersistenceCoordinator::
    save_active_draft_and_activate(
        const core::ImageProject& project,
        const core::ApplicationConfig& current_config)
    -> std::expected<
        core::ApplicationConfig,
        ImageProjectPersistenceError>
{
    const auto saved = projects_.save_active_draft(project);
    if (!saved)
    {
        return std::unexpected(project_failure(saved.error()));
    }

    auto updated = current_config;
    updated.active_image_project =
        core::ActiveImageProjectReference{
            core::ImageProjectReferenceKind::ActiveDraft,
            project.project_id,
        };
    const auto configured = configuration_.save(updated);
    if (!configured)
    {
        return std::unexpected(
            configuration_failure(configured.error()));
    }
    return updated;
}

ActiveDraftPersistenceWorker::ActiveDraftPersistenceWorker(
    ImageProjectStore& projects,
    std::chrono::milliseconds debounce_interval)
    : projects_{projects},
      debounce_interval_{
          std::max(debounce_interval, std::chrono::milliseconds{1})},
      worker_{[this] { run(); }}
{
}

ActiveDraftPersistenceWorker::~ActiveDraftPersistenceWorker()
{
    shutdown(true);
}

auto ActiveDraftPersistenceWorker::schedule(
    std::shared_ptr<const core::ImageProject> project)
    -> std::expected<std::uint64_t, ActiveDraftScheduleError>
{
    if (!project || !core::validate(*project).empty())
    {
        return std::unexpected(
            ActiveDraftScheduleError::InvalidProject);
    }

    const auto lock = std::scoped_lock{mutex_};
    if (stopping_ || stopped_)
    {
        return std::unexpected(ActiveDraftScheduleError::Stopped);
    }
    if (scheduled_generation_ ==
        std::numeric_limits<std::uint64_t>::max())
    {
        return std::unexpected(
            ActiveDraftScheduleError::GenerationOverflow);
    }

    ++scheduled_generation_;
    pending_generation_ = scheduled_generation_;
    pending_project_ = std::move(project);
    deadline_ =
        std::chrono::steady_clock::now() + debounce_interval_;
    condition_.notify_all();
    return scheduled_generation_;
}

auto ActiveDraftPersistenceWorker::wait_until_idle(
    std::chrono::milliseconds timeout) -> bool
{
    auto lock = std::unique_lock{mutex_};
    return condition_.wait_for(
        lock,
        timeout,
        [this]
        {
            return !pending_project_ && !in_flight_;
        });
}

auto ActiveDraftPersistenceWorker::shutdown(
    bool flush_pending) noexcept -> void
{
    try
    {
        {
            const auto lock = std::scoped_lock{mutex_};
            if (!stopped_)
            {
                stopping_ = true;
                flush_pending_ = flush_pending_ || flush_pending;
                if (!flush_pending_)
                {
                    pending_project_.reset();
                }
                deadline_ = std::chrono::steady_clock::now();
                condition_.notify_all();
            }
        }
        if (worker_.joinable() &&
            worker_.get_id() != std::this_thread::get_id())
        {
            worker_.join();
        }
    }
    catch (...)
    {
    }
}

auto ActiveDraftPersistenceWorker::snapshot() const
    -> ActiveDraftPersistenceSnapshot
{
    const auto lock = std::scoped_lock{mutex_};
    return ActiveDraftPersistenceSnapshot{
        scheduled_generation_,
        completed_generation_,
        static_cast<bool>(pending_project_),
        in_flight_,
        stopped_,
        last_error_,
    };
}

auto ActiveDraftPersistenceWorker::run() noexcept -> void
{
    try
    {
        auto lock = std::unique_lock{mutex_};
        for (;;)
        {
            condition_.wait(
                lock,
                [this]
                {
                    return stopping_ ||
                           static_cast<bool>(pending_project_);
                });

            if (stopping_ && !flush_pending_)
            {
                pending_project_.reset();
                stopped_ = true;
                condition_.notify_all();
                return;
            }
            if (!pending_project_)
            {
                stopped_ = true;
                condition_.notify_all();
                return;
            }

            if (!stopping_)
            {
                const auto observed_deadline = deadline_;
                const auto changed = condition_.wait_until(
                    lock,
                    observed_deadline,
                    [this, observed_deadline]
                    {
                        return stopping_ ||
                               deadline_ != observed_deadline;
                    });
                if (changed)
                {
                    continue;
                }
            }

            auto project = std::move(pending_project_);
            const auto generation = pending_generation_;
            in_flight_ = true;
            lock.unlock();

            auto saved = std::expected<
                void,
                ImageProjectStoreError>{};
            try
            {
                saved = projects_.save_active_draft(*project);
            }
            catch (...)
            {
                saved =
                    std::unexpected(unexpected_worker_failure());
            }

            lock.lock();
            completed_generation_ = generation;
            last_error_ =
                saved
                    ? std::optional<ImageProjectStoreError>{}
                    : std::optional<ImageProjectStoreError>{
                          std::move(saved.error())};
            in_flight_ = false;
            condition_.notify_all();

            if (stopping_ && !pending_project_)
            {
                stopped_ = true;
                condition_.notify_all();
                return;
            }
        }
    }
    catch (...)
    {
        const auto lock = std::scoped_lock{mutex_};
        pending_project_.reset();
        in_flight_ = false;
        stopped_ = true;
        last_error_ = unexpected_worker_failure();
        condition_.notify_all();
    }
}
} // namespace meccha::application
