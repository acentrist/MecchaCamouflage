#pragma once

#include <meccha/application/image_editor_contract.hpp>
#include <meccha/application/job_state.hpp>
#include <meccha/core/config.hpp>
#include <meccha/core/image_project.hpp>
#include <meccha/core/paint.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <variant>
#include <vector>

namespace meccha::application
{
struct StartPaint
{
    CommandId id{};
    core::PaintSettings settings{};
};

struct PreviewPaint
{
    CommandId id{};
    core::PaintSettings settings{};
};

struct RestorePaintPreview
{
    CommandId id{};
};

struct CancelPaint
{
    CommandId id{};
};

struct StartImagePaint
{
    CommandId id{};
    std::string project_id{};
    std::uint64_t project_revision{};
};

struct PreviewImagePaint
{
    CommandId id{};
    std::string project_id{};
    std::uint64_t project_revision{};
};

struct RestoreImagePaintPreview
{
    CommandId id{};
};

struct CancelImagePaint
{
    CommandId id{};
};

struct ToggleUi
{
    CommandId id{};
};

struct ToggleEsp
{
    CommandId id{};
};

struct ApplyValidatedSettings
{
    CommandId id{};
    core::ApplicationConfig settings{};
};

struct LoadImageProject
{
    CommandId id{};
    std::string project_id{};
};

struct ImportImageProject
{
    CommandId id{};
    std::shared_ptr<const std::vector<std::byte>> bytes{};
};

struct SaveImageProject
{
    CommandId id{};
    std::string project_id{};
    std::uint64_t expected_revision{};
};

struct RenameImageProject
{
    CommandId id{};
    std::string project_id{};
    std::uint64_t expected_revision{};
    std::string new_name{};
};

struct DeleteImageProject
{
    CommandId id{};
    std::string project_id{};
};

struct MutateImageProject
{
    CommandId id{};
    std::string project_id{};
    std::uint64_t expected_revision{};
    ImageEditorMutation mutation{};
};

using ApplicationCommand = std::variant<
    StartPaint,
    PreviewPaint,
    RestorePaintPreview,
    CancelPaint,
    StartImagePaint,
    PreviewImagePaint,
    RestoreImagePaintPreview,
    CancelImagePaint,
    ToggleUi,
    ToggleEsp,
    ApplyValidatedSettings,
    LoadImageProject,
    ImportImageProject,
    SaveImageProject,
    RenameImageProject,
    DeleteImageProject,
    MutateImageProject>;
} // namespace meccha::application
