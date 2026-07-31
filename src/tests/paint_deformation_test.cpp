#include <meccha/core/paint_deformation.hpp>

#include <cmath>
#include <iostream>
#include <optional>
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
        std::cerr << "FAIL paint_deformation: "
                  << message << '\n';
    }
    return condition;
}

auto near(double left, double right) -> bool
{
    return std::abs(left - right) < 1.0e-9;
}

auto root_bone() -> PaintSamplingBone
{
    return PaintSamplingBone{"root", std::nullopt};
}

auto vertex(
    Vector3d position,
    Vector3d normal,
    std::initializer_list<PaintBoneInfluence> influences)
    -> PaintDeformationVertex
{
    auto result = PaintDeformationVertex{};
    result.position = position;
    result.normal = normal;
    result.influence_count = influences.size();
    auto index = std::size_t{};
    for (const auto influence : influences)
    {
        result.influences[index++] = influence;
    }
    return result;
}
} // namespace

auto main() -> int
{
    using namespace meccha::core;

    auto passed = true;
    const auto bones = std::vector{root_bone()};
    const auto reference = std::vector{
        PaintReferenceBoneTransform{},
    };
    const auto current = std::vector{
        PaintReferenceBoneTransform{
            Vector3d{10.0, 0.0, 0.0},
            PaintQuaternion{},
        },
    };
    const auto vertices = std::vector{
        vertex(
            Vector3d{1.0, 2.0, 3.0},
            Vector3d{1.0, 0.0, 0.0},
            {PaintBoneInfluence{0U, 255U, 1.0}}),
    };
    const auto translated = deform_paint_vertices(
        vertices,
        bones,
        reference,
        current);
    passed &= expect(
        translated && translated->size() == 1U &&
            near(translated->front().position.x, 11.0) &&
            near(translated->front().position.y, 2.0) &&
            near(translated->front().position.z, 3.0) &&
            near(translated->front().normal.x, 1.0) &&
            near(translated->front().normal.y, 0.0) &&
            near(translated->front().normal.z, 0.0),
        "a one-bone world translation was not applied exactly");

    const auto weighted_bones = std::vector{
        root_bone(),
        PaintSamplingBone{
            "child",
            std::optional<std::size_t>{0U},
        },
    };
    const auto weighted_reference = std::vector{
        PaintReferenceBoneTransform{},
        PaintReferenceBoneTransform{},
    };
    const auto weighted_current = std::vector{
        PaintReferenceBoneTransform{
            Vector3d{10.0, 0.0, 0.0},
            PaintQuaternion{},
        },
        PaintReferenceBoneTransform{
            Vector3d{20.0, 0.0, 0.0},
            PaintQuaternion{},
        },
    };
    const auto weighted_vertices = std::vector{
        vertex(
            Vector3d{},
            Vector3d{0.0, 0.0, 1.0},
            {
                PaintBoneInfluence{0U, 64U, 0.25},
                PaintBoneInfluence{1U, 191U, 0.75},
            }),
    };
    const auto weighted = deform_paint_vertices(
        weighted_vertices,
        weighted_bones,
        weighted_reference,
        weighted_current);
    passed &= expect(
        weighted &&
            near(weighted->front().position.x, 17.5) &&
            near(weighted->front().normal.z, 1.0),
        "normalized multi-bone skin weights were not blended");

    const auto quarter_turn =
        std::sqrt(0.5);
    const auto rotated_current = std::vector{
        PaintReferenceBoneTransform{
            {},
            PaintQuaternion{
                0.0,
                0.0,
                quarter_turn,
                quarter_turn,
            },
        },
    };
    const auto rotated = deform_paint_vertices(
        vertices,
        bones,
        reference,
        rotated_current);
    passed &= expect(
        rotated &&
            near(rotated->front().position.x, -2.0) &&
            near(rotated->front().position.y, 1.0) &&
            near(rotated->front().position.z, 3.0) &&
            near(rotated->front().normal.x, 0.0) &&
            near(rotated->front().normal.y, 1.0),
        "bone rotation did not deform positions and normals");

    auto invalid_vertices = vertices;
    invalid_vertices.front().influences[0U].raw_weight = 0U;
    passed &= expect(
        deform_paint_vertices(
            invalid_vertices,
            bones,
            reference,
            current) ==
            std::unexpected(
                PaintDeformationError::InvalidVertex),
        "an invalid influence payload was accepted");

    auto cancellation = std::stop_source{};
    cancellation.request_stop();
    passed &= expect(
        deform_paint_vertices(
            vertices,
            bones,
            reference,
            current,
            cancellation.get_token()) ==
            std::unexpected(
                PaintDeformationError::Cancelled),
        "pre-cancelled deformation published output");

    if (passed)
    {
        std::cout << "PASS paint_deformation\n";
        return 0;
    }
    return 1;
}
