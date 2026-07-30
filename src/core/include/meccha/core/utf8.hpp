#pragma once

#include <optional>
#include <string_view>
#include <vector>

namespace meccha::core
{
[[nodiscard]] auto valid_utf8(std::string_view text) noexcept -> bool;

[[nodiscard]] auto decode_utf8(std::string_view text)
    -> std::optional<std::vector<char32_t>>;
} // namespace meccha::core
