#include <meccha/application/application_command_queue.hpp>

#include <cstddef>
#include <iostream>
#include <string_view>
#include <thread>
#include <variant>
#include <vector>

namespace
{
using namespace meccha::application;

auto expect(bool condition, std::string_view message) -> bool
{
    if (!condition)
    {
        std::cerr << "FAIL application_command_queue: "
                  << message << '\n';
    }
    return condition;
}
} // namespace

auto main() -> int
{
    using namespace meccha::application;

    auto passed = true;
    auto queue = ApplicationCommandQueue{2U};

    passed &= expect(
        queue.enqueue(ToggleUi{0U}) ==
            CommandEnqueueResult::InvalidCommand,
        "command id zero was accepted");
    passed &= expect(
        queue.enqueue(ToggleUi{1U}) ==
                CommandEnqueueResult::Accepted &&
            queue.enqueue(CancelPaint{2U}) ==
                CommandEnqueueResult::Accepted &&
            queue.enqueue(ToggleEsp{3U}) ==
                CommandEnqueueResult::Full &&
            queue.snapshot() ==
                CommandQueueSnapshot{2U, 2U, true},
        "the command queue did not apply its hard capacity");

    const auto first = queue.drain(1U);
    passed &= expect(
        first.size() == 1U &&
            std::get<ToggleUi>(first.front()).id == 1U &&
            queue.snapshot().queued == 1U,
        "bounded drain did not preserve FIFO order");
    const auto second = queue.drain(8U);
    passed &= expect(
        second.size() == 1U &&
            std::get<CancelPaint>(second.front()).id == 2U &&
            queue.snapshot().queued == 0U,
        "drain exceeded or lost the remaining command");

    auto concurrent = ApplicationCommandQueue{128U};
    auto producers = std::vector<std::thread>{};
    for (auto producer = 0U; producer < 4U; ++producer)
    {
        producers.emplace_back([producer, &concurrent] {
            for (auto offset = 0U; offset < 16U; ++offset)
            {
                const auto id =
                    static_cast<CommandId>(
                        producer * 16U + offset + 1U);
                static_cast<void>(
                    concurrent.enqueue(ToggleEsp{id}));
            }
        });
    }
    for (auto& producer : producers)
    {
        producer.join();
    }
    const auto all = concurrent.drain(128U);
    passed &= expect(
        all.size() == 64U &&
            concurrent.snapshot().queued == 0U,
        "concurrent immutable command publication lost work");

    passed &= expect(
        concurrent.enqueue(ToggleUi{100U}) ==
            CommandEnqueueResult::Accepted,
        "discard fixture did not enqueue");
    concurrent.close();
    passed &= expect(
        concurrent.enqueue(ToggleUi{101U}) ==
                CommandEnqueueResult::Closed &&
            concurrent.discard() == 1U &&
            concurrent.discard() == 0U &&
            concurrent.snapshot() ==
                CommandQueueSnapshot{0U, 128U, false},
        "close/discard did not terminate admission exactly");

    if (passed)
    {
        std::cout << "PASS application_command_queue\n";
    }
    return passed ? 0 : 1;
}
