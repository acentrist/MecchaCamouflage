#include <meccha/product_ui/image_editor_texture_coordinator.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <iostream>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
using namespace meccha;
using namespace meccha::application;
using namespace meccha::product_ui;

constexpr auto ProjectId =
    std::string_view{"0123456789abcdef0123456789abcdef"};
constexpr auto AssetId =
    std::string_view{
        "11111111111111111111111111111111"
        "11111111111111111111111111111111"};

auto expect(bool condition, std::string_view message) -> bool
{
    if (!condition)
    {
        std::cerr << "FAIL image_editor_texture_coordinator: "
                  << message << '\n';
    }
    return condition;
}

class ThreadContext final : public GameThreadContext
{
public:
    [[nodiscard]] auto is_game_thread() const noexcept
        -> bool override
    {
        return game_thread;
    }

    bool game_thread{true};
};

class TextureRuntime final : public ImageEditorTextureRuntimePort
{
public:
    auto create_texture(const ImageEditorTextureUpload& upload)
        -> std::expected<
            ui::CanvasTextureHandle,
            ImageEditorTextureRuntimeError> override
    {
        ++create_calls;
        uploads.push_back(upload);
        if (fail_create_call == create_calls)
        {
            return std::unexpected(
                ImageEditorTextureRuntimeError{
                    "create failure",
                });
        }
        const auto handle =
            ui::CanvasTextureHandle{next_handle++};
        live.insert(handle.identity);
        return handle;
    }

    auto release_texture(ui::CanvasTextureHandle handle)
        -> std::expected<
            void,
            ImageEditorTextureRuntimeError> override
    {
        release_attempts.push_back(handle.identity);
        if (fail_release.erase(handle.identity) != 0U)
        {
            return std::unexpected(
                ImageEditorTextureRuntimeError{
                    "release failure",
                });
        }
        if (live.erase(handle.identity) != 1U)
        {
            return std::unexpected(
                ImageEditorTextureRuntimeError{
                    "unknown texture",
                });
        }
        released.push_back(handle.identity);
        return {};
    }

    std::uint64_t next_handle{1U};
    std::size_t create_calls{};
    std::size_t fail_create_call{};
    std::vector<ImageEditorTextureUpload> uploads{};
    std::vector<std::uint64_t> release_attempts{};
    std::vector<std::uint64_t> released{};
    std::set<std::uint64_t> fail_release{};
    std::set<std::uint64_t> live{};
};

auto guide(core::BodyProfile body)
    -> core::ImageGuideBitmap
{
    auto rgba = std::vector<std::byte>(
        core::CanonicalAtlasByteLength,
        std::byte{});
    rgba[3U] = std::byte{0xFF};
    return core::ImageGuideBitmap{
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

auto content(
    std::uint64_t revision,
    core::BodyProfile body = core::BodyProfile::Round)
    -> std::shared_ptr<const ImageEditorReadyContent>
{
    auto encoded =
        std::make_shared<const std::vector<std::byte>>(
            std::initializer_list<std::byte>{
                std::byte{0x11},
            });
    auto atlas =
        std::make_shared<const std::vector<std::byte>>(
            core::CanonicalAtlasByteLength,
            static_cast<std::byte>(revision));
    auto project =
        std::make_shared<const core::ImageProject>(
            core::ImageProject{
                core::ImageProjectSchemaVersion,
                std::string{ProjectId},
                "Project",
                revision,
                core::ImageProjectSettings{body},
                {core::ImageLayer{
                    std::string{AssetId},
                    "source.png",
                    core::ImageMime::Png,
                    encoded->size(),
                }},
                {core::ImageSourceAsset{
                    std::string{AssetId},
                    core::ImageMime::Png,
                    encoded,
                }},
                std::move(atlas),
            });
    auto decoded =
        std::make_shared<
            const std::vector<core::DecodedImageSource>>(
            std::initializer_list<core::DecodedImageSource>{
                core::DecodedImageSource{
                    std::string{AssetId},
                    2U,
                    1U,
                    std::make_shared<
                        const std::vector<std::byte>>(
                        8U,
                        std::byte{0x44}),
                },
            });
    return std::make_shared<const ImageEditorReadyContent>(
        ImageEditorReadyContent{
            std::move(project),
            std::move(decoded),
        });
}
} // namespace

auto main() -> int
{
    auto passed = true;
    auto thread = ThreadContext{};
    auto runtime = TextureRuntime{};
    auto coordinator =
        ImageEditorTextureCoordinator{thread, runtime};
    const auto guides = std::array{
        guide(core::BodyProfile::Round),
        guide(core::BodyProfile::Cube),
        guide(core::BodyProfile::Fukuyoka),
    };

    thread.game_thread = false;
    passed &= expect(
        coordinator.install_guides(guides) ==
                std::unexpected(
                    ImageEditorTextureError{
                        ImageEditorTextureErrorCode::WrongThread,
                    }) &&
            runtime.create_calls == 0U,
        "guide installation entered the runtime off the game thread");

    thread.game_thread = true;
    passed &= expect(
        coordinator.install_guides(guides).has_value() &&
            runtime.create_calls == 3U &&
            coordinator.snapshot().guide_textures == 3U,
        "the three exact guide textures were not installed once");
    passed &= expect(
        coordinator.install_guides(guides) ==
            std::unexpected(
                ImageEditorTextureError{
                    ImageEditorTextureErrorCode::AlreadyInitialized,
                }),
        "guide installation could replace a live guide catalog");

    const auto revision_one = content(1U);
    passed &= expect(
        coordinator.synchronize(revision_one).has_value(),
        "ready editor content did not create frame textures");
    const auto first = coordinator.frame_assets();
    passed &= expect(
        first &&
            first->project_id == ProjectId &&
            first->project_revision == 1U &&
            first->atlas_texture.identity == 4U &&
            first->guide &&
            first->guide->profile.body ==
                core::BodyProfile::Round &&
            first->guide->texture.identity == 1U &&
            first->sources.size() == 1U &&
            first->sources.front().asset_id == AssetId &&
            first->sources.front().width == 2U &&
            first->sources.front().height == 1U &&
            first->sources.front().texture.identity == 5U &&
            coordinator.snapshot().active_project_textures == 2U,
        "frame assets did not bind atlas, source, and matching guide");
    const auto calls_after_first = runtime.create_calls;
    passed &= expect(
        coordinator.synchronize(revision_one).has_value() &&
            runtime.create_calls == calls_after_first &&
            coordinator.frame_assets() == first,
        "an unchanged immutable revision recreated textures");

    runtime.fail_create_call = runtime.create_calls + 2U;
    const auto failed_replacement = content(2U);
    const auto failed_sync =
        coordinator.synchronize(failed_replacement);
    passed &= expect(
        !failed_sync &&
            failed_sync.error().code ==
                ImageEditorTextureErrorCode::Create &&
            failed_sync.error().runtime &&
            coordinator.frame_assets() == first &&
            !runtime.live.contains(6U),
        "a partial replacement escaped or disturbed live assets");

    runtime.fail_create_call = 0U;
    const auto replacement = content(2U);
    runtime.fail_release.insert(4U);
    passed &= expect(
        coordinator.synchronize(replacement).has_value(),
        "a release retry prevented valid replacement publication");
    const auto second = coordinator.frame_assets();
    passed &= expect(
        second && second->project_revision == 2U &&
            second->atlas_texture.identity == 7U &&
            second->sources.front().texture.identity == 8U &&
            coordinator.snapshot().pending_releases == 1U &&
            runtime.live.contains(4U) &&
            !runtime.live.contains(5U),
        "replacement did not retain exactly the failed retirement");
    passed &= expect(
        coordinator.synchronize(replacement).has_value() &&
            coordinator.snapshot().pending_releases == 0U &&
            !runtime.live.contains(4U),
        "a later frame did not retry the failed texture retirement");

    passed &= expect(
        coordinator.clear().has_value() &&
            !coordinator.frame_assets() &&
            coordinator.snapshot().active_project_textures == 0U &&
            coordinator.snapshot().guide_textures == 3U &&
            runtime.live.size() == 3U,
        "clear did not release only project-owned textures");

    runtime.fail_release.insert(1U);
    const auto first_shutdown = coordinator.shutdown();
    passed &= expect(
        !first_shutdown &&
            first_shutdown.error().code ==
                ImageEditorTextureErrorCode::Release &&
            first_shutdown.error().runtime &&
            coordinator.snapshot().stopping &&
            coordinator.snapshot().pending_releases == 1U &&
            runtime.live.size() == 1U,
        "shutdown discarded a failed guide release");
    passed &= expect(
        coordinator.shutdown().has_value() &&
            coordinator.snapshot().stopped &&
            coordinator.snapshot().pending_releases == 0U &&
            runtime.live.empty() &&
            coordinator.synchronize(content(3U)) ==
                std::unexpected(
                    ImageEditorTextureError{
                        ImageEditorTextureErrorCode::Stopped,
                    }),
        "retryable shutdown did not reach a resource-free terminal state");

    if (passed)
    {
        std::cout << "PASS image_editor_texture_coordinator\n";
        return 0;
    }
    return 1;
}
