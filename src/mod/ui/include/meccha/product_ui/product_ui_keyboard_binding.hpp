#pragma once

#include <meccha/product_ui/product_ui_input_queue.hpp>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

namespace meccha::product_ui
{
enum class ProductUiKeyboardKey : std::uint8_t
{
    Backspace = 0x08U,
    Tab = 0x09U,
    Enter = 0x0DU,
    Escape = 0x1BU,
    Space = 0x20U,
    End = 0x23U,
    Home = 0x24U,
    Left = 0x25U,
    Right = 0x27U,
    DeleteForward = 0x2EU,
    Zero = 0x30U,
    One = 0x31U,
    Two = 0x32U,
    Three = 0x33U,
    Four = 0x34U,
    Five = 0x35U,
    Six = 0x36U,
    Seven = 0x37U,
    Eight = 0x38U,
    Nine = 0x39U,
    A = 0x41U,
    B = 0x42U,
    C = 0x43U,
    D = 0x44U,
    E = 0x45U,
    F = 0x46U,
    G = 0x47U,
    H = 0x48U,
    I = 0x49U,
    J = 0x4AU,
    K = 0x4BU,
    L = 0x4CU,
    M = 0x4DU,
    N = 0x4EU,
    O = 0x4FU,
    P = 0x50U,
    Q = 0x51U,
    R = 0x52U,
    S = 0x53U,
    T = 0x54U,
    U = 0x55U,
    V = 0x56U,
    W = 0x57U,
    X = 0x58U,
    Y = 0x59U,
    Z = 0x5AU,
    NumpadZero = 0x60U,
    NumpadOne = 0x61U,
    NumpadTwo = 0x62U,
    NumpadThree = 0x63U,
    NumpadFour = 0x64U,
    NumpadFive = 0x65U,
    NumpadSix = 0x66U,
    NumpadSeven = 0x67U,
    NumpadEight = 0x68U,
    NumpadNine = 0x69U,
    Multiply = 0x6AU,
    Add = 0x6BU,
    Separator = 0x6CU,
    Subtract = 0x6DU,
    Decimal = 0x6EU,
    Divide = 0x6FU,
    OemOne = 0xBAU,
    OemPlus = 0xBBU,
    OemComma = 0xBCU,
    OemMinus = 0xBDU,
    OemPeriod = 0xBEU,
    OemTwo = 0xBFU,
    OemThree = 0xC0U,
    OemFour = 0xDBU,
    OemFive = 0xDCU,
    OemSix = 0xDDU,
    OemSeven = 0xDEU,
    OemEight = 0xDFU,
    Oem102 = 0xE2U,
};

enum class ProductUiKeyboardModifiers : std::uint8_t
{
    None,
    Shift,
    ControlAlt,
    ShiftControlAlt,
};

inline constexpr std::size_t
    ProductUiKeyboardSemanticRegistrationCount = 10U;
inline constexpr std::size_t
    ProductUiKeyboardPrintableKeyCount = 66U;
inline constexpr std::size_t
    ProductUiKeyboardModifierCombinationCount = 4U;
inline constexpr std::size_t ProductUiKeyboardRegistrationCount =
    ProductUiKeyboardSemanticRegistrationCount +
    ProductUiKeyboardPrintableKeyCount *
        ProductUiKeyboardModifierCombinationCount;

using ProductUiKeyboardCallback = std::function<void()>;
using ProductUiKeyboardTextTranslator = std::function<
    std::optional<std::string>(ProductUiKeyboardKey)>;

struct ProductUiKeyboardRegistrationError
{
    std::string detail{};

    auto operator==(const ProductUiKeyboardRegistrationError&) const
        -> bool = default;
};

class ProductUiKeyboardRegistrationPort
{
public:
    ProductUiKeyboardRegistrationPort() = default;
    ProductUiKeyboardRegistrationPort(
        const ProductUiKeyboardRegistrationPort&) = delete;
    auto operator=(const ProductUiKeyboardRegistrationPort&)
        -> ProductUiKeyboardRegistrationPort& = delete;
    virtual ~ProductUiKeyboardRegistrationPort() = default;

    virtual auto register_keyboard_key(
        ProductUiKeyboardKey key,
        ProductUiKeyboardModifiers modifiers,
        ProductUiKeyboardCallback callback)
        -> std::expected<
            void,
            ProductUiKeyboardRegistrationError> = 0;
};

enum class ProductUiKeyboardBindingPhase : std::uint8_t
{
    Cold,
    Active,
    Failed,
    Stopped,
};

enum class ProductUiKeyboardBindingErrorCode : std::uint8_t
{
    InvalidQueue,
    InvalidTranslator,
    InvalidState,
    Registration,
    Unexpected,
};

struct ProductUiKeyboardBindingError
{
    ProductUiKeyboardBindingErrorCode code{
        ProductUiKeyboardBindingErrorCode::InvalidState};
    ProductUiKeyboardKey key{ProductUiKeyboardKey::Tab};
    ProductUiKeyboardModifiers modifiers{
        ProductUiKeyboardModifiers::None};
    std::string detail{};

    auto operator==(const ProductUiKeyboardBindingError&) const
        -> bool = default;
};

struct ProductUiKeyboardBindingSnapshot
{
    ProductUiKeyboardBindingPhase phase{
        ProductUiKeyboardBindingPhase::Cold};
    std::size_t registered_keys{};

    auto operator==(const ProductUiKeyboardBindingSnapshot&) const
        -> bool = default;
};

class ProductUiKeyboardBinding
{
public:
    ProductUiKeyboardBinding(
        std::shared_ptr<ProductUiInputQueue> queue,
        ProductUiKeyboardTextTranslator translator);
    ProductUiKeyboardBinding(
        const ProductUiKeyboardBinding&) = delete;
    auto operator=(const ProductUiKeyboardBinding&)
        -> ProductUiKeyboardBinding& = delete;
    ~ProductUiKeyboardBinding();

    [[nodiscard]] auto start(
        ProductUiKeyboardRegistrationPort& registrar)
        -> std::expected<
            ProductUiKeyboardBindingSnapshot,
            ProductUiKeyboardBindingError>;
    auto stop() noexcept -> void;
    [[nodiscard]] auto snapshot() const
        -> ProductUiKeyboardBindingSnapshot;

private:
    std::shared_ptr<ProductUiInputQueue> queue_{};
    std::shared_ptr<const ProductUiKeyboardTextTranslator>
        translator_{};
    std::shared_ptr<std::atomic_bool> active_{};
    mutable std::mutex mutex_{};
    ProductUiKeyboardBindingSnapshot snapshot_{};
};
} // namespace meccha::product_ui
