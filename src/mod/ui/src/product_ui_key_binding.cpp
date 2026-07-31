#include <meccha/product_ui/product_ui_key_binding.hpp>

#include <exception>
#include <mutex>
#include <string>
#include <utility>

namespace meccha::product_ui
{
namespace
{
inline constexpr std::size_t MaximumRegistrationDetailBytes =
    512U;
static_assert(
    static_cast<std::uint8_t>(core::FunctionKey::F24) -
            static_cast<std::uint8_t>(core::FunctionKey::F1) +
            1U ==
        ProductUiFunctionKeyRegistrationCount);

auto bounded_detail(std::string detail) -> std::string
{
    if (detail.size() > MaximumRegistrationDetailBytes)
    {
        detail.resize(MaximumRegistrationDetailBytes);
    }
    return detail;
}

auto function_key(std::size_t index) -> core::FunctionKey
{
    return static_cast<core::FunctionKey>(
        static_cast<std::uint8_t>(core::FunctionKey::F1) +
        static_cast<std::uint8_t>(index));
}

auto callback_for(
    std::shared_ptr<ProductUiInputQueue> queue,
    core::FunctionKey key) -> ProductUiFunctionKeyCallback
{
    return [queue = std::move(queue), key]() noexcept
    {
        try
        {
            static_cast<void>(
                queue->record_function_key(key));
        }
        catch (...)
        {
            queue->stop();
        }
    };
}
} // namespace

ProductUiFunctionKeyBinding::ProductUiFunctionKeyBinding(
    std::shared_ptr<ProductUiInputQueue> queue)
    : queue_{std::move(queue)}
{
}

ProductUiFunctionKeyBinding::~ProductUiFunctionKeyBinding()
{
    stop();
}

auto ProductUiFunctionKeyBinding::start(
    ProductUiFunctionKeyRegistrationPort& registrar)
    -> std::expected<
        ProductUiKeyBindingSnapshot,
        ProductUiKeyBindingError>
{
    const auto lock = std::scoped_lock{mutex_};
    if (snapshot_.phase != ProductUiKeyBindingPhase::Cold)
    {
        return std::unexpected(ProductUiKeyBindingError{
            ProductUiKeyBindingErrorCode::InvalidState,
            core::FunctionKey::F1,
            "Function-key registration was already started.",
        });
    }
    if (!queue_)
    {
        snapshot_.phase = ProductUiKeyBindingPhase::Failed;
        return std::unexpected(ProductUiKeyBindingError{
            ProductUiKeyBindingErrorCode::InvalidQueue,
            core::FunctionKey::F1,
            "The product input queue is unavailable.",
        });
    }

    for (auto index = std::size_t{};
         index < ProductUiFunctionKeyRegistrationCount;
         ++index)
    {
        const auto key = function_key(index);
        std::expected<
            void,
            ProductUiFunctionKeyRegistrationError>
            registered;
        try
        {
            registered = registrar.register_function_key(
                key,
                callback_for(queue_, key));
        }
        catch (const std::exception& error)
        {
            queue_->stop();
            snapshot_.phase =
                ProductUiKeyBindingPhase::Failed;
            return std::unexpected(ProductUiKeyBindingError{
                ProductUiKeyBindingErrorCode::Unexpected,
                key,
                bounded_detail(error.what()),
            });
        }
        catch (...)
        {
            queue_->stop();
            snapshot_.phase =
                ProductUiKeyBindingPhase::Failed;
            return std::unexpected(ProductUiKeyBindingError{
                ProductUiKeyBindingErrorCode::Unexpected,
                key,
                "Function-key registration threw an unknown "
                "exception.",
            });
        }
        if (!registered)
        {
            queue_->stop();
            snapshot_.phase =
                ProductUiKeyBindingPhase::Failed;
            return std::unexpected(ProductUiKeyBindingError{
                ProductUiKeyBindingErrorCode::Registration,
                key,
                bounded_detail(
                    std::move(registered.error().detail)),
            });
        }
        ++snapshot_.registered_keys;
    }

    snapshot_.phase = ProductUiKeyBindingPhase::Active;
    return snapshot_;
}

auto ProductUiFunctionKeyBinding::stop() noexcept -> void
{
    const auto lock = std::scoped_lock{mutex_};
    if (snapshot_.phase == ProductUiKeyBindingPhase::Stopped)
    {
        return;
    }
    if (queue_)
    {
        queue_->stop();
    }
    snapshot_.phase = ProductUiKeyBindingPhase::Stopped;
}

auto ProductUiFunctionKeyBinding::snapshot() const
    -> ProductUiKeyBindingSnapshot
{
    const auto lock = std::scoped_lock{mutex_};
    return snapshot_;
}
} // namespace meccha::product_ui
