#include <meccha/launcher/hash.hpp>
#include <meccha/launcher/owned_file_storage.hpp>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <winioctl.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace
{
using namespace meccha::launcher;
namespace fs = std::filesystem;

class TemporaryTree
{
public:
    TemporaryTree()
    {
        static auto sequence = std::uint64_t{};
        ++sequence;
        root = fs::temp_directory_path() /
               ("meccha-v2-owned-file-" +
                std::to_string(GetCurrentProcessId()) + "-" +
                std::to_string(GetTickCount64()) + "-" +
                std::to_string(sequence));
        fs::create_directories(root / "game");
        fs::create_directories(root / "ownership");
    }

    TemporaryTree(const TemporaryTree&) = delete;
    auto operator=(const TemporaryTree&) -> TemporaryTree& = delete;

    ~TemporaryTree()
    {
        std::error_code ignored{};
        fs::remove_all(root, ignored);
    }

    fs::path root{};
};

struct JunctionBuffer
{
    DWORD reparse_tag{};
    WORD reparse_data_length{};
    WORD reserved{};
    WORD substitute_name_offset{};
    WORD substitute_name_length{};
    WORD print_name_offset{};
    WORD print_name_length{};
    WCHAR path_buffer[4096]{};
};

auto create_junction(
    const fs::path& link,
    const fs::path& target) -> bool
{
    if (!fs::create_directory(link))
    {
        return false;
    }
    const auto absolute_target =
        fs::absolute(target).lexically_normal().wstring();
    const auto substitute = L"\\??\\" + absolute_target;
    if (substitute.size() + absolute_target.size() + 2U >
        std::size(JunctionBuffer{}.path_buffer))
    {
        return false;
    }

    JunctionBuffer buffer{};
    buffer.reparse_tag = IO_REPARSE_TAG_MOUNT_POINT;
    buffer.substitute_name_length = static_cast<WORD>(
        substitute.size() * sizeof(WCHAR));
    buffer.print_name_offset = static_cast<WORD>(
        buffer.substitute_name_length + sizeof(WCHAR));
    buffer.print_name_length = static_cast<WORD>(
        absolute_target.size() * sizeof(WCHAR));
    buffer.reparse_data_length = static_cast<WORD>(
        8U + buffer.substitute_name_length + sizeof(WCHAR) +
        buffer.print_name_length + sizeof(WCHAR));
    std::ranges::copy(
        substitute,
        buffer.path_buffer);
    std::ranges::copy(
        absolute_target,
        buffer.path_buffer + substitute.size() + 1U);

    const auto handle = CreateFileW(
        link.c_str(),
        GENERIC_WRITE,
        0,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT |
            FILE_FLAG_BACKUP_SEMANTICS,
        nullptr);
    if (handle == INVALID_HANDLE_VALUE)
    {
        fs::remove(link);
        return false;
    }
    DWORD returned{};
    const auto input_size =
        8U + buffer.reparse_data_length;
    const auto created = DeviceIoControl(
        handle,
        FSCTL_SET_REPARSE_POINT,
        &buffer,
        input_size,
        nullptr,
        0,
        &returned,
        nullptr);
    static_cast<void>(CloseHandle(handle));
    if (!created)
    {
        fs::remove(link);
        return false;
    }
    return true;
}

auto bytes(std::string_view value) -> std::vector<std::byte>
{
    const auto first =
        reinterpret_cast<const std::byte*>(value.data());
    return {first, first + value.size()};
}

auto expectation(
    std::string_view version,
    std::string_view payload) -> OwnedFileExpectation
{
    const auto payload_bytes = bytes(payload);
    return OwnedFileExpectation{
        std::string{version},
        sha256_bytes(std::as_bytes(std::span{version})).value(),
        ManifestFile{
            "dwmapi.dll",
            FileRole::Proxy,
            payload_bytes.size(),
            sha256_bytes(payload_bytes).value(),
        },
    };
}

auto write_bytes(const fs::path& path, std::string_view value) -> void
{
    std::ofstream output{path, std::ios::binary | std::ios::trunc};
    output.write(value.data(), static_cast<std::streamsize>(value.size()));
}

auto read_text(const fs::path& path) -> std::string
{
    std::ifstream input{path, std::ios::binary};
    return {
        std::istreambuf_iterator<char>{input},
        std::istreambuf_iterator<char>{}};
}

auto replace_phase(
    std::string receipt,
    std::string_view from,
    std::string_view to) -> std::string
{
    const auto position = receipt.find(from);
    if (position != std::string::npos)
    {
        receipt.replace(position, from.size(), to);
    }
    return receipt;
}

auto expect(bool condition, std::string_view message) -> bool
{
    if (!condition)
    {
        std::cerr << "FAIL owned_file_storage: " << message << '\n';
    }
    return condition;
}
} // namespace

auto main() -> int
{
    bool passed = true;

    TemporaryTree lifecycle{};
    const auto target = lifecycle.root / "game" / "dwmapi.dll";
    const auto record =
        lifecycle.root / "ownership" / "dwmapi.owner.json";
    Win32OwnedFileStore store{
        target,
        record,
        "dwmapi.dll",
        FileRole::Proxy};
    const auto first_expectation = expectation("1.0.0", "proxy-one");
    const auto first_payload = bytes("proxy-one");

    const auto missing = store.observe(first_expectation);
    passed &= expect(
        missing && *missing == ArtifactState::Missing,
        "a clean target was not reported as missing");

    const auto created = store.install(
        first_expectation,
        first_payload);
    passed &= expect(
        created && *created == OwnedFileInstallResult::Created &&
            read_text(target) == "proxy-one" && fs::exists(record),
        "fresh owned installation was not published");

    const auto owned = store.observe(first_expectation);
    passed &= expect(
        owned && *owned == ArtifactState::ExactOwned,
        "installed content was not classified as exact owned");

    const auto reused = store.install(
        first_expectation,
        first_payload);
    passed &= expect(
        reused && *reused == OwnedFileInstallResult::Reused,
        "exact owned content was rewritten");

    const auto second_expectation = expectation("2.0.0", "proxy-two");
    const auto second_payload = bytes("proxy-two");
    const auto previous = store.observe(second_expectation);
    passed &= expect(
        previous && *previous == ArtifactState::OwnedPrevious,
        "verified previous content was not replaceable");

    const auto replaced = store.install(
        second_expectation,
        second_payload);
    passed &= expect(
        replaced && *replaced == OwnedFileInstallResult::Replaced &&
            read_text(target) == "proxy-two",
        "owned update did not replace the target");

    const auto removed = store.remove_owned();
    passed &= expect(
        removed && *removed && !fs::exists(target) &&
            !fs::exists(record),
        "owned removal left the target or record");
    const auto removed_again = store.remove_owned();
    passed &= expect(
        removed_again && !*removed_again,
        "idempotent removal reported a mutation");

    TemporaryTree exact_unowned_tree{};
    const auto unowned_target =
        exact_unowned_tree.root / "game" / "dwmapi.dll";
    const auto unowned_record =
        exact_unowned_tree.root / "ownership" /
        "dwmapi.owner.json";
    write_bytes(unowned_target, "proxy-one");
    Win32OwnedFileStore unowned_store{
        unowned_target,
        unowned_record,
        "dwmapi.dll",
        FileRole::Proxy};
    const auto exact_unowned =
        unowned_store.observe(first_expectation);
    passed &= expect(
        exact_unowned &&
            *exact_unowned == ArtifactState::ExactUnowned,
        "matching unrecorded content was claimed as owned");
    const auto refused_unowned = unowned_store.install(
        first_expectation,
        first_payload);
    passed &= expect(
        !refused_unowned &&
            refused_unowned.error().code ==
                OwnedFileStoreErrorCode::Conflict &&
            read_text(unowned_target) == "proxy-one" &&
            !fs::exists(unowned_record),
        "an exact unowned file was claimed or modified");

    TemporaryTree tampered_tree{};
    const auto tampered_target =
        tampered_tree.root / "game" / "dwmapi.dll";
    const auto tampered_record =
        tampered_tree.root / "ownership" /
        "dwmapi.owner.json";
    Win32OwnedFileStore tampered_store{
        tampered_target,
        tampered_record,
        "dwmapi.dll",
        FileRole::Proxy};
    passed &= expect(
        tampered_store.install(
            first_expectation,
            first_payload).has_value(),
        "tamper fixture could not be installed");
    write_bytes(tampered_target, "user-change");
    const auto tampered = tampered_store.observe(first_expectation);
    passed &= expect(
        tampered &&
            *tampered == ArtifactState::Conflict,
        "modified owned content was trusted");
    const auto refused_tampered = tampered_store.install(
        second_expectation,
        second_payload);
    passed &= expect(
        !refused_tampered &&
            refused_tampered.error().code ==
                OwnedFileStoreErrorCode::Conflict &&
            read_text(tampered_target) == "user-change",
        "modified owned content was overwritten");
    const auto refused_remove = tampered_store.remove_owned();
    passed &= expect(
        !refused_remove &&
            refused_remove.error().code ==
                OwnedFileStoreErrorCode::Conflict &&
            fs::exists(tampered_target),
        "modified owned content was removed");

    TemporaryTree invalid_payload_tree{};
    Win32OwnedFileStore invalid_payload_store{
        invalid_payload_tree.root / "game" / "dwmapi.dll",
        invalid_payload_tree.root / "ownership" /
            "dwmapi.owner.json",
        "dwmapi.dll",
        FileRole::Proxy};
    const auto wrong_payload = bytes("wrong");
    const auto invalid_payload = invalid_payload_store.install(
        first_expectation,
        wrong_payload);
    passed &= expect(
        !invalid_payload &&
            invalid_payload.error().code ==
                OwnedFileStoreErrorCode::InvalidData &&
            !fs::exists(
                invalid_payload_tree.root / "game" /
                "dwmapi.dll"),
        "payload mismatch changed the filesystem");

    TemporaryTree read_only_observation_tree{};
    fs::remove_all(
        read_only_observation_tree.root / "ownership");
    Win32OwnedFileStore read_only_observation_store{
        read_only_observation_tree.root / "game" / "dwmapi.dll",
        read_only_observation_tree.root / "ownership" / "nested" /
            "dwmapi.owner.json",
        "dwmapi.dll",
        FileRole::Proxy};
    const auto read_only_observation =
        read_only_observation_store.observe(first_expectation);
    const auto read_only_removal =
        read_only_observation_store.remove_owned();
    passed &= expect(
        read_only_observation &&
            *read_only_observation == ArtifactState::Missing &&
            read_only_removal && !*read_only_removal &&
            !fs::exists(
                read_only_observation_tree.root / "ownership"),
        "read-only observation or no-op removal created metadata");

    TemporaryTree recovery_tree{};
    const auto recovery_target =
        recovery_tree.root / "game" / "dwmapi.dll";
    const auto recovery_record =
        recovery_tree.root / "ownership" /
        "dwmapi.owner.json";
    Win32OwnedFileStore recovery_store{
        recovery_target,
        recovery_record,
        "dwmapi.dll",
        FileRole::Proxy};
    passed &= expect(
        recovery_store.install(
            first_expectation,
            first_payload).has_value(),
        "recovery fixture could not be installed");
    auto atomic_receipt = recovery_record;
    atomic_receipt += ".meccha-next";
    write_bytes(atomic_receipt, read_text(recovery_record));
    const auto atomic_recovery_required =
        recovery_store.observe(first_expectation);
    const auto atomic_recovered = recovery_store.recover();
    passed &= expect(
        !atomic_recovery_required &&
            atomic_recovery_required.error().code ==
                OwnedFileStoreErrorCode::Conflict &&
            atomic_recovered && !fs::exists(atomic_receipt),
        "receipt observation mutated or ignored interrupted atomic "
        "metadata");
    const auto installing_receipt = replace_phase(
        read_text(recovery_record),
        R"("phase":"complete")",
        R"("phase":"installing")");
    write_bytes(recovery_record, installing_receipt);
    const auto recovery_required =
        recovery_store.observe(first_expectation);
    const auto recovered_install = recovery_store.recover();
    const auto recovered_owned =
        recovery_store.observe(first_expectation);
    passed &= expect(
        !recovery_required &&
            recovery_required.error().code ==
                OwnedFileStoreErrorCode::Conflict &&
            recovered_install && recovered_owned &&
            *recovered_owned == ArtifactState::ExactOwned,
        "post-publish installation interruption was not recovered");

    const auto removing_receipt = replace_phase(
        read_text(recovery_record),
        R"("phase":"complete")",
        R"("phase":"removing")");
    write_bytes(recovery_record, removing_receipt);
    const auto target_deleted = DeleteFileW(recovery_target.c_str());
    const auto recovered_remove = recovery_store.recover();
    const auto recovered_missing =
        recovery_store.observe(first_expectation);
    passed &= expect(
        target_deleted && recovered_remove &&
            recovered_missing &&
            *recovered_missing == ArtifactState::Missing &&
            !fs::exists(recovery_record),
        "post-delete removal interruption was not recovered");

    TemporaryTree reparse_tree{};
    const auto external =
        reparse_tree.root / "external.dll";
    const auto reparse_target =
        reparse_tree.root / "game" / "dwmapi.dll";
    write_bytes(external, "external");
    const auto linked = CreateSymbolicLinkW(
        reparse_target.c_str(),
        external.c_str(),
        SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE);
    if (linked)
    {
        Win32OwnedFileStore reparse_store{
            reparse_target,
            reparse_tree.root / "ownership" /
                "dwmapi.owner.json",
            "dwmapi.dll",
            FileRole::Proxy};
        const auto refused_reparse =
            reparse_store.observe(first_expectation);
        passed &= expect(
            !refused_reparse &&
                refused_reparse.error().code ==
                    OwnedFileStoreErrorCode::Conflict &&
                read_text(external) == "external",
            "target reparse point was followed or accepted");
    }
    else
    {
        std::cout
            << "SKIP owned_file_storage reparse fixture: Windows "
               "symbolic-link creation is unavailable\n";
    }

    TemporaryTree junction_tree{};
    const auto junction_target =
        junction_tree.root / "real-ownership";
    const auto junction =
        junction_tree.root / "ownership-junction";
    fs::create_directory(junction_target);
    const auto junction_created =
        create_junction(junction, junction_target);
    Win32OwnedFileStore junction_store{
        junction_tree.root / "game" / "dwmapi.dll",
        junction / "dwmapi.owner.json",
        "dwmapi.dll",
        FileRole::Proxy};
    const auto refused_junction =
        junction_created
            ? junction_store.observe(first_expectation)
            : std::expected<
                  ArtifactState,
                  OwnedFileStoreError>{
                  std::unexpected(OwnedFileStoreError{
                      OwnedFileStoreErrorCode::Io,
                      "junction fixture creation failed"})};
    passed &= expect(
        junction_created && !refused_junction &&
            refused_junction.error().code ==
                OwnedFileStoreErrorCode::Conflict &&
            fs::is_empty(junction_target),
        "ownership-directory junction was followed or accepted");

    if (passed)
    {
        std::cout << "PASS owned_file_storage\n";
    }
    return passed ? 0 : 1;
}
