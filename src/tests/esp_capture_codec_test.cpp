#include <meccha/runtime/esp_capture_codec.hpp>
#include <meccha/runtime/reflection_contract.hpp>
#include <meccha/runtime/unreal_contracts.hpp>

#include <cmath>
#include <iostream>
#include <limits>
#include <optional>
#include <string_view>
#include <vector>

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

    const auto calibration_view = decode_esp_view(
        {},
        {},
        90.0F,
        1920,
        1080);
    const auto calibration_points =
        calibration_view
            ? esp_projection_calibration_points(*calibration_view)
            : std::unexpected(
                  EspCaptureCodecError::InvalidCamera);
    passed &= expect(
        calibration_points &&
            (*calibration_points)[0U] ==
                EspWorldPoint{1000.0, 100.0, 0.0} &&
            (*calibration_points)[1U] ==
                EspWorldPoint{1000.0, 0.0, 100.0},
        "projection calibration samples did not follow the camera axes");
    const auto calibrated =
        calibration_view
            ? calibrate_esp_view(
                  *calibration_view,
                  EspViewport{1920.0, 1080.0},
                  EspScreenPoint{1080.0, 540.0},
                  EspScreenPoint{960.0, 499.5})
            : std::unexpected(
                  EspCaptureCodecError::InvalidCamera);
    passed &= expect(
        calibrated &&
            near(calibrated->projection_scale_x, 1.25) &&
            near(calibrated->projection_scale_y, 0.75),
        "engine projection samples did not calibrate both axes");
    passed &= expect(
        calibration_view &&
            calibrate_esp_view(
                *calibration_view,
                EspViewport{1920.0, 1080.0},
                EspScreenPoint{
                    std::numeric_limits<double>::quiet_NaN(),
                    540.0},
                EspScreenPoint{960.0, 499.5}) ==
                std::unexpected(
                    EspCaptureCodecError::
                        InvalidProjectionSample),
        "a non-finite engine projection sample was accepted");
    passed &= expect(
        calibration_view &&
            calibrate_esp_view(
                *calibration_view,
                EspViewport{1920.0, 1080.0},
                EspScreenPoint{1080.0, 560.0},
                EspScreenPoint{960.0, 499.5}) ==
                std::unexpected(
                    EspCaptureCodecError::
                        InvalidProjectionSample),
        "an engine projection with a shifted principal point was "
        "silently approximated");

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

    const auto skeleton = build_esp_skeleton_pose(
        std::vector<PaintSamplingBone>{
            {"root", std::nullopt},
            {"spine", 0U},
            {"head", 1U},
        },
        std::vector<EspVector3dAbi>{
            {1.0, 2.0, 3.0},
            {4.0, 5.0, 6.0},
            {7.0, 8.0, 9.0},
        });
    passed &= expect(
        skeleton &&
            skeleton->bones ==
                std::vector<EspWorldPoint>{
                    {1.0, 2.0, 3.0},
                    {4.0, 5.0, 6.0},
                    {7.0, 8.0, 9.0},
                } &&
            skeleton->edges ==
                std::vector<EspSkeletonEdge>{
                    {0U, 1U},
                    {1U, 2U},
                },
        "validated profile bones did not become exact skeleton edges");
    const auto topology_bones =
        std::vector<PaintSamplingBone>{
            {"root", std::nullopt},
            {"spine", 0U},
            {"neck", 1U},
            {"head", 2U},
        };
    const auto reference_bones =
        std::vector<ImageReferenceBone>{
            {std::nullopt, {0.0, 0.0, 0.0}},
            {0U, {1.0, 0.0, 0.0}},
            {1U, {2.0, 0.0, 0.0}},
            {2U, {2.0, 1.0, 0.0}},
        };
    const auto scaled_pose = build_esp_skeleton_pose(
        topology_bones,
        std::vector<EspVector3dAbi>{
            {10.0, 0.0, 0.0},
            {12.0, 0.0, 0.0},
            {14.0, 0.0, 0.0},
            {14.0, 2.0, 0.0},
        });
    const auto collapsed_pose = build_esp_skeleton_pose(
        topology_bones,
        std::vector<EspVector3dAbi>(4U));
    passed &= expect(
        scaled_pose &&
            validate_esp_skeleton_topology(
                *scaled_pose,
                reference_bones) &&
            collapsed_pose &&
            !validate_esp_skeleton_topology(
                *collapsed_pose,
                reference_bones),
        "skeleton topology validation did not preserve uniform scale "
        "or reject collapsed sockets");
    passed &= expect(
        build_esp_skeleton_pose(
            std::vector<PaintSamplingBone>{
                {"root", std::nullopt},
                {"bad", 2U},
            },
            std::vector<EspVector3dAbi>{
                {},
                {},
            }) ==
            std::unexpected(
                EspCaptureCodecError::InvalidSkeleton),
        "an invalid profile skeleton hierarchy was accepted");

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
        project_world_location_to_screen_contract(),
        get_socket_location_contract(),
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
            get_scaled_capsule_half_height_contract().size == 0x04U &&
            project_world_location_to_screen_contract().size ==
                0x30U &&
            project_world_location_to_screen_contract()
                    .properties[1U]
                    .direction ==
                ReflectionPropertyDirection::Output &&
            get_socket_location_contract().size == 0x28U &&
            get_socket_location_contract()
                    .properties.front()
                    .kind ==
                ReflectionPropertyKind::Name,
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
