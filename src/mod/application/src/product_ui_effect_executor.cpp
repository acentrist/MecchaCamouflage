#include <meccha/application/product_ui_effect_executor.hpp>

#include <meccha/application/product_ui_model.hpp>

#include <expected>
#include <memory>
#include <type_traits>
#include <utility>
#include <variant>

namespace meccha::application
{
namespace
{
auto effect_error(ProductUiEffectErrorCode code)
    -> std::unexpected<ProductUiEffectError>
{
    return std::unexpected(ProductUiEffectError{
        code,
        std::nullopt,
        std::nullopt,
    });
}

auto picker_error(ImageFilePickerError error)
    -> std::unexpected<ProductUiEffectError>
{
    return std::unexpected(ProductUiEffectError{
        ProductUiEffectErrorCode::Picker,
        std::move(error),
        std::nullopt,
    });
}

auto import_error(ImageFileImportError error)
    -> std::unexpected<ProductUiEffectError>
{
    return std::unexpected(ProductUiEffectError{
        ProductUiEffectErrorCode::ImageImport,
        std::nullopt,
        std::move(error),
    });
}

auto model_for(
    const std::shared_ptr<const ApplicationSnapshot>& snapshot)
    -> std::expected<ProductUiModel, ProductUiEffectError>
{
    if (!snapshot)
    {
        return effect_error(
            ProductUiEffectErrorCode::InvalidSnapshot);
    }
    auto model = build_product_ui_model(*snapshot);
    if (!model)
    {
        return effect_error(
            ProductUiEffectErrorCode::InvalidSnapshot);
    }
    return std::move(*model);
}
} // namespace

ProductUiEffectExecutor::ProductUiEffectExecutor(
    ApplicationSnapshotPort& snapshots,
    ImageFilePickerPort& picker,
    PresetHasher& hasher)
    : snapshots_{snapshots},
      picker_{picker},
      hasher_{hasher}
{
}

auto ProductUiEffectExecutor::execute(
    ProductUiEffectEnvelope effect,
    std::uintptr_t owner_window)
    -> std::expected<
        ProductUiEffectResult,
        ProductUiEffectError>
{
    const auto before = snapshots_.snapshot();
    const auto before_model = model_for(before);
    if (!before_model)
    {
        return std::unexpected(before_model.error());
    }
    if (effect.expected_snapshot_revision !=
        before->revision)
    {
        return effect_error(
            ProductUiEffectErrorCode::StaleEffect);
    }

    return std::visit(
        [this, owner_window, &before, &before_model](
            auto&& request)
            -> std::expected<
                ProductUiEffectResult,
                ProductUiEffectError>
        {
            using Request = std::decay_t<decltype(request)>;
            if constexpr (
                std::is_same_v<Request, UiPickImageFiles>)
            {
                if (!core::valid_image_project_id(
                        request.project_id) ||
                    request.expected_project_revision == 0U)
                {
                    return effect_error(
                        ProductUiEffectErrorCode::InvalidEffect);
                }
                if (!before_model->image_paint.project.edit ||
                    !before->image_editor.document ||
                    before->image_editor.document->project_id !=
                        request.project_id ||
                    before->image_editor.document->revision !=
                        request.expected_project_revision)
                {
                    return effect_error(
                        ProductUiEffectErrorCode::Unavailable);
                }

                auto selected =
                    picker_.pick_images(owner_window);
                if (!selected)
                {
                    return picker_error(
                        std::move(selected.error()));
                }
                if (!*selected)
                {
                    return ProductUiEffectResult{
                        true,
                        std::nullopt,
                    };
                }

                const auto after = snapshots_.snapshot();
                const auto after_model = model_for(after);
                if (!after_model)
                {
                    return std::unexpected(
                        after_model.error());
                }
                if (!after_model->image_paint.project.edit ||
                    !after->image_editor.document ||
                    after->image_editor.document->project_id !=
                        request.project_id ||
                    after->image_editor.document->revision !=
                        request.expected_project_revision)
                {
                    return effect_error(
                        ProductUiEffectErrorCode::StaleEffect);
                }

                auto mutation = prepare_image_file_import(
                    *after->image_editor.document,
                    **selected,
                    hasher_);
                if (!mutation)
                {
                    return import_error(
                        std::move(mutation.error()));
                }
                return ProductUiEffectResult{
                    false,
                    ProductUiActionEnvelope{
                        after->revision,
                        UiMutateCurrentImageProject{
                            std::move(*mutation),
                        },
                    },
                };
            }
            else
            {
                if (!before_model->image_paint.project.load)
                {
                    return effect_error(
                        ProductUiEffectErrorCode::Unavailable);
                }
                auto selected =
                    picker_.pick_image_project(owner_window);
                if (!selected)
                {
                    return picker_error(
                        std::move(selected.error()));
                }
                if (!*selected)
                {
                    return ProductUiEffectResult{
                        true,
                        std::nullopt,
                    };
                }

                const auto after = snapshots_.snapshot();
                const auto after_model = model_for(after);
                if (!after_model)
                {
                    return std::unexpected(
                        after_model.error());
                }
                if (!after_model->image_paint.project.load)
                {
                    return effect_error(
                        ProductUiEffectErrorCode::StaleEffect);
                }
                const auto& bytes = (**selected).bytes;
                if (!bytes || bytes->empty() ||
                    bytes->size() >
                        MaximumPresetContainerBytes)
                {
                    return effect_error(
                        ProductUiEffectErrorCode::InvalidEffect);
                }
                return ProductUiEffectResult{
                    false,
                    ProductUiActionEnvelope{
                        after->revision,
                        UiImportImageProject{bytes},
                    },
                };
            }
        },
        std::move(effect.request));
}
} // namespace meccha::application
