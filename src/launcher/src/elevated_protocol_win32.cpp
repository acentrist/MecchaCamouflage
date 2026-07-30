#include <meccha/launcher/elevated_loader.hpp>

#include <meccha/core/utf8.hpp>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace meccha::launcher
{
namespace
{
constexpr std::array<std::byte, 8> Magic{
    std::byte{'M'},
    std::byte{'C'},
    std::byte{'V'},
    std::byte{'2'},
    std::byte{'B'},
    std::byte{'R'},
    std::byte{'K'},
    std::byte{0},
};
constexpr std::uint8_t RequestMessage{1U};
constexpr std::uint8_t ResponseMessage{2U};
constexpr std::size_t MaximumMessageBytes{64U * 1024U};
constexpr std::size_t MaximumPathUnits{16U * 1024U};
constexpr std::size_t MaximumErrorBytes{4U * 1024U};

auto protocol_error(std::string detail)
    -> std::unexpected<ElevatedLoaderMutationError>
{
    return std::unexpected(ElevatedLoaderMutationError{
        ElevatedLoaderMutationErrorCode::InvalidRequest,
        std::move(detail),
    });
}

auto nibble(char character) -> std::optional<std::uint8_t>
{
    if (character >= '0' && character <= '9')
    {
        return static_cast<std::uint8_t>(character - '0');
    }
    if (character >= 'a' && character <= 'f')
    {
        return static_cast<std::uint8_t>(
            character - 'a' + 10);
    }
    return std::nullopt;
}

auto decode_nonce(std::string_view value)
    -> std::optional<std::array<std::byte, 16>>
{
    if (value.size() != 32U)
    {
        return std::nullopt;
    }
    auto result = std::array<std::byte, 16>{};
    for (std::size_t index = 0; index < result.size(); ++index)
    {
        const auto high = nibble(value[index * 2U]);
        const auto low = nibble(value[index * 2U + 1U]);
        if (!high || !low)
        {
            return std::nullopt;
        }
        result[index] = static_cast<std::byte>(
            static_cast<std::uint8_t>(
                (*high << 4U) | *low));
    }
    return result;
}

auto encode_nonce(std::span<const std::byte, 16> value)
    -> std::string
{
    constexpr std::array Hex{
        '0', '1', '2', '3', '4', '5', '6', '7',
        '8', '9', 'a', 'b', 'c', 'd', 'e', 'f',
    };
    auto result = std::string{};
    result.reserve(32U);
    for (const auto item : value)
    {
        const auto byte = std::to_integer<std::uint8_t>(item);
        result.push_back(Hex[byte >> 4U]);
        result.push_back(Hex[byte & 0x0FU]);
    }
    return result;
}

auto valid_operation(std::uint8_t value) -> bool
{
    return value <=
           static_cast<std::uint8_t>(
               ElevatedLoaderOperation::Remove);
}

auto valid_action(std::uint8_t value) -> bool
{
    return value <=
           static_cast<std::uint8_t>(
               ElevatedLoaderFileAction::Remove);
}

auto valid_error_code(std::uint8_t value) -> bool
{
    return value <=
           static_cast<std::uint8_t>(
               ElevatedLoaderMutationErrorCode::Io);
}

auto valid_utf16(std::wstring_view value) -> bool
{
    if (value.empty() ||
        value.size() > MaximumPathUnits)
    {
        return false;
    }
    for (std::size_t index = 0; index < value.size(); ++index)
    {
        const auto unit =
            static_cast<std::uint16_t>(value[index]);
        if (unit == 0U)
        {
            return false;
        }
        if (unit >= 0xD800U && unit <= 0xDBFFU)
        {
            if (++index >= value.size())
            {
                return false;
            }
            const auto low =
                static_cast<std::uint16_t>(value[index]);
            if (low < 0xDC00U || low > 0xDFFFU)
            {
                return false;
            }
        }
        else if (unit >= 0xDC00U && unit <= 0xDFFFU)
        {
            return false;
        }
    }
    return true;
}

class Writer
{
public:
    auto raw(std::span<const std::byte> value) -> void
    {
        bytes_.insert(bytes_.end(), value.begin(), value.end());
    }

    auto u8(std::uint8_t value) -> void
    {
        bytes_.push_back(static_cast<std::byte>(value));
    }

    auto u16(std::uint16_t value) -> void
    {
        u8(static_cast<std::uint8_t>(value));
        u8(static_cast<std::uint8_t>(value >> 8U));
    }

    auto u32(std::uint32_t value) -> void
    {
        for (std::size_t shift = 0; shift < 32U; shift += 8U)
        {
            u8(static_cast<std::uint8_t>(value >> shift));
        }
    }

    auto u64(std::uint64_t value) -> void
    {
        for (std::size_t shift = 0; shift < 64U; shift += 8U)
        {
            u8(static_cast<std::uint8_t>(value >> shift));
        }
    }

    auto digest(const Sha256Digest& value) -> void
    {
        raw(value.bytes);
    }

    auto measurement(const FileMeasurement& value) -> void
    {
        u64(value.size);
        digest(value.sha256);
    }

    auto optional_measurement(
        const std::optional<FileMeasurement>& value) -> void
    {
        u8(value ? 1U : 0U);
        if (value)
        {
            measurement(*value);
        }
    }

    auto finish() && -> std::vector<std::byte>
    {
        return std::move(bytes_);
    }

private:
    std::vector<std::byte> bytes_{};
};

class Reader
{
public:
    explicit Reader(std::span<const std::byte> bytes)
        : bytes_{bytes}
    {
    }

    auto raw(std::size_t size) -> std::span<const std::byte>
    {
        if (failed_ || size > bytes_.size() - position_)
        {
            failed_ = true;
            return {};
        }
        const auto result = bytes_.subspan(position_, size);
        position_ += size;
        return result;
    }

    auto u8() -> std::uint8_t
    {
        const auto value = raw(1U);
        return value.empty()
                   ? 0U
                   : std::to_integer<std::uint8_t>(value[0]);
    }

    auto u16() -> std::uint16_t
    {
        auto result = std::uint16_t{};
        for (std::size_t shift = 0; shift < 16U; shift += 8U)
        {
            result |= static_cast<std::uint16_t>(u8()) << shift;
        }
        return result;
    }

    auto u32() -> std::uint32_t
    {
        auto result = std::uint32_t{};
        for (std::size_t shift = 0; shift < 32U; shift += 8U)
        {
            result |= static_cast<std::uint32_t>(u8()) << shift;
        }
        return result;
    }

    auto u64() -> std::uint64_t
    {
        auto result = std::uint64_t{};
        for (std::size_t shift = 0; shift < 64U; shift += 8U)
        {
            result |= static_cast<std::uint64_t>(u8()) << shift;
        }
        return result;
    }

    auto digest() -> Sha256Digest
    {
        auto result = Sha256Digest{};
        const auto value = raw(result.bytes.size());
        if (!failed_)
        {
            std::ranges::copy(value, result.bytes.begin());
        }
        return result;
    }

    auto measurement() -> FileMeasurement
    {
        return FileMeasurement{u64(), digest()};
    }

    auto optional_measurement()
        -> std::optional<FileMeasurement>
    {
        const auto present = u8();
        if (present > 1U)
        {
            failed_ = true;
        }
        return present == 1U
                   ? std::optional{measurement()}
                   : std::nullopt;
    }

    [[nodiscard]] auto complete() const -> bool
    {
        return !failed_ && position_ == bytes_.size();
    }

    [[nodiscard]] auto failed() const -> bool
    {
        return failed_;
    }

private:
    std::span<const std::byte> bytes_{};
    std::size_t position_{};
    bool failed_{};
};

auto write_prefix(
    Writer& writer,
    std::uint8_t message) -> void
{
    writer.raw(Magic);
    writer.u32(ElevatedLoaderMutationSchemaVersion);
    writer.u8(message);
}

auto read_prefix(
    Reader& reader,
    std::uint8_t message) -> bool
{
    const auto magic = reader.raw(Magic.size());
    return !reader.failed() &&
           std::ranges::equal(magic, Magic) &&
           reader.u32() ==
               ElevatedLoaderMutationSchemaVersion &&
           reader.u8() == message;
}
} // namespace

auto encode_elevated_loader_request(
    const ElevatedLoaderMutationRequest& request)
    -> std::expected<
        std::vector<std::byte>,
        ElevatedLoaderMutationError>
{
    static_assert(sizeof(wchar_t) == sizeof(std::uint16_t));
    const auto nonce = decode_nonce(request.request_nonce);
    const auto native_path = request.game_directory.native();
    const auto operation =
        static_cast<std::uint8_t>(request.operation);
    const auto proxy =
        static_cast<std::uint8_t>(request.proxy.action);
    const auto override_file =
        static_cast<std::uint8_t>(
            request.override_file.action);
    if (request.schema_version !=
            ElevatedLoaderMutationSchemaVersion ||
        !nonce || !valid_operation(operation) ||
        !valid_action(proxy) ||
        !valid_action(override_file) ||
        !valid_utf16(native_path))
    {
        return protocol_error(
            "The elevated request cannot be encoded.");
    }

    auto writer = Writer{};
    write_prefix(writer, RequestMessage);
    writer.u8(operation);
    writer.u8(proxy);
    writer.u8(override_file);
    writer.digest(request.manifest_sha256);
    writer.raw(*nonce);
    writer.optional_measurement(
        request.proxy.expected_current);
    writer.measurement(request.proxy.desired);
    writer.optional_measurement(
        request.override_file.expected_current);
    writer.measurement(request.override_file.desired);
    writer.u32(static_cast<std::uint32_t>(native_path.size()));
    for (const auto unit : native_path)
    {
        writer.u16(static_cast<std::uint16_t>(unit));
    }
    auto result = std::move(writer).finish();
    if (result.size() > MaximumMessageBytes)
    {
        return protocol_error(
            "The encoded elevated request is too large.");
    }
    return result;
}

auto decode_elevated_loader_request(
    std::span<const std::byte> bytes)
    -> std::expected<
        ElevatedLoaderMutationRequest,
        ElevatedLoaderMutationError>
{
    if (bytes.empty() || bytes.size() > MaximumMessageBytes)
    {
        return protocol_error(
            "The elevated request frame size is invalid.");
    }
    auto reader = Reader{bytes};
    if (!read_prefix(reader, RequestMessage))
    {
        return protocol_error(
            "The elevated request prefix is invalid.");
    }
    const auto operation = reader.u8();
    const auto proxy_action = reader.u8();
    const auto override_action = reader.u8();
    const auto manifest_sha256 = reader.digest();
    const auto nonce_bytes = reader.raw(16U);
    const auto proxy_current =
        reader.optional_measurement();
    const auto proxy_desired = reader.measurement();
    const auto override_current =
        reader.optional_measurement();
    const auto override_desired = reader.measurement();
    const auto path_units = reader.u32();
    if (reader.failed() ||
        !valid_operation(operation) ||
        !valid_action(proxy_action) ||
        !valid_action(override_action) ||
        path_units == 0U ||
        path_units > MaximumPathUnits ||
        nonce_bytes.size() != 16U)
    {
        return protocol_error(
            "The elevated request fields are invalid.");
    }
    auto native_path = std::wstring{};
    native_path.reserve(path_units);
    for (std::uint32_t index = 0; index < path_units; ++index)
    {
        native_path.push_back(
            static_cast<wchar_t>(reader.u16()));
    }
    if (!reader.complete() || !valid_utf16(native_path))
    {
        return protocol_error(
            "The elevated request path or framing is invalid.");
    }
    std::array<std::byte, 16> nonce{};
    std::ranges::copy(nonce_bytes, nonce.begin());
    return ElevatedLoaderMutationRequest{
        ElevatedLoaderMutationSchemaVersion,
        static_cast<ElevatedLoaderOperation>(operation),
        manifest_sha256,
        encode_nonce(nonce),
        std::filesystem::path{std::move(native_path)},
        ElevatedLoaderFileMutation{
            static_cast<ElevatedLoaderFileAction>(
                proxy_action),
            proxy_current,
            proxy_desired,
        },
        ElevatedLoaderFileMutation{
            static_cast<ElevatedLoaderFileAction>(
                override_action),
            override_current,
            override_desired,
        },
    };
}

auto encode_elevated_loader_response(
    const ElevatedLoaderMutationResponse& response)
    -> std::expected<
        std::vector<std::byte>,
        ElevatedLoaderMutationError>
{
    const auto nonce = decode_nonce(response.request_nonce);
    if (!nonce)
    {
        return protocol_error(
            "The elevated response nonce is invalid.");
    }
    auto writer = Writer{};
    write_prefix(writer, ResponseMessage);
    writer.raw(*nonce);
    if (response.outcome)
    {
        writer.u8(1U);
        writer.u8(
            (response.outcome->proxy_mutated ? 1U : 0U) |
            (response.outcome->override_mutated ? 2U : 0U));
    }
    else
    {
        const auto& failure = response.outcome.error();
        const auto code = static_cast<std::uint8_t>(
            failure.code);
        if (!valid_error_code(code) ||
            failure.detail.empty() ||
            failure.detail.size() > MaximumErrorBytes ||
            !core::valid_utf8(failure.detail) ||
            failure.detail.find('\0') != std::string::npos)
        {
            return protocol_error(
                "The elevated response error is invalid.");
        }
        writer.u8(0U);
        writer.u8(code);
        writer.u16(
            static_cast<std::uint16_t>(
                failure.detail.size()));
        writer.raw(std::as_bytes(std::span{failure.detail}));
    }
    auto result = std::move(writer).finish();
    if (result.size() > MaximumMessageBytes)
    {
        return protocol_error(
            "The encoded elevated response is too large.");
    }
    return result;
}

auto decode_elevated_loader_response(
    std::span<const std::byte> bytes)
    -> std::expected<
        ElevatedLoaderMutationResponse,
        ElevatedLoaderMutationError>
{
    if (bytes.empty() || bytes.size() > MaximumMessageBytes)
    {
        return protocol_error(
            "The elevated response frame size is invalid.");
    }
    auto reader = Reader{bytes};
    if (!read_prefix(reader, ResponseMessage))
    {
        return protocol_error(
            "The elevated response prefix is invalid.");
    }
    const auto nonce_bytes = reader.raw(16U);
    const auto success = reader.u8();
    if (reader.failed() || nonce_bytes.size() != 16U ||
        success > 1U)
    {
        return protocol_error(
            "The elevated response fields are invalid.");
    }
    std::array<std::byte, 16> nonce{};
    std::ranges::copy(nonce_bytes, nonce.begin());
    const auto request_nonce = encode_nonce(nonce);
    if (success == 1U)
    {
        const auto flags = reader.u8();
        if (!reader.complete() || flags > 3U)
        {
            return protocol_error(
                "The elevated success response is invalid.");
        }
        return ElevatedLoaderMutationResponse{
            request_nonce,
            ElevatedLoaderMutationResult{
                (flags & 1U) != 0U,
                (flags & 2U) != 0U,
            },
        };
    }

    const auto code = reader.u8();
    const auto detail_size = reader.u16();
    const auto detail_bytes = reader.raw(detail_size);
    auto detail = std::string{
        reinterpret_cast<const char*>(detail_bytes.data()),
        detail_bytes.size()};
    if (!reader.complete() || !valid_error_code(code) ||
        detail.empty() || detail.size() > MaximumErrorBytes ||
        !core::valid_utf8(detail) ||
        detail.find('\0') != std::string::npos)
    {
        return protocol_error(
            "The elevated error response is invalid.");
    }
    return ElevatedLoaderMutationResponse{
        request_nonce,
        std::unexpected(ElevatedLoaderMutationError{
            static_cast<ElevatedLoaderMutationErrorCode>(code),
            std::move(detail),
        }),
    };
}
} // namespace meccha::launcher
