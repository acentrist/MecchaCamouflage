#pragma once

#include <meccha/application/image_editor_pipeline.hpp>
#include <meccha/application/runtime_operation_executor.hpp>
#include <meccha/core/image_guide.hpp>
#include <meccha/product_ui/product_panel.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace meccha::product_ui
{
enum class ImageEditorTextureKind : std::uint8_t
{
    Atlas,
    Source,
    Guide,
};

struct ImageEditorTextureUpload
{
    ImageEditorTextureKind kind{
        ImageEditorTextureKind::Atlas};
    std::string identity{};
    std::uint64_t revision{};
    std::uint32_t width{};
    std::uint32_t height{};
    std::shared_ptr<const std::vector<std::byte>> rgba{};

    auto operator==(const ImageEditorTextureUpload&) const
        -> bool = default;
};

struct ImageEditorTextureRuntimeError
{
    std::string detail{};

    auto operator==(const ImageEditorTextureRuntimeError&) const
        -> bool = default;
};

class ImageEditorTextureRuntimePort
{
public:
    ImageEditorTextureRuntimePort() = default;
    ImageEditorTextureRuntimePort(
        const ImageEditorTextureRuntimePort&) = delete;
    auto operator=(const ImageEditorTextureRuntimePort&)
        -> ImageEditorTextureRuntimePort& = delete;
    virtual ~ImageEditorTextureRuntimePort() = default;

    [[nodiscard]] virtual auto create_texture(
        const ImageEditorTextureUpload& upload)
        -> std::expected<
            ui::CanvasTextureHandle,
            ImageEditorTextureRuntimeError> = 0;

    virtual auto release_texture(ui::CanvasTextureHandle handle)
        -> std::expected<
            void,
            ImageEditorTextureRuntimeError> = 0;
};

enum class ImageEditorTextureErrorCode : std::uint8_t
{
    WrongThread,
    InvalidGuideCatalog,
    InvalidContent,
    IdentityConflict,
    AlreadyInitialized,
    NotInitialized,
    Create,
    InvalidHandle,
    Release,
    Stopped,
};

struct ImageEditorTextureError
{
    ImageEditorTextureErrorCode code{};
    std::optional<ImageEditorTextureRuntimeError> runtime{};

    auto operator==(const ImageEditorTextureError&) const
        -> bool = default;
};

struct ImageEditorTextureCoordinatorSnapshot
{
    std::size_t guide_textures{};
    std::size_t active_project_textures{};
    std::size_t pending_releases{};
    bool stopping{};
    bool stopped{};
    std::optional<ImageEditorTextureRuntimeError>
        last_release_failure{};

    auto operator==(
        const ImageEditorTextureCoordinatorSnapshot&) const
        -> bool = default;
};

class ImageEditorTextureCoordinator
{
public:
    ImageEditorTextureCoordinator(
        application::GameThreadContext& game_thread,
        ImageEditorTextureRuntimePort& runtime);
    ImageEditorTextureCoordinator(
        const ImageEditorTextureCoordinator&) = delete;
    auto operator=(const ImageEditorTextureCoordinator&)
        -> ImageEditorTextureCoordinator& = delete;

    [[nodiscard]] auto install_guides(
        std::span<const core::ImageGuideBitmap> guides)
        -> std::expected<void, ImageEditorTextureError>;

    [[nodiscard]] auto synchronize(
        std::shared_ptr<
            const application::ImageEditorReadyContent> content)
        -> std::expected<void, ImageEditorTextureError>;

    [[nodiscard]] auto clear()
        -> std::expected<void, ImageEditorTextureError>;

    [[nodiscard]] auto shutdown()
        -> std::expected<void, ImageEditorTextureError>;

    [[nodiscard]] auto frame_assets() const
        -> std::optional<ImageEditorFrameAssets>;

    [[nodiscard]] auto snapshot() const
        -> ImageEditorTextureCoordinatorSnapshot;

private:
    struct GuideTexture
    {
        core::MeshProfileIdentity profile{};
        ui::CanvasTextureHandle handle{};
    };

    struct ActiveProjectTextures
    {
        std::shared_ptr<
            const application::ImageEditorReadyContent>
            content{};
        ImageEditorFrameAssets assets{};
        std::vector<ui::CanvasTextureHandle> handles{};
    };

    [[nodiscard]] auto validate_thread() const
        -> std::expected<void, ImageEditorTextureError>;
    [[nodiscard]] auto drain_pending_releases() -> bool;
    auto retire(std::span<const ui::CanvasTextureHandle> handles)
        -> void;
    [[nodiscard]] auto guide_for(core::BodyProfile body) const
        -> const GuideTexture*;

    application::GameThreadContext& game_thread_;
    ImageEditorTextureRuntimePort& runtime_;
    std::array<std::optional<GuideTexture>, 3U> guides_{};
    std::optional<ActiveProjectTextures> active_{};
    std::vector<ui::CanvasTextureHandle> pending_releases_{};
    std::optional<ImageEditorTextureRuntimeError>
        last_release_failure_{};
    bool guides_initialized_{};
    bool stopping_{};
    bool stopped_{};
};
} // namespace meccha::product_ui
