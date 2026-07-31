#pragma once

#include <cstdint>
#include <expected>
#include <string>
#include <vector>

namespace meccha::runtime
{
enum class ReflectionPropertyKind : std::uint8_t
{
    Bool,
    Byte,
    Int32,
    Float32,
    Float64,
    Object,
    Struct,
    String,
    Enum,
    Array,
};

enum class ReflectionPropertyDirection : std::uint8_t
{
    Member,
    Input,
    Output,
    ReturnValue,
};

struct ReflectionPropertyDescriptor
{
    std::string name{};
    ReflectionPropertyKind kind{};
    std::string type_name{};
    std::uint32_t offset{};
    std::uint32_t size{};
    std::uint32_t array_dimension{1U};
    ReflectionPropertyDirection direction{
        ReflectionPropertyDirection::Member};

    auto operator==(const ReflectionPropertyDescriptor&) const
        -> bool = default;
};

struct ReflectionRecordDescriptor
{
    std::string name{};
    std::string owner_name{};
    std::uint32_t size{};
    std::vector<ReflectionPropertyDescriptor> properties{};

    auto operator==(const ReflectionRecordDescriptor&) const
        -> bool = default;
};

enum class ReflectionContractErrorCode : std::uint8_t
{
    RecordName,
    OwnerName,
    RecordSize,
    MissingProperty,
    UnexpectedProperty,
    DuplicateProperty,
    PropertyKind,
    PropertyType,
    PropertyOffset,
    PropertySize,
    PropertyArrayDimension,
    PropertyDirection,
};

struct ReflectionContractError
{
    ReflectionContractErrorCode code{};
    std::string property{};

    auto operator==(const ReflectionContractError&) const
        -> bool = default;
};

[[nodiscard]] auto validate_reflection_contract(
    const ReflectionRecordDescriptor& actual,
    const ReflectionRecordDescriptor& expected)
    -> std::expected<void, ReflectionContractError>;
} // namespace meccha::runtime
