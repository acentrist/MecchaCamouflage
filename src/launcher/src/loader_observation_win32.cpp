#include <meccha/launcher/loader_observation.hpp>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>

#include <expected>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace meccha::launcher
{
namespace
{
auto error(std::string detail)
    -> std::unexpected<LoaderObservationError>
{
    return std::unexpected(LoaderObservationError{
        LoaderObservationErrorCode::Arguments,
        std::move(detail),
    });
}

auto utf8_to_wide(std::string_view value)
    -> std::expected<std::wstring, LoaderObservationError>
{
    if (value.empty())
    {
        return std::wstring{};
    }
    const auto length = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        value.data(),
        static_cast<int>(value.size()),
        nullptr,
        0);
    if (length <= 0)
    {
        return error("Steam launch options are not valid UTF-8");
    }
    auto converted = std::wstring(static_cast<std::size_t>(length), L'\0');
    if (MultiByteToWideChar(
            CP_UTF8,
            MB_ERR_INVALID_CHARS,
            value.data(),
            static_cast<int>(value.size()),
            converted.data(),
            length) != length)
    {
        return error("Steam launch options could not be decoded");
    }
    return converted;
}

auto wide_to_utf8(std::wstring_view value)
    -> std::expected<std::string, LoaderObservationError>
{
    if (value.empty())
    {
        return std::string{};
    }
    const auto length = WideCharToMultiByte(
        CP_UTF8,
        WC_ERR_INVALID_CHARS,
        value.data(),
        static_cast<int>(value.size()),
        nullptr,
        0,
        nullptr,
        nullptr);
    if (length <= 0)
    {
        return error("a Steam launch argument is not valid Unicode");
    }
    auto converted = std::string(static_cast<std::size_t>(length), '\0');
    if (WideCharToMultiByte(
            CP_UTF8,
            WC_ERR_INVALID_CHARS,
            value.data(),
            static_cast<int>(value.size()),
            converted.data(),
            length,
            nullptr,
            nullptr) != length)
    {
        return error("a Steam launch argument could not be encoded");
    }
    return converted;
}
} // namespace

auto analyze_windows_launch_options(
    std::string_view utf8_options,
    const std::filesystem::path& game_directory)
    -> std::expected<
        std::optional<std::filesystem::path>,
        LoaderObservationError>
{
    if (utf8_options.empty())
    {
        return std::optional<std::filesystem::path>{};
    }
    const auto decoded = utf8_to_wide(utf8_options);
    if (!decoded)
    {
        return std::unexpected(decoded.error());
    }

    auto command_line = std::wstring{L"game.exe "};
    command_line += *decoded;
    int count{};
    LPWSTR* raw_arguments =
        CommandLineToArgvW(command_line.c_str(), &count);
    if (raw_arguments == nullptr || count < 1)
    {
        return error("Steam launch options could not be tokenized");
    }

    auto arguments = std::vector<std::string>{};
    arguments.reserve(static_cast<std::size_t>(count - 1));
    auto conversion_error =
        std::optional<LoaderObservationError>{};
    for (int index = 1; index < count; ++index)
    {
        const auto converted = wide_to_utf8(raw_arguments[index]);
        if (!converted)
        {
            conversion_error = converted.error();
            break;
        }
        arguments.push_back(*converted);
    }
    LocalFree(raw_arguments);
    if (conversion_error)
    {
        return std::unexpected(std::move(*conversion_error));
    }

    auto argument_views = std::vector<std::string_view>{};
    argument_views.reserve(arguments.size());
    for (const auto& argument : arguments)
    {
        argument_views.push_back(argument);
    }
    const auto analysis = analyze_ue4ss_arguments(argument_views);
    if (!analysis)
    {
        return std::unexpected(analysis.error());
    }
    if (!*analysis)
    {
        return std::optional<std::filesystem::path>{};
    }
    const auto target = resolve_command_line_target(
        game_directory,
        (**analysis).path);
    if (!target)
    {
        return std::unexpected(target.error());
    }
    return std::optional<std::filesystem::path>{*target};
}
} // namespace meccha::launcher
