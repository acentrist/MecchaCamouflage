#pragma once

#include <meccha/application/application_snapshot.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <vector>

namespace meccha::application
{
inline constexpr std::size_t MaximumProductUiDiagnostics = 64U;
inline constexpr std::size_t MaximumProductUiDiagnosticKeyBytes = 128U;

enum class ProductUiSection : std::uint8_t
{
    Paint,
    ImagePaint,
    Esp,
    Settings,
    Diagnostics,
};

inline constexpr std::array<ProductUiSection, 5U>
    ProductUiSections{
        ProductUiSection::Paint,
        ProductUiSection::ImagePaint,
        ProductUiSection::Esp,
        ProductUiSection::Settings,
        ProductUiSection::Diagnostics,
    };

struct FeatureActionAvailability
{
    bool start{};
    bool preview{};
    bool restore{};
    bool cancel{};

    auto operator==(const FeatureActionAvailability&) const
        -> bool = default;
};

struct ImageProjectActionAvailability
{
    bool edit{};
    bool load{};
    bool save{};
    bool rename{};
    bool remove{};
    bool busy{};

    auto operator==(const ImageProjectActionAvailability&) const
        -> bool = default;
};

struct QueuePresentation
{
    std::size_t queued{};
    std::size_t capacity{};
    double utilization{};
    bool accepting{};

    auto operator==(const QueuePresentation&) const
        -> bool = default;
};

struct ProgressPresentation
{
    std::size_t completed{};
    std::size_t total{};
    double fraction{};
    double queue_pressure{};
    std::uint64_t elapsed_ms{};
    std::optional<std::uint64_t> eta_ms{};

    auto operator==(const ProgressPresentation&) const
        -> bool = default;
};

struct PaintPanelModel
{
    core::PaintSettings settings{};
    FeatureActionAvailability actions{};

    auto operator==(const PaintPanelModel&) const -> bool = default;
};

struct ImagePaintPanelModel
{
    core::ImageProjectSettings settings{};
    std::optional<ImageEditorDocumentSnapshot> document{};
    ImageEditorPipelineSnapshot pipeline{};
    FeatureActionAvailability actions{};
    ImageProjectActionAvailability project{};

    auto operator==(const ImagePaintPanelModel&) const
        -> bool = default;
};

struct EspPanelModel
{
    bool enabled{};
    bool can_toggle{};
    core::EspSettings settings{};
    EspFrameSnapshot frame{};

    auto operator==(const EspPanelModel&) const -> bool = default;
};

struct SettingsPanelModel
{
    core::ApplicationConfig config{};
    bool can_apply{};

    auto operator==(const SettingsPanelModel&) const -> bool = default;
};

struct DiagnosticsPanelModel
{
    ApplicationRuntimePhase runtime_phase{
        ApplicationRuntimePhase::Cold};
    CompatibilitySnapshot compatibility{};
    QueuePresentation command_queue{};
    QueuePresentation runtime_queue{};
    std::vector<DiagnosticEntry> entries{};
    std::size_t omitted{};

    auto operator==(const DiagnosticsPanelModel&) const
        -> bool = default;
};

struct ProductUiModel
{
    std::uint64_t source_revision{};
    bool ui_open{};
    std::array<ProductUiSection, ProductUiSections.size()>
        sections{ProductUiSections};
    PaintPanelModel paint{};
    ImagePaintPanelModel image_paint{};
    EspPanelModel esp{};
    SettingsPanelModel settings{};
    DiagnosticsPanelModel diagnostics{};
    JobSnapshot job{};
    PreviewLeaseSnapshot preview{};
    ProgressPresentation progress{};

    auto operator==(const ProductUiModel&) const -> bool = default;
};

enum class ProductUiModelError : std::uint8_t
{
    InvalidSettings,
    InvalidQueue,
    InvalidProgress,
    InvalidDiagnostics,
};

[[nodiscard]] auto build_product_ui_model(
    const ApplicationSnapshot& snapshot)
    -> std::expected<ProductUiModel, ProductUiModelError>;
} // namespace meccha::application
