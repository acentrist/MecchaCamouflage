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

    const auto current_matches_expected =
        current->size == expected.file.size &&
        current->sha256 == expected.file.sha256;
    if (!record)
    {
        return current_matches_expected
                   ? ArtifactState::ExactUnowned
                   : ArtifactState::Conflict;
    }

    const auto current_matches_record =
        current->size == record->file.size &&
        current->sha256 == record->file.sha256;
    if (!current_matches_record ||
        record->file.path != expected.file.path ||
        record->file.role != expected.file.role)
    {
        return ArtifactState::Conflict;
    }

    const auto record_matches =
        record->product_version == expected.product_version &&
        record->manifest_sha256 == expected.manifest_sha256 &&
        record->file == expected.file;
    return record_matches && current_matches_expected
               ? ArtifactState::ExactOwned
               : ArtifactState::OwnedPrevious;
}
} // namespace meccha::launcher
