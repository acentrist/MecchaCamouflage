#include "MecchaCamouflage/core/paint_core.hpp"

#include <cassert>
#include <cstdint>
#include <vector>

namespace Core = MecchaCamouflage::Core;

auto make_channel(int width, int height, std::uint8_t value) -> Core::ChannelBuffer
{
    return Core::ChannelBuffer{width, height, std::vector<std::uint8_t>(static_cast<std::size_t>(width * height * 4), value)};
}

int main()
{
    {
        const auto decision = Core::validate_capture_quality(Core::CaptureQualityInput{
            true,
            false,
            3000,
            3000,
            2048,
            false,
            false,
            0.34,
            0.02,
            0.03});
        assert(!decision.ok);
        assert(decision.failure == "bulk_calibration_failed_no_paint");
    }

    {
        const auto decision = Core::validate_capture_quality(Core::CaptureQualityInput{
            true,
            true,
            3000,
            3000,
            2048,
            false,
            false,
            0.05,
            0.40,
            0.42});
        assert(decision.ok);
        assert(decision.failure == "ok");
    }

    {
        const auto decision = Core::validate_capture_quality(Core::CaptureQualityInput{
            true,
            true,
            3000,
            3000,
            2048,
            false,
            false,
            0.05,
            0.57,
            0.77});
        assert(decision.ok);
        assert(decision.failure == "ok");
    }

    {
        const auto before = make_channel(8, 8, 17);
        const auto decision = Core::validate_capture_quality(Core::CaptureQualityInput{
            true,
            false,
            3000,
            3000,
            2048,
            false,
            false,
            0.40,
            0.02,
            0.03});
        auto albedo_after = before;
        if (!decision.ok)
        {
            albedo_after = before;
        }
        assert(Core::hash_bytes(before.bytes) == Core::hash_bytes(albedo_after.bytes));
    }

    {
        const auto albedo = make_channel(16, 16, 0);
        const auto metallic = make_channel(16, 16, 0);
        const auto roughness = make_channel(16, 16, 0);
        const std::vector<Core::PaintSeed> seeds{
            Core::PaintSeed{0.5, 0.5, Core::Color{0.2, 0.3, 0.4, 0.8, 0.1}, false}};
        const auto result = Core::assemble_direct_texture(albedo, metallic, roughness, seeds);
        assert(result.stats.uv_coverage > 0);
        assert(result.stats.filled_by_extension > 0);
        assert(result.stats.direct_texels > 0);
        assert(result.stats.filled_by_direct == result.stats.direct_texels);
        bool has_white = false;
        for (std::size_t i = 0; i + 2 < result.albedo.bytes.size(); i += 4)
        {
            if (result.albedo.bytes[i] == 255 && result.albedo.bytes[i + 1] == 255 && result.albedo.bytes[i + 2] == 255)
            {
                has_white = true;
                break;
            }
        }
        assert(!has_white);
    }

    return 0;
}
