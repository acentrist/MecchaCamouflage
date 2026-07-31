#include <meccha/core/png_encoder.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <iostream>
#include <span>
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
        std::cerr << "FAIL png_encoder: " << message << '\n';
    }
    return condition;
}

auto has_png_signature(std::span<const std::byte> bytes) -> bool
{
    constexpr auto signature = std::array{
        std::byte{0x89},
        std::byte{'P'},
        std::byte{'N'},
        std::byte{'G'},
        std::byte{0x0D},
        std::byte{0x0A},
        std::byte{0x1A},
        std::byte{0x0A},
    };
    return bytes.size() >= signature.size() &&
           std::equal(signature.begin(), signature.end(), bytes.begin());
}
} // namespace

auto main() -> int
{
    using namespace meccha::core;

    auto passed = true;
    const auto rgba = std::array{
        std::byte{0x11},
        std::byte{0x22},
        std::byte{0x33},
        std::byte{0x44},
        std::byte{0x55},
        std::byte{0x66},
        std::byte{0x77},
        std::byte{0x88},
    };

    const auto encoded = encode_png_rgba8(2U, 1U, rgba);
    passed &= expect(
        encoded && has_png_signature(*encoded),
        "a valid RGBA image did not produce a PNG");
    const auto inspected =
        encoded ? inspect_canonical_png_rgba8(*encoded)
                : std::expected<PngImageInfo, PngEncodeError>{
                      std::unexpected(
                          PngEncodeError::InvalidEncoding)};
    passed &= expect(
        inspected && inspected->width == 2U &&
            inspected->height == 1U &&
            inspected->rgba_bytes == rgba.size(),
        "the canonical PNG metadata did not round-trip");

    const auto repeated = encode_png_rgba8(2U, 1U, rgba);
    passed &= expect(
        repeated && encoded && *repeated == *encoded,
        "encoding the same pixels was not deterministic");

    auto corrupt = encoded.value_or(std::vector<std::byte>{});
    if (corrupt.size() > 32U)
    {
        corrupt[32U] ^= std::byte{0x01};
    }
    passed &= expect(
        inspect_canonical_png_rgba8(corrupt) ==
            std::unexpected(PngEncodeError::InvalidEncoding),
        "a corrupt chunk CRC was accepted");

    passed &= expect(
        encode_png_rgba8(0U, 1U, {}) ==
            std::unexpected(PngEncodeError::InvalidDimensions),
        "zero-width PNG input was accepted");
    passed &= expect(
        encode_png_rgba8(
            2U,
            1U,
            std::span<const std::byte>{rgba}.first(7U)) ==
            std::unexpected(PngEncodeError::InvalidPixels),
        "a mismatched RGBA byte count was accepted");

    auto cancelled = std::stop_source{};
    cancelled.request_stop();
    passed &= expect(
        encode_png_rgba8(
            2U,
            1U,
            rgba,
            cancelled.get_token()) ==
            std::unexpected(PngEncodeError::Cancelled),
        "pre-cancelled PNG work was executed");

    if (passed)
    {
        std::cout << "PASS png_encoder\n";
        return 0;
    }
    return 1;
}
