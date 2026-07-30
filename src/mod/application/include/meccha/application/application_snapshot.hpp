#pragma once

#include <meccha/application/game_thread_scheduler.hpp>
#include <meccha/application/job_state.hpp>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace meccha::application
{
enum class CompatibilityStatus : std::uint8_t
{
    Unknown,
    Compatible,
    UnsupportedGame,
    RuntimeError,
};

enum class RuntimeContractId : std::uint8_t
{
    RuntimeInitialization,
    HudCallback,
    World,
    PlayerController,
    Hud,
    Canvas,
    PaintAtUvWithBrush,
    ImagePaintTexture,
    TextureMutation,
    InputControl,
};

enum class ContractFailureKind : std::uint8_t
{
    MissingObject,
    WrongClass,
    MissingProperty,
    WrongPropertyKind,
    MissingFunction,
    ParameterSizeMismatch,
    StaleObject,
    CallbackFailure,
    ExecutionFailure,
    UnsupportedGameBuild,
};

struct CompatibilityFailure
{
    RuntimeContractId contract{};
    ContractFailureKind kind{};
    std::string message_key{};

    auto operator==(const CompatibilityFailure&) const -> bool = default;
};

struct CompatibilitySnapshot
{
    CompatibilityStatus status{CompatibilityStatus::Unknown};
    std::optional<CompatibilityFailure> failure{};

    auto operator==(const CompatibilitySnapshot&) const -> bool = default;
};

class CompatibilityState
{
public:
    auto mark_compatible() -> void;
    auto fail(CompatibilityFailure failure) -> void;

    [[nodiscard]] auto snapshot() const -> CompatibilitySnapshot;

private:
    CompatibilitySnapshot snapshot_{};
};

enum class DiagnosticSeverity : std::uint8_t
{
    Information,
    Warning,
    Error,
};

struct DiagnosticEntry
{
    std::uint64_t sequence{};
    DiagnosticSeverity severity{};
    std::string message_key{};
    std::optional<CommandId> command_id{};
    std::optional<CompatibilityFailure> compatibility_failure{};

    auto operator==(const DiagnosticEntry&) const -> bool = default;
};

class BoundedDiagnostics
{
public:
    explicit BoundedDiagnostics(std::size_t capacity);

    auto push(
        DiagnosticSeverity severity,
        std::string message_key,
        std::optional<CommandId> command_id = std::nullopt,
        std::optional<CompatibilityFailure> compatibility_failure =
            std::nullopt) -> void;

    [[nodiscard]] auto entries() const -> std::vector<DiagnosticEntry>;

private:
    const std::size_t capacity_{};
    std::uint64_t next_sequence_{1U};
    std::vector<DiagnosticEntry> entries_{};
};

struct ApplicationSnapshot
{
    std::uint64_t revision{};
    bool ui_open{};
    bool esp_enabled{true};
    JobSnapshot job{};
    PreviewLeaseSnapshot preview{};
    QueueSnapshot runtime_queue{};
    CompatibilitySnapshot compatibility{};
    std::vector<DiagnosticEntry> diagnostics{};

    auto operator==(const ApplicationSnapshot&) const -> bool = default;
};

class SnapshotPublisher
{
public:
    SnapshotPublisher();
    SnapshotPublisher(const SnapshotPublisher&) = delete;
    auto operator=(const SnapshotPublisher&) -> SnapshotPublisher& = delete;

    auto publish(ApplicationSnapshot snapshot) -> void;

    [[nodiscard]] auto read() const
        -> std::shared_ptr<const ApplicationSnapshot>;

private:
    mutable std::mutex publish_mutex_{};
    std::atomic<std::shared_ptr<const ApplicationSnapshot>> current_{};
};
} // namespace meccha::application
