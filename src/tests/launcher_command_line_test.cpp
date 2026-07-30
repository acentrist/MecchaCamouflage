#include <meccha/launcher/command_line.hpp>

#include <array>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace
{
auto expect(bool condition, std::string_view message) -> bool
{
    if (!condition)
    {
        std::cerr << "FAIL launcher_command_line: "
                  << message << '\n';
    }
    return condition;
}
} // namespace

auto main() -> int
{
    using namespace meccha::launcher;

    const auto defaults =
        parse_launcher_arguments(std::span<const std::string_view>{});
    auto passed = expect(
        defaults &&
            defaults->mode ==
                LauncherInvocationMode::PrepareAndLaunch &&
            !defaults->game_directory_utf8,
        "empty arguments did not select prepare-and-launch");

    constexpr std::array prepare_only_arguments{
        std::string_view{"--game-dir"},
        std::string_view{R"(D:\Steam Library\game)"},
        std::string_view{"--prepare-only"},
    };
    const auto prepare_only =
        parse_launcher_arguments(prepare_only_arguments);
    passed &= expect(
        prepare_only &&
            prepare_only->mode ==
                LauncherInvocationMode::PrepareOnly &&
            prepare_only->game_directory_utf8 ==
                std::optional<std::string>{
                    R"(D:\Steam Library\game)"},
        "supported prepare-only arguments were not parsed");

    constexpr std::array remove_arguments{
        std::string_view{"--remove"},
    };
    const auto remove = parse_launcher_arguments(remove_arguments);
    passed &= expect(
        remove &&
            remove->mode == LauncherInvocationMode::Remove &&
            !remove->game_directory_utf8,
        "remove mode was not parsed");

    constexpr std::array missing_value_arguments{
        std::string_view{"--game-dir"},
    };
    const auto missing_value =
        parse_launcher_arguments(missing_value_arguments);
    passed &= expect(
        !missing_value &&
            missing_value.error().code ==
                LauncherArgumentErrorCode::MissingValue,
        "missing --game-dir value was accepted");

    constexpr std::array conflicting_arguments{
        std::string_view{"--prepare-only"},
        std::string_view{"--remove"},
    };
    const auto conflicting =
        parse_launcher_arguments(conflicting_arguments);
    passed &= expect(
        !conflicting &&
            conflicting.error().code ==
                LauncherArgumentErrorCode::ConflictingMode,
        "conflicting launcher modes were accepted");

    constexpr std::array duplicate_arguments{
        std::string_view{"--game-dir"},
        std::string_view{"first"},
        std::string_view{"--game-dir"},
        std::string_view{"second"},
    };
    const auto duplicate =
        parse_launcher_arguments(duplicate_arguments);
    passed &= expect(
        !duplicate &&
            duplicate.error().code ==
                LauncherArgumentErrorCode::DuplicateArgument,
        "duplicate --game-dir was accepted");

    constexpr std::array unknown_arguments{
        std::string_view{"--install"},
    };
    const auto unknown =
        parse_launcher_arguments(unknown_arguments);
    constexpr std::array internal_arguments{
        std::string_view{
            "--meccha-internal-elevated-broker-v1"},
        std::string_view{
            "0123456789abcdef0123456789abcdef"},
        std::string_view{"4242"},
    };
    const auto internal =
        parse_launcher_arguments(internal_arguments);
    passed &= expect(
        !unknown &&
            unknown.error().code ==
                LauncherArgumentErrorCode::UnknownArgument &&
            !internal &&
            internal.error().code ==
                LauncherArgumentErrorCode::UnknownArgument,
        "an unsupported or internal launcher operation was accepted");

    constexpr char EmbeddedNull[]{"bad\0path"};
    const std::array invalid_value_arguments{
        std::string_view{"--game-dir"},
        std::string_view{
            EmbeddedNull,
            sizeof(EmbeddedNull) - 1U},
    };
    const auto invalid_value =
        parse_launcher_arguments(invalid_value_arguments);
    passed &= expect(
        !invalid_value &&
            invalid_value.error().code ==
                LauncherArgumentErrorCode::InvalidValue,
        "an embedded-NUL game directory was accepted");

    if (passed)
    {
        std::cout << "PASS launcher_command_line\n";
        return 0;
    }
    return 1;
}
