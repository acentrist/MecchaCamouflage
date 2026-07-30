#include <meccha/product_ui/image_editor_texture_coordinator.hpp>

#include <meccha/core/image_compositor.hpp>
#include <meccha/core/image_project.hpp>

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
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace meccha::product_ui
{
namespace
{
constexpr auto MaximumPendingTextureReleases =
    (core::MaximumImageSources + 1U) * 2U + 3U;

auto error(ImageEditorTextureErrorCode code)
    -> std::unexpected<ImageEditorTextureError>
{
    return std::unexpected(ImageEditorTextureError{
        code,
        std::nullopt,
    });
}

auto runtime_error(
    ImageEditorTextureErrorCode code,
    ImageEditorTextureRuntimeError failure)
    -> std::unexpected<ImageEditorTextureError>
{
    return std::unexpected(ImageEditorTextureError{
        code,
        std::move(failure),
    });
}

auto body_index(core::BodyProfile body)
    -> std::optional<std::size_t>
{
    switch (body)
    {
    case core::BodyProfile::Round:
        return 0U;
    case core::BodyProfile::Cube:
        return 1U;
    case core::BodyProfile::Fukuyoka:
        return 2U;
    }
    return std::nullopt;
}

auto valid_rgba(
    std::uint32_t width,
    std::uint32_t height,
    const std::shared_ptr<const std::vector<std::byte>>& rgba,
    std::uint64_t maximum_bytes) -> bool
{
    if (width == 0U || height == 0U || !rgba)
    {
        return false;
    }
    const auto bytes =
        static_cast<std::uint64_t>(width) *
        static_cast<std::uint64_t>(height) * 4U;
    return bytes <= maximum_bytes &&
           bytes == rgba->size();
}

auto valid_guide(const core::ImageGuideBitmap& guide) -> bool
{
    if (guide.schema_version !=
            core::ImageGuideSchemaVersion ||
        guide.width != core::CanonicalAtlasWidth ||
        guide.height != core::CanonicalAtlasHeight ||
        guide.profile.role !=
            core::MeshProfileRole::ImageReference ||
        !core::validate(guide.profile).empty() ||
        !valid_rgba(
            guide.width,
            guide.height,
            guide.rgba,
            core::CanonicalAtlasByteLength) ||
        guide.projected_triangles == 0U ||
        guide.projected_triangles >
            guide.profile.triangle_count * 4U ||
        guide.bone_segments == 0U)
    {
        return false;
    }
    return std::ranges::any_of(
        std::views::iota(
            std::size_t{},
            guide.rgba->size() / 4U),
        [&guide](std::size_t pixel)
        {
            return guide.rgba->at(pixel * 4U + 3U) !=
                   std::byte{};
        });
}

auto valid_content(
    const application::ImageEditorReadyContent& content) -> bool
{
    if (!content.project || !content.decoded_sources ||
        !core::validate(*content.project).empty() ||
        !content.project->canonical_atlas ||
        content.project->canonical_atlas->size() !=
            core::CanonicalAtlasByteLength ||
        content.decoded_sources->empty() ||
        content.decoded_sources->size() !=
            content.project->sources.size() ||
        content.decoded_sources->size() >
            core::MaximumImageSources)
    {
        return false;
    }

    auto identities = std::vector<std::string_view>{};
    identities.reserve(content.decoded_sources->size());
    for (const auto& decoded : *content.decoded_sources)
    {
        const auto source = std::ranges::find_if(
            content.project->sources,
            [&decoded](const core::ImageSourceAsset& candidate)
            {
                return candidate.asset_id ==
                       decoded.asset_id;
            });
        if (source == content.project->sources.end() ||
            decoded.width >
                core::MaximumDecodedImageDimension ||
            decoded.height >
                core::MaximumDecodedImageDimension ||
            !valid_rgba(
                decoded.width,
                decoded.height,
                decoded.rgba,
                core::MaximumDecodedImageBytes) ||
            std::ranges::find(
                identities,
                decoded.asset_id) != identities.end())
        {
            return false;
        }
        identities.push_back(decoded.asset_id);
    }
    return true;
}

auto contains_handle(
    std::span<const ui::CanvasTextureHandle> handles,
    ui::CanvasTextureHandle candidate) -> bool
{
    return candidate.identity != 0U &&
           std::ranges::any_of(
               handles,
               [candidate](ui::CanvasTextureHandle handle)
               {
                   return handle == candidate;
               });
}
} // namespace

ImageEditorTextureCoordinator::ImageEditorTextureCoordinator(
    application::GameThreadContext& game_thread,
    ImageEditorTextureRuntimePort& runtime)
    : game_thread_{game_thread},
      runtime_{runtime}
{
}

auto ImageEditorTextureCoordinator::validate_thread() const
    -> std::expected<void, ImageEditorTextureError>
{
    if (!game_thread_.is_game_thread())
    {
        return error(
            ImageEditorTextureErrorCode::WrongThread);
    }
    return {};
}

auto ImageEditorTextureCoordinator::install_guides(
    std::span<const core::ImageGuideBitmap> guides)
    -> std::expected<void, ImageEditorTextureError>
{
    const auto thread = validate_thread();
    if (!thread)
    {
        return std::unexpected(thread.error());
    }
    if (stopping_ || stopped_)
    {
        return error(ImageEditorTextureErrorCode::Stopped);
    }
    if (guides_initialized_)
    {
        return error(
            ImageEditorTextureErrorCode::AlreadyInitialized);
    }
    if (guides.size() != guides_.size())
    {
        return error(
            ImageEditorTextureErrorCode::InvalidGuideCatalog);
    }

    auto seen = std::array<bool, 3U>{};
    for (const auto& guide : guides)
    {
        const auto index = body_index(guide.profile.body);
        if (!index || seen[*index] || !valid_guide(guide))
        {
            return error(
                ImageEditorTextureErrorCode::
                    InvalidGuideCatalog);
        }
        seen[*index] = true;
    }

    auto created =
        std::vector<ui::CanvasTextureHandle>{};
    auto staged =
        std::array<std::optional<GuideTexture>, 3U>{};
    for (const auto& guide : guides)
    {
        auto uploaded = runtime_.create_texture(
            ImageEditorTextureUpload{
                ImageEditorTextureKind::Guide,
                guide.profile.profile_id,
                guide.schema_version,
                guide.width,
                guide.height,
                guide.rgba,
            });
        if (!uploaded)
        {
            retire(created);
            static_cast<void>(drain_pending_releases());
            return runtime_error(
                ImageEditorTextureErrorCode::Create,
                std::move(uploaded.error()));
        }
        if (uploaded->identity == 0U ||
            contains_handle(created, *uploaded) ||
            contains_handle(pending_releases_, *uploaded))
        {
            retire(created);
            static_cast<void>(drain_pending_releases());
            return error(
                ImageEditorTextureErrorCode::InvalidHandle);
        }
        created.push_back(*uploaded);
        staged[*body_index(guide.profile.body)] =
            GuideTexture{
                guide.profile,
                *uploaded,
            };
    }
    guides_ = std::move(staged);
    guides_initialized_ = true;
    return {};
}

auto ImageEditorTextureCoordinator::synchronize(
    std::shared_ptr<
        const application::ImageEditorReadyContent> content)
    -> std::expected<void, ImageEditorTextureError>
{
    const auto thread = validate_thread();
    if (!thread)
    {
        return std::unexpected(thread.error());
    }
    if (stopping_ || stopped_)
    {
        return error(ImageEditorTextureErrorCode::Stopped);
    }
    if (!guides_initialized_)
    {
        return error(
            ImageEditorTextureErrorCode::NotInitialized);
    }
    static_cast<void>(drain_pending_releases());
    if (!content || !valid_content(*content))
    {
        return error(
            ImageEditorTextureErrorCode::InvalidContent);
    }
    const auto* guide =
        guide_for(content->project->settings.body);
    if (guide == nullptr)
    {
        return error(
            ImageEditorTextureErrorCode::NotInitialized);
    }
    if (active_ &&
        active_->assets.project_id ==
            content->project->project_id &&
        active_->assets.project_revision ==
            content->project->revision)
    {
        if (active_->content != content)
        {
            return error(
                ImageEditorTextureErrorCode::
                    IdentityConflict);
        }
        return {};
    }
    if (pending_releases_.size() +
            (active_ ? active_->handles.size() : 0U) >
        MaximumPendingTextureReleases)
    {
        return error(ImageEditorTextureErrorCode::Release);
    }

    auto created =
        std::vector<ui::CanvasTextureHandle>{};
    const auto create =
        [&](ImageEditorTextureUpload upload)
            -> std::expected<
                ui::CanvasTextureHandle,
                ImageEditorTextureError>
        {
            auto result = runtime_.create_texture(upload);
            if (!result)
            {
                return runtime_error(
                    ImageEditorTextureErrorCode::Create,
                    std::move(result.error()));
            }
            auto guide_handles =
                std::array<ui::CanvasTextureHandle, 3U>{};
            for (auto index = std::size_t{};
                 index < guides_.size();
                 ++index)
            {
                guide_handles[index] =
                    guides_[index]->handle;
            }
            if (result->identity == 0U ||
                contains_handle(created, *result) ||
                contains_handle(guide_handles, *result) ||
                contains_handle(pending_releases_, *result) ||
                (active_ &&
                 contains_handle(
                     active_->handles,
                     *result)))
            {
                return std::unexpected(
                    ImageEditorTextureError{
                        ImageEditorTextureErrorCode::
                            InvalidHandle,
                    });
            }
            created.push_back(*result);
            return *result;
        };

    const auto atlas = create(ImageEditorTextureUpload{
        ImageEditorTextureKind::Atlas,
        content->project->project_id,
        content->project->revision,
        core::CanonicalAtlasWidth,
        core::CanonicalAtlasHeight,
        content->project->canonical_atlas,
    });
    if (!atlas)
    {
        retire(created);
        static_cast<void>(drain_pending_releases());
        return std::unexpected(atlas.error());
    }

    auto sources = std::vector<ImageSourceFrameAsset>{};
    sources.reserve(content->decoded_sources->size());
    for (const auto& source : *content->decoded_sources)
    {
        const auto texture = create(ImageEditorTextureUpload{
            ImageEditorTextureKind::Source,
            source.asset_id,
            content->project->revision,
            source.width,
            source.height,
            source.rgba,
        });
        if (!texture)
        {
            retire(created);
            static_cast<void>(drain_pending_releases());
            return std::unexpected(texture.error());
        }
        sources.push_back(ImageSourceFrameAsset{
            source.asset_id,
            source.width,
            source.height,
            *texture,
        });
    }

    auto replacement = ActiveProjectTextures{
        std::move(content),
        ImageEditorFrameAssets{
            {},
            0U,
            *atlas,
            ui::ImageGuideOverlay{
                ui::ImageGuideOverlaySchemaVersion,
                guide->profile,
                guide->handle,
            },
            std::move(sources),
        },
        std::move(created),
    };
    replacement.assets.project_id =
        replacement.content->project->project_id;
    replacement.assets.project_revision =
        replacement.content->project->revision;
    if (active_)
    {
        retire(active_->handles);
    }
    active_ = std::move(replacement);
    static_cast<void>(drain_pending_releases());
    return {};
}

auto ImageEditorTextureCoordinator::clear()
    -> std::expected<void, ImageEditorTextureError>
{
    const auto thread = validate_thread();
    if (!thread)
    {
        return std::unexpected(thread.error());
    }
    if (stopped_)
    {
        return error(ImageEditorTextureErrorCode::Stopped);
    }
    if (active_)
    {
        retire(active_->handles);
        active_.reset();
    }
    if (!drain_pending_releases())
    {
        return runtime_error(
            ImageEditorTextureErrorCode::Release,
            *last_release_failure_);
    }
    return {};
}

auto ImageEditorTextureCoordinator::shutdown()
    -> std::expected<void, ImageEditorTextureError>
{
    const auto thread = validate_thread();
    if (!thread)
    {
        return std::unexpected(thread.error());
    }
    if (stopped_)
    {
        return {};
    }
    stopping_ = true;
    if (active_)
    {
        retire(active_->handles);
        active_.reset();
    }
    if (guides_initialized_)
    {
        for (const auto& guide : guides_)
        {
            if (guide)
            {
                const auto handle = guide->handle;
                retire(std::span{&handle, 1U});
            }
        }
        guides_ = {};
        guides_initialized_ = false;
    }
    if (!drain_pending_releases())
    {
        return runtime_error(
            ImageEditorTextureErrorCode::Release,
            *last_release_failure_);
    }
    stopping_ = false;
    stopped_ = true;
    return {};
}

auto ImageEditorTextureCoordinator::frame_assets() const
    -> std::optional<ImageEditorFrameAssets>
{
    if (!active_)
    {
        return std::nullopt;
    }
    return active_->assets;
}

auto ImageEditorTextureCoordinator::snapshot() const
    -> ImageEditorTextureCoordinatorSnapshot
{
    return {
        static_cast<std::size_t>(
            std::ranges::count_if(
                guides_,
                [](const auto& guide)
                {
                    return guide.has_value();
                })),
        active_ ? active_->handles.size() : 0U,
        pending_releases_.size(),
        stopping_,
        stopped_,
        last_release_failure_,
    };
}

auto ImageEditorTextureCoordinator::drain_pending_releases()
    -> bool
{
    last_release_failure_.reset();
    auto retained =
        std::vector<ui::CanvasTextureHandle>{};
    retained.reserve(pending_releases_.size());
    for (const auto handle : pending_releases_)
    {
        auto released = runtime_.release_texture(handle);
        if (!released)
        {
            if (!last_release_failure_)
            {
                last_release_failure_ =
                    released.error();
            }
            retained.push_back(handle);
        }
    }
    pending_releases_ = std::move(retained);
    return pending_releases_.empty();
}

auto ImageEditorTextureCoordinator::retire(
    std::span<const ui::CanvasTextureHandle> handles) -> void
{
    for (const auto handle : handles)
    {
        if (handle.identity != 0U &&
            !contains_handle(pending_releases_, handle))
        {
            pending_releases_.push_back(handle);
        }
    }
}

auto ImageEditorTextureCoordinator::guide_for(
    core::BodyProfile body) const -> const GuideTexture*
{
    const auto index = body_index(body);
    if (!index || !guides_[*index])
    {
        return nullptr;
    }
    return &*guides_[*index];
}
} // namespace meccha::product_ui
