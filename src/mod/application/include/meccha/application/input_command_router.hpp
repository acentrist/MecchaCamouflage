#pragma once

#include <meccha/application/application_snapshot.hpp>
#include <meccha/application/commands.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <vector>

namespace meccha::application
{
inline constexpr std::size_t MaximumFunctionKeyEventsPerFrame = 64U;

enum class FunctionKeyEventKind : std::uint8_t
{
    Pressed,
    Released,
};

struct FunctionKeyEvent
{
    core::FunctionKey key{core::FunctionKey::F1};
    FunctionKeyEventKind kind{FunctionKeyEventKind::Pressed};

    auto operator==(const FunctionKeyEvent&) const -> bool = default;
};

enum class InputCommandRejectionReason : std::uint8_t
{
    ImageProjectUnavailable,
};

struct InputCommandRejection
{
    core::FunctionKey key{core::FunctionKey::F1};
    InputCommandRejectionReason reason{
        InputCommandRejectionReason::ImageProjectUnavailable};

    auto operator==(const InputCommandRejection&) const
        -> bool = default;
};

struct InputCommandBatch
{
    std::vector<ApplicationCommand> commands{};
    std::vector<InputCommandRejection> rejections{};
    std::size_t suppressed_repeats{};
};

struct InputCommandRouterSnapshot
{
    std::size_t held_key_count{};
    CommandId next_command_id{1U};
    bool command_ids_exhausted{};
    bool stopped{};

    auto operator==(const InputCommandRouterSnapshot&) const
        -> bool = default;
};

enum class InputCommandRouterError : std::uint8_t
{
    InvalidCommandId,
    InvalidSettings,
    InvalidEvent,
    EventLimit,
    CommandOverflow,
    Stopped,
};

class InputCommandRouter
{
public:
    explicit InputCommandRouter(CommandId first_command_id = 1U);

    [[nodiscard]] auto route(
        const ApplicationSnapshot& snapshot,
        std::span<const FunctionKeyEvent> events)
        -> std::expected<
            InputCommandBatch,
            InputCommandRouterError>;

    auto release_all() noexcept -> void;
    auto shutdown() noexcept -> void;

    [[nodiscard]] auto snapshot() const noexcept
        -> InputCommandRouterSnapshot;

private:
    std::array<bool, 24U> held_{};
    CommandId next_command_id_{1U};
    bool command_ids_exhausted_{};
    bool invalid_command_id_{};
    bool stopped_{};
};
} // namespace meccha::application
