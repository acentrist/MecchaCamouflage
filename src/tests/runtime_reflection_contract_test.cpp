#include <meccha/runtime/reflection_contract.hpp>
#include <meccha/runtime/paint_call_codec.hpp>
#include <meccha/runtime/paint_queue_codec.hpp>
#include <meccha/runtime/unreal_contracts.hpp>

#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
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
    passed &= expect(
        recorded_stroke_count_contract().owner_name ==
                "/Script/PenguinHotel.RuntimePaintableComponent" &&
            recorded_stroke_count_contract().size == 0x04U &&
            queued_stroke_count_contract().owner_name ==
                "/Script/PenguinHotel.RuntimePaintReplicationManager" &&
            queued_stroke_count_contract().size == 0x04U &&
            queued_stroke_count_for_component_contract().size ==
                0x10U &&
            runtime_paint_replication_pressure_contract().size ==
                0x10U &&
            replication_pressure_contract().size == 0x10U,
        "the reviewed queue-observation contract sizes drifted");
    passed &= expect(
        validate_reflection_contract(
            recorded_stroke_count_contract(),
            recorded_stroke_count_contract()).has_value() &&
            validate_reflection_contract(
                queued_stroke_count_contract(),
                queued_stroke_count_contract()).has_value() &&
            validate_reflection_contract(
                queued_stroke_count_for_component_contract(),
                queued_stroke_count_for_component_contract())
                .has_value() &&
            validate_reflection_contract(
                runtime_paint_replication_pressure_contract(),
                runtime_paint_replication_pressure_contract())
                .has_value() &&
            validate_reflection_contract(
                replication_pressure_contract(),
                replication_pressure_contract()).has_value(),
        "an exact queue-observation contract was rejected");

    auto queue_tracker = PaintQueueObservationTracker{};
    const auto component = application::RuntimeObjectHandle{8U, 3U};
    const auto first_queue_sample = queue_tracker.observe(
        component,
        application::JobGeneration{5U},
        PaintQueueCounters{
            0,
            0,
            0,
            RuntimePaintReplicationPressureAbi{0, 0, 48, 0.0F},
        });
    passed &= expect(
        first_queue_sample &&
            *first_queue_sample ==
                application::PaintQueueObservation{
                    true,
                    false,
                    0U,
                    true,
                    0U,
                },
        "an idle exact queue sample was not preserved");
    const auto active_queue_sample = queue_tracker.observe(
        component,
        application::JobGeneration{5U},
        PaintQueueCounters{
            2,
            3,
            3,
            RuntimePaintReplicationPressureAbi{1, 3, 48, 1.0F},
        });
    passed &= expect(
        active_queue_sample &&
            *active_queue_sample ==
                application::PaintQueueObservation{
                    true,
                    true,
                    2U,
                    true,
                    3U,
                },
        "owned visual and outgoing queue counters were not mapped");
    const auto drained_queue_sample = queue_tracker.observe(
        component,
        application::JobGeneration{5U},
        PaintQueueCounters{
            0,
            0,
            0,
            RuntimePaintReplicationPressureAbi{0, 0, 48, 0.0F},
        });
    passed &= expect(
        drained_queue_sample &&
            drained_queue_sample->visual_observed_activity &&
            drained_queue_sample->visual_pending == 0U &&
            drained_queue_sample->outgoing_pending == 0U,
        "visual activity was not sticky through queue drain");
    const auto next_generation_sample = queue_tracker.observe(
        component,
        application::JobGeneration{6U},
        PaintQueueCounters{
            0,
            0,
            0,
            RuntimePaintReplicationPressureAbi{0, 0, 48, 0.0F},
        });
    passed &= expect(
        next_generation_sample &&
            !next_generation_sample->visual_observed_activity,
        "queue activity leaked into a new job generation");
    passed &= expect(
        queue_tracker.observe(
            component,
            application::JobGeneration{6U},
            PaintQueueCounters{
                -1,
                0,
                0,
                RuntimePaintReplicationPressureAbi{0, 0, 48, 0.0F},
            }) ==
            std::unexpected(PaintQueueCodecError::InvalidCounter),
        "a negative game-owned queue counter was accepted");
    passed &= expect(
        queue_tracker.observe(
            component,
            application::JobGeneration{6U},
            PaintQueueCounters{
                0,
                0,
                0,
                RuntimePaintReplicationPressureAbi{
                    0,
                    0,
                    48,
                    std::numeric_limits<float>::infinity(),
                },
            }) ==
            std::unexpected(PaintQueueCodecError::InvalidPressure),
        "a non-finite replication-pressure result was accepted");
    passed &= expect(
        queue_tracker.observe(
            component,
            application::JobGeneration{6U},
            PaintQueueCounters{
                0,
                std::nullopt,
                0,
                RuntimePaintReplicationPressureAbi{
                    0,
                    0,
                    48,
                    0.0F,
                },
            }) ==
            std::unexpected(
                PaintQueueCodecError::MissingOwnedObserver),
        "a queue sample without the component-owned observer was accepted");

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
