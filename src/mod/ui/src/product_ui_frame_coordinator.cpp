#include <meccha/product_ui/product_ui_frame_coordinator.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <utility>
#include <vector>

namespace meccha::product_ui
{
namespace
{
auto error(ProductUiFrameErrorCode code)
    -> std::unexpected<ProductUiFrameError>
{
    return std::unexpected(ProductUiFrameError{code});
}

auto runtime_error(
    ProductUiFrameErrorCode code,
    ProductUiFrameRuntimeError failure)
    -> std::unexpected<ProductUiFrameError>
{
    auto result = ProductUiFrameError{code};
    result.runtime = std::move(failure);
    return std::unexpected(std::move(result));
}

auto model_error(application::ProductUiModelError failure)
    -> std::unexpected<ProductUiFrameError>
{
    auto result =
        ProductUiFrameError{ProductUiFrameErrorCode::Model};
    result.model = failure;
    return std::unexpected(std::move(result));
}

auto router_error(
    ProductUiFrameErrorCode code,
    application::InputCommandRouterError failure)
    -> std::unexpected<ProductUiFrameError>
{
    auto result = ProductUiFrameError{code};
    result.router = failure;
    return std::unexpected(std::move(result));
}

auto texture_error(ImageEditorTextureError failure)
    -> std::unexpected<ProductUiFrameError>
{
    auto result =
        ProductUiFrameError{ProductUiFrameErrorCode::Texture};
    result.texture = std::move(failure);
    return std::unexpected(std::move(result));
}

auto panel_error(ProductPanelError failure)
    -> std::unexpected<ProductUiFrameError>
{
    auto result =
        ProductUiFrameError{ProductUiFrameErrorCode::Panel};
    result.panel = std::move(failure);
    return std::unexpected(std::move(result));
}

auto lease_error(ui::InputLeaseFailure failure)
    -> std::unexpected<ProductUiFrameError>
{
    auto result =
        ProductUiFrameError{ProductUiFrameErrorCode::InputLease};
    result.input_lease = std::move(failure);
    return std::unexpected(std::move(result));
}

auto effect_error(application::ProductUiEffectError failure)
    -> std::unexpected<ProductUiFrameError>
{
    auto result =
        ProductUiFrameError{ProductUiFrameErrorCode::Effect};
    result.effect = std::move(failure);
    return std::unexpected(std::move(result));
}

auto enqueue_error(application::CommandEnqueueResult failure)
    -> std::unexpected<ProductUiFrameError>
{
    auto result =
        ProductUiFrameError{ProductUiFrameErrorCode::Enqueue};
    result.enqueue = failure;
    return std::unexpected(std::move(result));
}

auto needs_texture_clear(
    application::ImageEditorPipelinePhase phase) -> bool
{
    return phase == application::ImageEditorPipelinePhase::Empty ||
           phase == application::ImageEditorPipelinePhase::Failed ||
           phase == application::ImageEditorPipelinePhase::Stopped;
}

auto valid_function_key_event(
    const application::FunctionKeyEvent& event) -> bool
{
    const auto key = static_cast<std::uint8_t>(event.key);
    return key >= static_cast<std::uint8_t>(core::FunctionKey::F1) &&
           key <= static_cast<std::uint8_t>(core::FunctionKey::F24) &&
           (event.kind ==
                application::FunctionKeyEventKind::Pressed ||
            event.kind ==
                application::FunctionKeyEventKind::Released);
}

auto frame_extension_error(
    application::RuntimeFrameExtensionStage stage,
    const ProductUiFrameError& error)
    -> application::RuntimeFrameExtensionError
{
    auto failure =
        std::optional<application::CompatibilityFailure>{};
    switch (error.code)
    {
    case ProductUiFrameErrorCode::InvalidFrameIdentity:
        failure = application::CompatibilityFailure{
            application::RuntimeContractId::Canvas,
            application::ContractFailureKind::StaleObject,
            "error.operation.failed",
        };
        break;
    case ProductUiFrameErrorCode::Capture:
    case ProductUiFrameErrorCode::Render:
        failure = application::CompatibilityFailure{
            application::RuntimeContractId::Canvas,
            application::ContractFailureKind::ExecutionFailure,
            "error.operation.failed",
        };
        break;
    case ProductUiFrameErrorCode::Texture:
        failure = application::CompatibilityFailure{
            application::RuntimeContractId::TextureMutation,
            application::ContractFailureKind::ExecutionFailure,
            "error.operation.failed",
        };
        break;
    case ProductUiFrameErrorCode::InputLease:
        failure = application::CompatibilityFailure{
            application::RuntimeContractId::InputControl,
            application::ContractFailureKind::ExecutionFailure,
            "error.operation.failed",
        };
        break;
    default:
        break;
    }
    return {
        stage,
        std::move(failure),
        "error.operation.failed",
    };
}
} // namespace

ProductUiFrameCoordinator::ProductUiFrameCoordinator(
    application::ApplicationSnapshotPort& snapshots,
    application::ApplicationCommandSink& commands,
    application::InputCommandRouter& router,
    const application::LocalizationCatalog& localization,
    ui::InputLeaseController& input_lease,
    ui::InputLeasePort& input_port,
    ProductUiFrameRuntimePort& runtime,
    application::ProductUiEffectExecutor* effects,
    application::ImageEditorReadyContentPort* ready_content,
    ImageEditorTextureCoordinator* textures)
    : snapshots_{snapshots},
      commands_{commands},
      router_{router},
      localization_{localization},
      input_lease_{input_lease},
      input_port_{input_port},
      runtime_{runtime},
      effects_{effects},
      ready_content_{ready_content},
      textures_{textures},
      invalid_dependencies_{
          (ready_content_ == nullptr) != (textures_ == nullptr)}
{
}

auto ProductUiFrameCoordinator::tick(
    const application::HudFrameIdentity& identity)
    -> std::expected<
        ProductUiFrameSnapshot,
        ProductUiFrameError>
{
    if (stopping_ || stopped_)
    {
        return error(ProductUiFrameErrorCode::Stopped);
    }
    if (invalid_dependencies_)
    {
        return error(
            ProductUiFrameErrorCode::InvalidDependencies);
    }
    if (!identity.valid())
    {
        return error(
            ProductUiFrameErrorCode::InvalidFrameIdentity);
    }

    try
    {
        const auto application_snapshot = snapshots_.snapshot();
        if (!application_snapshot)
        {
            return error(
                ProductUiFrameErrorCode::MissingSnapshot);
        }
        const auto model =
            application::build_product_ui_model(
                *application_snapshot);
        if (!model)
        {
            return model_error(model.error());
        }

        auto captured = runtime_.capture(identity);
        if (!captured)
        {
            return runtime_error(
                ProductUiFrameErrorCode::Capture,
                std::move(captured.error()));
        }
        if (captured->function_keys.size() >
            application::MaximumFunctionKeyEventsPerFrame)
        {
            return router_error(
                ProductUiFrameErrorCode::Hotkeys,
                application::InputCommandRouterError::EventLimit);
        }
        if (!std::ranges::all_of(
                captured->function_keys,
                valid_function_key_event))
        {
            return router_error(
                ProductUiFrameErrorCode::Hotkeys,
                application::InputCommandRouterError::InvalidEvent);
        }

        auto next = ProductUiFrameSnapshot{};
        const auto generation = next_generation();
        if (!generation)
        {
            return std::unexpected(generation.error());
        }
        next.generation = *generation;
        next.application_revision =
            application_snapshot->revision;
        next.selected_section = panel_state_.selected;

        auto captured_function_key =
            std::optional<core::FunctionKey>{};
        if (!captured->function_key_input_available)
        {
            router_.release_all();
        }
        else if (panel_state_.hotkey_capture.index)
        {
            router_.release_all();
            const auto pressed = std::ranges::find_if(
                captured->function_keys,
                [](const application::FunctionKeyEvent& event)
                {
                    return event.kind ==
                           application::FunctionKeyEventKind::Pressed;
                });
            if (pressed != captured->function_keys.end())
            {
                captured_function_key = pressed->key;
            }
        }
        else
        {
            auto routed = router_.route(
                *application_snapshot,
                captured->function_keys);
            if (!routed)
            {
                return router_error(
                    ProductUiFrameErrorCode::Hotkeys,
                    routed.error());
            }
            next.hotkey_rejections =
                routed->rejections.size();
            next.suppressed_repeats =
                routed->suppressed_repeats;
            const auto enqueued = enqueue(
                std::move(routed->commands),
                next.commands_enqueued);
            if (!enqueued)
            {
                return std::unexpected(enqueued.error());
            }
        }

        const auto image_assets =
            synchronize_textures(*application_snapshot);
        if (!image_assets)
        {
            return std::unexpected(image_assets.error());
        }

        auto panel_input = ProductPanelInput{
            captured->viewport,
            captured->safe_area,
            std::move(captured->pointer),
            std::move(captured->keyboard),
            std::move(captured->text_edit_events),
            *image_assets,
            captured->function_key_input_available,
            captured_function_key,
        };
        auto output = compose_product_panel(
            *model,
            panel_state_,
            std::move(panel_input),
            build_product_panel_labels(
                localization_,
                model->settings.config.ui.language));
        if (!output)
        {
            return panel_error(std::move(output.error()));
        }

        const auto lease = input_lease_.reconcile(
            model->ui_open,
            input_port_);
        if (!lease)
        {
            return lease_error(lease.error());
        }

        auto rendered = runtime_.render(
            identity,
            output->frame);
        if (!rendered)
        {
            const auto released =
                input_lease_.reconcile(false, input_port_);
            if (!released)
            {
                return lease_error(released.error());
            }
            return runtime_error(
                ProductUiFrameErrorCode::Render,
                std::move(rendered.error()));
        }

        panel_state_ = std::move(output->state);
        next.layout = std::move(output->layout);
        next.selected_section = panel_state_.selected;
        next.primitive_count =
            output->frame.primitives.size();
        next.input_lease = *lease;

        if (output->action)
        {
            const auto actions =
                std::array{*output->action};
            auto routed = router_.route_ui_actions(
                *application_snapshot,
                actions);
            if (!routed)
            {
                return router_error(
                    ProductUiFrameErrorCode::Action,
                    routed.error());
            }
            next.action_rejections =
                routed->rejections.size();
            const auto enqueued = enqueue(
                std::move(routed->commands),
                next.commands_enqueued);
            if (!enqueued)
            {
                return std::unexpected(enqueued.error());
            }
        }
        if (output->effect)
        {
            const auto routed = route_effect(
                *output->effect,
                captured->owner_window,
                next);
            if (!routed)
            {
                return std::unexpected(routed.error());
            }
        }

        snapshot_ = next;
        return snapshot_;
    }
    catch (...)
    {
        return error(ProductUiFrameErrorCode::Unexpected);
    }
}

auto ProductUiFrameCoordinator::shutdown()
    -> std::expected<void, ProductUiFrameError>
{
    if (stopped_)
    {
        return {};
    }
    stopping_ = true;
    router_.shutdown();

    auto first_failure =
        std::optional<ProductUiFrameError>{};
    const auto lease = input_lease_.shutdown(input_port_);
    if (!lease)
    {
        first_failure =
            lease_error(lease.error()).error();
    }
    if (textures_)
    {
        const auto texture_shutdown = textures_->shutdown();
        if (!texture_shutdown && !first_failure)
        {
            first_failure =
                texture_error(
                    texture_shutdown.error()).error();
        }
    }
    snapshot_.input_lease = input_lease_.snapshot();
    snapshot_.stopping = true;
    if (first_failure)
    {
        return std::unexpected(std::move(*first_failure));
    }

    panel_state_.interaction = {};
    stopped_ = true;
    stopping_ = false;
    snapshot_.stopping = false;
    snapshot_.stopped = true;
    return {};
}

auto ProductUiFrameCoordinator::on_hud_frame(
    const application::HudFrameIdentity& identity) noexcept
    -> std::expected<
        void,
        application::RuntimeFrameExtensionError>
{
    try
    {
        const auto result = tick(identity);
        if (!result)
        {
            return std::unexpected(
                frame_extension_error(
                    application::RuntimeFrameExtensionStage::Frame,
                    result.error()));
        }
        return {};
    }
    catch (...)
    {
        return std::unexpected(
            application::RuntimeFrameExtensionError{
                application::RuntimeFrameExtensionStage::Frame,
                std::nullopt,
                "error.operation.failed",
            });
    }
}

auto ProductUiFrameCoordinator::restore_and_stop() noexcept
    -> std::expected<
        void,
        application::RuntimeFrameExtensionError>
{
    try
    {
        const auto result = shutdown();
        if (!result)
        {
            return std::unexpected(
                frame_extension_error(
                    application::RuntimeFrameExtensionStage::Shutdown,
                    result.error()));
        }
        return {};
    }
    catch (...)
    {
        return std::unexpected(
            application::RuntimeFrameExtensionError{
                application::RuntimeFrameExtensionStage::Shutdown,
                std::nullopt,
                "error.operation.failed",
            });
    }
}

auto ProductUiFrameCoordinator::snapshot() const
    -> ProductUiFrameSnapshot
{
    return snapshot_;
}

auto ProductUiFrameCoordinator::synchronize_textures(
    const application::ApplicationSnapshot& snapshot)
    -> std::expected<
        std::optional<ImageEditorFrameAssets>,
        ProductUiFrameError>
{
    if (!textures_)
    {
        return std::optional<ImageEditorFrameAssets>{};
    }

    const auto& editor = snapshot.image_editor;
    const auto active = textures_->frame_assets();
    if (!editor.document ||
        needs_texture_clear(editor.pipeline.phase) ||
        (active &&
         active->project_id != editor.document->project_id))
    {
        if (active)
        {
            const auto cleared = textures_->clear();
            if (!cleared)
            {
                return texture_error(cleared.error());
            }
        }
        return std::optional<ImageEditorFrameAssets>{};
    }
    if (editor.pipeline.phase !=
        application::ImageEditorPipelinePhase::Ready)
    {
        return std::optional<ImageEditorFrameAssets>{};
    }

    const auto content = ready_content_->ready_content(
        editor.document->project_id,
        editor.document->revision);
    if (!content)
    {
        return error(ProductUiFrameErrorCode::InvalidDependencies);
    }
    const auto synchronized = textures_->synchronize(content);
    if (!synchronized)
    {
        return texture_error(synchronized.error());
    }
    auto result = textures_->frame_assets();
    if (!result ||
        result->project_id != editor.document->project_id ||
        result->project_revision != editor.document->revision)
    {
        return error(ProductUiFrameErrorCode::InvalidDependencies);
    }
    return result;
}

auto ProductUiFrameCoordinator::enqueue(
    std::vector<application::ApplicationCommand> commands,
    std::size_t& count)
    -> std::expected<void, ProductUiFrameError>
{
    for (auto& command : commands)
    {
        const auto result =
            commands_.enqueue_command(std::move(command));
        if (result != application::CommandEnqueueResult::Accepted)
        {
            return enqueue_error(result);
        }
        ++count;
    }
    return {};
}

auto ProductUiFrameCoordinator::route_effect(
    const application::ProductUiEffectEnvelope& effect,
    std::uintptr_t owner_window,
    ProductUiFrameSnapshot& next)
    -> std::expected<void, ProductUiFrameError>
{
    if (!effects_)
    {
        return error(ProductUiFrameErrorCode::InvalidDependencies);
    }
    auto executed = effects_->execute(effect, owner_window);
    if (!executed)
    {
        return effect_error(std::move(executed.error()));
    }
    if (executed->cancelled)
    {
        next.effect_cancelled = true;
        return {};
    }
    if (!executed->action)
    {
        return error(ProductUiFrameErrorCode::InvalidDependencies);
    }

    const auto latest = snapshots_.snapshot();
    if (!latest)
    {
        return error(ProductUiFrameErrorCode::MissingSnapshot);
    }
    const auto actions = std::array{*executed->action};
    auto routed = router_.route_ui_actions(*latest, actions);
    if (!routed)
    {
        return router_error(
            ProductUiFrameErrorCode::Action,
            routed.error());
    }
    next.action_rejections += routed->rejections.size();
    return enqueue(
        std::move(routed->commands),
        next.commands_enqueued);
}

auto ProductUiFrameCoordinator::next_generation()
    -> std::expected<std::uint64_t, ProductUiFrameError>
{
    if (snapshot_.generation ==
        std::numeric_limits<std::uint64_t>::max())
    {
        return error(
            ProductUiFrameErrorCode::GenerationOverflow);
    }
    return snapshot_.generation + 1U;
}
} // namespace meccha::product_ui
