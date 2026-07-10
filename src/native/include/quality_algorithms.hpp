#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace mc::quality
{
    inline auto detection_density(int detail) -> double
    {
        if (detail <= 1) return 0.85;
        if (detail == 2) return 1.00;
        if (detail == 3) return 1.15;
        if (detail == 4) return 1.35;
        return 1.60;
    }

    // FAST-preset footprint radius (in capture texels) for a stroke of the
    // given size in UV texels. A FAST stroke is coarse and covers several source
    // texels, so its color must be the integral of that footprint, not a single
    // point sample. The radius grows with the stroke and is clamped to [1,3] so
    // the per-sample cost stays bounded and monotonic. A non-positive stroke
    // degenerates to the minimum radius.
    inline auto fast_footprint_radius(double stroke_size_texels) -> int
    {
        if (!(stroke_size_texels > 0.0)) return 1;
        const long r = std::lround(stroke_size_texels / 4.0);
        return static_cast<int>(std::clamp<long>(r, 1L, 3L));
    }

    // Gamma-correct area (box) integration for the FAST preset. `sample(dx,dy)`
    // returns a LINEAR-light channel value in [0,1] for offsets in
    // [-radius,radius]; the caller decodes sRGB -> linear before averaging and
    // re-encodes the mean back to sRGB. Integrating the footprint yields the
    // representative color for a stroke that covers many texels and removes the
    // aliasing / moire that point sampling produces on high-frequency
    // corak/pattern edges. radius < 1 degenerates to the single centre sample,
    // preserving exact legacy nearest behavior.
    template <typename LinearSample>
    inline auto area_mean_linear(LinearSample&& sample, int radius) -> double
    {
        if (radius < 1) return sample(0, 0);
        double acc = 0.0;
        int count = 0;
        for (int dy = -radius; dy <= radius; ++dy)
        {
            for (int dx = -radius; dx <= radius; ++dx)
            {
                acc += sample(dx, dy);
                ++count;
            }
        }
        return count > 0 ? acc / static_cast<double>(count) : sample(0, 0);
    }

    inline auto clamp_bicubic_reconstruction(double reconstructed,
                                              double c00,
                                              double c10,
                                              double c01,
                                              double c11) -> double
    {
        const double local_min = std::min({c00, c10, c01, c11});
        const double local_max = std::max({c00, c10, c01, c11});
        return std::clamp(reconstructed, local_min, local_max);
    }

    inline auto adaptive_rate_ewma(double current, double sample, double delta_ms) -> double
    {
        if (!std::isfinite(sample) || sample <= 0.0 ||
            !std::isfinite(delta_ms) || delta_ms <= 0.0)
        {
            return current;
        }
        if (!std::isfinite(current) || current <= 0.0)
        {
            return sample;
        }
        // Limit one observation to a 4x swing, then choose smoothing from the
        // actual sample interval. This reacts quickly after a stall without
        // letting one noisy pressure snapshot destabilize ETA and batch pacing.
        const double bounded_sample = std::clamp(sample, current * 0.25, current * 4.0);
        const double alpha = std::clamp(1.0 - std::exp(-delta_ms / 750.0), 0.15, 0.65);
        return current + (bounded_sample - current) * alpha;
    }

    struct ScharrGradient
    {
        double gx{0.0};
        double gy{0.0};
        double magnitude{0.0};
        std::uint8_t direction{0};
    };

    template <typename Sample>
    inline auto scharr_3x3(Sample&& sample) -> ScharrGradient
    {
        ScharrGradient out{};
        out.gx = -3.0 * sample(-1, -1) + 3.0 * sample(1, -1) -
                 10.0 * sample(-1, 0) + 10.0 * sample(1, 0) -
                 3.0 * sample(-1, 1) + 3.0 * sample(1, 1);
        out.gy = -3.0 * sample(-1, -1) - 10.0 * sample(0, -1) - 3.0 * sample(1, -1) +
                  3.0 * sample(-1, 1) + 10.0 * sample(0, 1) + 3.0 * sample(1, 1);
        out.magnitude = std::min(255.0, std::sqrt(out.gx * out.gx + out.gy * out.gy) / 16.0);
        const double ax = std::abs(out.gx);
        const double ay = std::abs(out.gy);
        if (ax >= ay * 2.0) out.direction = 0;
        else if (ay >= ax * 2.0) out.direction = 1;
        else out.direction = (out.gx * out.gy >= 0.0) ? 2 : 3;
        return out;
    }
}
