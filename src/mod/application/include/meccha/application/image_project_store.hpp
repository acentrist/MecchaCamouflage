#pragma once

#include <meccha/application/image_project_codec.hpp>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace meccha::application
{
enum class ProjectStorageErrorCode : std::uint8_t
{
    Io,
    Conflict,
    TooLarge,
};

struct ProjectStorageError
{
    ProjectStorageErrorCode code{};
    std::string detail{};

    auto operator==(const ProjectStorageError&) const -> bool = default;
};

class AtomicProjectStorage
{
public:
    AtomicProjectStorage() = default;
    AtomicProjectStorage(const AtomicProjectStorage&) = delete;
    auto operator=(const AtomicProjectStorage&)
        -> AtomicProjectStorage& = delete;
    AtomicProjectStorage(AtomicProjectStorage&&) = delete;
    auto operator=(AtomicProjectStorage&&)
        -> AtomicProjectStorage& = delete;
    virtual ~AtomicProjectStorage() = default;

    virtual auto read(
        std::string_view name,
        std::size_t maximum_bytes)
        -> std::expected<
            std::optional<std::vector<std::byte>>,
            ProjectStorageError> = 0;

    virtual auto write_atomic(
        std::string_view name,
        std::span<const std::byte> bytes)
        -> std::expected<void, ProjectStorageError> = 0;

    virtual auto remove(std::string_view name)
        -> std::expected<bool, ProjectStorageError> = 0;
};

enum class ImageProjectStoreErrorCode : std::uint8_t
{
    Storage,
    Codec,
    InvalidProjectId,
    NotFound,
    RevisionConflict,
    RevisionOverflow,
    IdentityMismatch,
};

struct ImageProjectStoreError
{
    ImageProjectStoreErrorCode code{};
    std::optional<ProjectStorageError> storage{};
    std::optional<ImageProjectCodecError> codec{};
    std::string detail{};

    auto operator==(const ImageProjectStoreError&) const
        -> bool = default;
};

class ImageProjectStore
{
public:
    ImageProjectStore(
        AtomicProjectStorage& storage,
        PresetHasher& hasher);

    [[nodiscard]] auto load_named(std::string_view project_id)
        -> std::expected<
            std::optional<core::ImageProject>,
            ImageProjectStoreError>;

    [[nodiscard]] auto save_named(
        const core::ImageProject& project,
        std::uint64_t expected_revision)
        -> std::expected<void, ImageProjectStoreError>;

    [[nodiscard]] auto rename_named(
        std::string_view project_id,
        std::string new_name)
        -> std::expected<
            core::ImageProject,
            ImageProjectStoreError>;

    [[nodiscard]] auto delete_named(std::string_view project_id)
        -> std::expected<bool, ImageProjectStoreError>;

    [[nodiscard]] auto load_active_draft()
        -> std::expected<
            std::optional<core::ImageProject>,
            ImageProjectStoreError>;

    [[nodiscard]] auto save_active_draft(
        const core::ImageProject& project)
        -> std::expected<void, ImageProjectStoreError>;

    [[nodiscard]] auto clear_active_draft()
        -> std::expected<bool, ImageProjectStoreError>;

private:
    AtomicProjectStorage& storage_;
    PresetHasher& hasher_;
};
} // namespace meccha::application
