#pragma once

#include <meccha/application/runtime_lifecycle.hpp>
#include <meccha/application/runtime_operation_executor.hpp>

#include <memory>

namespace meccha::runtime
{
class UnrealRuntimeAdapter final
    : public application::RuntimeCallbackPort,
      public application::GameThreadContext
{
public:
    UnrealRuntimeAdapter();
    UnrealRuntimeAdapter(const UnrealRuntimeAdapter&) = delete;
    auto operator=(const UnrealRuntimeAdapter&)
        -> UnrealRuntimeAdapter& = delete;
    ~UnrealRuntimeAdapter() override;

    auto register_hud_callback(
        void* context,
        application::HudCallback callback)
        -> std::expected<
            application::CallbackId,
            application::CallbackPortError> override;

    auto unregister_hud_callback(application::CallbackId id)
        -> std::expected<
            void,
            application::CallbackPortError> override;

    [[nodiscard]] auto is_game_thread() const noexcept
        -> bool override;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace meccha::runtime
