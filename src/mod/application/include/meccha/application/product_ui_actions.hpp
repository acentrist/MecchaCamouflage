#pragma once

#include <meccha/application/commands.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace meccha::application
{
inline constexpr std::size_t MaximumProductUiActionsPerFrame = 1U;
inline constexpr std::size_t MaximumProductUiAssetIdBytes = 128U;
inline constexpr std::size_t MaximumProductUiProjectNameBytes = 256U;

enum class FeatureUiAction : std::uint8_t
{
    Start,
    Preview,
    Restore,
    Cancel,
};

struct UiPaintAction
{
    FeatureUiAction action{FeatureUiAction::Start};
};

struct UiImagePaintAction
{
    FeatureUiAction action{FeatureUiAction::Start};
};

struct UiToggleProductPanel
{
};

struct UiToggleEsp
{
};

struct UiApplySettings
{
    core::ApplicationConfig settings{};
};

struct UiLoadImageProject
{
    std::string project_id{};
};

struct UiSaveCurrentImageProject
{
};

struct UiRenameCurrentImageProject
{
    std::string new_name{};
};

struct UiDeleteCurrentImageProject
{
};

struct UiMutateCurrentImageProject
{
    ImageEditorMutation mutation{};
};

using ProductUiAction = std::variant<
    UiPaintAction,
    UiImagePaintAction,
    UiToggleProductPanel,
    UiToggleEsp,
    UiApplySettings,
    UiLoadImageProject,
    UiSaveCurrentImageProject,
    UiRenameCurrentImageProject,
    UiDeleteCurrentImageProject,
    UiMutateCurrentImageProject>;

struct ProductUiActionEnvelope
{
    std::uint64_t expected_snapshot_revision{};
    ProductUiAction action{};
};

enum class ProductUiActionRejectionReason : std::uint8_t
{
    Unavailable,
};

struct ProductUiActionRejection
{
    std::size_t action_index{};
    ProductUiActionRejectionReason reason{
        ProductUiActionRejectionReason::Unavailable};

    auto operator==(const ProductUiActionRejection&) const
        -> bool = default;
};

struct ProductUiActionBatch
{
    std::vector<ApplicationCommand> commands{};
    std::vector<ProductUiActionRejection> rejections{};
};
} // namespace meccha::application
