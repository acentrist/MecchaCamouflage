#include "unreal_reflection_validation.hpp"

#include <Helpers/String.hpp>
#include <Unreal/CoreUObject/UObject/Class.hpp>
#include <Unreal/CoreUObject/UObject/FStrProperty.hpp>
#include <Unreal/CoreUObject/UObject/UnrealType.hpp>
#include <Unreal/Property/FEnumProperty.hpp>
#include <Unreal/UObject.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <utility>

namespace meccha::runtime
{
namespace
{
using namespace RC::Unreal;

constexpr auto FailureMessage = "error.operation.failed";

auto property_direction(FProperty* property)
    -> ReflectionPropertyDirection
{
    if (property->HasAnyPropertyFlags(CPF_ReturnParm))
    {
        return ReflectionPropertyDirection::ReturnValue;
    }
    if (property->HasAnyPropertyFlags(CPF_OutParm))
    {
        return ReflectionPropertyDirection::Output;
    }
    if (property->HasAnyPropertyFlags(CPF_Parm))
    {
        return ReflectionPropertyDirection::Input;
    }
    return ReflectionPropertyDirection::Member;
}

auto reflected_enum_name(FProperty* property) -> std::string
{
    if (auto* enum_property = CastField<FEnumProperty>(property);
        enum_property != nullptr)
    {
        auto* value = enum_property->GetEnum().Get();
        return value == nullptr
                   ? std::string{}
                   : RC::to_string(value->GetName());
    }
    if (auto* byte_property = CastField<FByteProperty>(property);
        byte_property != nullptr)
    {
        auto* value = byte_property->GetEnum().Get();
        return value == nullptr
                   ? std::string{}
                   : RC::to_string(value->GetName());
    }
    return {};
}

auto describe_property(FProperty* property)
    -> std::optional<ReflectionPropertyDescriptor>
{
    if (property == nullptr ||
        property->GetOffset_Internal() < 0 ||
        property->GetElementSize() <= 0 ||
        property->GetArrayDim() <= 0)
    {
        return std::nullopt;
    }

    auto kind = ReflectionPropertyKind{};
    auto type_name = std::string{};
    if (CastField<FBoolProperty>(property) != nullptr)
    {
        kind = ReflectionPropertyKind::Bool;
    }
    else if (CastField<FEnumProperty>(property) != nullptr)
    {
        kind = ReflectionPropertyKind::Enum;
        type_name = reflected_enum_name(property);
    }
    else if (auto* byte_property =
                 CastField<FByteProperty>(property);
             byte_property != nullptr)
    {
        type_name = reflected_enum_name(property);
        kind = type_name.empty()
                   ? ReflectionPropertyKind::Byte
                   : ReflectionPropertyKind::Enum;
    }
    else if (CastField<FIntProperty>(property) != nullptr)
    {
        kind = ReflectionPropertyKind::Int32;
    }
    else if (CastField<FFloatProperty>(property) != nullptr)
    {
        kind = ReflectionPropertyKind::Float32;
    }
    else if (CastField<FDoubleProperty>(property) != nullptr)
    {
        kind = ReflectionPropertyKind::Float64;
    }
    else if (auto* object_property =
                 CastField<FObjectPropertyBase>(property);
             object_property != nullptr)
    {
        kind = ReflectionPropertyKind::Object;
        auto* property_class =
            object_property->GetPropertyClass().Get();
        if (property_class == nullptr)
        {
            return std::nullopt;
        }
        type_name = RC::to_string(property_class->GetName());
    }
    else if (auto* struct_property =
                 CastField<FStructProperty>(property);
             struct_property != nullptr)
    {
        kind = ReflectionPropertyKind::Struct;
        auto* script_struct = struct_property->GetStruct().Get();
        if (script_struct == nullptr)
        {
            return std::nullopt;
        }
        type_name = RC::to_string(script_struct->GetName());
    }
    else if (CastField<FStrProperty>(property) != nullptr)
    {
        kind = ReflectionPropertyKind::String;
    }
    else if (CastField<FNameProperty>(property) != nullptr)
    {
        kind = ReflectionPropertyKind::Name;
    }
    else if (auto* array_property =
                 CastField<FArrayProperty>(property);
             array_property != nullptr)
    {
        auto* inner = array_property->GetInner();
        if (inner == nullptr || inner->GetArrayDim() != 1)
        {
            return std::nullopt;
        }
        if (auto* inner_byte_property =
                CastField<FByteProperty>(inner);
            inner_byte_property != nullptr &&
            reflected_enum_name(inner_byte_property).empty() &&
            inner->GetElementSize() == 1)
        {
            type_name = "Byte";
        }
        else if (auto* inner_struct_property =
                     CastField<FStructProperty>(inner);
                 inner_struct_property != nullptr)
        {
            auto* script_struct =
                inner_struct_property->GetStruct().Get();
            if (script_struct == nullptr ||
                inner->GetElementSize() !=
                    script_struct->GetPropertiesSize())
            {
                return std::nullopt;
            }
            type_name =
                RC::to_string(script_struct->GetName());
        }
        else
        {
            return std::nullopt;
        }
        kind = ReflectionPropertyKind::Array;
    }
    else
    {
        return std::nullopt;
    }

    return ReflectionPropertyDescriptor{
        RC::to_string(property->GetName()),
        kind,
        std::move(type_name),
        static_cast<std::uint32_t>(
            property->GetOffset_Internal()),
        static_cast<std::uint32_t>(
            property->GetElementSize()),
        static_cast<std::uint32_t>(
            property->GetArrayDim()),
        property_direction(property),
    };
}

auto describe_record(UStruct* record)
    -> std::optional<ReflectionRecordDescriptor>
{
    if (record == nullptr || record->GetPropertiesSize() < 0 ||
        record->GetOuterPrivate() == nullptr)
    {
        return std::nullopt;
    }
    auto descriptor = ReflectionRecordDescriptor{
        RC::to_string(record->GetName()),
        RC::to_string(record->GetOuterPrivate()->GetPathName()),
        static_cast<std::uint32_t>(record->GetPropertiesSize()),
        {},
    };
    for (auto* property = record->GetFirstProperty();
         property != nullptr;
         property = GetNextField(property))
    {
        auto described = describe_property(property);
        if (!described)
        {
            return std::nullopt;
        }
        descriptor.properties.push_back(std::move(*described));
    }
    return descriptor;
}

auto contract_failure_kind(
    ReflectionContractErrorCode code)
    -> application::ContractFailureKind
{
    using Error = ReflectionContractErrorCode;
    using Failure = application::ContractFailureKind;
    switch (code)
    {
    case Error::RecordName:
    case Error::OwnerName:
        return Failure::WrongClass;
    case Error::RecordSize:
    case Error::PropertyOffset:
    case Error::PropertySize:
    case Error::PropertyArrayDimension:
        return Failure::ParameterSizeMismatch;
    case Error::MissingProperty:
        return Failure::MissingProperty;
    case Error::UnexpectedProperty:
    case Error::DuplicateProperty:
    case Error::PropertyKind:
    case Error::PropertyType:
    case Error::PropertyDirection:
        return Failure::WrongPropertyKind;
    }
    return Failure::WrongPropertyKind;
}

auto runtime_failure(
    application::RuntimeContractId contract,
    application::ContractFailureKind kind)
    -> std::unexpected<application::RuntimeExecutionError>
{
    return std::unexpected(application::RuntimeExecutionError{
        application::RuntimeExecutionErrorCode::OperationFailure,
        application::CompatibilityFailure{
            contract,
            kind,
            FailureMessage,
        },
    });
}
} // namespace

auto validate_unreal_record(
    RC::Unreal::UStruct* record,
    const ReflectionRecordDescriptor& expected,
    application::RuntimeContractId contract)
    -> std::expected<
        void,
        application::RuntimeExecutionError>
{
    const auto actual = describe_record(record);
    if (!actual)
    {
        return runtime_failure(
            contract,
            application::ContractFailureKind::
                WrongPropertyKind);
    }
    const auto validated =
        validate_reflection_contract(*actual, expected);
    if (!validated)
    {
        return runtime_failure(
            contract,
            contract_failure_kind(validated.error().code));
    }
    return {};
}
} // namespace meccha::runtime
