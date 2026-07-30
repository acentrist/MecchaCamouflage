#include <meccha/launcher/single_instance.hpp>

#include <iostream>
#include <string_view>

namespace
{
auto expect(bool condition, std::string_view message) -> bool
{
    if (!condition)
    {
        std::cerr << "FAIL single_instance: " << message << '\n';
    }
    return condition;
}
} // namespace

auto main() -> int
{
    using namespace meccha::launcher;

    auto passed = true;
    {
        auto first = acquire_launcher_instance();
        auto second = acquire_launcher_instance();
        passed &= expect(
            first && !second &&
                second.error().code ==
                    SingleInstanceErrorCode::AlreadyRunning,
            "a concurrent launcher instance was not rejected");
    }
    auto after_release = acquire_launcher_instance();
    passed &= expect(
        static_cast<bool>(after_release),
        "the launcher mutex was not released with its guard");

    if (passed)
    {
        std::cout << "PASS single_instance\n";
        return 0;
    }
    return 1;
}
