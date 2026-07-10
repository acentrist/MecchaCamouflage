#include "../include/quality_algorithms.hpp"
#include "../include/local_image_algorithms.hpp"

#include <array>
#include <chrono>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace
{
    void require(bool condition, const char* message)
    {
        if (!condition) throw std::runtime_error(message);
    }
}

int main()
{
    std::vector<std::uint8_t> bmp(58, 0);
    bmp[0] = 'B'; bmp[1] = 'M'; bmp[10] = 54; bmp[14] = 40;
    bmp[18] = 1; bmp[22] = 1; bmp[26] = 1; bmp[28] = 24;
    bmp[54] = 30; bmp[55] = 20; bmp[56] = 10;
    mc::image::RgbImage decoded{}; std::string decode_failure{};
    require(mc::image::decode_bmp(bmp, decoded, decode_failure), "valid 24-bit BMP did not decode");
    double local_r = 0.0, local_g = 0.0, local_b = 0.0;
    mc::image::sample_bilinear(decoded, 0.5, 0.5, local_r, local_g, local_b);
    require(std::abs(local_r - 10.0 / 255.0) < 1e-9, "local image red channel mismatch");
    require(std::abs(local_g - 20.0 / 255.0) < 1e-9, "local image green channel mismatch");
    require(std::abs(local_b - 30.0 / 255.0) < 1e-9, "local image blue channel mismatch");
    std::cout << "PASS local BMP decode and UV sampling\n";
    const std::array<double, 5> expected{0.85, 1.00, 1.15, 1.35, 1.60};
    for (int detail = 1; detail <= 5; ++detail)
    {
        const double density = mc::quality::detection_density(detail);
        require(std::abs(density - expected[static_cast<std::size_t>(detail - 1)]) < 1e-12,
                "detection density mapping mismatch");
        if (detail > 1)
            require(density >= mc::quality::detection_density(detail - 1),
                    "detection density is not monotonic");
    }
    std::cout << "PASS detection density is monotonic\n";

    require(std::abs(mc::quality::clamp_bicubic_reconstruction(1.4, 0.1, 0.8, 0.3, 0.7) - 0.8) < 1e-12,
            "positive bicubic overshoot was not clamped");
    require(std::abs(mc::quality::clamp_bicubic_reconstruction(-0.2, 0.1, 0.8, 0.3, 0.7) - 0.1) < 1e-12,
            "negative bicubic overshoot was not clamped");
    require(std::abs(mc::quality::clamp_bicubic_reconstruction(0.5, 0.1, 0.8, 0.3, 0.7) - 0.5) < 1e-12,
            "valid bicubic reconstruction changed");
    std::cout << "PASS bicubic reconstruction suppresses edge halos\n";

    require(std::abs(mc::quality::adaptive_rate_ewma(-1.0, 2.0, 250.0) - 2.0) < 1e-12,
            "rate estimator did not initialize from its first sample");
    const double smoothed_spike = mc::quality::adaptive_rate_ewma(1.0, 100.0, 250.0);
    require(smoothed_spike > 1.0 && smoothed_spike < 4.0,
            "rate estimator did not bound a transient spike");
    const double responsive = mc::quality::adaptive_rate_ewma(1.0, 2.0, 1500.0);
    const double conservative = mc::quality::adaptive_rate_ewma(1.0, 2.0, 250.0);
    require(responsive > conservative,
            "rate estimator should react more strongly to a longer observation window");
    std::cout << "PASS adaptive ETA rate estimator is bounded and interval-aware\n";

    const auto flat = mc::quality::scharr_3x3([](int, int) { return 80.0; });
    require(flat.magnitude == 0.0, "flat field produced a false edge");

    const auto vertical_edge = mc::quality::scharr_3x3([](int x, int) { return x < 0 ? 0.0 : 255.0; });
    require(vertical_edge.magnitude > 200.0, "strong vertical edge was not detected");
    require(vertical_edge.direction == 0, "vertical edge gradient direction is wrong");

    const auto horizontal_edge = mc::quality::scharr_3x3([](int, int y) { return y < 0 ? 0.0 : 255.0; });
    require(horizontal_edge.magnitude > 200.0, "strong horizontal edge was not detected");
    require(horizontal_edge.direction == 1, "horizontal edge gradient direction is wrong");
    std::cout << "PASS Scharr detector rejects flats and resolves edge direction\n";

    constexpr int size = 1024;
    std::vector<double> image(static_cast<std::size_t>(size) * size, 0.0);
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x)
            image[static_cast<std::size_t>(y) * size + x] = ((x / 16 + y / 16) & 1) ? 235.0 : 20.0;

    const auto started = std::chrono::steady_clock::now();
    double checksum = 0.0;
    for (int y = 1; y + 1 < size; ++y)
    {
        for (int x = 1; x + 1 < size; ++x)
        {
            const auto gradient = mc::quality::scharr_3x3([&](int dx, int dy) {
                return image[static_cast<std::size_t>(y + dy) * size + static_cast<std::size_t>(x + dx)];
            });
            checksum += gradient.magnitude;
        }
    }
    const double elapsed_ms = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - started).count();
    require(checksum > 0.0, "detector benchmark did no work");
    std::cout << "PASS Scharr 1024x1024 benchmark " << elapsed_ms << " ms\n";

    // --- FAST preset footprint reconstruction --------------------------------
    // Radius must grow with stroke size, clamp to [1,3], stay monotonic, and
    // never collapse to 0 (which would restore aliasing point sampling).
    require(mc::quality::fast_footprint_radius(0.0) == 1, "non-positive stroke should use minimum radius");
    require(mc::quality::fast_footprint_radius(-5.0) == 1, "negative stroke should use minimum radius");
    require(mc::quality::fast_footprint_radius(2.0) == 1, "small stroke should use radius 1");
    require(mc::quality::fast_footprint_radius(8.0) == 2, "fast preset stroke (8 texels) should use radius 2");
    require(mc::quality::fast_footprint_radius(12.0) == 3, "large stroke should use radius 3");
    require(mc::quality::fast_footprint_radius(100.0) == 3, "huge stroke radius must clamp to 3");
    for (double stroke = 1.0; stroke <= 40.0; stroke += 1.0)
    {
        const int lo = mc::quality::fast_footprint_radius(stroke);
        const int hi = mc::quality::fast_footprint_radius(stroke + 1.0);
        require(hi >= lo, "footprint radius must be monotonic in stroke size");
        require(lo >= 1 && lo <= 3, "footprint radius must stay in [1,3]");
    }
    std::cout << "PASS FAST footprint radius is bounded and monotonic\n";

    // A flat neighborhood must reproduce the input exactly (no bias/no blur).
    require(std::abs(mc::quality::area_mean_linear([](int, int) { return 0.42; }, 2) - 0.42) < 1e-12,
            "flat footprint must reproduce the source color exactly");
    // radius < 1 must degenerate to the exact centre sample (legacy nearest).
    require(std::abs(mc::quality::area_mean_linear([](int dx, int dy) { return (dx == 0 && dy == 0) ? 1.0 : 0.0; }, 0) - 1.0) < 1e-12,
            "radius 0 must return the exact centre sample");
    // A high-frequency checkerboard footprint (radius 1 -> 3x3, five 1.0 / four
    // 0.0 taps) must return the true area mean = 5/9, not an aliased 0 or 1.
    {
        const double mean = mc::quality::area_mean_linear(
            [](int dx, int dy) { return (((dx + dy) & 1) == 0) ? 1.0 : 0.0; }, 1);
        require(std::abs(mean - 5.0 / 9.0) < 1e-12, "checkerboard footprint must integrate to its true mean");
    }
    // The area mean must remain inside the min/max of its taps (energy-preserving,
    // never overshoots like an unclamped higher-order kernel could).
    {
        int k = 0;
        const double mean = mc::quality::area_mean_linear(
            [&](int, int) { const double v = (k % 3) * 0.25; ++k; return v; }, 1);
        require(mean >= 0.0 - 1e-12 && mean <= 0.5 + 1e-12, "area mean must stay within the tap range");
    }
    std::cout << "PASS FAST area integration removes aliasing without overshoot\n";
    return 0;
}
