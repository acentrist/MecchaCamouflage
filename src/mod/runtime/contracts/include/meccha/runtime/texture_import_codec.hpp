#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <type_traits>

namespace meccha::runtime
{
struct TextureImportByteArrayAbi
{
    const std::byte* data{};
    std::int32_t count{};
    std::int32_t capacity{};
};

struct ImportBufferAsTexture2DParametersAbi
{
    void* world_context_object{};
    TextureImportByteArrayAbi buffer{};
    void* return_value{};
};

static_assert(sizeof(TextureImportByteArrayAbi) == 0x10U);
static_assert(std::is_standard_layout_v<TextureImportByteArrayAbi>);
static_assert(alignof(TextureImportByteArrayAbi) == 0x08U);
static_assert(
    offsetof(TextureImportByteArrayAbi, data) == 0x00U);
static_assert(
    offsetof(TextureImportByteArrayAbi, count) == 0x08U);
static_assert(
    offsetof(TextureImportByteArrayAbi, capacity) == 0x0CU);
static_assert(
    sizeof(ImportBufferAsTexture2DParametersAbi) == 0x20U);
static_assert(
    offsetof(
        ImportBufferAsTexture2DParametersAbi,
        buffer) == 0x08U);
static_assert(
    offsetof(
        ImportBufferAsTexture2DParametersAbi,
        return_value) == 0x18U);

enum class TextureImportCodecError : std::uint8_t
{
    InvalidWorld,
    InvalidBuffer,
    ResourceLimit,
};

[[nodiscard]] auto encode_texture_import(
    void* world_context_object,
    std::span<const std::byte> encoded_image)
    -> std::expected<
        ImportBufferAsTexture2DParametersAbi,
        TextureImportCodecError>;
} // namespace meccha::runtime
