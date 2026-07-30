#include <meccha/launcher/hash.hpp>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <bcrypt.h>

#include <algorithm>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace meccha::launcher
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

[[nodiscard]] auto succeeded(NTSTATUS status) noexcept -> bool
{
    return status >= 0;
}

auto error(HashErrorCode code, NTSTATUS status, std::string detail)
    -> std::unexpected<HashError>
{
    return std::unexpected(HashError{
        code,
        static_cast<std::int32_t>(status),
        std::move(detail),
    });
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
} // namespace

auto sha256_bytes(std::span<const std::byte> bytes)
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
        return error(HashErrorCode::Provider, status, "BCrypt could not open SHA-256.");
    }

    std::uint32_t object_size{};
    status = read_u32_property(algorithm.get(), BCRYPT_OBJECT_LENGTH, object_size);
    if (!succeeded(status))
    {
        return error(
            HashErrorCode::AlgorithmProperty,
            status,
            "BCrypt could not read the SHA-256 object size.");
    }

    std::uint32_t digest_size{};
    status = read_u32_property(algorithm.get(), BCRYPT_HASH_LENGTH, digest_size);
    if (!succeeded(status))
    {
        return error(
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
        return error(HashErrorCode::HashObject, status, "BCrypt could not create SHA-256 state.");
    }

    auto remaining = bytes;
    while (!remaining.empty())
    {
        const auto chunk_size = std::min(
            remaining.size(),
            static_cast<std::size_t>(std::numeric_limits<ULONG>::max()));
        status = BCryptHashData(
            hash.get(),
            reinterpret_cast<PUCHAR>(
                const_cast<std::byte*>(remaining.data())),
            static_cast<ULONG>(chunk_size),
            0);
        if (!succeeded(status))
        {
            return error(HashErrorCode::Update, status, "BCrypt could not update SHA-256 state.");
        }
        remaining = remaining.subspan(chunk_size);
    }

    Sha256Digest digest{};
    status = BCryptFinishHash(
        hash.get(),
        reinterpret_cast<PUCHAR>(digest.bytes.data()),
        static_cast<ULONG>(digest.bytes.size()),
        0);
    if (!succeeded(status))
    {
        return error(HashErrorCode::Finish, status, "BCrypt could not finish SHA-256.");
    }
    return digest;
}
} // namespace meccha::launcher
