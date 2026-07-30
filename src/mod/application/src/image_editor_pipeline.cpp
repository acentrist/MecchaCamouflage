#include <meccha/application/image_editor_pipeline.hpp>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <utility>
#include <vector>

namespace meccha::application
{
ImageEditorPipeline::ImageEditorPipeline(
    ImageSourceDecoder& decoder,
    ImageAtlasComposer& composer)
    : decode_worker_{decoder},
      composition_worker_{composer}
{
}

ImageEditorPipeline::~ImageEditorPipeline()
{
    shutdown();
}

auto ImageEditorPipeline::submit(core::ImageProject project)
    -> std::expected<JobGeneration, ImageEditorSubmitError>
{
    if (stopped_)
    {
        return std::unexpected(ImageEditorSubmitError::Stopped);
    }
    if (!core::validate(project).empty())
    {
        return std::unexpected(
            ImageEditorSubmitError::InvalidProject);
    }
    if (!snapshot_.project_id.empty() &&
        snapshot_.project_id == project.project_id &&
        project.revision <= snapshot_.project_revision)
    {
        return std::unexpected(
            ImageEditorSubmitError::StaleRevision);
    }
    if (next_generation_ == 0U)
    {
        return std::unexpected(
            ImageEditorSubmitError::GenerationOverflow);
    }

    const auto generation = next_generation_;
    if (next_generation_ ==
        std::numeric_limits<JobGeneration>::max())
    {
        next_generation_ = 0U;
    }
    else
    {
        ++next_generation_;
    }

    auto submitted_project =
        std::make_shared<const core::ImageProject>(
            std::move(project));
    ready_project_.reset();
    if (work_active_)
    {
        pending_project_ = std::move(submitted_project);
        pending_generation_ = generation;
        snapshot_ = ImageEditorPipelineSnapshot{
            active_phase_,
            pending_generation_,
            pending_project_->project_id,
            pending_project_->revision,
            true,
            std::nullopt,
        };
        if (active_phase_ == ImageEditorPipelinePhase::Decoding)
        {
            static_cast<void>(
                decode_worker_.request_cancel(active_generation_));
        }
        else
        {
            static_cast<void>(
                composition_worker_.request_cancel(
                    active_generation_));
        }
        return generation;
    }

    if (!start(generation, std::move(submitted_project)))
    {
        return std::unexpected(ImageEditorSubmitError::DecodeStart);
    }
    return generation;
}

auto ImageEditorPipeline::update() -> void
{
    if (active_phase_ == ImageEditorPipelinePhase::Decoding)
    {
        auto completed = decode_worker_.poll();
        if (!completed)
        {
            return;
        }
        if (pending_project_)
        {
            static_cast<void>(start_pending());
            return;
        }
        if (completed->generation != active_generation_ ||
            completed->project_id != active_project_->project_id ||
            completed->project_revision !=
                active_project_->revision)
        {
            fail(ImageEditorPipelineFailure{
                ImageEditorPipelineFailureKind::InvalidCompletion,
            });
            return;
        }
        if (!completed->result)
        {
            fail(ImageEditorPipelineFailure{
                ImageEditorPipelineFailureKind::Decode,
                completed->result.error(),
            });
            return;
        }

        auto request = ImageCompositionRequest{
            active_project_->project_id,
            active_project_->revision,
            active_project_->settings,
            active_project_->layers,
            std::vector<core::DecodedImageSource>{
                completed->result.value()->begin(),
                completed->result.value()->end(),
            },
        };
        if (!composition_worker_.start(
                active_generation_,
                std::move(request)))
        {
            fail(ImageEditorPipelineFailure{
                ImageEditorPipelineFailureKind::InvalidCompletion,
            });
            return;
        }
        active_phase_ = ImageEditorPipelinePhase::Composing;
        snapshot_.phase = active_phase_;
    }

    if (active_phase_ != ImageEditorPipelinePhase::Composing)
    {
        return;
    }

    auto completed = composition_worker_.poll();
    if (!completed)
    {
        return;
    }
    if (pending_project_)
    {
        static_cast<void>(start_pending());
        return;
    }
    if (completed->generation != active_generation_ ||
        completed->project_id != active_project_->project_id ||
        completed->project_revision != active_project_->revision)
    {
        fail(ImageEditorPipelineFailure{
            ImageEditorPipelineFailureKind::InvalidCompletion,
        });
        return;
    }
    if (!completed->result)
    {
        fail(ImageEditorPipelineFailure{
            ImageEditorPipelineFailureKind::Composition,
            std::nullopt,
            completed->result.error(),
        });
        return;
    }

    const auto composition = completed->result.value();
    if (composition->rgba.size() !=
        core::CanonicalAtlasByteLength)
    {
        fail(ImageEditorPipelineFailure{
            ImageEditorPipelineFailureKind::InvalidCompletion,
        });
        return;
    }

    auto ready = std::make_shared<core::ImageProject>(
        *active_project_);
    ready->canonical_atlas =
        std::shared_ptr<const std::vector<std::byte>>{
            composition,
            &composition->rgba,
        };
    if (!core::validate(*ready).empty())
    {
        fail(ImageEditorPipelineFailure{
            ImageEditorPipelineFailureKind::InvalidCompletion,
        });
        return;
    }
    ready_project_ = std::move(ready);
    work_active_ = false;
    active_phase_ = ImageEditorPipelinePhase::Ready;
    snapshot_.phase = ImageEditorPipelinePhase::Ready;
    snapshot_.failure.reset();
}

auto ImageEditorPipeline::shutdown() noexcept -> void
{
    if (stopped_)
    {
        return;
    }
    stopped_ = true;
    decode_worker_.shutdown();
    composition_worker_.shutdown();
    active_project_.reset();
    pending_project_.reset();
    ready_project_.reset();
    active_generation_ = 0U;
    pending_generation_ = 0U;
    work_active_ = false;
    active_phase_ = ImageEditorPipelinePhase::Stopped;
    snapshot_.phase = ImageEditorPipelinePhase::Stopped;
    snapshot_.pending = false;
}

auto ImageEditorPipeline::snapshot() const
    -> ImageEditorPipelineSnapshot
{
    return snapshot_;
}

auto ImageEditorPipeline::ready_project(
    std::string_view project_id,
    std::uint64_t project_revision) const
    -> std::shared_ptr<const core::ImageProject>
{
    if (!ready_project_ ||
        ready_project_->project_id != project_id ||
        ready_project_->revision != project_revision)
    {
        return {};
    }
    return ready_project_;
}

auto ImageEditorPipeline::start(
    JobGeneration generation,
    std::shared_ptr<const core::ImageProject> project) -> bool
{
    auto request = ImageProjectDecodeRequest{
        project->project_id,
        project->revision,
        project->sources,
    };
    if (!decode_worker_.start(generation, std::move(request)))
    {
        work_active_ = false;
        active_phase_ = ImageEditorPipelinePhase::Failed;
        snapshot_ = ImageEditorPipelineSnapshot{
            ImageEditorPipelinePhase::Failed,
            generation,
            project->project_id,
            project->revision,
            false,
            ImageEditorPipelineFailure{
                ImageEditorPipelineFailureKind::InvalidCompletion,
            },
        };
        return false;
    }

    active_project_ = std::move(project);
    active_generation_ = generation;
    active_phase_ = ImageEditorPipelinePhase::Decoding;
    work_active_ = true;
    snapshot_ = ImageEditorPipelineSnapshot{
        active_phase_,
        active_generation_,
        active_project_->project_id,
        active_project_->revision,
        false,
        std::nullopt,
    };
    return true;
}

auto ImageEditorPipeline::start_pending() -> bool
{
    auto project = std::move(pending_project_);
    const auto generation = pending_generation_;
    pending_project_.reset();
    pending_generation_ = 0U;
    work_active_ = false;
    return start(generation, std::move(project));
}

auto ImageEditorPipeline::fail(
    ImageEditorPipelineFailure failure) -> void
{
    ready_project_.reset();
    work_active_ = false;
    active_phase_ = ImageEditorPipelinePhase::Failed;
    snapshot_.phase = ImageEditorPipelinePhase::Failed;
    snapshot_.pending = false;
    snapshot_.failure = std::move(failure);
}
} // namespace meccha::application
