#pragma once

#include <meccha/launcher/manifest.hpp>
#include <meccha/launcher/runtime_storage.hpp>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace meccha::launcher
{
#ifdef _WIN32
[[nodiscard]] auto read_current_module_rcdata(
    std::uint16_t resource_id)
    -> std::expected<std::vector<std::byte>, RuntimePayloadError>;

class Win32CabPayloadSource final : public RuntimePayloadSource
{
public:
    Win32CabPayloadSource(const Win32CabPayloadSource&) = delete;
    auto operator=(const Win32CabPayloadSource&)
        -> Win32CabPayloadSource& = delete;
    Win32CabPayloadSource(Win32CabPayloadSource&&) noexcept = default;
    auto operator=(Win32CabPayloadSource&&) noexcept
        -> Win32CabPayloadSource& = default;
    ~Win32CabPayloadSource() override = default;

    [[nodiscard]] static auto open(
        std::span<const std::byte> cabinet,
        const PayloadManifest& manifest,
        const std::filesystem::path& scratch_parent)
        -> std::expected<Win32CabPayloadSource, RuntimePayloadError>;

    auto read_file(std::string_view relative_path)
        -> std::expected<
            std::vector<std::byte>,
            RuntimePayloadError> override;

private:
    explicit Win32CabPayloadSource(
        std::unordered_map<std::string, std::vector<std::byte>> files);

    std::unordered_map<std::string, std::vector<std::byte>> files_{};
};
#endif
} // namespace meccha::launcher
