#include <meccha/common/hash.hpp>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <bcrypt.h>

#include <algorithm>
#include <array>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace meccha::common
{
namespace
{
class AlgorithmHandle
{
public:
    AlgorithmHandle() = default;
    AlgorithmHandle(const AlgorithmHandle&) = delete;
    auto operator=(const AlgorithmHandle&) -> AlgorithmHandle& = delete;

    ~AlgorithmHandle()
    {
        if (value_ != nullptr)
        {
            static_cast<void>(BCryptCloseAlgorithmProvider(value_, 0));
        }
    }

    [[nodiscard]] auto address() noexcept -> BCRYPT_ALG_HANDLE*
    {
        return &value_;
    }

    [[nodiscard]] auto get() const noexcept -> BCRYPT_ALG_HANDLE
    {
        return value_;
    }

private:
    BCRYPT_ALG_HANDLE value_{};
};

class HashHandle
{
public:
    HashHandle() = default;
    HashHandle(const HashHandle&) = delete;
    auto operator=(const HashHandle&) -> HashHandle& = delete;

    ~HashHandle()
    {
        if (value_ != nullptr)
        {
            static_cast<void>(BCryptDestroyHash(value_));
        }
    }

    [[nodiscard]] auto address() noexcept -> BCRYPT_HASH_HANDLE*
    {
        return &value_;
    }

    [[nodiscard]] auto get() const noexcept -> BCRYPT_HASH_HANDLE
    {
        return value_;
    }

private:
    BCRYPT_HASH_HANDLE value_{};
};

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

[[nodiscard]] auto succeeded(NTSTATUS status) noexcept -> bool
{
    return status >= 0;
}

auto error(
    HashErrorCode code,
    std::int32_t native_status,
    std::string detail) -> std::unexpected<HashError>
{
    return std::unexpected(HashError{
        code,
        native_status,
        std::move(detail),
    });
}

auto bcrypt_error(HashErrorCode code, NTSTATUS status, std::string detail)
    -> std::unexpected<HashError>
{
    return error(code, static_cast<std::int32_t>(status), std::move(detail));
}

auto read_u32_property(
    BCRYPT_ALG_HANDLE algorithm,
    const wchar_t* property,
    std::uint32_t& value) -> NTSTATUS
{
    ULONG bytes_written{};
    return BCryptGetProperty(
        algorithm,
        property,
        reinterpret_cast<PUCHAR>(&value),
        static_cast<ULONG>(sizeof(value)),
        &bytes_written,
        0);
}

template <typename Feed>
auto compute_sha256(Feed&& feed)
    -> std::expected<Sha256Digest, HashError>
{
    AlgorithmHandle algorithm{};
    auto status = BCryptOpenAlgorithmProvider(
        algorithm.address(),
        BCRYPT_SHA256_ALGORITHM,
        nullptr,
        0);
    if (!succeeded(status))
    {
        return bcrypt_error(
            HashErrorCode::Provider,
            status,
            "BCrypt could not open SHA-256.");
    }

    std::uint32_t object_size{};
    status =
        read_u32_property(algorithm.get(), BCRYPT_OBJECT_LENGTH, object_size);
    if (!succeeded(status))
    {
        return bcrypt_error(
            HashErrorCode::AlgorithmProperty,
            status,
            "BCrypt could not read the SHA-256 object size.");
    }

    std::uint32_t digest_size{};
    status =
        read_u32_property(algorithm.get(), BCRYPT_HASH_LENGTH, digest_size);
    if (!succeeded(status))
    {
        return bcrypt_error(
            HashErrorCode::AlgorithmProperty,
            status,
            "BCrypt could not read the SHA-256 digest size.");
    }
    if (digest_size != Sha256Digest{}.bytes.size())
    {
        return error(
            HashErrorCode::DigestSize,
            0,
            "BCrypt reported an unexpected SHA-256 digest size.");
    }

    std::vector<unsigned char> object_buffer(object_size);
    HashHandle hash{};
    status = BCryptCreateHash(
        algorithm.get(),
        hash.address(),
        object_buffer.data(),
        static_cast<ULONG>(object_buffer.size()),
        nullptr,
        0,
        0);
    if (!succeeded(status))
    {
        return error(
            HashErrorCode::HashObject,
            status,
            "BCrypt could not create SHA-256 state.");
    }

    const auto update =
        [&hash](std::span<const std::byte> bytes)
        -> std::expected<void, HashError> {
        auto remaining = bytes;
        while (!remaining.empty())
        {
            const auto chunk_size = std::min(
                remaining.size(),
                static_cast<std::size_t>(
                    std::numeric_limits<ULONG>::max()));
            const auto update_status = BCryptHashData(
                hash.get(),
                reinterpret_cast<PUCHAR>(
                    const_cast<std::byte*>(remaining.data())),
                static_cast<ULONG>(chunk_size),
                0);
            if (!succeeded(update_status))
            {
                return bcrypt_error(
                    HashErrorCode::Update,
                    update_status,
                    "BCrypt could not update SHA-256 state.");
            }
            remaining = remaining.subspan(chunk_size);
        }
        return {};
    };

    auto fed = std::forward<Feed>(feed)(update);
    if (!fed)
    {
        return std::unexpected(fed.error());
    }

    Sha256Digest digest{};
    status = BCryptFinishHash(
        hash.get(),
        reinterpret_cast<PUCHAR>(digest.bytes.data()),
        static_cast<ULONG>(digest.bytes.size()),
        0);
    if (!succeeded(status))
    {
        return bcrypt_error(
            HashErrorCode::Finish,
            status,
            "BCrypt could not finish SHA-256.");
    }
    return digest;
}
} // namespace

auto sha256_bytes(std::span<const std::byte> bytes)
    -> std::expected<Sha256Digest, HashError>
{
    return compute_sha256(
        [bytes](const auto& update) -> std::expected<void, HashError> {
            return update(bytes);
        });
}

auto sha256_file(const std::filesystem::path& path)
    -> std::expected<FileHash, HashError>
{
    FileHandle file{CreateFileW(
        path.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN |
            FILE_FLAG_OPEN_REPARSE_POINT,
        nullptr)};
    if (file.get() == INVALID_HANDLE_VALUE)
    {
        return error(
            HashErrorCode::FileOpen,
            static_cast<std::int32_t>(GetLastError()),
            "Windows could not open the file for hashing.");
    }

    LARGE_INTEGER file_size{};
    if (!GetFileSizeEx(file.get(), &file_size) || file_size.QuadPart < 0)
    {
        return error(
            HashErrorCode::FileSize,
            static_cast<std::int32_t>(GetLastError()),
            "Windows could not read the file size.");
    }

    auto digest = compute_sha256(
        [&file](const auto& update)
        -> std::expected<void, HashError> {
            std::array<std::byte, 64U * 1024U> buffer{};
            while (true)
            {
                DWORD bytes_read{};
                if (!ReadFile(
                        file.get(),
                        buffer.data(),
                        static_cast<DWORD>(buffer.size()),
                        &bytes_read,
                        nullptr))
                {
                    return error(
                        HashErrorCode::FileRead,
                        static_cast<std::int32_t>(GetLastError()),
                        "Windows could not read the file for hashing.");
                }
                if (bytes_read == 0)
                {
                    break;
                }
                auto updated = update(
                    std::span<const std::byte>{
                        buffer.data(),
                        static_cast<std::size_t>(bytes_read)});
                if (!updated)
                {
                    return std::unexpected(updated.error());
                }
            }
            return {};
        });
    if (!digest)
    {
        return std::unexpected(digest.error());
    }
    return FileHash{
        static_cast<std::uint64_t>(file_size.QuadPart),
        *digest,
    };
}
} // namespace meccha::common
