#pragma once

#include <meccha/core/image_profile_mapping.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace meccha::core
{
struct PaintSamplingVertex
{
    double u{};
    double v{};

    auto operator==(const PaintSamplingVertex&) const -> bool = default;
};

struct PaintSamplingTriangle
{
    std::uint32_t first{};
    std::uint32_t second{};
    std::uint32_t third{};
    std::uint32_t uv_island{};

    auto operator==(const PaintSamplingTriangle&) const -> bool = default;
};

struct PaintSamplingBone
{
    std::string name{};
    std::optional<std::size_t> parent{};

    auto operator==(const PaintSamplingBone&) const -> bool = default;
};

struct PaintSamplingProfile
{
    MeshProfileIdentity identity{};
    std::shared_ptr<const std::vector<PaintSamplingVertex>> vertices{};
    std::shared_ptr<const std::vector<PaintSamplingTriangle>> triangles{};
    std::shared_ptr<const std::vector<PaintSamplingBone>> bones{};
};

enum class PaintSamplingProfileField : std::uint8_t
{
    Identity,
    Vertices,
    Triangles,
    Bones,
    Topology,
    PairIdentity,
    PairTopology,
};

[[nodiscard]] auto validate(const PaintSamplingProfile& profile)
    -> std::vector<PaintSamplingProfileField>;

[[nodiscard]] auto validate_pair(
    const PaintSamplingProfile& sampling,
    const CanonicalImageProfile& image)
    -> std::vector<PaintSamplingProfileField>;
} // namespace meccha::core
