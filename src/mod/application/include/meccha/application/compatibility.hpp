#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace meccha::application
{
enum class CompatibilityStatus : std::uint8_t
{
    Unknown,
    Compatible,
    UnsupportedGame,
    RuntimeError,
};

enum class RuntimeContractId : std::uint8_t
{
    RuntimeInitialization,
    HudCallback,
    World,
    PlayerController,
    Hud,
    Canvas,
    PaintAtUvWithBrush,
    ImagePaintTexture,
    TextureMutation,
    InputControl,
};

enum class ContractFailureKind : std::uint8_t
{
    MissingObject,
    WrongClass,
    MissingProperty,
    WrongPropertyKind,
    MissingFunction,
    ParameterSizeMismatch,
    StaleObject,
    InvalidValue,
    CallbackFailure,
    ExecutionFailure,
    UnsupportedGameBuild,
};

struct CompatibilityFailure
{
    RuntimeContractId contract{};
    ContractFailureKind kind{};
    std::string message_key{};

    auto operator==(const CompatibilityFailure&) const -> bool = default;
};

struct CompatibilitySnapshot
{
    CompatibilityStatus status{CompatibilityStatus::Unknown};
    std::optional<CompatibilityFailure> failure{};

    auto operator==(const CompatibilitySnapshot&) const -> bool = default;
};

class CompatibilityState
{
public:
    auto mark_compatible() -> void;
    auto fail(CompatibilityFailure failure) -> void;

    [[nodiscard]] auto snapshot() const -> CompatibilitySnapshot;

private:
    CompatibilitySnapshot snapshot_{};
};
} // namespace meccha::application
