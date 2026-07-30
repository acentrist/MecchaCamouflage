#include <meccha/launcher/hash.hpp>
#include <meccha/launcher/loader_observation.hpp>
#include <meccha/launcher/managed_loader.hpp>
#include <meccha/launcher/manifest.hpp>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace
{
using namespace meccha::launcher;
namespace fs = std::filesystem;

class TemporaryTree
{
public:
    TemporaryTree()
    {
        static auto sequence = std::uint64_t{};
        ++sequence;
        root = fs::temp_directory_path() /
               ("meccha-v2-managed-loader-" +
                std::to_string(GetCurrentProcessId()) + "-" +
                std::to_string(GetTickCount64()) + "-" +
                std::to_string(sequence));
        fs::create_directories(root / "game");
        fs::create_directories(root / "active");
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

auto bytes(std::string_view value) -> std::vector<std::byte>
{
    const auto view = std::as_bytes(std::span{value});
    return {view.begin(), view.end()};
}

class MemoryPayload final : public RuntimePayloadSource
{
public:
    auto read_file(std::string_view relative_path)
        -> std::expected<std::vector<std::byte>, RuntimePayloadError> override
    {
        const auto found = files.find(std::string{relative_path});
        if (found == files.end())
        {
            return std::unexpected(
                RuntimePayloadError{"missing payload file"});
        }
        return found->second;
    }

    std::map<std::string, std::vector<std::byte>, std::less<>> files{};
};

struct Package
{
    PayloadManifest manifest{};
    Sha256Digest manifest_sha256{};
    MemoryPayload payload{};
};

auto make_package() -> Package
{
    Package package{};
    package.payload.files["dwmapi.dll"] = bytes("pinned-proxy");
    package.payload.files["UE4SS.dll"] = bytes("pinned-runtime");
    const auto proxy_hash =
        sha256_bytes(package.payload.files.at("dwmapi.dll")).value();
    const auto runtime_hash =
        sha256_bytes(package.payload.files.at("UE4SS.dll")).value();
    const auto manifest_json =
        std::string{
            R"json({"schema_version":1,"product_version":"2.0.0",)json"} +
        R"json("ue4ss_commit":"6c26f038751b3d96059d4a9148f5d093012d55ad",)json" +
        R"json("generated_paths":[],"files":[)json" +
        R"json({"path":"dwmapi.dll","role":"proxy","size":)json" +
        std::to_string(
            package.payload.files.at("dwmapi.dll").size()) +
        R"json(,"sha256":")json" + sha256_hex(proxy_hash) +
        R"json("},{"path":"UE4SS.dll","role":"runtime","size":)json" +
        std::to_string(
            package.payload.files.at("UE4SS.dll").size()) +
        R"json(,"sha256":")json" + sha256_hex(runtime_hash) +
        R"json("}]})json";
    package.manifest = parse_payload_manifest(manifest_json).value();
    package.manifest_sha256 = sha256_bytes(
        std::as_bytes(std::span{manifest_json})).value();
    return package;
}

auto write_bytes(const fs::path& path, std::string_view value) -> void
{
    std::ofstream output{path, std::ios::binary | std::ios::trunc};
    output.write(value.data(), static_cast<std::streamsize>(value.size()));
}

auto read_text(const fs::path& path) -> std::string
{
    std::ifstream input{path, std::ios::binary};
    return {
        std::istreambuf_iterator<char>{input},
        std::istreambuf_iterator<char>{}};
}

auto expect(bool condition, std::string_view message) -> bool
{
    if (!condition)
    {
        std::cerr << "FAIL managed_loader: " << message << '\n';
    }
    return condition;
}
} // namespace

auto main() -> int
{
    bool passed = true;
    auto package = make_package();

    TemporaryTree lifecycle{};
    const auto material = build_managed_loader_material(
        package.manifest,
        package.manifest_sha256,
        lifecycle.root / "active",
        package.payload);
    passed &= expect(
        material &&
            material->proxy.file.role == FileRole::Proxy &&
            material->override_file.file.role ==
                FileRole::Override &&
            !material->override_bytes.empty(),
        "managed loader material was not derived from the payload");

    const auto create_plan = ManagedLoaderPlan{
        ArtifactDisposition::CreateOwned,
        ArtifactDisposition::CreateOwned,
        false,
    };
    const auto created = material
                             ? apply_managed_loader_plan(
                                   create_plan,
                                   lifecycle.root / "game",
                                   lifecycle.root / "ownership",
                                   *material)
                             : std::expected<
                                   ManagedLoaderApplyResult,
                                   ManagedLoaderError>{
                                   std::unexpected(
                                       ManagedLoaderError{
                                           ManagedLoaderErrorCode::Payload,
                                           "missing material"})};
    passed &= expect(
        created &&
            created->proxy ==
                OwnedFileInstallResult::Created &&
            created->override_file ==
                OwnedFileInstallResult::Created &&
            read_text(
                lifecycle.root / "game" / "dwmapi.dll") ==
                "pinned-proxy" &&
            !read_text(
                 lifecycle.root / "game" / "override.txt")
                 .ends_with('\n'),
        "managed loader files were not created canonically");

    const auto stale_create = apply_managed_loader_plan(
        create_plan,
        lifecycle.root / "game",
        lifecycle.root / "ownership",
        *material);
    passed &= expect(
        !stale_create &&
            stale_create.error().code ==
                ManagedLoaderErrorCode::Plan,
        "a stale create plan was applied");

    const auto reuse_plan = ManagedLoaderPlan{
        ArtifactDisposition::ReuseOwned,
        ArtifactDisposition::ReuseOwned,
        false,
    };
    const auto reused = apply_managed_loader_plan(
        reuse_plan,
        lifecycle.root / "game",
        lifecycle.root / "ownership",
        *material);
    passed &= expect(
        reused && !reused->proxy && !reused->override_file,
        "exact loader reuse rewrote owned files");

    TemporaryTree preflight{};
    write_bytes(
        preflight.root / "game" / "override.txt",
        "unknown");
    const auto preflight_material = build_managed_loader_material(
        package.manifest,
        package.manifest_sha256,
        preflight.root / "active",
        package.payload);
    const auto refused_preflight = apply_managed_loader_plan(
        create_plan,
        preflight.root / "game",
        preflight.root / "ownership",
        *preflight_material);
    passed &= expect(
        !refused_preflight &&
            refused_preflight.error().code ==
                ManagedLoaderErrorCode::Plan &&
            !fs::exists(
                preflight.root / "game" / "dwmapi.dll") &&
            read_text(
                preflight.root / "game" / "override.txt") ==
                "unknown",
        "preflight conflict allowed a partial loader mutation");

    TemporaryTree exact_proxy{};
    write_bytes(
        exact_proxy.root / "game" / "dwmapi.dll",
        "pinned-proxy");
    const auto exact_material = build_managed_loader_material(
        package.manifest,
        package.manifest_sha256,
        exact_proxy.root / "active",
        package.payload);
    const auto exact_plan = ManagedLoaderPlan{
        ArtifactDisposition::ReuseUnowned,
        ArtifactDisposition::CreateOwned,
        false,
    };
    const auto exact_applied = apply_managed_loader_plan(
        exact_plan,
        exact_proxy.root / "game",
        exact_proxy.root / "ownership",
        *exact_material);
    passed &= expect(
        exact_applied && !exact_applied->proxy &&
            exact_applied->override_file ==
                OwnedFileInstallResult::Created &&
            !fs::exists(
                exact_proxy.root / "ownership" /
                "dwmapi.owner.json"),
        "exact unowned proxy was claimed or rewritten");

    TemporaryTree elevated{};
    const auto elevated_material = build_managed_loader_material(
        package.manifest,
        package.manifest_sha256,
        elevated.root / "active",
        package.payload);
    auto elevated_plan = create_plan;
    elevated_plan.elevated = true;
    const auto elevation_required = apply_managed_loader_plan(
        elevated_plan,
        elevated.root / "game",
        elevated.root / "ownership",
        *elevated_material);
    passed &= expect(
        !elevation_required &&
            elevation_required.error().code ==
                ManagedLoaderErrorCode::ElevationRequired &&
            fs::is_empty(elevated.root / "game"),
        "elevated plan mutated files before broker handoff");

    package.payload.files["dwmapi.dll"] = bytes("tampered");
    const auto bad_payload = build_managed_loader_material(
        package.manifest,
        package.manifest_sha256,
        elevated.root / "active",
        package.payload);
    passed &= expect(
        !bad_payload &&
            bad_payload.error().code ==
                ManagedLoaderErrorCode::Payload,
        "payload hash mismatch was accepted");

    TemporaryTree unicode{};
    const auto unicode_active =
        unicode.root / L"active-\u65e5\u672c\u8a9e";
    fs::create_directory(unicode_active);
    write_bytes(unicode_active / "UE4SS.dll", "runtime");
    auto unicode_package = make_package();
    const auto unicode_material = build_managed_loader_material(
        unicode_package.manifest,
        unicode_package.manifest_sha256,
        unicode_active,
        unicode_package.payload);
    if (unicode_material)
    {
        const auto generated = std::string{
            reinterpret_cast<const char*>(
                unicode_material->override_bytes.data()),
            unicode_material->override_bytes.size()};
        const auto parsed = parse_override_target(
            generated,
            unicode.root / "game");
        std::error_code equivalent_error{};
        passed &= expect(
            std::ranges::all_of(generated, [](char character) {
                const auto byte =
                    static_cast<unsigned char>(character);
                return byte >= 0x20U && byte <= 0x7eU;
            }) &&
                parsed &&
                fs::equivalent(
                    *parsed,
                    unicode_active / "UE4SS.dll",
                    equivalent_error) &&
                !equivalent_error,
            "Unicode runtime path did not use an equivalent ASCII "
            "short path");
    }
    else
    {
        passed &= expect(
            unicode_material.error().code ==
                ManagedLoaderErrorCode::PathEncoding,
            "unrepresentable Unicode runtime path did not fail "
            "closed");
    }

    if (passed)
    {
        std::cout << "PASS managed_loader\n";
    }
    return passed ? 0 : 1;
}
