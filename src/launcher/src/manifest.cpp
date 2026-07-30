#include <meccha/launcher/manifest.hpp>

#include <meccha/build_identity.hpp>

#include <glaze/glaze.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <limits>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>

namespace meccha::launcher
{
namespace detail
{
struct RawManifestFile
{
    std::string path{};
    std::string role{};
    std::uint64_t size{};
    std::string sha256{};
};

struct RawPayloadManifest
{
    std::uint32_t schema_version{};
    std::string product_version{};
    std::string ue4ss_commit{};
    std::vector<RawManifestFile> files{};
};
} // namespace detail

namespace
{
constexpr std::size_t MaximumManifestBytes = 4U * 1024U * 1024U;
constexpr std::size_t MaximumManifestFiles = 4096U;
constexpr std::size_t MaximumRelativePathBytes = 1024U;

auto error(ManifestErrorCode code, std::string detail)
    -> std::unexpected<ManifestError>
{
    return std::unexpected(ManifestError{code, std::move(detail)});
}

auto parse_role(std::string_view value) -> std::optional<FileRole>
{
    if (value == "proxy")
    {
        return FileRole::Proxy;
    }
    if (value == "override")
    {
        return FileRole::Override;
    }
    if (value == "runtime")
    {
        return FileRole::Runtime;
    }
    if (value == "mod")
    {
        return FileRole::Mod;
    }
    if (value == "config")
    {
        return FileRole::Config;
    }
    if (value == "profile")
    {
        return FileRole::Profile;
    }
    if (value == "localization")
    {
        return FileRole::Localization;
    }
    if (value == "font")
    {
        return FileRole::Font;
    }
    if (value == "license")
    {
        return FileRole::License;
    }
    return std::nullopt;
}

auto hex_nibble(char value) -> std::optional<std::byte>
{
    if (value >= '0' && value <= '9')
    {
        return static_cast<std::byte>(value - '0');
    }
    if (value >= 'a' && value <= 'f')
    {
        return static_cast<std::byte>(value - 'a' + 10);
    }
    return std::nullopt;
}

auto parse_sha256(std::string_view value) -> std::optional<Sha256Digest>
{
    if (value.size() != 64)
    {
        return std::nullopt;
    }

    Sha256Digest digest{};
    for (std::size_t index = 0; index < digest.bytes.size(); ++index)
    {
        const auto high = hex_nibble(value[index * 2]);
        const auto low = hex_nibble(value[index * 2 + 1]);
        if (!high || !low)
        {
            return std::nullopt;
        }
        digest.bytes[index] = (*high << 4) | *low;
    }
    return digest;
}

auto segment_is_windows_safe(std::string_view segment) -> bool
{
    if (segment.empty() || segment == "." || segment == ".." ||
        segment.front() == ' ' || segment.back() == ' ' || segment.back() == '.' ||
        segment.find_first_of("<>\"|?*") != std::string_view::npos)
    {
        return false;
    }

    const auto extension = segment.find('.');
    const auto stem = segment.substr(0, extension);
    std::string upper_stem{stem};
    std::ranges::transform(upper_stem, upper_stem.begin(), [](char value) {
        return static_cast<char>(std::toupper(static_cast<unsigned char>(value)));
    });
    constexpr std::array<std::string_view, 6> fixed_devices{
        "CON",
        "PRN",
        "AUX",
        "NUL",
        "CONIN$",
        "CONOUT$",
    };
    if (std::ranges::find(fixed_devices, upper_stem) != fixed_devices.end())
    {
        return false;
    }
    if (upper_stem.size() == 4 &&
        (upper_stem.starts_with("COM") || upper_stem.starts_with("LPT")) &&
        upper_stem[3] >= '1' && upper_stem[3] <= '9')
    {
        return false;
    }
    return true;
}

auto path_is_canonical(std::string_view path) -> bool
{
    if (path.empty() || path.size() > MaximumRelativePathBytes ||
        path.front() == '/' || path.back() == '/')
    {
        return false;
    }
    if (path.find('\\') != std::string_view::npos ||
        path.find(':') != std::string_view::npos ||
        std::any_of(path.begin(), path.end(), [](char value) {
            const auto byte = static_cast<unsigned char>(value);
            return byte < 0x20U || byte > 0x7EU;
        }))
    {
        return false;
    }

    std::size_t start = 0;
    while (start < path.size())
    {
        const auto end = path.find('/', start);
        const auto segment = path.substr(
            start,
            end == std::string_view::npos ? path.size() - start : end - start);
        if (!segment_is_windows_safe(segment))
        {
            return false;
        }
        if (end == std::string_view::npos)
        {
            break;
        }
        start = end + 1;
    }
    return true;
}

auto canonical_path_key(std::string_view path) -> std::string
{
    std::string key{path};
    std::ranges::transform(key, key.begin(), [](char value) {
        return static_cast<char>(std::tolower(static_cast<unsigned char>(value)));
    });
    return key;
}
} // namespace

auto parse_payload_manifest(std::string_view json)
    -> std::expected<PayloadManifest, ManifestError>
{
    if (json.empty() || json.size() > MaximumManifestBytes)
    {
        return error(ManifestErrorCode::Json, "Manifest size is outside the supported range.");
    }

    detail::RawPayloadManifest raw{};
    constexpr auto StrictJson = glz::opts{
        .error_on_unknown_keys = true,
        .error_on_missing_keys = true,
    };
    const auto parse_error = glz::read<StrictJson>(raw, json);
    if (parse_error)
    {
        return error(ManifestErrorCode::Json, glz::format_error(parse_error, json));
    }
    if (raw.schema_version != build::PayloadManifestSchemaVersion)
    {
        return error(ManifestErrorCode::Schema, "Unsupported payload manifest schema.");
    }
    if (raw.product_version != build::ProductVersion)
    {
        return error(ManifestErrorCode::ProductVersion, "Payload product version does not match the launcher.");
    }
    if (raw.ue4ss_commit != build::Ue4ssCommit)
    {
        return error(ManifestErrorCode::Ue4ssCommit, "Payload UE4SS commit does not match the launcher.");
    }
    if (raw.files.empty() || raw.files.size() > MaximumManifestFiles)
    {
        return error(ManifestErrorCode::FileCount, "Payload manifest file count is outside the supported range.");
    }

    PayloadManifest manifest{
        raw.schema_version,
        std::move(raw.product_version),
        std::move(raw.ue4ss_commit),
        {},
        0,
    };
    manifest.files.reserve(raw.files.size());
    std::unordered_set<std::string> unique_paths{};
    unique_paths.reserve(raw.files.size());
    for (auto& raw_file : raw.files)
    {
        if (!path_is_canonical(raw_file.path))
        {
            return error(ManifestErrorCode::Path, "Payload path is not canonical: " + raw_file.path);
        }
        const auto role = parse_role(raw_file.role);
        if (!role)
        {
            return error(ManifestErrorCode::Role, "Unknown payload role: " + raw_file.role);
        }
        const auto digest = parse_sha256(raw_file.sha256);
        if (!digest)
        {
            return error(ManifestErrorCode::Hash, "Payload SHA-256 is not canonical lowercase hex.");
        }
        if (!unique_paths.insert(canonical_path_key(raw_file.path)).second)
        {
            return error(ManifestErrorCode::Path, "Payload path is duplicated: " + raw_file.path);
        }
        if (raw_file.size > std::numeric_limits<std::uint64_t>::max() - manifest.total_size)
        {
            return error(ManifestErrorCode::TotalSize, "Payload size exceeds the supported range.");
        }
        manifest.total_size += raw_file.size;
        manifest.files.push_back(
            ManifestFile{
                std::move(raw_file.path),
                *role,
                raw_file.size,
                *digest,
            });
    }
    return manifest;
}
} // namespace meccha::launcher
