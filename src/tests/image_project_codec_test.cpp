#include <meccha/application/image_project_codec.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
using namespace meccha;

auto expect(bool condition, std::string_view message) -> bool
{
    if (!condition)
    {
        std::cerr << "FAIL image_project_codec: " << message << '\n';
    }
    return condition;
}

class DeterministicTestHasher final
    : public application::PresetHasher
{
public:
    auto hash(std::span<const std::byte> bytes)
        -> std::expected<
            common::Sha256Digest,
            application::PresetHashError> override
    {
        auto digest = common::Sha256Digest{};
        for (auto index = std::size_t{};
             index < digest.bytes.size();
             ++index)
        {
            digest.bytes[index] =
                static_cast<std::byte>(index * 17U + 11U);
        }
        for (auto index = std::size_t{}; index < bytes.size(); ++index)
        {
            const auto slot = index % digest.bytes.size();
            const auto value =
                std::to_integer<std::uint8_t>(digest.bytes[slot]);
            const auto input =
                std::to_integer<std::uint8_t>(bytes[index]);
            digest.bytes[slot] = static_cast<std::byte>(
                static_cast<std::uint8_t>(
                    value * 33U + input +
                    static_cast<std::uint8_t>(index)));
        }
        return digest;
    }
};

class FailingTestHasher final : public application::PresetHasher
{
public:
    explicit FailingTestHasher(
        application::PresetHashErrorCode code)
        : code_{code}
    {
    }

    auto hash(std::span<const std::byte>)
        -> std::expected<
            common::Sha256Digest,
            application::PresetHashError> override
    {
        return std::unexpected(application::PresetHashError{
            code_,
            "injected hash failure",
        });
    }

private:
    application::PresetHashErrorCode code_{};
};

auto make_project(DeterministicTestHasher& hasher)
    -> core::ImageProject
{
    struct PendingSource
    {
        core::ImageMime mime{};
        std::shared_ptr<const std::vector<std::byte>> bytes{};
        std::string asset_id{};
    };
    auto pending = std::vector{
        PendingSource{
            core::ImageMime::Png,
            std::make_shared<const std::vector<std::byte>>(
                std::initializer_list<std::byte>{
                    std::byte{1},
                    std::byte{2},
                    std::byte{3},
                }),
            {},
        },
        PendingSource{
            core::ImageMime::WebP,
            std::make_shared<const std::vector<std::byte>>(
                std::initializer_list<std::byte>{
                    std::byte{9},
                    std::byte{8},
                    std::byte{7},
                    std::byte{6},
                }),
            {},
        },
    };
    for (auto& source : pending)
    {
        source.asset_id = common::sha256_hex(
            hasher.hash(*source.bytes).value());
    }
    std::ranges::sort(pending, {}, &PendingSource::asset_id);

    auto sources = std::vector<core::ImageSourceAsset>{};
    for (const auto& source : pending)
    {
        sources.push_back(core::ImageSourceAsset{
            source.asset_id,
            source.mime,
            source.bytes,
        });
    }
    auto layers = std::vector<core::ImageLayer>{};
    layers.push_back(core::ImageLayer{
        pending[1].asset_id,
        "front 日本語.webp",
        pending[1].mime,
        pending[1].bytes->size(),
        0.4,
        0.6,
        1.25,
        0.75,
        core::NormalizedCrop{0.1, 0.2, 0.8, 0.7},
        true,
        false,
    });
    layers.push_back(core::ImageLayer{
        pending[0].asset_id,
        "back.png",
        pending[0].mime,
        pending[0].bytes->size(),
        0.5,
        0.5,
        1.0,
        1.0,
        {},
        false,
        true,
    });

    auto settings = core::ImageProjectSettings{};
    settings.body = core::BodyProfile::Cube;
    settings.placement = core::PlacementMode::Fill;
    settings.alpha = core::AlphaMode::Background;
    settings.front = core::FaceBaseMode::Fill;
    settings.left = core::FaceBaseMode::Fill;
    settings.brush_size_texels = 7.5;
    settings.color_compression_tolerance_percent = 2.5;
    settings.image_material = core::Material{0.3, 0.4, 0.6};
    settings.fill_color = core::Rgb8{12U, 34U, 56U};
    settings.fill_material = core::Material{0.25, 0.75, 0.5};

    return core::ImageProject{
        core::ImageProjectSchemaVersion,
        "0123456789abcdef0123456789abcdef",
        "Preset 日本語",
        7U,
        settings,
        std::move(layers),
        std::move(sources),
        std::make_shared<const std::vector<std::byte>>(
            core::CanonicalAtlasByteLength,
            std::byte{0x7F}),
    };
}

auto replace_last(
    std::vector<std::byte>& bytes,
    std::string_view from,
    std::string_view to) -> bool
{
    if (from.size() != to.size())
    {
        return false;
    }
    const auto needle = std::as_bytes(std::span{from});
    auto last = bytes.end();
    auto search = bytes.begin();
    while (true)
    {
        const auto found = std::search(
            search,
            bytes.end(),
            needle.begin(),
            needle.end());
        if (found == bytes.end())
        {
            break;
        }
        last = found;
        search = std::next(found);
    }
    if (last == bytes.end())
    {
        return false;
    }
    std::ranges::copy(
        std::as_bytes(std::span{to}),
        last);
    return true;
}

auto replace_first(
    std::vector<std::byte>& bytes,
    std::string_view from,
    std::string_view to) -> bool
{
    if (from.size() != to.size())
    {
        return false;
    }
    const auto needle = std::as_bytes(std::span{from});
    const auto found = std::search(
        bytes.begin(),
        bytes.end(),
        needle.begin(),
        needle.end());
    if (found == bytes.end())
    {
        return false;
    }
    std::ranges::copy(
        std::as_bytes(std::span{to}),
        found);
    return true;
}

auto read_u32(
    std::span<const std::byte> bytes,
    std::size_t offset) -> std::optional<std::uint32_t>
{
    if (offset > bytes.size() || bytes.size() - offset < 4U)
    {
        return std::nullopt;
    }
    auto value = std::uint32_t{};
    for (auto index = std::size_t{}; index < 4U; ++index)
    {
        value |= static_cast<std::uint32_t>(
                     std::to_integer<std::uint8_t>(
                         bytes[offset + index]))
                 << (index * 8U);
    }
    return value;
}

auto refresh_manifest_hash(
    std::vector<std::byte>& bytes,
    DeterministicTestHasher& hasher) -> bool
{
    constexpr auto manifest_length_offset = std::size_t{12U};
    constexpr auto manifest_digest_offset = std::size_t{24U};
    constexpr auto manifest_offset = std::size_t{56U};
    const auto manifest_length =
        read_u32(bytes, manifest_length_offset);
    if (!manifest_length ||
        manifest_offset > bytes.size() ||
        *manifest_length > bytes.size() - manifest_offset)
    {
        return false;
    }
    const auto manifest = std::span<const std::byte>{bytes}.subspan(
        manifest_offset,
        *manifest_length);
    const auto digest = hasher.hash(manifest);
    if (!digest)
    {
        return false;
    }
    std::ranges::copy(
        digest->bytes,
        bytes.begin() +
            static_cast<std::ptrdiff_t>(
                manifest_digest_offset));
    return true;
}
} // namespace

auto main() -> int
{
    using namespace meccha;

    auto passed = true;
    auto hasher = DeterministicTestHasher{};
    const auto project = make_project(hasher);
    passed &= expect(
        core::validate(project).empty(),
        "the test project is invalid");

    const auto encoded =
        application::encode_image_project(project, hasher);
    passed &= expect(
        encoded && encoded->size() >
                       core::CanonicalAtlasByteLength &&
            encoded->size() <
                application::MaximumPresetContainerBytes &&
            std::to_integer<char>((*encoded)[0]) == 'M' &&
            std::to_integer<char>((*encoded)[1]) == 'C' &&
            std::to_integer<char>((*encoded)[2]) == 'V',
        "a valid project did not encode as a bounded v2 container");
    if (!encoded)
    {
        return 1;
    }

    const auto encoded_again =
        application::encode_image_project(project, hasher);
    passed &= expect(
        encoded_again && *encoded_again == *encoded,
        "v2 project encoding is not deterministic");
    auto reordered_sources = project;
    std::ranges::reverse(reordered_sources.sources);
    const auto reordered_encoding =
        application::encode_image_project(
            reordered_sources,
            hasher);
    passed &= expect(
        reordered_encoding && *reordered_encoding == *encoded,
        "source storage order changed canonical preset bytes");

    const auto decoded =
        application::decode_image_project(*encoded, hasher);
    passed &= expect(
        decoded && *decoded == project,
        "v2 project did not round trip exactly");

    auto corrupted_manifest = *encoded;
    const auto changed_manifest = replace_first(
        corrupted_manifest,
        "Preset",
        "Reset ");
    const auto corrupted_manifest_result =
        application::decode_image_project(
            corrupted_manifest,
            hasher);
    passed &= expect(
        changed_manifest && !corrupted_manifest_result &&
            corrupted_manifest_result.error().code ==
                application::ImageProjectCodecErrorCode::HashMismatch,
        "changed manifest bytes passed integrity validation");

    auto corrupted = *encoded;
    corrupted.back() ^= std::byte{1};
    const auto corrupted_result =
        application::decode_image_project(corrupted, hasher);
    passed &= expect(
        !corrupted_result &&
            corrupted_result.error().code ==
                application::ImageProjectCodecErrorCode::HashMismatch,
        "changed source/atlas bytes passed hash validation");

    auto unsupported_codec = *encoded;
    const auto changed_codec = replace_first(
        unsupported_codec,
        "image/png",
        "image/gif");
    const auto refreshed_codec_hash =
        refresh_manifest_hash(unsupported_codec, hasher);
    const auto codec_result =
        application::decode_image_project(
            unsupported_codec,
            hasher);
    passed &= expect(
        changed_codec && refreshed_codec_hash && !codec_result &&
            codec_result.error().code ==
                application::ImageProjectCodecErrorCode::UnsupportedCodec,
        "an unsupported image codec was accepted");

    auto unknown_manifest_field = *encoded;
    const auto changed_manifest_field = replace_first(
        unknown_manifest_field,
        "display_name",
        "unknown_name");
    const auto refreshed_unknown_hash =
        refresh_manifest_hash(unknown_manifest_field, hasher);
    const auto unknown_manifest_result =
        application::decode_image_project(
            unknown_manifest_field,
            hasher);
    passed &= expect(
        changed_manifest_field && refreshed_unknown_hash &&
            !unknown_manifest_result &&
            unknown_manifest_result.error().code ==
                application::ImageProjectCodecErrorCode::MalformedManifest,
        "an unknown manifest field was accepted");

    auto hostile_entry = *encoded;
    const auto changed_entry = replace_last(
        hostile_entry,
        "atlas.rgba",
        "Atlas.rgba");
    const auto hostile_result =
        application::decode_image_project(hostile_entry, hasher);
    passed &= expect(
        changed_entry && !hostile_result &&
            hostile_result.error().code ==
                application::ImageProjectCodecErrorCode::InvalidEntry,
        "a non-canonical entry path was accepted");

    auto traversal_entry = *encoded;
    const auto changed_traversal = replace_last(
        traversal_entry,
        "sources/",
        "../badx/");
    const auto traversal_result =
        application::decode_image_project(
            traversal_entry,
            hasher);
    passed &= expect(
        changed_traversal && !traversal_result &&
            traversal_result.error().code ==
                application::ImageProjectCodecErrorCode::InvalidEntry,
        "a traversal entry path was accepted");

    auto trailing = *encoded;
    trailing.push_back(std::byte{0});
    const auto trailing_result =
        application::decode_image_project(trailing, hasher);
    passed &= expect(
        !trailing_result &&
            trailing_result.error().code ==
                application::ImageProjectCodecErrorCode::InvalidEntry,
        "trailing project-container bytes were accepted");

    auto truncated = *encoded;
    truncated.pop_back();
    const auto truncated_result =
        application::decode_image_project(truncated, hasher);
    passed &= expect(
        !truncated_result &&
            truncated_result.error().code ==
                application::ImageProjectCodecErrorCode::InvalidEntry,
        "truncated project-container bytes were accepted");

    const auto legacy_text = std::string_view{"MCIPRST1"};
    const auto legacy = std::as_bytes(std::span{legacy_text});
    const auto legacy_result =
        application::decode_image_project(legacy, hasher);
    passed &= expect(
        !legacy_result &&
            legacy_result.error().code ==
                application::ImageProjectCodecErrorCode::LegacyFormat,
        "a recognized v1 preset was not rejected explicitly");

    auto wrong_asset = project;
    wrong_asset.sources.front().asset_id.assign(64U, '0');
    wrong_asset.layers[1].asset_id =
        wrong_asset.sources.front().asset_id;
    const auto wrong_asset_result =
        application::encode_image_project(wrong_asset, hasher);
    passed &= expect(
        !wrong_asset_result &&
            wrong_asset_result.error().code ==
                application::ImageProjectCodecErrorCode::HashMismatch,
        "a content-address mismatch was serialized");

    auto invalid_utf8 = project;
    invalid_utf8.display_name =
        std::string{"bad"} + static_cast<char>(0xC0);
    const auto invalid_utf8_result =
        application::encode_image_project(invalid_utf8, hasher);
    passed &= expect(
        !invalid_utf8_result &&
            invalid_utf8_result.error().code ==
                application::ImageProjectCodecErrorCode::InvalidProject &&
            invalid_utf8_result.error().fields ==
                std::vector{core::ImageProjectField::DisplayName},
        "an invalid UTF-8 project was serialized");

    auto invalid_mime = project;
    invalid_mime.sources.front().mime =
        static_cast<core::ImageMime>(0xFFU);
    const auto invalid_mime_result =
        application::encode_image_project(invalid_mime, hasher);
    passed &= expect(
        !invalid_mime_result &&
            invalid_mime_result.error().code ==
                application::ImageProjectCodecErrorCode::InvalidProject &&
            invalid_mime_result.error().fields ==
                std::vector{
                    core::ImageProjectField::SourceCodec,
                    core::ImageProjectField::SourceReference,
                },
        "an invalid source codec enum was serialized");

    auto unavailable_hasher = FailingTestHasher{
        application::PresetHashErrorCode::Unavailable};
    const auto unavailable_hash_result =
        application::encode_image_project(
            project,
            unavailable_hasher);
    passed &= expect(
        !unavailable_hash_result &&
            unavailable_hash_result.error().code ==
                application::ImageProjectCodecErrorCode::HashUnavailable,
        "an unavailable native hash provider was not distinguished");

    auto failing_hasher = FailingTestHasher{
        application::PresetHashErrorCode::Failure};
    const auto failed_hash_result =
        application::encode_image_project(project, failing_hasher);
    passed &= expect(
        !failed_hash_result &&
            failed_hash_result.error().code ==
                application::ImageProjectCodecErrorCode::HashFailure,
        "a native hash failure was reported as provider unavailability");

#ifdef _WIN32
    auto native_hasher = application::NativePresetHasher{};
    constexpr std::array abc{
        std::byte{'a'},
        std::byte{'b'},
        std::byte{'c'},
    };
    const auto native_digest = native_hasher.hash(abc);
    passed &= expect(
        native_digest &&
            common::sha256_hex(*native_digest) ==
                "ba7816bf8f01cfea414140de5dae2223"
                "b00361a396177a9cb410ff61f20015ad",
        "the preset hasher is not backed by native SHA-256");
#endif

    if (passed)
    {
        std::cout << "PASS image_project_codec\n";
        return 0;
    }
    return 1;
}
