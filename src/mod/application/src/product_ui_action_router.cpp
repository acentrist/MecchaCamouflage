#include <meccha/application/input_command_router.hpp>
#include <meccha/application/product_ui_model.hpp>
#include <meccha/common/hash.hpp>
#include <meccha/core/utf8.hpp>

#include <algorithm>
#include <limits>
#include <string_view>
#include <type_traits>
#include <variant>

namespace meccha::application
{
namespace
{
auto valid_feature_action(FeatureUiAction action) -> bool
{
    return action == FeatureUiAction::Start ||
           action == FeatureUiAction::Preview ||
           action == FeatureUiAction::Restore ||
           action == FeatureUiAction::Cancel;
}

auto valid_project_name(std::string_view name) -> bool
{
    return !name.empty() &&
           name.size() <= MaximumProductUiProjectNameBytes &&
           core::valid_utf8(name) &&
           std::ranges::none_of(
               name,
               [](unsigned char character)
               {
                   return character < 0x20U ||
                          character == 0x7FU;
               });
}

auto bounded_asset_id(std::string_view asset_id) -> bool
{
    return !asset_id.empty() &&
           asset_id.size() <= MaximumProductUiAssetIdBytes;
}

auto valid_added_layers(
    const AddImageLayersMutation& request,
    const std::optional<ImageEditorDocumentSnapshot>& document)
    -> bool
{
    if (request.layers.empty() ||
        request.layers.size() > core::MaximumImageLayers ||
        request.sources.size() > core::MaximumImageSources ||
        (document &&
         (document->layers.size() >
              core::MaximumImageLayers ||
          request.layers.size() >
              core::MaximumImageLayers -
                  document->layers.size())) ||
        std::ranges::any_of(
            request.layers,
            [](const core::ImageLayer& layer)
            {
                return !core::validate(layer).empty();
            }))
    {
        return false;
    }

    for (const auto& source : request.sources)
    {
        if (!common::parse_sha256_hex(source.asset_id) ||
            !source.bytes || source.bytes->empty() ||
            source.bytes->size() >
                core::MaximumImageSourceBytes ||
            std::ranges::count_if(
                request.sources,
                [&source](
                    const core::ImageSourceAsset& candidate)
                {
                    return candidate.asset_id ==
                           source.asset_id;
                }) != 1 ||
            (document &&
             std::ranges::any_of(
                 document->layers,
                 [&source](const core::ImageLayer& layer)
                 {
                     return layer.asset_id ==
                            source.asset_id;
                 })))
        {
            return false;
        }
        const auto referenced = std::ranges::any_of(
            request.layers,
            [&source](const core::ImageLayer& layer)
            {
                return layer.asset_id == source.asset_id &&
                       layer.mime == source.mime &&
                       layer.source_bytes ==
                           source.bytes->size();
            });
        if (!referenced)
        {
            return false;
        }
    }

    return std::ranges::all_of(
        request.layers,
        [&request, &document](const core::ImageLayer& layer)
        {
            const auto added = std::ranges::any_of(
                request.sources,
                [&layer](
                    const core::ImageSourceAsset& source)
                {
                    return source.asset_id == layer.asset_id &&
                           source.mime == layer.mime &&
                           source.bytes &&
                           source.bytes->size() ==
                               layer.source_bytes;
                });
            const auto existing =
                document &&
                std::ranges::any_of(
                    document->layers,
                    [&layer](
                        const core::ImageLayer& candidate)
                    {
                        return candidate.asset_id ==
                                   layer.asset_id &&
                               candidate.mime == layer.mime &&
                               candidate.source_bytes ==
                                   layer.source_bytes;
                    });
            return added || existing;
        });
}

auto valid_mutation(
    const ImageEditorMutation& mutation,
    const std::optional<ImageEditorDocumentSnapshot>& document)
    -> bool
{
    return std::visit(
        [&document](const auto& request)
        {
            using Request = std::decay_t<decltype(request)>;
            if constexpr (
                std::is_same_v<Request, AddImageLayersMutation>)
            {
                return valid_added_layers(request, document);
            }
            else if constexpr (
                std::is_same_v<Request, ReplaceImageLayerMutation>)
            {
                if (!bounded_asset_id(
                        request.expected_asset_id) ||
                    !bounded_asset_id(
                        request.layer.asset_id) ||
                    !core::validate(request.layer).empty())
                {
                    return false;
                }
                if (!document)
                {
                    return true;
                }
                return request.layer_index <
                           document->layers.size() &&
                       document->layers[request.layer_index]
                               .asset_id ==
                           request.expected_asset_id &&
                       request.layer.asset_id ==
                           request.expected_asset_id &&
                       request.layer !=
                           document->layers[
                               request.layer_index];
            }
            else if constexpr (
                std::is_same_v<Request, ReorderImageLayerMutation>)
            {
                if (!bounded_asset_id(
                        request.expected_asset_id))
                {
                    return false;
                }
                if (!document)
                {
                    return true;
                }
                return request.layer_index <
                           document->layers.size() &&
                       request.destination_index <
                           document->layers.size() &&
                       request.layer_index !=
                           request.destination_index &&
                       document->layers[request.layer_index]
                               .asset_id ==
                           request.expected_asset_id;
            }
            else if constexpr (
                std::is_same_v<Request, RemoveImageLayerMutation>)
            {
                if (!bounded_asset_id(
                        request.expected_asset_id))
                {
                    return false;
                }
                if (!document)
                {
                    return true;
                }
                return document->layers.size() > 1U &&
                       request.layer_index <
                           document->layers.size() &&
                       document->layers[request.layer_index]
                               .asset_id ==
                           request.expected_asset_id;
            }
            else
            {
                return core::validate(request.settings).empty() &&
                       (!document ||
                        request.settings != document->settings);
            }
        },
        mutation);
}

auto valid_action(
    const ProductUiAction& action,
    const ApplicationSnapshot& snapshot) -> bool
{
    return std::visit(
        [&snapshot](const auto& request)
        {
            using Request = std::decay_t<decltype(request)>;
            if constexpr (
                std::is_same_v<Request, UiPaintAction> ||
                std::is_same_v<Request, UiImagePaintAction>)
            {
                return valid_feature_action(request.action);
            }
            else if constexpr (
                std::is_same_v<Request, UiApplySettings>)
            {
                return core::validate(request.settings).empty() &&
                       request.settings.active_image_project ==
                           snapshot.settings.active_image_project;
            }
            else if constexpr (
                std::is_same_v<Request, UiLoadImageProject>)
            {
                return core::valid_image_project_id(
                    request.project_id);
            }
            else if constexpr (
                std::is_same_v<
                    Request,
                    UiRenameCurrentImageProject>)
            {
                return valid_project_name(request.new_name);
            }
            else if constexpr (
                std::is_same_v<
                    Request,
                    UiMutateCurrentImageProject>)
            {
                return valid_mutation(
                    request.mutation,
                    snapshot.image_editor.document);
            }
            else
            {
                return true;
            }
        },
        action);
}

auto feature_action_available(
    const FeatureActionAvailability& availability,
    FeatureUiAction action) -> bool
{
    switch (action)
    {
    case FeatureUiAction::Start:
        return availability.start;
    case FeatureUiAction::Preview:
        return availability.preview;
    case FeatureUiAction::Restore:
        return availability.restore;
    case FeatureUiAction::Cancel:
        return availability.cancel;
    }
    return false;
}

auto action_available(
    const ProductUiAction& action,
    const ProductUiModel& model,
    const ApplicationSnapshot& snapshot) -> bool
{
    return std::visit(
        [&model, &snapshot](const auto& request)
        {
            using Request = std::decay_t<decltype(request)>;
            if constexpr (
                std::is_same_v<Request, UiPaintAction>)
            {
                return feature_action_available(
                    model.paint.actions,
                    request.action);
            }
            else if constexpr (
                std::is_same_v<Request, UiImagePaintAction>)
            {
                return feature_action_available(
                    model.image_paint.actions,
                    request.action);
            }
            else if constexpr (
                std::is_same_v<Request, UiToggleProductPanel>)
            {
                return snapshot.command_queue.accepting &&
                       snapshot.command_queue.queued <
                           snapshot.command_queue.capacity &&
                       snapshot.runtime_phase !=
                           ApplicationRuntimePhase::ShuttingDown &&
                       snapshot.runtime_phase !=
                           ApplicationRuntimePhase::Stopped;
            }
            else if constexpr (
                std::is_same_v<Request, UiToggleEsp>)
            {
                return model.esp.can_toggle;
            }
            else if constexpr (
                std::is_same_v<Request, UiApplySettings>)
            {
                return model.settings.can_apply;
            }
            else if constexpr (
                std::is_same_v<Request, UiLoadImageProject>)
            {
                return model.image_paint.project.load;
            }
            else if constexpr (
                std::is_same_v<
                    Request,
                    UiSaveCurrentImageProject>)
            {
                return model.image_paint.project.save;
            }
            else if constexpr (
                std::is_same_v<
                    Request,
                    UiRenameCurrentImageProject>)
            {
                return model.image_paint.project.rename;
            }
            else if constexpr (
                std::is_same_v<
                    Request,
                    UiDeleteCurrentImageProject>)
            {
                return model.image_paint.project.remove;
            }
            else
            {
                return model.image_paint.project.edit;
            }
        },
        action);
}

auto make_command(
    const ProductUiAction& action,
    CommandId command_id,
    const ApplicationSnapshot& snapshot)
    -> ApplicationCommand
{
    return std::visit(
        [&snapshot, command_id](const auto& request)
            -> ApplicationCommand
        {
            using Request = std::decay_t<decltype(request)>;
            if constexpr (
                std::is_same_v<Request, UiPaintAction>)
            {
                switch (request.action)
                {
                case FeatureUiAction::Start:
                    return StartPaint{
                        command_id,
                        snapshot.settings.paint,
                    };
                case FeatureUiAction::Preview:
                    return PreviewPaint{
                        command_id,
                        snapshot.settings.paint,
                    };
                case FeatureUiAction::Restore:
                    return RestorePaintPreview{command_id};
                case FeatureUiAction::Cancel:
                    return CancelPaint{command_id};
                }
            }
            else if constexpr (
                std::is_same_v<Request, UiImagePaintAction>)
            {
                switch (request.action)
                {
                case FeatureUiAction::Start:
                {
                    const auto& document =
                        *snapshot.image_editor.document;
                    return StartImagePaint{
                        command_id,
                        document.project_id,
                        document.revision,
                    };
                }
                case FeatureUiAction::Preview:
                {
                    const auto& document =
                        *snapshot.image_editor.document;
                    return PreviewImagePaint{
                        command_id,
                        document.project_id,
                        document.revision,
                    };
                }
                case FeatureUiAction::Restore:
                    return RestoreImagePaintPreview{command_id};
                case FeatureUiAction::Cancel:
                    return CancelImagePaint{command_id};
                }
            }
            else if constexpr (
                std::is_same_v<Request, UiToggleProductPanel>)
            {
                return ToggleUi{command_id};
            }
            else if constexpr (
                std::is_same_v<Request, UiToggleEsp>)
            {
                return ToggleEsp{command_id};
            }
            else if constexpr (
                std::is_same_v<Request, UiApplySettings>)
            {
                return ApplyValidatedSettings{
                    command_id,
                    request.settings,
                };
            }
            else if constexpr (
                std::is_same_v<Request, UiLoadImageProject>)
            {
                return LoadImageProject{
                    command_id,
                    request.project_id,
                };
            }
            else if constexpr (
                std::is_same_v<
                    Request,
                    UiSaveCurrentImageProject>)
            {
                const auto& document =
                    *snapshot.image_editor.document;
                return SaveImageProject{
                    command_id,
                    document.project_id,
                    document.revision,
                };
            }
            else if constexpr (
                std::is_same_v<
                    Request,
                    UiRenameCurrentImageProject>)
            {
                const auto& document =
                    *snapshot.image_editor.document;
                return RenameImageProject{
                    command_id,
                    document.project_id,
                    document.revision,
                    request.new_name,
                };
            }
            else if constexpr (
                std::is_same_v<
                    Request,
                    UiDeleteCurrentImageProject>)
            {
                return DeleteImageProject{
                    command_id,
                    snapshot.image_editor.document->project_id,
                };
            }
            else
            {
                const auto& document =
                    *snapshot.image_editor.document;
                return MutateImageProject{
                    command_id,
                    document.project_id,
                    document.revision,
                    request.mutation,
                };
            }
            return ToggleUi{command_id};
        },
        action);
}
} // namespace

auto InputCommandRouter::route_ui_actions(
    const ApplicationSnapshot& snapshot,
    std::span<const ProductUiActionEnvelope> actions)
    -> std::expected<
        ProductUiActionBatch,
        InputCommandRouterError>
{
    const auto lock = std::scoped_lock{mutex_};
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
    if (actions.size() > MaximumProductUiActionsPerFrame)
    {
        return std::unexpected(
            InputCommandRouterError::UiActionLimit);
    }
    if (std::ranges::any_of(
            actions,
            [&snapshot](const ProductUiActionEnvelope& action)
            {
                return action.expected_snapshot_revision !=
                       snapshot.revision;
            }))
    {
        return std::unexpected(
            InputCommandRouterError::StaleSnapshot);
    }

    const auto model = build_product_ui_model(snapshot);
    if (!model)
    {
        return std::unexpected(
            model.error() ==
                    ProductUiModelError::InvalidSettings
                ? InputCommandRouterError::InvalidSettings
                : InputCommandRouterError::InvalidSnapshot);
    }
    if (std::ranges::any_of(
            actions,
            [&snapshot](const ProductUiActionEnvelope& action)
            {
                return !valid_action(
                    action.action,
                    snapshot);
            }))
    {
        return std::unexpected(
            InputCommandRouterError::InvalidUiAction);
    }

    auto batch = ProductUiActionBatch{};
    batch.commands.reserve(actions.size());
    batch.rejections.reserve(actions.size());
    for (std::size_t index = 0U;
         index < actions.size();
         ++index)
    {
        if (!action_available(
                actions[index].action,
                *model,
                snapshot))
        {
            batch.rejections.push_back(
                ProductUiActionRejection{
                    index,
                    ProductUiActionRejectionReason::Unavailable,
                });
            continue;
        }
        if (command_ids_exhausted_)
        {
            return std::unexpected(
                InputCommandRouterError::CommandOverflow);
        }
        batch.commands.push_back(make_command(
            actions[index].action,
            next_command_id_,
            snapshot));
    }

    if (!batch.commands.empty())
    {
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
    return batch;
}
} // namespace meccha::application
