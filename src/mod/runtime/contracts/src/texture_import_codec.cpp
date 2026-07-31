#include <meccha/runtime/texture_import_codec.hpp>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <span>

namespace meccha::runtime
{
auto encode_texture_import(
    void* world_context_object,
    std::span<const std::byte> encoded_image)
    -> std::expected<
        ImportBufferAsTexture2DParametersAbi,
        TextureImportCodecError>
{
    if (world_context_object == nullptr)
    {
        return std::unexpected(
            TextureImportCodecError::InvalidWorld);
    }
    if (encoded_image.empty() || encoded_image.data() == nullptr)
    {
        return std::unexpected(
            TextureImportCodecError::InvalidBuffer);
    }
    if (encoded_image.size() >
        static_cast<std::size_t>(
            std::numeric_limits<std::int32_t>::max()))
    {
        return std::unexpected(
            TextureImportCodecError::ResourceLimit);
    }
    const auto count =
        static_cast<std::int32_t>(encoded_image.size());
    return ImportBufferAsTexture2DParametersAbi{
        world_context_object,
        TextureImportByteArrayAbi{
            encoded_image.data(),
            count,
            count,
        },
        nullptr,
    };
}
} // namespace meccha::runtime
