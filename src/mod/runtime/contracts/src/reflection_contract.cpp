#include <meccha/runtime/reflection_contract.hpp>

#include <algorithm>
#include <expected>
#include <string>
#include <unordered_set>

namespace meccha::runtime
{
namespace
{
auto failure(
    ReflectionContractErrorCode code,
    std::string property = {})
    -> std::unexpected<ReflectionContractError>
{
    return std::unexpected(
        ReflectionContractError{code, std::move(property)});
}

auto find_property(
    const ReflectionRecordDescriptor& record,
    const std::string& name)
    -> const ReflectionPropertyDescriptor*
{
    const auto found = std::ranges::find(
        record.properties,
        name,
        &ReflectionPropertyDescriptor::name);
    return found == record.properties.end()
               ? nullptr
               : &*found;
}

auto duplicate_property(
    const ReflectionRecordDescriptor& record)
    -> const ReflectionPropertyDescriptor*
{
    auto names = std::unordered_set<std::string>{};
    for (const auto& property : record.properties)
    {
        if (!names.insert(property.name).second)
        {
            return &property;
        }
    }
    return nullptr;
}
} // namespace

auto validate_reflection_contract(
    const ReflectionRecordDescriptor& actual,
    const ReflectionRecordDescriptor& expected)
    -> std::expected<void, ReflectionContractError>
{
    if (actual.name != expected.name)
    {
        return failure(ReflectionContractErrorCode::RecordName);
    }
    if (actual.owner_name != expected.owner_name)
    {
        return failure(ReflectionContractErrorCode::OwnerName);
    }
    if (actual.size != expected.size)
    {
        return failure(ReflectionContractErrorCode::RecordSize);
    }

    if (const auto* duplicate = duplicate_property(actual);
        duplicate != nullptr)
    {
        return failure(
            ReflectionContractErrorCode::DuplicateProperty,
            duplicate->name);
    }
    if (const auto* duplicate = duplicate_property(expected);
        duplicate != nullptr)
    {
        return failure(
            ReflectionContractErrorCode::DuplicateProperty,
            duplicate->name);
    }

    for (const auto& contract : expected.properties)
    {
        const auto* property =
            find_property(actual, contract.name);
        if (property == nullptr)
        {
            return failure(
                ReflectionContractErrorCode::MissingProperty,
                contract.name);
        }
        if (property->kind != contract.kind)
        {
            return failure(
                ReflectionContractErrorCode::PropertyKind,
                contract.name);
        }
        if (property->type_name != contract.type_name)
        {
            return failure(
                ReflectionContractErrorCode::PropertyType,
                contract.name);
        }
        if (property->offset != contract.offset)
        {
            return failure(
                ReflectionContractErrorCode::PropertyOffset,
                contract.name);
        }
        if (property->size != contract.size)
        {
            return failure(
                ReflectionContractErrorCode::PropertySize,
                contract.name);
        }
        if (property->array_dimension !=
            contract.array_dimension)
        {
            return failure(
                ReflectionContractErrorCode::PropertyArrayDimension,
                contract.name);
        }
        if (property->direction != contract.direction)
        {
            return failure(
                ReflectionContractErrorCode::PropertyDirection,
                contract.name);
        }
    }

    for (const auto& property : actual.properties)
    {
        if (find_property(expected, property.name) == nullptr)
        {
            return failure(
                ReflectionContractErrorCode::UnexpectedProperty,
                property.name);
        }
    }
    return {};
}
} // namespace meccha::runtime
