#pragma once

#include <meccha/core/config.hpp>

#include <cstddef>
#include <expected>
#include <string>
#include <string_view>
#include <vector>

namespace meccha::application
{
inline constexpr std::size_t MaximumConfigBytes = 64U * 1024U;

enum class ConfigCodecErrorCode : std::uint8_t
{
    TooLarge,
    MalformedJson,
    UnsupportedSchema,
    InvalidValue,
    Serialization,
};

struct ConfigCodecError
{
    ConfigCodecErrorCode code{};
    std::vector<core::ConfigurationField> fields{};
    std::string detail{};

    auto operator==(const ConfigCodecError&) const -> bool = default;
};

[[nodiscard]] auto decode_config(std::string_view json)
    -> std::expected<core::ApplicationConfig, ConfigCodecError>;

[[nodiscard]] auto encode_config(const core::ApplicationConfig& config)
    -> std::expected<std::string, ConfigCodecError>;
} // namespace meccha::application
