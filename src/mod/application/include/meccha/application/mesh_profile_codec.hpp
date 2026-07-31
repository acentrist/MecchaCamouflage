#pragma once

#include <meccha/core/image_profile_mapping.hpp>
#include <meccha/core/paint_sampling_profile.hpp>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <vector>

namespace meccha::application
{
inline constexpr std::size_t MaximumMeshProfileBytes =
    8U * 1024U * 1024U;

enum class MeshProfileCodecErrorCode : std::uint8_t
{
    TooLarge,
    MalformedJson,
    InvalidProfile,
};

struct MeshProfileCodecError
{
    MeshProfileCodecErrorCode code{};
    std::vector<core::MeshProfileField> fields{};
    std::string detail{};

    auto operator==(const MeshProfileCodecError&) const -> bool = default;
};

[[nodiscard]] auto decode_mesh_profile_identity(
    std::string_view json,
    core::BodyProfile body,
    core::MeshProfileRole role)
    -> std::expected<
        core::MeshProfileIdentity,
        MeshProfileCodecError>;

[[nodiscard]] auto decode_paint_sampling_profile(
    std::string_view json,
    core::BodyProfile body)
    -> std::expected<
        core::PaintSamplingProfile,
        MeshProfileCodecError>;

[[nodiscard]] auto decode_canonical_image_profile(
    std::string_view json,
    core::BodyProfile body)
    -> std::expected<
        core::CanonicalImageProfile,
        MeshProfileCodecError>;
} // namespace meccha::application
