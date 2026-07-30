#include <meccha/launcher/elevated_child.hpp>

#include <meccha/launcher/application_win32.hpp>
#include <meccha/launcher/managed_loader.hpp>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <sddl.h>
#include <shlobj.h>

#include <cstddef>
#include <expected>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace meccha::launcher
{
namespace
{
namespace fs = std::filesystem;

auto child_error(
    ElevatedLoaderMutationErrorCode code,
    std::string detail)
    -> std::unexpected<ElevatedLoaderMutationError>
{
    return std::unexpected(ElevatedLoaderMutationError{
        code,
        std::move(detail),
    });
}

auto windows_error(
    std::string operation,
    DWORD code = GetLastError())
    -> std::unexpected<ElevatedLoaderMutationError>
{
    return child_error(
        ElevatedLoaderMutationErrorCode::Io,
        std::move(operation) +
            " failed with Windows error " +
            std::to_string(code) + ".");
}

class UniqueHandle
{
public:
    UniqueHandle() = default;

    explicit UniqueHandle(HANDLE value)
        : value_(value)
    {
    }

    UniqueHandle(const UniqueHandle&) = delete;
    auto operator=(const UniqueHandle&)
        -> UniqueHandle& = delete;

    ~UniqueHandle()
    {
        if (value_ != nullptr &&
            value_ != INVALID_HANDLE_VALUE)
        {
            static_cast<void>(CloseHandle(value_));
        }
    }

    [[nodiscard]] auto get() const -> HANDLE
    {
        return value_;
    }

    [[nodiscard]] explicit operator bool() const
    {
        return value_ != nullptr &&
               value_ != INVALID_HANDLE_VALUE;
    }

private:
    HANDLE value_{};
};

class UniqueLocalMemory
{
public:
    explicit UniqueLocalMemory(void* value)
        : value_(value)
    {
    }

    UniqueLocalMemory(const UniqueLocalMemory&) = delete;
    auto operator=(const UniqueLocalMemory&)
        -> UniqueLocalMemory& = delete;

    ~UniqueLocalMemory()
    {
        if (value_ != nullptr)
        {
            static_cast<void>(LocalFree(value_));
        }
    }

private:
    void* value_{};
};

class UniqueKnownFolderPath
{
public:
    explicit UniqueKnownFolderPath(PWSTR value)
        : value_(value)
    {
    }

    UniqueKnownFolderPath(const UniqueKnownFolderPath&) = delete;
    auto operator=(const UniqueKnownFolderPath&)
        -> UniqueKnownFolderPath& = delete;

    ~UniqueKnownFolderPath()
    {
        if (value_ != nullptr)
        {
            CoTaskMemFree(value_);
        }
    }

private:
    PWSTR value_{};
};

auto token_user_sid(HANDLE token)
    -> std::expected<
        std::wstring,
        ElevatedLoaderMutationError>
{
    DWORD required{};
    static_cast<void>(GetTokenInformation(
        token,
        TokenUser,
        nullptr,
        0U,
        &required));
    if (required == 0U ||
        GetLastError() != ERROR_INSUFFICIENT_BUFFER)
    {
        return windows_error(
            "GetTokenInformation(parent SID size)");
    }
    auto buffer = std::vector<std::byte>(required);
    if (!GetTokenInformation(
            token,
            TokenUser,
            buffer.data(),
            required,
            &required))
    {
        return windows_error(
            "GetTokenInformation(parent SID)");
    }
    const auto* user =
        reinterpret_cast<const TOKEN_USER*>(
            buffer.data());
    LPWSTR raw_sid{};
    if (!ConvertSidToStringSidW(
            user->User.Sid,
            &raw_sid))
    {
        return windows_error(
            "ConvertSidToStringSidW(parent)");
    }
    auto owner = UniqueLocalMemory{raw_sid};
    return std::wstring{raw_sid};
}

auto plain_directory(fs::path path)
    -> std::expected<
        fs::path,
        ElevatedLoaderMutationError>
{
    path = path.lexically_normal();
    if (!path.is_absolute() ||
        path.root_path().empty())
    {
        return child_error(
            ElevatedLoaderMutationErrorCode::InvalidRequest,
            "The elevated child directory is not absolute.");
    }
    auto current = path.root_path();
    for (const auto& component : path.relative_path())
    {
        current /= component;
        const auto attributes =
            GetFileAttributesW(current.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES ||
            !(attributes & FILE_ATTRIBUTE_DIRECTORY) ||
            (attributes & FILE_ATTRIBUTE_REPARSE_POINT))
        {
            return child_error(
                ElevatedLoaderMutationErrorCode::InvalidRequest,
                "The elevated child directory is unavailable or "
                "traverses a reparse point.");
        }
    }
    return path;
}

auto payload_error(
    std::string operation,
    std::string detail)
    -> std::unexpected<ElevatedLoaderMutationError>
{
    return child_error(
        ElevatedLoaderMutationErrorCode::Payload,
        std::move(operation) + ": " + std::move(detail));
}
} // namespace

auto Win32ElevatedBrokerChildEnvironment::
    temporary_directory()
    -> std::expected<
        fs::path,
        ElevatedLoaderMutationError>
{
    auto buffer = std::vector<wchar_t>(32768U);
    const auto size = GetTempPathW(
        static_cast<DWORD>(buffer.size()),
        buffer.data());
    if (size == 0U || size >= buffer.size())
    {
        return windows_error("GetTempPathW");
    }
    return plain_directory(
        fs::path{
            std::wstring_view{buffer.data(), size}});
}

auto Win32ElevatedBrokerChildEnvironment::
    original_user_local_app_data(
        const ElevatedBrokerParentIdentity& parent)
    -> std::expected<
        fs::path,
        ElevatedLoaderMutationError>
{
    if (parent.process_id == 0U ||
        parent.user_sid.empty())
    {
        return child_error(
            ElevatedLoaderMutationErrorCode::InvalidRequest,
            "The validated original-parent identity is empty.");
    }
    auto process = UniqueHandle{OpenProcess(
        PROCESS_QUERY_LIMITED_INFORMATION,
        FALSE,
        parent.process_id)};
    if (!process)
    {
        return windows_error(
            "OpenProcess(original user)");
    }
    DWORD session{};
    if (!ProcessIdToSessionId(
            parent.process_id,
            &session))
    {
        return windows_error(
            "ProcessIdToSessionId(original user)");
    }
    if (session != parent.session_id)
    {
        return child_error(
            ElevatedLoaderMutationErrorCode::InvalidRequest,
            "The original-parent session changed.");
    }
    HANDLE raw_token{};
    if (!OpenProcessToken(
            process.get(),
            TOKEN_QUERY | TOKEN_DUPLICATE,
            &raw_token))
    {
        return windows_error(
            "OpenProcessToken(original user)");
    }
    auto token = UniqueHandle{raw_token};
    const auto sid = token_user_sid(token.get());
    if (!sid)
    {
        return std::unexpected(sid.error());
    }
    if (_wcsicmp(
            sid->c_str(),
            parent.user_sid.c_str()) != 0)
    {
        return child_error(
            ElevatedLoaderMutationErrorCode::InvalidRequest,
            "The original-parent user SID changed.");
    }
    HANDLE raw_duplicate{};
    if (!DuplicateTokenEx(
            token.get(),
            TOKEN_QUERY,
            nullptr,
            SecurityImpersonation,
            TokenPrimary,
            &raw_duplicate))
    {
        return windows_error(
            "DuplicateTokenEx(original user)");
    }
    auto duplicate = UniqueHandle{raw_duplicate};
    PWSTR raw_path{};
    const auto known_folder = SHGetKnownFolderPath(
        FOLDERID_LocalAppData,
        KF_FLAG_DEFAULT,
        duplicate.get(),
        &raw_path);
    if (FAILED(known_folder) || raw_path == nullptr)
    {
        if (raw_path != nullptr)
        {
            CoTaskMemFree(raw_path);
        }
        return child_error(
            ElevatedLoaderMutationErrorCode::Io,
            "The original user's LocalAppData directory could "
            "not be resolved (HRESULT " +
                std::to_string(
                    static_cast<unsigned long>(
                        known_folder)) +
                ").");
    }
    auto owner = UniqueKnownFolderPath{raw_path};
    return plain_directory(fs::path{raw_path});
}

Win32EmbeddedElevatedLoaderRequestExecutor::
    Win32EmbeddedElevatedLoaderRequestExecutor(
        LauncherPackageSource& package_source,
        ElevatedBrokerChildEnvironment& environment,
        ElevatedLoaderMutationPlatform& mutation_platform)
    : package_source_(package_source),
      environment_(environment),
      mutation_platform_(mutation_platform)
{
}

auto Win32EmbeddedElevatedLoaderRequestExecutor::execute(
    const ElevatedLoaderMutationRequest& request,
    const ElevatedBrokerParentIdentity& parent_identity)
    -> std::expected<
        ElevatedLoaderMutationResult,
        ElevatedLoaderMutationError>
{
    const auto temporary =
        environment_.temporary_directory();
    if (!temporary)
    {
        return std::unexpected(temporary.error());
    }
    const auto mode =
        request.operation ==
                ElevatedLoaderOperation::Remove
            ? LauncherInvocationMode::Remove
            : LauncherInvocationMode::PrepareOnly;
    auto package = package_source_.load(
        *temporary,
        mode);
    if (!package)
    {
        return payload_error(
            "Embedded elevated package",
            package.error().detail);
    }
    if (!package->payload_source)
    {
        return payload_error(
            "Embedded elevated package",
            "the payload source is missing");
    }
    if (package->manifest_sha256 !=
        request.manifest_sha256)
    {
        return child_error(
            ElevatedLoaderMutationErrorCode::InvalidRequest,
            "The elevated request manifest does not match "
            "the independently loaded package.");
    }
    if (request.operation ==
        ElevatedLoaderOperation::Remove)
    {
        return execute_elevated_loader_mutation(
            request,
            package->manifest_sha256,
            nullptr,
            mutation_platform_);
    }
    const auto local_app_data =
        environment_.original_user_local_app_data(
            parent_identity);
    if (!local_app_data)
    {
        return std::unexpected(local_app_data.error());
    }
    const auto paths =
        make_launcher_data_paths(*local_app_data);
    if (!paths)
    {
        return child_error(
            ElevatedLoaderMutationErrorCode::InvalidRequest,
            paths.error().detail);
    }
    auto material = build_managed_loader_material(
        package->manifest,
        package->manifest_sha256,
        paths->runtime_root / "active",
        *package->payload_source);
    if (!material)
    {
        return payload_error(
            "Embedded elevated loader material",
            material.error().detail);
    }
    return execute_elevated_loader_mutation(
        request,
        package->manifest_sha256,
        std::addressof(*material),
        mutation_platform_);
}

auto run_embedded_elevated_broker_child(
    const ElevatedBrokerChildInvocation& invocation,
    LauncherPackageSource& package_source,
    ElevatedBrokerChildEnvironment& environment,
    ElevatedLoaderMutationPlatform& mutation_platform,
    ElevatedBrokerPeerValidator& peer_validator)
    -> std::expected<
        ElevatedBrokerParentIdentity,
        ElevatedLoaderMutationError>
{
    auto executor =
        Win32EmbeddedElevatedLoaderRequestExecutor{
            package_source,
            environment,
            mutation_platform,
        };
    return run_elevated_broker_child(
        invocation,
        executor,
        peer_validator);
}
} // namespace meccha::launcher
