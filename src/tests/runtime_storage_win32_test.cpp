#include <meccha/launcher/hash.hpp>
#include <meccha/launcher/runtime_storage.hpp>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
using namespace meccha::launcher;
namespace fs = std::filesystem;

auto bytes(std::string_view value) -> std::vector<std::byte>
{
    const auto first =
        reinterpret_cast<const std::byte*>(value.data());
    return {first, first + value.size()};
}

class MemoryPayload final : public RuntimePayloadSource
{
public:
    MemoryPayload() = default;
    MemoryPayload(MemoryPayload&&) = default;
    auto operator=(MemoryPayload&&) -> MemoryPayload& = default;

    auto read_file(std::string_view relative_path)
        -> std::expected<std::vector<std::byte>, RuntimePayloadError> override
    {
        ++read_count;
        if (fail_on_read != 0 && read_count == fail_on_read)
        {
            return std::unexpected(RuntimePayloadError{
                "injected payload read failure"});
        }
        const auto found = files.find(std::string{relative_path});
        if (found == files.end())
        {
            return std::unexpected(RuntimePayloadError{
                "missing test payload file"});
        }
        return found->second;
    }

    std::map<std::string, std::vector<std::byte>, std::less<>> files{};
    std::size_t read_count{};
    std::size_t fail_on_read{};
};

struct Package
{
    std::string manifest_json{};
    Sha256Digest manifest_sha256{};
    MemoryPayload payload{};
};

auto make_package(std::string_view suffix) -> Package
{
    Package package{};
    package.payload.files["UE4SS.dll"] =
        bytes(std::string{"runtime-"} + std::string{suffix});
    package.payload.files["Mods/MecchaCamouflage/dlls/main.dll"] =
        bytes(std::string{"mod-"} + std::string{suffix});
    package.payload.files["dwmapi.dll"] =
        bytes(std::string{"proxy-"} + std::string{suffix});

    const auto runtime_hash =
        sha256_bytes(package.payload.files.at("UE4SS.dll")).value();
    const auto mod_hash = sha256_bytes(
        package.payload.files.at(
            "Mods/MecchaCamouflage/dlls/main.dll")).value();
    const auto proxy_hash =
        sha256_bytes(package.payload.files.at("dwmapi.dll")).value();
    package.manifest_json =
        std::string{R"json({"schema_version":1,"product_version":"2.0.0",)json"} +
        R"json("ue4ss_commit":"6c26f038751b3d96059d4a9148f5d093012d55ad",)json" +
        R"json("generated_paths":["Logs"],"files":[)json" +
        R"json({"path":"UE4SS.dll","role":"runtime","size":)json" +
        std::to_string(package.payload.files.at("UE4SS.dll").size()) +
        R"json(,"sha256":")json" + sha256_hex(runtime_hash) +
        R"json("},{"path":"Mods/MecchaCamouflage/dlls/main.dll",)json" +
        R"json("role":"mod","size":)json" +
        std::to_string(
            package.payload.files.at(
                "Mods/MecchaCamouflage/dlls/main.dll").size()) +
        R"json(,"sha256":")json" + sha256_hex(mod_hash) +
        R"json("},{"path":"dwmapi.dll","role":"proxy","size":)json" +
        std::to_string(
            package.payload.files.at("dwmapi.dll").size()) +
        R"json(,"sha256":")json" + sha256_hex(proxy_hash) +
        R"json("}]})json";
    package.manifest_sha256 =
        sha256_bytes(std::as_bytes(std::span{package.manifest_json})).value();
    return package;
}

class TemporaryTree
{
public:
    TemporaryTree()
    {
        root = fs::temp_directory_path() /
               ("meccha-v2-runtime-" +
                std::to_string(GetCurrentProcessId()) + "-" +
                std::to_string(GetTickCount64()));
        fs::create_directories(root);
    }

    TemporaryTree(const TemporaryTree&) = delete;
    auto operator=(const TemporaryTree&) -> TemporaryTree& = delete;

    ~TemporaryTree()
    {
        std::error_code ignored{};
        fs::remove_all(root, ignored);
    }

    fs::path root{};
};

auto write_bytes(const fs::path& path, std::string_view value) -> void
{
    std::ofstream output{path, std::ios::binary | std::ios::trunc};
    output.write(value.data(), static_cast<std::streamsize>(value.size()));
}

auto expect(bool condition, std::string_view message) -> bool
{
    if (!condition)
    {
        std::cerr << "FAIL runtime_storage: " << message << '\n';
    }
    return condition;
}

auto root_has_only_active(const fs::path& root) -> bool
{
    auto count = std::size_t{};
    for (const auto& entry : fs::directory_iterator{root})
    {
        ++count;
        if (entry.path().filename() != "active")
        {
            return false;
        }
    }
    return count == 1;
}
} // namespace

auto main() -> int
{
    constexpr std::string_view Nonce{"0123456789abcdef0123456789abcdef"};
    bool passed = true;

    TemporaryTree reuse_tree{};
    auto v1 = make_package("v1");
    Win32RuntimeStorage reuse_storage{
        reuse_tree.root,
        v1.manifest_json,
        v1.manifest_sha256,
        v1.payload};
    const auto first =
        prepare_runtime(reuse_storage, v1.manifest_sha256, Nonce);
    passed &= expect(
        first && *first == RuntimePrepareResult::Published &&
            root_has_only_active(reuse_tree.root) &&
            !fs::exists(
                reuse_tree.root / "active" / "dwmapi.dll") &&
            v1.payload.read_count == 2U,
        "fresh publication extracted a loader file into active");

    fs::create_directories(reuse_tree.root / "active" / "Logs");
    write_bytes(
        reuse_tree.root / "active" / "Logs" / "runtime.log",
        "generated");
    const auto reads_before_reuse = v1.payload.read_count;
    const auto reused =
        prepare_runtime(reuse_storage, v1.manifest_sha256, Nonce);
    passed &= expect(
        reused && *reused == RuntimePrepareResult::Reused &&
            v1.payload.read_count == reads_before_reuse,
        "exact disk generation was not reused without extraction");

    write_bytes(
        reuse_tree.root / "active" / "Mods" / "MecchaCamouflage" /
            "dlls" / "main.dll",
        "tampered");
    const auto tampered =
        prepare_runtime(reuse_storage, v1.manifest_sha256, Nonce);
    passed &= expect(
        !tampered &&
            tampered.error().code ==
                RuntimeTransactionErrorCode::Conflict,
        "tampered immutable payload was repaired instead of refused");

    TemporaryTree update_tree{};
    auto old_package = make_package("old");
    Win32RuntimeStorage old_storage{
        update_tree.root,
        old_package.manifest_json,
        old_package.manifest_sha256,
        old_package.payload};
    passed &= expect(
        prepare_runtime(
            old_storage,
            old_package.manifest_sha256,
            Nonce).has_value(),
        "old disk generation could not be prepared");

    auto new_package = make_package("new");
    Win32RuntimeStorage new_storage{
        update_tree.root,
        new_package.manifest_json,
        new_package.manifest_sha256,
        new_package.payload};
    const auto updated = prepare_runtime(
        new_storage,
        new_package.manifest_sha256,
        Nonce);
    passed &= expect(
        updated && *updated == RuntimePrepareResult::Published &&
            root_has_only_active(update_tree.root),
        "disk update left rollback, staging, or journal");
    const auto updated_hash = sha256_file(
        update_tree.root / "active" / "UE4SS.dll");
    const auto expected_hash =
        sha256_bytes(new_package.payload.files.at("UE4SS.dll"));
    passed &= expect(
        updated_hash && expected_hash &&
            updated_hash->sha256 == *expected_hash,
        "disk update did not publish the new payload");

    write_bytes(update_tree.root / "active" / "unknown.bin", "unknown");
    const auto unknown =
        prepare_runtime(new_storage, new_package.manifest_sha256, Nonce);
    passed &= expect(
        !unknown &&
            unknown.error().code ==
                RuntimeTransactionErrorCode::Conflict,
        "unknown active content was removed or ignored");

    TemporaryTree partial_tree{};
    auto partial_package = make_package("partial");
    partial_package.payload.fail_on_read = 2;
    Win32RuntimeStorage partial_storage{
        partial_tree.root,
        partial_package.manifest_json,
        partial_package.manifest_sha256,
        partial_package.payload};
    const auto partial_failure = prepare_runtime(
        partial_storage,
        partial_package.manifest_sha256,
        Nonce);
    passed &= expect(
        !partial_failure && !root_has_only_active(partial_tree.root),
        "injected extraction failure did not leave recoverable staging");
    partial_package.payload.fail_on_read = 0;
    partial_package.payload.read_count = 0;
    const auto partial_recovery = prepare_runtime(
        partial_storage,
        partial_package.manifest_sha256,
        Nonce);
    passed &= expect(
        partial_recovery &&
            *partial_recovery == RuntimePrepareResult::Published &&
            root_has_only_active(partial_tree.root),
        "partial disk staging was not safely cleaned and retried");

    TemporaryTree unknown_root{};
    write_bytes(unknown_root.root / "unowned.txt", "unknown");
    auto unknown_package = make_package("unknown-root");
    Win32RuntimeStorage unknown_storage{
        unknown_root.root,
        unknown_package.manifest_json,
        unknown_package.manifest_sha256,
        unknown_package.payload};
    const auto unknown_root_result = prepare_runtime(
        unknown_storage,
        unknown_package.manifest_sha256,
        Nonce);
    passed &= expect(
        !unknown_root_result &&
            unknown_root_result.error().code ==
                RuntimeTransactionErrorCode::Conflict &&
            fs::exists(unknown_root.root / "unowned.txt"),
        "unknown runtime-root content was modified");

    if (passed)
    {
        std::cout << "PASS runtime_storage\n";
        return 0;
    }
    return 1;
}
