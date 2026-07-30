#pragma once

#include <meccha/common/hash.hpp>
#include <meccha/core/image_project.hpp>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <vector>

namespace meccha::application
{
inline constexpr std::uint32_t ImagePresetContainerVersion = 1U;
inline constexpr std::size_t MaximumPresetManifestBytes =
    1024U * 1024U;
inline constexpr std::size_t MaximumPresetEntries =
    core::MaximumImageSources + 1U;
inline constexpr std::size_t MaximumPresetContainerBytes =
    68U * 1024U * 1024U;

enum class PresetHashErrorCode : std::uint8_t
{
    Unavailable,
    Failure,
};

struct PresetHashError
{
    PresetHashErrorCode code{};
    std::string detail{};

    auto operator==(const PresetHashError&) const -> bool = default;
};

class PresetHasher
{
public:
    PresetHasher() = default;
    PresetHasher(const PresetHasher&) = delete;
    auto operator=(const PresetHasher&) -> PresetHasher& = delete;
    virtual ~PresetHasher() = default;

    virtual auto hash(std::span<const std::byte> bytes)
        -> std::expected<common::Sha256Digest, PresetHashError> = 0;
};

class NativePresetHasher final : public PresetHasher
{
public:
    auto hash(std::span<const std::byte> bytes)
        -> std::expected<common::Sha256Digest, PresetHashError> override;
};

enum class ImageProjectCodecErrorCode : std::uint8_t
{
    TooLarge,
    LegacyFormat,
    UnsupportedFormat,
    UnsupportedCodec,
    MalformedHeader,
    MalformedManifest,
    InvalidEntry,
    HashMismatch,
    InvalidProject,
    HashUnavailable,
    HashFailure,
    Serialization,
};

struct ImageProjectCodecError
{
    ImageProjectCodecErrorCode code{};
    std::vector<core::ImageProjectField> fields{};
    std::string detail{};

    auto operator==(const ImageProjectCodecError&) const -> bool = default;
};

[[nodiscard]] auto encode_image_project(
    const core::ImageProject& project,
    PresetHasher& hasher)
    -> std::expected<std::vector<std::byte>, ImageProjectCodecError>;

[[nodiscard]] auto decode_image_project(
    std::span<const std::byte> container,
    PresetHasher& hasher)
    -> std::expected<core::ImageProject, ImageProjectCodecError>;
} // namespace meccha::application
