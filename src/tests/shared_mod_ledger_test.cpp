#include "shared_mod_ledger.hpp"

#include <meccha/launcher/hash.hpp>

#include <algorithm>
#include <cstddef>
#include <iostream>
#include <string>
#include <string_view>

namespace
{
using namespace meccha::launcher;
using namespace meccha::launcher::detail;

auto digest(std::string_view value) -> Sha256Digest
{
    Sha256Digest result{};
    for (std::size_t index = 0;
         index < result.bytes.size();
         ++index)
    {
        const auto value_byte = value.empty()
                                    ? 0U
                                    : static_cast<unsigned char>(
                                          value[index % value.size()]);
        result.bytes[index] =
            static_cast<std::byte>(value_byte ^ index);
    }
    return result;
}

auto record(
    std::string version,
    std::string path,
    std::string_view content) -> OwnershipRecord
{
    return OwnershipRecord{
        version,
        digest(version),
        ManifestFile{
            std::move(path),
            FileRole::Mod,
            content.size(),
            digest(content),
        },
    };
}

auto find_record(
    const SharedModLedger& ledger,
    std::string_view path) -> const OwnershipRecord*
{
    const auto found = std::ranges::find_if(
        ledger.files,
        [path](const OwnershipRecord& candidate) {
            return candidate.file.path == path;
        });
    return found == ledger.files.end()
               ? nullptr
               : &*found;
}

auto expect(bool condition, std::string_view message) -> bool
{
    if (!condition)
    {
        std::cerr << "FAIL shared_mod_ledger: " << message << '\n';
    }
    return condition;
}
} // namespace

auto main() -> int
{
    bool passed = true;
    const auto old_main = record(
        "2.0.0",
        "Mods/MecchaCamouflage/dlls/main.dll",
        "old-main");
    const auto removed_later = record(
        "2.0.0",
        "Mods/MecchaCamouflage/resources/old.bin",
        "old");
    const auto new_main = record(
        "2.1.0",
        "Mods/MecchaCamouflage/dlls/main.dll",
        "new-main");
    const auto added = record(
        "2.1.0",
        "Mods/MecchaCamouflage/resources/added.bin",
        "added");
    const auto installed = SharedModLedger{
        "2.0.0",
        digest("2.0.0"),
        {old_main, removed_later},
    };
    const auto current = SharedModLedger{
        "2.1.0",
        digest("2.1.0"),
        {new_main, added},
    };
    const auto transition =
        make_shared_mod_transition_ledger(
            installed,
            current);
    const auto transition_main = find_record(
        transition,
        "Mods/MecchaCamouflage/dlls/main.dll");
    passed &= expect(
        transition.product_version == "2.1.0" &&
            transition.manifest_sha256 == digest("2.1.0") &&
            transition.files.size() == 3 &&
            transition_main &&
            transition_main->product_version == "2.1.0" &&
            find_record(
                transition,
                "Mods/MecchaCamouflage/resources/old.bin") &&
            find_record(
                transition,
                "Mods/MecchaCamouflage/resources/added.bin"),
        "transition did not retain the old/new ownership union");

    const auto serialized =
        serialize_shared_mod_ledger(transition);
    const auto parsed = serialized
                            ? parse_shared_mod_ledger(*serialized)
                            : std::expected<
                                  SharedModLedger,
                                  SharedModError>{
                                  std::unexpected(
                                      serialized.error())};
    const auto parsed_old = parsed
                                ? find_record(
                                      *parsed,
                                      "Mods/MecchaCamouflage/"
                                      "resources/old.bin")
                                : nullptr;
    passed &= expect(
        parsed && parsed->files.size() == 3 && parsed_old &&
            parsed_old->product_version == "2.0.0" &&
            parsed_old->manifest_sha256 == digest("2.0.0"),
        "ledger round trip lost per-file ownership identity");

    auto reordered = transition;
    std::ranges::reverse(reordered.files);
    const auto reordered_json =
        serialize_shared_mod_ledger(reordered);
    passed &= expect(
        serialized && reordered_json &&
            *serialized == *reordered_json,
        "ledger serialization depends on input ordering");

    auto duplicate = transition;
    duplicate.files.push_back(new_main);
    passed &= expect(
        !serialize_shared_mod_ledger(duplicate),
        "ledger accepted a case-insensitive duplicate path");

    const auto malformed = parse_shared_mod_ledger(
        R"({"schema_version":1})");
    passed &= expect(
        !malformed,
        "ledger accepted missing strict fields");

    if (passed)
    {
        std::cout << "PASS shared_mod_ledger\n";
    }
    return passed ? 0 : 1;
}
