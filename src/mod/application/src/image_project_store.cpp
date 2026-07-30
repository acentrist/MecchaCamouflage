#include <meccha/application/image_project_store.hpp>

#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace meccha::application
{
namespace
{
constexpr auto ActiveDraftFileName = "active-draft.mcpreset";

auto storage_error(ProjectStorageError error)
    -> ImageProjectStoreError
{
    return ImageProjectStoreError{
        ImageProjectStoreErrorCode::Storage,
        std::move(error),
        std::nullopt,
        "Image project storage failed.",
    };
}

auto codec_error(ImageProjectCodecError error)
    -> ImageProjectStoreError
{
    return ImageProjectStoreError{
        ImageProjectStoreErrorCode::Codec,
        std::nullopt,
        std::move(error),
        "Image project validation or decoding failed.",
    };
}

auto project_error(
    ImageProjectStoreErrorCode code,
    std::string detail) -> ImageProjectStoreError
{
    return ImageProjectStoreError{
        code,
        std::nullopt,
        std::nullopt,
        std::move(detail),
    };
}

auto project_file_name(std::string_view project_id)
    -> std::expected<std::string, ImageProjectStoreError>
{
    if (!core::valid_image_project_id(project_id))
    {
        return std::unexpected(project_error(
            ImageProjectStoreErrorCode::InvalidProjectId,
            "Image project ID must be 32 lowercase hexadecimal bytes."));
    }
    return std::string{project_id} + ".mcpreset";
}

auto decode_stored_project(
    std::span<const std::byte> bytes,
    PresetHasher& hasher)
    -> std::expected<core::ImageProject, ImageProjectStoreError>
{
    auto decoded = decode_image_project(bytes, hasher);
    if (!decoded)
    {
        return std::unexpected(codec_error(decoded.error()));
    }
    return std::move(*decoded);
}
} // namespace

ImageProjectStore::ImageProjectStore(
    AtomicProjectStorage& storage,
    PresetHasher& hasher)
    : storage_{storage},
      hasher_{hasher}
{
}

auto ImageProjectStore::load_named(std::string_view project_id)
    -> std::expected<
        std::optional<core::ImageProject>,
        ImageProjectStoreError>
{
    const auto file_name = project_file_name(project_id);
    if (!file_name)
    {
        return std::unexpected(file_name.error());
    }
    auto bytes =
        storage_.read(*file_name, MaximumPresetContainerBytes);
    if (!bytes)
    {
        return std::unexpected(storage_error(bytes.error()));
    }
    if (!*bytes)
    {
        return std::nullopt;
    }
    auto project = decode_stored_project(**bytes, hasher_);
    if (!project)
    {
        return std::unexpected(project.error());
    }
    if (project->project_id != project_id)
    {
        return std::unexpected(project_error(
            ImageProjectStoreErrorCode::IdentityMismatch,
            "Stored project ID does not match its file name."));
    }
    return std::optional<core::ImageProject>{
        std::move(*project)};
}

auto ImageProjectStore::save_named(
    const core::ImageProject& project,
    std::uint64_t expected_revision)
    -> std::expected<void, ImageProjectStoreError>
{
    const auto file_name =
        project_file_name(project.project_id);
    if (!file_name)
    {
        return std::unexpected(file_name.error());
    }
    if (project.revision <= expected_revision)
    {
        const auto overflow =
            expected_revision ==
            std::numeric_limits<std::uint64_t>::max();
        return std::unexpected(project_error(
            overflow
                ? ImageProjectStoreErrorCode::RevisionOverflow
                : ImageProjectStoreErrorCode::RevisionConflict,
            overflow
                ? "Image project revision cannot be advanced."
                : "Image project revision did not advance."));
    }

    auto encoded = encode_image_project(project, hasher_);
    if (!encoded)
    {
        return std::unexpected(codec_error(encoded.error()));
    }
    auto current = load_named(project.project_id);
    if (!current)
    {
        return std::unexpected(current.error());
    }
    if ((!*current && expected_revision != 0U) ||
        (*current &&
         (*current)->revision != expected_revision))
    {
        return std::unexpected(project_error(
            ImageProjectStoreErrorCode::RevisionConflict,
            "Stored project revision changed before publication."));
    }

    auto written = storage_.write_atomic(*file_name, *encoded);
    if (!written)
    {
        return std::unexpected(storage_error(written.error()));
    }
    return {};
}

auto ImageProjectStore::rename_named(
    std::string_view project_id,
    std::string new_name)
    -> std::expected<
        core::ImageProject,
        ImageProjectStoreError>
{
    auto current = load_named(project_id);
    if (!current)
    {
        return std::unexpected(current.error());
    }
    if (!*current)
    {
        return std::unexpected(project_error(
            ImageProjectStoreErrorCode::NotFound,
            "Image project does not exist."));
    }
    if ((*current)->revision ==
        std::numeric_limits<std::uint64_t>::max())
    {
        return std::unexpected(project_error(
            ImageProjectStoreErrorCode::RevisionOverflow,
            "Image project revision cannot be advanced."));
    }

    auto renamed = std::move(**current);
    const auto previous_revision = renamed.revision;
    renamed.display_name = std::move(new_name);
    ++renamed.revision;
    auto saved = save_named(renamed, previous_revision);
    if (!saved)
    {
        return std::unexpected(saved.error());
    }
    return renamed;
}

auto ImageProjectStore::delete_named(std::string_view project_id)
    -> std::expected<bool, ImageProjectStoreError>
{
    const auto file_name = project_file_name(project_id);
    if (!file_name)
    {
        return std::unexpected(file_name.error());
    }
    auto removed = storage_.remove(*file_name);
    if (!removed)
    {
        return std::unexpected(storage_error(removed.error()));
    }
    return *removed;
}

auto ImageProjectStore::load_active_draft()
    -> std::expected<
        std::optional<core::ImageProject>,
        ImageProjectStoreError>
{
    auto bytes = storage_.read(
        ActiveDraftFileName,
        MaximumPresetContainerBytes);
    if (!bytes)
    {
        return std::unexpected(storage_error(bytes.error()));
    }
    if (!*bytes)
    {
        return std::nullopt;
    }
    auto project = decode_stored_project(**bytes, hasher_);
    if (!project)
    {
        return std::unexpected(project.error());
    }
    return std::optional<core::ImageProject>{
        std::move(*project)};
}

auto ImageProjectStore::save_active_draft(
    const core::ImageProject& project)
    -> std::expected<void, ImageProjectStoreError>
{
    auto encoded = encode_image_project(project, hasher_);
    if (!encoded)
    {
        return std::unexpected(codec_error(encoded.error()));
    }
    auto written =
        storage_.write_atomic(ActiveDraftFileName, *encoded);
    if (!written)
    {
        return std::unexpected(storage_error(written.error()));
    }
    return {};
}

auto ImageProjectStore::clear_active_draft()
    -> std::expected<bool, ImageProjectStoreError>
{
    auto removed = storage_.remove(ActiveDraftFileName);
    if (!removed)
    {
        return std::unexpected(storage_error(removed.error()));
    }
    return *removed;
}
} // namespace meccha::application
