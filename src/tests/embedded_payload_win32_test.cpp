#include <meccha/launcher/embedded_payload.hpp>
#include <meccha/launcher/embedded_package.hpp>
#include <meccha/launcher/hash.hpp>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <process.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
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
        static std::uint64_t sequence{};
        ++sequence;
        root = fs::temp_directory_path() /
               ("meccha-v2-embedded-payload-" +
                std::to_string(GetCurrentProcessId()) + "-" +
                std::to_string(GetTickCount64()) + "-" +
                std::to_string(sequence));
        fs::create_directories(root / "source");
        fs::create_directories(root / "scratch");
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

auto bytes(std::string_view text) -> std::vector<std::byte>
{
    const auto view = std::as_bytes(std::span{text});
    return {view.begin(), view.end()};
}

auto write_bytes(
    const fs::path& path,
    std::span<const std::byte> value) -> void
{
    fs::create_directories(path.parent_path());
    std::ofstream output{path, std::ios::binary | std::ios::trunc};
    output.write(
        reinterpret_cast<const char*>(value.data()),
        static_cast<std::streamsize>(value.size()));
}

auto read_bytes(const fs::path& path) -> std::vector<std::byte>
{
    std::ifstream input{path, std::ios::binary};
    const auto characters = std::vector<char>{
        std::istreambuf_iterator<char>{input},
        std::istreambuf_iterator<char>{}};
    auto result = std::vector<std::byte>{};
    result.reserve(characters.size());
    for (const auto character : characters)
    {
        result.push_back(static_cast<std::byte>(
            static_cast<unsigned char>(character)));
    }
    return result;
}

auto quoted(const fs::path& path) -> std::string
{
    const auto text = path.string();
    return '"' + text + '"';
}

auto build_fixture_cab(
    const fs::path& root,
    const fs::path& runtime,
    const fs::path& mod,
    const fs::path& enabled) -> fs::path
{
    const auto ddf = root / "fixture.ddf";
    std::ofstream output{ddf, std::ios::binary | std::ios::trunc};
    output
        << ".OPTION EXPLICIT\r\n"
        << ".Set Cabinet=on\r\n"
        << ".Set Compress=on\r\n"
        << ".Set CompressionType=LZX\r\n"
        << ".Set CabinetNameTemplate=payload.cab\r\n"
        << ".Set DiskDirectory1=" << quoted(root) << "\r\n"
        << ".Set RptFileName=nul\r\n"
        << ".Set InfFileName=nul\r\n"
        << ".Set DestinationDir=\r\n"
        << quoted(runtime) << " \"UE4SS.dll\"\r\n"
        << ".Set DestinationDir=\"Mods\\MecchaCamouflage\\dlls\"\r\n"
        << quoted(mod) << " \"main.dll\"\r\n"
        << ".Set DestinationDir=\"Mods\\MecchaCamouflage\"\r\n"
        << quoted(enabled) << " \"enabled.txt\"\r\n";
    output.close();

    const auto result = _wspawnlp(
        _P_WAIT,
        L"makecab.exe",
        L"makecab.exe",
        L"/V0",
        L"/F",
        ddf.c_str(),
        nullptr);
    if (result != 0)
    {
        return {};
    }
    return root / "payload.cab";
}

auto manifest_file(
    std::string path,
    FileRole role,
    std::span<const std::byte> value) -> ManifestFile
{
    const auto digest = sha256_bytes(value);
    return ManifestFile{
        std::move(path),
        role,
        value.size(),
        digest.value_or(Sha256Digest{}),
    };
}

auto expect(bool condition, std::string_view message) -> bool
{
    if (!condition)
    {
        std::cerr << "FAIL embedded_payload: " << message << '\n';
    }
    return condition;
}
} // namespace

int main()
{
    auto ok = true;

    const auto resource = read_current_module_rcdata(501);
    const auto expected_resource = bytes("meccha-embedded-resource-v2\n");
    ok &= expect(
        resource && *resource == expected_resource,
        "RCDATA bytes were not loaded exactly");
    const auto missing_resource = read_current_module_rcdata(65000);
    ok &= expect(
        !missing_resource,
        "a missing RCDATA resource was accepted");

    TemporaryTree tree{};
    const auto runtime_bytes = bytes("runtime");
    const auto mod_bytes = bytes("mod");
    const auto enabled_bytes = std::vector<std::byte>{};
    const auto runtime_path = tree.root / "source/UE4SS.dll";
    const auto mod_path = tree.root / "source/main.dll";
    const auto enabled_path = tree.root / "source/enabled.txt";
    write_bytes(runtime_path, runtime_bytes);
    write_bytes(mod_path, mod_bytes);
    write_bytes(enabled_path, enabled_bytes);
    const auto cab_path = build_fixture_cab(
        tree.root,
        runtime_path,
        mod_path,
        enabled_path);
    ok &= expect(
        !cab_path.empty() && fs::is_regular_file(cab_path),
        "MakeCab did not create the fixture");
    if (cab_path.empty() || !fs::is_regular_file(cab_path))
    {
        return 1;
    }

    auto manifest = PayloadManifest{
        1,
        "2.0.0",
        "6c26f038751b3d96059d4a9148f5d093012d55ad",
        {
            manifest_file(
                "UE4SS.dll",
                FileRole::Runtime,
                runtime_bytes),
            manifest_file(
                "Mods/MecchaCamouflage/dlls/main.dll",
                FileRole::Mod,
                mod_bytes),
            manifest_file(
                "Mods/MecchaCamouflage/enabled.txt",
                FileRole::Mod,
                enabled_bytes),
        },
        {"Logs"},
        runtime_bytes.size() + mod_bytes.size(),
    };
    const auto cab_bytes = read_bytes(cab_path);
    const auto entry = [](const ManifestFile& file)
    {
        return std::string{R"json({"path":")json"} +
               file.path + R"json(","role":")json" +
               (file.role == FileRole::Runtime
                    ? "runtime"
                    : "mod") +
               R"json(","size":)json" +
               std::to_string(file.size) +
               R"json(,"sha256":")json" +
               sha256_hex(file.sha256) + R"json("})json";
    };
    const auto manifest_json =
        std::string{
            R"json({"schema_version":1,"product_version":"2.0.0",)json"} +
        R"json("ue4ss_commit":"6c26f038751b3d96059d4a9148f5d093012d55ad",)json" +
        R"json("generated_paths":["Logs"],"files":[)json" +
        entry(manifest.files[0]) + "," +
        entry(manifest.files[1]) + "," +
        entry(manifest.files[2]) + "]}";
    const auto loaded_package =
        load_embedded_launcher_package(
            std::as_bytes(std::span{manifest_json}),
            cab_bytes,
            tree.root / "scratch");
    ok &= expect(
        loaded_package &&
            loaded_package->manifest == manifest &&
            loaded_package->manifest_json ==
                manifest_json &&
            loaded_package->manifest_sha256 ==
                sha256_bytes(
                    std::as_bytes(
                        std::span{manifest_json}))
                    .value() &&
            loaded_package->payload_source &&
            loaded_package->payload_source
                    ->read_file("UE4SS.dll") ==
                runtime_bytes,
        "verified embedded launcher package was not assembled");

    auto source = Win32CabPayloadSource::open(
        cab_bytes,
        manifest,
        tree.root / "scratch");
    ok &= expect(
        source.has_value(),
        source ? "" : source.error().detail);
    if (!source)
    {
        return 1;
    }
    const auto runtime = source->read_file("UE4SS.dll");
    const auto mod = source->read_file(
        "Mods/MecchaCamouflage/dlls/main.dll");
    const auto enabled = source->read_file(
        "Mods/MecchaCamouflage/enabled.txt");
    ok &= expect(
        runtime && *runtime == runtime_bytes,
        "runtime bytes did not round trip");
    ok &= expect(
        mod && *mod == mod_bytes,
        "nested mod bytes did not round trip");
    ok &= expect(
        enabled && enabled->empty(),
        "empty payload file did not round trip");
    ok &= expect(
        !source->read_file("../UE4SS.dll"),
        "a non-canonical payload lookup was accepted");
    ok &= expect(
        !source->read_file("unknown.dll"),
        "an undeclared payload lookup was accepted");
    ok &= expect(
        fs::is_empty(tree.root / "scratch"),
        "CAB extraction workspace accumulated");

    auto wrong_manifest = manifest;
    wrong_manifest.files.front().sha256.bytes.front() =
        static_cast<std::byte>(0xff);
    ok &= expect(
        !Win32CabPayloadSource::open(
            cab_bytes,
            wrong_manifest,
            tree.root / "scratch"),
        "payload bytes with the wrong manifest hash were accepted");

    auto missing_entry_manifest = manifest;
    missing_entry_manifest.files.pop_back();
    ok &= expect(
        !Win32CabPayloadSource::open(
            cab_bytes,
            missing_entry_manifest,
            tree.root / "scratch"),
        "a CAB containing an undeclared file was accepted");

    auto duplicate_manifest = manifest;
    duplicate_manifest.files.push_back(
        duplicate_manifest.files.front());
    duplicate_manifest.total_size +=
        duplicate_manifest.files.back().size;
    ok &= expect(
        !Win32CabPayloadSource::open(
            cab_bytes,
            duplicate_manifest,
            tree.root / "scratch"),
        "a duplicate manifest path was accepted");

    auto truncated = cab_bytes;
    truncated.resize(truncated.size() / 2U);
    ok &= expect(
        !Win32CabPayloadSource::open(
            truncated,
            manifest,
            tree.root / "scratch"),
        "a truncated CAB was accepted");
    ok &= expect(
        fs::is_empty(tree.root / "scratch"),
        "failed CAB extraction accumulated a workspace");

    return ok ? 0 : 1;
}
