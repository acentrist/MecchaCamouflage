#pragma once

namespace meccha::runtime
{
struct IgnoreInputQueryParametersAbi
{
    bool return_value{};
};
static_assert(sizeof(IgnoreInputQueryParametersAbi) == 0x01U);

struct IgnoreInputCommandParametersAbi
{
    bool new_input{};
};
static_assert(sizeof(IgnoreInputCommandParametersAbi) == 0x01U);

[[nodiscard]] constexpr auto encode_ignore_input(bool ignored) noexcept
    -> IgnoreInputCommandParametersAbi
{
    return IgnoreInputCommandParametersAbi{ignored};
}
} // namespace meccha::runtime
