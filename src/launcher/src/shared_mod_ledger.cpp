#include "shared_mod_ledger.hpp"

#include <meccha/build_identity.hpp>
#include <meccha/launcher/hash.hpp>

#include <glaze/glaze.hpp>

#include <algorithm>
#include <cstdint>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace meccha::launcher::detail
{
struct RawSharedModLedgerFile
{
    std::string path{};
    std::string role{};
    std::uint64_t size{};
    std::string sha256{};
};

struct RawSharedModLedger
{
    std::uint32_t schema_version{};
    std::string product{};
    std::string product_version{};
    std::string manifest_sha256{};
    std::vector<RawSharedModLedgerFile> files{};
};

namespace
{
constexpr std::size_t MaximumLedgerBytes = 4U * 1024U * 1024U;
constexpr std::size_t MaximumLedgerFiles = 4096U;
constexpr std::string_view ModPrefix{"Mods/MecchaCamouflage/"};

auto error(std::string detail)
    -> std::unexpected<SharedModError>
{
    return std::unexpected(SharedModError{
        SharedModErrorCode::Manifest,
        std::move(detail),
    });
}

auto valid_file(const ManifestFile& file) -> bool
{
    return file.role == FileRole::Mod &&
           file.path.starts_with(ModPrefix) &&
           is_canonical_payload_path(file.path);
}
} // namespace

auto serialize_shared_mod_ledger(
    const SharedModLedger& ledger)
    -> std::expected<std::string, SharedModError>
{
    if (ledger.product_version.empty() ||
        ledger.product_version.size() > 128U ||
        ledger.files.empty() ||
        ledger.files.size() > MaximumLedgerFiles)
    {
        return error("Shared mod ledger identity is invalid.");
    }

    auto files = ledger.files;
    std::ranges::sort(
        files,
        {},
        [](const ManifestFile& file) {
            return canonical_payload_path_key(file.path);
        });
    RawSharedModLedger raw{
        build::SharedModLedgerSchemaVersion,
        std::string{build::ProductName},
        ledger.product_version,
        sha256_hex(ledger.manifest_sha256),
        {},
    };
    raw.files.reserve(files.size());
    std::string previous_key{};
    for (const auto& file : files)
    {
        if (!valid_file(file))
        {
            return error(
                "Shared mod ledger contains an invalid file.");
        }
        const auto key = canonical_payload_path_key(file.path);
        if (key == previous_key)
        {
            return error(
                "Shared mod ledger contains a duplicate file.");
        }
        previous_key = key;
        raw.files.push_back(RawSharedModLedgerFile{
            file.path,
            "mod",
            file.size,
            sha256_hex(file.sha256),
        });
    }

    auto json = glz::write_json(raw);
    if (!json || json->size() > MaximumLedgerBytes)
    {
        return error("Could not serialize the shared mod ledger.");
    }
    return *json;
}

auto parse_shared_mod_ledger(
    std::string_view json)
    -> std::expected<SharedModLedger, SharedModError>
{
    if (json.empty() || json.size() > MaximumLedgerBytes)
    {
        return error("Shared mod ledger size is invalid.");
    }
    RawSharedModLedger raw{};
    constexpr auto StrictJson = glz::opts{
        .error_on_unknown_keys = true,
        .error_on_missing_keys = true,
    };
    const auto parsed = glz::read<StrictJson>(raw, json);
    const auto manifest_sha256 =
        parse_sha256_hex(raw.manifest_sha256);
    if (parsed ||
        raw.schema_version !=
            build::SharedModLedgerSchemaVersion ||
        raw.product != build::ProductName ||
        raw.product_version.empty() ||
        raw.product_version.size() > 128U ||
        !manifest_sha256 || raw.files.empty() ||
        raw.files.size() > MaximumLedgerFiles)
    {
        return error("Shared mod ledger is malformed.");
    }

    SharedModLedger ledger{
        std::move(raw.product_version),
        *manifest_sha256,
        {},
    };
    ledger.files.reserve(raw.files.size());
    std::unordered_set<std::string> unique_paths{};
    unique_paths.reserve(raw.files.size());
    for (auto& file : raw.files)
    {
        const auto digest = parse_sha256_hex(file.sha256);
        const auto key = canonical_payload_path_key(file.path);
        if (file.role != "mod" || !digest ||
            !file.path.starts_with(ModPrefix) ||
            !is_canonical_payload_path(file.path) ||
            !unique_paths.insert(key).second)
        {
            return error(
                "Shared mod ledger contains an invalid file.");
        }
        ledger.files.push_back(ManifestFile{
            std::move(file.path),
            FileRole::Mod,
            file.size,
            *digest,
        });
    }
    return ledger;
}
} // namespace meccha::launcher::detail
