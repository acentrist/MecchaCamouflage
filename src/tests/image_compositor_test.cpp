#include <meccha/core/image_compositor.hpp>

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <stop_token>
#include <string_view>
#include <vector>

namespace
{
using namespace meccha::core;

auto expect(bool condition, std::string_view message) -> bool
{
    if (!condition)
    {
        std::cerr << "FAIL image_compositor: "
                  << message << '\n';
    }
    return condition;
}

auto bytes(std::initializer_list<std::uint8_t> values)
    -> std::shared_ptr<const std::vector<std::byte>>
{
    auto result = std::vector<std::byte>{};
    result.reserve(values.size());
    for (const auto value : values)
    {
        result.push_back(static_cast<std::byte>(value));
    }
    return std::make_shared<const std::vector<std::byte>>(
        std::move(result));
}

auto source(
    std::string id,
    std::uint32_t width,
    std::uint32_t height,
    std::shared_ptr<const std::vector<std::byte>> rgba)
    -> DecodedImageSource
{
    return DecodedImageSource{
        std::move(id),
        width,
        height,
        std::move(rgba),
    };
}

auto layer(std::string id = "source") -> ImageLayer
{
    return ImageLayer{
        std::move(id),
        "source.png",
        ImageMime::Png,
        4U,
    };
}

auto channel(
    const ImageAtlasComposition& composition,
    std::uint32_t x,
    std::uint32_t y,
    std::size_t channel_index) -> std::uint8_t
{
    const auto offset =
        (static_cast<std::size_t>(y) *
             CanonicalAtlasWidth +
         x) *
            4U +
        channel_index;
    return std::to_integer<std::uint8_t>(
        composition.rgba.at(offset));
}

auto alpha(
    const ImageAtlasComposition& composition,
    std::uint32_t x,
    std::uint32_t y) -> std::uint8_t
{
    return channel(composition, x, y, 3U);
}
} // namespace

auto main() -> int
{
    using namespace meccha::core;

    auto passed = true;
    auto settings = ImageProjectSettings{};
    settings.placement = PlacementMode::Fill;

    auto bottom = layer("red");
    auto top = layer("blue");
    const auto ordered = compose_image_atlas(
        settings,
        std::vector{bottom, top},
        std::vector{
            source("red", 1U, 1U, bytes({255U, 0U, 0U, 255U})),
            source("blue", 1U, 1U, bytes({0U, 0U, 255U, 128U})),
        });
    passed &= expect(
        ordered &&
            ordered->rgba.size() == CanonicalAtlasByteLength &&
            ordered->layers_composed == 2U &&
            channel(*ordered, 512U, 256U, 0U) == 127U &&
            channel(*ordered, 512U, 256U, 1U) == 0U &&
            channel(*ordered, 512U, 256U, 2U) == 128U &&
            alpha(*ordered, 512U, 256U) == 255U,
        "explicit layer order or source-over alpha composition drifted");

    auto cropped = layer();
    cropped.crop = NormalizedCrop{0.5, 0.0, 0.5, 1.0};
    const auto crop_result = compose_image_atlas(
        settings,
        std::vector{cropped},
        std::vector{source(
            "source",
            2U,
            1U,
            bytes({
                255U, 0U, 0U, 255U,
                0U, 255U, 0U, 255U,
            }))});
    passed &= expect(
        crop_result &&
            channel(*crop_result, 512U, 256U, 0U) == 0U &&
            channel(*crop_result, 512U, 256U, 1U) == 255U,
        "normalized crop sampled outside its source rectangle");

    auto fitted_layer = layer();
    fitted_layer.width = 0.25;
    fitted_layer.height = 0.5;
    auto fit_settings = settings;
    fit_settings.placement = PlacementMode::Fit;
    const auto fitted = compose_image_atlas(
        fit_settings,
        std::vector{fitted_layer},
        std::vector{source(
            "source",
            2U,
            1U,
            bytes({
                255U, 0U, 0U, 255U,
                0U, 255U, 0U, 255U,
            }))});
    const auto filled = compose_image_atlas(
        settings,
        std::vector{fitted_layer},
        std::vector{source(
            "source",
            2U,
            1U,
            bytes({
                255U, 0U, 0U, 255U,
                0U, 255U, 0U, 255U,
            }))});
    passed &= expect(
        fitted && filled &&
            alpha(*fitted, 512U, 150U) == 0U &&
            alpha(*filled, 512U, 150U) == 255U,
        "Fit letterboxing and Fill cropping were not distinct");

    auto wrapped = layer();
    wrapped.center_x = 0.0;
    wrapped.width = 0.2;
    wrapped.height = 0.2;
    wrapped.wrap_atlas_seam = true;
    const auto seam = compose_image_atlas(
        settings,
        std::vector{wrapped},
        std::vector{source(
            "source",
            1U,
            1U,
            bytes({255U, 0U, 0U, 255U}))});
    passed &= expect(
        seam &&
            channel(*seam, 10U, 256U, 0U) == 255U &&
            channel(*seam, 1000U, 256U, 0U) == 255U,
        "seam wrapping did not publish clipped copies on both atlas edges");

    auto mirrored = layer();
    mirrored.center_x = 0.125;
    mirrored.width = 0.25;
    mirrored.height = 0.25;
    mirrored.mirror_front_back = true;
    const auto mirror = compose_image_atlas(
        settings,
        std::vector{mirrored},
        std::vector{source(
            "source",
            2U,
            1U,
            bytes({
                255U, 0U, 0U, 255U,
                0U, 255U, 0U, 255U,
            }))});
    passed &= expect(
        mirror &&
            channel(*mirror, 32U, 256U, 0U) == 255U &&
            channel(*mirror, 224U, 256U, 1U) == 255U &&
            channel(*mirror, 544U, 256U, 1U) == 255U &&
            channel(*mirror, 736U, 256U, 0U) == 255U,
        "front/back mirror placement or horizontal reflection drifted");

    auto background_settings = settings;
    background_settings.alpha = AlphaMode::Background;
    background_settings.fill_color = Rgb8{12U, 34U, 56U};
    const auto background = compose_image_atlas(
        background_settings,
        std::vector{layer()},
        std::vector{source(
            "source",
            1U,
            1U,
            bytes({200U, 100U, 50U, 0U}))});
    passed &= expect(
        background &&
            channel(*background, 0U, 0U, 0U) == 12U &&
            channel(*background, 0U, 0U, 1U) == 34U &&
            channel(*background, 0U, 0U, 2U) == 56U &&
            alpha(*background, 0U, 0U) ==
                ImageBackgroundAlphaMarker,
        "Background alpha mode did not emit the reserved Fill marker");

    const auto malformed = compose_image_atlas(
        settings,
        std::vector{layer()},
        std::vector{source(
            "source",
            2U,
            2U,
            bytes({1U, 2U, 3U}))});
    passed &= expect(
        !malformed &&
            malformed.error() ==
                ImageComposeError::InvalidSource,
        "a truncated decoded RGBA source was accepted");
    const auto missing = compose_image_atlas(
        settings,
        std::vector{layer("missing")},
        std::vector{source(
            "source",
            1U,
            1U,
            bytes({0U, 0U, 0U, 255U}))});
    passed &= expect(
        !missing &&
            missing.error() ==
                ImageComposeError::SourceMismatch,
        "a layer without an exact decoded source was accepted");

    auto cancelled = std::stop_source{};
    cancelled.request_stop();
    passed &= expect(
        compose_image_atlas(
            settings,
            std::vector{layer()},
            std::vector{source(
                "source",
                1U,
                1U,
                bytes({0U, 0U, 0U, 255U}))},
            cancelled.get_token()) ==
            std::unexpected(ImageComposeError::Cancelled),
        "a pre-cancelled composition published an atlas");

    auto excessive_layers =
        std::vector<ImageLayer>(MaximumImageLayers, layer());
    for (auto& item : excessive_layers)
    {
        item.wrap_atlas_seam = true;
        item.mirror_front_back = true;
    }
    const auto excessive = compose_image_atlas(
        settings,
        excessive_layers,
        std::vector{source(
            "source",
            1U,
            1U,
            bytes({0U, 0U, 0U, 255U}))});
    passed &= expect(
        !excessive &&
            excessive.error() == ImageComposeError::ResourceLimit,
        "the composition pixel-operation budget was not enforced");

    if (passed)
    {
        std::cout << "PASS image_compositor\n";
    }
    return passed ? 0 : 1;
}
