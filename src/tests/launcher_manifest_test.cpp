#include <meccha/launcher/manifest.hpp>

#ifdef _WIN32
#include <meccha/launcher/hash.hpp>
#endif

#include <array>
#include <cstddef>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace
{
constexpr std::string_view ValidManifest = R"json({
  "schema_version": 1,
  "product_version": "2.0.0",
  "ue4ss_commit": "6c26f038751b3d96059d4a9148f5d093012d55ad",
  "files": [
    {
      "path": "Mods/MecchaCamouflage/dlls/main.dll",
      "role": "mod",
      "size": 4096,
      "sha256": "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
    },
    {
      "path": "UE4SS.dll",
      "role": "runtime",
      "size": 8192,
      "sha256": "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"
    }
  ]
})json";

auto expect(bool condition, std::string_view message) -> bool
{
    if (!condition)
    {
        std::cerr << "FAIL launcher_manifest: " << message << '\n';
    }
    return condition;
}

auto manifest_with_second_path(std::string_view path) -> std::string
{
    std::string manifest{ValidManifest};
    constexpr std::string_view original{"UE4SS.dll"};
    const auto position = manifest.find(original);
    manifest.replace(position, original.size(), path);
    return manifest;
}

auto manifest_with_replacement(
    std::string_view original,
    std::string_view replacement) -> std::string
{
    std::string manifest{ValidManifest};
    const auto position = manifest.find(original);
    if (position == std::string::npos)
    {
        return {};
    }
    manifest.replace(position, original.size(), replacement);
    return manifest;
}

auto expect_error(
    std::string_view name,
    std::string manifest,
    meccha::launcher::ManifestErrorCode expected) -> bool
{
    const auto result = meccha::launcher::parse_payload_manifest(manifest);
    if (!expect(!result.has_value(), std::string{name} + " was accepted"))
    {
        return false;
    }
    return expect(
        result.error().code == expected,
        std::string{name} + " reported the wrong error");
}
} // namespace

auto main() -> int
{
    const auto result = meccha::launcher::parse_payload_manifest(ValidManifest);
    bool passed = true;
    passed &= expect(result.has_value(), "valid manifest was rejected");
    if (result)
    {
        passed &= expect(result->files.size() == 2, "file count changed");
        passed &= expect(result->files[0].path == "Mods/MecchaCamouflage/dlls/main.dll",
                         "canonical path changed");
        passed &= expect(result->files[0].role == meccha::launcher::FileRole::Mod,
                         "mod role changed");
        passed &= expect(result->files[1].role == meccha::launcher::FileRole::Runtime,
                         "runtime role changed");
        passed &= expect(result->total_size == 12'288, "total payload size changed");
    }

    passed &= expect_error(
        "malformed JSON",
        "{",
        meccha::launcher::ManifestErrorCode::Json);
    passed &= expect_error(
        "unknown JSON key",
        manifest_with_replacement(
            "\"files\": [",
            "\"unexpected\": true,\n  \"files\": ["),
        meccha::launcher::ManifestErrorCode::Json);
    passed &= expect_error(
        "missing JSON key",
        manifest_with_replacement("      \"role\": \"mod\",\n", ""),
        meccha::launcher::ManifestErrorCode::Json);
    passed &= expect_error(
        "unknown schema",
        manifest_with_replacement("\"schema_version\": 1", "\"schema_version\": 2"),
        meccha::launcher::ManifestErrorCode::Schema);
    passed &= expect_error(
        "product version mismatch",
        manifest_with_replacement(
            "\"product_version\": \"2.0.0\"",
            "\"product_version\": \"2.0.1\""),
        meccha::launcher::ManifestErrorCode::ProductVersion);
    passed &= expect_error(
        "UE4SS commit mismatch",
        manifest_with_replacement(
            "6c26f038751b3d96059d4a9148f5d093012d55ad",
            "7c26f038751b3d96059d4a9148f5d093012d55ad"),
        meccha::launcher::ManifestErrorCode::Ue4ssCommit);
    passed &= expect_error(
        "unknown role",
        manifest_with_replacement("\"role\": \"mod\"", "\"role\": \"unknown\""),
        meccha::launcher::ManifestErrorCode::Role);
    passed &= expect_error(
        "non-canonical SHA-256",
        manifest_with_replacement(
            "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
            "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"),
        meccha::launcher::ManifestErrorCode::Hash);
    auto overflowing_manifest = manifest_with_replacement(
        "\"size\": 4096",
        "\"size\": 18446744073709551615");
    const auto second_size = overflowing_manifest.find("\"size\": 8192");
    overflowing_manifest.replace(
        second_size,
        std::string_view{"\"size\": 8192"}.size(),
        "\"size\": 1");
    passed &= expect_error(
        "overflowing total size",
        std::move(overflowing_manifest),
        meccha::launcher::ManifestErrorCode::TotalSize);

    const auto duplicate = meccha::launcher::parse_payload_manifest(
        manifest_with_second_path("mods/mecchacamouflage/DLLS/MAIN.DLL"));
    passed &= expect(!duplicate.has_value(),
                     "case-insensitive duplicate path was accepted");
    if (!duplicate)
    {
        passed &= expect(duplicate.error().code == meccha::launcher::ManifestErrorCode::Path,
                         "duplicate path did not report a path error");
    }

    constexpr std::array<std::string_view, 11> hostile_paths{
        "../escape.dll",
        "/absolute.dll",
        "C:/drive.dll",
        "Mods\\backslash.dll",
        "Mods//empty.dll",
        "Mods/./current.dll",
        "Mods/../parent.dll",
        "Mods/file.dll:stream",
        "Mods/CON/file.dll",
        "Mods/trailing./file.dll",
        "Mods/trailing /file.dll",
    };
    for (const auto path : hostile_paths)
    {
        const auto hostile = meccha::launcher::parse_payload_manifest(
            manifest_with_second_path(path));
        passed &= expect(!hostile.has_value(),
                         std::string{"hostile path was accepted: "} + std::string{path});
        if (!hostile)
        {
            passed &= expect(hostile.error().code == meccha::launcher::ManifestErrorCode::Path,
                             "hostile path did not report a path error");
        }
    }

#ifdef _WIN32
    const auto empty_hash = meccha::launcher::sha256_bytes({});
    passed &= expect(empty_hash.has_value(), "Windows SHA-256 rejected empty input");
    if (empty_hash)
    {
        passed &= expect(
            meccha::launcher::sha256_hex(*empty_hash) ==
                "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
            "Windows SHA-256 empty vector changed");
    }

    constexpr std::array<std::byte, 3> abc{
        std::byte{'a'},
        std::byte{'b'},
        std::byte{'c'},
    };
    const auto abc_hash = meccha::launcher::sha256_bytes(abc);
    passed &= expect(abc_hash.has_value(), "Windows SHA-256 rejected abc input");
    if (abc_hash)
    {
        passed &= expect(
            meccha::launcher::sha256_hex(*abc_hash) ==
                "ba7816bf8f01cfea414140de5dae2223"
                "b00361a396177a9cb410ff61f20015ad",
            "Windows SHA-256 abc vector changed");
    }
#else
    const auto unsupported_hash = meccha::launcher::sha256_bytes({});
    passed &= expect(!unsupported_hash.has_value(), "non-Windows hashing was accepted");
    if (!unsupported_hash)
    {
        passed &= expect(
            unsupported_hash.error().code ==
                meccha::launcher::HashErrorCode::PlatformUnsupported,
            "non-Windows hashing reported the wrong error");
    }
#endif

    if (passed)
    {
        std::cout << "PASS launcher_manifest\n";
        return 0;
    }
    return 1;
}
