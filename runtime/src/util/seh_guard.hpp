#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <excpt.h>
#include <type_traits>
#include <utility>

namespace util {

template <typename Fn>
auto seh(Fn&& fn) -> std::invoke_result_t<Fn> {
    using result_t = std::invoke_result_t<Fn>;
    __try {
        if constexpr (std::is_void_v<result_t>) {
            std::forward<Fn>(fn)();
        } else {
            return std::forward<Fn>(fn)();
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        if constexpr (std::is_void_v<result_t>) {
            return;
        } else if constexpr (std::is_pointer_v<result_t>) {
            return nullptr;
        } else if constexpr (std::is_integral_v<result_t>) {
            return static_cast<result_t>(0);
        } else if constexpr (std::is_floating_point_v<result_t>) {
            return static_cast<result_t>(0.0);
        } else {
            return result_t{};
        }
    }
}

template <typename T>
auto seh_read(std::uintptr_t address, T fallback = T{}) -> T {
    return seh([&]() -> T {
        return *reinterpret_cast<const T*>(address);
    });
}

inline auto seh_copy(void* dest, const void* src, std::size_t size) -> bool {
    return seh([&]() -> bool {
        std::memcpy(dest, src, size);
        return true;
    });
}

} // namespace util
