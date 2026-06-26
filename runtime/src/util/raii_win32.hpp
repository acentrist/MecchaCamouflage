#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>
#include <cstdio>
#include <stdexcept>
#include <string>

namespace raii {

struct handle {
    HANDLE h{INVALID_HANDLE_VALUE};
    handle() = default;
    explicit handle(HANDLE h_) : h(h_) {}
    ~handle() { if (h != INVALID_HANDLE_VALUE && h != nullptr) CloseHandle(h); }
    handle(handle&& o) noexcept : h(o.h) { o.h = INVALID_HANDLE_VALUE; }
    handle& operator=(handle&& o) noexcept { if (this != &o) { reset(); std::swap(h, o.h); } return *this; }
    handle(const handle&) = delete;
    handle& operator=(const handle&) = delete;
    void reset(HANDLE h_ = INVALID_HANDLE_VALUE) { if (h != INVALID_HANDLE_VALUE && h != nullptr) CloseHandle(h); h = h_; }
    HANDLE get() const { return h; }
    explicit operator bool() const { return h != INVALID_HANDLE_VALUE && h != nullptr; }
};

struct socket_ {
    SOCKET s{INVALID_SOCKET};
    socket_() = default;
    explicit socket_(SOCKET s_) : s(s_) {}
    ~socket_() { if (s != INVALID_SOCKET) closesocket(s); }
    socket_(socket_&& o) noexcept : s(o.s) { o.s = INVALID_SOCKET; }
    socket_& operator=(socket_&& o) noexcept { if (this != &o) { reset(); std::swap(s, o.s); } return *this; }
    socket_(const socket_&) = delete;
    socket_& operator=(const socket_&) = delete;
    void reset(SOCKET s_ = INVALID_SOCKET) { if (s != INVALID_SOCKET) closesocket(s); s = s_; }
    SOCKET get() const { return s; }
    explicit operator bool() const { return s != INVALID_SOCKET; }
};

struct virtual_alloc {
    void* ptr{nullptr};
    SIZE_T size{0};
    HANDLE process{nullptr};
    virtual_alloc() = default;
    virtual_alloc(HANDLE proc, void* addr, SIZE_T sz, DWORD alloc, DWORD prot)
        : ptr(VirtualAllocEx(proc, addr, sz, alloc, prot)), size(sz), process(proc) {}
    ~virtual_alloc() { if (ptr) VirtualFreeEx(process, ptr, 0, MEM_RELEASE); }
    virtual_alloc(virtual_alloc&& o) noexcept : ptr(o.ptr), size(o.size), process(o.process) { o.ptr = nullptr; o.size = 0; o.process = nullptr; }
    virtual_alloc& operator=(virtual_alloc&& o) noexcept { if (this != &o) { reset(); std::swap(ptr, o.ptr); std::swap(size, o.size); std::swap(process, o.process); } return *this; }
    virtual_alloc(const virtual_alloc&) = delete;
    virtual_alloc& operator=(const virtual_alloc&) = delete;
    void reset() { if (ptr) VirtualFreeEx(process, ptr, 0, MEM_RELEASE); ptr = nullptr; size = 0; process = nullptr; }
    void* get() const { return ptr; }
    explicit operator bool() const { return ptr != nullptr; }
};

struct module_file {
    HMODULE mod{nullptr};
    module_file() = default;
    explicit module_file(HMODULE m) : mod(m) {}
    ~module_file() { if (mod) FreeLibrary(mod); }
    module_file(module_file&& o) noexcept : mod(o.mod) { o.mod = nullptr; }
    module_file& operator=(module_file&& o) noexcept { if (this != &o) { reset(); std::swap(mod, o.mod); } return *this; }
    module_file(const module_file&) = delete;
    module_file& operator=(const module_file&) = delete;
    void reset(HMODULE m = nullptr) { if (mod) FreeLibrary(mod); mod = m; }
    HMODULE get() const { return mod; }
    explicit operator bool() const { return mod != nullptr; }
};

struct find_handle {
    HANDLE h{INVALID_HANDLE_VALUE};
    find_handle() = default;
    explicit find_handle(HANDLE h_) : h(h_) {}
    ~find_handle() { if (h != INVALID_HANDLE_VALUE && h != INVALID_HANDLE_VALUE) FindClose(h); }
    find_handle(find_handle&& o) noexcept : h(o.h) { o.h = INVALID_HANDLE_VALUE; }
    find_handle& operator=(find_handle&& o) noexcept { if (this != &o) { reset(); std::swap(h, o.h); } return *this; }
    find_handle(const find_handle&) = delete;
    find_handle& operator=(const find_handle&) = delete;
    void reset(HANDLE h_ = INVALID_HANDLE_VALUE) { if (h != INVALID_HANDLE_VALUE && h != INVALID_HANDLE_VALUE) FindClose(h); h = h_; }
    HANDLE get() const { return h; }
    explicit operator bool() const { return h != INVALID_HANDLE_VALUE && h != INVALID_HANDLE_VALUE; }
};

inline auto win32_error_string(DWORD code) -> std::string {
    LPSTR msg = nullptr;
    const DWORD len = FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                                      nullptr, code, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), reinterpret_cast<LPSTR>(&msg), 0, nullptr);
    std::string out(msg, len > 0 ? len : 0);
    if (msg) LocalFree(msg);
    while (!out.empty() && (out.back() == '\r' || out.back() == '\n')) out.pop_back();
    return out;
}

inline auto last_error_string() -> std::string {
    return win32_error_string(GetLastError());
}

} // namespace raii
