#include <meccha/launcher/ownership.hpp>

namespace meccha::launcher
{
auto classify_owned_file(
    const OwnedFileExpectation& expected,
    const std::optional<FileMeasurement>& current,
    const std::optional<OwnershipRecord>& record) -> ArtifactState
{
    if (!current)
    {
        return ArtifactState::Missing;
    }

    const auto content_matches =
        current->size == expected.file.size &&
        current->sha256 == expected.file.sha256;
    if (!content_matches)
    {
        return ArtifactState::Conflict;
    }
    if (!record)
    {
        return ArtifactState::ExactUnowned;
    }

    const auto record_matches =
        record->product_version == expected.product_version &&
        record->manifest_sha256 == expected.manifest_sha256 &&
        record->file == expected.file &&
        current->size == record->file.size &&
        current->sha256 == record->file.sha256;
    return record_matches ? ArtifactState::ExactOwned : ArtifactState::Conflict;
}
} // namespace meccha::launcher
