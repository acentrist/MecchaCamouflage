#pragma once

#include <meccha/application/game_thread_scheduler.hpp>
#include <meccha/runtime/reflection_contract.hpp>

#include <expected>

namespace RC::Unreal
{
class UStruct;
}

namespace meccha::runtime
{
[[nodiscard]] auto validate_unreal_record(
    RC::Unreal::UStruct* record,
    const ReflectionRecordDescriptor& expected,
    application::RuntimeContractId contract)
    -> std::expected<
        void,
        application::RuntimeExecutionError>;
} // namespace meccha::runtime
