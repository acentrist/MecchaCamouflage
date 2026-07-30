#include <meccha/launcher/elevated_loader.hpp>

#include <meccha/launcher/hash.hpp>

#include "owned_file_win32_io.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace meccha::launcher
{
namespace
{
namespace fs = std::filesystem;

auto error(
    ElevatedLoaderMutationErrorCode code,
    std::string detail)
    -> std::unexpected<ElevatedLoaderMutationError>
{
    return std::unexpected(ElevatedLoaderMutationError{
        code,
        std::move(detail),
    });
}

auto map_store_error(
    std::string_view file,
    const OwnedFileStoreError& cause)
    -> std::unexpected<ElevatedLoaderMutationError>
{
    const auto code =
        cause.code == OwnedFileStoreErrorCode::Conflict
            ? ElevatedLoaderMutationErrorCode::Precondition
            : ElevatedLoaderMutationErrorCode::Io;
    return error(
        code,
        std::string{file} + ": " + cause.detail);
}

auto valid_nonce(std::string_view value) -> bool
{
    return value.size() == 32U &&
           std::ranges::all_of(
               value,
               [](char character)
               {
                   return (character >= '0' &&
                           character <= '9') ||
                          (character >= 'a' &&
                           character <= 'f');
               });
}

auto empty_measurement(const FileMeasurement& value) -> bool
{
    return value == FileMeasurement{};
}

auto valid_file_request(
    ElevatedLoaderOperation operation,
    const ElevatedLoaderFileMutation& mutation) -> bool
{
    switch (mutation.action)
    {
    case ElevatedLoaderFileAction::Ignore:
        return operation == ElevatedLoaderOperation::Remove &&
               !mutation.expected_current &&
               empty_measurement(mutation.desired);
    case ElevatedLoaderFileAction::Verify:
        return operation == ElevatedLoaderOperation::Apply &&
               mutation.expected_current &&
               *mutation.expected_current == mutation.desired &&
               !empty_measurement(mutation.desired);
    case ElevatedLoaderFileAction::Install:
        return operation == ElevatedLoaderOperation::Apply &&
               !empty_measurement(mutation.desired);
    case ElevatedLoaderFileAction::Remove:
        return operation == ElevatedLoaderOperation::Remove &&
               mutation.expected_current &&
               empty_measurement(mutation.desired);
    }
    return false;
}

auto measurement(const OwnedFileExpectation& expectation)
    -> FileMeasurement
{
    return FileMeasurement{
        expectation.file.size,
        expectation.file.sha256,
    };
}

auto validate_material_file(
    std::string_view path,
    FileRole role,
    const OwnedFileExpectation& expectation,
    std::span<const std::byte> bytes,
    const Sha256Digest& manifest_sha256)
    -> std::expected<FileMeasurement, ElevatedLoaderMutationError>
{
    const auto digest = sha256_bytes(bytes);
    if (expectation.manifest_sha256 != manifest_sha256 ||
        expectation.file.path != path ||
        expectation.file.role != role ||
        expectation.file.size == 0U ||
        !digest ||
        bytes.size() != expectation.file.size ||
        *digest != expectation.file.sha256)
    {
        return error(
            ElevatedLoaderMutationErrorCode::Payload,
            "The elevated loader material does not match " +
                std::string{path} + '.');
    }
    return measurement(expectation);
}

auto path_equal(const fs::path& left, const fs::path& right)
    -> bool
{
    return _wcsicmp(
               left.lexically_normal().c_str(),
               right.lexically_normal().c_str()) == 0;
}

struct FileBinding
{
    std::string_view name{};
    fs::path path{};
    ElevatedLoaderFileMutation mutation{};
    std::span<const std::byte> payload{};
};

auto preflight(const FileBinding& file)
    -> std::expected<
        std::optional<FileMeasurement>,
        ElevatedLoaderMutationError>
{
    if (file.mutation.action == ElevatedLoaderFileAction::Ignore)
    {
        return std::nullopt;
    }
    const auto current = detail::measure_plain_file(file.path);
    if (!current)
    {
        return map_store_error(file.name, current.error());
    }
    if (*current != file.mutation.expected_current)
    {
        return error(
            ElevatedLoaderMutationErrorCode::Precondition,
            std::string{file.name} +
                " changed after the original-user preflight.");
    }
    if (file.mutation.action == ElevatedLoaderFileAction::Install)
    {
        const auto staging =
            detail::measure_plain_file(
                detail::owned_file_staging_path(file.path));
        if (!staging)
        {
            return map_store_error(file.name, staging.error());
        }
        if (*staging)
        {
            return error(
                ElevatedLoaderMutationErrorCode::Precondition,
                std::string{file.name} +
                    " has an unexpected staging file.");
        }
    }
    return *current;
}

auto revalidate(
    const FileBinding& file,
    const std::optional<FileMeasurement>& expected)
    -> std::expected<void, ElevatedLoaderMutationError>
{
    const auto current = detail::measure_plain_file(file.path);
    if (!current)
    {
        return map_store_error(file.name, current.error());
    }
    if (*current != expected)
    {
        return error(
            ElevatedLoaderMutationErrorCode::Precondition,
            std::string{file.name} +
                " changed during privileged execution.");
    }
    return {};
}

auto mutate(
    const FileBinding& file,
    const std::optional<FileMeasurement>& expected)
    -> std::expected<bool, ElevatedLoaderMutationError>
{
    if (file.mutation.action == ElevatedLoaderFileAction::Ignore ||
        file.mutation.action == ElevatedLoaderFileAction::Verify)
    {
        return false;
    }
    const auto unchanged = revalidate(file, expected);
    if (!unchanged)
    {
        return std::unexpected(unchanged.error());
    }

    if (file.mutation.action == ElevatedLoaderFileAction::Remove)
    {
        auto removed = detail::delete_plain_file(file.path);
        if (!removed)
        {
            return map_store_error(file.name, removed.error());
        }
        const auto verified = detail::measure_plain_file(file.path);
        if (!verified)
        {
            return map_store_error(file.name, verified.error());
        }
        if (*verified)
        {
            return error(
                ElevatedLoaderMutationErrorCode::Io,
                std::string{file.name} +
                    " remained after privileged removal.");
        }
        return true;
    }

    const auto staging =
        detail::owned_file_staging_path(file.path);
    auto written =
        detail::write_new_durable(staging, file.payload);
    if (!written)
    {
        return map_store_error(file.name, written.error());
    }
    const auto still_unchanged = revalidate(file, expected);
    if (!still_unchanged)
    {
        static_cast<void>(detail::delete_plain_file(staging));
        return std::unexpected(still_unchanged.error());
    }
    auto published = detail::publish_staged_file(
        staging,
        file.path,
        expected.has_value());
    if (!published)
    {
        static_cast<void>(detail::delete_plain_file(staging));
        return map_store_error(file.name, published.error());
    }
    const auto verified = detail::measure_plain_file(file.path);
    if (!verified)
    {
        return map_store_error(file.name, verified.error());
    }
    if (!*verified ||
        **verified != file.mutation.desired)
    {
        return error(
            ElevatedLoaderMutationErrorCode::Io,
            std::string{file.name} +
                " failed verification after privileged publication.");
    }
    return true;
}
} // namespace

auto Win32ElevatedLoaderMutationPlatform::game_running()
    -> std::expected<bool, ElevatedLoaderMutationError>
{
    const auto running = is_target_game_running();
    if (!running)
    {
        return error(
            ElevatedLoaderMutationErrorCode::Io,
            "The elevated broker could not determine whether the "
            "game is running: " + running.error().detail);
    }
    return *running;
}

auto Win32ElevatedLoaderMutationPlatform::validate_game_directory(
    const fs::path& path)
    -> std::expected<
        GameInstallation,
        ElevatedLoaderMutationError>
{
    const auto installation =
        meccha::launcher::validate_game_directory(path);
    if (!installation)
    {
        return error(
            ElevatedLoaderMutationErrorCode::GameDirectory,
            installation.error().detail);
    }
    return *installation;
}

auto execute_elevated_loader_mutation(
    const ElevatedLoaderMutationRequest& request,
    const Sha256Digest& accepted_manifest_sha256,
    const ManagedLoaderMaterial* material,
    ElevatedLoaderMutationPlatform& platform)
    -> std::expected<
        ElevatedLoaderMutationResult,
        ElevatedLoaderMutationError>
{
    const auto has_mutation =
        request.proxy.action ==
            ElevatedLoaderFileAction::Install ||
        request.proxy.action ==
            ElevatedLoaderFileAction::Remove ||
        request.override_file.action ==
            ElevatedLoaderFileAction::Install ||
        request.override_file.action ==
            ElevatedLoaderFileAction::Remove;
    if (request.schema_version !=
            ElevatedLoaderMutationSchemaVersion ||
        request.manifest_sha256 != accepted_manifest_sha256 ||
        !valid_nonce(request.request_nonce) ||
        !request.game_directory.is_absolute() ||
        request.game_directory.lexically_normal() !=
            request.game_directory ||
        request.game_directory.filename().empty() ||
        !valid_file_request(request.operation, request.proxy) ||
        !valid_file_request(
            request.operation,
            request.override_file) ||
        !has_mutation)
    {
        return error(
            ElevatedLoaderMutationErrorCode::InvalidRequest,
            "The elevated loader mutation request is invalid.");
    }

    const auto running = platform.game_running();
    if (!running)
    {
        return std::unexpected(running.error());
    }
    if (*running)
    {
        return error(
            ElevatedLoaderMutationErrorCode::GameRunning,
            "Elevated loader mutation is blocked while the game "
            "is running.");
    }
    const auto installation =
        platform.validate_game_directory(request.game_directory);
    if (!installation ||
        !path_equal(
            installation->binaries_directory,
            request.game_directory))
    {
        return error(
            ElevatedLoaderMutationErrorCode::GameDirectory,
            installation
                ? "The validated game binaries directory changed."
                : installation.error().detail);
    }
    const auto plain_game =
        detail::require_plain_directory_tree(
            request.game_directory);
    if (!plain_game)
    {
        return map_store_error(
            "game directory",
            plain_game.error());
    }

    auto proxy_payload = std::span<const std::byte>{};
    auto override_payload = std::span<const std::byte>{};
    if (request.operation == ElevatedLoaderOperation::Apply)
    {
        if (material == nullptr)
        {
            return error(
                ElevatedLoaderMutationErrorCode::Payload,
                "Apply requires independently rebuilt loader "
                "material.");
        }
        const auto proxy = validate_material_file(
            "dwmapi.dll",
            FileRole::Proxy,
            material->proxy,
            material->proxy_bytes,
            accepted_manifest_sha256);
        const auto override_file = validate_material_file(
            "override.txt",
            FileRole::Override,
            material->override_file,
            material->override_bytes,
            accepted_manifest_sha256);
        if (!proxy)
        {
            return std::unexpected(proxy.error());
        }
        if (!override_file)
        {
            return std::unexpected(override_file.error());
        }
        if ((request.proxy.action !=
                 ElevatedLoaderFileAction::Ignore &&
             request.proxy.desired != *proxy) ||
            (request.override_file.action !=
                 ElevatedLoaderFileAction::Ignore &&
             request.override_file.desired != *override_file))
        {
            return error(
                ElevatedLoaderMutationErrorCode::Payload,
                "The request does not match independently rebuilt "
                "loader material.");
        }
        proxy_payload = material->proxy_bytes;
        override_payload = material->override_bytes;
    }

    const std::array files{
        FileBinding{
            "dwmapi.dll",
            request.game_directory / "dwmapi.dll",
            request.proxy,
            proxy_payload,
        },
        FileBinding{
            "override.txt",
            request.game_directory / "override.txt",
            request.override_file,
            override_payload,
        },
    };
    std::array<std::optional<FileMeasurement>, 2> current{};
    for (std::size_t index = 0; index < files.size(); ++index)
    {
        const auto observed = preflight(files[index]);
        if (!observed)
        {
            return std::unexpected(observed.error());
        }
        current[index] = *observed;
    }

    auto result = ElevatedLoaderMutationResult{};
    const auto proxy_mutated = mutate(files[0], current[0]);
    if (!proxy_mutated)
    {
        return std::unexpected(proxy_mutated.error());
    }
    result.proxy_mutated = *proxy_mutated;
    const auto override_mutated = mutate(files[1], current[1]);
    if (!override_mutated)
    {
        return std::unexpected(override_mutated.error());
    }
    result.override_mutated = *override_mutated;
    return result;
}
} // namespace meccha::launcher
