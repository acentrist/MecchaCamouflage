#include <meccha/core/utf8.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace meccha::core
{
namespace
{
auto scan_utf8(
    std::string_view text,
    std::vector<char32_t>* output) -> bool
{
    auto index = std::size_t{};
    while (index < text.size())
    {
        const auto first =
            static_cast<std::uint8_t>(text[index]);
        if (first <= 0x7FU)
        {
            if (output != nullptr)
            {
                output->push_back(static_cast<char32_t>(first));
            }
            ++index;
            continue;
        }

        auto length = std::size_t{};
        auto codepoint = std::uint32_t{};
        auto minimum = std::uint32_t{};
        if (first >= 0xC2U && first <= 0xDFU)
        {
            length = 2U;
            codepoint = first & 0x1FU;
            minimum = 0x80U;
        }
        else if (first >= 0xE0U && first <= 0xEFU)
        {
            length = 3U;
            codepoint = first & 0x0FU;
            minimum = 0x800U;
        }
        else if (first >= 0xF0U && first <= 0xF4U)
        {
            length = 4U;
            codepoint = first & 0x07U;
            minimum = 0x10000U;
        }
        else
        {
            return false;
        }
        if (index + length > text.size())
        {
            return false;
        }
        for (auto offset = std::size_t{1U};
             offset < length;
             ++offset)
        {
            const auto continuation = static_cast<std::uint8_t>(
                text[index + offset]);
            if ((continuation & 0xC0U) != 0x80U)
            {
                return false;
            }
            codepoint =
                (codepoint << 6U) | (continuation & 0x3FU);
        }
        if (codepoint < minimum || codepoint > 0x10FFFFU ||
            (codepoint >= 0xD800U && codepoint <= 0xDFFFU))
        {
            return false;
        }
        if (output != nullptr)
        {
            output->push_back(static_cast<char32_t>(codepoint));
        }
        index += length;
    }
    return true;
}
} // namespace

auto valid_utf8(std::string_view text) noexcept -> bool
{
    return scan_utf8(text, nullptr);
}

auto decode_utf8(std::string_view text)
    -> std::optional<std::vector<char32_t>>
{
    auto output = std::vector<char32_t>{};
    output.reserve(text.size());
    if (!scan_utf8(text, &output))
    {
        return std::nullopt;
    }
    return output;
}
} // namespace meccha::core
