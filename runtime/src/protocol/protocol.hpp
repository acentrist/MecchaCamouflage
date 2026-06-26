#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>

#include "../util/raii_win32.hpp"
#include "../util/seh_guard.hpp"
#include "../reflection/reflection.hpp"

namespace protocol {

using namespace util;

constexpr std::size_t MaxRequestBytes = 8 * 1024 * 1024;
constexpr int DefaultBridgePort = 47654;

struct BridgeResponse {
    bool ok{false};
    bool success{false};
    std::string stage{};
    std::string message{};
    std::string raw{};
    std::string transport_error{};
    DWORD win32_error{0};
};

inline auto json_escape(const std::string& text) -> std::string {
    std::string out;
    out.reserve(text.size() + 8);
    for (const char c : text) {
        const auto value = static_cast<unsigned char>(c);
        if (c == '\\' || c == '"') { out.push_back('\\'); out.push_back(c); }
        else if (c == '\n') { out += "\\n"; }
        else if (c == '\r') { out += "\\r"; }
        else if (c == '\t') { out += "\\t"; }
        else if (value < 0x20) {
            static constexpr char digits[] = "0123456789abcdef";
            out += "\\u00";
            out.push_back(digits[(value >> 4) & 0x0f]);
            out.push_back(digits[value & 0x0f]);
        } else {
            out.push_back(c);
        }
    }
    return out;
}

inline auto json_string(const std::string& value) -> std::string {
    return std::string("\"") + json_escape(value) + "\"";
}

inline auto response_json(bool success, const char* stage, int applied, int failures,
                           const std::string& message, const std::string& metadata = "") -> std::string
{
    std::string out = "{\"success\":";
    out += success ? "true" : "false";
    out += ",\"stage\":\"";
    out += stage;
    out += "\",\"applied\":";
    out += std::to_string(applied);
    out += ",\"failures\":";
    out += std::to_string(failures);
    out += ",\"message\":\"";
    out += json_escape(message);
    out += "\",\"timing_ms\":{},\"metadata\":{\"bridge\":\"meccha-xenos-bridge\"";
    if (!metadata.empty()) { out += ","; out += metadata; }
    out += "}}\n";
    return out;
}

inline auto fnv1a_update(std::uint64_t hash, const void* data, std::size_t size) -> std::uint64_t {
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    for (std::size_t i = 0; i < size; ++i) { hash ^= static_cast<std::uint64_t>(bytes[i]); hash *= 1099511628211ULL; }
    return hash;
}

inline auto hash_bytes(const std::vector<std::uint8_t>& bytes) -> std::uint64_t {
    return bytes.empty() ? 1469598103934665603ULL : fnv1a_update(1469598103934665603ULL, bytes.data(), bytes.size());
}

inline auto hex64(std::uint64_t value) -> std::string {
    char buf[32]{};
    std::snprintf(buf, sizeof(buf), "%016llx", static_cast<unsigned long long>(value));
    return buf;
}

class Server {
public:
    Server(int port) : port_(port) {}

    auto start(std::string& failure) -> bool {
        socket_.reset(socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
        if (socket_.get() == INVALID_SOCKET) { failure = "socket_failed"; return false; }
        int opt = 1;
        setsockopt(socket_.get(), SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&opt), sizeof(opt));
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = htons(static_cast<u_short>(port_));
        if (bind(socket_.get(), reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
            failure = "bind_failed"; return false;
        }
        if (listen(socket_.get(), 1) == SOCKET_ERROR) {
            failure = "listen_failed"; return false;
        }
        return true;
    }

    auto accept(int timeout_ms = 30000) -> raii::socket_ {
        if (timeout_ms > 0) {
            fd_set fds; FD_ZERO(&fds); FD_SET(socket_.get(), &fds);
            timeval tv{}; tv.tv_sec = timeout_ms / 1000; tv.tv_usec = (timeout_ms % 1000) * 1000;
            const int result = select(0, &fds, nullptr, nullptr, &tv);
            if (result <= 0) return raii::socket_{};
        }
        sockaddr_in client{};
        int client_len = sizeof(client);
        SOCKET client_sock = ::accept(socket_.get(), reinterpret_cast<sockaddr*>(&client), &client_len);
        return raii::socket_(client_sock);
    }

    auto stop() -> void { socket_.reset(); }
    auto port() const -> int { return port_; }

private:
    raii::socket_ socket_;
    int port_;
};

class Client {
public:
    Client(const std::string& host, int port, double timeout_seconds)
        : host_(host), port_(port), timeout_ms_(static_cast<int>(std::max(0.1, timeout_seconds) * 1000.0)) {}

    auto request(const std::string& command, const std::string& payload_json = "{}") -> BridgeResponse {
        raii::socket_ s(socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
        if (s.get() == INVALID_SOCKET) return fail("socket_failed", WSAGetLastError());
        int rcv_timeout = timeout_ms_;
        setsockopt(s.get(), SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&rcv_timeout), sizeof(rcv_timeout));
        setsockopt(s.get(), SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&rcv_timeout), sizeof(rcv_timeout));
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(static_cast<u_short>(port_));
        inet_pton(AF_INET, host_.c_str(), &addr.sin_addr);
        if (connect(s.get(), reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
            return fail("connect_failed", WSAGetLastError());
        }
        const std::string line = make_request_line(command, payload_json);
        if (send(s.get(), line.c_str(), static_cast<int>(line.size()), 0) <= 0) {
            return fail("send_failed", WSAGetLastError());
        }
        std::string raw;
        raw.reserve(65536);
        char buffer[16384]{};
        while (raw.find('\n') == std::string::npos && raw.size() < MaxRequestBytes) {
            const int received = recv(s.get(), buffer, static_cast<int>(sizeof(buffer)), 0);
            if (received <= 0) break;
            raw.append(buffer, static_cast<std::size_t>(received));
        }
        if (raw.empty()) return fail("empty_response", 0);
        if (const auto newline = raw.find('\n'); newline != std::string::npos) raw.resize(newline);
        return parse_response(raw);
    }

private:
    static auto fail(const std::string& message, DWORD error) -> BridgeResponse {
        return {false, false, "bridge_connect", message, {}, message, error};
    }

    static auto make_request_line(const std::string& command, const std::string& payload_json) -> std::string {
        static std::atomic<unsigned long long> counter{1};
        const auto id = counter.fetch_add(1);
        const std::string request_id = hex64(reflection::fnv1a64(&id, sizeof(id))) + hex64(GetTickCount64());
        return std::string("{\"type\":") + json_string(command) +
               ",\"request_id\":" + json_string(request_id) +
               ",\"payload\":" + (payload_json.empty() ? "{}" : payload_json) + "}\n";
    }

    static auto parse_response(std::string raw) -> BridgeResponse {
        BridgeResponse response{};
        response.ok = !raw.empty();
        response.raw = std::move(raw);
        auto extract_bool = [&](const std::string& key) -> bool {
            const std::string needle = std::string("\"") + key + "\":";
            const auto start = response.raw.find(needle);
            if (start == std::string::npos) return false;
            return response.raw.compare(start + needle.size(), 4, "true") == 0;
        };
        auto extract_string = [&](const std::string& key) -> std::string {
            const std::string needle = std::string("\"") + key + "\":\"";
            const auto start = response.raw.find(needle);
            if (start == std::string::npos) return {};
            std::string out; bool escape = false;
            for (std::size_t i = start + needle.size(); i < response.raw.size(); ++i) {
                const char c = response.raw[i];
                if (escape) { switch (c) { case 'n': out.push_back('\n'); break; case 'r': out.push_back('\r'); break; case 't': out.push_back('\t'); break; default: out.push_back(c); } escape = false; continue; }
                if (c == '\\') { escape = true; continue; }
                if (c == '"') break;
                out.push_back(c);
            }
            return out;
        };
        response.success = extract_bool("success");
        response.stage = extract_string("stage");
        response.message = extract_string("message");
        if (response.stage.empty()) response.stage = response.success ? "ok" : "bridge_response";
        return response;
    }

    std::string host_;
    int port_;
    int timeout_ms_;
};

struct ScriptArrayParam {
    void* data{nullptr};
    int num{0};
    int max{0};
};

} // namespace protocol
