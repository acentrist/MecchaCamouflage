#include <meccha/launcher/elevated_loader.hpp>
#include <meccha/launcher/hash.hpp>

#include <cstddef>
#include <filesystem>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace
{
using namespace meccha::launcher;
namespace fs = std::filesystem;

auto expect(bool condition, std::string_view message) -> bool
{
    if (!condition)
    {
        std::cerr << "FAIL elevated_protocol_win32: "
                  << message << '\n';
    }
    return condition;
}

auto digest(std::string_view value) -> Sha256Digest
{
    return sha256_bytes(
               std::as_bytes(std::span{value}))
        .value();
}

auto measurement(std::string_view value) -> FileMeasurement
{
    return FileMeasurement{value.size(), digest(value)};
}
} // namespace

auto main() -> int
{
    using namespace meccha::launcher;

    auto passed = true;
    const auto request = ElevatedLoaderMutationRequest{
        ElevatedLoaderMutationSchemaVersion,
        ElevatedLoaderOperation::Apply,
        digest("manifest"),
        "0123456789abcdef0123456789abcdef",
        fs::path{L"C:\\Games\\日本語\\Chameleon\\Binaries\\Win64"},
        ElevatedLoaderFileMutation{
            ElevatedLoaderFileAction::Verify,
            measurement("proxy"),
            measurement("proxy"),
        },
        ElevatedLoaderFileMutation{
            ElevatedLoaderFileAction::Install,
            std::nullopt,
            measurement("override"),
        },
    };
    const auto encoded = encode_elevated_loader_request(request);
    const auto decoded = encoded
                             ? decode_elevated_loader_request(
                                   *encoded)
                             : std::expected<
                                   ElevatedLoaderMutationRequest,
                                   ElevatedLoaderMutationError>{
                                   std::unexpected(
                                       encoded.error())};
    passed &= expect(
        encoded && decoded && *decoded == request,
        "request did not round trip exactly");

    if (encoded)
    {
        auto all_truncations_rejected = true;
        for (std::size_t size = 0; size < encoded->size(); ++size)
        {
            all_truncations_rejected &=
                !decode_elevated_loader_request(
                     std::span<const std::byte>{*encoded}.first(size));
        }
        auto trailing = *encoded;
        trailing.push_back(std::byte{0});
        auto unknown_action = *encoded;
        constexpr std::size_t ProxyActionOffset{14U};
        unknown_action[ProxyActionOffset] = std::byte{0xff};
        passed &= expect(
            all_truncations_rejected &&
                !decode_elevated_loader_request(trailing) &&
                !decode_elevated_loader_request(unknown_action),
            "truncated, trailing, or unknown-action request was accepted");
    }

    auto embedded_nul = request;
    embedded_nul.game_directory =
        fs::path{std::wstring{L"C:\\Games\0Hidden", 15U}};
    passed &= expect(
        !encode_elevated_loader_request(embedded_nul),
        "embedded-NUL game path was encoded");

    const auto success = ElevatedLoaderMutationResponse{
        request.request_nonce,
        ElevatedLoaderMutationResult{true, false},
    };
    const auto success_bytes =
        encode_elevated_loader_response(success);
    const auto success_round_trip =
        success_bytes
            ? decode_elevated_loader_response(*success_bytes)
            : std::expected<
                  ElevatedLoaderMutationResponse,
                  ElevatedLoaderMutationError>{
                  std::unexpected(success_bytes.error())};
    passed &= expect(
        success_bytes && success_round_trip &&
            *success_round_trip == success,
        "success response did not round trip");

    const auto failure = ElevatedLoaderMutationResponse{
        request.request_nonce,
        std::unexpected(ElevatedLoaderMutationError{
            ElevatedLoaderMutationErrorCode::Precondition,
            "対象ファイル changed",
        }),
    };
    const auto failure_bytes =
        encode_elevated_loader_response(failure);
    const auto failure_round_trip =
        failure_bytes
            ? decode_elevated_loader_response(*failure_bytes)
            : std::expected<
                  ElevatedLoaderMutationResponse,
                  ElevatedLoaderMutationError>{
                  std::unexpected(failure_bytes.error())};
    passed &= expect(
        failure_bytes && failure_round_trip &&
            *failure_round_trip == failure,
        "error response did not round trip");

    if (failure_bytes)
    {
        auto trailing = *failure_bytes;
        trailing.push_back(std::byte{0});
        auto truncated = *failure_bytes;
        truncated.pop_back();
        passed &= expect(
            !decode_elevated_loader_response(trailing) &&
                !decode_elevated_loader_response(truncated),
            "malformed response framing was accepted");
    }

    auto oversized = failure;
    oversized.outcome = std::unexpected(
        ElevatedLoaderMutationError{
            ElevatedLoaderMutationErrorCode::Io,
            std::string(5000U, 'x'),
        });
    passed &= expect(
        !encode_elevated_loader_response(oversized),
        "oversized error detail was encoded");

    if (passed)
    {
        std::cout << "PASS elevated_protocol_win32\n";
        return 0;
    }
    return 1;
}
