#pragma once

#include <meccha/launcher/manifest.hpp>

#include <optional>
#include <string>

namespace meccha::launcher
{
enum class ArtifactState : std::uint8_t
{
    Missing,
    ExactOwned,
    ExactUnowned,
    OwnedPrevious,
    Conflict,
};

struct FileMeasurement
{
    std::uint64_t size{};
    Sha256Digest sha256{};

    auto operator==(const FileMeasurement&) const -> bool = default;
};

struct OwnershipRecord
{
    std::string product_version{};
    Sha256Digest manifest_sha256{};
    ManifestFile file{};

    auto operator==(const OwnershipRecord&) const -> bool = default;
};

struct OwnedFileExpectation
{
    std::string product_version{};
    Sha256Digest manifest_sha256{};
    ManifestFile file{};

    auto operator==(const OwnedFileExpectation&) const -> bool = default;
};

[[nodiscard]] auto classify_owned_file(
    const OwnedFileExpectation& expected,
    const std::optional<FileMeasurement>& current,
    const std::optional<OwnershipRecord>& record) -> ArtifactState;
} // namespace meccha::launcher
