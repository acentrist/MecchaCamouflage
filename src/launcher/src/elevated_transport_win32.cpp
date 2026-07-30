#include <meccha/launcher/elevated_transport.hpp>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commctrl.h>
#include <sddl.h>
#include <shellapi.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace meccha::launcher
{
namespace
{
constexpr std::size_t MaximumFrameBytes{64U * 1024U};
constexpr DWORD BrokerTimeoutMilliseconds{120000U};
constexpr std::wstring_view PipePrefix{
    LR"(\\.\pipe\MecchaCamouflage-v2-elevated-)"};
constexpr std::wstring_view InternalBrokerSwitch{
    L"--meccha-internal-elevated-broker-v1"};

auto transport_error(std::string detail)
    -> std::unexpected<ElevatedLoaderMutationError>
{
    return std::unexpected(ElevatedLoaderMutationError{
        ElevatedLoaderMutationErrorCode::Io,
        std::move(detail),
    });
}

auto invalid_transport(std::string detail)
    -> std::unexpected<ElevatedLoaderMutationError>
{
    return std::unexpected(ElevatedLoaderMutationError{
        ElevatedLoaderMutationErrorCode::InvalidRequest,
        std::move(detail),
    });
}

auto windows_error(
    std::string_view operation,
    DWORD code = GetLastError())
    -> std::unexpected<ElevatedLoaderMutationError>
{
    return transport_error(
        std::string{operation} +
        " failed with Windows error " +
        std::to_string(code) + ".");
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

auto pipe_name(std::string_view nonce)
    -> std::expected<
        std::wstring,
        ElevatedLoaderMutationError>
{
    if (!valid_nonce(nonce))
    {
        return invalid_transport(
            "The elevated broker nonce is invalid.");
    }
    auto result = std::wstring{PipePrefix};
    result.reserve(result.size() + nonce.size());
    for (const auto character : nonce)
    {
        result.push_back(static_cast<wchar_t>(character));
    }
    return result;
}

class UniqueHandle
{
public:
    UniqueHandle() = default;

    explicit UniqueHandle(HANDLE handle)
        : handle_(handle)
    {
    }

    UniqueHandle(const UniqueHandle&) = delete;
    auto operator=(const UniqueHandle&) -> UniqueHandle& = delete;

    UniqueHandle(UniqueHandle&& other) noexcept
        : handle_(std::exchange(other.handle_, nullptr))
    {
    }

    auto operator=(UniqueHandle&& other) noexcept
        -> UniqueHandle&
    {
        if (this != &other)
        {
            reset();
            handle_ = std::exchange(other.handle_, nullptr);
        }
        return *this;
    }

    ~UniqueHandle()
    {
        reset();
    }

    [[nodiscard]] auto get() const -> HANDLE
    {
        return handle_;
    }

    [[nodiscard]] explicit operator bool() const
    {
        return handle_ != nullptr &&
               handle_ != INVALID_HANDLE_VALUE;
    }

private:
    auto reset() -> void
    {
        if (*this)
        {
            static_cast<void>(CloseHandle(handle_));
        }
        handle_ = nullptr;
    }

    HANDLE handle_{};
};

class UniqueLocalMemory
{
public:
    UniqueLocalMemory() = default;

    explicit UniqueLocalMemory(void* value)
        : value_(value)
    {
    }

    UniqueLocalMemory(const UniqueLocalMemory&) = delete;
    auto operator=(const UniqueLocalMemory&)
        -> UniqueLocalMemory& = delete;

    UniqueLocalMemory(UniqueLocalMemory&& other) noexcept
        : value_(std::exchange(other.value_, nullptr))
    {
    }

    auto operator=(UniqueLocalMemory&& other) noexcept
        -> UniqueLocalMemory&
    {
        if (this != &other)
        {
            if (value_ != nullptr)
            {
                static_cast<void>(LocalFree(value_));
            }
            value_ = std::exchange(other.value_, nullptr);
        }
        return *this;
    }

    ~UniqueLocalMemory()
    {
        if (value_ != nullptr)
        {
            static_cast<void>(LocalFree(value_));
        }
    }

    [[nodiscard]] auto get() const -> void*
    {
        return value_;
    }

private:
    void* value_{};
};

auto process_user_sid(HANDLE process)
    -> std::expected<
        std::wstring,
        ElevatedLoaderMutationError>
{
    HANDLE raw_token{};
    if (!OpenProcessToken(
            process,
            TOKEN_QUERY,
            &raw_token))
    {
        return windows_error("OpenProcessToken");
    }
    auto token = UniqueHandle{raw_token};
    DWORD required{};
    static_cast<void>(GetTokenInformation(
        token.get(),
        TokenUser,
        nullptr,
        0U,
        &required));
    if (required == 0U ||
        GetLastError() != ERROR_INSUFFICIENT_BUFFER)
    {
        return windows_error("GetTokenInformation(size)");
    }
    auto buffer = std::vector<std::byte>{};
    buffer.resize(required);
    if (!GetTokenInformation(
            token.get(),
            TokenUser,
            buffer.data(),
            required,
            &required))
    {
        return windows_error("GetTokenInformation");
    }
    const auto* token_user =
        reinterpret_cast<const TOKEN_USER*>(buffer.data());
    LPWSTR raw_sid{};
    if (!ConvertSidToStringSidW(
            token_user->User.Sid,
            &raw_sid))
    {
        return windows_error("ConvertSidToStringSidW");
    }
    auto sid_memory = UniqueLocalMemory{raw_sid};
    return std::wstring{raw_sid};
}

auto process_image(HANDLE process)
    -> std::expected<
        std::wstring,
        ElevatedLoaderMutationError>
{
    auto buffer = std::wstring(32768U, L'\0');
    auto size = static_cast<DWORD>(buffer.size());
    if (!QueryFullProcessImageNameW(
            process,
            0U,
            buffer.data(),
            &size) ||
        size == 0U)
    {
        return windows_error("QueryFullProcessImageNameW");
    }
    buffer.resize(size);
    return buffer;
}

auto same_executable(HANDLE process)
    -> std::expected<bool, ElevatedLoaderMutationError>
{
    const auto current =
        process_image(GetCurrentProcess());
    if (!current)
    {
        return std::unexpected(current.error());
    }
    const auto peer = process_image(process);
    if (!peer)
    {
        return std::unexpected(peer.error());
    }
    return current->size() == peer->size() &&
           _wcsicmp(
               current->c_str(),
               peer->c_str()) == 0;
}

auto process_session(std::uint32_t process_id)
    -> std::expected<
        std::uint32_t,
        ElevatedLoaderMutationError>
{
    DWORD session{};
    if (!ProcessIdToSessionId(process_id, &session))
    {
        return windows_error("ProcessIdToSessionId");
    }
    return session;
}

auto process_elevated(HANDLE process)
    -> std::expected<bool, ElevatedLoaderMutationError>
{
    HANDLE raw_token{};
    if (!OpenProcessToken(
            process,
            TOKEN_QUERY,
            &raw_token))
    {
        return windows_error(
            "OpenProcessToken(elevation)");
    }
    auto token = UniqueHandle{raw_token};
    TOKEN_ELEVATION elevation{};
    DWORD size{};
    if (!GetTokenInformation(
            token.get(),
            TokenElevation,
            &elevation,
            sizeof(elevation),
            &size) ||
        size != sizeof(elevation))
    {
        return windows_error(
            "GetTokenInformation(elevation)");
    }
    return elevation.TokenIsElevated != 0U;
}

auto open_peer_process(std::uint32_t process_id)
    -> std::expected<
        UniqueHandle,
        ElevatedLoaderMutationError>
{
    auto process = UniqueHandle{OpenProcess(
        PROCESS_QUERY_LIMITED_INFORMATION |
            SYNCHRONIZE,
        FALSE,
        process_id)};
    if (!process)
    {
        return windows_error("OpenProcess(peer)");
    }
    return process;
}

struct PipeSecurity
{
    SECURITY_ATTRIBUTES attributes{};
    UniqueLocalMemory descriptor{};
};

auto make_pipe_security()
    -> std::expected<
        PipeSecurity,
        ElevatedLoaderMutationError>
{
    const auto sid =
        process_user_sid(GetCurrentProcess());
    if (!sid)
    {
        return std::unexpected(sid.error());
    }
    const auto sddl =
        std::wstring{
            L"D:P"
            L"(A;;GA;;;SY)"
            L"(A;;GA;;;BA)"
            L"(A;;GA;;;"} +
        *sid + L")";
    PSECURITY_DESCRIPTOR raw_descriptor{};
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
            sddl.c_str(),
            SDDL_REVISION_1,
            &raw_descriptor,
            nullptr))
    {
        return windows_error(
            "ConvertStringSecurityDescriptorToSecurityDescriptorW");
    }
    auto result = PipeSecurity{};
    result.descriptor =
        UniqueLocalMemory{raw_descriptor};
    result.attributes.nLength =
        sizeof(result.attributes);
    result.attributes.lpSecurityDescriptor =
        result.descriptor.get();
    result.attributes.bInheritHandle = FALSE;
    return result;
}

auto wait_for_io(
    HANDLE io_handle,
    OVERLAPPED& operation,
    HANDLE peer_wait_handle,
    std::string_view name)
    -> std::expected<DWORD, ElevatedLoaderMutationError>
{
    const std::array handles{
        operation.hEvent,
        peer_wait_handle,
    };
    const auto count =
        peer_wait_handle == nullptr ? 1U : 2U;
    const auto wait = WaitForMultipleObjects(
        count,
        handles.data(),
        FALSE,
        BrokerTimeoutMilliseconds);
    if (wait == WAIT_OBJECT_0)
    {
        DWORD transferred{};
        if (!GetOverlappedResult(
                io_handle,
                &operation,
                &transferred,
                FALSE))
        {
            return windows_error(
                std::string{name} + " completion");
        }
        return transferred;
    }
    static_cast<void>(CancelIoEx(io_handle, &operation));
    if (count == 2U &&
        wait == WAIT_OBJECT_0 + 1U)
    {
        return transport_error(
            std::string{name} +
            " failed because the peer process exited.");
    }
    if (wait == WAIT_TIMEOUT)
    {
        return transport_error(
            std::string{name} + " timed out.");
    }
    return windows_error(
        std::string{name} + " wait");
}

auto write_exact(
    HANDLE pipe,
    std::span<const std::byte> bytes,
    HANDLE peer_wait_handle)
    -> std::expected<void, ElevatedLoaderMutationError>
{
    std::size_t offset{};
    while (offset < bytes.size())
    {
        auto event = UniqueHandle{
            CreateEventW(nullptr, TRUE, FALSE, nullptr)};
        if (!event)
        {
            return windows_error("CreateEventW(write)");
        }
        OVERLAPPED operation{};
        operation.hEvent = event.get();
        const auto remaining = bytes.size() - offset;
        const auto count = static_cast<DWORD>(
            std::min<std::size_t>(
                remaining,
                std::numeric_limits<DWORD>::max()));
        DWORD transferred{};
        if (!WriteFile(
                pipe,
                bytes.data() + offset,
                count,
                &transferred,
                &operation))
        {
            const auto error = GetLastError();
            if (error != ERROR_IO_PENDING)
            {
                return windows_error(
                    "Elevated broker write",
                    error);
            }
            const auto completed = wait_for_io(
                pipe,
                operation,
                peer_wait_handle,
                "Elevated broker write");
            if (!completed)
            {
                return std::unexpected(completed.error());
            }
            transferred = *completed;
        }
        if (transferred == 0U)
        {
            return transport_error(
                "Elevated broker write made no progress.");
        }
        offset += transferred;
    }
    return {};
}

auto read_exact(
    HANDLE pipe,
    std::span<std::byte> bytes,
    HANDLE peer_wait_handle)
    -> std::expected<void, ElevatedLoaderMutationError>
{
    std::size_t offset{};
    while (offset < bytes.size())
    {
        auto event = UniqueHandle{
            CreateEventW(nullptr, TRUE, FALSE, nullptr)};
        if (!event)
        {
            return windows_error("CreateEventW(read)");
        }
        OVERLAPPED operation{};
        operation.hEvent = event.get();
        const auto remaining = bytes.size() - offset;
        const auto count = static_cast<DWORD>(
            std::min<std::size_t>(
                remaining,
                std::numeric_limits<DWORD>::max()));
        DWORD transferred{};
        if (!ReadFile(
                pipe,
                bytes.data() + offset,
                count,
                &transferred,
                &operation))
        {
            const auto error = GetLastError();
            if (error != ERROR_IO_PENDING)
            {
                return windows_error(
                    "Elevated broker read",
                    error);
            }
            const auto completed = wait_for_io(
                pipe,
                operation,
                peer_wait_handle,
                "Elevated broker read");
            if (!completed)
            {
                return std::unexpected(completed.error());
            }
            transferred = *completed;
        }
        if (transferred == 0U)
        {
            return transport_error(
                "Elevated broker read reached an unexpected end.");
        }
        offset += transferred;
    }
    return {};
}

auto write_frame(
    HANDLE pipe,
    std::span<const std::byte> bytes,
    HANDLE peer_wait_handle)
    -> std::expected<void, ElevatedLoaderMutationError>
{
    if (bytes.empty() || bytes.size() > MaximumFrameBytes)
    {
        return invalid_transport(
            "The elevated broker frame size is invalid.");
    }
    const auto size =
        static_cast<std::uint32_t>(bytes.size());
    const std::array prefix{
        static_cast<std::byte>(size),
        static_cast<std::byte>(size >> 8U),
        static_cast<std::byte>(size >> 16U),
        static_cast<std::byte>(size >> 24U),
    };
    const auto wrote_prefix =
        write_exact(pipe, prefix, peer_wait_handle);
    if (!wrote_prefix)
    {
        return std::unexpected(wrote_prefix.error());
    }
    return write_exact(pipe, bytes, peer_wait_handle);
}

auto read_frame(
    HANDLE pipe,
    HANDLE peer_wait_handle)
    -> std::expected<
        std::vector<std::byte>,
        ElevatedLoaderMutationError>
{
    auto prefix = std::array<std::byte, 4>{};
    const auto read_prefix =
        read_exact(pipe, prefix, peer_wait_handle);
    if (!read_prefix)
    {
        return std::unexpected(read_prefix.error());
    }
    const auto size =
        std::to_integer<std::uint32_t>(prefix[0]) |
        (std::to_integer<std::uint32_t>(prefix[1]) << 8U) |
        (std::to_integer<std::uint32_t>(prefix[2]) << 16U) |
        (std::to_integer<std::uint32_t>(prefix[3]) << 24U);
    if (size == 0U || size > MaximumFrameBytes)
    {
        return invalid_transport(
            "The elevated broker frame length is invalid.");
    }
    auto bytes = std::vector<std::byte>{};
    bytes.resize(size);
    const auto read_body =
        read_exact(pipe, bytes, peer_wait_handle);
    if (!read_body)
    {
        return std::unexpected(read_body.error());
    }
    return bytes;
}

auto connect_server(
    HANDLE pipe,
    HANDLE child_wait_handle)
    -> std::expected<void, ElevatedLoaderMutationError>
{
    auto event = UniqueHandle{
        CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    if (!event)
    {
        return windows_error("CreateEventW(connect)");
    }
    OVERLAPPED operation{};
    operation.hEvent = event.get();
    if (ConnectNamedPipe(pipe, &operation))
    {
        return {};
    }
    const auto error = GetLastError();
    if (error == ERROR_PIPE_CONNECTED)
    {
        return {};
    }
    if (error != ERROR_IO_PENDING)
    {
        return windows_error(
            "ConnectNamedPipe",
            error);
    }
    const auto connected = wait_for_io(
        pipe,
        operation,
        child_wait_handle,
        "Elevated broker connection");
    if (!connected)
    {
        return std::unexpected(connected.error());
    }
    return {};
}

auto create_server_pipe(std::wstring_view name)
    -> std::expected<UniqueHandle, ElevatedLoaderMutationError>
{
    auto security = make_pipe_security();
    if (!security)
    {
        return std::unexpected(security.error());
    }
    auto pipe = UniqueHandle{CreateNamedPipeW(
        std::wstring{name}.c_str(),
        PIPE_ACCESS_DUPLEX |
            FILE_FLAG_FIRST_PIPE_INSTANCE |
            FILE_FLAG_OVERLAPPED,
        PIPE_TYPE_BYTE |
            PIPE_READMODE_BYTE |
            PIPE_WAIT |
            PIPE_REJECT_REMOTE_CLIENTS,
        1U,
        static_cast<DWORD>(MaximumFrameBytes + 4U),
        static_cast<DWORD>(MaximumFrameBytes + 4U),
        0U,
        &security->attributes)};
    if (!pipe)
    {
        return windows_error("CreateNamedPipeW");
    }
    return pipe;
}

auto connect_child_pipe(std::wstring_view name)
    -> std::expected<UniqueHandle, ElevatedLoaderMutationError>
{
    auto pipe = UniqueHandle{CreateFileW(
        std::wstring{name}.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        0U,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_OVERLAPPED |
            SECURITY_SQOS_PRESENT |
            SECURITY_IDENTIFICATION,
        nullptr)};
    if (!pipe)
    {
        return windows_error(
            "CreateFileW(elevated broker pipe)");
    }
    return pipe;
}

auto native_handle(const ElevatedBrokerChildProcess& process)
    -> std::expected<HANDLE, ElevatedLoaderMutationError>
{
    const auto value = process.native_wait_handle();
    if (value == 0U)
    {
        return invalid_transport(
            "The elevated child process handle is invalid.");
    }
    return reinterpret_cast<HANDLE>(value);
}

auto action_label(
    ElevatedLoaderFileAction action) -> std::wstring_view
{
    switch (action)
    {
    case ElevatedLoaderFileAction::Ignore:
        return L"leave unchanged";
    case ElevatedLoaderFileAction::Verify:
        return L"verify only";
    case ElevatedLoaderFileAction::Install:
        return L"create or replace after hash checks";
    case ElevatedLoaderFileAction::Remove:
        return L"remove only after ownership/hash checks";
    }
    return L"invalid";
}

auto confirm_elevation(
    const ElevatedBrokerChildLaunchRequest& request)
    -> std::expected<void, ElevatedLoaderMutationError>
{
    if (!request.game_directory.is_absolute() ||
        request.game_directory.lexically_normal() !=
            request.game_directory)
    {
        return invalid_transport(
            "The UAC explanation has an invalid game directory.");
    }
    auto content =
        std::wstring{
            L"MecchaCamouflage needs administrator permission "
            L"because this game directory is not writable:\n\n"} +
        request.game_directory.native() +
        L"\n\nOnly these two game-directory files are in scope:\n"
        L"• dwmapi.dll — " +
        std::wstring{action_label(request.proxy_action)} +
        L"\n• override.txt — " +
        std::wstring{action_label(request.override_action)} +
        L"\n\nThe elevated process revalidates the game, embedded "
        L"payload, current hashes, and request nonce. It does not "
        L"change Windows Defender, the firewall, Steam, or any "
        L"unrelated game file.";
    int selected{};
    const auto dialog = TaskDialog(
        nullptr,
        nullptr,
        L"MecchaCamouflage",
        L"Administrator permission is required",
        content.c_str(),
        TDCBF_OK_BUTTON | TDCBF_CANCEL_BUTTON,
        TD_INFORMATION_ICON,
        &selected);
    if (FAILED(dialog))
    {
        return invalid_transport(
            "The UAC scope explanation could not be displayed.");
    }
    if (selected != IDOK)
    {
        return transport_error(
            "The UAC request was cancelled.");
    }
    return {};
}

class NativeElevatedBrokerChildProcess final
    : public ElevatedBrokerChildProcess
{
public:
    NativeElevatedBrokerChildProcess(
        UniqueHandle process,
        std::uint32_t process_id)
        : process_(std::move(process)),
          process_id_(process_id)
    {
    }

    auto process_id() const -> std::uint32_t override
    {
        return process_id_;
    }

    auto native_wait_handle() const
        -> std::uintptr_t override
    {
        return reinterpret_cast<std::uintptr_t>(
            process_.get());
    }

private:
    UniqueHandle process_{};
    std::uint32_t process_id_{};
};

class ServerConnection
{
public:
    explicit ServerConnection(HANDLE pipe)
        : pipe_(pipe)
    {
    }

    ServerConnection(const ServerConnection&) = delete;
    auto operator=(const ServerConnection&)
        -> ServerConnection& = delete;

    ~ServerConnection()
    {
        if (pipe_ != nullptr &&
            pipe_ != INVALID_HANDLE_VALUE)
        {
            static_cast<void>(DisconnectNamedPipe(pipe_));
        }
    }

private:
    HANDLE pipe_{};
};

class RestrictedMutationRequestExecutor final
    : public ElevatedBrokerRequestExecutor
{
public:
    RestrictedMutationRequestExecutor(
        Sha256Digest accepted_manifest_sha256,
        const ManagedLoaderMaterial* material,
        ElevatedLoaderMutationPlatform& mutation_platform)
        : accepted_manifest_sha256_(
              accepted_manifest_sha256),
          material_(material),
          mutation_platform_(mutation_platform)
    {
    }

    auto execute(
        const ElevatedLoaderMutationRequest& request,
        const ElevatedBrokerParentIdentity& parent_identity)
        -> std::expected<
            ElevatedLoaderMutationResult,
            ElevatedLoaderMutationError> override
    {
        static_cast<void>(parent_identity);
        return execute_elevated_loader_mutation(
            request,
            accepted_manifest_sha256_,
            material_,
            mutation_platform_);
    }

private:
    Sha256Digest accepted_manifest_sha256_{};
    const ManagedLoaderMaterial* material_{};
    ElevatedLoaderMutationPlatform& mutation_platform_;
};
} // namespace

auto Win32RunAsElevatedBrokerChildLauncher::launch(
    const ElevatedBrokerChildLaunchRequest& request)
    -> std::expected<
        std::unique_ptr<ElevatedBrokerChildProcess>,
        ElevatedLoaderMutationError>
{
    const auto confirmed = confirm_elevation(request);
    if (!confirmed)
    {
        return std::unexpected(confirmed.error());
    }
    const auto parameters =
        format_elevated_broker_child_parameters(
            request.invocation);
    if (!parameters)
    {
        return std::unexpected(parameters.error());
    }
    const auto executable =
        process_image(GetCurrentProcess());
    if (!executable)
    {
        return std::unexpected(executable.error());
    }
    SHELLEXECUTEINFOW launch{};
    launch.cbSize = sizeof(launch);
    launch.fMask =
        SEE_MASK_NOCLOSEPROCESS |
        SEE_MASK_NOASYNC |
        SEE_MASK_FLAG_NO_UI;
    launch.lpVerb = L"runas";
    launch.lpFile = executable->c_str();
    launch.lpParameters = parameters->c_str();
    launch.nShow = SW_HIDE;
    if (!ShellExecuteExW(&launch))
    {
        const auto error = GetLastError();
        if (error == ERROR_CANCELLED)
        {
            return transport_error(
                "The UAC request was cancelled.");
        }
        return windows_error(
            "ShellExecuteExW(runas)",
            error);
    }
    auto process = UniqueHandle{launch.hProcess};
    if (!process)
    {
        return invalid_transport(
            "ShellExecuteExW did not return the elevated "
            "child process handle.");
    }
    const auto process_id = GetProcessId(process.get());
    if (process_id == 0U)
    {
        return windows_error(
            "GetProcessId(elevated child)");
    }
    return std::unique_ptr<ElevatedBrokerChildProcess>{
        std::make_unique<NativeElevatedBrokerChildProcess>(
            std::move(process),
            process_id)};
}

auto Win32ElevatedBrokerPeerValidator::
    validate_original_parent(
        std::uint32_t expected_process_id,
        std::uint32_t actual_process_id)
    -> std::expected<
        ElevatedBrokerParentIdentity,
        ElevatedLoaderMutationError>
{
    if (expected_process_id == 0U ||
        expected_process_id != actual_process_id)
    {
        return invalid_transport(
            "The original launcher PID does not match "
            "the named-pipe server.");
    }
    const auto process =
        open_peer_process(actual_process_id);
    if (!process)
    {
        return std::unexpected(process.error());
    }
    const auto image = same_executable(process->get());
    if (!image)
    {
        return std::unexpected(image.error());
    }
    if (!*image)
    {
        return invalid_transport(
            "The original launcher is not the same executable.");
    }
    const auto current_session =
        process_session(GetCurrentProcessId());
    const auto peer_session =
        process_session(actual_process_id);
    if (!current_session || !peer_session)
    {
        return std::unexpected(
            current_session
                ? peer_session.error()
                : current_session.error());
    }
    if (*current_session != *peer_session)
    {
        return invalid_transport(
            "The original launcher is in a different session.");
    }
    const auto sid = process_user_sid(process->get());
    if (!sid)
    {
        return std::unexpected(sid.error());
    }
    return ElevatedBrokerParentIdentity{
        actual_process_id,
        *peer_session,
        *sid,
    };
}

auto Win32ElevatedBrokerPeerValidator::
    validate_elevated_child(
        std::uint32_t expected_process_id,
        std::uint32_t actual_process_id)
    -> std::expected<
        void,
        ElevatedLoaderMutationError>
{
    if (expected_process_id == 0U ||
        expected_process_id != actual_process_id)
    {
        return invalid_transport(
            "The elevated child PID does not match "
            "the launched process.");
    }
    const auto process =
        open_peer_process(actual_process_id);
    if (!process)
    {
        return std::unexpected(process.error());
    }
    const auto image = same_executable(process->get());
    if (!image)
    {
        return std::unexpected(image.error());
    }
    if (!*image)
    {
        return invalid_transport(
            "The elevated child is not the same executable.");
    }
    const auto current_session =
        process_session(GetCurrentProcessId());
    const auto peer_session =
        process_session(actual_process_id);
    if (!current_session || !peer_session)
    {
        return std::unexpected(
            current_session
                ? peer_session.error()
                : current_session.error());
    }
    if (*current_session != *peer_session)
    {
        return invalid_transport(
            "The elevated child is in a different session.");
    }
    const auto elevated =
        process_elevated(process->get());
    if (!elevated)
    {
        return std::unexpected(elevated.error());
    }
    if (!*elevated)
    {
        return invalid_transport(
            "The broker child process is not elevated.");
    }
    return {};
}

Win32NamedPipeElevatedLoaderMutationClient::
    Win32NamedPipeElevatedLoaderMutationClient(
        ElevatedBrokerChildLauncher& child_launcher,
        ElevatedBrokerPeerValidator& peer_validator)
    : child_launcher_(child_launcher),
      peer_validator_(peer_validator)
{
}

auto Win32NamedPipeElevatedLoaderMutationClient::execute(
    const ElevatedLoaderMutationRequest& request)
    -> std::expected<
        ElevatedLoaderMutationResult,
        ElevatedLoaderMutationError>
{
    const auto encoded =
        encode_elevated_loader_request(request);
    if (!encoded)
    {
        return std::unexpected(encoded.error());
    }
    const auto name = pipe_name(request.request_nonce);
    if (!name)
    {
        return std::unexpected(name.error());
    }
    auto pipe = create_server_pipe(*name);
    if (!pipe)
    {
        return std::unexpected(pipe.error());
    }
    const auto parent_process_id = GetCurrentProcessId();
    auto child = child_launcher_.launch(
        ElevatedBrokerChildLaunchRequest{
            ElevatedBrokerChildInvocation{
                request.request_nonce,
                parent_process_id,
            },
            request.game_directory,
            request.operation,
            request.proxy.action,
            request.override_file.action,
        });
    if (!child || !*child)
    {
        return child
                   ? invalid_transport(
                         "The elevated child launcher returned no process.")
                   : std::unexpected(child.error());
    }
    const auto child_handle = native_handle(**child);
    if (!child_handle)
    {
        return std::unexpected(child_handle.error());
    }
    const auto connected =
        connect_server(pipe->get(), *child_handle);
    if (!connected)
    {
        return std::unexpected(connected.error());
    }
    auto connection =
        ServerConnection{pipe->get()};

    ULONG actual_process_id{};
    if (!GetNamedPipeClientProcessId(
            pipe->get(),
            &actual_process_id))
    {
        return windows_error(
            "GetNamedPipeClientProcessId");
    }
    const auto expected_process_id =
        (**child).process_id();
    if (actual_process_id != expected_process_id)
    {
        return invalid_transport(
            "The connected elevated child PID does not match "
            "the launched process.");
    }
    const auto validated =
        peer_validator_.validate_elevated_child(
            expected_process_id,
            actual_process_id);
    if (!validated)
    {
        return std::unexpected(validated.error());
    }
    const auto wrote = write_frame(
        pipe->get(),
        *encoded,
        *child_handle);
    if (!wrote)
    {
        return std::unexpected(wrote.error());
    }
    const auto response_bytes =
        read_frame(pipe->get(), *child_handle);
    if (!response_bytes)
    {
        return std::unexpected(response_bytes.error());
    }
    const auto response =
        decode_elevated_loader_response(*response_bytes);
    if (!response)
    {
        return std::unexpected(response.error());
    }
    if (response->request_nonce != request.request_nonce)
    {
        return invalid_transport(
            "The elevated child response nonce does not match.");
    }
    if (!response->outcome)
    {
        return std::unexpected(response->outcome.error());
    }
    return *response->outcome;
}

auto is_elevated_broker_child_invocation(
    std::span<const std::wstring_view> arguments) -> bool
{
    return !arguments.empty() &&
           arguments.front() == InternalBrokerSwitch;
}

auto parse_elevated_broker_child_invocation(
    std::span<const std::wstring_view> arguments)
    -> std::expected<
        ElevatedBrokerChildInvocation,
        ElevatedLoaderMutationError>
{
    if (arguments.size() != 3U ||
        arguments[0] != InternalBrokerSwitch ||
        arguments[1].size() != 32U)
    {
        return invalid_transport(
            "The internal elevated broker arguments are invalid.");
    }
    auto nonce = std::string{};
    nonce.reserve(arguments[1].size());
    for (const auto character : arguments[1])
    {
        if (character > 0x7f)
        {
            return invalid_transport(
                "The internal elevated broker nonce is invalid.");
        }
        nonce.push_back(static_cast<char>(character));
    }
    if (!valid_nonce(nonce) ||
        arguments[2].empty())
    {
        return invalid_transport(
            "The internal elevated broker arguments are invalid.");
    }
    auto process_id = std::uint32_t{};
    for (const auto character : arguments[2])
    {
        if (character < L'0' || character > L'9')
        {
            return invalid_transport(
                "The internal elevated broker PID is invalid.");
        }
        const auto digit =
            static_cast<std::uint32_t>(character - L'0');
        if (process_id >
            (std::numeric_limits<std::uint32_t>::max() -
             digit) /
                10U)
        {
            return invalid_transport(
                "The internal elevated broker PID is invalid.");
        }
        process_id = process_id * 10U + digit;
    }
    if (process_id == 0U)
    {
        return invalid_transport(
            "The internal elevated broker PID is invalid.");
    }
    return ElevatedBrokerChildInvocation{
        std::move(nonce),
        process_id,
    };
}

auto format_elevated_broker_child_parameters(
    const ElevatedBrokerChildInvocation& invocation)
    -> std::expected<
        std::wstring,
        ElevatedLoaderMutationError>
{
    if (!valid_nonce(invocation.request_nonce) ||
        invocation.parent_process_id == 0U)
    {
        return invalid_transport(
            "The internal elevated broker invocation is invalid.");
    }
    auto result = std::wstring{InternalBrokerSwitch};
    result.push_back(L' ');
    for (const auto character : invocation.request_nonce)
    {
        result.push_back(static_cast<wchar_t>(character));
    }
    result.push_back(L' ');
    result +=
        std::to_wstring(invocation.parent_process_id);
    return result;
}

auto run_elevated_broker_child(
    const ElevatedBrokerChildInvocation& invocation,
    ElevatedBrokerRequestExecutor& request_executor,
    ElevatedBrokerPeerValidator& peer_validator)
    -> std::expected<
        ElevatedBrokerParentIdentity,
        ElevatedLoaderMutationError>
{
    if (invocation.parent_process_id == 0U)
    {
        return invalid_transport(
            "The original launcher PID is invalid.");
    }
    const auto name = pipe_name(invocation.request_nonce);
    if (!name)
    {
        return std::unexpected(name.error());
    }
    auto parent_process = UniqueHandle{
        OpenProcess(
            SYNCHRONIZE |
                PROCESS_QUERY_LIMITED_INFORMATION,
            FALSE,
            invocation.parent_process_id)};
    if (!parent_process)
    {
        return windows_error(
            "OpenProcess(original launcher)");
    }
    auto pipe = connect_child_pipe(*name);
    if (!pipe)
    {
        return std::unexpected(pipe.error());
    }
    ULONG actual_process_id{};
    if (!GetNamedPipeServerProcessId(
            pipe->get(),
            &actual_process_id))
    {
        return windows_error(
            "GetNamedPipeServerProcessId");
    }
    if (actual_process_id !=
        invocation.parent_process_id)
    {
        return invalid_transport(
            "The connected pipe server PID does not match "
            "the original launcher.");
    }
    const auto parent_identity =
        peer_validator.validate_original_parent(
            invocation.parent_process_id,
            actual_process_id);
    if (!parent_identity)
    {
        return std::unexpected(parent_identity.error());
    }
    const auto request_bytes =
        read_frame(pipe->get(), parent_process.get());
    if (!request_bytes)
    {
        return std::unexpected(request_bytes.error());
    }
    const auto request =
        decode_elevated_loader_request(*request_bytes);
    auto outcome = std::expected<
        ElevatedLoaderMutationResult,
        ElevatedLoaderMutationError>{
        std::unexpected(ElevatedLoaderMutationError{
            ElevatedLoaderMutationErrorCode::InvalidRequest,
            "The elevated broker request is invalid.",
        })};
    if (request &&
        request->request_nonce == invocation.request_nonce)
    {
        outcome = request_executor.execute(
            *request,
            *parent_identity);
    }
    const auto response =
        encode_elevated_loader_response(
            ElevatedLoaderMutationResponse{
                invocation.request_nonce,
                std::move(outcome),
            });
    if (!response)
    {
        return std::unexpected(response.error());
    }
    const auto wrote = write_frame(
        pipe->get(),
        *response,
        parent_process.get());
    if (!wrote)
    {
        return std::unexpected(wrote.error());
    }
    return *parent_identity;
}

auto run_elevated_broker_child(
    const ElevatedBrokerChildInvocation& invocation,
    const Sha256Digest& accepted_manifest_sha256,
    const ManagedLoaderMaterial* material,
    ElevatedLoaderMutationPlatform& mutation_platform,
    ElevatedBrokerPeerValidator& peer_validator)
    -> std::expected<
        ElevatedBrokerParentIdentity,
        ElevatedLoaderMutationError>
{
    auto executor = RestrictedMutationRequestExecutor{
        accepted_manifest_sha256,
        material,
        mutation_platform,
    };
    return run_elevated_broker_child(
        invocation,
        executor,
        peer_validator);
}
} // namespace meccha::launcher
