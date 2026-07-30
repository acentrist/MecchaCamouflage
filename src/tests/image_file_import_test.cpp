#include <meccha/application/image_file_import.hpp>

#include <array>
#include <cstddef>
#include <expected>
#include <iostream>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace
{
using namespace meccha;
using namespace meccha::application;

auto expect(bool condition, std::string_view message) -> bool
{
    if (!condition)
    {
        std::cerr << "FAIL image_file_import: "
                  << message << '\n';
    }
    return condition;
}

class TestHasher final : public PresetHasher
{
public:
    auto hash(std::span<const std::byte> bytes)
        -> std::expected<
            common::Sha256Digest,
            PresetHashError> override
    {
        if (fail)
        {
            return std::unexpected(PresetHashError{
                PresetHashErrorCode::Failure,
                "fake hash failure",
            });
        }
        auto digest = common::Sha256Digest{};
        if (constant)
        {
            digest.bytes.front() = std::byte{0xA5};
            return digest;
        }
        for (auto index = std::size_t{};
             index < bytes.size();
             ++index)
        {
            digest.bytes[index % digest.bytes.size()] ^=
                bytes[index];
        }
        return digest;
    }

    bool fail{};
    bool constant{};
};

auto bytes(std::byte value)
    -> std::shared_ptr<const std::vector<std::byte>>
{
    return std::make_shared<const std::vector<std::byte>>(
        std::initializer_list<std::byte>{value});
}
} // namespace

auto main() -> int
{
    using namespace meccha;
    using namespace meccha::application;

    auto passed = true;
    auto hasher = TestHasher{};
    const auto existing_bytes = bytes(std::byte{0x11});
    const auto existing_id = common::sha256_hex(
        hasher.hash(*existing_bytes).value());
    auto document = ImageEditorDocumentSnapshot{
        "0123456789abcdef0123456789abcdef",
        "Project",
        7U,
        {},
        {core::ImageLayer{
            existing_id,
            "existing.png",
            core::ImageMime::Png,
            existing_bytes->size(),
        }},
    };

    const auto new_bytes = bytes(std::byte{0x22});
    const auto imported = prepare_image_file_import(
        document,
        std::array{
            PickedImageFile{
                "first.webp",
                core::ImageMime::WebP,
                new_bytes,
            },
            PickedImageFile{
                "copy.webp",
                core::ImageMime::WebP,
                new_bytes,
            },
            PickedImageFile{
                "existing-again.png",
                core::ImageMime::Png,
                existing_bytes,
            },
        },
        hasher);
    passed &= expect(
        imported && imported->layers.size() == 3U &&
            imported->sources.size() == 1U &&
            imported->sources.front().bytes == new_bytes &&
            imported->layers[0U].asset_id ==
                imported->layers[1U].asset_id &&
            imported->layers[2U].asset_id == existing_id,
        "file import did not deduplicate new and existing source bytes");

    passed &= expect(
        prepare_image_file_import(
            document,
            std::span<const PickedImageFile>{},
            hasher) ==
            std::unexpected(ImageFileImportError{
                ImageFileImportErrorCode::EmptySelection,
                std::nullopt,
                "No image files were selected.",
            }),
        "empty file selection was accepted");

    auto invalid_document = document;
    invalid_document.layers.front().source_bytes = 0U;
    passed &= expect(
        prepare_image_file_import(
            invalid_document,
            std::array{
                PickedImageFile{
                    "image.png",
                    core::ImageMime::Png,
                    new_bytes,
                },
            },
            hasher)
                .error()
                .code ==
            ImageFileImportErrorCode::InvalidDocument,
        "invalid document metadata was accepted");

    const auto invalid_file = prepare_image_file_import(
        document,
        std::array{
            PickedImageFile{
                "",
                core::ImageMime::Png,
                new_bytes,
            },
        },
        hasher);
    passed &= expect(
        !invalid_file &&
            invalid_file.error().code ==
                ImageFileImportErrorCode::InvalidFile,
        "invalid selected-file metadata was accepted");

    auto full_document = document;
    full_document.layers.resize(
        core::MaximumImageLayers,
        full_document.layers.front());
    const auto layer_limit = prepare_image_file_import(
        full_document,
        std::array{
            PickedImageFile{
                "image.png",
                core::ImageMime::Png,
                new_bytes,
            },
        },
        hasher);
    passed &= expect(
        !layer_limit &&
            layer_limit.error().code ==
                ImageFileImportErrorCode::LayerLimit,
        "layer-capacity exhaustion was accepted");

    auto full_source_bytes_document = document;
    full_source_bytes_document.layers.clear();
    for (auto index = std::size_t{}; index < 6U; ++index)
    {
        full_source_bytes_document.layers.push_back(
            core::ImageLayer{
                std::string(63U, '0') +
                    static_cast<char>('0' + index),
                "existing-" + std::to_string(index) + ".png",
                core::ImageMime::Png,
                index < 5U
                    ? core::MaximumImageSourceBytes
                    : core::MaximumProjectSourceBytes -
                          5U *
                              core::MaximumImageSourceBytes,
            });
    }
    const auto source_bytes_limit = prepare_image_file_import(
        full_source_bytes_document,
        std::array{
            PickedImageFile{
                "one-more.png",
                core::ImageMime::Png,
                new_bytes,
            },
        },
        hasher);
    passed &= expect(
        !source_bytes_limit &&
            source_bytes_limit.error().code ==
                ImageFileImportErrorCode::SourceBytesLimit,
        "project source-byte capacity exhaustion was accepted");

    hasher.constant = true;
    const auto collision = prepare_image_file_import(
        document,
        std::array{
            PickedImageFile{
                "first.png",
                core::ImageMime::Png,
                bytes(std::byte{0x31}),
            },
            PickedImageFile{
                "second.png",
                core::ImageMime::Png,
                bytes(std::byte{0x32}),
            },
        },
        hasher);
    passed &= expect(
        !collision &&
            collision.error().code ==
                ImageFileImportErrorCode::IdentityConflict,
        "conflicting bytes with one source identity were accepted");
    hasher.constant = false;

    hasher.fail = true;
    const auto hash_failure = prepare_image_file_import(
        document,
        std::array{
            PickedImageFile{
                "image.png",
                core::ImageMime::Png,
                new_bytes,
            },
        },
        hasher);
    passed &= expect(
        !hash_failure &&
            hash_failure.error().code ==
                ImageFileImportErrorCode::HashFailure &&
            hash_failure.error().hash,
        "hash failure was not retained");

    return passed ? 0 : 1;
}
