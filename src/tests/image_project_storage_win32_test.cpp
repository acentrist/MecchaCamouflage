#include <meccha/application/image_project_storage_win32.hpp>
#include <meccha/application/image_project_codec.hpp>

#include <Windows.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace
{
namespace fs = std::filesystem;

auto expect(bool condition, std::string_view message) -> bool
{
    if (!condition)
    {
        std::cerr << "FAIL image_project_storage: "
                  << message << '\n';
    }
    return condition;
}

class TemporaryDirectory
{
public:
    TemporaryDirectory()
    {
        auto buffer = std::wstring(MAX_PATH, L'\0');
        const auto length = GetTempPathW(
            static_cast<DWORD>(buffer.size()),
            buffer.data());
        if (length == 0U || length >= buffer.size())
        {
            return;
        }
        buffer.resize(length);
        const auto nonce =
            std::chrono::steady_clock::now()
                .time_since_epoch()
                .count();
        path_ =
            fs::path{buffer} /
            (L"meccha-project-storage-test-" +
             std::to_wstring(GetCurrentProcessId()) + L"-" +
             std::to_wstring(nonce) + L"-日本語");
        auto error = std::error_code{};
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
            auto ignored = std::error_code{};
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

auto bytes(std::initializer_list<unsigned char> values)
    -> std::vector<std::byte>
{
    auto result = std::vector<std::byte>{};
    result.reserve(values.size());
    for (const auto value : values)
    {
        result.push_back(static_cast<std::byte>(value));
    }
    return result;
}
} // namespace

auto main() -> int
{
    using namespace meccha;
    using namespace meccha::application;

    constexpr auto name =
        std::string_view{
            "0123456789abcdef0123456789abcdef.mcpreset"};
    auto passed = true;
    const auto temporary = TemporaryDirectory{};
    passed &= expect(
        !temporary.path().empty(),
        "temporary Windows directory could not be created");
    if (temporary.path().empty())
    {
        return 1;
    }

    auto storage =
        Win32AtomicProjectStorage{temporary.path()};
    passed &= expect(
        storage.root() ==
            temporary.path() / "MecchaCamouflage" / "v2" /
                "image-projects",
        "project storage uses the wrong v2 directory");

    const auto missing = storage.read(name, 128U);
    if (!missing)
    {
        std::cerr << "project missing-read detail: "
                  << missing.error().detail << '\n';
    }
    passed &= expect(
        missing && !*missing && !fs::exists(storage.root()),
        "missing project read created storage or failed");

    const auto first_bytes = bytes({1U, 2U, 3U});
    const auto first =
        storage.write_atomic(name, first_bytes);
    const auto first_read = storage.read(name, 128U);
    passed &= expect(
        first && first_read && *first_read &&
            **first_read == first_bytes,
        "initial project publication did not round trip");

    const auto stale =
        storage.root() /
        "0123456789abcdef0123456789abcdef.mcpreset.tmp."
        "00000000000000000000000000000000";
    {
        auto stream = std::ofstream{stale, std::ios::binary};
        stream << "interrupted";
    }
    const auto second_bytes = bytes({9U, 8U, 7U, 6U});
    const auto second =
        storage.write_atomic(name, second_bytes);
    const auto second_read = storage.read(name, 128U);
    passed &= expect(
        second && second_read && *second_read &&
            **second_read == second_bytes &&
            !fs::exists(stale) &&
            std::ranges::none_of(
                fs::directory_iterator{storage.root()},
                [](const fs::directory_entry& entry)
                {
                    return entry.path().filename().wstring().contains(
                        L".mcpreset.tmp.");
                }),
        "replacement did not recover and remove unique staging");

    const auto too_large = storage.read(name, 3U);
    passed &= expect(
        !too_large &&
            too_large.error().code ==
                ProjectStorageErrorCode::TooLarge,
        "bounded project read accepted an oversized file");

    {
        const auto oversized_bytes =
            std::vector<std::byte>(
                MaximumPresetContainerBytes + 1U,
                std::byte{0});
        const auto oversized_write =
            storage.write_atomic(name, oversized_bytes);
        passed &= expect(
            !oversized_write &&
                oversized_write.error().code ==
                    ProjectStorageErrorCode::TooLarge,
            "oversized project bytes reached staging");
    }

    const auto traversal =
        storage.write_atomic("../escape.mcpreset", first_bytes);
    passed &= expect(
        !traversal &&
            traversal.error().code ==
                ProjectStorageErrorCode::Conflict,
        "project path traversal reached the filesystem");

    const auto staging_conflict =
        storage.root() /
        "0123456789abcdef0123456789abcdef.mcpreset.tmp."
        "11111111111111111111111111111111";
    auto filesystem_error = std::error_code{};
    const auto foreign_staging =
        storage.root() /
        "0123456789abcdef0123456789abcdef.mcpreset.tmp.foreign";
    {
        auto stream =
            std::ofstream{foreign_staging, std::ios::binary};
        stream << "foreign";
    }
    passed &= expect(
        storage.write_atomic(name, second_bytes) &&
            fs::exists(foreign_staging),
        "project storage removed an unowned staging-like file");

    fs::create_directory(staging_conflict, filesystem_error);
    const auto conflicted =
        storage.write_atomic(name, first_bytes);
    const auto preserved = storage.read(name, 128U);
    passed &= expect(
        !conflicted &&
            conflicted.error().code ==
                ProjectStorageErrorCode::Conflict &&
            preserved && *preserved &&
            **preserved == second_bytes,
        "unsafe staging conflict changed the valid project");

    fs::remove_all(staging_conflict, filesystem_error);
    const auto removed = storage.remove(name);
    const auto removed_again = storage.remove(name);
    passed &= expect(
        removed && *removed && removed_again &&
            !*removed_again,
        "project removal was not idempotent");

    fs::create_directory(storage.root() / name, filesystem_error);
    const auto unsafe_target = storage.read(name, 128U);
    passed &= expect(
        !unsafe_target &&
            unsafe_target.error().code ==
                ProjectStorageErrorCode::Conflict,
        "project target directory was treated as a file");

    fs::remove_all(storage.root() / name, filesystem_error);
    const auto external =
        temporary.path() / "external.mcpreset";
    {
        auto stream = std::ofstream{external, std::ios::binary};
        stream << "external";
    }
    const auto linked = CreateSymbolicLinkW(
        (storage.root() / name).c_str(),
        external.c_str(),
        SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE);
    if (linked)
    {
        const auto reparse_target = storage.read(name, 128U);
        passed &= expect(
            !reparse_target &&
                reparse_target.error().code ==
                    ProjectStorageErrorCode::Conflict &&
                fs::file_size(external) == 8U,
            "project target reparse point was followed or accepted");
    }
    else
    {
        std::cout
            << "SKIP image_project_storage reparse fixture: Windows "
               "symbolic-link creation is unavailable\n";
    }

    auto native_hasher = NativePresetHasher{};
    const auto source =
        std::make_shared<const std::vector<std::byte>>(
            std::initializer_list<std::byte>{
                std::byte{1},
                std::byte{2},
                std::byte{3},
            });
    const auto source_digest = native_hasher.hash(*source);
    if (source_digest)
    {
        const auto asset_id =
            common::sha256_hex(*source_digest);
        const auto project_id =
            std::string{"fedcba9876543210fedcba9876543210"};
        const auto project = core::ImageProject{
            core::ImageProjectSchemaVersion,
            project_id,
            "Native 日本語",
            1U,
            {},
            {core::ImageLayer{
                asset_id,
                "source.png",
                core::ImageMime::Png,
                source->size(),
            }},
            {core::ImageSourceAsset{
                asset_id,
                core::ImageMime::Png,
                source,
            }},
            std::make_shared<const std::vector<std::byte>>(
                core::CanonicalAtlasByteLength,
                std::byte{0x7F}),
        };
        auto project_store =
            ImageProjectStore{storage, native_hasher};
        const auto saved =
            project_store.save_named(project, 0U);
        const auto loaded =
            project_store.load_named(project_id);
        passed &= expect(
            saved && loaded && *loaded &&
                **loaded == project,
            "native codec/store/storage composition did not round trip");
    }
    else
    {
        passed &= expect(
            false,
            "native SHA-256 provider was unavailable");
    }

    if (passed)
    {
        std::cout << "PASS image_project_storage\n";
        return 0;
    }
    return 1;
}
