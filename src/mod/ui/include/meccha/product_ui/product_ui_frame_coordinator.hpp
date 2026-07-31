#pragma once

#include <meccha/application/application_command_queue.hpp>
#include <meccha/application/application_snapshot.hpp>
#include <meccha/application/input_command_router.hpp>
#include <meccha/application/localization.hpp>
#include <meccha/application/product_ui_effect_executor.hpp>
#include <meccha/product_ui/image_editor_texture_coordinator.hpp>
#include <meccha/product_ui/product_panel.hpp>
#include <meccha/ui/input_lease.hpp>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <vector>

namespace meccha::product_ui
{
struct ProductUiFrameInput
{
    ui::CanvasViewport viewport{};
    ui::CanvasInsets safe_area{};
    ui::PointerFrame pointer{};
    ui::KeyboardNavigationFrame keyboard{};
    std::vector<ui::TextEditEvent> text_edit_events{};
    std::vector<application::FunctionKeyEvent> function_keys{};
    bool function_key_input_available{true};
    std::uintptr_t owner_window{};
};

struct ProductUiFrameRuntimeError
{
    std::string detail{};

    auto operator==(const ProductUiFrameRuntimeError&) const
        -> bool = default;
};

class ProductUiFrameRuntimePort
{
public:
    ProductUiFrameRuntimePort() = default;
    ProductUiFrameRuntimePort(
        const ProductUiFrameRuntimePort&) = delete;
    auto operator=(const ProductUiFrameRuntimePort&)
        -> ProductUiFrameRuntimePort& = delete;
    virtual ~ProductUiFrameRuntimePort() = default;

    [[nodiscard]] virtual auto capture(
        const application::HudFrameIdentity& identity)
        -> std::expected<
            ProductUiFrameInput,
            ProductUiFrameRuntimeError> = 0;

    [[nodiscard]] virtual auto render(
        const application::HudFrameIdentity& identity,
        const ui::CanvasFrame& frame)
        -> std::expected<
            void,
            ProductUiFrameRuntimeError> = 0;
};

enum class ProductUiFrameErrorCode : std::uint8_t
{
    InvalidFrameIdentity,
    MissingSnapshot,
    InvalidDependencies,
    Capture,
    Model,
    Hotkeys,
    Texture,
    Panel,
    InputLease,
    Render,
    Effect,
    Action,
    Enqueue,
    GenerationOverflow,
    Unexpected,
    Stopped,
};

struct ProductUiFrameError
{
    ProductUiFrameErrorCode code{};
    std::optional<ProductUiFrameRuntimeError> runtime{};
    std::optional<application::ProductUiModelError> model{};
    std::optional<application::InputCommandRouterError> router{};
    std::optional<ImageEditorTextureError> texture{};
    std::optional<ProductPanelError> panel{};
    std::optional<ui::InputLeaseFailure> input_lease{};
    std::optional<application::ProductUiEffectError> effect{};
    std::optional<application::CommandEnqueueResult> enqueue{};

    auto operator==(const ProductUiFrameError&) const -> bool = default;
};

struct ProductUiFrameSnapshot
{
    std::uint64_t generation{};
    std::uint64_t application_revision{};
    std::size_t commands_enqueued{};
    std::size_t hotkey_rejections{};
    std::size_t action_rejections{};
    std::size_t suppressed_repeats{};
    std::size_t primitive_count{};
    bool effect_cancelled{};
    std::optional<ui::PanelLayout> layout{};
    application::ProductUiSection selected_section{
        application::ProductUiSection::Paint};
    ui::InputLeaseSnapshot input_lease{};
    bool stopping{};
    bool stopped{};

    auto operator==(const ProductUiFrameSnapshot&) const -> bool = default;
};

class ProductUiFrameCoordinator
{
public:
    ProductUiFrameCoordinator(
        application::ApplicationSnapshotPort& snapshots,
        application::ApplicationCommandSink& commands,
        application::InputCommandRouter& router,
        const application::LocalizationCatalog& localization,
        ui::InputLeaseController& input_lease,
        ui::InputLeasePort& input_port,
        ProductUiFrameRuntimePort& runtime,
        application::ProductUiEffectExecutor* effects = nullptr,
        application::ImageEditorReadyContentPort* ready_content = nullptr,
        ImageEditorTextureCoordinator* textures = nullptr);
    ProductUiFrameCoordinator(
        const ProductUiFrameCoordinator&) = delete;
    auto operator=(const ProductUiFrameCoordinator&)
        -> ProductUiFrameCoordinator& = delete;

    [[nodiscard]] auto tick(
        const application::HudFrameIdentity& identity)
        -> std::expected<
            ProductUiFrameSnapshot,
            ProductUiFrameError>;

    [[nodiscard]] auto shutdown()
        -> std::expected<void, ProductUiFrameError>;

    [[nodiscard]] auto snapshot() const
        -> ProductUiFrameSnapshot;

private:
    [[nodiscard]] auto synchronize_textures(
        const application::ApplicationSnapshot& snapshot)
        -> std::expected<
            std::optional<ImageEditorFrameAssets>,
            ProductUiFrameError>;
    [[nodiscard]] auto enqueue(
        std::vector<application::ApplicationCommand> commands,
        std::size_t& count)
        -> std::expected<void, ProductUiFrameError>;
    [[nodiscard]] auto route_effect(
        const application::ProductUiEffectEnvelope& effect,
        std::uintptr_t owner_window,
        ProductUiFrameSnapshot& next)
        -> std::expected<void, ProductUiFrameError>;
    [[nodiscard]] auto next_generation()
        -> std::expected<std::uint64_t, ProductUiFrameError>;

    application::ApplicationSnapshotPort& snapshots_;
    application::ApplicationCommandSink& commands_;
    application::InputCommandRouter& router_;
    const application::LocalizationCatalog& localization_;
    ui::InputLeaseController& input_lease_;
    ui::InputLeasePort& input_port_;
    ProductUiFrameRuntimePort& runtime_;
    application::ProductUiEffectExecutor* effects_{};
    application::ImageEditorReadyContentPort* ready_content_{};
    ImageEditorTextureCoordinator* textures_{};
    ProductPanelState panel_state_{};
    ProductUiFrameSnapshot snapshot_{};
    bool invalid_dependencies_{};
    bool stopping_{};
    bool stopped_{};
};
} // namespace meccha::product_ui
