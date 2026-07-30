#include "image_decoder_wic.hpp"

#include <Windows.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

namespace meccha::application
{
namespace
{
using Microsoft::WRL::ComPtr;

class ComApartment
{
public:
    ComApartment() noexcept
        : result_{CoInitializeEx(
              nullptr,
              COINIT_MULTITHREADED)}
    {
    }

    ComApartment(const ComApartment&) = delete;
    auto operator=(const ComApartment&) -> ComApartment& = delete;

    ~ComApartment()
    {
        if (result_ == S_OK || result_ == S_FALSE)
        {
            CoUninitialize();
        }
    }

    [[nodiscard]] auto usable() const noexcept -> bool
    {
        return SUCCEEDED(result_) ||
               result_ == RPC_E_CHANGED_MODE;
    }

private:
    HRESULT result_{};
};

auto expected_container(core::ImageMime mime) -> const GUID*
{
    switch (mime)
    {
    case core::ImageMime::Png:
        return &GUID_ContainerFormatPng;
    case core::ImageMime::Jpeg:
        return &GUID_ContainerFormatJpeg;
    case core::ImageMime::WebP:
        return nullptr;
    }
    return nullptr;
}
} // namespace

auto decode_wic_image(
    std::string_view asset_id,
    core::ImageMime mime,
    std::span<const std::byte> encoded,
    std::stop_token cancellation)
    -> std::expected<core::DecodedImageSource, ImageDecodeError>
{
    if (cancellation.stop_requested())
    {
        return std::unexpected(ImageDecodeError::Cancelled);
    }
    const auto expected = expected_container(mime);
    if (expected == nullptr)
    {
        return std::unexpected(ImageDecodeError::UnsupportedMime);
    }

    const auto apartment = ComApartment{};
    if (!apartment.usable())
    {
        return std::unexpected(ImageDecodeError::PlatformFailure);
    }

    auto factory = ComPtr<IWICImagingFactory>{};
    auto result = CoCreateInstance(
        CLSID_WICImagingFactory,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(factory.GetAddressOf()));
    if (FAILED(result))
    {
        return std::unexpected(ImageDecodeError::PlatformFailure);
    }

    auto stream = ComPtr<IWICStream>{};
    result = factory->CreateStream(stream.GetAddressOf());
    if (FAILED(result))
    {
        return std::unexpected(ImageDecodeError::PlatformFailure);
    }
    result = stream->InitializeFromMemory(
        const_cast<BYTE*>(
            reinterpret_cast<const BYTE*>(encoded.data())),
        static_cast<DWORD>(encoded.size()));
    if (FAILED(result))
    {
        return std::unexpected(ImageDecodeError::MalformedImage);
    }

    auto decoder = ComPtr<IWICBitmapDecoder>{};
    result = factory->CreateDecoderFromStream(
        stream.Get(),
        nullptr,
        WICDecodeMetadataCacheOnDemand,
        decoder.GetAddressOf());
    if (FAILED(result))
    {
        return std::unexpected(ImageDecodeError::MalformedImage);
    }
    auto container = GUID{};
    result = decoder->GetContainerFormat(&container);
    if (FAILED(result) || container != *expected)
    {
        return std::unexpected(ImageDecodeError::HeaderMismatch);
    }
    auto frame_count = UINT{};
    result = decoder->GetFrameCount(&frame_count);
    if (FAILED(result) || frame_count != 1U)
    {
        return std::unexpected(ImageDecodeError::MalformedImage);
    }

    auto frame = ComPtr<IWICBitmapFrameDecode>{};
    result = decoder->GetFrame(0U, frame.GetAddressOf());
    if (FAILED(result))
    {
        return std::unexpected(ImageDecodeError::MalformedImage);
    }
    auto width = UINT{};
    auto height = UINT{};
    result = frame->GetSize(&width, &height);
    if (FAILED(result))
    {
        return std::unexpected(ImageDecodeError::MalformedImage);
    }
    const auto bytes = checked_decoded_rgba_bytes(width, height);
    if (!bytes)
    {
        return std::unexpected(ImageDecodeError::DimensionLimit);
    }
    if (cancellation.stop_requested())
    {
        return std::unexpected(ImageDecodeError::Cancelled);
    }

    auto converter = ComPtr<IWICFormatConverter>{};
    result = factory->CreateFormatConverter(
        converter.GetAddressOf());
    if (FAILED(result))
    {
        return std::unexpected(ImageDecodeError::PlatformFailure);
    }
    auto source_format = GUID{};
    result = frame->GetPixelFormat(&source_format);
    if (FAILED(result))
    {
        return std::unexpected(ImageDecodeError::MalformedImage);
    }
    if (cancellation.stop_requested())
    {
        return std::unexpected(ImageDecodeError::Cancelled);
    }
    auto can_convert = BOOL{};
    result = converter->CanConvert(
        source_format,
        GUID_WICPixelFormat32bppRGBA,
        &can_convert);
    if (FAILED(result) || can_convert == FALSE)
    {
        return std::unexpected(ImageDecodeError::MalformedImage);
    }
    result = converter->Initialize(
        frame.Get(),
        GUID_WICPixelFormat32bppRGBA,
        WICBitmapDitherTypeNone,
        nullptr,
        0.0,
        WICBitmapPaletteTypeCustom);
    if (FAILED(result))
    {
        return std::unexpected(ImageDecodeError::MalformedImage);
    }

    auto rgba = std::make_shared<std::vector<std::byte>>(*bytes);
    const auto stride = width * 4U;
    result = converter->CopyPixels(
        nullptr,
        stride,
        static_cast<UINT>(rgba->size()),
        reinterpret_cast<BYTE*>(rgba->data()));
    if (FAILED(result))
    {
        return std::unexpected(ImageDecodeError::MalformedImage);
    }
    return core::DecodedImageSource{
        std::string{asset_id},
        width,
        height,
        std::move(rgba),
    };
}
} // namespace meccha::application
