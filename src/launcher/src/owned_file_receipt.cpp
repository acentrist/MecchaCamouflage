#include "owned_file_receipt.hpp"

#include <meccha/build_identity.hpp>
#include <meccha/launcher/hash.hpp>

#include <glaze/glaze.hpp>

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace meccha::launcher::detail
{
struct RawOwnedFile
{
    std::string path{};
    std::string role{};
    std::uint64_t size{};
    std::string sha256{};
};

struct RawOwnershipRecord
{
    std::string product_version{};
    std::string manifest_sha256{};
    RawOwnedFile file{};
};

struct RawOwnedFileReceipt
{
    std::uint32_t schema_version{};
    std::string product{};
    std::string phase{};
    RawOwnershipRecord next{};
    std::optional<RawOwnershipRecord> previous{};
};

namespace
{
constexpr std::size_t MaximumReceiptBytes = 64U * 1024U;

auto error(std::string detail)
    -> std::unexpected<OwnedFileStoreError>
{
    return std::unexpected(OwnedFileStoreError{
        OwnedFileStoreErrorCode::InvalidData,
        std::move(detail),
    });
}

auto role_name(FileRole role) -> std::string_view
{
    switch (role)
    {
    case FileRole::Proxy:
        return "proxy";
    case FileRole::Override:
        return "override";
    case FileRole::Runtime:
        return "runtime";
    case FileRole::Mod:
        return "mod";
    case FileRole::Config:
        return "config";
    case FileRole::Profile:
        return "profile";
    case FileRole::Localization:
        return "localization";
    case FileRole::Font:
        return "font";
    case FileRole::License:
        return "license";
    }
    return {};
}

auto parse_role(std::string_view role) -> std::optional<FileRole>
{
    constexpr std::array roles{
        FileRole::Proxy,
        FileRole::Override,
        FileRole::Runtime,
        FileRole::Mod,
        FileRole::Config,
        FileRole::Profile,
        FileRole::Localization,
        FileRole::Font,
        FileRole::License,
    };
    for (const auto candidate : roles)
    {
        if (role == role_name(candidate))
        {
            return candidate;
        }
    }
    return std::nullopt;
}

auto to_raw(const OwnershipRecord& record) -> RawOwnershipRecord
{
    return RawOwnershipRecord{
        record.product_version,
        sha256_hex(record.manifest_sha256),
        RawOwnedFile{
            record.file.path,
            std::string{role_name(record.file.role)},
            record.file.size,
            sha256_hex(record.file.sha256),
        },
    };
}

auto parse_record(
    const RawOwnershipRecord& raw,
    std::string_view manifest_path,
    FileRole role)
    -> std::expected<OwnershipRecord, OwnedFileStoreError>
{
    const auto manifest_sha256 =
        parse_sha256_hex(raw.manifest_sha256);
    const auto file_sha256 = parse_sha256_hex(raw.file.sha256);
    const auto parsed_role = parse_role(raw.file.role);
    if (raw.product_version.empty() ||
        raw.product_version.size() > 128U ||
        raw.file.path != manifest_path || !parsed_role ||
        *parsed_role != role || !manifest_sha256 || !file_sha256)
    {
        return error("Owned-file receipt identity is invalid.");
    }
    return OwnershipRecord{
        raw.product_version,
        *manifest_sha256,
        ManifestFile{
            raw.file.path,
            *parsed_role,
            raw.file.size,
            *file_sha256,
        },
    };
}
} // namespace

auto serialize_owned_file_receipt(
    const OwnedFileReceipt& receipt)
    -> std::expected<std::string, OwnedFileStoreError>
{
    RawOwnedFileReceipt raw{
        build::RuntimeOwnershipSchemaVersion,
        std::string{build::ProductName},
        receipt.phase == OwnedFileReceiptPhase::Installing
            ? "installing"
            : receipt.phase == OwnedFileReceiptPhase::Complete
                  ? "complete"
                  : "removing",
        to_raw(receipt.next),
        std::nullopt,
    };
    if (receipt.previous)
    {
        raw.previous = to_raw(*receipt.previous);
    }
    auto json = glz::write_json(raw);
    if (!json)
    {
        return error(
            "Could not serialize an owned-file receipt.");
    }
    return *json;
}

auto parse_owned_file_receipt(
    std::string_view json,
    std::string_view manifest_path,
    FileRole role)
    -> std::expected<OwnedFileReceipt, OwnedFileStoreError>
{
    if (json.empty() || json.size() > MaximumReceiptBytes)
    {
        return error("Owned-file receipt size is invalid.");
    }
    RawOwnedFileReceipt raw{};
    constexpr auto StrictJson = glz::opts{
        .error_on_unknown_keys = true,
        .error_on_missing_keys = true,
    };
    const auto parsed = glz::read<StrictJson>(raw, json);
    if (parsed ||
        raw.schema_version !=
            build::RuntimeOwnershipSchemaVersion ||
        raw.product != build::ProductName)
    {
        return error("Owned-file receipt is malformed.");
    }

    OwnedFileReceiptPhase phase{};
    if (raw.phase == "installing")
    {
        phase = OwnedFileReceiptPhase::Installing;
    }
    else if (raw.phase == "complete")
    {
        phase = OwnedFileReceiptPhase::Complete;
    }
    else if (raw.phase == "removing")
    {
        phase = OwnedFileReceiptPhase::Removing;
    }
    else
    {
        return error("Owned-file receipt phase is invalid.");
    }

    auto next = parse_record(raw.next, manifest_path, role);
    if (!next)
    {
        return std::unexpected(next.error());
    }
    std::optional<OwnershipRecord> previous{};
    if (raw.previous)
    {
        auto parsed_previous =
            parse_record(*raw.previous, manifest_path, role);
        if (!parsed_previous)
        {
            return std::unexpected(parsed_previous.error());
        }
        previous = std::move(*parsed_previous);
    }
    if (phase != OwnedFileReceiptPhase::Installing && previous)
    {
        return error(
            "Only an installing receipt may contain previous "
            "ownership.");
    }
    return OwnedFileReceipt{
        phase,
        std::move(*next),
        std::move(previous),
    };
}
} // namespace meccha::launcher::detail
