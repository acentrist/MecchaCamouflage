#include <meccha/application/config_storage_win32.hpp>

#include <Windows.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>

namespace
{
namespace fs = std::filesystem;

auto expect(bool condition, std::string_view message) -> bool
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        return false;
    }
    return true;
}

class TemporaryDirectory
{
public:
    TemporaryDirectory()
    {
        auto buffer = std::wstring(MAX_PATH, L'\0');
        const auto length =
            GetTempPathW(static_cast<DWORD>(buffer.size()), buffer.data());
        if (length == 0U || length >= buffer.size())
        {
            return;
        }
        buffer.resize(length);
        const auto nonce =
            std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = fs::path{buffer} /
                (L"meccha-config-test-" +
                 std::to_wstring(GetCurrentProcessId()) + L"-" +
                 std::to_wstring(nonce));
        std::error_code error{};
        fs::create_directories(path_, error);
        if (error)
        {
            path_.clear();
        }
    }

    TemporaryDirectory(const TemporaryDirectory&) = delete;
    auto operator=(const TemporaryDirectory&)
        -> TemporaryDirectory& = delete;

    ~TemporaryDirectory()
    {
        if (!path_.empty())
        {
            std::error_code ignored{};
            fs::remove_all(path_, ignored);
        }
    }

    [[nodiscard]] auto path() const -> const fs::path&
    {
        return path_;
    }

private:
    fs::path path_{};
};
} // namespace

auto main() -> int
{
    using namespace meccha::application;

    auto passed = true;
    const auto temporary = TemporaryDirectory{};
    passed &= expect(
        !temporary.path().empty(),
        "temporary Windows directory could not be created");
    if (temporary.path().empty())
    {
        return 1;
    }

    auto storage = Win32AtomicTextStorage{temporary.path()};
    passed &= expect(
        storage.root() ==
            temporary.path() / "MecchaCamouflage" / "v2",
        "Win32 storage uses the wrong v2 data root");

    const auto missing = storage.read_text("config.json", 128U);
    passed &= expect(
        missing && !*missing &&
            !fs::exists(storage.root()),
        "read of missing config created storage or failed");

    passed &= expect(
        storage.write_text_atomic("config.json", "first").has_value(),
        "initial atomic write failed");
    const auto first = storage.read_text("config.json", 128U);
    passed &= expect(
        first && *first == std::optional<std::string>{"first"},
        "initial atomic write did not round trip");

    passed &= expect(
        storage.write_text_atomic("config.json", "second").has_value(),
        "atomic replacement failed");
    const auto second = storage.read_text("config.json", 128U);
    passed &= expect(
        second && *second == std::optional<std::string>{"second"} &&
            !fs::exists(storage.root() / "config.json.tmp"),
        "atomic replacement left stale state");

    const auto too_large = storage.read_text("config.json", 5U);
    passed &= expect(
        !too_large &&
            too_large.error().code == TextStorageErrorCode::TooLarge,
        "bounded read accepted an oversized file");

    const auto rejected_name =
        storage.write_text_atomic("../config.json", "escape");
    passed &= expect(
        !rejected_name &&
            rejected_name.error().code ==
                TextStorageErrorCode::Conflict &&
            !fs::exists(
                temporary.path() / "MecchaCamouflage" /
                "config.json"),
        "relative path traversal escaped the v2 root");

    {
        const auto stale = storage.root() / "config.json.tmp";
        auto stream = std::ofstream{stale, std::ios::binary};
        stream << "interrupted";
    }
    passed &= expect(
        storage.write_text_atomic("config.json", "recovered").has_value(),
        "owned regular staging file was not recovered");
    const auto recovered = storage.read_text("config.json", 128U);
    passed &= expect(
        recovered &&
            *recovered == std::optional<std::string>{"recovered"},
        "recovery did not publish the new value");

    std::error_code error{};
    fs::create_directory(storage.root() / "config.json.tmp", error);
    const auto conflict =
        storage.write_text_atomic("config.json", "must-not-publish");
    const auto preserved = storage.read_text("config.json", 128U);
    passed &= expect(
        !conflict &&
            conflict.error().code == TextStorageErrorCode::Conflict &&
            preserved &&
            *preserved == std::optional<std::string>{"recovered"},
        "unsafe staging conflict replaced the valid config");

    if (passed)
    {
        std::cout << "PASS config_storage\n";
    }
    return passed ? 0 : 1;
}
