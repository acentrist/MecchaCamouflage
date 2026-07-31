#pragma once

#include <meccha/core/mesh_profile.hpp>

#include <cstdint>
#include <memory>
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

struct PaintSamplingProfile
{
    MeshProfileIdentity identity{};
    std::shared_ptr<const std::vector<PaintSamplingVertex>> vertices{};
    std::shared_ptr<const std::vector<PaintSamplingTriangle>> triangles{};
};
} // namespace meccha::core
