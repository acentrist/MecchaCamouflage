#include <meccha/launcher/runtime_storage.hpp>

#include <meccha/build_identity.hpp>
#include <meccha/launcher/manifest.hpp>

#include <glaze/glaze.hpp>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <system_error>
#include <unordered_set>
#include <utility>
#include <vector>

namespace meccha::launcher
{
namespace detail
{
struct RawRuntimeRecord
{
    std::uint32_t schema_version{};
    std::string product{};
    std::string state{};
    std::string manifest_sha256{};
    std::string manifest_json{};
};

struct RawRuntimeJournal
{
    std::uint32_t schema_version{};
    std::string phase{};
    std::optional<std::string> previous_manifest_sha256{};
    std::string next_manifest_sha256{};
    std::string staging_name{};
};
} // namespace detail

namespace
{
namespace fs = std::filesystem;

constexpr std::size_t MaximumRecordBytes = 12U * 1024U * 1024U;
constexpr std::size_t MaximumJournalBytes = 16U * 1024U;
constexpr std::string_view RecordFileName{".meccha-runtime.json"};
constexpr std::string_view JournalFileName{".meccha-transaction.json"};
constexpr std::string_view AtomicSuffix{".meccha-next"};
constexpr std::string_view ReceiptPrefix{".meccha-"};
constexpr std::string_view ReceiptSuffix{".owner.json"};
constexpr std::string_view StagingPrefix{"staging-"};

class FileHandle
{
public:
    explicit FileHandle(HANDLE value) : value_(value) {}
    FileHandle(const FileHandle&) = delete;
    auto operator=(const FileHandle&) -> FileHandle& = delete;

    ~FileHandle()
    {
        if (value_ != INVALID_HANDLE_VALUE)
        {
            static_cast<void>(CloseHandle(value_));
        }
    }

    [[nodiscard]] auto get() const noexcept -> HANDLE
    {
        return value_;
    }

private:
    HANDLE value_{INVALID_HANDLE_VALUE};
};

struct ParsedRecord
{
    GenerationState state{};
    Sha256Digest manifest_sha256{};
    std::string manifest_json{};
    PayloadManifest manifest{};
};

auto storage_error(RuntimeStorageErrorCode code, std::string detail)
    -> std::unexpected<RuntimeStorageError>
{
    return std::unexpected(RuntimeStorageError{code, std::move(detail)});
}

auto windows_error(std::string detail)
    -> std::unexpected<RuntimeStorageError>
{
    return storage_error(
        RuntimeStorageErrorCode::Io,
        std::move(detail) + " (Windows error " +
            std::to_string(GetLastError()) + ")");
}

auto lower_ascii(std::string_view value) -> std::string
{
    std::string result{value};
    std::ranges::transform(result, result.begin(), [](char character) {
        return static_cast<char>(
            std::tolower(static_cast<unsigned char>(character)));
    });
    return result;
}

auto is_same_or_below(std::string_view path, std::string_view root) -> bool
{
    const auto path_key = lower_ascii(path);
    const auto root_key = lower_ascii(root);
    return path_key == root_key ||
           (path_key.size() > root_key.size() &&
            path_key.starts_with(root_key) &&
            path_key[root_key.size()] == '/');
}

auto is_parent_of(std::string_view parent, std::string_view child) -> bool
{
    const auto parent_key = lower_ascii(parent);
    const auto child_key = lower_ascii(child);
    return child_key.size() > parent_key.size() &&
           child_key.starts_with(parent_key) &&
           child_key[parent_key.size()] == '/';
}

auto valid_generation_name(std::string_view name) -> bool
{
    if (name == "active" || name == "rollback")
    {
        return true;
    }
    if (!name.starts_with(StagingPrefix) ||
        name.size() != StagingPrefix.size() + 32)
    {
        return false;
    }
    return std::ranges::all_of(
        name.substr(StagingPrefix.size()),
        [](char character) {
            return (character >= '0' && character <= '9') ||
                   (character >= 'a' && character <= 'f');
        });
}

auto receipt_name(std::string_view generation) -> std::string
{
    return std::string{ReceiptPrefix} + std::string{generation} +
           std::string{ReceiptSuffix};
}

auto receipt_generation(std::string_view filename)
    -> std::optional<std::string>
{
    if (filename.ends_with(AtomicSuffix))
    {
        filename.remove_suffix(AtomicSuffix.size());
    }
    if (!filename.starts_with(ReceiptPrefix) ||
        !filename.ends_with(ReceiptSuffix))
    {
        return std::nullopt;
    }
    const auto begin = ReceiptPrefix.size();
    const auto length =
        filename.size() - ReceiptPrefix.size() - ReceiptSuffix.size();
    auto generation = std::string{filename.substr(begin, length)};
    if (!generation.starts_with(StagingPrefix) ||
        !valid_generation_name(generation))
    {
        return std::nullopt;
    }
    return generation;
}

auto attributes(const fs::path& path)
    -> std::expected<std::optional<DWORD>, RuntimeStorageError>
{
    const auto value = GetFileAttributesW(path.c_str());
    if (value == INVALID_FILE_ATTRIBUTES)
    {
        const auto error = GetLastError();
        if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND)
        {
            return std::nullopt;
        }
        return windows_error("Could not inspect runtime path.");
    }
    return value;
}

auto require_plain_directory(const fs::path& path)
    -> std::expected<void, RuntimeStorageError>
{
    const auto value = attributes(path);
    if (!value)
    {
        return std::unexpected(value.error());
    }
    if (!*value || !(**value & FILE_ATTRIBUTE_DIRECTORY) ||
        (**value & FILE_ATTRIBUTE_REPARSE_POINT))
    {
        return storage_error(
            RuntimeStorageErrorCode::Conflict,
            "Runtime directory is missing, not a directory, or a reparse point: " +
                path.string());
    }
    return {};
}

auto ensure_plain_directory_tree(const fs::path& path)
    -> std::expected<void, RuntimeStorageError>
{
    if (!path.is_absolute() || path.lexically_normal() != path)
    {
        return storage_error(
            RuntimeStorageErrorCode::Conflict,
            "Runtime root must be an absolute normalized path.");
    }

    auto current = path.root_path();
    for (const auto& component : path.relative_path())
    {
        current /= component;
        auto value = attributes(current);
        if (!value)
        {
            return std::unexpected(value.error());
        }
        if (!*value)
        {
            if (!CreateDirectoryW(current.c_str(), nullptr))
            {
                return windows_error("Could not create runtime directory.");
            }
            value = attributes(current);
            if (!value)
            {
                return std::unexpected(value.error());
            }
        }
        if (!(**value & FILE_ATTRIBUTE_DIRECTORY) ||
            (**value & FILE_ATTRIBUTE_REPARSE_POINT))
        {
            return storage_error(
                RuntimeStorageErrorCode::Conflict,
                "Runtime path traverses a non-directory or reparse point: " +
                    current.string());
        }
    }
    return {};
}

auto ensure_relative_directories(
    const fs::path& root,
    const fs::path& relative_parent)
    -> std::expected<void, RuntimeStorageError>
{
    auto current = root;
    for (const auto& component : relative_parent)
    {
        current /= component;
        auto value = attributes(current);
        if (!value)
        {
            return std::unexpected(value.error());
        }
        if (!*value)
        {
            if (!CreateDirectoryW(current.c_str(), nullptr))
            {
                return windows_error(
                    "Could not create payload directory.");
            }
            value = attributes(current);
            if (!value)
            {
                return std::unexpected(value.error());
            }
        }
        if (!(**value & FILE_ATTRIBUTE_DIRECTORY) ||
            (**value & FILE_ATTRIBUTE_REPARSE_POINT))
        {
            return storage_error(
                RuntimeStorageErrorCode::Conflict,
                "Payload path traverses a non-directory or reparse point: " +
                    current.string());
        }
    }
    return {};
}

auto write_new_durable(
    const fs::path& path,
    std::span<const std::byte> bytes)
    -> std::expected<void, RuntimeStorageError>
{
    FileHandle file{CreateFileW(
        path.c_str(),
        GENERIC_WRITE,
        0,
        nullptr,
        CREATE_NEW,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH,
        nullptr)};
    if (file.get() == INVALID_HANDLE_VALUE)
    {
        return windows_error("Could not create runtime file.");
    }

    auto remaining = bytes;
    while (!remaining.empty())
    {
        const auto chunk_size = std::min(
            remaining.size(),
            static_cast<std::size_t>(
                std::numeric_limits<DWORD>::max()));
        DWORD written{};
        if (!WriteFile(
                file.get(),
                remaining.data(),
                static_cast<DWORD>(chunk_size),
                &written,
                nullptr) ||
            written != chunk_size)
        {
            return windows_error("Could not write runtime file.");
        }
        remaining = remaining.subspan(written);
    }
    if (!FlushFileBuffers(file.get()))
    {
        return windows_error("Could not flush runtime file.");
    }
    return {};
}

auto write_text_atomic(const fs::path& path, std::string_view text)
    -> std::expected<void, RuntimeStorageError>
{
    auto temporary = path;
    temporary += AtomicSuffix;
    const auto temporary_attributes = attributes(temporary);
    if (!temporary_attributes)
    {
        return std::unexpected(temporary_attributes.error());
    }
    if (*temporary_attributes)
    {
        return storage_error(
            RuntimeStorageErrorCode::Conflict,
            "Atomic runtime temporary file already exists: " +
                temporary.string());
    }

    const auto data = std::as_bytes(std::span{text});
    auto written = write_new_durable(temporary, data);
    if (!written)
    {
        return std::unexpected(written.error());
    }
    if (!MoveFileExW(
            temporary.c_str(),
            path.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        return windows_error("Could not publish atomic runtime file.");
    }
    return {};
}

auto read_text(const fs::path& path, std::size_t maximum_bytes)
    -> std::expected<std::optional<std::string>, RuntimeStorageError>
{
    const auto value = attributes(path);
    if (!value)
    {
        return std::unexpected(value.error());
    }
    if (!*value)
    {
        return std::nullopt;
    }
    if ((**value & FILE_ATTRIBUTE_DIRECTORY) ||
        (**value & FILE_ATTRIBUTE_REPARSE_POINT))
    {
        return storage_error(
            RuntimeStorageErrorCode::Conflict,
            "Runtime metadata is a directory or reparse point: " +
                path.string());
    }

    FileHandle file{CreateFileW(
        path.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT,
        nullptr)};
    if (file.get() == INVALID_HANDLE_VALUE)
    {
        return windows_error("Could not open runtime metadata.");
    }
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file.get(), &size) || size.QuadPart < 0 ||
        static_cast<std::uint64_t>(size.QuadPart) > maximum_bytes)
    {
        return storage_error(
            RuntimeStorageErrorCode::InvalidData,
            "Runtime metadata size is invalid.");
    }
    std::string result(static_cast<std::size_t>(size.QuadPart), '\0');
    auto remaining = std::span<char>{result};
    while (!remaining.empty())
    {
        DWORD read{};
        if (!ReadFile(
                file.get(),
                remaining.data(),
                static_cast<DWORD>(std::min(
                    remaining.size(),
                    static_cast<std::size_t>(
                        std::numeric_limits<DWORD>::max()))),
                &read,
                nullptr) ||
            read == 0)
        {
            return windows_error("Could not read runtime metadata.");
        }
        remaining = remaining.subspan(read);
    }
    return result;
}

auto parse_record(std::string_view json)
    -> std::expected<ParsedRecord, RuntimeStorageError>
{
    detail::RawRuntimeRecord raw{};
    constexpr auto StrictJson = glz::opts{
        .error_on_unknown_keys = true,
        .error_on_missing_keys = true,
    };
    const auto parsed = glz::read<StrictJson>(raw, json);
    if (parsed)
    {
        return storage_error(
            RuntimeStorageErrorCode::InvalidData,
            "Runtime ownership record is malformed.");
    }
    if (raw.schema_version != build::RuntimeOwnershipSchemaVersion ||
        raw.product != build::ProductName)
    {
        return storage_error(
            RuntimeStorageErrorCode::InvalidData,
            "Runtime ownership record identity is invalid.");
    }

    GenerationState state{};
    if (raw.state == "partial")
    {
        state = GenerationState::OwnedPartial;
    }
    else if (raw.state == "complete")
    {
        state = GenerationState::OwnedExact;
    }
    else
    {
        return storage_error(
            RuntimeStorageErrorCode::InvalidData,
            "Runtime ownership record state is invalid.");
    }

    const auto recorded_digest = parse_sha256_hex(raw.manifest_sha256);
    const auto computed_digest =
        sha256_bytes(std::as_bytes(std::span{raw.manifest_json}));
    const auto manifest =
        parse_payload_manifest_unbound(raw.manifest_json);
    if (!recorded_digest || !computed_digest || !manifest ||
        *recorded_digest != *computed_digest)
    {
        return storage_error(
            RuntimeStorageErrorCode::InvalidData,
            "Runtime ownership manifest is invalid.");
    }
    return ParsedRecord{
        state,
        *recorded_digest,
        std::move(raw.manifest_json),
        *manifest,
    };
}

auto record_json(
    std::string_view state,
    std::string_view manifest_json,
    const Sha256Digest& manifest_sha256)
    -> std::expected<std::string, RuntimeStorageError>
{
    detail::RawRuntimeRecord raw{
        build::RuntimeOwnershipSchemaVersion,
        std::string{build::ProductName},
        std::string{state},
        sha256_hex(manifest_sha256),
        std::string{manifest_json},
    };
    auto json = glz::write_json(raw);
    if (!json)
    {
        return storage_error(
            RuntimeStorageErrorCode::InvalidData,
            "Could not serialize runtime ownership record.");
    }
    return *json;
}

auto read_record(const fs::path& path)
    -> std::expected<std::optional<ParsedRecord>, RuntimeStorageError>
{
    auto text = read_text(path, MaximumRecordBytes);
    if (!text)
    {
        return std::unexpected(text.error());
    }
    if (!*text)
    {
        return std::nullopt;
    }
    auto parsed = parse_record(**text);
    if (!parsed)
    {
        return std::unexpected(parsed.error());
    }
    return *parsed;
}

auto verify_generation_tree(
    const fs::path& root,
    const PayloadManifest& manifest,
    bool require_all_files)
    -> std::expected<void, RuntimeStorageError>
{
    std::map<std::string, const ManifestFile*, std::less<>> expected_files{};
    for (const auto& file : manifest.files)
    {
        if (is_runtime_cache_role(file.role))
        {
            expected_files.emplace(lower_ascii(file.path), &file);
        }
    }
    std::unordered_set<std::string> seen_files{};

    std::error_code iterator_error{};
    fs::recursive_directory_iterator iterator{
        root,
        fs::directory_options::none,
        iterator_error};
    const fs::recursive_directory_iterator end{};
    if (iterator_error)
    {
        return storage_error(
            RuntimeStorageErrorCode::Io,
            "Could not enumerate runtime generation.");
    }

    for (; iterator != end; iterator.increment(iterator_error))
    {
        if (iterator_error)
        {
            return storage_error(
                RuntimeStorageErrorCode::Io,
                "Could not enumerate runtime generation.");
        }
        const auto entry_attributes = attributes(iterator->path());
        if (!entry_attributes)
        {
            return std::unexpected(entry_attributes.error());
        }
        if (!*entry_attributes ||
            (**entry_attributes & FILE_ATTRIBUTE_REPARSE_POINT))
        {
            return storage_error(
                RuntimeStorageErrorCode::Conflict,
                "Runtime generation contains a missing or reparse entry.");
        }

        const auto relative =
            iterator->path().lexically_relative(root).generic_string();
        const auto key = lower_ascii(relative);
        const auto in_generated_path = std::ranges::any_of(
            manifest.generated_paths,
            [&relative](const std::string& generated) {
                return is_same_or_below(relative, generated);
            });

        if (**entry_attributes & FILE_ATTRIBUTE_DIRECTORY)
        {
            const auto is_required_parent = std::ranges::any_of(
                manifest.files,
                [&relative](const ManifestFile& file) {
                    return is_runtime_cache_role(file.role) &&
                           is_parent_of(relative, file.path);
                }) ||
                std::ranges::any_of(
                    manifest.generated_paths,
                    [&relative](const std::string& generated) {
                        return is_parent_of(relative, generated);
                    });
            if (!in_generated_path && !is_required_parent)
            {
                return storage_error(
                    RuntimeStorageErrorCode::Conflict,
                    "Runtime generation contains an unknown directory: " +
                        relative);
            }
            continue;
        }

        if (relative == RecordFileName ||
            (!require_all_files &&
             relative ==
                 std::string{RecordFileName} +
                     std::string{AtomicSuffix}))
        {
            continue;
        }
        if (in_generated_path)
        {
            continue;
        }
        const auto expected = expected_files.find(key);
        if (expected == expected_files.end())
        {
            return storage_error(
                RuntimeStorageErrorCode::Conflict,
                "Runtime generation contains an unknown file: " +
                    relative);
        }
        const auto measured = sha256_file(iterator->path());
        if (!measured ||
            measured->size != expected->second->size ||
            measured->sha256 != expected->second->sha256)
        {
            return storage_error(
                RuntimeStorageErrorCode::Conflict,
                "Runtime file does not match its ownership manifest: " +
                    relative);
        }
        seen_files.insert(key);
    }

    if (require_all_files && seen_files.size() != expected_files.size())
    {
        return storage_error(
            RuntimeStorageErrorCode::Conflict,
            "Runtime generation is missing immutable files.");
    }
    return {};
}

auto delete_plain_file(const fs::path& path)
    -> std::expected<void, RuntimeStorageError>
{
    if (!DeleteFileW(path.c_str()))
    {
        const auto error = GetLastError();
        if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND)
        {
            return {};
        }
        return windows_error("Could not remove runtime file.");
    }
    return {};
}

auto remove_validated_tree(const fs::path& root)
    -> std::expected<void, RuntimeStorageError>
{
    std::vector<std::pair<fs::path, bool>> entries{};
    std::error_code iterator_error{};
    fs::recursive_directory_iterator iterator{
        root,
        fs::directory_options::none,
        iterator_error};
    const fs::recursive_directory_iterator end{};
    if (iterator_error)
    {
        return storage_error(
            RuntimeStorageErrorCode::Io,
            "Could not enumerate runtime generation for removal.");
    }
    for (; iterator != end; iterator.increment(iterator_error))
    {
        if (iterator_error)
        {
            return storage_error(
                RuntimeStorageErrorCode::Io,
                "Could not enumerate runtime generation for removal.");
        }
        const auto value = attributes(iterator->path());
        if (!value)
        {
            return std::unexpected(value.error());
        }
        if (!*value || (**value & FILE_ATTRIBUTE_REPARSE_POINT))
        {
            return storage_error(
                RuntimeStorageErrorCode::Conflict,
                "Runtime removal encountered a reparse or missing entry.");
        }
        entries.emplace_back(
            iterator->path(),
            (**value & FILE_ATTRIBUTE_DIRECTORY) != 0);
    }
    std::ranges::reverse(entries);
    for (const auto& [path, directory] : entries)
    {
        if (directory)
        {
            if (!RemoveDirectoryW(path.c_str()))
            {
                return windows_error(
                    "Could not remove runtime directory.");
            }
        }
        else
        {
            auto removed = delete_plain_file(path);
            if (!removed)
            {
                return std::unexpected(removed.error());
            }
        }
    }
    if (!RemoveDirectoryW(root.c_str()))
    {
        return windows_error("Could not remove runtime generation.");
    }
    return {};
}

auto journal_json(const RuntimeTransactionJournal& journal)
    -> std::expected<std::string, RuntimeStorageError>
{
    detail::RawRuntimeJournal raw{
        journal.schema_version,
        journal.phase == TransactionPhase::Prepared
            ? "prepared"
            : "committed",
        journal.previous_manifest_sha256
            ? std::optional<std::string>{
                  sha256_hex(*journal.previous_manifest_sha256)}
            : std::nullopt,
        sha256_hex(journal.next_manifest_sha256),
        journal.staging_name,
    };
    auto json = glz::write_json(raw);
    if (!json)
    {
        return storage_error(
            RuntimeStorageErrorCode::InvalidData,
            "Could not serialize runtime transaction journal.");
    }
    return *json;
}

auto parse_journal(std::string_view json)
    -> std::expected<RuntimeTransactionJournal, RuntimeStorageError>
{
    detail::RawRuntimeJournal raw{};
    constexpr auto StrictJson = glz::opts{
        .error_on_unknown_keys = true,
        .error_on_missing_keys = true,
    };
    const auto parsed = glz::read<StrictJson>(raw, json);
    if (parsed ||
        raw.schema_version != build::RuntimeTransactionSchemaVersion)
    {
        return storage_error(
            RuntimeStorageErrorCode::InvalidData,
            "Runtime transaction journal is malformed.");
    }
    TransactionPhase phase{};
    if (raw.phase == "prepared")
    {
        phase = TransactionPhase::Prepared;
    }
    else if (raw.phase == "committed")
    {
        phase = TransactionPhase::Committed;
    }
    else
    {
        return storage_error(
            RuntimeStorageErrorCode::InvalidData,
            "Runtime transaction phase is invalid.");
    }

    std::optional<Sha256Digest> previous{};
    if (raw.previous_manifest_sha256)
    {
        previous = parse_sha256_hex(*raw.previous_manifest_sha256);
        if (!previous)
        {
            return storage_error(
                RuntimeStorageErrorCode::InvalidData,
                "Runtime previous manifest hash is invalid.");
        }
    }
    const auto next = parse_sha256_hex(raw.next_manifest_sha256);
    if (!next)
    {
        return storage_error(
            RuntimeStorageErrorCode::InvalidData,
            "Runtime next manifest hash is invalid.");
    }
    return RuntimeTransactionJournal{
        raw.schema_version,
        phase,
        previous,
        *next,
        std::move(raw.staging_name),
    };
}
} // namespace

Win32RuntimeStorage::Win32RuntimeStorage(
    fs::path root,
    std::string payload_manifest_json,
    Sha256Digest payload_manifest_sha256,
    RuntimePayloadSource& payload_source)
    : root_(std::move(root)),
      payload_manifest_json_(std::move(payload_manifest_json)),
      payload_manifest_sha256_(payload_manifest_sha256),
      payload_source_(payload_source)
{
}

auto Win32RuntimeStorage::identify_generation(std::string_view name)
    -> std::expected<GenerationIdentity, RuntimeStorageError>
{
    if (!valid_generation_name(name))
    {
        return storage_error(
            RuntimeStorageErrorCode::Conflict,
            "Runtime generation name is invalid.");
    }
    auto root_ready = ensure_plain_directory_tree(root_);
    if (!root_ready)
    {
        return std::unexpected(root_ready.error());
    }

    const auto generation_path = root_ / fs::path{name};
    const auto generation_attributes = attributes(generation_path);
    if (!generation_attributes)
    {
        return std::unexpected(generation_attributes.error());
    }
    const auto receipt_path = root_ / receipt_name(name);
    auto receipt = read_record(receipt_path);
    if (!receipt)
    {
        return std::unexpected(receipt.error());
    }
    if (!*receipt)
    {
        auto temporary_receipt = receipt_path;
        temporary_receipt += AtomicSuffix;
        receipt = read_record(temporary_receipt);
        if (!receipt)
        {
            return std::unexpected(receipt.error());
        }
    }

    if (!*generation_attributes)
    {
        if (*receipt)
        {
            if ((**receipt).state != GenerationState::OwnedPartial)
            {
                return storage_error(
                    RuntimeStorageErrorCode::Conflict,
                    "Staging receipt has an invalid state.");
            }
            return GenerationIdentity{
                GenerationState::OwnedPartial,
                (**receipt).manifest_sha256,
            };
        }
        return GenerationIdentity{GenerationState::Missing, {}};
    }
    if (!(**generation_attributes & FILE_ATTRIBUTE_DIRECTORY) ||
        (**generation_attributes & FILE_ATTRIBUTE_REPARSE_POINT))
    {
        return GenerationIdentity{GenerationState::Conflict, {}};
    }

    auto internal =
        read_record(generation_path / fs::path{RecordFileName});
    if (!internal)
    {
        return std::unexpected(internal.error());
    }
    if (!*internal)
    {
        if (*receipt &&
            (**receipt).state == GenerationState::OwnedPartial)
        {
            auto verified = verify_generation_tree(
                generation_path,
                (**receipt).manifest,
                false);
            if (!verified)
            {
                return GenerationIdentity{
                    GenerationState::Conflict,
                    {}};
            }
            return GenerationIdentity{
                GenerationState::OwnedPartial,
                (**receipt).manifest_sha256,
            };
        }
        return GenerationIdentity{GenerationState::Conflict, {}};
    }
    if ((**internal).state != GenerationState::OwnedExact ||
        (*receipt &&
         ((**receipt).manifest_sha256 !=
              (**internal).manifest_sha256 ||
          (**receipt).manifest_json != (**internal).manifest_json)))
    {
        return GenerationIdentity{GenerationState::Conflict, {}};
    }
    auto verified = verify_generation_tree(
        generation_path,
        (**internal).manifest,
        true);
    if (!verified)
    {
        return GenerationIdentity{GenerationState::Conflict, {}};
    }
    return GenerationIdentity{
        GenerationState::OwnedExact,
        (**internal).manifest_sha256,
    };
}

auto Win32RuntimeStorage::list_staging_generations()
    -> std::expected<std::vector<std::string>, RuntimeStorageError>
{
    auto root_ready = ensure_plain_directory_tree(root_);
    if (!root_ready)
    {
        return std::unexpected(root_ready.error());
    }
    std::set<std::string, std::less<>> names{};
    std::error_code iterator_error{};
    for (fs::directory_iterator iterator{root_, iterator_error}, end{};
         iterator != end;
         iterator.increment(iterator_error))
    {
        if (iterator_error)
        {
            return storage_error(
                RuntimeStorageErrorCode::Io,
                "Could not enumerate runtime root.");
        }
        const auto filename = iterator->path().filename().string();
        if (filename == "active" || filename == "rollback" ||
            filename == JournalFileName ||
            filename ==
                std::string{JournalFileName} + std::string{AtomicSuffix})
        {
            continue;
        }
        if (filename.starts_with(StagingPrefix) &&
            valid_generation_name(filename))
        {
            names.insert(filename);
            continue;
        }
        if (const auto generation = receipt_generation(filename);
            generation)
        {
            names.insert(*generation);
            continue;
        }
        return storage_error(
            RuntimeStorageErrorCode::Conflict,
            "Runtime root contains an unknown entry: " + filename);
    }
    return std::vector<std::string>{names.begin(), names.end()};
}

auto Win32RuntimeStorage::read_journal()
    -> std::expected<
        std::optional<RuntimeTransactionJournal>,
        RuntimeStorageError>
{
    auto root_ready = ensure_plain_directory_tree(root_);
    if (!root_ready)
    {
        return std::unexpected(root_ready.error());
    }
    const auto journal_path = root_ / fs::path{JournalFileName};
    auto temporary_path = journal_path;
    temporary_path += AtomicSuffix;
    auto temporary = read_text(temporary_path, MaximumJournalBytes);
    if (!temporary)
    {
        return std::unexpected(temporary.error());
    }
    if (*temporary)
    {
        const auto temporary_journal = parse_journal(**temporary);
        if (!temporary_journal)
        {
            return std::unexpected(temporary_journal.error());
        }
        auto removed = delete_plain_file(temporary_path);
        if (!removed)
        {
            return std::unexpected(removed.error());
        }
    }

    auto text = read_text(journal_path, MaximumJournalBytes);
    if (!text)
    {
        return std::unexpected(text.error());
    }
    if (!*text)
    {
        return std::nullopt;
    }
    auto journal = parse_journal(**text);
    if (!journal)
    {
        return std::unexpected(journal.error());
    }
    return *journal;
}

auto Win32RuntimeStorage::stage_generation(
    std::string_view name,
    const Sha256Digest& manifest_sha256)
    -> std::expected<void, RuntimeStorageError>
{
    if (!name.starts_with(StagingPrefix) ||
        !valid_generation_name(name) ||
        manifest_sha256 != payload_manifest_sha256_)
    {
        return storage_error(
            RuntimeStorageErrorCode::Conflict,
            "Staging request does not match the embedded payload.");
    }
    auto root_ready = ensure_plain_directory_tree(root_);
    if (!root_ready)
    {
        return std::unexpected(root_ready.error());
    }
    const auto computed_manifest_sha256 = sha256_bytes(
        std::as_bytes(std::span{payload_manifest_json_}));
    const auto manifest =
        parse_payload_manifest(payload_manifest_json_);
    if (!computed_manifest_sha256 || !manifest ||
        *computed_manifest_sha256 != payload_manifest_sha256_)
    {
        return storage_error(
            RuntimeStorageErrorCode::InvalidData,
            "Embedded runtime payload identity is invalid.");
    }

    const auto generation_path = root_ / fs::path{name};
    const auto generation_attributes = attributes(generation_path);
    const auto receipt_path = root_ / receipt_name(name);
    const auto receipt_attributes = attributes(receipt_path);
    if (!generation_attributes || !receipt_attributes)
    {
        return storage_error(
            RuntimeStorageErrorCode::Io,
            "Could not inspect staging destinations.");
    }
    if (*generation_attributes || *receipt_attributes)
    {
        return storage_error(
            RuntimeStorageErrorCode::Conflict,
            "Staging destination already exists.");
    }

    auto partial_record = record_json(
        "partial",
        payload_manifest_json_,
        payload_manifest_sha256_);
    if (!partial_record)
    {
        return std::unexpected(partial_record.error());
    }
    auto receipt_written =
        write_text_atomic(receipt_path, *partial_record);
    if (!receipt_written)
    {
        return std::unexpected(receipt_written.error());
    }
    if (!CreateDirectoryW(generation_path.c_str(), nullptr))
    {
        return windows_error("Could not create staging generation.");
    }

    for (const auto& file : manifest->files)
    {
        if (!is_runtime_cache_role(file.role))
        {
            continue;
        }
        auto payload = payload_source_.read_file(file.path);
        if (!payload)
        {
            return storage_error(
                RuntimeStorageErrorCode::InvalidData,
                "Payload source could not provide " + file.path +
                    ": " + payload.error().detail);
        }
        const auto digest = sha256_bytes(*payload);
        if (!digest || payload->size() != file.size ||
            *digest != file.sha256)
        {
            return storage_error(
                RuntimeStorageErrorCode::InvalidData,
                "Payload source file does not match the manifest: " +
                    file.path);
        }
        const auto relative = fs::path{file.path};
        auto directories = ensure_relative_directories(
            generation_path,
            relative.parent_path());
        if (!directories)
        {
            return std::unexpected(directories.error());
        }
        auto written =
            write_new_durable(generation_path / relative, *payload);
        if (!written)
        {
            return std::unexpected(written.error());
        }
    }

    auto complete_record = record_json(
        "complete",
        payload_manifest_json_,
        payload_manifest_sha256_);
    if (!complete_record)
    {
        return std::unexpected(complete_record.error());
    }
    auto record_written = write_text_atomic(
        generation_path / fs::path{RecordFileName},
        *complete_record);
    if (!record_written)
    {
        return std::unexpected(record_written.error());
    }
    const auto identity = identify_generation(name);
    if (!identity)
    {
        return std::unexpected(identity.error());
    }
    if (identity->state != GenerationState::OwnedExact ||
        identity->manifest_sha256 != manifest_sha256)
    {
        return storage_error(
            RuntimeStorageErrorCode::Conflict,
            "Staged runtime failed complete verification.");
    }
    return {};
}

auto Win32RuntimeStorage::write_journal(
    const RuntimeTransactionJournal& journal)
    -> std::expected<void, RuntimeStorageError>
{
    auto json = journal_json(journal);
    if (!json)
    {
        return std::unexpected(json.error());
    }
    return write_text_atomic(root_ / fs::path{JournalFileName}, *json);
}

auto Win32RuntimeStorage::rename_generation(
    std::string_view from,
    std::string_view to) -> std::expected<void, RuntimeStorageError>
{
    if (!valid_generation_name(from) || !valid_generation_name(to) ||
        from == to)
    {
        return storage_error(
            RuntimeStorageErrorCode::Conflict,
            "Runtime rename uses an invalid generation name.");
    }
    const auto source = root_ / fs::path{from};
    const auto destination = root_ / fs::path{to};
    auto source_ready = require_plain_directory(source);
    if (!source_ready)
    {
        return std::unexpected(source_ready.error());
    }
    const auto destination_attributes = attributes(destination);
    if (!destination_attributes)
    {
        return std::unexpected(destination_attributes.error());
    }
    if (*destination_attributes)
    {
        return storage_error(
            RuntimeStorageErrorCode::Conflict,
            "Runtime rename destination already exists.");
    }
    if (!MoveFileExW(
            source.c_str(),
            destination.c_str(),
            MOVEFILE_WRITE_THROUGH))
    {
        return windows_error("Could not rename runtime generation.");
    }
    if (from.starts_with(StagingPrefix))
    {
        auto receipt_removed =
            delete_plain_file(root_ / receipt_name(from));
        if (!receipt_removed)
        {
            return std::unexpected(receipt_removed.error());
        }
    }
    return {};
}

auto Win32RuntimeStorage::remove_generation(
    std::string_view name,
    const Sha256Digest& expected_manifest_sha256)
    -> std::expected<void, RuntimeStorageError>
{
    const auto identity = identify_generation(name);
    if (!identity)
    {
        return std::unexpected(identity.error());
    }
    if ((identity->state != GenerationState::OwnedExact &&
         identity->state != GenerationState::OwnedPartial) ||
        identity->manifest_sha256 != expected_manifest_sha256)
    {
        return storage_error(
            RuntimeStorageErrorCode::Conflict,
            "Runtime generation is not safe to remove.");
    }

    const auto generation_path = root_ / fs::path{name};
    const auto generation_attributes = attributes(generation_path);
    if (!generation_attributes)
    {
        return std::unexpected(generation_attributes.error());
    }
    if (*generation_attributes)
    {
        std::optional<ParsedRecord> record{};
        auto internal =
            read_record(generation_path / fs::path{RecordFileName});
        if (!internal)
        {
            return std::unexpected(internal.error());
        }
        if (*internal)
        {
            record = **internal;
        }
        else
        {
            auto receipt =
                read_record(root_ / receipt_name(name));
            if (!receipt)
            {
                return std::unexpected(receipt.error());
            }
            if (*receipt)
            {
                record = **receipt;
            }
        }
        if (!record ||
            record->manifest_sha256 != expected_manifest_sha256)
        {
            return storage_error(
                RuntimeStorageErrorCode::Conflict,
                "Runtime removal lacks a matching ownership manifest.");
        }
        auto verified = verify_generation_tree(
            generation_path,
            record->manifest,
            identity->state == GenerationState::OwnedExact);
        if (!verified)
        {
            return std::unexpected(verified.error());
        }
        auto removed = remove_validated_tree(generation_path);
        if (!removed)
        {
            return std::unexpected(removed.error());
        }
    }
    auto receipt_path = root_ / receipt_name(name);
    auto receipt_removed = delete_plain_file(receipt_path);
    if (!receipt_removed)
    {
        return std::unexpected(receipt_removed.error());
    }
    receipt_path += AtomicSuffix;
    return delete_plain_file(receipt_path);
}

auto Win32RuntimeStorage::remove_journal()
    -> std::expected<void, RuntimeStorageError>
{
    return delete_plain_file(root_ / fs::path{JournalFileName});
}
} // namespace meccha::launcher
