#pragma once

#include <meccha/runtime/paint_call_codec.hpp>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <type_traits>

namespace meccha::runtime
{
inline constexpr std::size_t MaximumPaintChannelBytes =
    64U * 1024U * 1024U;
inline constexpr std::uint32_t MaximumPaintTextureDimension =
    4096U;

struct RuntimeByteArrayAbi
{
    const std::byte* data{};
    std::int32_t count{};
    std::int32_t capacity{};
};

struct ExportChannelToBytesParametersAbi
{
    RuntimePaintChannel channel{RuntimePaintChannel::Albedo};
    std::byte padding_01[0x07]{};
    RuntimeByteArrayAbi out_data{};
    bool return_value{};
    std::byte padding_19[0x07]{};
};

struct ImportChannelFromBytesParametersAbi
{
    RuntimePaintChannel channel{RuntimePaintChannel::Albedo};
    std::byte padding_01[0x07]{};
    RuntimeByteArrayAbi data{};
    bool return_value{};
    std::byte padding_19[0x07]{};
};

static_assert(sizeof(RuntimeByteArrayAbi) == 0x10U);
static_assert(std::is_standard_layout_v<RuntimeByteArrayAbi>);
static_assert(alignof(RuntimeByteArrayAbi) == 0x08U);
static_assert(offsetof(RuntimeByteArrayAbi, data) == 0x00U);
static_assert(offsetof(RuntimeByteArrayAbi, count) == 0x08U);
static_assert(offsetof(RuntimeByteArrayAbi, capacity) == 0x0CU);
static_assert(sizeof(ExportChannelToBytesParametersAbi) == 0x20U);
static_assert(
    offsetof(ExportChannelToBytesParametersAbi, out_data) ==
    0x08U);
static_assert(
    offsetof(
        ExportChannelToBytesParametersAbi,
        return_value) == 0x18U);
static_assert(sizeof(ImportChannelFromBytesParametersAbi) == 0x20U);
static_assert(
    offsetof(ImportChannelFromBytesParametersAbi, data) ==
    0x08U);
static_assert(
    offsetof(
        ImportChannelFromBytesParametersAbi,
        return_value) == 0x18U);

enum class PaintPreviewCodecError : std::uint8_t
{
    InvalidBytes,
    MismatchedChannels,
    InvalidDimension,
    ResourceLimit,
};

[[nodiscard]] auto infer_paint_texture_dimension(
    std::span<const std::byte> albedo,
    std::span<const std::byte> packed_pbr)
    -> std::expected<
        std::uint32_t,
        PaintPreviewCodecError>;

[[nodiscard]] auto encode_channel_import(
    RuntimePaintChannel channel,
    std::span<const std::byte> bytes)
    -> std::expected<
        ImportChannelFromBytesParametersAbi,
        PaintPreviewCodecError>;
} // namespace meccha::runtime
