#include <meccha/application/input_command_router.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace
{
auto expect(bool condition, std::string_view message) -> bool
{
    if (!condition)
    {
        std::cerr << "FAIL input_command_router: "
                  << message << '\n';
    }
    return condition;
}

constexpr auto ProjectId =
    std::string_view{"0123456789abcdef0123456789abcdef"};
} // namespace

auto main() -> int
{
    using namespace meccha;
    using namespace meccha::application;

    auto passed = true;
    auto snapshot = ApplicationSnapshot{};
    snapshot.settings.paint.brush_size_texels = 7.0;
    snapshot.image_editor.document =
        ImageEditorDocumentSnapshot{
            std::string{ProjectId},
            "Project",
            9U,
        };

    auto router = InputCommandRouter{100U};
    const auto first = router.route(
        snapshot,
        std::array{
            FunctionKeyEvent{
                core::FunctionKey::F9,
                FunctionKeyEventKind::Pressed,
            },
            FunctionKeyEvent{
                core::FunctionKey::F9,
                FunctionKeyEventKind::Pressed,
            },
            FunctionKeyEvent{
                core::FunctionKey::F9,
                FunctionKeyEventKind::Released,
            },
            FunctionKeyEvent{
                core::FunctionKey::F9,
                FunctionKeyEventKind::Pressed,
            },
            FunctionKeyEvent{
                core::FunctionKey::F1,
                FunctionKeyEventKind::Pressed,
            },
            FunctionKeyEvent{
                core::FunctionKey::F2,
                FunctionKeyEventKind::Pressed,
            },
            FunctionKeyEvent{
                core::FunctionKey::F5,
                FunctionKeyEventKind::Pressed,
            },
            FunctionKeyEvent{
                core::FunctionKey::F6,
                FunctionKeyEventKind::Pressed,
            },
        });
    passed &= expect(
        first && first->commands.size() == 6U &&
            first->suppressed_repeats == 1U &&
            first->rejections.empty() &&
            std::get<ToggleUi>(first->commands[0]).id == 100U &&
            std::get<ToggleUi>(first->commands[1]).id == 101U &&
            std::get<StartPaint>(first->commands[2]).settings
                    .brush_size_texels == 7.0 &&
            std::get<PreviewPaint>(first->commands[3]).id == 103U &&
            std::get<StartImagePaint>(first->commands[4]).id ==
                104U &&
            std::get<StartImagePaint>(first->commands[4])
                    .project_id == ProjectId &&
            std::get<StartImagePaint>(first->commands[4])
                    .project_revision == 9U &&
            std::get<PreviewImagePaint>(first->commands[5]).id ==
                105U &&
            std::get<PreviewImagePaint>(first->commands[5])
                    .project_id == ProjectId &&
            std::get<PreviewImagePaint>(first->commands[5])
                    .project_revision == 9U,
        "default hotkeys did not emit ordered typed commands");

    auto no_project = snapshot;
    no_project.image_editor.document.reset();
    const auto unavailable = router.route(
        no_project,
        std::array{
            FunctionKeyEvent{
                core::FunctionKey::F5,
                FunctionKeyEventKind::Released,
            },
            FunctionKeyEvent{
                core::FunctionKey::F5,
                FunctionKeyEventKind::Pressed,
            },
            FunctionKeyEvent{
                core::FunctionKey::F5,
                FunctionKeyEventKind::Pressed,
            },
        });
    passed &= expect(
        unavailable && unavailable->commands.empty() &&
            unavailable->suppressed_repeats == 1U &&
            unavailable->rejections ==
                std::vector<InputCommandRejection>{
                    InputCommandRejection{
                        core::FunctionKey::F5,
                        InputCommandRejectionReason::
                            ImageProjectUnavailable,
                    },
                },
        "unavailable Image Paint hotkeys were not rejected once per press");

    const auto still_held = router.route(
        snapshot,
        std::array{
            FunctionKeyEvent{
                core::FunctionKey::F5,
                FunctionKeyEventKind::Pressed,
            },
        });
    passed &= expect(
        still_held && still_held->commands.empty() &&
            still_held->suppressed_repeats == 1U,
        "a held unavailable key retriggered after project publication");

    snapshot.settings.ui.hotkeys = core::HotkeySettings{
        core::FunctionKey::F10,
        core::FunctionKey::F11,
        core::FunctionKey::F12,
        core::FunctionKey::F13,
        core::FunctionKey::F14,
        core::FunctionKey::F15,
        core::FunctionKey::F16,
        core::FunctionKey::F17,
        core::FunctionKey::F18,
    };
    router.release_all();
    const auto remapped = router.route(
        snapshot,
        std::array{
            FunctionKeyEvent{
                core::FunctionKey::F10,
                FunctionKeyEventKind::Pressed,
            },
            FunctionKeyEvent{
                core::FunctionKey::F11,
                FunctionKeyEventKind::Pressed,
            },
            FunctionKeyEvent{
                core::FunctionKey::F13,
                FunctionKeyEventKind::Pressed,
            },
            FunctionKeyEvent{
                core::FunctionKey::F14,
                FunctionKeyEventKind::Pressed,
            },
            FunctionKeyEvent{
                core::FunctionKey::F17,
                FunctionKeyEventKind::Pressed,
            },
            FunctionKeyEvent{
                core::FunctionKey::F18,
                FunctionKeyEventKind::Pressed,
            },
        });
    passed &= expect(
        remapped && remapped->commands.size() == 6U &&
            std::holds_alternative<ToggleUi>(
                remapped->commands[0]) &&
            std::holds_alternative<StartPaint>(
                remapped->commands[1]) &&
            std::holds_alternative<RestorePaintPreview>(
                remapped->commands[2]) &&
            std::holds_alternative<CancelPaint>(
                remapped->commands[3]) &&
            std::holds_alternative<RestoreImagePaintPreview>(
                remapped->commands[4]) &&
            std::holds_alternative<CancelImagePaint>(
                remapped->commands[5]),
        "validated remapped function keys did not preserve action identity");

    auto invalid_snapshot = snapshot;
    invalid_snapshot.settings.ui.hotkeys.paint_start =
        invalid_snapshot.settings.ui.hotkeys.toggle_ui;
    const auto before_invalid = router.snapshot();
    passed &= expect(
        router.route(
            invalid_snapshot,
            std::span<const FunctionKeyEvent>{}) ==
                std::unexpected(
                    InputCommandRouterError::InvalidSettings) &&
            router.snapshot() == before_invalid,
        "invalid hotkey settings mutated router state");

    auto oversized = std::vector<FunctionKeyEvent>(
        MaximumFunctionKeyEventsPerFrame + 1U);
    passed &= expect(
        router.route(snapshot, oversized) ==
                std::unexpected(
                    InputCommandRouterError::EventLimit) &&
            router.route(
                snapshot,
                std::array{
                    FunctionKeyEvent{
                        static_cast<core::FunctionKey>(0U),
                        FunctionKeyEventKind::Pressed,
                    },
                }) ==
                std::unexpected(
                    InputCommandRouterError::InvalidEvent),
        "invalid or oversized input entered the router");

    auto overflow = InputCommandRouter{
        std::numeric_limits<CommandId>::max()};
    const auto last = overflow.route(
        snapshot,
        std::array{
            FunctionKeyEvent{
                core::FunctionKey::F10,
                FunctionKeyEventKind::Pressed,
            },
        });
    overflow.release_all();
    passed &= expect(
        last && last->commands.size() == 1U &&
            std::get<ToggleUi>(last->commands.front()).id ==
                std::numeric_limits<CommandId>::max() &&
            overflow.route(
                snapshot,
                std::array{
                    FunctionKeyEvent{
                        core::FunctionKey::F10,
                        FunctionKeyEventKind::Pressed,
                    },
                }) ==
                std::unexpected(
                    InputCommandRouterError::CommandOverflow),
        "command ID exhaustion wrapped or discarded the final ID");

    router.shutdown();
    passed &= expect(
        router.snapshot().stopped &&
            router.snapshot().held_key_count == 0U &&
            router.route(
                snapshot,
                std::span<const FunctionKeyEvent>{}) ==
                std::unexpected(
                    InputCommandRouterError::Stopped),
        "terminal input shutdown retained key or command admission");

    if (passed)
    {
        std::cout << "PASS input_command_router\n";
    }
    return passed ? 0 : 1;
}
