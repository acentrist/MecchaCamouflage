#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace MecchaCamouflage::Core
{
    struct Color
    {
        double r{0.0};
        double g{0.0};
        double b{0.0};
        double roughness{0.0};
        double metallic{0.0};
    };

    struct PaintSeed
    {
        double u{0.0};
        double v{0.0};
        Color color{};
        bool floor_like{false};
    };

    struct ChannelBuffer
    {
        int width{0};
        int height{0};
        std::vector<std::uint8_t> bytes{};
    };

    struct TextureWriteStats
    {
        int uv_coverage{0};
        int filled_by_direct{0};
        int filled_by_extension{0};
        int filled_by_floor{0};
        int preserved_original{0};
        int direct_texels{0};
        unsigned worker_threads{1};
    };

    struct TextureAssemblyResult
    {
        ChannelBuffer albedo{};
        ChannelBuffer metallic{};
        ChannelBuffer roughness{};
        TextureWriteStats stats{};
    };

    struct CaptureQualityInput
    {
        bool image_ok{false};
        bool bulk_calibration_ok{false};
        int background_pixels{0};
        int trace_hits{0};
        int min_hits{0};
        bool uniform{false};
        bool clear_suspect{false};
        double bulk_best_median{0.0};
        double capture_trace_chroma_avg{0.0};
        double capture_trace_chroma_p95{0.0};
    };

    struct CaptureQualityDecision
    {
        bool ok{false};
        std::string failure{"not_evaluated"};
    };

    constexpr double MaxBulkMedianRgbError = 0.18;
    constexpr double MaxCaptureTraceChromaAvg = 0.50;
    constexpr double MaxCaptureTraceChromaP95 = 0.72;

    auto clamp_unit(double value) -> double;
    auto byte_from_unit(double value) -> std::uint8_t;
    auto chroma_distance_rgb(const Color& a, const Color& b) -> double;
    auto hash_bytes(const std::vector<std::uint8_t>& bytes) -> std::uint64_t;
    auto changed_byte_count(const std::vector<std::uint8_t>& before,
                            const std::vector<std::uint8_t>& after) -> int;

    auto validate_capture_quality(const CaptureQualityInput& input) -> CaptureQualityDecision;

    auto assemble_direct_texture(const ChannelBuffer& albedo_before,
                                 const ChannelBuffer& metallic_before,
                                 const ChannelBuffer& roughness_before,
                                 const std::vector<PaintSeed>& seeds) -> TextureAssemblyResult;
}
