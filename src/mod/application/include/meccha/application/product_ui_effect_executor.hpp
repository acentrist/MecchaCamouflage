#pragma once

#include <meccha/application/application_snapshot.hpp>
#include <meccha/application/image_file_picker.hpp>
#include <meccha/application/product_ui_actions.hpp>

#include <cstdint>
#include <expected>
#include <optional>

namespace meccha::application
{
enum class ProductUiEffectErrorCode : std::uint8_t
{
    InvalidSnapshot,
    InvalidEffect,
    StaleEffect,
    Unavailable,
    Picker,
    ImageImport,
};

struct ProductUiEffectError
{
    ProductUiEffectErrorCode code{};
    std::optional<ImageFilePickerError> picker{};
    std::optional<ImageFileImportError> image_import{};

    auto operator==(const ProductUiEffectError&) const
        -> bool = default;
};

struct ProductUiEffectResult
{
    bool cancelled{};
    std::optional<ProductUiActionEnvelope> action{};

    auto operator==(const ProductUiEffectResult&) const
        -> bool = default;
};

class ProductUiEffectExecutor
{
public:
    ProductUiEffectExecutor(
        ApplicationSnapshotPort& snapshots,
        ImageFilePickerPort& picker,
        PresetHasher& hasher);

    [[nodiscard]] auto execute(
        ProductUiEffectEnvelope effect,
        std::uintptr_t owner_window)
        -> std::expected<
            ProductUiEffectResult,
            ProductUiEffectError>;

private:
    ApplicationSnapshotPort& snapshots_;
    ImageFilePickerPort& picker_;
    PresetHasher& hasher_;
};
} // namespace meccha::application
