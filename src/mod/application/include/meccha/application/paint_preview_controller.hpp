#pragma once

#include <meccha/application/job_state.hpp>
#include <meccha/application/runtime_operation_executor.hpp>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <vector>

namespace meccha::application
{
struct PaintTextureImage
{
    std::uint32_t dimension{};
    std::shared_ptr<const std::vector<std::byte>> albedo_rgba{};
    std::shared_ptr<const std::vector<std::byte>> packed_pbr_rgba{};
};

struct PaintPreviewSnapshot
{
    RuntimeObjectHandle component{};
    PaintTextureImage original{};
};

class PaintPreviewRuntimePort
{
public:
    PaintPreviewRuntimePort() = default;
    PaintPreviewRuntimePort(const PaintPreviewRuntimePort&) = delete;
    auto operator=(const PaintPreviewRuntimePort&)
        -> PaintPreviewRuntimePort& = delete;
    PaintPreviewRuntimePort(PaintPreviewRuntimePort&&) = default;
    auto operator=(PaintPreviewRuntimePort&&)
        -> PaintPreviewRuntimePort& = default;
    virtual ~PaintPreviewRuntimePort() = default;

    virtual auto capture(RuntimeObjectHandle component)
        -> std::expected<
            PaintPreviewSnapshot,
            RuntimeExecutionError> = 0;

    virtual auto apply(
        RuntimeObjectHandle component,
        const PaintTextureImage& image)
        -> std::expected<void, RuntimeExecutionError> = 0;

    virtual auto restore(const PaintPreviewSnapshot& snapshot)
        -> std::expected<void, RuntimeExecutionError> = 0;
};

enum class PaintPreviewAcquire : std::uint8_t
{
    Created,
    Reused,
    Replaced,
};

enum class PaintPreviewRestore : std::uint8_t
{
    Restored,
    NoPreview,
    WrongComponent,
};

enum class PaintPreviewErrorCode : std::uint8_t
{
    WrongThread,
    InvalidComponent,
    InvalidImage,
    InvalidSnapshot,
    NoPreview,
    WrongFeature,
    CaptureFailure,
    ApplyFailureRecovered,
    ApplyFailureRestorePending,
    RestoreFailure,
    StateFailure,
};

struct PaintPreviewError
{
    PaintPreviewErrorCode code{};
    std::optional<RuntimeExecutionError> runtime_error{};
    std::optional<RuntimeExecutionError> recovery_error{};
};

class PaintPreviewController
{
public:
    PaintPreviewController(
        GameThreadContext& thread_context,
        PaintPreviewRuntimePort& runtime,
        PreviewStateMachine& leases);

    [[nodiscard]] auto begin(
        Feature feature,
        RuntimeObjectHandle component)
        -> std::expected<
            PaintPreviewAcquire,
            PaintPreviewError>;

    [[nodiscard]] auto apply(
        Feature feature,
        RuntimeObjectHandle component,
        const PaintTextureImage& image)
        -> std::expected<void, PaintPreviewError>;

    [[nodiscard]] auto source(
        Feature feature,
        RuntimeObjectHandle component)
        -> std::expected<PaintTextureImage, PaintPreviewError>;

    [[nodiscard]] auto restore(RuntimeObjectHandle component)
        -> std::expected<
            PaintPreviewRestore,
            PaintPreviewError>;

    [[nodiscard]] auto restore_active()
        -> std::expected<
            PaintPreviewRestore,
            PaintPreviewError>;

    auto expire_invalid_component(
        RuntimeObjectHandle component) -> bool;

private:
    [[nodiscard]] auto valid_image(
        const PaintTextureImage& image) const -> bool;
    [[nodiscard]] auto lease_consistent() const -> bool;
    auto release_restored_lease() -> bool;

    GameThreadContext& thread_context_;
    PaintPreviewRuntimePort& runtime_;
    PreviewStateMachine& leases_;
    std::optional<PaintPreviewSnapshot> snapshot_{};
};
} // namespace meccha::application
