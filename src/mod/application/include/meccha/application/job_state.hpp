#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

namespace meccha::application
{
using CommandId = std::uint64_t;
using JobGeneration = std::uint64_t;

enum class Feature : std::uint8_t
{
    Paint,
    ImagePaint,
};

enum class JobPhase : std::uint8_t
{
    Idle,
    Planning,
    Dispatching,
    Cancelling,
    Draining,
    Completed,
    Cancelled,
    Failed,
};

struct JobProgress
{
    std::size_t fill_count{};
    std::size_t paint_count{};
    std::size_t total{};
    std::size_t submitted{};
    std::size_t visual_pending{};
    std::size_t outgoing_pending{};
    double queue_pressure{};
    std::uint64_t elapsed_ms{};
    std::optional<std::uint64_t> eta_ms{};

    auto operator==(const JobProgress&) const -> bool = default;
};

struct JobSnapshot
{
    std::uint64_t revision{};
    JobPhase phase{JobPhase::Idle};
    std::optional<Feature> feature{};
    std::optional<CommandId> command_id{};
    JobGeneration generation{};
    JobProgress progress{};

    auto operator==(const JobSnapshot&) const -> bool = default;
};

enum class JobMutationResult : std::uint8_t
{
    Applied,
    Busy,
    StaleGeneration,
    InvalidState,
    InvalidProgress,
    PendingAdmission,
    PendingQueueDrain,
};

class JobStateMachine
{
public:
    [[nodiscard]] auto start(Feature feature, CommandId command_id)
        -> JobMutationResult;

    [[nodiscard]] auto planning_ready(
        JobGeneration generation,
        std::size_t fill_count,
        std::size_t paint_count) -> JobMutationResult;

    [[nodiscard]] auto dispatch_progress(
        JobGeneration generation,
        std::size_t submitted,
        std::size_t visual_pending,
        std::size_t outgoing_pending,
        double queue_pressure,
        std::uint64_t elapsed_ms,
        std::optional<std::uint64_t> eta_ms)
        -> JobMutationResult;

    [[nodiscard]] auto begin_drain(JobGeneration generation)
        -> JobMutationResult;

    [[nodiscard]] auto complete_if_drained(
        JobGeneration generation,
        bool visual_confirmation_complete) -> JobMutationResult;

    [[nodiscard]] auto request_cancel(JobGeneration generation)
        -> JobMutationResult;

    [[nodiscard]] auto acknowledge_cancel(
        JobGeneration generation,
        bool native_admission_active,
        std::size_t visual_pending,
        std::size_t outgoing_pending) -> JobMutationResult;

    [[nodiscard]] auto fail(JobGeneration generation)
        -> JobMutationResult;

    [[nodiscard]] auto snapshot() const -> JobSnapshot;

private:
    [[nodiscard]] auto generation_matches(
        JobGeneration generation) const -> bool;
    auto mutate() -> void;

    JobSnapshot snapshot_{};
};

enum class PreviewAcquireResult : std::uint8_t
{
    Created,
    Reused,
    Replaced,
    InvalidComponent,
};

enum class PreviewRestoreResult : std::uint8_t
{
    Restored,
    NoLease,
    WrongComponent,
};

struct PreviewLeaseSnapshot
{
    std::uint64_t revision{};
    std::optional<Feature> feature{};
    std::uint64_t component_identity{};
    std::uint64_t lease_generation{};

    auto operator==(const PreviewLeaseSnapshot&) const -> bool = default;
};

class PreviewStateMachine
{
public:
    [[nodiscard]] auto acquire(
        Feature feature,
        std::uint64_t component_identity)
        -> PreviewAcquireResult;

    [[nodiscard]] auto restore(std::uint64_t component_identity)
        -> PreviewRestoreResult;

    auto invalidate_component(std::uint64_t component_identity) -> bool;

    [[nodiscard]] auto snapshot() const -> PreviewLeaseSnapshot;

private:
    PreviewLeaseSnapshot snapshot_{};
};
} // namespace meccha::application
