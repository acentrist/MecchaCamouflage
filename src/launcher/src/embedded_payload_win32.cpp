#include <meccha/launcher/embedded_payload.hpp>

#include <meccha/launcher/hash.hpp>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <objbase.h>
#include <setupapi.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace meccha::launcher
{
namespace
{
namespace fs = std::filesystem;

constexpr std::uint64_t MaximumCabinetBytes =
    512ULL * 1024ULL * 1024ULL;
constexpr std::uint64_t MaximumPayloadFileBytes =
    256ULL * 1024ULL * 1024ULL;
constexpr std::uint64_t MaximumPayloadBytes =
    512ULL * 1024ULL * 1024ULL;

auto payload_error(std::string detail)
    -> std::unexpected<RuntimePayloadError>
{
    return std::unexpected(RuntimePayloadError{std::move(detail)});
}

class UniqueHandle
{
public:
    explicit UniqueHandle(HANDLE value) : value_{value}
    {
    }

    UniqueHandle(const UniqueHandle&) = delete;
    auto operator=(const UniqueHandle&) -> UniqueHandle& = delete;

    ~UniqueHandle()
    {
        if (value_ != nullptr && value_ != INVALID_HANDLE_VALUE)
        {
            static_cast<void>(CloseHandle(value_));
        }
    }

    [[nodiscard]] auto get() const -> HANDLE
    {
        return value_;
    }

private:
    HANDLE value_{};
};

class ExtractionWorkspace
{
public:
    ExtractionWorkspace(const ExtractionWorkspace&) = delete;
    auto operator=(const ExtractionWorkspace&)
        -> ExtractionWorkspace& = delete;

    ExtractionWorkspace(ExtractionWorkspace&& other) noexcept
        : path_{std::move(other.path_)}
    {
        other.path_.clear();
    }

    auto operator=(ExtractionWorkspace&& other) noexcept
        -> ExtractionWorkspace&
    {
        if (this != &other)
        {
            cleanup();
            path_ = std::move(other.path_);
            other.path_.clear();
        }
        return *this;
    }

    ~ExtractionWorkspace()
    {
        cleanup();
    }

    [[nodiscard]] static auto create(const fs::path& parent)
        -> std::expected<ExtractionWorkspace, RuntimePayloadError>
    {
        std::error_code canonical_error{};
        const auto canonical_parent =
            fs::canonical(parent, canonical_error);
        if (canonical_error)
        {
            return payload_error(
                "The payload scratch directory is unavailable");
        }
        const auto attributes =
            GetFileAttributesW(canonical_parent.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES ||
            !(attributes & FILE_ATTRIBUTE_DIRECTORY) ||
            (attributes & FILE_ATTRIBUTE_REPARSE_POINT))
        {
            return payload_error(
                "The payload scratch directory is not a plain directory");
        }

        GUID guid{};
        if (FAILED(CoCreateGuid(&guid)))
        {
            return payload_error(
                "A private payload workspace name could not be created");
        }
        std::array<wchar_t, 64> encoded{};
        if (StringFromGUID2(
                guid,
                encoded.data(),
                static_cast<int>(encoded.size())) <= 0)
        {
            return payload_error(
                "The private payload workspace name is invalid");
        }
        auto path =
            canonical_parent /
            (std::wstring{L".meccha-payload-source-"} +
             encoded.data());
        if (!CreateDirectoryW(path.c_str(), nullptr))
        {
            return payload_error(
                "The private payload workspace could not be created "
                "(Win32 " +
                std::to_string(GetLastError()) + ')');
        }
        return ExtractionWorkspace{std::move(path)};
    }

    [[nodiscard]] auto path() const -> const fs::path&
    {
        return path_;
    }

    auto remove() -> std::expected<void, RuntimePayloadError>
    {
        std::error_code remove_error{};
        fs::remove_all(path_, remove_error);
        if (remove_error)
        {
            return payload_error(
                "The private payload workspace could not be removed");
        }
        path_.clear();
        return {};
    }

private:
    explicit ExtractionWorkspace(fs::path path)
        : path_{std::move(path)}
    {
    }

    void cleanup() noexcept
    {
        if (path_.empty())
        {
            return;
        }
        std::error_code ignored{};
        fs::remove_all(path_, ignored);
        path_.clear();
    }

    fs::path path_{};
};

auto write_cabinet(
    const fs::path& path,
    std::span<const std::byte> cabinet)
    -> std::expected<void, RuntimePayloadError>
{
    if (cabinet.empty() ||
        cabinet.size() > MaximumCabinetBytes)
    {
        return payload_error(
            "The embedded payload CAB size is outside the supported range");
    }
    const UniqueHandle output{CreateFileW(
        path.c_str(),
        GENERIC_WRITE,
        0,
        nullptr,
        CREATE_NEW,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH,
        nullptr)};
    if (output.get() == INVALID_HANDLE_VALUE)
    {
        return payload_error(
            "The embedded payload CAB could not be staged (Win32 " +
            std::to_string(GetLastError()) + ')');
    }

    auto remaining = cabinet;
    while (!remaining.empty())
    {
        const auto chunk = static_cast<DWORD>(std::min<std::size_t>(
            remaining.size(),
            std::numeric_limits<DWORD>::max()));
        DWORD written{};
        if (!WriteFile(
                output.get(),
                remaining.data(),
                chunk,
                &written,
                nullptr) ||
            written != chunk)
        {
            return payload_error(
                "The embedded payload CAB could not be written (Win32 " +
                std::to_string(GetLastError()) + ')');
        }
        remaining = remaining.subspan(written);
    }
    if (!FlushFileBuffers(output.get()))
    {
        return payload_error(
            "The embedded payload CAB could not be flushed (Win32 " +
            std::to_string(GetLastError()) + ')');
    }
    return {};
}

using ManifestLookup =
    std::unordered_map<std::string, const ManifestFile*>;

auto validate_manifest(const PayloadManifest& manifest)
    -> std::expected<ManifestLookup, RuntimePayloadError>
{
    if (manifest.files.empty() ||
        manifest.total_size > MaximumPayloadBytes)
    {
        return payload_error(
            "The payload manifest size is outside the supported range");
    }
    auto lookup = ManifestLookup{};
    lookup.reserve(manifest.files.size());
    std::uint64_t total{};
    for (const auto& file : manifest.files)
    {
        if (!is_canonical_payload_path(file.path) ||
            file.size > MaximumPayloadFileBytes ||
            file.size >
                MaximumPayloadBytes - total)
        {
            return payload_error(
                "The payload manifest contains an invalid file: " +
                file.path);
        }
        const auto [unused, inserted] = lookup.emplace(
            canonical_payload_path_key(file.path),
            std::addressof(file));
        static_cast<void>(unused);
        if (!inserted)
        {
            return payload_error(
                "The payload manifest contains a duplicate path: " +
                file.path);
        }
        total += file.size;
    }
    if (total != manifest.total_size)
    {
        return payload_error(
            "The payload manifest total size is inconsistent");
    }
    return lookup;
}

auto canonical_cab_path(PCWSTR raw)
    -> std::expected<std::string, RuntimePayloadError>
{
    if (raw == nullptr || *raw == L'\0')
    {
        return payload_error("The CAB contains an empty path");
    }
    auto result = std::string{};
    for (const auto* current = raw; *current != L'\0'; ++current)
    {
        if (*current == L'\\')
        {
            result.push_back('/');
            continue;
        }
        if (*current < 0x20 || *current > 0x7e)
        {
            return payload_error(
                "The CAB contains a non-canonical path encoding");
        }
        result.push_back(static_cast<char>(*current));
    }
    if (!is_canonical_payload_path(result))
    {
        return payload_error(
            "The CAB contains a non-canonical path: " + result);
    }
    return result;
}

auto ensure_plain_directories(
    const fs::path& root,
    const fs::path& parent)
    -> std::expected<void, RuntimePayloadError>
{
    std::error_code create_error{};
    fs::create_directories(parent, create_error);
    if (create_error)
    {
        return payload_error(
            "A payload extraction directory could not be created");
    }
    auto current = root;
    const auto relative = parent.lexically_relative(root);
    for (const auto& component : relative)
    {
        current /= component;
        const auto attributes = GetFileAttributesW(current.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES ||
            !(attributes & FILE_ATTRIBUTE_DIRECTORY) ||
            (attributes & FILE_ATTRIBUTE_REPARSE_POINT))
        {
            return payload_error(
                "The payload extraction path is not a plain directory");
        }
    }
    return {};
}

struct ExtractionContext
{
    const ManifestLookup& expected;
    fs::path root{};
    std::unordered_set<std::string> seen{};
    std::string error{};
};

auto CALLBACK cabinet_callback(
    PVOID raw_context,
    UINT notification,
    UINT_PTR parameter_one,
    UINT_PTR) -> UINT
{
    auto& context =
        *static_cast<ExtractionContext*>(raw_context);
    if (notification == SPFILENOTIFY_NEEDNEWCABINET)
    {
        context.error =
            "Multi-cabinet payloads are not supported";
        return ERROR_NOT_SUPPORTED;
    }
    if (notification != SPFILENOTIFY_FILEINCABINET)
    {
        return NO_ERROR;
    }

    auto& file = *reinterpret_cast<FILE_IN_CABINET_INFO_W*>(
        parameter_one);
    const auto relative = canonical_cab_path(file.NameInCabinet);
    if (!relative)
    {
        context.error = relative.error().detail;
        return FILEOP_ABORT;
    }
    const auto key = canonical_payload_path_key(*relative);
    const auto expected = context.expected.find(key);
    if (expected == context.expected.end() ||
        !context.seen.insert(key).second)
    {
        context.error =
            "The CAB contains an undeclared or duplicate path: " +
            *relative;
        return FILEOP_ABORT;
    }
    if (expected->second->size != file.FileSize)
    {
        context.error =
            "The CAB file size does not match the manifest: " +
            *relative;
        return FILEOP_ABORT;
    }

    const auto destination =
        context.root / fs::path{*relative};
    const auto directories = ensure_plain_directories(
        context.root,
        destination.parent_path());
    if (!directories)
    {
        context.error = directories.error().detail;
        return FILEOP_ABORT;
    }
    const auto& target = destination.native();
    if (target.size() + 1U >
        std::size(file.FullTargetName))
    {
        context.error =
            "A CAB extraction target exceeds the supported Windows path";
        return FILEOP_ABORT;
    }
    if (wmemcpy_s(
            file.FullTargetName,
            std::size(file.FullTargetName),
            target.c_str(),
            target.size() + 1U) != 0)
    {
        context.error =
            "A CAB extraction target could not be encoded";
        return FILEOP_ABORT;
    }
    return FILEOP_DOIT;
}

auto read_plain_file(
    const fs::path& path,
    const ManifestFile& expected)
    -> std::expected<std::vector<std::byte>, RuntimePayloadError>
{
    const UniqueHandle input{CreateFileW(
        path.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL |
            FILE_FLAG_OPEN_REPARSE_POINT |
            FILE_FLAG_SEQUENTIAL_SCAN,
        nullptr)};
    if (input.get() == INVALID_HANDLE_VALUE)
    {
        return payload_error(
            "An extracted payload file could not be opened: " +
            expected.path);
    }
    FILE_ATTRIBUTE_TAG_INFO tag{};
    if (!GetFileInformationByHandleEx(
            input.get(),
            FileAttributeTagInfo,
            &tag,
            sizeof(tag)) ||
        (tag.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
        (tag.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
    {
        return payload_error(
            "An extracted payload entry is not a plain file: " +
            expected.path);
    }
    LARGE_INTEGER raw_size{};
    if (!GetFileSizeEx(input.get(), &raw_size) ||
        raw_size.QuadPart < 0 ||
        static_cast<std::uint64_t>(raw_size.QuadPart) != expected.size ||
        expected.size > MaximumPayloadFileBytes ||
        expected.size >
            std::numeric_limits<std::size_t>::max())
    {
        return payload_error(
            "An extracted payload file has the wrong size: " +
            expected.path);
    }

    auto result = std::vector<std::byte>(
        static_cast<std::size_t>(expected.size));
    auto remaining = std::span<std::byte>{result};
    while (!remaining.empty())
    {
        const auto chunk = static_cast<DWORD>(std::min<std::size_t>(
            remaining.size(),
            std::numeric_limits<DWORD>::max()));
        DWORD read{};
        if (!ReadFile(
                input.get(),
                remaining.data(),
                chunk,
                &read,
                nullptr) ||
            read != chunk)
        {
            return payload_error(
                "An extracted payload file could not be read: " +
                expected.path);
        }
        remaining = remaining.subspan(read);
    }
    const auto digest = sha256_bytes(result);
    if (!digest || *digest != expected.sha256)
    {
        return payload_error(
            "An extracted payload file does not match the manifest: " +
            expected.path);
    }
    return result;
}

auto load_extracted_payload(
    const fs::path& root,
    const ManifestLookup& expected)
    -> std::expected<
        std::unordered_map<std::string, std::vector<std::byte>>,
        RuntimePayloadError>
{
    auto actual = std::unordered_set<std::string>{};
    std::error_code iterator_error{};
    for (fs::recursive_directory_iterator iterator{
             root,
             fs::directory_options::none,
             iterator_error};
         !iterator_error &&
         iterator != fs::recursive_directory_iterator{};
         iterator.increment(iterator_error))
    {
        const auto attributes =
            GetFileAttributesW(iterator->path().c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES ||
            (attributes & FILE_ATTRIBUTE_REPARSE_POINT))
        {
            return payload_error(
                "The extracted payload contains an unreadable or "
                "reparse entry");
        }
        if (attributes & FILE_ATTRIBUTE_DIRECTORY)
        {
            continue;
        }
        const auto relative =
            iterator->path().lexically_relative(root).generic_string();
        if (!is_canonical_payload_path(relative))
        {
            return payload_error(
                "The extracted payload contains a non-canonical path");
        }
        const auto key = canonical_payload_path_key(relative);
        if (!actual.insert(key).second ||
            !expected.contains(key))
        {
            return payload_error(
                "The extracted payload contains an undeclared or "
                "duplicate file: " +
                relative);
        }
    }
    if (iterator_error || actual.size() != expected.size())
    {
        return payload_error(
            "The extracted payload file set does not match the manifest");
    }

    auto files =
        std::unordered_map<std::string, std::vector<std::byte>>{};
    files.reserve(expected.size());
    for (const auto& [key, file] : expected)
    {
        const auto loaded = read_plain_file(
            root / fs::path{file->path},
            *file);
        if (!loaded)
        {
            return std::unexpected(loaded.error());
        }
        files.emplace(key, std::move(*loaded));
    }
    return files;
}
} // namespace

auto read_current_module_rcdata(std::uint16_t resource_id)
    -> std::expected<std::vector<std::byte>, RuntimePayloadError>
{
    const auto resource = FindResourceW(
        nullptr,
        MAKEINTRESOURCEW(resource_id),
        MAKEINTRESOURCEW(10));
    if (resource == nullptr)
    {
        return payload_error(
            "The embedded RCDATA resource is missing (Win32 " +
            std::to_string(GetLastError()) + ')');
    }
    const auto size = SizeofResource(nullptr, resource);
    if (size == 0 ||
        static_cast<std::uint64_t>(size) > MaximumCabinetBytes)
    {
        return payload_error(
            "The embedded RCDATA resource size is outside the "
            "supported range");
    }
    const auto loaded = LoadResource(nullptr, resource);
    if (loaded == nullptr)
    {
        return payload_error(
            "The embedded RCDATA resource could not be loaded (Win32 " +
            std::to_string(GetLastError()) + ')');
    }
    const auto* locked =
        static_cast<const std::byte*>(LockResource(loaded));
    if (locked == nullptr)
    {
        return payload_error(
            "The embedded RCDATA resource could not be locked");
    }
    return std::vector<std::byte>{locked, locked + size};
}

Win32CabPayloadSource::Win32CabPayloadSource(
    std::unordered_map<std::string, std::vector<std::byte>> files)
    : files_{std::move(files)}
{
}

auto Win32CabPayloadSource::open(
    std::span<const std::byte> cabinet,
    const PayloadManifest& manifest,
    const fs::path& scratch_parent)
    -> std::expected<Win32CabPayloadSource, RuntimePayloadError>
{
    const auto expected = validate_manifest(manifest);
    if (!expected)
    {
        return std::unexpected(expected.error());
    }
    auto workspace = ExtractionWorkspace::create(scratch_parent);
    if (!workspace)
    {
        return std::unexpected(workspace.error());
    }
    const auto cab_path = workspace->path() / "payload.cab";
    const auto written = write_cabinet(cab_path, cabinet);
    if (!written)
    {
        return std::unexpected(written.error());
    }
    const auto extract_root = workspace->path() / "expanded";
    if (!CreateDirectoryW(extract_root.c_str(), nullptr))
    {
        return payload_error(
            "The payload extraction root could not be created (Win32 " +
            std::to_string(GetLastError()) + ')');
    }
    auto context = ExtractionContext{
        *expected,
        extract_root,
        {},
        {},
    };
    SetLastError(ERROR_SUCCESS);
    const auto iterated = SetupIterateCabinetW(
        cab_path.c_str(),
        0,
        cabinet_callback,
        &context);
    if (!context.error.empty())
    {
        return payload_error(std::move(context.error));
    }
    if (!iterated)
    {
        return payload_error(
            "The embedded payload CAB could not be expanded (Win32 " +
            std::to_string(GetLastError()) + ')');
    }
    if (context.seen.size() != expected->size())
    {
        return payload_error(
            "The CAB file set does not match the payload manifest");
    }
    auto files = load_extracted_payload(
        extract_root,
        *expected);
    if (!files)
    {
        return std::unexpected(files.error());
    }
    const auto removed = workspace->remove();
    if (!removed)
    {
        return std::unexpected(removed.error());
    }
    return Win32CabPayloadSource{std::move(*files)};
}

auto Win32CabPayloadSource::read_file(std::string_view relative_path)
    -> std::expected<std::vector<std::byte>, RuntimePayloadError>
{
    if (!is_canonical_payload_path(relative_path))
    {
        return payload_error(
            "The payload lookup path is not canonical");
    }
    const auto found = files_.find(
        canonical_payload_path_key(relative_path));
    if (found == files_.end())
    {
        return payload_error(
            "The payload lookup path is not declared: " +
            std::string{relative_path});
    }
    return found->second;
}
} // namespace meccha::launcher
