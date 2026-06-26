#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>
#include <cctype>

#include "../util/seh_guard.hpp"

namespace reflection {

using namespace util;

struct ModuleRange {
    std::uintptr_t base{0};
    std::size_t size{0};
};

inline auto main_module_range() -> ModuleRange {
    auto* base = reinterpret_cast<std::uint8_t*>(GetModuleHandleW(nullptr));
    if (!base) return {};
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return {};
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return {};
    return {reinterpret_cast<std::uintptr_t>(base), nt->OptionalHeader.SizeOfImage};
}

inline auto address_in_main_module(std::uintptr_t address) -> bool {
    const auto module = main_module_range();
    return module.base && address >= module.base && address < module.base + module.size;
}

inline auto live_uobject(std::uintptr_t object) -> bool {
    static constexpr std::uintptr_t OffObjectFlags = 0x08;
    static constexpr std::uint32_t RFClassDefaultObject = 0x10;
    if (!object || address_in_main_module(object)) return false;
    const auto flags = seh_read<std::uint32_t>(object + OffObjectFlags, 0);
    return (flags & RFClassDefaultObject) == 0;
}

inline auto match_pattern(const std::uint8_t* data, const std::uint8_t* pattern, const std::uint8_t* mask, std::size_t length) -> bool {
    for (std::size_t i = 0; i < length; ++i) {
        if (mask[i] && data[i] != pattern[i]) return false;
    }
    return true;
}

inline auto scan_pattern(const std::vector<std::uint8_t>& pattern, const std::vector<std::uint8_t>& mask) -> std::uintptr_t {
    const auto module = main_module_range();
    if (!module.base || !module.size || pattern.empty() || pattern.size() != mask.size()) return 0;
    const auto* base = reinterpret_cast<const std::uint8_t*>(module.base);
    const std::size_t length = pattern.size();
    for (std::size_t offset = 0; offset + length < module.size; ++offset) {
        const auto result = seh([&]() -> std::uintptr_t {
            if (match_pattern(base + offset, pattern.data(), mask.data(), length))
                return module.base + offset;
            return 0;
        });
        if (result) return result;
    }
    return 0;
}

struct FieldOffsets {
    static constexpr std::uintptr_t OffClass = 0x10;
    static constexpr std::uintptr_t OffName = 0x18;
    static constexpr std::uintptr_t OffOuter = 0x20;
    static constexpr std::uintptr_t OffObjectFlags = 0x08;
    static constexpr std::uint32_t RFClassDefaultObject = 0x10;
    static constexpr std::uintptr_t OffSuperStruct = 0x40;
    static constexpr std::uintptr_t OffChildren = 0x48;
    static constexpr std::uintptr_t OffChildProperties = 0x50;
    static constexpr std::uintptr_t OffPropertiesSize = 0x58;
    static constexpr std::uintptr_t OffUFieldNext = 0x28;
    static constexpr std::uintptr_t OffFFieldNext = 0x18;
    static constexpr std::uintptr_t OffFFieldName = 0x20;
    static constexpr std::uintptr_t OffFPropertyElementSize = 0x3C;
    static constexpr std::uintptr_t OffFPropertyOffset = 0x44;
    static constexpr std::uintptr_t OffFStructPropertyStruct = 0x78;
};

struct FNameResolver {
    std::uintptr_t pool{0};
    int table_offset{0x10};
    int style{1};
    static constexpr int offsets[14]{0x8, 0x10, 0x18, 0x20, 0x28, 0x30, 0x38, 0x40, 0x48, 0x50, 0x58, 0x60, 0x68, 0x70};

    auto entry(std::uint32_t id, int table, int entry_style) const -> std::string {
        const auto block_index = id >> 16;
        const auto within = (id & 0xFFFF) << 1;
        const auto block = seh_read<std::uintptr_t>(pool + table + static_cast<std::uintptr_t>(block_index) * 8);
        if (!block) return {};
        const auto header = seh_read<std::uint16_t>(block + within);
        bool wide = false;
        int length = 0;
        if (entry_style == 0) {
            wide = (header & 1) != 0;
            length = header >> 1;
        } else if (entry_style == 2) {
            wide = (header & 1) != 0;
            length = (header >> 6) & 0x3FF;
        } else {
            length = header & 0x3FF;
            wide = ((header >> 10) & 1) != 0;
        }
        if (length <= 0 || length > 512) return {};
        if (wide) {
            std::wstring text(length, L'\0');
            if (!seh_copy(text.data(), reinterpret_cast<void*>(block + within + 2), static_cast<std::size_t>(length) * sizeof(wchar_t)))
                return {};
            std::string out;
            out.reserve(text.size());
            for (wchar_t c : text) out.push_back(c >= 0 && c < 128 ? static_cast<char>(c) : '?');
            return out;
        }
        std::string text(length, '\0');
        if (!seh_copy(text.data(), reinterpret_cast<void*>(block + within + 2), static_cast<std::size_t>(length)))
            return {};
        return text;
    }

    auto detect() -> void {
        for (const int off : offsets) {
            for (const int st : {2, 1, 0}) {
                if (entry(0, off, st) == "None") {
                    table_offset = off;
                    style = st;
                    return;
                }
            }
        }
    }

    auto resolve(std::uint32_t id) -> std::string {
        auto name = entry(id, table_offset, style);
        if (!name.empty()) return name;
        for (const int off : offsets) {
            for (const int st : {2, 1, 0}) {
                name = entry(id, off, st);
                if (!name.empty()) {
                    table_offset = off;
                    style = st;
                    return name;
                }
            }
        }
        return {};
    }
};

struct Reflection {
    std::uintptr_t guobject_array{0};
    std::uintptr_t fname_pool{0};
    FNameResolver names{};
    std::uintptr_t meta_class{0};

    auto init(std::string& failure) -> bool {
        static const std::vector<std::uint8_t> gu_sig{0x48, 0x8D, 0x05, 0, 0, 0, 0, 0x48, 0x89, 0x01, 0x45, 0x8B, 0xD1};
        static const std::vector<std::uint8_t> gu_mask{1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1};
        const auto gu_ref = scan_pattern(gu_sig, gu_mask);
        if (!gu_ref) { failure = "guobject_pattern_not_found"; return false; }
        const auto rel = seh_read<std::int32_t>(gu_ref + 3);
        guobject_array = gu_ref + 7 + rel;
        const auto delta_candidate = guobject_array - 0xE3B40;
        names.pool = delta_candidate;
        names.detect();
        if (names.resolve(0) == "None") {
            fname_pool = delta_candidate;
            return true;
        }
        const std::vector<std::pair<std::vector<std::uint8_t>, std::vector<std::uint8_t>>> patterns{
            {{0x48, 0x8D, 0x0D, 0, 0, 0, 0, 0xE8, 0, 0, 0, 0, 0x4C, 0x8B, 0xC0}, {1, 1, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 1, 1}},
            {{0x48, 0x8D, 0x0D, 0, 0, 0, 0, 0xE8, 0, 0, 0, 0, 0x48, 0x8B}, {1, 1, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 1}},
            {{0x48, 0x8D, 0x35, 0, 0, 0, 0}, {1, 1, 1, 0, 0, 0, 0}},
            {{0x48, 0x8D, 0x3D, 0, 0, 0, 0}, {1, 1, 1, 0, 0, 0, 0}},
        };
        for (const auto& [sig, mask] : patterns) {
            const auto ref = scan_pattern(sig, mask);
            if (!ref) continue;
            const auto fname_rel = seh_read<std::int32_t>(ref + 3);
            names.pool = ref + 7 + fname_rel;
            names.detect();
            if (names.resolve(0) == "None") { fname_pool = names.pool; return true; }
        }
        failure = "fname_pool_not_found";
        return false;
    }

    auto object_name(std::uintptr_t object) -> std::string {
        if (!object) return {};
        auto out = names.resolve(seh_read<std::uint32_t>(object + FieldOffsets::OffName));
        const auto slash = out.find_last_of("/.");
        if (slash != std::string::npos) out = out.substr(slash + 1);
        if (out.rfind("Default__", 0) == 0) out = out.substr(9);
        return out;
    }

    auto class_ptr(std::uintptr_t object) -> std::uintptr_t {
        return object ? seh_read<std::uintptr_t>(object + FieldOffsets::OffClass) : 0;
    }

    auto class_name(std::uintptr_t object) -> std::string {
        return object_name(class_ptr(object));
    }

    template <typename Fn>
    auto for_each_object(Fn fn) -> void {
        const auto chunks = seh_read<std::uintptr_t>(guobject_array + 0x10);
        if (!chunks) return;
        for (int ci = 0; ci < 64; ++ci) {
            const auto chunk = seh_read<std::uintptr_t>(chunks + static_cast<std::uintptr_t>(ci) * 8);
            if (!chunk) break;
            for (int wi = 0; wi < 65536; ++wi) {
                const auto obj = seh_read<std::uintptr_t>(chunk + static_cast<std::uintptr_t>(wi) * 0x18);
                if (obj && fn(obj)) return;
            }
        }
    }

    auto find_meta_class() -> std::uintptr_t {
        if (meta_class) return meta_class;
        for_each_object([&](std::uintptr_t obj) {
            if (object_name(obj) == "Class") { meta_class = obj; return true; }
            return false;
        });
        return meta_class;
    }

    auto find_class(const char* name) -> std::uintptr_t {
        const auto meta = find_meta_class();
        if (!meta) return 0;
        std::uintptr_t found = 0;
        for_each_object([&](std::uintptr_t obj) {
            if (class_ptr(obj) == meta && object_name(obj) == name) { found = obj; return true; }
            return false;
        });
        return found;
    }

    auto find_first_instance(const char* class_name_text) -> std::uintptr_t {
        const auto cls = find_class(class_name_text);
        if (!cls) return 0;
        std::uintptr_t found = 0;
        for_each_object([&](std::uintptr_t obj) {
            if (class_ptr(obj) == cls && object_name(obj).rfind("Default__", 0) != 0) { found = obj; return true; }
            return false;
        });
        return found;
    }

    auto find_property(std::uintptr_t structure, const char* property_name) -> std::uintptr_t {
        for (auto prop = seh_read<std::uintptr_t>(structure + FieldOffsets::OffChildProperties); prop;
             prop = seh_read<std::uintptr_t>(prop + FieldOffsets::OffFFieldNext)) {
            if (names.resolve(seh_read<std::uint32_t>(prop + FieldOffsets::OffFFieldName)) == property_name)
                return prop;
        }
        return 0;
    }

    auto resolve_property_offset(const char* class_name_text, const char* property_name) -> int {
        auto cls = find_class(class_name_text);
        for (int depth = 0; cls && depth < 32; ++depth) {
            const auto prop = find_property(cls, property_name);
            if (prop) return seh_read<int>(prop + FieldOffsets::OffFPropertyOffset, -1);
            cls = seh_read<std::uintptr_t>(cls + FieldOffsets::OffSuperStruct);
        }
        return -1;
    }

    auto find_function(std::uintptr_t object, const char* function_name) -> std::uintptr_t {
        auto cls = class_ptr(object);
        for (int depth = 0; cls && depth < 64; ++depth) {
            for (auto child = seh_read<std::uintptr_t>(cls + FieldOffsets::OffChildren); child;
                 child = seh_read<std::uintptr_t>(child + FieldOffsets::OffUFieldNext)) {
                if (object_name(child) == function_name) return child;
            }
            cls = seh_read<std::uintptr_t>(cls + FieldOffsets::OffSuperStruct);
        }
        return 0;
    }
};

inline auto lower_copy(std::string text) -> std::string {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

inline auto contains_text(const std::string& text, const char* needle) -> bool {
    return text.find(needle) != std::string::npos;
}

inline auto prop_offset(std::uintptr_t prop) -> int {
    return seh_read<int>(prop + FieldOffsets::OffFPropertyOffset, -1);
}

inline auto prop_element_size(std::uintptr_t prop) -> int {
    return seh_read<int>(prop + FieldOffsets::OffFPropertyElementSize, 0);
}

inline auto hex_address(std::uintptr_t value) -> std::string {
    char buffer[32]{};
    std::snprintf(buffer, sizeof(buffer), "0x%llx", static_cast<unsigned long long>(value));
    return buffer;
}

inline auto fnv1a64(const void* data, std::size_t size) -> std::uint64_t {
    const auto* bytes = static_cast<const unsigned char*>(data);
    std::uint64_t hash = 14695981039346656037ull;
    for (std::size_t i = 0; i < size; ++i) { hash ^= bytes[i]; hash *= 1099511628211ull; }
    return hash;
}

} // namespace reflection
