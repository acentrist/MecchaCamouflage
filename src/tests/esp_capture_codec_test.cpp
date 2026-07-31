#include <meccha/runtime/esp_capture_codec.hpp>
#include <meccha/runtime/reflection_contract.hpp>
#include <meccha/runtime/unreal_contracts.hpp>

#include <cmath>
#include <iostream>
#include <limits>
#include <string_view>

namespace
{
using namespace meccha::core;
using namespace meccha::runtime;

auto expect(bool condition, std::string_view message) -> bool
{
    if (!condition)
    {
        std::cerr << "FAIL esp_capture_codec: " << message << '\n';
    }
    return condition;
}

auto near(double left, double right) -> bool
{
    return std::abs(left - right) < 0.000001;
}
} // namespace

auto main() -> int
{
    auto passed = true;

    const auto view = decode_esp_view(
        EspVector3dAbi{10.0, 20.0, 30.0},
        EspRotatorAbi{5.0, 15.0, 1.0},
        90.0F,
        1920,
        1080);
    passed &= expect(
        view &&
            view->location == EspWorldPoint{10.0, 20.0, 30.0} &&
            near(view->pitch_degrees, 5.0) &&
            near(view->yaw_degrees, 15.0) &&
            near(view->roll_degrees, 1.0) &&
            near(view->field_of_view_degrees, 90.0) &&
            near(view->aspect_ratio, 16.0 / 9.0) &&
            view->aspect_constraint ==
                EspAspectConstraint::MaintainXFov &&
            near(view->projection_scale_x, 1.0) &&
            near(view->projection_scale_y, 1.0),
        "a valid reflected camera did not become an exact ESP view");
    passed &= expect(
        decode_esp_view(
            {},
            {},
            90.0F,
            0,
            1080) ==
                std::unexpected(
                    EspCaptureCodecError::InvalidViewport) &&
            decode_esp_view(
                {},
                {},
                0.0F,
                1920,
                1080) ==
                std::unexpected(
                    EspCaptureCodecError::InvalidFieldOfView) &&
            decode_esp_view(
                {
                    std::numeric_limits<double>::quiet_NaN(),
                    0.0,
                    0.0,
                },
                {},
                90.0F,
                1920,
                1080) ==
                std::unexpected(
                    EspCaptureCodecError::InvalidCamera),
        "invalid reflected view values were accepted");

    const auto capsule = sample_esp_capsule(
        EspVector3dAbi{100.0, 200.0, 300.0},
        EspRotatorAbi{},
        40.0F,
        90.0F);
    passed &= expect(
        capsule && capsule->size() == 18U &&
            capsule->front() ==
                EspWorldPoint{100.0, 200.0, 390.0} &&
            (*capsule)[1] ==
                EspWorldPoint{100.0, 200.0, 210.0} &&
            (*capsule)[2] ==
                EspWorldPoint{140.0, 200.0, 350.0} &&
            (*capsule)[3] ==
                EspWorldPoint{140.0, 200.0, 250.0},
        "the bounded upright capsule samples drifted");

    const auto pitched = sample_esp_capsule(
        {},
        EspRotatorAbi{90.0, 0.0, 0.0},
        40.0F,
        90.0F);
    passed &= expect(
        pitched && near(pitched->front().x, -90.0) &&
            near(pitched->front().y, 0.0) &&
            near(pitched->front().z, 0.0) &&
            near((*pitched)[1].x, 90.0),
        "capsule samples ignored the reflected world rotation");
    passed &= expect(
        sample_esp_capsule({}, {}, 0.0F, 90.0F) ==
                std::unexpected(
                    EspCaptureCodecError::InvalidCapsule) &&
            sample_esp_capsule({}, {}, 91.0F, 90.0F) ==
                std::unexpected(
                    EspCaptureCodecError::InvalidCapsule) &&
            sample_esp_capsule(
                {},
                {
                    0.0,
                    std::numeric_limits<double>::infinity(),
                    0.0,
                },
                40.0F,
                90.0F) ==
                std::unexpected(
                    EspCaptureCodecError::InvalidCapsule),
        "invalid reflected capsule values were accepted");
    passed &= expect(
        !should_refresh_esp_capture_directory(
            true,
            true,
            false,
            1'100U,
            1'000U,
            1'000U) &&
            should_refresh_esp_capture_directory(
                true,
                true,
                true,
                1'100U,
                1'000U,
                1'000U) &&
            should_refresh_esp_capture_directory(
                true,
                false,
                false,
                1'100U,
                1'000U,
                1'000U) &&
            !should_refresh_esp_capture_directory(
                false,
                false,
                true,
                1'100U,
                1'000U,
                1'000U),
        "the production avatar refresh policy can rescan every frame");

    const auto exact_contracts = {
        vector_contract(),
        rotator_contract(),
        get_camera_location_contract(),
        get_camera_rotation_contract(),
        get_fov_angle_contract(),
        k2_get_component_location_contract(),
        k2_get_component_rotation_contract(),
        get_scaled_capsule_radius_contract(),
        get_scaled_capsule_half_height_contract(),
    };
    for (const auto& contract : exact_contracts)
    {
        passed &= expect(
            validate_reflection_contract(contract, contract).has_value(),
            "an exact ESP reflection contract rejected itself");
    }
    passed &= expect(
        vector_contract().size == 0x18U &&
            rotator_contract().size == 0x18U &&
            get_camera_location_contract().owner_name ==
                "/Script/Engine.PlayerCameraManager" &&
            get_camera_rotation_contract().size == 0x18U &&
            get_fov_angle_contract().size == 0x04U &&
            k2_get_component_location_contract().owner_name ==
                "/Script/Engine.SceneComponent" &&
            k2_get_component_rotation_contract().size == 0x18U &&
            get_scaled_capsule_radius_contract().owner_name ==
                "/Script/Engine.CapsuleComponent" &&
            get_scaled_capsule_half_height_contract().size == 0x04U,
        "the reviewed ESP reflected ABI sizes or owners drifted");

    auto wrong_camera = get_camera_location_contract();
    wrong_camera.properties.front().type_name = "Vector3f";
    const auto wrong_result = validate_reflection_contract(
        wrong_camera,
        get_camera_location_contract());
    passed &= expect(
        !wrong_result &&
            wrong_result.error().code ==
                ReflectionContractErrorCode::PropertyType &&
            wrong_result.error().property == "ReturnValue",
        "a wrong ESP camera return type was accepted");

    if (passed)
    {
        std::cout << "PASS esp_capture_codec\n";
        return 0;
    }
    return 1;
}
