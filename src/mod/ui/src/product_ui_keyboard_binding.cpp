#include <meccha/product_ui/product_ui_keyboard_binding.hpp>

#include <array>
#include <atomic>
#include <cstddef>
#include <exception>
#include <memory>
#include <string>
#include <utility>

namespace meccha::product_ui
{
namespace
{
inline constexpr std::size_t MaximumRegistrationDetailBytes = 512U;

enum class SemanticAction : std::uint8_t
{
    FocusNext,
    FocusPrevious,
    Enter,
    Cancel,
    MoveHome,
    MoveEnd,
    MoveLeft,
    MoveRight,
    Backspace,
    DeleteForward,
};

struct SemanticRegistration
{
    ProductUiKeyboardKey key{};
    ProductUiKeyboardModifiers modifiers{};
    SemanticAction action{};
};

constexpr auto SemanticRegistrations = std::array{
    SemanticRegistration{
        ProductUiKeyboardKey::Tab,
        ProductUiKeyboardModifiers::None,
        SemanticAction::FocusNext,
    },
    SemanticRegistration{
        ProductUiKeyboardKey::Tab,
        ProductUiKeyboardModifiers::Shift,
        SemanticAction::FocusPrevious,
    },
    SemanticRegistration{
        ProductUiKeyboardKey::Enter,
        ProductUiKeyboardModifiers::None,
        SemanticAction::Enter,
    },
    SemanticRegistration{
        ProductUiKeyboardKey::Escape,
        ProductUiKeyboardModifiers::None,
        SemanticAction::Cancel,
    },
    SemanticRegistration{
        ProductUiKeyboardKey::Home,
        ProductUiKeyboardModifiers::None,
        SemanticAction::MoveHome,
    },
    SemanticRegistration{
        ProductUiKeyboardKey::End,
        ProductUiKeyboardModifiers::None,
        SemanticAction::MoveEnd,
    },
    SemanticRegistration{
        ProductUiKeyboardKey::Left,
        ProductUiKeyboardModifiers::None,
        SemanticAction::MoveLeft,
    },
    SemanticRegistration{
        ProductUiKeyboardKey::Right,
        ProductUiKeyboardModifiers::None,
        SemanticAction::MoveRight,
    },
    SemanticRegistration{
        ProductUiKeyboardKey::Backspace,
        ProductUiKeyboardModifiers::None,
        SemanticAction::Backspace,
    },
    SemanticRegistration{
        ProductUiKeyboardKey::DeleteForward,
        ProductUiKeyboardModifiers::None,
        SemanticAction::DeleteForward,
    },
};
static_assert(
    SemanticRegistrations.size() ==
    ProductUiKeyboardSemanticRegistrationCount);

constexpr auto PrintableKeys = std::array{
    ProductUiKeyboardKey::Space,
    ProductUiKeyboardKey::Zero,
    ProductUiKeyboardKey::One,
    ProductUiKeyboardKey::Two,
    ProductUiKeyboardKey::Three,
    ProductUiKeyboardKey::Four,
    ProductUiKeyboardKey::Five,
    ProductUiKeyboardKey::Six,
    ProductUiKeyboardKey::Seven,
    ProductUiKeyboardKey::Eight,
    ProductUiKeyboardKey::Nine,
    ProductUiKeyboardKey::A,
    ProductUiKeyboardKey::B,
    ProductUiKeyboardKey::C,
    ProductUiKeyboardKey::D,
    ProductUiKeyboardKey::E,
    ProductUiKeyboardKey::F,
    ProductUiKeyboardKey::G,
    ProductUiKeyboardKey::H,
    ProductUiKeyboardKey::I,
    ProductUiKeyboardKey::J,
    ProductUiKeyboardKey::K,
    ProductUiKeyboardKey::L,
    ProductUiKeyboardKey::M,
    ProductUiKeyboardKey::N,
    ProductUiKeyboardKey::O,
    ProductUiKeyboardKey::P,
    ProductUiKeyboardKey::Q,
    ProductUiKeyboardKey::R,
    ProductUiKeyboardKey::S,
    ProductUiKeyboardKey::T,
    ProductUiKeyboardKey::U,
    ProductUiKeyboardKey::V,
    ProductUiKeyboardKey::W,
    ProductUiKeyboardKey::X,
    ProductUiKeyboardKey::Y,
    ProductUiKeyboardKey::Z,
    ProductUiKeyboardKey::NumpadZero,
    ProductUiKeyboardKey::NumpadOne,
    ProductUiKeyboardKey::NumpadTwo,
    ProductUiKeyboardKey::NumpadThree,
    ProductUiKeyboardKey::NumpadFour,
    ProductUiKeyboardKey::NumpadFive,
    ProductUiKeyboardKey::NumpadSix,
    ProductUiKeyboardKey::NumpadSeven,
    ProductUiKeyboardKey::NumpadEight,
    ProductUiKeyboardKey::NumpadNine,
    ProductUiKeyboardKey::Multiply,
    ProductUiKeyboardKey::Add,
    ProductUiKeyboardKey::Separator,
    ProductUiKeyboardKey::Subtract,
    ProductUiKeyboardKey::Decimal,
    ProductUiKeyboardKey::Divide,
    ProductUiKeyboardKey::OemOne,
    ProductUiKeyboardKey::OemPlus,
    ProductUiKeyboardKey::OemComma,
    ProductUiKeyboardKey::OemMinus,
    ProductUiKeyboardKey::OemPeriod,
    ProductUiKeyboardKey::OemTwo,
    ProductUiKeyboardKey::OemThree,
    ProductUiKeyboardKey::OemFour,
    ProductUiKeyboardKey::OemFive,
    ProductUiKeyboardKey::OemSix,
    ProductUiKeyboardKey::OemSeven,
    ProductUiKeyboardKey::OemEight,
    ProductUiKeyboardKey::Oem102,
};
static_assert(
    PrintableKeys.size() == ProductUiKeyboardPrintableKeyCount);

constexpr auto ModifierCombinations = std::array{
    ProductUiKeyboardModifiers::None,
    ProductUiKeyboardModifiers::Shift,
    ProductUiKeyboardModifiers::ControlAlt,
    ProductUiKeyboardModifiers::ShiftControlAlt,
};
static_assert(
    ModifierCombinations.size() ==
    ProductUiKeyboardModifierCombinationCount);

auto bounded_detail(std::string detail) -> std::string
{
    if (detail.size() > MaximumRegistrationDetailBytes)
    {
        detail.resize(MaximumRegistrationDetailBytes);
    }
    return detail;
}

auto text_event(SemanticAction action)
    -> std::optional<ui::TextEditEventKind>
{
    switch (action)
    {
    case SemanticAction::MoveHome:
        return ui::TextEditEventKind::MoveHome;
    case SemanticAction::MoveEnd:
        return ui::TextEditEventKind::MoveEnd;
    case SemanticAction::MoveLeft:
        return ui::TextEditEventKind::MoveLeft;
    case SemanticAction::MoveRight:
        return ui::TextEditEventKind::MoveRight;
    case SemanticAction::Backspace:
        return ui::TextEditEventKind::Backspace;
    case SemanticAction::DeleteForward:
        return ui::TextEditEventKind::DeleteForward;
    default:
        return std::nullopt;
    }
}

auto semantic_callback(
    std::shared_ptr<std::atomic_bool> active,
    std::shared_ptr<ProductUiInputQueue> queue,
    SemanticAction action) -> ProductUiKeyboardCallback
{
    return [active = std::move(active),
            queue = std::move(queue),
            action]() noexcept
    {
        if (!active->load(std::memory_order_acquire))
        {
            return;
        }
        try
        {
            switch (action)
            {
            case SemanticAction::FocusNext:
                static_cast<void>(
                    queue->record_keyboard_navigation(
                        ProductUiNavigationInput::FocusNext));
                return;
            case SemanticAction::FocusPrevious:
                static_cast<void>(
                    queue->record_keyboard_navigation(
                        ProductUiNavigationInput::FocusPrevious));
                return;
            case SemanticAction::Enter:
                static_cast<void>(queue->record_keyboard_enter());
                return;
            case SemanticAction::Cancel:
                static_cast<void>(queue->record_keyboard_cancel());
                return;
            default:
                break;
            }
            const auto event = text_event(action);
            if (event)
            {
                static_cast<void>(
                    queue->record_keyboard_text_edit(
                        ui::TextEditEvent{*event, {}}));
            }
        }
        catch (...)
        {
            queue->stop();
        }
    };
}

auto printable_callback(
    std::shared_ptr<std::atomic_bool> active,
    std::shared_ptr<ProductUiInputQueue> queue,
    std::shared_ptr<const ProductUiKeyboardTextTranslator>
        translator,
    ProductUiKeyboardKey key) -> ProductUiKeyboardCallback
{
    return [active = std::move(active),
            queue = std::move(queue),
            translator = std::move(translator),
            key]() noexcept
    {
        if (!active->load(std::memory_order_acquire))
        {
            return;
        }
        try
        {
            const auto mode = queue->keyboard_input_mode();
            if (mode == ProductUiKeyboardInputMode::Navigation)
            {
                if (key == ProductUiKeyboardKey::Space)
                {
                    static_cast<void>(
                        queue->record_keyboard_enter());
                }
                return;
            }
            if (mode != ProductUiKeyboardInputMode::TextEdit)
            {
                return;
            }
            const auto translated = (*translator)(key);
            if (translated && !translated->empty())
            {
                static_cast<void>(
                    queue->record_keyboard_text_edit(
                        ui::TextEditEvent{
                            ui::TextEditEventKind::Insert,
                            std::move(*translated),
                        }));
            }
        }
        catch (...)
        {
            queue->stop();
        }
    };
}
} // namespace

ProductUiKeyboardBinding::ProductUiKeyboardBinding(
    std::shared_ptr<ProductUiInputQueue> queue,
    ProductUiKeyboardTextTranslator translator)
    : queue_{std::move(queue)},
      translator_{
          translator
              ? std::make_shared<
                    const ProductUiKeyboardTextTranslator>(
                    std::move(translator))
              : nullptr},
      active_{std::make_shared<std::atomic_bool>(false)}
{
}

ProductUiKeyboardBinding::~ProductUiKeyboardBinding()
{
    stop();
}

auto ProductUiKeyboardBinding::start(
    ProductUiKeyboardRegistrationPort& registrar)
    -> std::expected<
        ProductUiKeyboardBindingSnapshot,
        ProductUiKeyboardBindingError>
{
    const auto lock = std::scoped_lock{mutex_};
    if (snapshot_.phase != ProductUiKeyboardBindingPhase::Cold)
    {
        return std::unexpected(ProductUiKeyboardBindingError{
            ProductUiKeyboardBindingErrorCode::InvalidState,
            ProductUiKeyboardKey::Tab,
            ProductUiKeyboardModifiers::None,
            "Keyboard registration was already started.",
        });
    }
    if (!queue_)
    {
        snapshot_.phase = ProductUiKeyboardBindingPhase::Failed;
        return std::unexpected(ProductUiKeyboardBindingError{
            ProductUiKeyboardBindingErrorCode::InvalidQueue,
            ProductUiKeyboardKey::Tab,
            ProductUiKeyboardModifiers::None,
            "The product input queue is unavailable.",
        });
    }
    if (!translator_)
    {
        queue_->stop();
        snapshot_.phase = ProductUiKeyboardBindingPhase::Failed;
        return std::unexpected(ProductUiKeyboardBindingError{
            ProductUiKeyboardBindingErrorCode::InvalidTranslator,
            ProductUiKeyboardKey::Space,
            ProductUiKeyboardModifiers::None,
            "The keyboard text translator is unavailable.",
        });
    }

    const auto register_one =
        [&](ProductUiKeyboardKey key,
            ProductUiKeyboardModifiers modifiers,
            ProductUiKeyboardCallback callback)
        -> std::expected<void, ProductUiKeyboardBindingError>
    {
        try
        {
            const auto registered = registrar.register_keyboard_key(
                key,
                modifiers,
                std::move(callback));
            if (!registered)
            {
                return std::unexpected(
                    ProductUiKeyboardBindingError{
                        ProductUiKeyboardBindingErrorCode::
                            Registration,
                        key,
                        modifiers,
                        bounded_detail(
                            std::move(registered.error().detail)),
                    });
            }
            ++snapshot_.registered_keys;
            return {};
        }
        catch (const std::exception& error)
        {
            return std::unexpected(ProductUiKeyboardBindingError{
                ProductUiKeyboardBindingErrorCode::Unexpected,
                key,
                modifiers,
                bounded_detail(error.what()),
            });
        }
        catch (...)
        {
            return std::unexpected(ProductUiKeyboardBindingError{
                ProductUiKeyboardBindingErrorCode::Unexpected,
                key,
                modifiers,
                "Keyboard registration threw an unknown exception.",
            });
        }
    };

    for (const auto& registration : SemanticRegistrations)
    {
        const auto registered = register_one(
            registration.key,
            registration.modifiers,
            semantic_callback(
                active_,
                queue_,
                registration.action));
        if (!registered)
        {
            active_->store(false, std::memory_order_release);
            queue_->stop();
            snapshot_.phase = ProductUiKeyboardBindingPhase::Failed;
            return std::unexpected(registered.error());
        }
    }
    for (const auto key : PrintableKeys)
    {
        for (const auto modifiers : ModifierCombinations)
        {
            const auto registered = register_one(
                key,
                modifiers,
                printable_callback(
                    active_,
                    queue_,
                    translator_,
                    key));
            if (!registered)
            {
                active_->store(false, std::memory_order_release);
                queue_->stop();
                snapshot_.phase =
                    ProductUiKeyboardBindingPhase::Failed;
                return std::unexpected(registered.error());
            }
        }
    }

    active_->store(true, std::memory_order_release);
    snapshot_.phase = ProductUiKeyboardBindingPhase::Active;
    return snapshot_;
}

auto ProductUiKeyboardBinding::stop() noexcept -> void
{
    const auto lock = std::scoped_lock{mutex_};
    active_->store(false, std::memory_order_release);
    if (snapshot_.phase != ProductUiKeyboardBindingPhase::Stopped)
    {
        snapshot_.phase = ProductUiKeyboardBindingPhase::Stopped;
    }
}

auto ProductUiKeyboardBinding::snapshot() const
    -> ProductUiKeyboardBindingSnapshot
{
    const auto lock = std::scoped_lock{mutex_};
    return snapshot_;
}
} // namespace meccha::product_ui
