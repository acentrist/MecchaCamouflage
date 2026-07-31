#pragma once

#include <meccha/core/config.hpp>
#include <meccha/product_ui/product_ui_input_queue.hpp>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <mutex>
#include <string>

namespace meccha::product_ui
{
inline constexpr std::size_t
    ProductUiFunctionKeyRegistrationCount = 24U;

using ProductUiFunctionKeyCallback = std::function<void()>;

struct ProductUiFunctionKeyRegistrationError
{
    std::string detail{};

    auto operator==(
        const ProductUiFunctionKeyRegistrationError&) const
        -> bool = default;
};

class ProductUiFunctionKeyRegistrationPort
{
public:
    ProductUiFunctionKeyRegistrationPort() = default;
    ProductUiFunctionKeyRegistrationPort(
        const ProductUiFunctionKeyRegistrationPort&) = delete;
    auto operator=(
        const ProductUiFunctionKeyRegistrationPort&)
        -> ProductUiFunctionKeyRegistrationPort& = delete;
    virtual ~ProductUiFunctionKeyRegistrationPort() = default;

    virtual auto register_function_key(
        core::FunctionKey key,
        ProductUiFunctionKeyCallback callback)
        -> std::expected<
            void,
            ProductUiFunctionKeyRegistrationError> = 0;
};

enum class ProductUiKeyBindingPhase : std::uint8_t
{
    Cold,
    Active,
    Failed,
    Stopped,
};

enum class ProductUiKeyBindingErrorCode : std::uint8_t
{
    InvalidQueue,
    InvalidState,
    Registration,
    Unexpected,
};

struct ProductUiKeyBindingError
{
    ProductUiKeyBindingErrorCode code{
        ProductUiKeyBindingErrorCode::InvalidState};
    core::FunctionKey key{core::FunctionKey::F1};
    std::string detail{};

    auto operator==(const ProductUiKeyBindingError&) const
        -> bool = default;
};

struct ProductUiKeyBindingSnapshot
{
    ProductUiKeyBindingPhase phase{
        ProductUiKeyBindingPhase::Cold};
    std::size_t registered_keys{};

    auto operator==(const ProductUiKeyBindingSnapshot&) const
        -> bool = default;
};

class ProductUiFunctionKeyBinding
{
public:
    explicit ProductUiFunctionKeyBinding(
        std::shared_ptr<ProductUiInputQueue> queue);
    ProductUiFunctionKeyBinding(
        const ProductUiFunctionKeyBinding&) = delete;
    auto operator=(const ProductUiFunctionKeyBinding&)
        -> ProductUiFunctionKeyBinding& = delete;
    ~ProductUiFunctionKeyBinding();

    [[nodiscard]] auto start(
        ProductUiFunctionKeyRegistrationPort& registrar)
        -> std::expected<
            ProductUiKeyBindingSnapshot,
            ProductUiKeyBindingError>;

    auto stop() noexcept -> void;

    [[nodiscard]] auto snapshot() const
        -> ProductUiKeyBindingSnapshot;

private:
    std::shared_ptr<ProductUiInputQueue> queue_{};
    mutable std::mutex mutex_{};
    ProductUiKeyBindingSnapshot snapshot_{};
};
} // namespace meccha::product_ui
