#pragma once

#include <meccha/core/image_project.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace meccha::application
{
struct ImageEditorDocumentSnapshot
{
    std::string project_id{};
    std::string display_name{};
    std::uint64_t revision{};
    core::ImageProjectSettings settings{};
    std::vector<core::ImageLayer> layers{};

    auto operator==(const ImageEditorDocumentSnapshot&) const
        -> bool = default;
};

struct ReplaceImageLayerMutation
{
    std::size_t layer_index{};
    std::string expected_asset_id{};
    core::ImageLayer layer{};
};

struct ReorderImageLayerMutation
{
    std::size_t layer_index{};
    std::size_t destination_index{};
    std::string expected_asset_id{};
};

struct RemoveImageLayerMutation
{
    std::size_t layer_index{};
    std::string expected_asset_id{};
};

struct AddImageLayersMutation
{
    std::vector<core::ImageLayer> layers{};
    std::vector<core::ImageSourceAsset> sources{};
};

struct ReplaceImageProjectSettingsMutation
{
    core::ImageProjectSettings settings{};
};

using ImageEditorMutation = std::variant<
    ReplaceImageLayerMutation,
    ReorderImageLayerMutation,
    RemoveImageLayerMutation,
    AddImageLayersMutation,
    ReplaceImageProjectSettingsMutation>;

enum class ImageEditorMutationError : std::uint8_t
{
    NoProject,
    InvalidProject,
    StaleRevision,
    InvalidLayer,
    InvalidSettings,
    NoChange,
    RevisionOverflow,
    Stopped,
    Busy,
    GenerationOverflow,
    PipelineStart,
};
} // namespace meccha::application
