#pragma once

#include <meccha/application/paint_dispatch.hpp>
#include <meccha/application/runtime_operation_executor.hpp>
#include <meccha/core/image_paint_plan.hpp>

#include <expected>
#include <vector>

namespace meccha::application
{
struct CapturedImagePaintJob
{
    RuntimeObjectHandle component{};
    core::MeshProfileIdentity raw_profile{};
    core::CanonicalImageProfile image_profile{};
    std::vector<core::CapturedImagePaintSample> samples{};
    core::ReplicationPacingPlan pacing{};
};

class ImagePaintGameRuntimePort
{
public:
    ImagePaintGameRuntimePort() = default;
    ImagePaintGameRuntimePort(
        const ImagePaintGameRuntimePort&) = delete;
    auto operator=(const ImagePaintGameRuntimePort&)
        -> ImagePaintGameRuntimePort& = delete;
    virtual ~ImagePaintGameRuntimePort() = default;

    virtual auto capture(core::BodyProfile body)
        -> std::expected<
            CapturedImagePaintJob,
            RuntimeExecutionError> = 0;

    virtual auto observe_queues(
        RuntimeObjectHandle component,
        JobGeneration generation)
        -> std::expected<
            PaintQueueObservation,
            RuntimeExecutionError> = 0;
};
} // namespace meccha::application
