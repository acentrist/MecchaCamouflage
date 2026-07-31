#pragma once

#include <meccha/application/mesh_profile_codec.hpp>
#include <meccha/core/paint_sampling_profile.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace meccha::application
{
inline constexpr std::size_t ImagePaintProfilePairCount = 3U;

struct ImagePaintProfileDocuments
{
    core::BodyProfile body{core::BodyProfile::Round};
    std::string_view raw_json{};
    std::string_view image_json{};
};

struct ImagePaintProfilePair
{
    core::PaintSamplingProfile sampling{};
    core::CanonicalImageProfile image{};
};

enum class ImagePaintProfileCatalogErrorCode : std::uint8_t
{
    InvalidDocumentCount,
    DuplicateBody,
    MissingBody,
    RawProfile,
    ImageProfile,
    InvalidPair,
    InvalidDirectory,
    FileRead,
    Construction,
};

struct ImagePaintProfileCatalogError
{
    ImagePaintProfileCatalogErrorCode code{};
    std::optional<core::BodyProfile> body{};
    std::optional<MeshProfileCodecErrorCode> codec_error{};
    std::string detail{};

    auto operator==(const ImagePaintProfileCatalogError&) const
        -> bool = default;
};

class ImagePaintProfileCatalog final
{
public:
    [[nodiscard]] static auto create(
        std::span<const ImagePaintProfileDocuments> documents)
        -> std::expected<
            ImagePaintProfileCatalog,
            ImagePaintProfileCatalogError>;

    [[nodiscard]] auto find(core::BodyProfile body) const noexcept
        -> std::shared_ptr<const ImagePaintProfilePair>;

    [[nodiscard]] auto size() const noexcept -> std::size_t;

private:
    ImagePaintProfileCatalog() = default;

    std::array<
        std::shared_ptr<const ImagePaintProfilePair>,
        ImagePaintProfilePairCount>
        pairs_{};
};

[[nodiscard]] auto load_image_paint_profile_catalog(
    const std::filesystem::path& profile_directory)
    -> std::expected<
        ImagePaintProfileCatalog,
        ImagePaintProfileCatalogError>;
} // namespace meccha::application
