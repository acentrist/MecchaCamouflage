#include <meccha/application/input_command_router.hpp>

#include <algorithm>
#include <limits>
#include <optional>
#include <utility>

namespace meccha::application
{
namespace
{
enum class InputAction : std::uint8_t
{
    ToggleUi,
    StartPaint,
    PreviewPaint,
    RestorePaintPreview,
    CancelPaint,
    StartImagePaint,
    PreviewImagePaint,
    RestoreImagePaintPreview,
    CancelImagePaint,
};

struct PendingInputAction
{
    InputAction action{InputAction::ToggleUi};
};

auto key_index(core::FunctionKey key)
    -> std::optional<std::size_t>
{
    const auto value = static_cast<std::uint8_t>(key);
    if (value <
            static_cast<std::uint8_t>(
                core::FunctionKey::F1) ||
        value >
            static_cast<std::uint8_t>(
                core::FunctionKey::F24))
    {
        return std::nullopt;
    }
    return static_cast<std::size_t>(value - 1U);
}

auto valid_event_kind(FunctionKeyEventKind kind) -> bool
{
    return kind == FunctionKeyEventKind::Pressed ||
           kind == FunctionKeyEventKind::Released;
}

auto action_for(
    core::FunctionKey key,
    const core::HotkeySettings& hotkeys)
    -> std::optional<InputAction>
{
    if (key == hotkeys.toggle_ui)
    {
        return InputAction::ToggleUi;
    }
    if (key == hotkeys.paint_start)
    {
        return InputAction::StartPaint;
    }
    if (key == hotkeys.paint_preview)
    {
        return InputAction::PreviewPaint;
    }
    if (key == hotkeys.paint_restore)
    {
        return InputAction::RestorePaintPreview;
    }
    if (key == hotkeys.paint_cancel)
    {
        return InputAction::CancelPaint;
    }
    if (key == hotkeys.image_start)
    {
        return InputAction::StartImagePaint;
    }
    if (key == hotkeys.image_preview)
    {
        return InputAction::PreviewImagePaint;
    }
    if (key == hotkeys.image_restore)
    {
        return InputAction::RestoreImagePaintPreview;
    }
    if (key == hotkeys.image_cancel)
    {
        return InputAction::CancelImagePaint;
    }
    return std::nullopt;
}

auto image_project_available(
    const ApplicationSnapshot& snapshot) -> bool
{
    return snapshot.image_editor.document &&
           core::valid_image_project_id(
               snapshot.image_editor.document->project_id) &&
           snapshot.image_editor.document->revision != 0U;
}

auto requires_image_project(InputAction action) -> bool
{
    return action == InputAction::StartImagePaint ||
           action == InputAction::PreviewImagePaint;
}

auto make_command(
    InputAction action,
    CommandId command_id,
    const ApplicationSnapshot& snapshot)
    -> ApplicationCommand
{
    switch (action)
    {
    case InputAction::ToggleUi:
        return ToggleUi{command_id};
    case InputAction::StartPaint:
        return StartPaint{
            command_id,
            snapshot.settings.paint,
        };
    case InputAction::PreviewPaint:
        return PreviewPaint{
            command_id,
            snapshot.settings.paint,
        };
    case InputAction::RestorePaintPreview:
        return RestorePaintPreview{command_id};
    case InputAction::CancelPaint:
        return CancelPaint{command_id};
    case InputAction::StartImagePaint:
        return StartImagePaint{
            command_id,
            snapshot.image_editor.document->project_id,
            snapshot.image_editor.document->revision,
        };
    case InputAction::PreviewImagePaint:
        return PreviewImagePaint{
            command_id,
            snapshot.image_editor.document->project_id,
            snapshot.image_editor.document->revision,
        };
    case InputAction::RestoreImagePaintPreview:
        return RestoreImagePaintPreview{command_id};
    case InputAction::CancelImagePaint:
        return CancelImagePaint{command_id};
    }
    return ToggleUi{command_id};
}
} // namespace

InputCommandRouter::InputCommandRouter(
    CommandId first_command_id)
    : next_command_id_{first_command_id},
      invalid_command_id_{first_command_id == 0U}
{
}

auto InputCommandRouter::route(
    const ApplicationSnapshot& snapshot,
    std::span<const FunctionKeyEvent> events)
    -> std::expected<
        InputCommandBatch,
        InputCommandRouterError>
{
    if (stopped_)
    {
        return std::unexpected(
            InputCommandRouterError::Stopped);
    }
    if (invalid_command_id_)
    {
        return std::unexpected(
            InputCommandRouterError::InvalidCommandId);
    }
    if (!core::validate(snapshot.settings).empty())
    {
        return std::unexpected(
            InputCommandRouterError::InvalidSettings);
    }
    if (events.size() > MaximumFunctionKeyEventsPerFrame)
    {
        return std::unexpected(
            InputCommandRouterError::EventLimit);
    }
    if (std::ranges::any_of(
            events,
            [](const FunctionKeyEvent& event)
            {
                return !key_index(event.key) ||
                       !valid_event_kind(event.kind);
            }))
    {
        return std::unexpected(
            InputCommandRouterError::InvalidEvent);
    }

    auto next_held = held_;
    auto pending = std::vector<PendingInputAction>{};
    auto batch = InputCommandBatch{};
    pending.reserve(events.size());
    batch.rejections.reserve(events.size());
    for (const auto& event : events)
    {
        const auto index = *key_index(event.key);
        if (event.kind == FunctionKeyEventKind::Released)
        {
            next_held[index] = false;
            continue;
        }
        if (next_held[index])
        {
            ++batch.suppressed_repeats;
            continue;
        }
        next_held[index] = true;

        const auto action = action_for(
            event.key,
            snapshot.settings.ui.hotkeys);
        if (!action)
        {
            continue;
        }
        if (requires_image_project(*action) &&
            !image_project_available(snapshot))
        {
            batch.rejections.push_back(
                InputCommandRejection{
                    event.key,
                    InputCommandRejectionReason::
                        ImageProjectUnavailable,
                });
            continue;
        }
        pending.push_back(PendingInputAction{*action});
    }

    if (!pending.empty())
    {
        if (command_ids_exhausted_)
        {
            return std::unexpected(
                InputCommandRouterError::CommandOverflow);
        }
        const auto remaining =
            std::numeric_limits<CommandId>::max() -
            next_command_id_ + 1U;
        if (pending.size() > remaining)
        {
            return std::unexpected(
                InputCommandRouterError::CommandOverflow);
        }
    }

    batch.commands.reserve(pending.size());
    for (const auto& item : pending)
    {
        batch.commands.push_back(make_command(
            item.action,
            next_command_id_,
            snapshot));
        if (next_command_id_ ==
            std::numeric_limits<CommandId>::max())
        {
            command_ids_exhausted_ = true;
        }
        else
        {
            ++next_command_id_;
        }
    }
    held_ = next_held;
    return batch;
}

auto InputCommandRouter::release_all() noexcept -> void
{
    held_.fill(false);
}

auto InputCommandRouter::shutdown() noexcept -> void
{
    release_all();
    stopped_ = true;
}

auto InputCommandRouter::snapshot() const noexcept
    -> InputCommandRouterSnapshot
{
    return {
        static_cast<std::size_t>(
            std::ranges::count(held_, true)),
        next_command_id_,
        command_ids_exhausted_,
        stopped_,
    };
}
} // namespace meccha::application
