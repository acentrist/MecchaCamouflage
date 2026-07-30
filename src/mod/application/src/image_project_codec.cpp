#include <meccha/application/image_project_codec.hpp>

#include "json_domain_meta.hpp"
#include "strict_json.hpp"

#include <glaze/glaze.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <memory>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace meccha::application
{
namespace preset_detail
{
constexpr std::array<std::byte, 8> V2Magic{
    std::byte{'M'},
    std::byte{'C'},
    std::byte{'V'},
    std::byte{'2'},
    std::byte{'P'},
    std::byte{'R'},
    std::byte{'0'},
    std::byte{'1'},
};
constexpr std::array<std::byte, 8> V1Magic{
    std::byte{'M'},
    std::byte{'C'},
    std::byte{'I'},
    std::byte{'P'},
    std::byte{'R'},
    std::byte{'S'},
    std::byte{'T'},
    std::byte{'1'},
};
constexpr std::uint8_t AtlasRole = 1U;
constexpr std::uint8_t SourceRole = 2U;
constexpr std::size_t MaximumEntryNameBytes = 96U;
constexpr std::uint32_t AtlasWidth = 1024U;
constexpr std::uint32_t AtlasHeight = 512U;
constexpr std::uint32_t AtlasChannels = 4U;
constexpr std::size_t HeaderByteLength = 56U;

struct ManifestLayer
{
    std::string asset_id{};
    std::string file_name{};
    std::string mime{};
    std::uint64_t source_bytes{};
    double center_x{0.5};
    double center_y{0.5};
    double width{1.0};
    double height{1.0};
    core::NormalizedCrop crop{};
    bool wrap_atlas_seam{};
    bool mirror_front_back{};
};

struct ManifestSource
{
    std::string asset_id{};
    std::string mime{};
    std::uint64_t byte_length{};
    std::string path{};
    std::string sha256{};
};

struct ManifestAtlas
{
    std::uint32_t width{};
    std::uint32_t height{};
    std::uint32_t channels{};
    std::string encoding{};
    std::uint64_t byte_length{};
    std::string path{};
    std::string sha256{};
};

struct ProjectManifest
{
    std::uint32_t schema_version{};
    std::string project_id{};
    std::string display_name{};
    std::uint64_t revision{};
    core::ImageProjectSettings settings{};
    std::vector<ManifestLayer> layers{};
    std::vector<ManifestSource> sources{};
    ManifestAtlas atlas{};
};

struct ContainerEntry
{
    std::string name{};
    std::uint8_t role{};
    std::uint64_t byte_length{};
    common::Sha256Digest digest{};
    std::shared_ptr<const std::vector<std::byte>> bytes{};
    bool consumed{};
};

auto codec_error(
    ImageProjectCodecErrorCode code,
    std::string detail,
    std::vector<core::ImageProjectField> fields = {})
    -> std::unexpected<ImageProjectCodecError>
{
    return std::unexpected(ImageProjectCodecError{
        code,
        std::move(fields),
        std::move(detail),
    });
}

auto checked_add(std::size_t left, std::size_t right)
    -> std::optional<std::size_t>
{
    if (right > std::numeric_limits<std::size_t>::max() - left)
    {
        return std::nullopt;
    }
    return left + right;
}

auto starts_with(
    std::span<const std::byte> bytes,
    std::span<const std::byte> prefix) -> bool
{
    return bytes.size() >= prefix.size() &&
           std::ranges::equal(bytes.first(prefix.size()), prefix);
}

auto mime_text(core::ImageMime mime) -> std::string_view
{
    switch (mime)
    {
    case core::ImageMime::Png:
        return "image/png";
    case core::ImageMime::Jpeg:
        return "image/jpeg";
    case core::ImageMime::WebP:
        return "image/webp";
    }
    return {};
}

auto mime_extension(core::ImageMime mime) -> std::string_view
{
    switch (mime)
    {
    case core::ImageMime::Png:
        return "png";
    case core::ImageMime::Jpeg:
        return "jpg";
    case core::ImageMime::WebP:
        return "webp";
    }
    return {};
}

auto parse_mime(std::string_view value)
    -> std::optional<core::ImageMime>
{
    if (value == "image/png")
    {
        return core::ImageMime::Png;
    }
    if (value == "image/jpeg")
    {
        return core::ImageMime::Jpeg;
    }
    if (value == "image/webp")
    {
        return core::ImageMime::WebP;
    }
    return std::nullopt;
}

auto source_path(std::string_view asset_id, core::ImageMime mime)
    -> std::string
{
    return "sources/" + std::string{asset_id} + "." +
           std::string{mime_extension(mime)};
}

auto valid_entry_name(std::string_view value) -> bool
{
    if (value.empty() || value.size() > MaximumEntryNameBytes ||
        value.starts_with('/') || value.ends_with('/') ||
        value.contains(".."))
    {
        return false;
    }
    return std::ranges::all_of(
        value,
        [](unsigned char character)
        {
            return (character >= 'a' && character <= 'z') ||
                   (character >= '0' && character <= '9') ||
                   character == '/' || character == '.' ||
                   character == '-' || character == '_';
        });
}

auto ascii_lower(std::string_view value) -> std::string
{
    auto result = std::string{value};
    std::ranges::transform(
        result,
        result.begin(),
        [](unsigned char character)
        {
            return character >= 'A' && character <= 'Z'
                       ? static_cast<char>(character - 'A' + 'a')
                       : static_cast<char>(character);
        });
    return result;
}

auto hash_bytes(
    PresetHasher& hasher,
    std::span<const std::byte> bytes)
    -> std::expected<common::Sha256Digest, ImageProjectCodecError>
{
    auto result = hasher.hash(bytes);
    if (!result)
    {
        return std::unexpected(ImageProjectCodecError{
            result.error().code == PresetHashErrorCode::Unavailable
                ? ImageProjectCodecErrorCode::HashUnavailable
                : ImageProjectCodecErrorCode::HashFailure,
            {},
            result.error().detail,
        });
    }
    return *result;
}

class ByteWriter
{
public:
    explicit ByteWriter(std::size_t capacity)
    {
        bytes_.reserve(capacity);
    }

    auto append(std::span<const std::byte> bytes) -> void
    {
        bytes_.insert(bytes_.end(), bytes.begin(), bytes.end());
    }

    auto u8(std::uint8_t value) -> void
    {
        bytes_.push_back(static_cast<std::byte>(value));
    }

    auto u16(std::uint16_t value) -> void
    {
        for (auto shift = 0U; shift < 16U; shift += 8U)
        {
            u8(static_cast<std::uint8_t>(value >> shift));
        }
    }

    auto u32(std::uint32_t value) -> void
    {
        for (auto shift = 0U; shift < 32U; shift += 8U)
        {
            u8(static_cast<std::uint8_t>(value >> shift));
        }
    }

    auto u64(std::uint64_t value) -> void
    {
        for (auto shift = 0U; shift < 64U; shift += 8U)
        {
            u8(static_cast<std::uint8_t>(value >> shift));
        }
    }

    auto finish() && -> std::vector<std::byte>
    {
        return std::move(bytes_);
    }

private:
    std::vector<std::byte> bytes_{};
};

class ByteReader
{
public:
    explicit ByteReader(std::span<const std::byte> bytes)
        : bytes_{bytes}
    {
    }

    auto take(std::size_t count)
        -> std::optional<std::span<const std::byte>>
    {
        if (count > bytes_.size() - position_)
        {
            return std::nullopt;
        }
        const auto result = bytes_.subspan(position_, count);
        position_ += count;
        return result;
    }

    auto u8() -> std::optional<std::uint8_t>
    {
        const auto bytes = take(1U);
        return bytes
                   ? std::optional<std::uint8_t>{
                         std::to_integer<std::uint8_t>((*bytes)[0])}
                   : std::nullopt;
    }

    auto u16() -> std::optional<std::uint16_t>
    {
        const auto bytes = take(2U);
        if (!bytes)
        {
            return std::nullopt;
        }
        return static_cast<std::uint16_t>(
            std::to_integer<std::uint8_t>((*bytes)[0]) |
            static_cast<std::uint16_t>(
                std::to_integer<std::uint8_t>((*bytes)[1]))
                << 8U);
    }

    auto u32() -> std::optional<std::uint32_t>
    {
        const auto bytes = take(4U);
        if (!bytes)
        {
            return std::nullopt;
        }
        auto result = std::uint32_t{};
        for (auto index = 0U; index < 4U; ++index)
        {
            result |=
                static_cast<std::uint32_t>(
                    std::to_integer<std::uint8_t>((*bytes)[index]))
                << (index * 8U);
        }
        return result;
    }

    auto u64() -> std::optional<std::uint64_t>
    {
        const auto bytes = take(8U);
        if (!bytes)
        {
            return std::nullopt;
        }
        auto result = std::uint64_t{};
        for (auto index = 0U; index < 8U; ++index)
        {
            result |=
                static_cast<std::uint64_t>(
                    std::to_integer<std::uint8_t>((*bytes)[index]))
                << (index * 8U);
        }
        return result;
    }

    [[nodiscard]] auto at_end() const -> bool
    {
        return position_ == bytes_.size();
    }

private:
    std::span<const std::byte> bytes_{};
    std::size_t position_{};
};

auto manifest_layer(const core::ImageLayer& layer) -> ManifestLayer
{
    return ManifestLayer{
        layer.asset_id,
        layer.file_name,
        std::string{mime_text(layer.mime)},
        static_cast<std::uint64_t>(layer.source_bytes),
        layer.center_x,
        layer.center_y,
        layer.width,
        layer.height,
        layer.crop,
        layer.wrap_atlas_seam,
        layer.mirror_front_back,
    };
}

auto project_layer(const ManifestLayer& layer)
    -> std::expected<core::ImageLayer, ImageProjectCodecError>
{
    const auto mime = parse_mime(layer.mime);
    if (!mime)
    {
        return codec_error(
            ImageProjectCodecErrorCode::UnsupportedCodec,
            "Image project layer uses an unsupported codec.");
    }
    if (layer.source_bytes >
        static_cast<std::uint64_t>(
            std::numeric_limits<std::size_t>::max()))
    {
        return codec_error(
            ImageProjectCodecErrorCode::InvalidProject,
            "Image project layer size exceeds the platform limit.");
    }
    return core::ImageLayer{
        layer.asset_id,
        layer.file_name,
        *mime,
        static_cast<std::size_t>(layer.source_bytes),
        layer.center_x,
        layer.center_y,
        layer.width,
        layer.height,
        layer.crop,
        layer.wrap_atlas_seam,
        layer.mirror_front_back,
    };
}

auto parse_manifest(std::string_view json)
    -> std::expected<ProjectManifest, ImageProjectCodecError>
{
    const auto strict = detail::validate_strict_json_document(json);
    if (!strict)
    {
        return codec_error(
            ImageProjectCodecErrorCode::MalformedManifest,
            strict.error().detail);
    }
    auto manifest = ProjectManifest{};
    const auto parsed =
        glz::read<detail::StrictJson>(manifest, json);
    if (parsed)
    {
        return codec_error(
            ImageProjectCodecErrorCode::MalformedManifest,
            glz::format_error(parsed, json));
    }
    if (manifest.schema_version !=
        core::ImageProjectSchemaVersion)
    {
        return codec_error(
            ImageProjectCodecErrorCode::UnsupportedFormat,
            "Image project manifest schema is unsupported.");
    }
    return manifest;
}

auto find_entry(
    std::vector<ContainerEntry>& entries,
    std::string_view name,
    std::uint8_t role)
    -> ContainerEntry*
{
    const auto found = std::ranges::find_if(
        entries,
        [name, role](const ContainerEntry& entry)
        {
            return entry.name == name && entry.role == role;
        });
    return found == entries.end() ? nullptr : &*found;
}
} // namespace preset_detail

using namespace preset_detail;

auto NativePresetHasher::hash(std::span<const std::byte> bytes)
    -> std::expected<common::Sha256Digest, PresetHashError>
{
    const auto hashed = common::sha256_bytes(bytes);
    if (!hashed)
    {
        return std::unexpected(PresetHashError{
            hashed.error().code ==
                    common::HashErrorCode::PlatformUnsupported
                ? PresetHashErrorCode::Unavailable
                : PresetHashErrorCode::Failure,
            hashed.error().detail,
        });
    }
    return *hashed;
}

auto encode_image_project(
    const core::ImageProject& project,
    PresetHasher& hasher)
    -> std::expected<std::vector<std::byte>, ImageProjectCodecError>
{
    auto fields = core::validate(project);
    if (!fields.empty())
    {
        return codec_error(
            ImageProjectCodecErrorCode::InvalidProject,
            "Image project contains invalid values.",
            std::move(fields));
    }

    auto entries = std::vector<ContainerEntry>{};
    entries.reserve(project.sources.size() + 1U);
    auto manifest_sources = std::vector<ManifestSource>{};
    manifest_sources.reserve(project.sources.size());
    for (const auto& source : project.sources)
    {
        const auto digest = hash_bytes(
            hasher,
            std::span<const std::byte>{*source.bytes});
        if (!digest)
        {
            return std::unexpected(digest.error());
        }
        const auto digest_text = common::sha256_hex(*digest);
        if (digest_text != source.asset_id)
        {
            return codec_error(
                ImageProjectCodecErrorCode::HashMismatch,
                "Image source asset ID does not match its bytes.");
        }
        const auto path = source_path(source.asset_id, source.mime);
        entries.push_back(ContainerEntry{
            path,
            SourceRole,
            static_cast<std::uint64_t>(source.bytes->size()),
            *digest,
            source.bytes,
            false,
        });
        manifest_sources.push_back(ManifestSource{
            source.asset_id,
            std::string{mime_text(source.mime)},
            static_cast<std::uint64_t>(source.bytes->size()),
            path,
            digest_text,
        });
    }
    std::ranges::sort(
        manifest_sources,
        {},
        &ManifestSource::asset_id);

    const auto atlas_digest = hash_bytes(
        hasher,
        std::span<const std::byte>{*project.canonical_atlas});
    if (!atlas_digest)
    {
        return std::unexpected(atlas_digest.error());
    }
    entries.push_back(ContainerEntry{
        "atlas.rgba",
        AtlasRole,
        static_cast<std::uint64_t>(
            project.canonical_atlas->size()),
        *atlas_digest,
        project.canonical_atlas,
        false,
    });
    std::ranges::sort(entries, {}, &ContainerEntry::name);

    auto layers = std::vector<ManifestLayer>{};
    layers.reserve(project.layers.size());
    std::ranges::transform(
        project.layers,
        std::back_inserter(layers),
        manifest_layer);
    const auto manifest = ProjectManifest{
        project.schema_version,
        project.project_id,
        project.display_name,
        project.revision,
        project.settings,
        std::move(layers),
        std::move(manifest_sources),
        ManifestAtlas{
            AtlasWidth,
            AtlasHeight,
            AtlasChannels,
            "rgba8",
            static_cast<std::uint64_t>(
                project.canonical_atlas->size()),
            "atlas.rgba",
            common::sha256_hex(*atlas_digest),
        },
    };
    auto json = glz::write_json(manifest);
    if (!json)
    {
        return codec_error(
            ImageProjectCodecErrorCode::Serialization,
            "Image project manifest could not be serialized.");
    }
    if (json->size() < 2U ||
        json->size() > MaximumPresetManifestBytes ||
        json->size() >
            std::numeric_limits<std::uint32_t>::max())
    {
        return codec_error(
            ImageProjectCodecErrorCode::TooLarge,
            "Image project manifest exceeds its byte limit.");
    }
    const auto manifest_digest = hash_bytes(
        hasher,
        std::as_bytes(std::span{*json}));
    if (!manifest_digest)
    {
        return std::unexpected(manifest_digest.error());
    }

    auto total = checked_add(HeaderByteLength, json->size());
    for (const auto& entry : entries)
    {
        if (!total || !valid_entry_name(entry.name) ||
            entry.name.size() >
                std::numeric_limits<std::uint16_t>::max())
        {
            return codec_error(
                ImageProjectCodecErrorCode::InvalidEntry,
                "Image project entry name is invalid.");
        }
        total = checked_add(
            *total,
            44U + entry.name.size());
        if (total)
        {
            total = checked_add(
                *total,
                static_cast<std::size_t>(entry.byte_length));
        }
    }
    if (!total || *total > MaximumPresetContainerBytes ||
        entries.size() > MaximumPresetEntries ||
        entries.size() >
            std::numeric_limits<std::uint32_t>::max())
    {
        return codec_error(
            ImageProjectCodecErrorCode::TooLarge,
            "Image project container exceeds its resource limits.");
    }

    auto writer = ByteWriter{*total};
    writer.append(V2Magic);
    writer.u32(ImagePresetContainerVersion);
    writer.u32(static_cast<std::uint32_t>(json->size()));
    writer.u32(static_cast<std::uint32_t>(entries.size()));
    writer.u32(0U);
    writer.append(manifest_digest->bytes);
    writer.append(std::as_bytes(std::span{*json}));
    for (const auto& entry : entries)
    {
        writer.u16(static_cast<std::uint16_t>(entry.name.size()));
        writer.u8(entry.role);
        writer.u8(0U);
        writer.u64(entry.byte_length);
        writer.append(entry.digest.bytes);
        writer.append(std::as_bytes(std::span{entry.name}));
    }
    for (const auto& entry : entries)
    {
        writer.append(*entry.bytes);
    }
    return std::move(writer).finish();
}

auto decode_image_project(
    std::span<const std::byte> container,
    PresetHasher& hasher)
    -> std::expected<core::ImageProject, ImageProjectCodecError>
{
    if (container.size() > MaximumPresetContainerBytes)
    {
        return codec_error(
            ImageProjectCodecErrorCode::TooLarge,
            "Image project container exceeds its byte limit.");
    }
    if (starts_with(container, V1Magic))
    {
        return codec_error(
            ImageProjectCodecErrorCode::LegacyFormat,
            "v1 Image Paint presets are unsupported and were not modified.");
    }
    if (!starts_with(container, V2Magic))
    {
        return codec_error(
            ImageProjectCodecErrorCode::UnsupportedFormat,
            "Image project container magic is unsupported.");
    }

    auto reader = ByteReader{container};
    static_cast<void>(reader.take(V2Magic.size()));
    const auto version = reader.u32();
    const auto manifest_length = reader.u32();
    const auto entry_count = reader.u32();
    const auto reserved = reader.u32();
    const auto manifest_digest_bytes = reader.take(32U);
    if (!version || !manifest_length || !entry_count || !reserved ||
        !manifest_digest_bytes ||
        *version != ImagePresetContainerVersion ||
        *manifest_length < 2U ||
        *manifest_length > MaximumPresetManifestBytes ||
        *entry_count == 0U ||
        *entry_count > MaximumPresetEntries ||
        *reserved != 0U)
    {
        return codec_error(
            ImageProjectCodecErrorCode::MalformedHeader,
            "Image project container header is invalid.");
    }
    const auto manifest_bytes = reader.take(*manifest_length);
    if (!manifest_bytes)
    {
        return codec_error(
            ImageProjectCodecErrorCode::MalformedHeader,
            "Image project manifest is truncated.");
    }
    const auto actual_manifest_digest =
        hash_bytes(hasher, *manifest_bytes);
    if (!actual_manifest_digest)
    {
        return std::unexpected(actual_manifest_digest.error());
    }
    if (!std::ranges::equal(
            actual_manifest_digest->bytes,
            *manifest_digest_bytes))
    {
        return codec_error(
            ImageProjectCodecErrorCode::HashMismatch,
            "Image project manifest hash does not match.");
    }
    const auto manifest_text = std::string_view{
        reinterpret_cast<const char*>(manifest_bytes->data()),
        manifest_bytes->size(),
    };
    auto manifest = parse_manifest(manifest_text);
    if (!manifest)
    {
        return std::unexpected(manifest.error());
    }

    auto entries = std::vector<ContainerEntry>{};
    entries.reserve(*entry_count);
    auto folded_names = std::set<std::string, std::less<>>{};
    auto previous_name = std::string{};
    auto source_total = std::uint64_t{};
    for (auto index = std::uint32_t{}; index < *entry_count; ++index)
    {
        const auto name_length = reader.u16();
        const auto role = reader.u8();
        const auto descriptor_reserved = reader.u8();
        const auto byte_length = reader.u64();
        const auto digest_bytes = reader.take(32U);
        if (!name_length || !role || !descriptor_reserved ||
            !byte_length || !digest_bytes ||
            *name_length == 0U ||
            *name_length > MaximumEntryNameBytes ||
            (*role != AtlasRole && *role != SourceRole) ||
            *descriptor_reserved != 0U)
        {
            return codec_error(
                ImageProjectCodecErrorCode::InvalidEntry,
                "Image project entry descriptor is invalid.");
        }
        const auto name_bytes = reader.take(*name_length);
        if (!name_bytes)
        {
            return codec_error(
                ImageProjectCodecErrorCode::InvalidEntry,
                "Image project entry name is truncated.");
        }
        auto name = std::string{
            reinterpret_cast<const char*>(name_bytes->data()),
            name_bytes->size(),
        };
        if (!valid_entry_name(name) ||
            (!previous_name.empty() && name <= previous_name) ||
            !folded_names.insert(ascii_lower(name)).second)
        {
            return codec_error(
                ImageProjectCodecErrorCode::InvalidEntry,
                "Image project entry names are not canonical.");
        }
        previous_name = name;

        if (*role == AtlasRole &&
            (*byte_length != core::CanonicalAtlasByteLength ||
             name != "atlas.rgba"))
        {
            return codec_error(
                ImageProjectCodecErrorCode::InvalidEntry,
                "Image project atlas descriptor is invalid.");
        }
        if (*role == SourceRole)
        {
            if (*byte_length == 0U ||
                *byte_length > core::MaximumImageSourceBytes ||
                *byte_length >
                    std::numeric_limits<std::uint64_t>::max() -
                        source_total)
            {
                return codec_error(
                    ImageProjectCodecErrorCode::TooLarge,
                    "Image project source entry exceeds its limit.");
            }
            source_total += *byte_length;
            if (source_total > core::MaximumProjectSourceBytes)
            {
                return codec_error(
                    ImageProjectCodecErrorCode::TooLarge,
                    "Image project source entries exceed their total limit.");
            }
        }

        auto digest = common::Sha256Digest{};
        std::ranges::copy(*digest_bytes, digest.bytes.begin());
        entries.push_back(ContainerEntry{
            std::move(name),
            *role,
            *byte_length,
            digest,
            {},
            false,
        });
    }

    for (auto& entry : entries)
    {
        if (entry.byte_length >
            static_cast<std::uint64_t>(
                std::numeric_limits<std::size_t>::max()))
        {
            return codec_error(
                ImageProjectCodecErrorCode::TooLarge,
                "Image project entry exceeds the platform size limit.");
        }
        const auto bytes =
            reader.take(static_cast<std::size_t>(entry.byte_length));
        if (!bytes)
        {
            return codec_error(
                ImageProjectCodecErrorCode::InvalidEntry,
                "Image project entry data is truncated.");
        }
        const auto digest = hash_bytes(hasher, *bytes);
        if (!digest)
        {
            return std::unexpected(digest.error());
        }
        if (*digest != entry.digest)
        {
            return codec_error(
                ImageProjectCodecErrorCode::HashMismatch,
                "Image project entry hash does not match.");
        }
        entry.bytes =
            std::make_shared<const std::vector<std::byte>>(
                bytes->begin(),
                bytes->end());
    }
    if (!reader.at_end())
    {
        return codec_error(
            ImageProjectCodecErrorCode::InvalidEntry,
            "Image project container has trailing bytes.");
    }

    if (manifest->atlas.width != AtlasWidth ||
        manifest->atlas.height != AtlasHeight ||
        manifest->atlas.channels != AtlasChannels ||
        manifest->atlas.encoding != "rgba8" ||
        manifest->atlas.byte_length !=
            core::CanonicalAtlasByteLength ||
        manifest->atlas.path != "atlas.rgba")
    {
        return codec_error(
            ImageProjectCodecErrorCode::InvalidProject,
            "Image project atlas manifest is invalid.");
    }
    auto* atlas = find_entry(
        entries,
        manifest->atlas.path,
        AtlasRole);
    if (atlas == nullptr ||
        common::sha256_hex(atlas->digest) !=
            manifest->atlas.sha256)
    {
        return codec_error(
            ImageProjectCodecErrorCode::HashMismatch,
            "Image project atlas identity does not match.");
    }
    atlas->consumed = true;

    auto sources = std::vector<core::ImageSourceAsset>{};
    sources.reserve(manifest->sources.size());
    auto previous_source_id = std::string{};
    for (const auto& source : manifest->sources)
    {
        const auto mime = parse_mime(source.mime);
        if (!mime)
        {
            return codec_error(
                ImageProjectCodecErrorCode::UnsupportedCodec,
                "Image project source uses an unsupported codec.");
        }
        const auto expected_path =
            source_path(source.asset_id, *mime);
        if ((!previous_source_id.empty() &&
             source.asset_id <= previous_source_id) ||
            source.path != expected_path ||
            source.sha256 != source.asset_id ||
            source.byte_length == 0U ||
            source.byte_length > core::MaximumImageSourceBytes)
        {
            return codec_error(
                ImageProjectCodecErrorCode::InvalidProject,
                "Image project source manifest is not canonical.");
        }
        previous_source_id = source.asset_id;
        auto* entry = find_entry(
            entries,
            source.path,
            SourceRole);
        if (entry == nullptr || entry->consumed ||
            entry->byte_length != source.byte_length ||
            common::sha256_hex(entry->digest) != source.sha256)
        {
            return codec_error(
                ImageProjectCodecErrorCode::HashMismatch,
                "Image project source identity does not match.");
        }
        entry->consumed = true;
        sources.push_back(core::ImageSourceAsset{
            source.asset_id,
            *mime,
            entry->bytes,
        });
    }
    if (sources.empty() ||
        sources.size() > core::MaximumImageSources ||
        std::ranges::any_of(
            entries,
            [](const ContainerEntry& entry)
            {
                return !entry.consumed;
            }))
    {
        return codec_error(
            ImageProjectCodecErrorCode::InvalidEntry,
            "Image project contains missing or unused entries.");
    }

    auto layers = std::vector<core::ImageLayer>{};
    layers.reserve(manifest->layers.size());
    for (const auto& layer : manifest->layers)
    {
        auto converted = project_layer(layer);
        if (!converted)
        {
            return std::unexpected(converted.error());
        }
        layers.push_back(std::move(*converted));
    }
    auto project = core::ImageProject{
        manifest->schema_version,
        std::move(manifest->project_id),
        std::move(manifest->display_name),
        manifest->revision,
        manifest->settings,
        std::move(layers),
        std::move(sources),
        atlas->bytes,
    };
    auto fields = core::validate(project);
    if (!fields.empty())
    {
        return codec_error(
            ImageProjectCodecErrorCode::InvalidProject,
            "Image project manifest contains invalid values.",
            std::move(fields));
    }
    return project;
}
} // namespace meccha::application
