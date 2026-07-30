#include <meccha/launcher/application_win32.hpp>
#include <meccha/launcher/elevated_broker.hpp>
#include <meccha/launcher/elevated_child.hpp>
#include <meccha/launcher/elevated_loader.hpp>
#include <meccha/launcher/elevated_transport.hpp>
#include <meccha/launcher/embedded_package.hpp>
#include <meccha/launcher/execution_win32.hpp>
#include <meccha/launcher/observation_win32.hpp>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commctrl.h>
#include <objbase.h>
#include <shellapi.h>
#include <shlobj.h>

#include <array>
#include <cstddef>
#include <expected>
#include <filesystem>
#include <fstream>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace
{
using namespace meccha::launcher;
namespace fs = std::filesystem;

class CommandLineArguments
{
public:
    CommandLineArguments(const CommandLineArguments&) = delete;
    auto operator=(const CommandLineArguments&)
        -> CommandLineArguments& = delete;

    CommandLineArguments(CommandLineArguments&& other) noexcept
        : values_(std::exchange(other.values_, nullptr)),
          count_(std::exchange(other.count_, 0))
    {
    }

    ~CommandLineArguments()
    {
        if (values_ != nullptr)
        {
            static_cast<void>(LocalFree(values_));
        }
    }

    [[nodiscard]] static auto read()
        -> std::expected<
            CommandLineArguments,
            std::string>
    {
        int count{};
        auto** values = CommandLineToArgvW(
            GetCommandLineW(),
            &count);
        if (values == nullptr || count < 1)
        {
            if (values != nullptr)
            {
                static_cast<void>(LocalFree(values));
            }
            return std::unexpected(
                "The Windows command line could not be parsed.");
        }
        return CommandLineArguments{values, count};
    }

    [[nodiscard]] auto values() const
        -> std::span<wchar_t* const>
    {
        return {values_, static_cast<std::size_t>(count_)};
    }

private:
    CommandLineArguments(wchar_t** values, int count)
        : values_(values),
          count_(count)
    {
    }

    wchar_t** values_{};
    int count_{};
};

class ComApartment
{
public:
    ComApartment(const ComApartment&) = delete;
    auto operator=(const ComApartment&) -> ComApartment& = delete;

    ComApartment(ComApartment&& other) noexcept
        : initialized_(
              std::exchange(other.initialized_, false))
    {
    }

    ~ComApartment()
    {
        if (initialized_)
        {
            CoUninitialize();
        }
    }

    [[nodiscard]] static auto initialize()
        -> std::expected<ComApartment, std::string>
    {
        const auto result = CoInitializeEx(
            nullptr,
            COINIT_APARTMENTTHREADED |
                COINIT_DISABLE_OLE1DDE);
        if (FAILED(result))
        {
            return std::unexpected(
                "COM initialization failed (HRESULT " +
                std::to_string(
                    static_cast<unsigned long>(result)) +
                ").");
        }
        return ComApartment{true};
    }

private:
    explicit ComApartment(bool initialized)
        : initialized_(initialized)
    {
    }

    bool initialized_{};
};

auto wide_from_utf8(std::string_view value)
    -> std::wstring
{
    if (value.empty() ||
        value.size() >
            static_cast<std::size_t>(
                std::numeric_limits<int>::max()))
    {
        return L"An error occurred.";
    }
    const auto required = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        value.data(),
        static_cast<int>(value.size()),
        nullptr,
        0);
    if (required <= 0)
    {
        return L"An error occurred; see launcher.log.";
    }
    auto result =
        std::wstring(
            static_cast<std::size_t>(required),
            L'\0');
    if (MultiByteToWideChar(
            CP_UTF8,
            MB_ERR_INVALID_CHARS,
            value.data(),
            static_cast<int>(value.size()),
            result.data(),
            required) != required)
    {
        return L"An error occurred; see launcher.log.";
    }
    return result;
}

auto utf8_from_wide(std::wstring_view value)
    -> std::expected<std::string, std::string>
{
    if (value.empty())
    {
        return std::string{};
    }
    if (value.size() >
        static_cast<std::size_t>(
            std::numeric_limits<int>::max()))
    {
        return std::unexpected(
            "A launcher argument exceeds the supported length.");
    }
    const auto required = WideCharToMultiByte(
        CP_UTF8,
        WC_ERR_INVALID_CHARS,
        value.data(),
        static_cast<int>(value.size()),
        nullptr,
        0,
        nullptr,
        nullptr);
    if (required <= 0)
    {
        return std::unexpected(
            "A launcher argument is not valid Unicode.");
    }
    auto result =
        std::string(
            static_cast<std::size_t>(required),
            '\0');
    if (WideCharToMultiByte(
            CP_UTF8,
            WC_ERR_INVALID_CHARS,
            value.data(),
            static_cast<int>(value.size()),
            result.data(),
            required,
            nullptr,
            nullptr) != required)
    {
        return std::unexpected(
            "A launcher argument could not be converted to UTF-8.");
    }
    return result;
}

auto public_arguments(
    std::span<const std::wstring_view> values)
    -> std::expected<
        std::vector<std::string>,
        std::string>
{
    auto result = std::vector<std::string>{};
    result.reserve(values.size());
    for (const auto value : values)
    {
        auto converted = utf8_from_wide(value);
        if (!converted)
        {
            return std::unexpected(converted.error());
        }
        result.push_back(std::move(*converted));
    }
    return result;
}

auto workflow_error_detail(
    const LauncherWorkflowError& error) -> std::string
{
    return std::visit(
        [](const auto& value)
        {
            return value.detail;
        },
        error);
}

auto application_error_detail(
    const Win32LauncherApplicationError& error)
    -> std::string
{
    return std::visit(
        [](const auto& value) -> std::string
        {
            using Value =
                std::remove_cvref_t<decltype(value)>;
            if constexpr (std::is_same_v<
                              Value,
                              LauncherWorkflowError>)
            {
                return workflow_error_detail(value);
            }
            else
            {
                return value.detail;
            }
        },
        error);
}

auto show_dialog(
    std::wstring_view instruction,
    std::wstring_view content,
    PCWSTR icon) -> void
{
    int selected{};
    static_cast<void>(TaskDialog(
        nullptr,
        nullptr,
        L"MecchaCamouflage",
        std::wstring{instruction}.c_str(),
        std::wstring{content}.c_str(),
        TDCBF_OK_BUTTON,
        icon,
        &selected));
}

auto launcher_log_path() -> fs::path
{
    PWSTR raw_path{};
    if (SUCCEEDED(SHGetKnownFolderPath(
            FOLDERID_LocalAppData,
            KF_FLAG_DEFAULT,
            nullptr,
            &raw_path)) &&
        raw_path != nullptr)
    {
        auto result =
            fs::path{raw_path} /
            "MecchaCamouflage/v2/logs/launcher.log";
        CoTaskMemFree(raw_path);
        return result;
    }
    if (raw_path != nullptr)
    {
        CoTaskMemFree(raw_path);
    }
    auto buffer = std::vector<wchar_t>(32768U);
    const auto size = GetTempPathW(
        static_cast<DWORD>(buffer.size()),
        buffer.data());
    if (size == 0U || size >= buffer.size())
    {
        return {};
    }
    return fs::path{
               std::wstring_view{buffer.data(), size}} /
           "MecchaCamouflage-v2-launcher.log";
}

auto append_log(std::string_view level, std::string_view detail)
    -> void
{
    const auto path = launcher_log_path();
    if (path.empty())
    {
        return;
    }
    std::error_code directory_error{};
    fs::create_directories(
        path.parent_path(),
        directory_error);
    if (directory_error)
    {
        return;
    }
    SYSTEMTIME time{};
    GetLocalTime(&time);
    std::ofstream output{
        path,
        std::ios::binary | std::ios::app};
    if (!output)
    {
        return;
    }
    output << time.wYear << '-'
           << time.wMonth << '-'
           << time.wDay << ' '
           << time.wHour << ':'
           << time.wMinute << ':'
           << time.wSecond << ' '
           << level << ' ' << detail << '\n';
}

auto run_internal_child(
    std::span<const std::wstring_view> arguments) -> int
{
    const auto invocation =
        parse_elevated_broker_child_invocation(arguments);
    if (!invocation)
    {
        return 2;
    }
    auto package_source =
        Win32EmbeddedLauncherPackageSource{};
    auto environment =
        Win32ElevatedBrokerChildEnvironment{};
    auto mutation_platform =
        Win32ElevatedLoaderMutationPlatform{};
    auto peer_validator =
        Win32ElevatedBrokerPeerValidator{};
    const auto result =
        run_embedded_elevated_broker_child(
            *invocation,
            package_source,
            environment,
            mutation_platform,
            peer_validator);
    return result ? 0 : 3;
}

auto run_normal_launcher(
    std::span<const std::wstring_view> wide_arguments) -> int
{
    const auto converted =
        public_arguments(wide_arguments);
    if (!converted)
    {
        append_log("ERROR", converted.error());
        show_dialog(
            L"Invalid launcher arguments",
            wide_from_utf8(converted.error()),
            TD_ERROR_ICON);
        return 2;
    }
    auto views = std::vector<std::string_view>{};
    views.reserve(converted->size());
    for (const auto& argument : *converted)
    {
        views.push_back(argument);
    }

    auto bootstrap_platform =
        Win32LauncherBootstrapPlatform{};
    auto package_source =
        Win32EmbeddedLauncherPackageSource{};
    auto observation_platform =
        Win32OriginalUserObservationPlatform{};
    auto nonce_source =
        Win32ElevatedBrokerNonceSource{};
    auto child_launcher =
        Win32RunAsElevatedBrokerChildLauncher{};
    auto peer_validator =
        Win32ElevatedBrokerPeerValidator{};
    auto mutation_client =
        Win32NamedPipeElevatedLoaderMutationClient{
            child_launcher,
            peer_validator,
        };
    auto broker_provider =
        Win32ElevatedLoaderBrokerProvider{
            nonce_source,
            mutation_client,
        };
    auto steam_launcher = Win32SteamGameLauncher{};

    const auto result = run_win32_launcher_application(
        Win32LauncherApplicationInputs{
            views,
            bootstrap_platform,
            package_source,
            observation_platform,
            broker_provider,
            steam_launcher,
        });
    if (!result)
    {
        const auto detail =
            application_error_detail(result.error());
        append_log("ERROR", detail);
        show_dialog(
            L"MecchaCamouflage could not continue",
            wide_from_utf8(detail),
            TD_ERROR_ICON);
        return 1;
    }
    append_log("INFO", "Launcher operation completed.");
    if (result->arguments.mode ==
        LauncherInvocationMode::PrepareOnly)
    {
        show_dialog(
            L"Preparation completed",
            L"MecchaCamouflage is ready. No game was launched.",
            TD_INFORMATION_ICON);
    }
    else if (result->arguments.mode ==
             LauncherInvocationMode::Remove)
    {
        show_dialog(
            L"Removal completed",
            L"MecchaCamouflage-owned files and cache were removed.",
            TD_INFORMATION_ICON);
    }
    return 0;
}
} // namespace

auto WINAPI wWinMain(
    HINSTANCE,
    HINSTANCE,
    PWSTR,
    int) -> int
{
    const auto arguments = CommandLineArguments::read();
    if (!arguments)
    {
        show_dialog(
            L"MecchaCamouflage could not start",
            wide_from_utf8(arguments.error()),
            TD_ERROR_ICON);
        return 2;
    }
    auto wide_arguments =
        std::vector<std::wstring_view>{};
    const auto raw = arguments->values();
    wide_arguments.reserve(raw.size() - 1U);
    for (std::size_t index = 1U;
         index < raw.size();
         ++index)
    {
        wide_arguments.emplace_back(raw[index]);
    }
    if (is_elevated_broker_child_invocation(
            wide_arguments))
    {
        return run_internal_child(wide_arguments);
    }
    const auto apartment = ComApartment::initialize();
    if (!apartment)
    {
        append_log("ERROR", apartment.error());
        show_dialog(
            L"MecchaCamouflage could not start",
            wide_from_utf8(apartment.error()),
            TD_ERROR_ICON);
        return 2;
    }
    return run_normal_launcher(wide_arguments);
}
