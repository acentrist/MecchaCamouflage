#pragma once
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <string>
#include <vector>

#include "../reflection/reflection.hpp"
#include "../protocol/protocol.hpp"

namespace paint {

using namespace reflection;
using namespace protocol;
using namespace util;

constexpr int PaintChannelAlbedoMetallicRoughness = 5;

struct Color {
    double r{0.0}, g{0.0}, b{0.0}, a{1.0};
    double roughness{0.65}, metallic{0.0};
    int apply_mode{1};
};

struct FrontSample {
    double u{0.5}, v{0.5};
    double r{1.0}, g{1.0}, b{1.0};
    double roughness{0.65}, metallic{0.0}, radius{0.012};
    bool floor_like{false};
    int atlas_priority{0}, atlas_radius{2};
    double atlas_weight{0.0};
    double screen_nx{0.5}, screen_ny{0.5};
    double capture_nx{-1.0}, capture_ny{-1.0};
    bool has_world_position{false};
    meccha_sdk::FVector world_position{};
    meccha_sdk::FVector normal{};
};

struct ChannelBuffer {
    bool ok{false};
    int channel{0}, width{0}, height{0}, bytes_per_pixel{1};
    std::uint64_t hash{1469598103934665603ULL};
    std::string failure{};
    std::vector<std::uint8_t> bytes{};
};

struct PaintCallStats {
    int server_success{0}, server_failure{0};
    int local_success{0}, local_failure{0};
    std::string first_failure{};
};

struct ComponentSelection {
    std::uintptr_t component{0}, pawn{0}, target{0}, target_mesh{0}, owner{0};
    std::string source{}, target_source{}, mesh_source{};
};

inline auto clamp01(double v) -> double { return std::max(0.0, std::min(1.0, v)); }

inline auto infer_channel_dimensions(std::size_t byte_count) -> std::pair<int, int> {
    if (byte_count == 0) return {0, 0};
    const auto pixels_rgba = byte_count % 4 == 0 ? byte_count / 4 : byte_count;
    int width = static_cast<int>(std::sqrt(static_cast<double>(pixels_rgba)));
    width = std::max(1, width);
    while (width > 1 && pixels_rgba % static_cast<std::size_t>(width) != 0) --width;
    return {width, std::max(1, static_cast<int>(pixels_rgba / static_cast<std::size_t>(width)))};
}

inline auto channel_byte_count(const std::string& request, const std::string& channel_label) -> std::size_t {
    const auto label_pos = request.find(channel_label);
    if (label_pos == std::string::npos) return 0;
    const auto size_pos = request.find("\"size\":", label_pos);
    if (size_pos == std::string::npos) return 0;
    auto val = std::strtol(request.c_str() + size_pos + 7, nullptr, 10);
    return static_cast<std::size_t>(std::max(0L, val));
}

inline auto hex_address(std::uintptr_t value) -> std::string {
    char buf[32]{};
    std::snprintf(buf, sizeof(buf), "0x%llx", static_cast<unsigned long long>(value));
    return buf;
}

inline auto sdk_luma(const Color& c) -> double { return c.r * 0.2126 + c.g * 0.7152 + c.b * 0.0722; }

inline auto sdk_is_red_paint_artifact(const Color& c) -> bool { return c.r > 0.78 && c.g < 0.22 && c.b < 0.22; }

inline auto sdk_infer_surface_material(Color c, bool floor_like) -> Color {
    if (floor_like) { c.roughness = std::max(0.86, std::min(0.99, std::max(c.roughness, 0.86))); c.metallic = std::max(0.0, std::min(0.12, c.metallic)); return c; }
    c.roughness = std::max(0.35, std::min(0.99, c.roughness <= 0.0 ? 0.92 : c.roughness));
    c.metallic = clamp01(c.metallic);
    return c;
}

inline auto sdk_sanitize_background_color(const Color& captured, const Color& material_hint) -> Color {
    if (!sdk_is_red_paint_artifact(captured)) return captured;
    Color fb = material_hint;
    if (sdk_is_red_paint_artifact(fb)) fb = {0.34, 0.37, 0.31, 0.94, 0.0};
    return fb;
}

inline auto find_object_property(Reflection& ref, std::uintptr_t object, const char* property_name) -> std::uintptr_t {
    auto cls = ref.class_ptr(object);
    if (!cls) return 0;
    for (int depth = 0; cls && depth < 32; ++depth) {
        for (auto prop = seh_read<std::uintptr_t>(cls + FieldOffsets::OffChildProperties); prop;
             prop = seh_read<std::uintptr_t>(prop + FieldOffsets::OffFFieldNext)) {
            if (ref.names.resolve(seh_read<std::uint32_t>(prop + FieldOffsets::OffFFieldName)) == property_name)
                return prop;
        }
        const auto super = seh_read<std::uintptr_t>(cls + FieldOffsets::OffSuperStruct);
        if (!super || super == cls) break;
        cls = super;
    }
    return 0;
}

inline auto find_property_any(Reflection& ref, std::uintptr_t structure, std::initializer_list<const char*> names) -> std::uintptr_t {
    for (const auto* name : names) {
        if (!name) continue;
        for (auto prop = seh_read<std::uintptr_t>(structure + FieldOffsets::OffChildProperties); prop;
             prop = seh_read<std::uintptr_t>(prop + FieldOffsets::OffFFieldNext)) {
            const auto resolved = ref.names.resolve(seh_read<std::uint32_t>(prop + FieldOffsets::OffFFieldName));
            if (resolved == name || lower_copy(resolved) == lower_copy(name))
                return prop;
        }
    }
    return 0;
}

inline auto struct_type(Reflection& ref, std::uintptr_t prop, std::initializer_list<const char*> expected_fields) -> std::uintptr_t {
    const auto st = seh_read<std::uintptr_t>(prop + FieldOffsets::OffFStructPropertyStruct);
    if (!st) return 0;
    int found = 0;
    for (auto child = seh_read<std::uintptr_t>(st + FieldOffsets::OffChildProperties); child;
         child = seh_read<std::uintptr_t>(child + FieldOffsets::OffFFieldNext)) {
        const auto name = ref.names.resolve(seh_read<std::uint32_t>(child + FieldOffsets::OffFFieldName));
        for (const auto* expected : expected_fields) {
            if (lower_copy(name) == lower_copy(expected)) { ++found; break; }
        }
    }
    return found > 0 ? st : 0;
}

inline auto parse_front_samples(const std::string& request, int limit = 4096) -> std::vector<FrontSample> {
    std::vector<FrontSample> samples;
    auto pos = request.find("\"front_samples\"");
    if (pos == std::string::npos) return samples;
    while (static_cast<int>(samples.size()) < limit) {
        const auto bp = request.find('{', pos + 1);
        if (bp == std::string::npos || bp + 1 >= request.size()) break;
        const auto up = request.find("\"u\":", bp);
        const auto vp = request.find("\"v\":", bp);
        const auto rp = request.find("\"r\":", bp);
        const auto gp = request.find("\"g\":", bp);
        const auto bp_ = request.find("\"b\":", bp);
        if (up == std::string::npos || rp == std::string::npos) break;
        const auto se = request.find('}', bp);
        if (se == std::string::npos) break;
        FrontSample s{};
        char* end = nullptr;
        s.u = clamp01(std::strtod(request.c_str() + up + 4, &end));
        s.v = clamp01(std::strtod(request.c_str() + vp + 4, &end));
        s.r = clamp01(std::strtod(request.c_str() + rp + 4, &end));
        s.g = clamp01(std::strtod(request.c_str() + gp + 4, &end));
        s.b = clamp01(std::strtod(request.c_str() + bp_ + 4, &end));
        const auto roughp = request.find("\"roughness\":", bp);
        if (roughp != std::string::npos && roughp < se) s.roughness = clamp01(std::strtod(request.c_str() + roughp + 12, &end));
        const auto radp = request.find("\"radius\":", bp);
        if (radp != std::string::npos && radp < se) s.radius = std::min(0.08, std::max(0.001, std::strtod(request.c_str() + radp + 9, &end)));
        const auto wxp = request.find("\"world_x\":", bp);
        const auto wyp = request.find("\"world_y\":", bp);
        const auto wzp = request.find("\"world_z\":", bp);
        if (wxp != std::string::npos && wyp != std::string::npos && wzp != std::string::npos && wxp < se && wyp < se && wzp < se) {
            s.world_position.X = std::strtod(request.c_str() + wxp + 10, &end);
            s.world_position.Y = std::strtod(request.c_str() + wyp + 10, &end);
            s.world_position.Z = std::strtod(request.c_str() + wzp + 10, &end);
            s.has_world_position = true;
        }
        samples.push_back(s);
        pos = bp_ + 4;
    }
    return samples;
}

inline auto parse_average_color(const std::string& request) -> Color {
    Color out{0.42, 0.42, 0.36, 1.0, 0.65, 0.0, 1};
    double sr = 0, sg = 0, sb = 0;
    int count = 0;
    auto pos = request.find("\"front_samples\"");
    if (pos == std::string::npos) pos = 0;
    while (count < 256) {
        const auto rp = request.find("\"r\":", pos);
        if (rp == std::string::npos) break;
        const auto gp = request.find("\"g\":", rp);
        const auto bp = request.find("\"b\":", gp == std::string::npos ? rp : gp);
        if (gp == std::string::npos || bp == std::string::npos) break;
        char* end = nullptr;
        double r = std::strtod(request.c_str() + rp + 4, &end);
        double g = std::strtod(request.c_str() + gp + 4, &end);
        double b = std::strtod(request.c_str() + bp + 4, &end);
        if (std::isfinite(r) && std::isfinite(g) && std::isfinite(b)) { sr += clamp01(r); sg += clamp01(g); sb += clamp01(b); ++count; }
        pos = bp + 4;
    }
    if (count > 0) { out.r = sr / count; out.g = sg / count; out.b = sb / count; }
    return out;
}

inline auto fill_channel(std::vector<std::uint8_t>& bytes, std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a) -> void {
    if (bytes.empty()) return;
    if (bytes.size() % 4 == 0) {
        for (std::size_t i = 0; i + 3 < bytes.size(); i += 4) { bytes[i] = r; bytes[i + 1] = g; bytes[i + 2] = b; bytes[i + 3] = a; }
    } else {
        std::fill(bytes.begin(), bytes.end(), r);
    }
}

inline auto paint_disc(std::vector<std::uint8_t>& bytes, int width, int height, const FrontSample& sample, bool albedo) -> void {
    if (bytes.empty() || width <= 0 || height <= 0) return;
    const bool rgba = bytes.size() == static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4;
    const int cx = std::min(width - 1, std::max(0, static_cast<int>(sample.u * static_cast<double>(width))));
    const int cy = std::min(height - 1, std::max(0, static_cast<int>((1.0 - sample.v) * static_cast<double>(height))));
    const int radius = std::max(1, static_cast<int>(sample.radius * static_cast<double>(std::min(width, height))));
    const int r2 = radius * radius;
    const auto rb = static_cast<std::uint8_t>(std::round(clamp01(sample.r) * 255.0));
    const auto gb = static_cast<std::uint8_t>(std::round(clamp01(sample.g) * 255.0));
    const auto bb = static_cast<std::uint8_t>(std::round(clamp01(sample.b) * 255.0));
    const auto scalar = static_cast<std::uint8_t>(std::round(clamp01(sample.roughness) * 255.0));
    for (int y = std::max(0, cy - radius); y <= std::min(height - 1, cy + radius); ++y) {
        for (int x = std::max(0, cx - radius); x <= std::min(width - 1, cx + radius); ++x) {
            if ((x - cx) * (x - cx) + (y - cy) * (y - cy) > r2) continue;
            const auto pixel = static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x);
            if (rgba) { const auto o = pixel * 4; bytes[o] = albedo ? rb : scalar; bytes[o + 1] = albedo ? gb : scalar; bytes[o + 2] = albedo ? bb : scalar; bytes[o + 3] = 255; }
            else if (pixel < bytes.size()) bytes[pixel] = albedo ? rb : scalar;
        }
    }
}

inline auto paint_material_disc(std::vector<std::uint8_t>& bytes, int width, int height,
                                int bytes_per_pixel, const FrontSample& sample, std::uint8_t value) -> void {
    if (bytes.empty() || width <= 0 || height <= 0) return;
    bytes_per_pixel = bytes_per_pixel >= 4 ? 4 : 1;
    const int cx = std::min(width - 1, std::max(0, static_cast<int>(sample.u * static_cast<double>(width))));
    const int cy = std::min(height - 1, std::max(0, static_cast<int>((1.0 - sample.v) * static_cast<double>(height))));
    const int radius = std::max(1, static_cast<int>(sample.radius * static_cast<double>(std::min(width, height))));
    const int r2 = radius * radius;
    for (int y = std::max(0, cy - radius); y <= std::min(height - 1, cy + radius); ++y) {
        for (int x = std::max(0, cx - radius); x <= std::min(width - 1, cx + radius); ++x) {
            if ((x - cx) * (x - cx) + (y - cy) * (y - cy) > r2) continue;
            const auto pixel = static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x);
            const auto o = pixel * static_cast<std::size_t>(bytes_per_pixel);
            if (o >= bytes.size()) continue;
            if (bytes_per_pixel >= 4 && o + 3 < bytes.size()) { bytes[o] = value; bytes[o + 1] = value; bytes[o + 2] = value; bytes[o + 3] = 255; }
            else { bytes[o] = value; }
        }
    }
}

inline auto normalize_material_channel_layout(ChannelBuffer& channel, const ChannelBuffer& albedo) -> void {
    if (!channel.ok || !albedo.ok || albedo.width <= 0 || albedo.height <= 0) return;
    const auto pixels = static_cast<std::size_t>(albedo.width) * static_cast<std::size_t>(albedo.height);
    if (pixels == 0) return;
    if (channel.bytes.size() == pixels) { channel.width = albedo.width; channel.height = albedo.height; channel.bytes_per_pixel = 1; }
    else if (channel.bytes.size() == pixels * 4) { channel.width = albedo.width; channel.height = albedo.height; channel.bytes_per_pixel = 4; }
}

inline auto fill_material_channel(std::vector<std::uint8_t>& bytes, int bpp, std::uint8_t value) -> void {
    if (bytes.empty()) return;
    if (bpp >= 4) {
        for (std::size_t i = 0; i + 3 < bytes.size(); i += 4) { bytes[i] = value; bytes[i + 1] = value; bytes[i + 2] = value; bytes[i + 3] = 255; }
    } else {
        std::fill(bytes.begin(), bytes.end(), value);
    }
}

} // namespace paint
