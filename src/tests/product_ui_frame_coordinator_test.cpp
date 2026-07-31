#include <meccha/product_ui/product_ui_frame_coordinator.hpp>

#include <meccha/application/image_project_codec.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace
{
using namespace meccha;
using namespace meccha::application;
using namespace meccha::product_ui;

auto expect(bool condition, std::string_view message) -> bool
{
    if (!condition)
    {
        std::cerr << "FAIL product_ui_frame_coordinator: "
                  << message << '\n';
    }
    return condition;
}

auto read_file(const std::filesystem::path& path) -> std::string
{
    auto input = std::ifstream{path, std::ios::binary};
    return {
        std::istreambuf_iterator<char>{input},
        std::istreambuf_iterator<char>{},
    };
}

auto center(const ui::CanvasRect& rect) -> ui::CanvasPoint
{
    return {
        rect.x + rect.width * 0.5,
        rect.y + rect.height * 0.5,
    };
}

auto snapshot(bool open) -> ApplicationSnapshot
{
    auto value = ApplicationSnapshot{};
    value.revision = open ? 18U : 17U;
    value.runtime_phase = ApplicationRuntimePhase::Compatible;
    value.compatibility.status =
        CompatibilityStatus::Compatible;
    value.ui_open = open;
    value.command_queue = {0U, 32U, true};
    value.runtime_queue = {0U, 32U, true};
    return value;
}

constexpr auto ProjectId =
    std::string_view{"0123456789abcdef0123456789abcdef"};
constexpr auto AssetId =
    std::string_view{
        "11111111111111111111111111111111"
        "11111111111111111111111111111111"};

auto ready_snapshot() -> ApplicationSnapshot
{
    auto value = snapshot(true);
    value.image_editor.document =
        ImageEditorDocumentSnapshot{
            std::string{ProjectId},
            "Project",
            9U,
            core::ImageProjectSettings{},
            {core::ImageLayer{
                std::string{AssetId},
                "source.png",
                core::ImageMime::Png,
                1U,
            }},
        };
    value.image_editor.pipeline =
        ImageEditorPipelineSnapshot{
            ImageEditorPipelinePhase::Ready,
            4U,
            std::string{ProjectId},
            9U,
        };
    return value;
}

auto guide(core::BodyProfile body) -> core::ImageGuideBitmap
{
    auto rgba = std::vector<std::byte>(
        core::CanonicalAtlasByteLength,
        std::byte{});
    rgba[3U] = std::byte{0xFF};
    return {
        core::ImageGuideSchemaVersion,
        core::expected_mesh_profile(
            body,
            core::MeshProfileRole::ImageReference),
        core::CanonicalAtlasWidth,
        core::CanonicalAtlasHeight,
        std::make_shared<const std::vector<std::byte>>(
            std::move(rgba)),
        1U,
        1U,
    };
}

auto ready_content()
    -> std::shared_ptr<const ImageEditorReadyContent>
{
    const auto encoded =
        std::make_shared<const std::vector<std::byte>>(
            std::initializer_list<std::byte>{
                std::byte{0x11},
            });
    const auto atlas =
        std::make_shared<const std::vector<std::byte>>(
            core::CanonicalAtlasByteLength,
            std::byte{0x22});
    const auto project =
        std::make_shared<const core::ImageProject>(
            core::ImageProject{
                core::ImageProjectSchemaVersion,
                std::string{ProjectId},
                "Project",
                9U,
                core::ImageProjectSettings{},
                {core::ImageLayer{
                    std::string{AssetId},
                    "source.png",
                    core::ImageMime::Png,
                    1U,
                }},
                {core::ImageSourceAsset{
                    std::string{AssetId},
                    core::ImageMime::Png,
                    encoded,
                }},
                atlas,
            });
    const auto decoded =
        std::make_shared<
            const std::vector<core::DecodedImageSource>>(
            std::initializer_list<core::DecodedImageSource>{
                core::DecodedImageSource{
                    std::string{AssetId},
                    1U,
                    1U,
                    std::make_shared<
                        const std::vector<std::byte>>(
                        4U,
                        std::byte{0x33}),
                },
            });
    return std::make_shared<const ImageEditorReadyContent>(
        ImageEditorReadyContent{project, decoded});
}

class ReadyContentPort final : public ImageEditorReadyContentPort
{
public:
    auto ready_content(
        std::string_view project_id,
        std::uint64_t project_revision) const
        -> std::shared_ptr<const ImageEditorReadyContent> override
    {
        if (!value || !value->project ||
            value->project->project_id != project_id ||
            value->project->revision != project_revision)
        {
            return {};
        }
        return value;
    }

    std::shared_ptr<const ImageEditorReadyContent> value{
        ::ready_content()};
};

class ThreadContext final : public GameThreadContext
{
public:
    auto is_game_thread() const noexcept -> bool override
    {
        return true;
    }
};

class TexturePort final : public ImageEditorTextureRuntimePort
{
public:
    auto create_texture(const ImageEditorTextureUpload&)
        -> std::expected<
            ui::CanvasTextureHandle,
            ImageEditorTextureRuntimeError> override
    {
        const auto handle =
            ui::CanvasTextureHandle{next_identity++};
        live.insert(handle.identity);
        return handle;
    }

    auto release_texture(ui::CanvasTextureHandle handle)
        -> std::expected<
            void,
            ImageEditorTextureRuntimeError> override
    {
        if (live.erase(handle.identity) != 1U)
        {
            return std::unexpected(
                ImageEditorTextureRuntimeError{
                    "unknown handle",
                });
        }
        return {};
    }

    std::uint64_t next_identity{1U};
    std::set<std::uint64_t> live{};
};

class SnapshotPort final : public ApplicationSnapshotPort
{
public:
    auto snapshot() const
        -> std::shared_ptr<const ApplicationSnapshot> override
    {
        return value;
    }

    std::shared_ptr<const ApplicationSnapshot> value{
        std::make_shared<const ApplicationSnapshot>(
            ::snapshot(false))};
};

class CommandSink final : public ApplicationCommandSink
{
public:
    auto enqueue_command(ApplicationCommand command)
        -> CommandEnqueueResult override
    {
        commands.push_back(std::move(command));
        return next_result;
    }

    std::vector<ApplicationCommand> commands{};
    CommandEnqueueResult next_result{
        CommandEnqueueResult::Accepted};
};

class FrameRuntime final : public ProductUiFrameRuntimePort
{
public:
    auto capture(const HudFrameIdentity& identity)
        -> std::expected<
            ProductUiFrameInput,
            ProductUiFrameRuntimeError> override
    {
        ++capture_calls;
        captured_identities.push_back(identity);
        return input;
    }

    auto render(
        const HudFrameIdentity& identity,
        const ui::CanvasFrame& frame)
        -> std::expected<
            void,
            ProductUiFrameRuntimeError> override
    {
        ++render_calls;
        rendered_identities.push_back(identity);
        last_frame = frame;
        if (fail_render)
        {
            return std::unexpected(
                ProductUiFrameRuntimeError{
                    "render failed",
                });
        }
        return {};
    }

    ProductUiFrameInput input{
        ui::CanvasViewport{1920.0, 1080.0, 1.0},
    };
    ui::CanvasFrame last_frame{};
    std::vector<HudFrameIdentity> captured_identities{};
    std::vector<HudFrameIdentity> rendered_identities{};
    std::size_t capture_calls{};
    std::size_t render_calls{};
    bool fail_render{};
};

class InputPort final : public ui::InputLeasePort
{
public:
    auto capture()
        -> std::expected<
            ui::RuntimeInputState,
            ui::InputPortError> override
    {
        ++capture_calls;
        return previous;
    }

    auto apply_panel_controls()
        -> std::expected<void, ui::InputPortError> override
    {
        ++apply_calls;
        return {};
    }

    auto restore(const ui::RuntimeInputState& state)
        -> std::expected<void, ui::InputPortError> override
    {
        ++restore_calls;
        restored = state;
        return {};
    }

    ui::RuntimeInputState previous{
        false,
        false,
        false,
        ui::RuntimeInputMode::GameOnly,
    };
    std::optional<ui::RuntimeInputState> restored{};
    std::size_t capture_calls{};
    std::size_t apply_calls{};
    std::size_t restore_calls{};
};

class Picker final : public ImageFilePickerPort
{
public:
    auto pick_images(std::uintptr_t owner_window)
        -> ImageFilePickerResult<
            std::vector<PickedImageFile>> override
    {
        ++image_calls;
        last_owner = owner_window;
        return std::nullopt;
    }

    auto pick_image_project(std::uintptr_t owner_window)
        -> ImageFilePickerResult<
            PickedImageProjectFile> override
    {
        ++project_calls;
        last_owner = owner_window;
        return std::nullopt;
    }

    std::size_t image_calls{};
    std::size_t project_calls{};
    std::uintptr_t last_owner{};
};

class Hasher final : public PresetHasher
{
public:
    auto hash(std::span<const std::byte>)
        -> std::expected<
            common::Sha256Digest,
            PresetHashError> override
    {
        return common::Sha256Digest{};
    }
};
} // namespace

auto main(int argc, char** argv) -> int
{
    auto passed = true;
    passed &= expect(argc == 2, "localization resource path is missing");
    if (argc != 2)
    {
        return 1;
    }

    const auto catalog =
        LocalizationCatalog::parse(read_file(argv[1]));
    passed &= expect(
        catalog.has_value(),
        "localization catalog did not parse");
    if (!catalog)
    {
        return 1;
    }

    auto snapshots = SnapshotPort{};
    auto commands = CommandSink{};
    auto router = InputCommandRouter{100U};
    auto runtime = FrameRuntime{};
    auto input_port = InputPort{};
    auto input_lease = ui::InputLeaseController{};
    auto picker = Picker{};
    auto hasher = Hasher{};
    auto effects =
        ProductUiEffectExecutor{snapshots, picker, hasher};
    auto coordinator = ProductUiFrameCoordinator{
        snapshots,
        commands,
        router,
        *catalog,
        input_lease,
        input_port,
        runtime,
        &effects,
    };
    const auto frame_identity =
        HudFrameIdentity{1U, 2U, 3U, 4U};

    runtime.input.function_keys = {
        FunctionKeyEvent{
            core::FunctionKey::F9,
            FunctionKeyEventKind::Pressed,
        },
    };
    const auto closed = coordinator.tick(frame_identity);
    const auto* toggle =
        commands.commands.empty()
            ? nullptr
            : std::get_if<ToggleUi>(
                  &commands.commands.back());
    passed &= expect(
        closed &&
            closed->commands_enqueued == 1U &&
            toggle && toggle->id == 100U &&
            runtime.last_frame.primitives.empty() &&
            input_port.capture_calls == 0U,
        "closed-frame hotkey routing did not enqueue one typed UI toggle");

    runtime.input.function_keys = {
        FunctionKeyEvent{
            core::FunctionKey::F9,
            FunctionKeyEventKind::Released,
        },
    };
    snapshots.value =
        std::make_shared<const ApplicationSnapshot>(
            snapshot(true));
    const auto opened = coordinator.tick(frame_identity);
    passed &= expect(
        opened && opened->layout &&
            !runtime.last_frame.primitives.empty() &&
            input_port.capture_calls == 1U &&
            input_port.apply_calls == 1U &&
            input_lease.snapshot().phase ==
                ui::InputLeasePhase::Held,
        "an open frame did not compose, render, and acquire input");
    if (!opened || !opened->layout)
    {
        return 1;
    }

    runtime.input.function_keys.clear();
    runtime.input.pointer = ui::PointerFrame{
        {
            opened->layout->content.x + 10.0,
            opened->layout->content.y + 10.0,
        },
        true,
        false,
        true,
        0.0,
    };
    const auto paint = coordinator.tick(frame_identity);
    const auto* start =
        commands.commands.empty()
            ? nullptr
            : std::get_if<StartPaint>(
                  &commands.commands.back());
    passed &= expect(
        paint && paint->commands_enqueued == 1U &&
            start && start->id == 101U,
        "a rendered Paint activation did not enqueue its typed action");

    runtime.input.pointer = ui::PointerFrame{
        center(paint->layout->section_tabs[1U]),
        true,
        false,
        true,
        0.0,
    };
    const auto image_tab = coordinator.tick(frame_identity);
    passed &= expect(
        image_tab && image_tab->layout &&
            image_tab->selected_section ==
                ProductUiSection::ImagePaint &&
            image_tab->commands_enqueued == 0U,
        "Image Paint tab selection emitted a command");
    if (!image_tab || !image_tab->layout)
    {
        return 1;
    }

    constexpr auto ProjectToolbarOffset = 46.0;
    constexpr auto ProjectToolbarHeight = 38.0;
    constexpr auto ProjectControlGap = 6.0;
    const auto load_width =
        (image_tab->layout->content.width -
         ProjectControlGap) /
        2.0;
    runtime.input.owner_window = 0x1234U;
    runtime.input.pointer = ui::PointerFrame{
        {
            image_tab->layout->content.x +
                load_width * 0.5,
            image_tab->layout->content.y +
                ProjectToolbarOffset +
                ProjectToolbarHeight * 0.5,
        },
        true,
        false,
        true,
        0.0,
    };
    const auto cancelled_effect =
        coordinator.tick(frame_identity);
    passed &= expect(
        cancelled_effect &&
            cancelled_effect->effect_cancelled &&
            cancelled_effect->commands_enqueued == 0U &&
            picker.project_calls == 1U &&
            picker.last_owner == 0x1234U,
        "the Load effect did not execute outside composition as cancellation");

    runtime.fail_render = true;
    runtime.input.pointer = {};
    const auto failed_render =
        coordinator.tick(frame_identity);
    passed &= expect(
        !failed_render &&
            failed_render.error().code ==
                ProductUiFrameErrorCode::Render &&
            input_port.restore_calls == 1U &&
            input_port.restored == input_port.previous &&
            input_lease.snapshot().phase ==
                ui::InputLeasePhase::Released,
        "render failure left the game input lease held");

    runtime.fail_render = false;
    snapshots.value =
        std::make_shared<const ApplicationSnapshot>(
            snapshot(false));
    const auto reclosed = coordinator.tick(frame_identity);
    passed &= expect(
        reclosed &&
            runtime.last_frame.primitives.empty() &&
            input_lease.snapshot().phase ==
                ui::InputLeasePhase::Released,
        "a closed snapshot did not keep the input lease released");

    const auto shutdown = coordinator.shutdown();
    passed &= expect(
        shutdown.has_value(),
        "terminal shutdown did not complete");
    passed &= expect(
        coordinator.snapshot().stopped,
        "terminal shutdown did not publish stopped state");
    const auto stopped_frame = coordinator.tick(frame_identity);
    passed &= expect(
        !stopped_frame &&
            stopped_frame.error().code ==
                ProductUiFrameErrorCode::Stopped,
        "terminal shutdown did not close UI frame admission");

    auto texture_snapshots = SnapshotPort{};
    texture_snapshots.value =
        std::make_shared<const ApplicationSnapshot>(
            ready_snapshot());
    auto texture_commands = CommandSink{};
    auto texture_router = InputCommandRouter{200U};
    auto texture_frame_runtime = FrameRuntime{};
    auto texture_input_port = InputPort{};
    auto texture_input_lease = ui::InputLeaseController{};
    auto ready_content_port = ReadyContentPort{};
    auto game_thread = ThreadContext{};
    auto texture_port = TexturePort{};
    auto textures = ImageEditorTextureCoordinator{
        game_thread,
        texture_port,
    };
    const auto guides = std::array{
        guide(core::BodyProfile::Round),
        guide(core::BodyProfile::Cube),
        guide(core::BodyProfile::Fukuyoka),
    };
    passed &= expect(
        textures.install_guides(guides).has_value(),
        "texture integration fixture could not install guides");
    auto texture_frames = ProductUiFrameCoordinator{
        texture_snapshots,
        texture_commands,
        texture_router,
        *catalog,
        texture_input_lease,
        texture_input_port,
        texture_frame_runtime,
        nullptr,
        &ready_content_port,
        &textures,
    };
    const auto ready_frame =
        texture_frames.tick(frame_identity);
    passed &= expect(
        ready_frame &&
            textures.frame_assets() &&
            textures.frame_assets()->project_id == ProjectId &&
            texture_port.live.size() == 5U,
        "a ready revision did not publish guide, atlas, and source textures");

    texture_snapshots.value =
        std::make_shared<const ApplicationSnapshot>(
            snapshot(false));
    const auto cleared_frame =
        texture_frames.tick(frame_identity);
    passed &= expect(
        cleared_frame &&
            !textures.frame_assets() &&
            texture_port.live.size() == 3U,
        "project removal did not release only project frame textures");
    passed &= expect(
        texture_frames.shutdown().has_value() &&
            texture_port.live.empty(),
        "frame shutdown did not release the shared guide catalog");

    if (passed)
    {
        std::cout << "PASS product_ui_frame_coordinator\n";
    }
    return passed ? 0 : 1;
}
