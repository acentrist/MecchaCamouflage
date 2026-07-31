#include <meccha/runtime/reflection_contract.hpp>
#include <meccha/runtime/paint_call_codec.hpp>
#include <meccha/runtime/unreal_contracts.hpp>

#include <cmath>
#include <cstdint>
#include <iostream>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
using namespace meccha::runtime;

auto expect(bool condition, std::string_view message) -> bool
{
    if (!condition)
    {
        std::cerr << "FAIL runtime_reflection_contract: "
                  << message << '\n';
    }
    return condition;
}

auto paint_contract() -> ReflectionRecordDescriptor
{
    return paint_at_uv_with_brush_contract();
}

auto changed_property(
    ReflectionRecordDescriptor record,
    std::size_t index,
    ReflectionPropertyDescriptor property)
    -> ReflectionRecordDescriptor
{
    record.properties[index] = std::move(property);
    return record;
}

auto has_error(
    const ReflectionRecordDescriptor& actual,
    const ReflectionRecordDescriptor& expected,
    ReflectionContractErrorCode code,
    std::string_view property = {}) -> bool
{
    const auto result =
        validate_reflection_contract(actual, expected);
    return !result && result.error().code == code &&
           result.error().property == property;
}
} // namespace

auto main() -> int
{
    using namespace meccha;
    using namespace meccha::runtime;

    auto passed = true;
    const auto expected = paint_contract();

    passed &= expect(
        validate_reflection_contract(expected, expected).has_value(),
        "the exact Paint contract was rejected");
    passed &= expect(
        expected.owner_name ==
                "/Script/PenguinHotel.RuntimePaintableComponent" &&
            expected.size == 0x68U &&
            vector2d_contract().size == 0x10U &&
            paint_channel_data_contract().size == 0x24U &&
            runtime_brush_settings_contract().size == 0x28U,
        "the reviewed game-owned Paint contract sizes drifted");

    const auto request = application::PaintAtUvWithBrush{
        1U,
        2U,
        application::RuntimeObjectHandle{3U, 4U},
        0.25,
        0.75,
        5.0,
        1024U,
        core::Rgb8{128U, 64U, 255U},
        core::Material{0.2, 0.8, 0.4},
        true,
    };
    const auto encoded = encode_paint_call(request);
    passed &= expect(
        encoded &&
            encoded->uv.x == 0.25 &&
            encoded->uv.y == 0.75 &&
            std::abs(
                encoded->brush_settings.radius -
                (5.0F / 1024.0F)) < 0.000001F &&
            encoded->brush_settings.hardness == 1.0F &&
            encoded->brush_settings.opacity == 1.0F &&
            encoded->brush_settings.spacing == 1.0F &&
            encoded->brush_settings.falloff ==
                RuntimeBrushFalloff::Spherical &&
            encoded->brush_settings.blend_mode ==
                RuntimePaintBlendMode::Normal &&
            encoded->brush_settings.brush_texture == nullptr &&
            encoded->channel_data.apply_mode ==
                PaintChannelApplyMode::Override &&
            encoded->channel ==
                RuntimePaintChannel::
                    AlbedoMetallicRoughnessEmissive &&
            std::abs(encoded->channel_data.albedo.red -
                     0.2158605F) < 0.00001F &&
            std::abs(encoded->channel_data.albedo.green -
                     0.0512695F) < 0.00001F &&
            encoded->channel_data.albedo.blue == 1.0F &&
            encoded->channel_data.albedo.alpha == 1.0F &&
            std::abs(encoded->channel_data.metallic - 0.2F) <
                0.000001F &&
            std::abs(encoded->channel_data.roughness - 0.8F) <
                0.000001F &&
            std::abs(encoded->channel_data.emissive - 0.4F) <
                0.000001F,
        "the typed Paint request did not encode to the reviewed game ABI");

    auto invalid_call = request;
    invalid_call.texture_dimension = 0U;
    passed &= expect(
        encode_paint_call(invalid_call) ==
            std::unexpected(PaintCallEncodingError::InvalidDimension),
        "the Paint codec accepted a missing texture dimension");

    auto wrong_name = expected;
    wrong_name.name = "PaintAtUvWithBrush";
    passed &= expect(
        has_error(
            wrong_name,
            expected,
            ReflectionContractErrorCode::RecordName),
        "a function-name near-match was accepted");

    auto wrong_owner = expected;
    wrong_owner.owner_name = "OtherPaintableComponent";
    passed &= expect(
        has_error(
            wrong_owner,
            expected,
            ReflectionContractErrorCode::OwnerName),
        "a wrong function owner was accepted");

    auto wrong_record_size = expected;
    wrong_record_size.size = 0x61U;
    passed &= expect(
        has_error(
            wrong_record_size,
            expected,
            ReflectionContractErrorCode::RecordSize),
        "a wrong parameter buffer size was accepted");

    auto missing = expected;
    missing.properties.erase(missing.properties.begin() + 1);
    passed &= expect(
        has_error(
            missing,
            expected,
            ReflectionContractErrorCode::MissingProperty,
            "ChannelData"),
        "a missing parameter was accepted");

    auto unexpected = expected;
    unexpected.properties.push_back(
        ReflectionPropertyDescriptor{
            "ResearchOnly",
            ReflectionPropertyKind::Bool,
            {},
            0x61U,
            0x01U,
            1U,
            ReflectionPropertyDirection::Input,
        });
    passed &= expect(
        has_error(
            unexpected,
            expected,
            ReflectionContractErrorCode::UnexpectedProperty,
            "ResearchOnly"),
        "an extra parameter was accepted");

    auto duplicate = expected;
    duplicate.properties[1].name = "Uv";
    passed &= expect(
        has_error(
            duplicate,
            expected,
            ReflectionContractErrorCode::DuplicateProperty,
            "Uv"),
        "duplicate reflected parameter names were accepted");

    auto property = expected.properties[0];
    property.kind = ReflectionPropertyKind::Object;
    passed &= expect(
        has_error(
            changed_property(expected, 0U, property),
            expected,
            ReflectionContractErrorCode::PropertyKind,
            "Uv"),
        "a wrong parameter property kind was accepted");

    property = expected.properties[0];
    property.type_name = "Vector2f";
    passed &= expect(
        has_error(
            changed_property(expected, 0U, property),
            expected,
            ReflectionContractErrorCode::PropertyType,
            "Uv"),
        "a wrong struct type was accepted");

    property = expected.properties[0];
    property.offset = 0x08U;
    passed &= expect(
        has_error(
            changed_property(expected, 0U, property),
            expected,
            ReflectionContractErrorCode::PropertyOffset,
            "Uv"),
        "a wrong parameter offset was accepted");

    property = expected.properties[0];
    property.size = 0x08U;
    passed &= expect(
        has_error(
            changed_property(expected, 0U, property),
            expected,
            ReflectionContractErrorCode::PropertySize,
            "Uv"),
        "a wrong parameter size was accepted");

    property = expected.properties[0];
    property.array_dimension = 2U;
    passed &= expect(
        has_error(
            changed_property(expected, 0U, property),
            expected,
            ReflectionContractErrorCode::PropertyArrayDimension,
            "Uv"),
        "a fixed-array parameter was accepted");

    property = expected.properties[0];
    property.direction = ReflectionPropertyDirection::Output;
    passed &= expect(
        has_error(
            changed_property(expected, 0U, property),
            expected,
            ReflectionContractErrorCode::PropertyDirection,
            "Uv"),
        "an output parameter was accepted as an input");

    if (passed)
    {
        std::cout << "PASS runtime_reflection_contract\n";
        return 0;
    }
    return 1;
}
