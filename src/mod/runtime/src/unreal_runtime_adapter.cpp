#include <meccha/runtime/unreal_runtime_adapter.hpp>

#include <meccha/application/paint_appearance_worker.hpp>
#include <meccha/runtime/canvas_call_codec.hpp>
#include <meccha/runtime/esp_capture_codec.hpp>
#include <meccha/runtime/input_control_codec.hpp>
#include <meccha/runtime/paint_call_codec.hpp>
#include <meccha/runtime/paint_capture_codec.hpp>
#include <meccha/runtime/paint_preview_codec.hpp>
#include <meccha/runtime/paint_queue_codec.hpp>
#include <meccha/runtime/texture_import_codec.hpp>
#include <meccha/runtime/unreal_contracts.hpp>
#include <meccha/core/png_encoder.hpp>
#include <meccha/product_ui/product_ui_pointer_capture.hpp>
#include <meccha/ui/esp_canvas_frame.hpp>

#include "unreal_reflection_validation.hpp"

#include <Unreal/CoreUObject/UObject/Class.hpp>
#include <Unreal/CoreUObject/UObject/FStrProperty.hpp>
#include <Unreal/CoreUObject/UObject/UnrealType.hpp>
#include <Unreal/AActor.hpp>
#include <Unreal/FMemory.hpp>
#include <Unreal/FSoftObjectPath.hpp>
#include <Unreal/FWeakObjectPtr.hpp>
#include <Unreal/Property/FEnumProperty.hpp>
#include <Unreal/Rotator.hpp>
#include <Unreal/UFunctionStructs.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/UnrealCoreStructs.hpp>
#include <Unreal/UnrealFlags.hpp>
#include <Unreal/UnrealInitializer.hpp>
#include <Unreal/World.hpp>
#include <Helpers/String.hpp>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <expected>
#include <limits>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace meccha::runtime
{
namespace
{
using namespace RC::Unreal;

constexpr auto HudCallbackId = application::CallbackId{1U};
constexpr auto ReceiveDrawHudPath =
    STR("/Script/Engine.HUD:ReceiveDrawHUD");
constexpr auto HudClassPath = STR("/Script/Engine.HUD");
constexpr auto WorldClassPath = STR("/Script/Engine.World");
constexpr auto PlayerControllerClassPath =
    STR("/Script/Engine.PlayerController");
constexpr auto ActorClassPath = STR("/Script/Engine.Actor");
constexpr auto ControllerClassPath =
    STR("/Script/Engine.Controller");
constexpr auto PawnClassPath = STR("/Script/Engine.Pawn");
constexpr auto CharacterClassPath = STR("/Script/Engine.Character");
constexpr auto SpectatorPawnClassPath =
    STR("/Script/Engine.SpectatorPawn");
constexpr auto GameStateBaseClassPath =
    STR("/Script/Engine.GameStateBase");
constexpr auto PlayerStateClassPath =
    STR("/Script/Engine.PlayerState");
constexpr auto SceneComponentClassPath =
    STR("/Script/Engine.SceneComponent");
constexpr auto PrimitiveComponentClassPath =
    STR("/Script/Engine.PrimitiveComponent");
constexpr auto SceneCaptureComponentClassPath =
    STR("/Script/Engine.SceneCaptureComponent");
constexpr auto SceneCaptureComponent2dClassPath =
    STR("/Script/Engine.SceneCaptureComponent2D");
constexpr auto SceneCapture2dClassPath =
    STR("/Script/Engine.SceneCapture2D");
constexpr auto StaticMeshComponentClassPath =
    STR("/Script/Engine.StaticMeshComponent");
constexpr auto NiagaraComponentClassPath =
    STR("/Script/Niagara.NiagaraComponent");
constexpr auto BrushPlaneClassPath =
    STR("/Game/BluePrints/cLeon/BP_BrushPlane."
        "BP_BrushPlane_C");
constexpr auto CapsuleComponentClassPath =
    STR("/Script/Engine.CapsuleComponent");
constexpr auto PlayerCameraManagerClassPath =
    STR("/Script/Engine.PlayerCameraManager");
constexpr auto CanvasClassPath = STR("/Script/Engine.Canvas");
constexpr auto FontClassPath = STR("/Script/Engine.Font");
constexpr auto GameFontAssetPath =
    STR("/Game/UI/NotoFonts/MainFont.MainFont");
constexpr auto K2DrawLinePath =
    STR("/Script/Engine.Canvas:K2_DrawLine");
constexpr auto K2DrawTexturePath =
    STR("/Script/Engine.Canvas:K2_DrawTexture");
constexpr auto K2DrawTextPath =
    STR("/Script/Engine.Canvas:K2_DrawText");
constexpr auto KismetRenderingLibraryClassPath =
    STR("/Script/Engine.KismetRenderingLibrary");
constexpr auto ImportBufferAsTexture2DPath =
    STR("/Script/Engine.KismetRenderingLibrary:"
        "ImportBufferAsTexture2D");
constexpr auto IsLookInputIgnoredPath =
    STR("/Script/Engine.Controller:IsLookInputIgnored");
constexpr auto IsMoveInputIgnoredPath =
    STR("/Script/Engine.Controller:IsMoveInputIgnored");
constexpr auto SetIgnoreLookInputPath =
    STR("/Script/Engine.Controller:SetIgnoreLookInput");
constexpr auto SetIgnoreMoveInputPath =
    STR("/Script/Engine.Controller:SetIgnoreMoveInput");
constexpr auto Texture2dClassPath =
    STR("/Script/Engine.Texture2D");
constexpr auto TextureRenderTarget2dClassPath =
    STR("/Script/Engine.TextureRenderTarget2D");
constexpr auto CreateRenderTarget2dPath =
    STR("/Script/Engine.KismetRenderingLibrary:"
        "CreateRenderTarget2D");
constexpr auto ReadRenderTargetRawPath =
    STR("/Script/Engine.KismetRenderingLibrary:"
        "ReadRenderTargetRaw");
constexpr auto CaptureScenePath =
    STR("/Script/Engine.SceneCaptureComponent2D:"
        "CaptureScene");
constexpr auto SetShowFlagSettingsPath =
    STR("/Script/Engine.SceneCaptureComponent:"
        "SetShowFlagSettings");
constexpr auto HideComponentPath =
    STR("/Script/Engine.SceneCaptureComponent:"
        "HideComponent");
constexpr auto K2DestroyActorPath =
    STR("/Script/Engine.Actor:K2_DestroyActor");
constexpr auto RuntimePaintableClassPath =
    STR("/Script/PenguinHotel.RuntimePaintableComponent");
constexpr auto MeshComponentClassPath =
    STR("/Script/Engine.MeshComponent");
constexpr auto SkinnedMeshComponentClassPath =
    STR("/Script/Engine.SkinnedMeshComponent");
constexpr auto SkeletalMeshComponentClassPath =
    STR("/Script/Engine.SkeletalMeshComponent");
constexpr auto SkinnedAssetClassPath =
    STR("/Script/Engine.SkinnedAsset");
constexpr auto IsPaintInitializedPath =
    STR("/Script/PenguinHotel.RuntimePaintableComponent:"
        "IsInitialized");
constexpr auto InitializePaintPath =
    STR("/Script/PenguinHotel.RuntimePaintableComponent:"
        "InitializePaint");
constexpr auto GetInitializedPaintMeshPath =
    STR("/Script/PenguinHotel.RuntimePaintableComponent:"
        "GetInitializedPaintMesh");
constexpr auto GetSocketTransformPath =
    STR("/Script/Engine.SceneComponent:GetSocketTransform");
constexpr auto PaintAtUvWithBrushPath =
    STR("/Script/PenguinHotel.RuntimePaintableComponent:"
        "PaintAtUVWithBrush");
constexpr auto GetRecordedStrokeCountPath =
    STR("/Script/PenguinHotel.RuntimePaintableComponent:"
        "GetRecordedStrokeCount");
constexpr auto RuntimePaintReplicationManagerClassPath =
    STR("/Script/PenguinHotel.RuntimePaintReplicationManager");
constexpr auto GetQueuedStrokeCountPath =
    STR("/Script/PenguinHotel.RuntimePaintReplicationManager:"
        "GetQueuedStrokeCount");
constexpr auto GetQueuedStrokeCountForComponentPath =
    STR("/Script/PenguinHotel.RuntimePaintReplicationManager:"
        "GetQueuedStrokeCountForComponent");
constexpr auto GetReplicationPressurePath =
    STR("/Script/PenguinHotel.RuntimePaintReplicationManager:"
        "GetReplicationPressure");
constexpr auto ExportChannelToBytesPath =
    STR("/Script/PenguinHotel.RuntimePaintableComponent:"
        "ExportChannelToBytes");
constexpr auto ImportChannelFromBytesPath =
    STR("/Script/PenguinHotel.RuntimePaintableComponent:"
        "ImportChannelFromBytes");
constexpr auto RuntimePaintReplicationPressurePath =
    STR("/Script/PenguinHotel.RuntimePaintReplicationPressure");
constexpr auto Vector2dPath =
    STR("/Script/CoreUObject.Vector2D");
constexpr auto LinearColorPath =
    STR("/Script/CoreUObject.LinearColor");
constexpr auto VectorPath = STR("/Script/CoreUObject.Vector");
constexpr auto RotatorPath = STR("/Script/CoreUObject.Rotator");
constexpr auto GetCameraLocationPath =
    STR("/Script/Engine.PlayerCameraManager:GetCameraLocation");
constexpr auto GetCameraRotationPath =
    STR("/Script/Engine.PlayerCameraManager:GetCameraRotation");
constexpr auto GetFovAnglePath =
    STR("/Script/Engine.PlayerCameraManager:GetFOVAngle");
constexpr auto K2GetComponentLocationPath =
    STR("/Script/Engine.SceneComponent:K2_GetComponentLocation");
constexpr auto K2GetComponentRotationPath =
    STR("/Script/Engine.SceneComponent:K2_GetComponentRotation");
constexpr auto GetScaledCapsuleRadiusPath =
    STR("/Script/Engine.CapsuleComponent:GetScaledCapsuleRadius");
constexpr auto GetScaledCapsuleHalfHeightPath =
    STR("/Script/Engine.CapsuleComponent:"
        "GetScaledCapsuleHalfHeight");
constexpr auto ProjectWorldLocationToScreenPath =
    STR("/Script/Engine.PlayerController:"
        "ProjectWorldLocationToScreen");
constexpr auto GetSocketLocationPath =
    STR("/Script/Engine.SceneComponent:GetSocketLocation");
constexpr auto GameStateCLeonClassPath =
    STR("/Game/BluePrints/cLeon/BP_GameState_cLeon."
        "BP_GameState_cLeon_C");
constexpr auto PlayerStateOnlineClassPath =
    STR("/Game/FirstPerson/Blueprints/"
        "BP_FirstPersonPlayerState_Online."
        "BP_FirstPersonPlayerState_Online_C");
constexpr auto PlayerStateCLeonClassPath =
    STR("/Game/BluePrints/cLeon/"
        "BP_FirstPersonPlayerState_Online_cLeon."
        "BP_FirstPersonPlayerState_Online_cLeon_C");
constexpr auto HunterCharacterClassPath =
    STR("/Game/BluePrints/cLeon/"
        "BP_FirstPersonCharacter_cLeon_Character_Hunter."
        "BP_FirstPersonCharacter_cLeon_Character_Hunter_C");
constexpr auto SurvivorCharacterClassPath =
    STR("/Game/BluePrints/cLeon/"
        "BP_FirstPersonCharacter_cLeon_Character_Survivor."
        "BP_FirstPersonCharacter_cLeon_Character_Survivor_C");
constexpr auto SpectatePawnCLeonClassPath =
    STR("/Game/BluePrints/cLeon/BP_SpectatePawn_cLeon."
        "BP_SpectatePawn_cLeon_C");
constexpr auto PaintChannelDataPath =
    STR("/Script/PenguinHotel.PaintChannelData");
constexpr auto RuntimeBrushSettingsPath =
    STR("/Script/PenguinHotel.RuntimeBrushSettings");
constexpr auto ReceiveDrawHudParameterBytes = 8;
constexpr auto FailureMessage = "error.operation.failed";
constexpr auto MaximumOwnedCanvasTextures = std::size_t{1024U};
constexpr auto EspAvatarRefreshIntervalMs = std::uint64_t{1000U};
constexpr auto MaximumEspAvatarCandidates =
    core::MaximumEspTargets * 2U;

struct ReceiveDrawHudParametersAbi
{
    std::int32_t size_x{};
    std::int32_t size_y{};
};

static_assert(sizeof(ReceiveDrawHudParametersAbi) == 0x08U);

struct HudContracts
{
    UFunction* receive_draw_hud{};
    UClass* hud_class{};
    UClass* world_class{};
    UClass* player_controller_class{};
    UClass* canvas_class{};
    FObjectPropertyBase* player_owner{};
    FObjectPropertyBase* canvas{};
};

struct CanvasContracts
{
    UClass* canvas_class{};
    UFunction* draw_line{};
    UFunction* draw_texture{};
    UFunction* draw_text{};
    UScriptStruct* vector2d{};
    UScriptStruct* linear_color{};
    UClass* font_class{};
    FWeakObjectPtr game_font{};
    UClass* kismet_rendering_library_class{};
    UObject* kismet_rendering_library_cdo{};
    UClass* texture2d_class{};
    UFunction* import_buffer_as_texture2d{};
};

struct InputContracts
{
    UClass* controller_class{};
    UClass* player_controller_class{};
    FBoolProperty* show_mouse_cursor{};
    UFunction* is_look_input_ignored{};
    UFunction* is_move_input_ignored{};
    UFunction* set_ignore_look_input{};
    UFunction* set_ignore_move_input{};
};

struct PaintContracts
{
    UClass* player_controller_class{};
    UClass* pawn_class{};
    UClass* mesh_component_class{};
    UClass* runtime_paintable_class{};
    UClass* replication_manager_class{};
    FObjectPropertyBase* acknowledged_pawn{};
    UFunction* is_initialized{};
    UFunction* initialize_paint{};
    UFunction* get_initialized_paint_mesh{};
    UFunction* get_socket_transform{};
    UFunction* paint_at_uv_with_brush{};
    UFunction* get_recorded_stroke_count{};
    UFunction* get_queued_stroke_count{};
    UFunction* get_queued_stroke_count_for_component{};
    UFunction* get_replication_pressure{};
    UFunction* export_channel_to_bytes{};
    UFunction* import_channel_from_bytes{};
    UScriptStruct* vector2d{};
    UScriptStruct* paint_channel_data{};
    UScriptStruct* runtime_brush_settings{};
    UScriptStruct* runtime_paint_replication_pressure{};
};

struct ExactEnumProperty
{
    FProperty* property{};
    FNumericProperty* underlying{};
    UEnum* enumeration{};
};

struct PaintSceneCaptureContracts
{
    UClass* actor_class{};
    UClass* scene_component_class{};
    UClass* primitive_component_class{};
    UClass* scene_capture_component_class{};
    UClass* scene_capture_component_2d_class{};
    UClass* scene_capture_2d_class{};
    UClass* texture_render_target_2d_class{};
    UClass* kismet_rendering_library_class{};
    UObject* kismet_rendering_library_cdo{};
    FObjectPropertyBase* capture_component_2d{};
    ExactEnumProperty capture_source{};
    FBoolProperty* capture_every_frame{};
    FBoolProperty* capture_on_movement{};
    FBoolProperty* always_persist_rendering_state{};
    ExactEnumProperty projection_type{};
    FFloatProperty* field_of_view_angle{};
    FObjectPropertyBase* texture_target{};
    UFunction* create_render_target_2d{};
    UFunction* read_render_target_raw{};
    UFunction* capture_scene{};
    UFunction* set_show_flag_settings{};
    FArrayProperty* set_show_flag_settings_parameter{};
    FArrayProperty* show_flag_settings{};
    UScriptStruct* show_flag_setting_struct{};
    FStrProperty* show_flag_name{};
    FBoolProperty* show_flag_enabled{};
    UFunction* hide_component{};
    UFunction* destroy_actor{};
};

struct PaintBrushPlaneVisuals
{
    UClass* actor_class{};
    std::array<UClass*, 3U> component_classes{};
    AActor* actor{};
    std::array<UObject*, 3U> components{};
};

struct ImagePaintContracts
{
    UClass* runtime_paintable_class{};
    UClass* pawn_class{};
    UClass* world_class{};
    UClass* mesh_component_class{};
    UClass* skinned_mesh_component_class{};
    UClass* skinned_asset_class{};
    FWeakObjectProperty* target_mesh_component{};
    FObjectProperty* skinned_asset{};
};

struct EspContracts
{
    UClass* world_class{};
    UClass* controller_class{};
    UClass* player_controller_class{};
    UClass* game_state_base_class{};
    UClass* player_state_class{};
    UClass* pawn_class{};
    UClass* character_class{};
    UClass* spectator_pawn_class{};
    UClass* scene_component_class{};
    UClass* capsule_component_class{};
    UClass* skinned_mesh_component_class{};
    UClass* skeletal_mesh_component_class{};
    UClass* skinned_asset_class{};
    UClass* player_camera_manager_class{};
    UClass* game_state_cleon_class{};
    UClass* player_state_online_class{};
    UClass* player_state_cleon_class{};
    UClass* hunter_character_class{};
    UClass* survivor_character_class{};
    UClass* spectate_pawn_cleon_class{};
    UScriptStruct* vector{};
    UScriptStruct* vector2d{};
    UScriptStruct* rotator{};
    UFunction* get_camera_location{};
    UFunction* get_camera_rotation{};
    UFunction* get_fov_angle{};
    UFunction* k2_get_component_location{};
    UFunction* k2_get_component_rotation{};
    UFunction* get_scaled_capsule_radius{};
    UFunction* get_scaled_capsule_half_height{};
    UFunction* project_world_location_to_screen{};
    UFunction* get_socket_location{};
    FObjectPropertyBase* world_game_state{};
    FObjectPropertyBase* controller_player_state{};
    FObjectPropertyBase* player_state_pawn{};
    FObjectPropertyBase* pawn_player_state{};
    FObjectPropertyBase* character_capsule{};
    FObjectPropertyBase* character_mesh{};
    FObjectPropertyBase* skinned_asset{};
    FObjectPropertyBase* player_camera_manager{};
    FArrayProperty* player_array{};
    FArrayProperty* live_survivor_player_states{};
    FArrayProperty* hunter_player_states{};
    FBoolProperty* is_spectator{};
    FBoolProperty* only_spectator{};
    FStrProperty* custom_player_name{};
};

struct EspAvatarBinding
{
    FWeakObjectPtr player_state{};
    FWeakObjectPtr avatar{};
    core::EspRole role{core::EspRole::Unknown};
    bool avatar_resolved{};
};

struct EspAvatarDirectory
{
    std::uint64_t world_identity{};
    std::uint64_t hud_identity{};
    std::uint64_t refresh_ms{};
    std::vector<EspAvatarBinding> bindings{};
};

struct ActiveFrame
{
    application::HudFrameIdentity identity{};
    UObject* world{};
    UObject* controller{};
    UObject* hud{};
    UObject* canvas{};
    std::int32_t viewport_width{};
    std::int32_t viewport_height{};
};

struct OwnedCanvasTextCall
{
    std::vector<char16_t> storage{};
    K2DrawTextParametersAbi parameters{};
};

struct OwnedCanvasTexture
{
    FWeakObjectPtr object{};
    std::uint64_t object_identity{};
};

struct InputMutationLease
{
    FWeakObjectPtr controller{};
    ui::RuntimeInputState captured{};
    bool cursor_changed{};
    bool look_changed{};
    bool movement_changed{};
};

struct InputTarget
{
    InputContracts contracts{};
    UObject* controller{};
    std::uint64_t identity{};
};

class RootedObjectGuard final
{
public:
    explicit RootedObjectGuard(UObject* object)
        : object_{object}
    {
    }
    RootedObjectGuard(const RootedObjectGuard&) = delete;
    auto operator=(const RootedObjectGuard&)
        -> RootedObjectGuard& = delete;
    ~RootedObjectGuard() noexcept
    {
        if (object_ != nullptr)
        {
            try
            {
                if (object_->IsRootSet())
                {
                    object_->ClearRootSet();
                }
            }
            catch (...)
            {
            }
        }
    }

    auto dismiss() noexcept -> void
    {
        object_ = nullptr;
    }

private:
    UObject* object_{};
};

struct BoundFrame
{
    application::HudFrameIdentity identity{};
    FWeakObjectPtr world{};
    FWeakObjectPtr controller{};
    FWeakObjectPtr hud{};
    FWeakObjectPtr canvas{};
    FWeakObjectPtr pawn{};
    FWeakObjectPtr component{};
    FWeakObjectPtr replication_manager{};
    std::uint64_t component_identity{};
    std::uint64_t component_generation{};
};

auto find_class(const TCHAR* path) -> UClass*
{
    return UObjectGlobals::StaticFindObject<UClass*>(
        nullptr,
        nullptr,
        path);
}

auto validate_parameter(
    FProperty* property,
    const TCHAR* expected_name,
    int expected_offset) -> bool
{
    return property != nullptr &&
           property->GetName() == expected_name &&
           property->HasAllPropertyFlags(CPF_Parm) &&
           !property->HasAnyPropertyFlags(CPF_ReturnParm) &&
           CastField<FIntProperty>(property) != nullptr &&
           property->GetArrayDim() == 1 &&
           property->GetElementSize() ==
               static_cast<int>(sizeof(std::int32_t)) &&
           property->GetOffset_Internal() == expected_offset &&
           property->IsInContainer(ReceiveDrawHudParameterBytes);
}

auto find_object_property(
    UClass* owner,
    const TCHAR* name,
    UClass* expected_class) -> FObjectPropertyBase*
{
    if (owner == nullptr || expected_class == nullptr)
    {
        return nullptr;
    }
    auto* property =
        CastField<FObjectPropertyBase>(
            owner->FindProperty(FName{name, FNAME_Find}));
    if (property == nullptr ||
        property->GetArrayDim() != 1 ||
        property->GetElementSize() !=
            static_cast<int>(sizeof(void*)) ||
        property->GetPropertyClass().Get() != expected_class ||
        !property->IsInContainer(owner))
    {
        return nullptr;
    }
    return property;
}

auto find_bool_property(UClass* owner, const TCHAR* name)
    -> FBoolProperty*
{
    if (owner == nullptr)
    {
        return nullptr;
    }
    auto* property =
        CastField<FBoolProperty>(
            owner->FindProperty(FName{name, FNAME_Find}));
    if (property == nullptr ||
        property->GetOwner<UClass>() != owner ||
        property->GetArrayDim() != 1 ||
        property->GetElementSize() != 1 ||
        property->GetFieldMask() == 0U ||
        property->GetByteMask() == 0U ||
        !property->IsInContainer(owner))
    {
        return nullptr;
    }
    return property;
}

auto find_exact_float_property(
    UClass* lookup_class,
    UClass* declared_owner,
    const TCHAR* name) -> FFloatProperty*
{
    if (lookup_class == nullptr || declared_owner == nullptr)
    {
        return nullptr;
    }
    auto* property = CastField<FFloatProperty>(
        lookup_class->FindProperty(FName{name, FNAME_Find}));
    if (property == nullptr ||
        property->GetOwner<UClass>() != declared_owner ||
        property->GetArrayDim() != 1 ||
        property->GetElementSize() !=
            static_cast<int>(sizeof(float)) ||
        !property->IsInContainer(lookup_class))
    {
        return nullptr;
    }
    return property;
}

auto find_exact_enum_property(
    UClass* lookup_class,
    UClass* declared_owner,
    const TCHAR* name,
    const TCHAR* expected_enum_name)
    -> ExactEnumProperty
{
    if (lookup_class == nullptr || declared_owner == nullptr)
    {
        return {};
    }
    auto* property =
        lookup_class->FindProperty(FName{name, FNAME_Find});
    auto* underlying = static_cast<FNumericProperty*>(nullptr);
    auto* enumeration = static_cast<UEnum*>(nullptr);
    if (auto* enum_property =
            CastField<FEnumProperty>(property);
        enum_property != nullptr)
    {
        underlying = enum_property->GetUnderlyingProperty();
        enumeration = enum_property->GetEnum().Get();
    }
    else if (auto* byte_property =
                 CastField<FByteProperty>(property);
             byte_property != nullptr)
    {
        underlying = byte_property;
        enumeration = byte_property->GetIntPropertyEnum();
    }
    if (property == nullptr || underlying == nullptr ||
        enumeration == nullptr ||
        property->GetOwner<UClass>() != declared_owner ||
        property->GetArrayDim() != 1 ||
        property->GetElementSize() != 1 ||
        !property->IsInContainer(lookup_class) ||
        !underlying->IsInteger() ||
        underlying->GetElementSize() != 1 ||
        enumeration->GetName() != expected_enum_name)
    {
        return {};
    }
    return ExactEnumProperty{
        property,
        underlying,
        enumeration,
    };
}

auto find_weak_object_property(
    UClass* owner,
    const TCHAR* name) -> FWeakObjectProperty*
{
    if (owner == nullptr)
    {
        return nullptr;
    }
    auto* property = CastField<FWeakObjectProperty>(
        owner->FindProperty(FName{name, FNAME_Find}));
    if (property == nullptr ||
        property->GetOwner<UClass>() != owner ||
        property->GetArrayDim() != 1 ||
        property->GetElementSize() !=
            static_cast<int>(sizeof(FWeakObjectPtr)) ||
        property->GetPropertyClass().Get() == nullptr ||
        !property->IsInContainer(owner))
    {
        return nullptr;
    }
    return property;
}

auto find_exact_object_property(
    UClass* lookup_class,
    UClass* declared_owner,
    const TCHAR* name,
    UClass* expected_class) -> FObjectPropertyBase*
{
    if (lookup_class == nullptr || declared_owner == nullptr ||
        expected_class == nullptr)
    {
        return nullptr;
    }
    auto* property = CastField<FObjectPropertyBase>(
        lookup_class->FindProperty(FName{name, FNAME_Find}));
    if (property == nullptr ||
        property->GetOwner<UClass>() != declared_owner ||
        property->GetArrayDim() != 1 ||
        property->GetElementSize() !=
            static_cast<int>(sizeof(void*)) ||
        property->GetPropertyClass().Get() != expected_class ||
        !property->IsInContainer(lookup_class))
    {
        return nullptr;
    }
    return property;
}

auto find_exact_object_array_property(
    UClass* lookup_class,
    UClass* declared_owner,
    const TCHAR* name,
    UClass* expected_element_class) -> FArrayProperty*
{
    if (lookup_class == nullptr || declared_owner == nullptr ||
        expected_element_class == nullptr)
    {
        return nullptr;
    }
    auto* property = CastField<FArrayProperty>(
        lookup_class->FindProperty(FName{name, FNAME_Find}));
    auto* inner =
        property == nullptr
            ? nullptr
            : CastField<FObjectPropertyBase>(
                  property->GetInner());
    if (property == nullptr || inner == nullptr ||
        property->GetOwner<UClass>() != declared_owner ||
        property->GetArrayDim() != 1 ||
        property->GetElementSize() !=
            static_cast<int>(sizeof(FScriptArray)) ||
        !property->IsInContainer(lookup_class) ||
        inner->GetArrayDim() != 1 ||
        inner->GetElementSize() !=
            static_cast<int>(sizeof(void*)) ||
        inner->GetPropertyClass().Get() !=
            expected_element_class)
    {
        return nullptr;
    }
    return property;
}

auto find_exact_string_property(
    UClass* lookup_class,
    UClass* declared_owner,
    const TCHAR* name) -> FStrProperty*
{
    if (lookup_class == nullptr || declared_owner == nullptr)
    {
        return nullptr;
    }
    auto* property = CastField<FStrProperty>(
        lookup_class->FindProperty(FName{name, FNAME_Find}));
    if (property == nullptr ||
        property->GetOwner<UClass>() != declared_owner ||
        property->GetArrayDim() != 1 ||
        property->GetElementSize() !=
            static_cast<int>(sizeof(FString)) ||
        !property->IsInContainer(lookup_class))
    {
        return nullptr;
    }
    return property;
}

auto resolve_hud_contracts() -> std::optional<HudContracts>
{
    auto contracts = HudContracts{};
    contracts.receive_draw_hud =
        UObjectGlobals::StaticFindObject<UFunction*>(
            nullptr,
            nullptr,
            ReceiveDrawHudPath);
    contracts.hud_class = find_class(HudClassPath);
    contracts.world_class = find_class(WorldClassPath);
    contracts.player_controller_class =
        find_class(PlayerControllerClassPath);
    contracts.canvas_class = find_class(CanvasClassPath);
    if (contracts.receive_draw_hud == nullptr ||
        contracts.hud_class == nullptr ||
        contracts.world_class == nullptr ||
        contracts.player_controller_class == nullptr ||
        contracts.canvas_class == nullptr ||
        contracts.receive_draw_hud->GetOuterPrivate() !=
            contracts.hud_class ||
        contracts.receive_draw_hud->GetPropertiesSize() !=
            ReceiveDrawHudParameterBytes)
    {
        return std::nullopt;
    }

    auto* parameter = contracts.receive_draw_hud->GetFirstProperty();
    if (!validate_parameter(parameter, STR("SizeX"), 0))
    {
        return std::nullopt;
    }
    parameter = GetNextField(parameter);
    if (!validate_parameter(parameter, STR("SizeY"), 4) ||
        GetNextField(parameter) != nullptr)
    {
        return std::nullopt;
    }

    contracts.player_owner = find_object_property(
        contracts.hud_class,
        STR("PlayerOwner"),
        contracts.player_controller_class);
    contracts.canvas = find_object_property(
        contracts.hud_class,
        STR("Canvas"),
        contracts.canvas_class);
    if (contracts.player_owner == nullptr ||
        contracts.canvas == nullptr)
    {
        return std::nullopt;
    }
    return contracts;
}

auto object_is_live(UObject* object, UClass* expected_class) -> bool
{
    if (object == nullptr || expected_class == nullptr ||
        !UObject::IsReal(object) || !object->IsA(expected_class))
    {
        return false;
    }
    const auto weak = FWeakObjectPtr{object};
    return weak.Get() == object &&
           weak.ObjectSerialNumber != 0;
}

auto object_is_live_exact(
    UObject* object,
    UClass* expected_class) -> bool
{
    constexpr auto rejected_flags = static_cast<EObjectFlags>(
        RF_ClassDefaultObject | RF_ArchetypeObject |
        RF_BeginDestroyed | RF_FinishDestroyed);
    return object_is_live(object, expected_class) &&
           object->GetClassPrivate() == expected_class &&
           !object->HasAnyFlags(rejected_flags);
}

auto object_identity(UObject* object) -> std::uint64_t
{
    const auto weak = FWeakObjectPtr{object};
    return
        (static_cast<std::uint64_t>(
             static_cast<std::uint32_t>(
                 weak.ObjectSerialNumber))
         << 32U) |
        static_cast<std::uint32_t>(weak.ObjectIndex);
}

auto read_object(
    FObjectPropertyBase* property,
    UObject* container) -> UObject*
{
    if (property == nullptr || container == nullptr)
    {
        return nullptr;
    }
    const auto* value =
        property->ContainerPtrToValuePtr<void>(container);
    return property->GetObjectPropertyValue(value);
}

auto read_weak_object(
    FWeakObjectProperty* property,
    UObject* container) -> UObject*
{
    if (property == nullptr || container == nullptr)
    {
        return nullptr;
    }
    return property->GetPropertyValueInContainer(container).Get();
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

auto resolve_esp_contracts()
    -> std::expected<
        EspContracts,
        application::RuntimeExecutionError>
{
    auto contracts = EspContracts{};
    contracts.world_class = find_class(WorldClassPath);
    contracts.controller_class = find_class(ControllerClassPath);
    contracts.player_controller_class =
        find_class(PlayerControllerClassPath);
    contracts.game_state_base_class =
        find_class(GameStateBaseClassPath);
    contracts.player_state_class =
        find_class(PlayerStateClassPath);
    contracts.pawn_class = find_class(PawnClassPath);
    contracts.character_class = find_class(CharacterClassPath);
    contracts.spectator_pawn_class =
        find_class(SpectatorPawnClassPath);
    contracts.scene_component_class =
        find_class(SceneComponentClassPath);
    contracts.capsule_component_class =
        find_class(CapsuleComponentClassPath);
    contracts.skinned_mesh_component_class =
        find_class(SkinnedMeshComponentClassPath);
    contracts.skeletal_mesh_component_class =
        find_class(SkeletalMeshComponentClassPath);
    contracts.skinned_asset_class =
        find_class(SkinnedAssetClassPath);
    contracts.player_camera_manager_class =
        find_class(PlayerCameraManagerClassPath);
    contracts.game_state_cleon_class =
        find_class(GameStateCLeonClassPath);
    contracts.player_state_online_class =
        find_class(PlayerStateOnlineClassPath);
    contracts.player_state_cleon_class =
        find_class(PlayerStateCLeonClassPath);
    contracts.hunter_character_class =
        find_class(HunterCharacterClassPath);
    contracts.survivor_character_class =
        find_class(SurvivorCharacterClassPath);
    contracts.spectate_pawn_cleon_class =
        find_class(SpectatePawnCLeonClassPath);
    contracts.vector =
        UObjectGlobals::StaticFindObject<UScriptStruct*>(
            nullptr,
            nullptr,
            VectorPath);
    contracts.vector2d =
        UObjectGlobals::StaticFindObject<UScriptStruct*>(
            nullptr,
            nullptr,
            Vector2dPath);
    contracts.rotator =
        UObjectGlobals::StaticFindObject<UScriptStruct*>(
            nullptr,
            nullptr,
            RotatorPath);
    contracts.get_camera_location =
        UObjectGlobals::StaticFindObject<UFunction*>(
            nullptr,
            nullptr,
            GetCameraLocationPath);
    contracts.get_camera_rotation =
        UObjectGlobals::StaticFindObject<UFunction*>(
            nullptr,
            nullptr,
            GetCameraRotationPath);
    contracts.get_fov_angle =
        UObjectGlobals::StaticFindObject<UFunction*>(
            nullptr,
            nullptr,
            GetFovAnglePath);
    contracts.k2_get_component_location =
        UObjectGlobals::StaticFindObject<UFunction*>(
            nullptr,
            nullptr,
            K2GetComponentLocationPath);
    contracts.k2_get_component_rotation =
        UObjectGlobals::StaticFindObject<UFunction*>(
            nullptr,
            nullptr,
            K2GetComponentRotationPath);
    contracts.get_scaled_capsule_radius =
        UObjectGlobals::StaticFindObject<UFunction*>(
            nullptr,
            nullptr,
            GetScaledCapsuleRadiusPath);
    contracts.get_scaled_capsule_half_height =
        UObjectGlobals::StaticFindObject<UFunction*>(
            nullptr,
            nullptr,
            GetScaledCapsuleHalfHeightPath);
    contracts.project_world_location_to_screen =
        UObjectGlobals::StaticFindObject<UFunction*>(
            nullptr,
            nullptr,
            ProjectWorldLocationToScreenPath);
    contracts.get_socket_location =
        UObjectGlobals::StaticFindObject<UFunction*>(
            nullptr,
            nullptr,
            GetSocketLocationPath);

    const auto required_objects = std::array{
        static_cast<UObject*>(contracts.world_class),
        static_cast<UObject*>(contracts.controller_class),
        static_cast<UObject*>(contracts.player_controller_class),
        static_cast<UObject*>(contracts.game_state_base_class),
        static_cast<UObject*>(contracts.player_state_class),
        static_cast<UObject*>(contracts.pawn_class),
        static_cast<UObject*>(contracts.character_class),
        static_cast<UObject*>(contracts.spectator_pawn_class),
        static_cast<UObject*>(contracts.scene_component_class),
        static_cast<UObject*>(contracts.capsule_component_class),
        static_cast<UObject*>(
            contracts.skinned_mesh_component_class),
        static_cast<UObject*>(
            contracts.skeletal_mesh_component_class),
        static_cast<UObject*>(contracts.skinned_asset_class),
        static_cast<UObject*>(contracts.player_camera_manager_class),
        static_cast<UObject*>(contracts.game_state_cleon_class),
        static_cast<UObject*>(contracts.player_state_online_class),
        static_cast<UObject*>(contracts.player_state_cleon_class),
        static_cast<UObject*>(contracts.hunter_character_class),
        static_cast<UObject*>(contracts.survivor_character_class),
        static_cast<UObject*>(contracts.spectate_pawn_cleon_class),
        static_cast<UObject*>(contracts.vector),
        static_cast<UObject*>(contracts.vector2d),
        static_cast<UObject*>(contracts.rotator),
    };
    if (std::ranges::any_of(
            required_objects,
            [](UObject* object) { return object == nullptr; }))
    {
        return runtime_failure(
            application::RuntimeContractId::EspFrame,
            application::ContractFailureKind::MissingObject);
    }
    if (!contracts.game_state_cleon_class->IsChildOf(
            contracts.game_state_base_class) ||
        !contracts.player_state_online_class->IsChildOf(
            contracts.player_state_class) ||
        !contracts.player_state_cleon_class->IsChildOf(
            contracts.player_state_online_class) ||
        !contracts.hunter_character_class->IsChildOf(
            contracts.character_class) ||
        !contracts.survivor_character_class->IsChildOf(
            contracts.character_class) ||
        !contracts.spectate_pawn_cleon_class->IsChildOf(
            contracts.spectator_pawn_class) ||
        !contracts.capsule_component_class->IsChildOf(
            contracts.scene_component_class) ||
        !contracts.skinned_mesh_component_class->IsChildOf(
            contracts.scene_component_class) ||
        !contracts.skeletal_mesh_component_class->IsChildOf(
            contracts.skinned_mesh_component_class))
    {
        return runtime_failure(
            application::RuntimeContractId::EspFrame,
            application::ContractFailureKind::WrongClass);
    }
    const auto required_functions = std::array{
        contracts.get_camera_location,
        contracts.get_camera_rotation,
        contracts.get_fov_angle,
        contracts.k2_get_component_location,
        contracts.k2_get_component_rotation,
        contracts.get_scaled_capsule_radius,
        contracts.get_scaled_capsule_half_height,
        contracts.project_world_location_to_screen,
        contracts.get_socket_location,
    };
    if (std::ranges::any_of(
            required_functions,
            [](UFunction* function)
            {
                return function == nullptr;
            }))
    {
        return runtime_failure(
            application::RuntimeContractId::EspFrame,
            application::ContractFailureKind::MissingFunction);
    }

    contracts.world_game_state = find_exact_object_property(
        contracts.world_class,
        contracts.world_class,
        STR("GameState"),
        contracts.game_state_base_class);
    contracts.controller_player_state =
        find_exact_object_property(
            contracts.player_controller_class,
            contracts.controller_class,
            STR("PlayerState"),
            contracts.player_state_class);
    contracts.player_state_pawn = find_exact_object_property(
        contracts.player_state_cleon_class,
        contracts.player_state_class,
        STR("PawnPrivate"),
        contracts.pawn_class);
    contracts.pawn_player_state = find_exact_object_property(
        contracts.pawn_class,
        contracts.pawn_class,
        STR("PlayerState"),
        contracts.player_state_class);
    contracts.character_capsule = find_exact_object_property(
        contracts.character_class,
        contracts.character_class,
        STR("CapsuleComponent"),
        contracts.capsule_component_class);
    contracts.character_mesh = find_exact_object_property(
        contracts.character_class,
        contracts.character_class,
        STR("Mesh"),
        contracts.skeletal_mesh_component_class);
    contracts.skinned_asset = find_exact_object_property(
        contracts.skeletal_mesh_component_class,
        contracts.skinned_mesh_component_class,
        STR("SkinnedAsset"),
        contracts.skinned_asset_class);
    contracts.player_camera_manager =
        find_exact_object_property(
            contracts.player_controller_class,
            contracts.player_controller_class,
            STR("PlayerCameraManager"),
            contracts.player_camera_manager_class);
    contracts.player_array = find_exact_object_array_property(
        contracts.game_state_cleon_class,
        contracts.game_state_base_class,
        STR("PlayerArray"),
        contracts.player_state_class);
    contracts.live_survivor_player_states =
        find_exact_object_array_property(
            contracts.game_state_cleon_class,
            contracts.game_state_cleon_class,
            STR("LiveSurvivors_PlayerState"),
            contracts.player_state_cleon_class);
    contracts.hunter_player_states =
        find_exact_object_array_property(
            contracts.game_state_cleon_class,
            contracts.game_state_cleon_class,
            STR("HuntersPlayerState"),
            contracts.player_state_cleon_class);
    contracts.is_spectator = find_bool_property(
        contracts.player_state_class,
        STR("bIsSpectator"));
    contracts.only_spectator = find_bool_property(
        contracts.player_state_class,
        STR("bOnlySpectator"));
    contracts.custom_player_name = find_exact_string_property(
        contracts.player_state_cleon_class,
        contracts.player_state_online_class,
        STR("CustomPlayerName"));
    const auto required_properties = std::array{
        static_cast<FProperty*>(contracts.world_game_state),
        static_cast<FProperty*>(contracts.controller_player_state),
        static_cast<FProperty*>(contracts.player_state_pawn),
        static_cast<FProperty*>(contracts.pawn_player_state),
        static_cast<FProperty*>(contracts.character_capsule),
        static_cast<FProperty*>(contracts.character_mesh),
        static_cast<FProperty*>(contracts.skinned_asset),
        static_cast<FProperty*>(contracts.player_camera_manager),
        static_cast<FProperty*>(contracts.player_array),
        static_cast<FProperty*>(
            contracts.live_survivor_player_states),
        static_cast<FProperty*>(contracts.hunter_player_states),
        static_cast<FProperty*>(contracts.is_spectator),
        static_cast<FProperty*>(contracts.only_spectator),
        static_cast<FProperty*>(contracts.custom_player_name),
    };
    if (std::ranges::any_of(
            required_properties,
            [](FProperty* property)
            {
                return property == nullptr;
            }))
    {
        return runtime_failure(
            application::RuntimeContractId::EspFrame,
            application::ContractFailureKind::MissingProperty);
    }

    const auto validations = std::array{
        std::pair{
            static_cast<UStruct*>(contracts.vector),
            vector_contract()},
        std::pair{
            static_cast<UStruct*>(contracts.vector2d),
            vector2d_contract()},
        std::pair{
            static_cast<UStruct*>(contracts.rotator),
            rotator_contract()},
        std::pair{
            static_cast<UStruct*>(contracts.get_camera_location),
            get_camera_location_contract()},
        std::pair{
            static_cast<UStruct*>(contracts.get_camera_rotation),
            get_camera_rotation_contract()},
        std::pair{
            static_cast<UStruct*>(contracts.get_fov_angle),
            get_fov_angle_contract()},
        std::pair{
            static_cast<UStruct*>(
                contracts.k2_get_component_location),
            k2_get_component_location_contract()},
        std::pair{
            static_cast<UStruct*>(
                contracts.k2_get_component_rotation),
            k2_get_component_rotation_contract()},
        std::pair{
            static_cast<UStruct*>(
                contracts.get_scaled_capsule_radius),
            get_scaled_capsule_radius_contract()},
        std::pair{
            static_cast<UStruct*>(
                contracts.get_scaled_capsule_half_height),
            get_scaled_capsule_half_height_contract()},
        std::pair{
            static_cast<UStruct*>(
                contracts.project_world_location_to_screen),
            project_world_location_to_screen_contract()},
        std::pair{
            static_cast<UStruct*>(
                contracts.get_socket_location),
            get_socket_location_contract()},
    };
    for (const auto& [record, expected] : validations)
    {
        const auto validation = validate_unreal_record(
            record,
            expected,
            application::RuntimeContractId::EspFrame);
        if (!validation)
        {
            return std::unexpected(validation.error());
        }
    }
    return contracts;
}

auto read_object_array(
    FArrayProperty* property,
    UObject* container,
    UClass* expected_class)
    -> std::optional<std::vector<UObject*>>
{
    if (property == nullptr || container == nullptr ||
        expected_class == nullptr)
    {
        return std::nullopt;
    }
    auto* inner = CastField<FObjectPropertyBase>(
        property->GetInner());
    if (inner == nullptr ||
        inner->GetPropertyClass().Get() != expected_class)
    {
        return std::nullopt;
    }
    auto helper = FScriptArrayHelper_InContainer{
        property,
        container};
    const auto count = helper.Num();
    if (count < 0 ||
        count > static_cast<std::int32_t>(
                    core::MaximumEspTargets))
    {
        return std::nullopt;
    }
    auto values = std::vector<UObject*>{};
    values.reserve(static_cast<std::size_t>(count));
    for (auto index = std::int32_t{}; index < count; ++index)
    {
        auto* value = inner->GetObjectPropertyValue(
            helper.GetRawPtr(index));
        if (!object_is_live(value, expected_class) ||
            std::ranges::find(values, value) != values.end())
        {
            return std::nullopt;
        }
        values.push_back(value);
    }
    return values;
}

auto contains_object(
    std::span<UObject* const> values,
    UObject* object) -> bool
{
    return std::ranges::find(values, object) != values.end();
}

auto classify_esp_pawn(
    UObject* pawn,
    const EspContracts& contracts) -> core::EspRole
{
    if (object_is_live(pawn, contracts.hunter_character_class))
    {
        return core::EspRole::Hunter;
    }
    if (object_is_live(
            pawn,
            contracts.survivor_character_class))
    {
        return core::EspRole::Hider;
    }
    if (object_is_live(
            pawn,
            contracts.spectate_pawn_cleon_class) ||
        object_is_live(pawn, contracts.spectator_pawn_class))
    {
        return core::EspRole::Spectator;
    }
    return core::EspRole::Unknown;
}

auto read_esp_display_name(
    FStrProperty* property,
    UObject* player_state) -> std::string
{
    if (property == nullptr || player_state == nullptr)
    {
        return {};
    }
    const auto& value =
        property->GetPropertyValueInContainer(player_state);
    const auto& characters = value.GetCharArray();
    const auto count = characters.Num();
    if (count <= 1 || count > 257 ||
        characters.Max() < count ||
        characters.Max() > 512 ||
        characters.GetData() == nullptr ||
        characters.GetData()[count - 1] != STR('\0'))
    {
        return {};
    }
    const auto character_count = count - 1;
    const auto byte_count = WideCharToMultiByte(
        CP_UTF8,
        WC_ERR_INVALID_CHARS,
        characters.GetData(),
        character_count,
        nullptr,
        0,
        nullptr,
        nullptr);
    if (byte_count <= 0 ||
        byte_count >
            static_cast<int>(core::MaximumEspNameBytes))
    {
        return {};
    }
    auto utf8 = std::string(
        static_cast<std::size_t>(byte_count),
        '\0');
    if (WideCharToMultiByte(
            CP_UTF8,
            WC_ERR_INVALID_CHARS,
            characters.GetData(),
            character_count,
            utf8.data(),
            byte_count,
            nullptr,
            nullptr) != byte_count)
    {
        return {};
    }
    return utf8;
}

auto call_esp_vector(UObject* object, UFunction* function)
    -> EspVector3dAbi
{
    auto parameters = EspVectorReturnParametersAbi{};
    object->ProcessEvent(function, &parameters);
    return parameters.return_value;
}

auto call_esp_rotator(UObject* object, UFunction* function)
    -> EspRotatorAbi
{
    auto parameters = EspRotatorReturnParametersAbi{};
    object->ProcessEvent(function, &parameters);
    return parameters.return_value;
}

auto call_esp_float(UObject* object, UFunction* function)
    -> float
{
    auto parameters = EspFloatReturnParametersAbi{};
    object->ProcessEvent(function, &parameters);
    return parameters.return_value;
}

auto call_esp_project_world_to_screen(
    UObject* controller,
    UFunction* function,
    core::EspWorldPoint world)
    -> std::optional<core::EspScreenPoint>
{
    auto parameters =
        EspProjectWorldLocationToScreenParametersAbi{};
    parameters.world_location = EspVector3dAbi{
        world.x,
        world.y,
        world.z,
    };
    parameters.player_viewport_relative = false;
    controller->ProcessEvent(function, &parameters);
    if (!parameters.return_value ||
        !std::isfinite(parameters.screen_location.x) ||
        !std::isfinite(parameters.screen_location.y))
    {
        return std::nullopt;
    }
    return core::EspScreenPoint{
        parameters.screen_location.x,
        parameters.screen_location.y,
    };
}

auto capture_calibrated_esp_view(
    const ActiveFrame& active,
    const EspContracts& contracts,
    application::RuntimeContractId contract)
    -> std::expected<
        core::EspView,
        application::RuntimeExecutionError>
{
    if (active.viewport_width <= 0 ||
        active.viewport_height <= 0 ||
        !object_is_live(
            active.world,
            contracts.world_class) ||
        !object_is_live(
            active.controller,
            contracts.player_controller_class))
    {
        return runtime_failure(
            contract,
            application::ContractFailureKind::StaleObject);
    }
    auto* camera_manager = read_object(
        contracts.player_camera_manager,
        active.controller);
    if (!object_is_live(
            camera_manager,
            contracts.player_camera_manager_class) ||
        camera_manager->GetWorld() != active.world)
    {
        return runtime_failure(
            contract,
            application::ContractFailureKind::MissingObject);
    }

    const auto uncalibrated = decode_esp_view(
        call_esp_vector(
            camera_manager,
            contracts.get_camera_location),
        call_esp_rotator(
            camera_manager,
            contracts.get_camera_rotation),
        call_esp_float(
            camera_manager,
            contracts.get_fov_angle),
        active.viewport_width,
        active.viewport_height);
    if (!uncalibrated)
    {
        return runtime_failure(
            contract,
            application::ContractFailureKind::InvalidValue);
    }
    const auto calibration_points =
        esp_projection_calibration_points(*uncalibrated);
    if (!calibration_points)
    {
        return runtime_failure(
            contract,
            application::ContractFailureKind::InvalidValue);
    }
    const auto horizontal_engine_sample =
        call_esp_project_world_to_screen(
            active.controller,
            contracts.project_world_location_to_screen,
            (*calibration_points)[0U]);
    const auto vertical_engine_sample =
        call_esp_project_world_to_screen(
            active.controller,
            contracts.project_world_location_to_screen,
            (*calibration_points)[1U]);
    if (!horizontal_engine_sample ||
        !vertical_engine_sample)
    {
        return runtime_failure(
            contract,
            application::ContractFailureKind::InvalidValue);
    }
    const auto calibrated = calibrate_esp_view(
        *uncalibrated,
        core::EspViewport{
            static_cast<double>(active.viewport_width),
            static_cast<double>(active.viewport_height),
        },
        *horizontal_engine_sample,
        *vertical_engine_sample);
    if (!calibrated)
    {
        return runtime_failure(
            contract,
            application::ContractFailureKind::InvalidValue);
    }
    return *calibrated;
}

auto call_esp_socket_location(
    UObject* mesh,
    UFunction* function,
    std::string_view socket_name)
    -> std::optional<EspVector3dAbi>
{
    const auto wide_name = RC::to_wstring(socket_name);
    const auto unreal_name =
        FName{wide_name.c_str(), FNAME_Find};
    if (unreal_name.IsNone())
    {
        return std::nullopt;
    }
    auto parameters = EspGetSocketLocationParametersAbi{};
    parameters.in_socket_name = EspNameAbi{
        unreal_name.GetComparisonIndex().ToUnstableInt(),
        unreal_name.GetDisplayIndex().ToUnstableInt(),
        static_cast<std::uint32_t>(
            unreal_name.GetNumber()),
    };
    mesh->ProcessEvent(function, &parameters);
    if (!std::isfinite(parameters.return_value.x) ||
        !std::isfinite(parameters.return_value.y) ||
        !std::isfinite(parameters.return_value.z))
    {
        return std::nullopt;
    }
    return parameters.return_value;
}

struct EspUnresolvedAvatar
{
    UObject* player_state{};
    core::EspRole role{core::EspRole::Unknown};
};

auto build_esp_avatar_directory(
    UObject* world,
    const EspContracts& contracts,
    std::span<const EspUnresolvedAvatar> unresolved)
    -> std::optional<std::vector<EspAvatarBinding>>
{
    constexpr auto rejected_flags = static_cast<EObjectFlags>(
        RF_ClassDefaultObject | RF_ArchetypeObject |
        RF_BeginDestroyed | RF_FinishDestroyed);
    auto candidates = std::vector<
        std::pair<UObject*, UObject*>>{};
    candidates.reserve(unresolved.size());
    auto overflow = false;
    UObjectGlobals::ForEachUObject(
        [&](UObject* object, std::int32_t, std::int32_t)
            -> RC::LoopAction
        {
            const auto role = classify_esp_pawn(object, contracts);
            if ((role != core::EspRole::Hider &&
                 role != core::EspRole::Hunter) ||
                object->HasAnyFlags(rejected_flags) ||
                object->GetWorld() != world)
            {
                return RC::LoopAction::Continue;
            }
            auto* player_state = read_object(
                contracts.pawn_player_state,
                object);
            const auto relevant = std::ranges::find_if(
                unresolved,
                [&](const EspUnresolvedAvatar& value)
                {
                    return value.player_state == player_state &&
                           value.role == role;
                });
            if (relevant == unresolved.end())
            {
                return RC::LoopAction::Continue;
            }
            if (candidates.size() >=
                MaximumEspAvatarCandidates)
            {
                overflow = true;
                return RC::LoopAction::Break;
            }
            candidates.emplace_back(player_state, object);
            return RC::LoopAction::Continue;
        });
    if (overflow)
    {
        return std::nullopt;
    }

    auto bindings = std::vector<EspAvatarBinding>{};
    bindings.reserve(unresolved.size());
    for (const auto& value : unresolved)
    {
        auto match = static_cast<UObject*>(nullptr);
        auto count = std::size_t{};
        for (const auto& [player_state, avatar] : candidates)
        {
            if (player_state == value.player_state)
            {
                match = avatar;
                ++count;
            }
        }
        if (count == 1U)
        {
            bindings.push_back(EspAvatarBinding{
                FWeakObjectPtr{value.player_state},
                FWeakObjectPtr{match},
                value.role,
                true,
            });
            continue;
        }
        bindings.push_back(EspAvatarBinding{
            FWeakObjectPtr{value.player_state},
            {},
            value.role,
            false,
        });
    }
    return bindings;
}

auto steady_milliseconds() -> std::uint64_t
{
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

auto canvas_failure(const char* detail)
    -> std::unexpected<
        product_ui::ProductUiFrameRuntimeError>
{
    return std::unexpected(
        product_ui::ProductUiFrameRuntimeError{detail});
}

auto is_current_process_unreal_window(HWND window) -> bool
{
    if (window == nullptr || !IsWindow(window))
    {
        return false;
    }
    DWORD process_id{};
    if (GetWindowThreadProcessId(window, &process_id) == 0U ||
        process_id != GetCurrentProcessId())
    {
        return false;
    }
    wchar_t class_name[32]{};
    const auto length = GetClassNameW(
        window,
        class_name,
        static_cast<int>(
            sizeof(class_name) / sizeof(class_name[0])));
    return length > 0 &&
           lstrcmpW(class_name, L"UnrealWindow") == 0;
}

auto texture_failure(const char* detail)
    -> std::unexpected<
        product_ui::ImageEditorTextureRuntimeError>
{
    return std::unexpected(
        product_ui::ImageEditorTextureRuntimeError{detail});
}

auto input_failure(const char* detail)
    -> std::unexpected<ui::InputPortError>
{
    return std::unexpected(ui::InputPortError{detail});
}

auto resolve_input_contracts(
    UClass* player_controller_class)
    -> std::expected<
        InputContracts,
        application::RuntimeExecutionError>
{
    auto contracts = InputContracts{};
    contracts.controller_class = find_class(ControllerClassPath);
    contracts.player_controller_class =
        player_controller_class;
    contracts.is_look_input_ignored =
        UObjectGlobals::StaticFindObject<UFunction*>(
            nullptr,
            nullptr,
            IsLookInputIgnoredPath);
    contracts.is_move_input_ignored =
        UObjectGlobals::StaticFindObject<UFunction*>(
            nullptr,
            nullptr,
            IsMoveInputIgnoredPath);
    contracts.set_ignore_look_input =
        UObjectGlobals::StaticFindObject<UFunction*>(
            nullptr,
            nullptr,
            SetIgnoreLookInputPath);
    contracts.set_ignore_move_input =
        UObjectGlobals::StaticFindObject<UFunction*>(
            nullptr,
            nullptr,
            SetIgnoreMoveInputPath);
    contracts.show_mouse_cursor = find_bool_property(
        player_controller_class,
        STR("bShowMouseCursor"));

    if (contracts.controller_class == nullptr ||
        contracts.player_controller_class == nullptr)
    {
        return runtime_failure(
            application::RuntimeContractId::InputControl,
            application::ContractFailureKind::MissingObject);
    }
    if (contracts.show_mouse_cursor == nullptr)
    {
        return runtime_failure(
            application::RuntimeContractId::InputControl,
            application::ContractFailureKind::MissingProperty);
    }
    if (contracts.is_look_input_ignored == nullptr ||
        contracts.is_move_input_ignored == nullptr ||
        contracts.set_ignore_look_input == nullptr ||
        contracts.set_ignore_move_input == nullptr)
    {
        return runtime_failure(
            application::RuntimeContractId::InputControl,
            application::ContractFailureKind::MissingFunction);
    }
    if (contracts.is_look_input_ignored->GetOuterPrivate() !=
            contracts.controller_class ||
        contracts.is_move_input_ignored->GetOuterPrivate() !=
            contracts.controller_class ||
        contracts.set_ignore_look_input->GetOuterPrivate() !=
            contracts.controller_class ||
        contracts.set_ignore_move_input->GetOuterPrivate() !=
            contracts.controller_class)
    {
        return runtime_failure(
            application::RuntimeContractId::InputControl,
            application::ContractFailureKind::WrongClass);
    }

    const auto look_query = validate_unreal_record(
        contracts.is_look_input_ignored,
        is_look_input_ignored_contract(),
        application::RuntimeContractId::InputControl);
    if (!look_query)
    {
        return std::unexpected(look_query.error());
    }
    const auto move_query = validate_unreal_record(
        contracts.is_move_input_ignored,
        is_move_input_ignored_contract(),
        application::RuntimeContractId::InputControl);
    if (!move_query)
    {
        return std::unexpected(move_query.error());
    }
    const auto look_command = validate_unreal_record(
        contracts.set_ignore_look_input,
        set_ignore_look_input_contract(),
        application::RuntimeContractId::InputControl);
    if (!look_command)
    {
        return std::unexpected(look_command.error());
    }
    const auto move_command = validate_unreal_record(
        contracts.set_ignore_move_input,
        set_ignore_move_input_contract(),
        application::RuntimeContractId::InputControl);
    if (!move_command)
    {
        return std::unexpected(move_command.error());
    }
    return contracts;
}

auto resolve_canvas_contracts(UClass* canvas_class)
    -> std::expected<
        CanvasContracts,
        application::RuntimeExecutionError>
{
    auto contracts = CanvasContracts{};
    contracts.canvas_class = canvas_class;
    contracts.draw_line =
        UObjectGlobals::StaticFindObject<UFunction*>(
            nullptr,
            nullptr,
            K2DrawLinePath);
    contracts.draw_texture =
        UObjectGlobals::StaticFindObject<UFunction*>(
            nullptr,
            nullptr,
            K2DrawTexturePath);
    contracts.draw_text =
        UObjectGlobals::StaticFindObject<UFunction*>(
            nullptr,
            nullptr,
            K2DrawTextPath);
    contracts.vector2d =
        UObjectGlobals::StaticFindObject<UScriptStruct*>(
            nullptr,
            nullptr,
            Vector2dPath);
    contracts.linear_color =
        UObjectGlobals::StaticFindObject<UScriptStruct*>(
            nullptr,
            nullptr,
            LinearColorPath);
    contracts.font_class = find_class(FontClassPath);
    contracts.kismet_rendering_library_class =
        find_class(KismetRenderingLibraryClassPath);
    contracts.texture2d_class = find_class(Texture2dClassPath);
    contracts.import_buffer_as_texture2d =
        UObjectGlobals::StaticFindObject<UFunction*>(
            nullptr,
            nullptr,
            ImportBufferAsTexture2DPath);
    if (contracts.kismet_rendering_library_class != nullptr)
    {
        contracts.kismet_rendering_library_cdo =
            contracts.kismet_rendering_library_class
                ->GetClassDefaultObject()
                .Get();
    }

    if (contracts.canvas_class == nullptr ||
        contracts.vector2d == nullptr ||
        contracts.linear_color == nullptr ||
        contracts.font_class == nullptr ||
        contracts.kismet_rendering_library_class == nullptr ||
        contracts.kismet_rendering_library_cdo == nullptr ||
        contracts.texture2d_class == nullptr)
    {
        return runtime_failure(
            application::RuntimeContractId::Canvas,
            application::ContractFailureKind::MissingObject);
    }
    if (contracts.draw_line == nullptr ||
        contracts.draw_texture == nullptr ||
        contracts.draw_text == nullptr ||
        contracts.import_buffer_as_texture2d == nullptr)
    {
        return runtime_failure(
            application::RuntimeContractId::Canvas,
            application::ContractFailureKind::MissingFunction);
    }
    if (contracts.draw_line->GetOuterPrivate() !=
            contracts.canvas_class ||
        contracts.draw_texture->GetOuterPrivate() !=
            contracts.canvas_class ||
        contracts.draw_text->GetOuterPrivate() !=
            contracts.canvas_class ||
        contracts.import_buffer_as_texture2d
                ->GetOuterPrivate() !=
            contracts.kismet_rendering_library_class ||
        contracts.kismet_rendering_library_cdo
                ->GetClassPrivate() !=
            contracts.kismet_rendering_library_class ||
        !contracts.kismet_rendering_library_cdo
             ->HasAnyFlags(RF_ClassDefaultObject) ||
        !object_is_live(
            contracts.kismet_rendering_library_cdo,
            contracts.kismet_rendering_library_class))
    {
        return runtime_failure(
            application::RuntimeContractId::Canvas,
            application::ContractFailureKind::WrongClass);
    }

    const auto vector_result = validate_unreal_record(
        contracts.vector2d,
        vector2d_contract(),
        application::RuntimeContractId::Canvas);
    if (!vector_result)
    {
        return std::unexpected(vector_result.error());
    }
    const auto color_result = validate_unreal_record(
        contracts.linear_color,
        linear_color_contract(),
        application::RuntimeContractId::Canvas);
    if (!color_result)
    {
        return std::unexpected(color_result.error());
    }
    const auto line_result = validate_unreal_record(
        contracts.draw_line,
        k2_draw_line_contract(),
        application::RuntimeContractId::Canvas);
    if (!line_result)
    {
        return std::unexpected(line_result.error());
    }
    const auto texture_result = validate_unreal_record(
        contracts.draw_texture,
        k2_draw_texture_contract(),
        application::RuntimeContractId::Canvas);
    if (!texture_result)
    {
        return std::unexpected(texture_result.error());
    }
    const auto text_result = validate_unreal_record(
        contracts.draw_text,
        k2_draw_text_contract(),
        application::RuntimeContractId::Canvas);
    if (!text_result)
    {
        return std::unexpected(text_result.error());
    }
    const auto import_result = validate_unreal_record(
        contracts.import_buffer_as_texture2d,
        import_buffer_as_texture2d_contract(),
        application::RuntimeContractId::Canvas);
    if (!import_result)
    {
        return std::unexpected(import_result.error());
    }

    const auto game_font_path =
        FSoftObjectPath{FString{GameFontAssetPath}};
    auto* game_font = game_font_path.ResolveObject();
    if (game_font == nullptr)
    {
        game_font = game_font_path.TryLoad();
    }
    if (!object_is_live_exact(
        game_font,
            contracts.font_class) ||
        game_font->GetPathName() !=
            RC::StringType{GameFontAssetPath})
    {
        return runtime_failure(
            application::RuntimeContractId::Canvas,
            game_font == nullptr
                ? application::ContractFailureKind::
                      MissingObject
                : application::ContractFailureKind::
                      WrongClass);
    }
    contracts.game_font = FWeakObjectPtr{game_font};
    return contracts;
}

class ExportedChannelBuffer final
{
public:
    explicit ExportedChannelBuffer(RuntimeByteArrayAbi& array)
        : array_{array}
    {
    }

    ExportedChannelBuffer(const ExportedChannelBuffer&) = delete;
    auto operator=(const ExportedChannelBuffer&)
        -> ExportedChannelBuffer& = delete;

    ~ExportedChannelBuffer() noexcept
    {
        if (array_.data != nullptr && GMalloc != nullptr &&
            *GMalloc != nullptr)
        {
            try
            {
                (*GMalloc)->Free(
                    const_cast<std::byte*>(array_.data));
            }
            catch (...)
            {
            }
        }
        array_ = {};
    }

private:
    RuntimeByteArrayAbi& array_;
};

auto export_channel(
    UObject* component,
    UFunction* function,
    RuntimePaintChannel channel)
    -> std::expected<
        std::vector<std::byte>,
        application::RuntimeExecutionError>
{
    if (component == nullptr || function == nullptr)
    {
        return runtime_failure(
            application::RuntimeContractId::TextureMutation,
            application::ContractFailureKind::MissingObject);
    }

    auto parameters = ExportChannelToBytesParametersAbi{};
    [[maybe_unused]] auto release =
        ExportedChannelBuffer{parameters.out_data};
    parameters.channel = channel;
    component->ProcessEvent(function, &parameters);

    if (!parameters.return_value ||
        parameters.out_data.data == nullptr ||
        parameters.out_data.count <= 0 ||
        parameters.out_data.capacity <
            parameters.out_data.count ||
        static_cast<std::size_t>(
            parameters.out_data.capacity) >
            MaximumPaintChannelBytes ||
        static_cast<std::size_t>(
            parameters.out_data.count) >
            MaximumPaintChannelBytes)
    {
        return runtime_failure(
            application::RuntimeContractId::TextureMutation,
            application::ContractFailureKind::InvalidValue);
    }

    auto bytes = std::vector<std::byte>(
        static_cast<std::size_t>(
            parameters.out_data.count));
    std::memcpy(
        bytes.data(),
        parameters.out_data.data,
        bytes.size());
    return bytes;
}

auto import_channel(
    UObject* component,
    UFunction* function,
    RuntimePaintChannel channel,
    std::span<const std::byte> bytes)
    -> std::expected<
        void,
        application::RuntimeExecutionError>
{
    if (component == nullptr || function == nullptr)
    {
        return runtime_failure(
            application::RuntimeContractId::TextureMutation,
            application::ContractFailureKind::MissingObject);
    }
    const auto encoded =
        encode_channel_import(channel, bytes);
    if (!encoded)
    {
        return runtime_failure(
            application::RuntimeContractId::TextureMutation,
            application::ContractFailureKind::InvalidValue);
    }

    auto parameters = *encoded;
    component->ProcessEvent(function, &parameters);
    if (!parameters.return_value)
    {
        return runtime_failure(
            application::RuntimeContractId::TextureMutation,
            application::ContractFailureKind::ExecutionFailure);
    }
    return {};
}

auto import_channel_verified(
    UObject* component,
    UFunction* import_function,
    UFunction* export_function,
    RuntimePaintChannel import_channel_value,
    RuntimePaintChannel verification_channel,
    std::span<const std::byte> bytes)
    -> std::expected<
        void,
        application::RuntimeExecutionError>
{
    const auto imported = import_channel(
        component,
        import_function,
        import_channel_value,
        bytes);
    if (!imported)
    {
        return imported;
    }

    const auto readback = export_channel(
        component,
        export_function,
        verification_channel);
    if (!readback)
    {
        return std::unexpected(readback.error());
    }
    if (readback->size() != bytes.size() ||
        !std::equal(
            readback->begin(),
            readback->end(),
            bytes.begin()))
    {
        return runtime_failure(
            application::RuntimeContractId::TextureMutation,
            application::ContractFailureKind::InvalidValue);
    }
    return {};
}

auto resolve_paint_contracts(UClass* player_controller_class)
    -> std::expected<
        PaintContracts,
        application::RuntimeExecutionError>
{
    auto contracts = PaintContracts{};
    contracts.player_controller_class = player_controller_class;
    contracts.pawn_class = find_class(PawnClassPath);
    contracts.mesh_component_class =
        find_class(MeshComponentClassPath);
    contracts.runtime_paintable_class =
        find_class(RuntimePaintableClassPath);
    contracts.replication_manager_class =
        find_class(RuntimePaintReplicationManagerClassPath);
    contracts.is_initialized =
        UObjectGlobals::StaticFindObject<UFunction*>(
            nullptr,
            nullptr,
            IsPaintInitializedPath);
    contracts.initialize_paint =
        UObjectGlobals::StaticFindObject<UFunction*>(
            nullptr,
            nullptr,
            InitializePaintPath);
    contracts.get_initialized_paint_mesh =
        UObjectGlobals::StaticFindObject<UFunction*>(
            nullptr,
            nullptr,
            GetInitializedPaintMeshPath);
    contracts.get_socket_transform =
        UObjectGlobals::StaticFindObject<UFunction*>(
            nullptr,
            nullptr,
            GetSocketTransformPath);
    contracts.paint_at_uv_with_brush =
        UObjectGlobals::StaticFindObject<UFunction*>(
            nullptr,
            nullptr,
            PaintAtUvWithBrushPath);
    contracts.get_recorded_stroke_count =
        UObjectGlobals::StaticFindObject<UFunction*>(
            nullptr,
            nullptr,
            GetRecordedStrokeCountPath);
    contracts.get_queued_stroke_count =
        UObjectGlobals::StaticFindObject<UFunction*>(
            nullptr,
            nullptr,
            GetQueuedStrokeCountPath);
    contracts.get_queued_stroke_count_for_component =
        UObjectGlobals::StaticFindObject<UFunction*>(
            nullptr,
            nullptr,
            GetQueuedStrokeCountForComponentPath);
    contracts.get_replication_pressure =
        UObjectGlobals::StaticFindObject<UFunction*>(
            nullptr,
            nullptr,
            GetReplicationPressurePath);
    contracts.export_channel_to_bytes =
        UObjectGlobals::StaticFindObject<UFunction*>(
            nullptr,
            nullptr,
            ExportChannelToBytesPath);
    contracts.import_channel_from_bytes =
        UObjectGlobals::StaticFindObject<UFunction*>(
            nullptr,
            nullptr,
            ImportChannelFromBytesPath);
    contracts.vector2d =
        UObjectGlobals::StaticFindObject<UScriptStruct*>(
            nullptr,
            nullptr,
            Vector2dPath);
    contracts.paint_channel_data =
        UObjectGlobals::StaticFindObject<UScriptStruct*>(
            nullptr,
            nullptr,
            PaintChannelDataPath);
    contracts.runtime_brush_settings =
        UObjectGlobals::StaticFindObject<UScriptStruct*>(
            nullptr,
            nullptr,
            RuntimeBrushSettingsPath);
    contracts.runtime_paint_replication_pressure =
        UObjectGlobals::StaticFindObject<UScriptStruct*>(
            nullptr,
            nullptr,
            RuntimePaintReplicationPressurePath);
    if (player_controller_class == nullptr ||
        contracts.pawn_class == nullptr ||
        contracts.mesh_component_class == nullptr ||
        contracts.runtime_paintable_class == nullptr ||
        contracts.replication_manager_class == nullptr ||
        contracts.vector2d == nullptr ||
        contracts.paint_channel_data == nullptr ||
        contracts.runtime_brush_settings == nullptr ||
        contracts.runtime_paint_replication_pressure == nullptr)
    {
        return runtime_failure(
            application::RuntimeContractId::
                PaintAtUvWithBrush,
            application::ContractFailureKind::MissingObject);
    }
    if (contracts.is_initialized == nullptr ||
        contracts.initialize_paint == nullptr ||
        contracts.get_initialized_paint_mesh == nullptr ||
        contracts.get_socket_transform == nullptr)
    {
        return runtime_failure(
            application::RuntimeContractId::PaintCapture,
            application::ContractFailureKind::MissingFunction);
    }
    if (contracts.paint_at_uv_with_brush == nullptr)
    {
        return runtime_failure(
            application::RuntimeContractId::
                PaintAtUvWithBrush,
            application::ContractFailureKind::MissingFunction);
    }
    if (contracts.get_recorded_stroke_count == nullptr ||
        contracts.get_queued_stroke_count == nullptr ||
        contracts.get_queued_stroke_count_for_component == nullptr ||
        contracts.get_replication_pressure == nullptr)
    {
        return runtime_failure(
            application::RuntimeContractId::
                PaintQueueObservation,
            application::ContractFailureKind::MissingFunction);
    }
    if (contracts.export_channel_to_bytes == nullptr ||
        contracts.import_channel_from_bytes == nullptr)
    {
        return runtime_failure(
            application::RuntimeContractId::TextureMutation,
            application::ContractFailureKind::MissingFunction);
    }
    if (contracts.paint_at_uv_with_brush->GetOuterPrivate() !=
        contracts.runtime_paintable_class)
    {
        return runtime_failure(
            application::RuntimeContractId::
                PaintAtUvWithBrush,
            application::ContractFailureKind::WrongClass);
    }
    if (contracts.is_initialized->GetOuterPrivate() !=
            contracts.runtime_paintable_class ||
        contracts.initialize_paint->GetOuterPrivate() !=
            contracts.runtime_paintable_class ||
        contracts.get_initialized_paint_mesh
                ->GetOuterPrivate() !=
            contracts.runtime_paintable_class)
    {
        return runtime_failure(
            application::RuntimeContractId::PaintCapture,
            application::ContractFailureKind::WrongClass);
    }
    if (contracts.get_recorded_stroke_count->GetOuterPrivate() !=
            contracts.runtime_paintable_class ||
        contracts.get_queued_stroke_count->GetOuterPrivate() !=
            contracts.replication_manager_class ||
        contracts.get_queued_stroke_count_for_component
                ->GetOuterPrivate() !=
            contracts.replication_manager_class ||
        contracts.get_replication_pressure->GetOuterPrivate() !=
            contracts.replication_manager_class)
    {
        return runtime_failure(
            application::RuntimeContractId::
                PaintQueueObservation,
            application::ContractFailureKind::WrongClass);
    }
    if (contracts.export_channel_to_bytes->GetOuterPrivate() !=
            contracts.runtime_paintable_class ||
        contracts.import_channel_from_bytes->GetOuterPrivate() !=
            contracts.runtime_paintable_class)
    {
        return runtime_failure(
            application::RuntimeContractId::TextureMutation,
            application::ContractFailureKind::WrongClass);
    }

    contracts.acknowledged_pawn = find_object_property(
        player_controller_class,
        STR("AcknowledgedPawn"),
        contracts.pawn_class);
    if (contracts.acknowledged_pawn == nullptr)
    {
        return runtime_failure(
            application::RuntimeContractId::PlayerController,
            application::ContractFailureKind::MissingProperty);
    }

    const auto vector_result = validate_unreal_record(
        contracts.vector2d,
        vector2d_contract(),
        application::RuntimeContractId::PaintAtUvWithBrush);
    if (!vector_result)
    {
        return std::unexpected(vector_result.error());
    }
    const auto channel_result = validate_unreal_record(
        contracts.paint_channel_data,
        paint_channel_data_contract(),
        application::RuntimeContractId::PaintAtUvWithBrush);
    if (!channel_result)
    {
        return std::unexpected(channel_result.error());
    }
    const auto brush_result = validate_unreal_record(
        contracts.runtime_brush_settings,
        runtime_brush_settings_contract(),
        application::RuntimeContractId::PaintAtUvWithBrush);
    if (!brush_result)
    {
        return std::unexpected(brush_result.error());
    }
    const auto function_result = validate_unreal_record(
        contracts.paint_at_uv_with_brush,
        paint_at_uv_with_brush_contract(),
        application::RuntimeContractId::PaintAtUvWithBrush);
    if (!function_result)
    {
        return std::unexpected(function_result.error());
    }
    const auto initialized_result = validate_unreal_record(
        contracts.is_initialized,
        is_paint_initialized_contract(),
        application::RuntimeContractId::PaintCapture);
    if (!initialized_result)
    {
        return std::unexpected(initialized_result.error());
    }
    const auto initialize_result = validate_unreal_record(
        contracts.initialize_paint,
        initialize_paint_contract(),
        application::RuntimeContractId::PaintCapture);
    if (!initialize_result)
    {
        return std::unexpected(initialize_result.error());
    }
    const auto initialized_mesh_result =
        validate_unreal_record(
            contracts.get_initialized_paint_mesh,
            get_initialized_paint_mesh_contract(),
            application::RuntimeContractId::PaintCapture);
    if (!initialized_mesh_result)
    {
        return std::unexpected(
            initialized_mesh_result.error());
    }
    const auto socket_transform_result =
        validate_unreal_record(
            contracts.get_socket_transform,
            get_socket_transform_contract(),
            application::RuntimeContractId::PaintCapture);
    if (!socket_transform_result)
    {
        return std::unexpected(
            socket_transform_result.error());
    }
    const auto recorded_result = validate_unreal_record(
        contracts.get_recorded_stroke_count,
        recorded_stroke_count_contract(),
        application::RuntimeContractId::PaintQueueObservation);
    if (!recorded_result)
    {
        return std::unexpected(recorded_result.error());
    }
    const auto pressure_struct_result = validate_unreal_record(
        contracts.runtime_paint_replication_pressure,
        runtime_paint_replication_pressure_contract(),
        application::RuntimeContractId::PaintQueueObservation);
    if (!pressure_struct_result)
    {
        return std::unexpected(pressure_struct_result.error());
    }
    const auto manager_count_result = validate_unreal_record(
        contracts.get_queued_stroke_count,
        queued_stroke_count_contract(),
        application::RuntimeContractId::PaintQueueObservation);
    if (!manager_count_result)
    {
        return std::unexpected(manager_count_result.error());
    }
    const auto component_count_result = validate_unreal_record(
        contracts.get_queued_stroke_count_for_component,
        queued_stroke_count_for_component_contract(),
        application::RuntimeContractId::PaintQueueObservation);
    if (!component_count_result)
    {
        return std::unexpected(component_count_result.error());
    }
    const auto pressure_result = validate_unreal_record(
        contracts.get_replication_pressure,
        replication_pressure_contract(),
        application::RuntimeContractId::PaintQueueObservation);
    if (!pressure_result)
    {
        return std::unexpected(pressure_result.error());
    }
    const auto export_result = validate_unreal_record(
        contracts.export_channel_to_bytes,
        export_channel_to_bytes_contract(),
        application::RuntimeContractId::TextureMutation);
    if (!export_result)
    {
        return std::unexpected(export_result.error());
    }
    const auto import_result = validate_unreal_record(
        contracts.import_channel_from_bytes,
        import_channel_from_bytes_contract(),
        application::RuntimeContractId::TextureMutation);
    if (!import_result)
    {
        return std::unexpected(import_result.error());
    }
    return contracts;
}

auto resolve_paint_scene_capture_contracts(
    UClass* kismet_rendering_library_class,
    UObject* kismet_rendering_library_cdo)
    -> std::expected<
        PaintSceneCaptureContracts,
        application::RuntimeExecutionError>
{
    auto contracts = PaintSceneCaptureContracts{};
    contracts.actor_class = find_class(ActorClassPath);
    contracts.scene_component_class =
        find_class(SceneComponentClassPath);
    contracts.primitive_component_class =
        find_class(PrimitiveComponentClassPath);
    contracts.scene_capture_component_class =
        find_class(SceneCaptureComponentClassPath);
    contracts.scene_capture_component_2d_class =
        find_class(SceneCaptureComponent2dClassPath);
    contracts.scene_capture_2d_class =
        find_class(SceneCapture2dClassPath);
    contracts.texture_render_target_2d_class =
        find_class(TextureRenderTarget2dClassPath);
    contracts.kismet_rendering_library_class =
        kismet_rendering_library_class;
    contracts.kismet_rendering_library_cdo =
        kismet_rendering_library_cdo;
    contracts.create_render_target_2d =
        UObjectGlobals::StaticFindObject<UFunction*>(
            nullptr,
            nullptr,
            CreateRenderTarget2dPath);
    contracts.read_render_target_raw =
        UObjectGlobals::StaticFindObject<UFunction*>(
            nullptr,
            nullptr,
            ReadRenderTargetRawPath);
    contracts.capture_scene =
        UObjectGlobals::StaticFindObject<UFunction*>(
            nullptr,
            nullptr,
            CaptureScenePath);
    contracts.set_show_flag_settings =
        UObjectGlobals::StaticFindObject<UFunction*>(
            nullptr,
            nullptr,
            SetShowFlagSettingsPath);
    contracts.hide_component =
        UObjectGlobals::StaticFindObject<UFunction*>(
            nullptr,
            nullptr,
            HideComponentPath);
    contracts.destroy_actor =
        UObjectGlobals::StaticFindObject<UFunction*>(
            nullptr,
            nullptr,
            K2DestroyActorPath);

    const auto required_objects = std::array{
        static_cast<UObject*>(contracts.actor_class),
        static_cast<UObject*>(
            contracts.scene_component_class),
        static_cast<UObject*>(
            contracts.primitive_component_class),
        static_cast<UObject*>(
            contracts.scene_capture_component_class),
        static_cast<UObject*>(
            contracts.scene_capture_component_2d_class),
        static_cast<UObject*>(contracts.scene_capture_2d_class),
        static_cast<UObject*>(
            contracts.texture_render_target_2d_class),
        static_cast<UObject*>(
            contracts.kismet_rendering_library_class),
        contracts.kismet_rendering_library_cdo,
    };
    if (std::ranges::any_of(
            required_objects,
            [](UObject* object) { return object == nullptr; }))
    {
        return runtime_failure(
            application::RuntimeContractId::PaintCapture,
            application::ContractFailureKind::MissingObject);
    }
    if (!contracts.primitive_component_class->IsChildOf(
            contracts.scene_component_class) ||
        !contracts.scene_capture_component_class->IsChildOf(
            contracts.scene_component_class) ||
        !contracts.scene_capture_component_2d_class->IsChildOf(
            contracts.scene_capture_component_class) ||
        !contracts.scene_capture_2d_class->IsChildOf(
            contracts.actor_class) ||
        contracts.kismet_rendering_library_cdo
                ->GetClassPrivate() !=
            contracts.kismet_rendering_library_class ||
        !contracts.kismet_rendering_library_cdo->HasAnyFlags(
            RF_ClassDefaultObject) ||
        !object_is_live(
            contracts.kismet_rendering_library_cdo,
            contracts.kismet_rendering_library_class))
    {
        return runtime_failure(
            application::RuntimeContractId::PaintCapture,
            application::ContractFailureKind::WrongClass);
    }
    const auto required_functions = std::array{
        contracts.create_render_target_2d,
        contracts.read_render_target_raw,
        contracts.capture_scene,
        contracts.set_show_flag_settings,
        contracts.hide_component,
        contracts.destroy_actor,
    };
    if (std::ranges::any_of(
            required_functions,
            [](UFunction* function)
            {
                return function == nullptr;
            }))
    {
        return runtime_failure(
            application::RuntimeContractId::PaintCapture,
            application::ContractFailureKind::MissingFunction);
    }
    if (contracts.create_render_target_2d->GetOuterPrivate() !=
            contracts.kismet_rendering_library_class ||
        contracts.read_render_target_raw->GetOuterPrivate() !=
            contracts.kismet_rendering_library_class ||
        contracts.capture_scene->GetOuterPrivate() !=
            contracts.scene_capture_component_2d_class ||
        contracts.set_show_flag_settings->GetOuterPrivate() !=
            contracts.scene_capture_component_class ||
        contracts.hide_component->GetOuterPrivate() !=
            contracts.scene_capture_component_class ||
        contracts.destroy_actor->GetOuterPrivate() !=
            contracts.actor_class)
    {
        return runtime_failure(
            application::RuntimeContractId::PaintCapture,
            application::ContractFailureKind::WrongClass);
    }

    contracts.capture_component_2d =
        find_exact_object_property(
            contracts.scene_capture_2d_class,
            contracts.scene_capture_2d_class,
            STR("CaptureComponent2D"),
            contracts.scene_capture_component_2d_class);
    contracts.capture_source = find_exact_enum_property(
        contracts.scene_capture_component_class,
        contracts.scene_capture_component_class,
        STR("CaptureSource"),
        STR("ESceneCaptureSource"));
    contracts.capture_every_frame = find_bool_property(
        contracts.scene_capture_component_class,
        STR("bCaptureEveryFrame"));
    contracts.capture_on_movement = find_bool_property(
        contracts.scene_capture_component_class,
        STR("bCaptureOnMovement"));
    contracts.always_persist_rendering_state =
        find_bool_property(
            contracts.scene_capture_component_class,
            STR("bAlwaysPersistRenderingState"));
    contracts.projection_type = find_exact_enum_property(
        contracts.scene_capture_component_2d_class,
        contracts.scene_capture_component_2d_class,
        STR("ProjectionType"),
        STR("ECameraProjectionMode"));
    contracts.field_of_view_angle =
        find_exact_float_property(
            contracts.scene_capture_component_2d_class,
            contracts.scene_capture_component_2d_class,
            STR("FOVAngle"));
    contracts.texture_target = find_exact_object_property(
        contracts.scene_capture_component_2d_class,
        contracts.scene_capture_component_2d_class,
        STR("TextureTarget"),
        contracts.texture_render_target_2d_class);
    contracts.set_show_flag_settings_parameter =
        CastField<FArrayProperty>(
            contracts.set_show_flag_settings->FindProperty(
                FName{STR("ShowFlagSettings"), FNAME_Find}));
    contracts.show_flag_settings =
        CastField<FArrayProperty>(
            contracts.scene_capture_component_class->FindProperty(
                FName{STR("ShowFlagSettings"), FNAME_Find}));
    auto* parameter_inner =
        contracts.set_show_flag_settings_parameter == nullptr
            ? nullptr
            : CastField<FStructProperty>(
                  contracts.set_show_flag_settings_parameter
                      ->GetInner());
    auto* component_inner =
        contracts.show_flag_settings == nullptr
            ? nullptr
            : CastField<FStructProperty>(
                  contracts.show_flag_settings->GetInner());
    auto* parameter_struct =
        parameter_inner == nullptr
            ? nullptr
            : parameter_inner->GetStruct().Get();
    auto* component_struct =
        component_inner == nullptr
            ? nullptr
            : component_inner->GetStruct().Get();
    contracts.show_flag_setting_struct =
        parameter_struct == component_struct
            ? parameter_struct
            : nullptr;
    contracts.show_flag_name =
        contracts.show_flag_setting_struct == nullptr
            ? nullptr
            : CastField<FStrProperty>(
                  contracts.show_flag_setting_struct
                      ->FindProperty(
                          FName{
                              STR("ShowFlagName"),
                              FNAME_Find}));
    contracts.show_flag_enabled =
        contracts.show_flag_setting_struct == nullptr
            ? nullptr
            : CastField<FBoolProperty>(
                  contracts.show_flag_setting_struct
                      ->FindProperty(
                          FName{STR("Enabled"), FNAME_Find}));
    const auto required_properties = std::array{
        static_cast<FProperty*>(
            contracts.capture_component_2d),
        contracts.capture_source.property,
        static_cast<FProperty*>(
            contracts.capture_every_frame),
        static_cast<FProperty*>(
            contracts.capture_on_movement),
        static_cast<FProperty*>(
            contracts.always_persist_rendering_state),
        contracts.projection_type.property,
        static_cast<FProperty*>(
            contracts.field_of_view_angle),
        static_cast<FProperty*>(contracts.texture_target),
        static_cast<FProperty*>(
            contracts.set_show_flag_settings_parameter),
        static_cast<FProperty*>(
            contracts.show_flag_settings),
        static_cast<FProperty*>(
            contracts.show_flag_name),
        static_cast<FProperty*>(
            contracts.show_flag_enabled),
    };
    if (std::ranges::any_of(
            required_properties,
            [](FProperty* property)
            {
                return property == nullptr;
            }))
    {
        return runtime_failure(
            application::RuntimeContractId::PaintCapture,
            application::ContractFailureKind::MissingProperty);
    }
    if (contracts.set_show_flag_settings_parameter
                ->GetOwner<UFunction>() !=
            contracts.set_show_flag_settings ||
        contracts.set_show_flag_settings_parameter
                ->GetArrayDim() != 1 ||
        contracts.set_show_flag_settings_parameter
                ->GetElementSize() !=
            static_cast<int>(sizeof(FScriptArray)) ||
        contracts.show_flag_settings->GetOwner<UClass>() !=
            contracts.scene_capture_component_class ||
        contracts.show_flag_settings->GetArrayDim() != 1 ||
        contracts.show_flag_settings->GetElementSize() !=
            static_cast<int>(sizeof(FScriptArray)) ||
        !contracts.show_flag_settings->IsInContainer(
            contracts.scene_capture_component_class) ||
        contracts.show_flag_setting_struct
                ->GetPropertiesSize() <= 0 ||
        RC::to_string(
            contracts.show_flag_setting_struct->GetName()) !=
            "EngineShowFlagsSetting" ||
        parameter_inner->GetElementSize() !=
            contracts.show_flag_setting_struct
                ->GetPropertiesSize() ||
        component_inner->GetElementSize() !=
            contracts.show_flag_setting_struct
                ->GetPropertiesSize() ||
        contracts.show_flag_name
                ->GetOwner<UScriptStruct>() !=
            contracts.show_flag_setting_struct ||
        contracts.show_flag_name->GetArrayDim() != 1 ||
        contracts.show_flag_name->GetElementSize() !=
            static_cast<int>(sizeof(FString)) ||
        !contracts.show_flag_name->IsInContainer(
            contracts.show_flag_setting_struct) ||
        contracts.show_flag_enabled
                ->GetOwner<UScriptStruct>() !=
            contracts.show_flag_setting_struct ||
        contracts.show_flag_enabled->GetArrayDim() != 1 ||
        !contracts.show_flag_enabled->IsInContainer(
            contracts.show_flag_setting_struct))
    {
        return runtime_failure(
            application::RuntimeContractId::PaintCapture,
            application::ContractFailureKind::
                WrongPropertyKind);
    }

    const auto validations = std::array{
        std::pair{
            static_cast<UStruct*>(
                contracts.create_render_target_2d),
            create_render_target_2d_contract()},
        std::pair{
            static_cast<UStruct*>(
                contracts.read_render_target_raw),
            read_render_target_raw_contract()},
        std::pair{
            static_cast<UStruct*>(
                contracts.set_show_flag_settings),
            set_show_flag_settings_contract()},
        std::pair{
            static_cast<UStruct*>(contracts.capture_scene),
            capture_scene_contract()},
        std::pair{
            static_cast<UStruct*>(contracts.hide_component),
            hide_component_contract()},
        std::pair{
            static_cast<UStruct*>(contracts.destroy_actor),
            k2_destroy_actor_contract()},
    };
    for (const auto& [record, expected] : validations)
    {
        const auto validated = validate_unreal_record(
            record,
            expected,
            application::RuntimeContractId::PaintCapture);
        if (!validated)
        {
            return std::unexpected(validated.error());
        }
    }
    return contracts;
}

auto resolve_image_paint_contracts(
    UClass* runtime_paintable_class,
    UClass* pawn_class,
    UClass* world_class)
    -> std::expected<
        ImagePaintContracts,
        application::RuntimeExecutionError>
{
    auto contracts = ImagePaintContracts{};
    contracts.runtime_paintable_class = runtime_paintable_class;
    contracts.pawn_class = pawn_class;
    contracts.world_class = world_class;
    contracts.mesh_component_class =
        find_class(MeshComponentClassPath);
    contracts.skinned_mesh_component_class =
        find_class(SkinnedMeshComponentClassPath);
    contracts.skinned_asset_class =
        find_class(SkinnedAssetClassPath);
    if (contracts.runtime_paintable_class == nullptr ||
        contracts.pawn_class == nullptr ||
        contracts.world_class == nullptr ||
        contracts.mesh_component_class == nullptr ||
        contracts.skinned_mesh_component_class == nullptr ||
        contracts.skinned_asset_class == nullptr)
    {
        return runtime_failure(
            application::RuntimeContractId::
                ImagePaintMeshProfile,
            application::ContractFailureKind::MissingObject);
    }
    contracts.target_mesh_component =
        find_weak_object_property(
            contracts.runtime_paintable_class,
            STR("TargetMeshComponent"));
    auto* target_mesh_property_class =
        contracts.target_mesh_component == nullptr
            ? nullptr
            : contracts.target_mesh_component
                  ->GetPropertyClass()
                  .Get();
    if (contracts.target_mesh_component == nullptr ||
        (target_mesh_property_class !=
             contracts.mesh_component_class &&
         target_mesh_property_class !=
             contracts.skinned_mesh_component_class))
    {
        return runtime_failure(
            application::RuntimeContractId::
                ImagePaintMeshProfile,
            application::ContractFailureKind::
                WrongPropertyKind);
    }
    contracts.skinned_asset = CastField<FObjectProperty>(
        contracts.skinned_mesh_component_class->FindProperty(
            FName{STR("SkinnedAsset"), FNAME_Find}));
    if (contracts.skinned_asset == nullptr ||
        contracts.skinned_asset->GetOwner<UClass>() !=
            contracts.skinned_mesh_component_class ||
        contracts.skinned_asset->GetArrayDim() != 1 ||
        contracts.skinned_asset->GetElementSize() !=
            static_cast<int>(sizeof(void*)) ||
        contracts.skinned_asset->GetPropertyClass().Get() !=
            contracts.skinned_asset_class ||
        !contracts.skinned_asset->IsInContainer(
            contracts.skinned_mesh_component_class))
    {
        return runtime_failure(
            application::RuntimeContractId::
                ImagePaintMeshProfile,
            application::ContractFailureKind::
                WrongPropertyKind);
    }
    return contracts;
}

auto outer_chain_contains(UObject* object, UObject* expected)
    -> bool
{
    auto depth = 0U;
    for (auto* current =
             object == nullptr
                 ? nullptr
                 : object->GetOuterPrivate();
         current != nullptr && depth < 16U;
         current = current->GetOuterPrivate(), ++depth)
    {
        if (current == expected)
        {
            return true;
        }
    }
    return false;
}

auto resolve_paint_brush_plane_visuals(
    UObject* world,
    const PaintSceneCaptureContracts& scene)
    -> std::expected<
        PaintBrushPlaneVisuals,
        application::RuntimeExecutionError>
{
    auto visuals = PaintBrushPlaneVisuals{};
    visuals.actor_class = find_class(BrushPlaneClassPath);
    auto* static_mesh_component_class =
        find_class(StaticMeshComponentClassPath);
    auto* niagara_component_class =
        find_class(NiagaraComponentClassPath);
    visuals.component_classes = {
        static_mesh_component_class,
        static_mesh_component_class,
        niagara_component_class,
    };
    if (world == nullptr ||
        visuals.actor_class == nullptr ||
        static_mesh_component_class == nullptr ||
        niagara_component_class == nullptr)
    {
        return runtime_failure(
            application::RuntimeContractId::PaintCapture,
            application::ContractFailureKind::MissingObject);
    }
    if (!visuals.actor_class->IsChildOf(scene.actor_class) ||
        !static_mesh_component_class->IsChildOf(
            scene.primitive_component_class) ||
        !niagara_component_class->IsChildOf(
            scene.primitive_component_class))
    {
        return runtime_failure(
            application::RuntimeContractId::PaintCapture,
            application::ContractFailureKind::WrongClass);
    }

    auto actor_count = std::size_t{};
    UObjectGlobals::ForEachUObject(
        [&](UObject* object, std::int32_t, std::int32_t)
            -> RC::LoopAction
        {
            if (object_is_live_exact(
                    object,
                    visuals.actor_class) &&
                object->GetWorld() == world)
            {
                visuals.actor = static_cast<AActor*>(object);
                ++actor_count;
            }
            return actor_count > 1U
                       ? RC::LoopAction::Break
                       : RC::LoopAction::Continue;
        });
    if (actor_count != 1U)
    {
        return runtime_failure(
            application::RuntimeContractId::PaintCapture,
            application::ContractFailureKind::MissingObject);
    }

    const auto cooked =
        paint_brush_plane_visual_contract();
    auto component_counts =
        std::array<std::size_t, 3U>{};
    UObjectGlobals::ForEachUObject(
        [&](UObject* object, std::int32_t, std::int32_t)
            -> RC::LoopAction
        {
            if (object == nullptr ||
                object->GetWorld() != world ||
                !outer_chain_contains(object, visuals.actor))
            {
                return RC::LoopAction::Continue;
            }
            const auto name = RC::to_string(object->GetName());
            for (auto index = std::size_t{};
                 index < cooked.components.size();
                 ++index)
            {
                if (name != cooked.components[index].name ||
                    !object_is_live_exact(
                        object,
                        visuals.component_classes[index]))
                {
                    continue;
                }
                visuals.components[index] = object;
                ++component_counts[index];
                break;
            }
            return RC::LoopAction::Continue;
        });
    if (std::ranges::any_of(
            component_counts,
            [](std::size_t count) { return count != 1U; }))
    {
        return runtime_failure(
            application::RuntimeContractId::PaintCapture,
            application::ContractFailureKind::MissingObject);
    }
    return visuals;
}

struct HideSceneCaptureComponentParametersAbi
{
    void* in_component{};
};

static_assert(
    sizeof(HideSceneCaptureComponentParametersAbi) == 0x08U);

class SceneCaptureActorGuard final
{
public:
    SceneCaptureActorGuard(
        AActor* actor,
        UFunction* destroy_function)
        : actor_{actor},
          destroy_function_{destroy_function}
    {
    }
    SceneCaptureActorGuard(const SceneCaptureActorGuard&) =
        delete;
    auto operator=(const SceneCaptureActorGuard&)
        -> SceneCaptureActorGuard& = delete;
    ~SceneCaptureActorGuard() noexcept
    {
        try
        {
            if (actor_ != nullptr &&
                destroy_function_ != nullptr &&
                FWeakObjectPtr{actor_}.Get() == actor_)
            {
                actor_->ProcessEvent(
                    destroy_function_,
                    nullptr);
            }
        }
        catch (...)
        {
        }
    }

private:
    AActor* actor_{};
    UFunction* destroy_function_{};
};

class PaintCaptureReadbackGuard final
{
public:
    explicit PaintCaptureReadbackGuard(
        PaintCaptureLinearColorArray& array)
        : array_{array}
    {
    }
    PaintCaptureReadbackGuard(
        const PaintCaptureReadbackGuard&) = delete;
    auto operator=(const PaintCaptureReadbackGuard&)
        -> PaintCaptureReadbackGuard& = delete;
    ~PaintCaptureReadbackGuard() noexcept
    {
        if (array_.data == nullptr)
        {
            return;
        }
        try
        {
            FMemory::Free(array_.data);
        }
        catch (...)
        {
        }
        array_ = {};
    }

private:
    PaintCaptureLinearColorArray& array_;
};

struct BorrowedScriptArrayAbi
{
    void* data{};
    std::int32_t count{};
    std::int32_t capacity{};
};

struct BorrowedScriptStringAbi
{
    TCHAR* data{};
    std::int32_t count{};
    std::int32_t capacity{};
};

static_assert(sizeof(BorrowedScriptArrayAbi) == 0x10U);
static_assert(sizeof(BorrowedScriptStringAbi) == 0x10U);

auto read_ascii_show_flag_name(
    FStrProperty* property,
    const void* container) -> std::optional<std::string>
{
    if (property == nullptr || container == nullptr)
    {
        return std::nullopt;
    }
    const auto& value =
        property->GetPropertyValueInContainer(container);
    const auto& characters = value.GetCharArray();
    const auto count = characters.Num();
    if (count <= 1 || count > 129 ||
        characters.Max() < count ||
        characters.Max() > 256 ||
        characters.GetData() == nullptr ||
        characters.GetData()[count - 1] != STR('\0'))
    {
        return std::nullopt;
    }
    auto output = std::string{};
    output.reserve(static_cast<std::size_t>(count - 1));
    for (auto index = 0; index < count - 1; ++index)
    {
        const auto character = characters.GetData()[index];
        if (character <= 0 || character > 0x7f)
        {
            return std::nullopt;
        }
        output.push_back(static_cast<char>(character));
    }
    return output;
}

auto apply_intrinsic_emission_show_flags(
    UObject* component,
    const PaintSceneCaptureContracts& contracts) -> bool
{
    if (!object_is_live_exact(
            component,
            contracts.scene_capture_component_2d_class) ||
        contracts.set_show_flag_settings == nullptr ||
        contracts.set_show_flag_settings_parameter == nullptr ||
        contracts.show_flag_settings == nullptr ||
        contracts.show_flag_name == nullptr ||
        contracts.show_flag_enabled == nullptr)
    {
        return false;
    }
    const auto& requested =
        paint_intrinsic_emission_show_flags();
    auto parameters = std::vector<std::byte>(
        static_cast<std::size_t>(
            contracts.set_show_flag_settings
                ->GetPropertiesSize()));
    auto* parameter_array =
        contracts.set_show_flag_settings_parameter
            ->ContainerPtrToValuePtr<void>(
                parameters.data());
    if (parameter_array == nullptr)
    {
        return false;
    }
    const auto element_size =
        contracts.show_flag_setting_struct
            ->GetPropertiesSize();
    if (element_size <= 0 ||
        requested.size() >
            std::numeric_limits<std::size_t>::max() /
                static_cast<std::size_t>(element_size))
    {
        return false;
    }
    auto records = std::vector<std::byte>(
        requested.size() *
        static_cast<std::size_t>(element_size));
    auto string_backing =
        std::vector<std::basic_string<TCHAR>>{};
    string_backing.reserve(requested.size());
    for (auto index = std::size_t{};
         index < requested.size();
         ++index)
    {
        auto* record =
            records.data() +
            index * static_cast<std::size_t>(
                        element_size);
        string_backing.push_back(
            RC::ensure_str(std::string{
                requested[index].name}));
        auto* string_value =
            contracts.show_flag_name
                ->ContainerPtrToValuePtr<void>(record);
        if (string_value == nullptr ||
            string_backing.back().empty() ||
            string_backing.back().size() + 1U >
                static_cast<std::size_t>(
                    std::numeric_limits<std::int32_t>::max()))
        {
            return false;
        }
        *static_cast<BorrowedScriptStringAbi*>(
            string_value) = BorrowedScriptStringAbi{
            string_backing.back().data(),
            static_cast<std::int32_t>(
                string_backing.back().size() + 1U),
            static_cast<std::int32_t>(
                string_backing.back().size() + 1U),
        };
        contracts.show_flag_enabled
            ->SetPropertyValueInContainer(
                record,
                requested[index].enabled);
        const auto written_name =
            read_ascii_show_flag_name(
                contracts.show_flag_name,
                record);
        if (!written_name ||
            *written_name != requested[index].name ||
            contracts.show_flag_enabled
                    ->GetPropertyValueInContainer(record) !=
                requested[index].enabled)
        {
            return false;
        }
    }
    *static_cast<BorrowedScriptArrayAbi*>(
        parameter_array) = BorrowedScriptArrayAbi{
        records.data(),
        static_cast<std::int32_t>(requested.size()),
        static_cast<std::int32_t>(requested.size()),
    };

    component->ProcessEvent(
        contracts.set_show_flag_settings,
        parameters.data());

    auto readback = FScriptArrayHelper_InContainer{
        contracts.show_flag_settings,
        component};
    if (readback.Num() !=
        static_cast<std::int32_t>(requested.size()))
    {
        return false;
    }
    for (auto index = std::size_t{};
         index < requested.size();
         ++index)
    {
        auto* record = readback.GetRawPtr(
            static_cast<std::int32_t>(index));
        const auto name = read_ascii_show_flag_name(
            contracts.show_flag_name,
            record);
        if (!name ||
            *name != requested[index].name ||
            contracts.show_flag_enabled
                    ->GetPropertyValueInContainer(record) !=
                requested[index].enabled)
        {
            return false;
        }
    }
    return true;
}

auto set_scene_capture_enum(
    const ExactEnumProperty& property,
    UObject* container,
    std::uint8_t value) -> bool
{
    if (property.property == nullptr ||
        property.underlying == nullptr ||
        container == nullptr)
    {
        return false;
    }
    auto* address =
        property.property->ContainerPtrToValuePtr<void>(
            container);
    if (address == nullptr)
    {
        return false;
    }
    property.underlying->SetIntPropertyValue(
        address,
        static_cast<uint64>(value));
    return property.underlying
               ->GetUnsignedIntPropertyValue(address) ==
           value;
}

auto set_scene_capture_object(
    FObjectPropertyBase* property,
    UObject* container,
    UObject* value) -> bool
{
    if (property == nullptr || container == nullptr ||
        value == nullptr)
    {
        return false;
    }
    auto* address =
        property->ContainerPtrToValuePtr<void>(container);
    if (address == nullptr)
    {
        return false;
    }
    property->SetObjectPropertyValue(address, value);
    return property->GetObjectPropertyValue(address) == value;
}

auto configure_scene_capture_component(
    UObject* component,
    UObject* render_target,
    const PaintSceneCaptureCamera& camera,
    const PaintSceneCapturePass& pass,
    const PaintSceneCaptureContracts& contracts) -> bool
{
    if (component == nullptr || render_target == nullptr ||
        !set_scene_capture_object(
            contracts.texture_target,
            component,
            render_target) ||
        !set_scene_capture_enum(
            contracts.capture_source,
            component,
            static_cast<std::uint8_t>(pass.source)) ||
        !set_scene_capture_enum(
            contracts.projection_type,
            component,
            static_cast<std::uint8_t>(
                PaintSceneCaptureProjection::Perspective)))
    {
        return false;
    }
    contracts.capture_every_frame->SetPropertyValueInContainer(
        component,
        false);
    contracts.capture_on_movement->SetPropertyValueInContainer(
        component,
        false);
    contracts.always_persist_rendering_state
        ->SetPropertyValueInContainer(component, true);
    contracts.field_of_view_angle->SetPropertyValueInContainer(
        component,
        camera.field_of_view_degrees);
    return
        !contracts.capture_every_frame
             ->GetPropertyValueInContainer(component) &&
        !contracts.capture_on_movement
             ->GetPropertyValueInContainer(component) &&
        contracts.always_persist_rendering_state
            ->GetPropertyValueInContainer(component) &&
        contracts.field_of_view_angle
                ->GetPropertyValueInContainer(component) ==
            camera.field_of_view_degrees;
}

auto capture_paint_scene_pass(
    UWorld* world,
    UObject* target_mesh,
    const PaintBrushPlaneVisuals& brush_plane,
    const PaintSceneCaptureCamera& camera,
    const PaintSceneCapturePass& pass,
    const PaintSceneCaptureContracts& contracts)
    -> std::expected<
        std::vector<PaintCaptureLinearColor>,
        application::RuntimeExecutionError>
{
    if (world == nullptr ||
        (pass.profile !=
             PaintSceneCaptureProfile::Standard &&
         pass.profile !=
             PaintSceneCaptureProfile::
                 IntrinsicEmission) ||
        (pass.subject !=
             PaintSceneCaptureSubject::BackgroundOnly &&
         pass.subject !=
             PaintSceneCaptureSubject::TargetVisible) ||
        !object_is_live(
            target_mesh,
            contracts.primitive_component_class) ||
        target_mesh->GetWorld() != world ||
        !object_is_live_exact(
            brush_plane.actor,
            brush_plane.actor_class) ||
        brush_plane.actor->GetWorld() != world)
    {
        return runtime_failure(
            application::RuntimeContractId::PaintCapture,
            application::ContractFailureKind::StaleObject);
    }
    const auto create_parameters =
        encode_create_paint_capture_render_target(
            PaintCaptureRenderTargetInput{
                world,
                camera.width,
                camera.height,
                pass.format,
            });
    if (!create_parameters)
    {
        return runtime_failure(
            application::RuntimeContractId::PaintCapture,
            application::ContractFailureKind::InvalidValue);
    }
    auto mutable_create_parameters = *create_parameters;
    contracts.kismet_rendering_library_cdo->ProcessEvent(
        contracts.create_render_target_2d,
        &mutable_create_parameters);
    auto* render_target = static_cast<UObject*>(
        mutable_create_parameters.return_value);
    if (!object_is_live(
            render_target,
            contracts.texture_render_target_2d_class))
    {
        return runtime_failure(
            application::RuntimeContractId::PaintCapture,
            application::ContractFailureKind::MissingObject);
    }
    render_target->SetRootSet();
    [[maybe_unused]] auto rooted_render_target =
        RootedObjectGuard{render_target};

    const auto location = FVector{
        camera.location.x,
        camera.location.y,
        camera.location.z,
    };
    const auto rotation = FRotator{
        camera.rotation.pitch,
        camera.rotation.yaw,
        camera.rotation.roll,
    };
    auto* actor = world->SpawnActor(
        contracts.scene_capture_2d_class,
        &location,
        &rotation);
    if (!object_is_live_exact(
            actor,
            contracts.scene_capture_2d_class) ||
        actor->GetWorld() != world)
    {
        return runtime_failure(
            application::RuntimeContractId::PaintCapture,
            application::ContractFailureKind::MissingObject);
    }
    [[maybe_unused]] auto actor_guard = SceneCaptureActorGuard{
        actor,
        contracts.destroy_actor,
    };
    auto* component = read_object(
        contracts.capture_component_2d,
        actor);
    if (!object_is_live_exact(
            component,
            contracts.scene_capture_component_2d_class) ||
        component->GetWorld() != world ||
        !outer_chain_contains(component, actor) ||
        !configure_scene_capture_component(
            component,
            render_target,
            camera,
            pass,
            contracts))
    {
        return runtime_failure(
            application::RuntimeContractId::PaintCapture,
            application::ContractFailureKind::InvalidValue);
    }
    if (pass.profile ==
            PaintSceneCaptureProfile::IntrinsicEmission &&
        !apply_intrinsic_emission_show_flags(
            component,
            contracts))
    {
        return runtime_failure(
            application::RuntimeContractId::PaintCapture,
            application::ContractFailureKind::
                InvalidValue);
    }

    auto hide_component = [&](UObject* hidden) -> bool
    {
        if (!object_is_live(
                hidden,
                contracts.primitive_component_class) ||
            hidden->GetWorld() != world)
        {
            return false;
        }
        auto parameters =
            HideSceneCaptureComponentParametersAbi{hidden};
        component->ProcessEvent(
            contracts.hide_component,
            &parameters);
        return true;
    };
    if (pass.subject ==
            PaintSceneCaptureSubject::BackgroundOnly &&
        !hide_component(target_mesh))
    {
        return runtime_failure(
            application::RuntimeContractId::PaintCapture,
            application::ContractFailureKind::StaleObject);
    }
    for (auto index = std::size_t{};
         index < brush_plane.components.size();
         ++index)
    {
        auto* hidden = brush_plane.components[index];
        if (!object_is_live_exact(
                hidden,
                brush_plane.component_classes[index]) ||
            !outer_chain_contains(hidden, brush_plane.actor) ||
            !hide_component(hidden))
        {
            return runtime_failure(
                application::RuntimeContractId::PaintCapture,
                application::ContractFailureKind::StaleObject);
        }
    }
    component->ProcessEvent(
        contracts.capture_scene,
        nullptr);

    const auto read_parameters =
        encode_read_paint_capture_render_target(
            world,
            render_target,
            pass.normalize_readback);
    if (!read_parameters)
    {
        return runtime_failure(
            application::RuntimeContractId::PaintCapture,
            application::ContractFailureKind::InvalidValue);
    }
    auto mutable_read_parameters = *read_parameters;
    contracts.kismet_rendering_library_cdo->ProcessEvent(
        contracts.read_render_target_raw,
        &mutable_read_parameters);
    [[maybe_unused]] auto readback_guard =
        PaintCaptureReadbackGuard{
        mutable_read_parameters.out_linear_samples};
    const auto decoded = decode_paint_capture_linear_colors(
        mutable_read_parameters,
        camera.width,
        camera.height);
    if (!decoded)
    {
        return runtime_failure(
            application::RuntimeContractId::PaintCapture,
            application::ContractFailureKind::InvalidValue);
    }
    return *decoded;
}

auto ensure_paint_mesh_initialized(
    UObject* component,
    UObject* mesh,
    const PaintContracts& contracts) -> bool
{
    if (!object_is_live(
            component,
            contracts.runtime_paintable_class) ||
        !object_is_live(
            mesh,
            contracts.mesh_component_class))
    {
        return false;
    }
    auto initialized = IsPaintInitializedParameters{};
    component->ProcessEvent(
        contracts.is_initialized,
        &initialized);
    if (!initialized.return_value)
    {
        const auto encoded = encode_initialize_paint(mesh);
        if (!encoded)
        {
            return false;
        }
        auto parameters = *encoded;
        component->ProcessEvent(
            contracts.initialize_paint,
            &parameters);
        if (!parameters.return_value)
        {
            return false;
        }
        initialized = {};
        component->ProcessEvent(
            contracts.is_initialized,
            &initialized);
    }
    auto initialized_mesh =
        GetInitializedPaintMeshParameters{};
    component->ProcessEvent(
        contracts.get_initialized_paint_mesh,
        &initialized_mesh);
    return initialized.return_value &&
           initialized_mesh.return_value == mesh;
}

auto capture_paint_bone_transforms(
    UObject* mesh,
    const core::PaintSamplingProfile& profile,
    const PaintContracts& contracts)
    -> std::optional<
        std::vector<core::PaintReferenceBoneTransform>>
{
    if (!profile.bones ||
        profile.bones->empty() ||
        profile.bones->size() !=
            profile.identity.bone_count)
    {
        return std::nullopt;
    }
    auto transforms =
        std::vector<core::PaintReferenceBoneTransform>{};
    transforms.reserve(profile.bones->size());
    for (const auto& bone : *profile.bones)
    {
        const auto wide_name = RC::to_wstring(bone.name);
        const auto unreal_name =
            FName{wide_name.c_str(), FNAME_Find};
        if (unreal_name.IsNone())
        {
            return std::nullopt;
        }
        auto parameters = GetSocketTransformParameters{};
        parameters.socket_name = RuntimeName{
            unreal_name.GetComparisonIndex().ToUnstableInt(),
            static_cast<std::uint32_t>(
                unreal_name.GetNumber()),
        };
        parameters.transform_space =
            RuntimeRelativeTransformSpace::World;
        mesh->ProcessEvent(
            contracts.get_socket_transform,
            &parameters);
        const auto decoded =
            decode_runtime_transform(
                parameters.return_value);
        if (!decoded)
        {
            return std::nullopt;
        }
        transforms.push_back(*decoded);
    }
    return transforms;
}

auto capture_esp_skeleton_pose(
    UObject* avatar,
    UObject* world,
    const EspContracts& contracts,
    const application::ImagePaintProfileCatalog& profiles)
    -> std::optional<core::EspSkeletonPose>
{
    auto* mesh = read_object(
        contracts.character_mesh,
        avatar);
    if (!object_is_live(
            mesh,
            contracts.skeletal_mesh_component_class) ||
        mesh->GetWorld() != world ||
        !outer_chain_contains(mesh, avatar))
    {
        return std::nullopt;
    }
    auto* asset = read_object(
        contracts.skinned_asset,
        mesh);
    if (!object_is_live(
            asset,
            contracts.skinned_asset_class))
    {
        return std::nullopt;
    }
    const auto pair = profiles.find_by_unreal_asset_path(
        RC::to_string(asset->GetPathName()));
    if (!pair ||
        RC::to_string(asset->GetName()) !=
            pair->sampling.identity.export_name ||
        !pair->sampling.bones ||
        !pair->image.geometry.bones ||
        pair->sampling.bones->size() !=
            pair->sampling.identity.bone_count ||
        pair->image.geometry.bones->size() !=
            pair->sampling.bones->size())
    {
        return std::nullopt;
    }
    auto positions = std::vector<EspVector3dAbi>{};
    positions.reserve(pair->sampling.bones->size());
    for (const auto& bone : *pair->sampling.bones)
    {
        const auto position = call_esp_socket_location(
            mesh,
            contracts.get_socket_location,
            bone.name);
        if (!position)
        {
            return std::nullopt;
        }
        positions.push_back(*position);
    }
    auto pose = build_esp_skeleton_pose(
        *pair->sampling.bones,
        positions);
    if (!pose ||
        !validate_esp_skeleton_topology(
            *pose,
            *pair->image.geometry.bones))
    {
        return std::nullopt;
    }
    return std::move(*pose);
}

auto resolve_owned_paint_component(
    const ActiveFrame& frame,
    const PaintContracts& contracts)
    -> std::optional<std::pair<UObject*, UObject*>>
{
    if (!object_is_live(
            frame.controller,
            contracts.player_controller_class))
    {
        return std::nullopt;
    }
    auto* pawn = read_object(
        contracts.acknowledged_pawn,
        frame.controller);
    if (!object_is_live(pawn, contracts.pawn_class))
    {
        return std::nullopt;
    }
    auto* component_property = find_object_property(
        pawn->GetClassPrivate(),
        STR("RuntimePaintable"),
        contracts.runtime_paintable_class);
    if (component_property == nullptr)
    {
        return std::nullopt;
    }
    auto* component = read_object(component_property, pawn);
    if (!object_is_live(
            component,
            contracts.runtime_paintable_class) ||
        !outer_chain_contains(component, pawn))
    {
        return std::nullopt;
    }
    return std::pair{pawn, component};
}

auto resolve_unique_replication_manager(
    UObject* world,
    const PaintContracts& contracts) -> UObject*
{
    if (world == nullptr ||
        contracts.replication_manager_class == nullptr)
    {
        return nullptr;
    }
    auto* match = static_cast<UObject*>(nullptr);
    auto count = std::size_t{};
    UObjectGlobals::ForEachUObject(
        [&](UObject* object, std::int32_t, std::int32_t)
            -> RC::LoopAction
        {
            if (!object_is_live_exact(
                    object,
                    contracts.replication_manager_class) ||
                object->GetWorld() != world)
            {
                return RC::LoopAction::Continue;
            }
            match = object;
            ++count;
            return count > 1U
                       ? RC::LoopAction::Break
                       : RC::LoopAction::Continue;
        });
    return count == 1U ? match : nullptr;
}

auto query_input_ignored(UObject* controller, UFunction* function)
    -> bool
{
    auto parameters = IgnoreInputQueryParametersAbi{};
    controller->ProcessEvent(function, &parameters);
    return parameters.return_value;
}

auto set_input_ignored(
    UObject* controller,
    UFunction* function,
    bool ignored) -> void
{
    auto parameters = encode_ignore_input(ignored);
    controller->ProcessEvent(function, &parameters);
}

struct PreparedPaintCaptureSeed
{
    core::PaintSettings settings{};
    PaintSceneCapturePlan pass_plan{};
    PaintContracts paint{};
    PaintSceneCaptureContracts scene{};
    ImagePaintContracts image{};
    BoundFrame bound{};
    ActiveFrame active{};
    UObject* world{};
    UObject* pawn{};
    UObject* component{};
    UObject* mesh{};
    std::shared_ptr<
        const application::ImagePaintProfilePair>
        profile{};
    std::vector<core::PaintReferenceBoneTransform>
        bone_transforms{};
    core::EspView view{};
    core::EspViewport capture_viewport{};
    core::PaintAppearanceCameraFingerprint camera_fingerprint{};
    PaintSceneCaptureCamera camera{};
    PaintBrushPlaneVisuals brush_plane{};
};

enum class AutomaticPaintCaptureStage : std::uint8_t
{
    GeometryPending,
    SourceCapture,
    PreliminaryModelPending,
    SnapshotPending,
    CandidatePending,
    PreviewApplyPending,
    PreviewSettle,
    FeedbackCapture,
    RestorePending,
    TargetE0Pending,
    FinalModelPending,
    EvaluationPending,
    FinalResolvePending,
    FinalResolved,
    Failed,
};

enum class AutomaticPaintCandidatePurpose : std::uint8_t
{
    TargetE0,
    FitBaseline,
    FitTrial,
};

struct AutomaticPaintCaptureSession
{
    application::JobGeneration generation{};
    PreparedPaintCaptureSeed seed{};
    AutomaticPaintCaptureStage stage{
        AutomaticPaintCaptureStage::GeometryPending};
    std::size_t next_source_pass{};
    core::PaintAppearanceCaptureEvidence evidence{};
    std::shared_ptr<const std::vector<
        core::PaintCaptureGeometrySample>>
        geometry{};
    std::shared_ptr<const core::PaintAppearanceModel> model{};
    std::vector<double> parameters{};
    AutomaticPaintCandidatePurpose candidate_purpose{
        AutomaticPaintCandidatePurpose::TargetE0};
    std::optional<application::PaintPreviewSnapshot>
        preview_snapshot{};
    std::shared_ptr<const application::PaintTextureImage>
        candidate_preview{};
    std::shared_ptr<const std::vector<
        core::PaintAppearanceReadbackReference>>
        readback_references{};
    core::PaintAppearanceFeedbackEvidence feedback_evidence{};
    core::PaintAppearanceCapturedPass<core::AppearanceRgb>
        target_intrinsic_e0{};
    std::size_t next_feedback_pass{};
    std::uint64_t preview_applied_ms{};
    bool preview_mutated{};
    std::optional<core::PaintAppearanceFeedback> feedback{};
    std::optional<core::PaintAppearanceTargetE0> target_e0{};
    std::optional<core::PaintAppearanceFitSession> fit{};
    std::optional<core::PaintAppearanceFitResult> fit_result{};
    std::shared_ptr<const std::vector<
        core::ResolvedPaintAppearance>>
        resolved_appearances{};
};
} // namespace

class UnrealRuntimeAdapter::Impl
{
public:
    explicit Impl(
        std::shared_ptr<
            product_ui::ProductUiInputQueue> input_queue,
        std::shared_ptr<
            const application::ImagePaintProfileCatalog>
            image_paint_profiles)
        : input_queue_{std::move(input_queue)},
          image_paint_profiles_{
              std::move(image_paint_profiles)}
    {
    }

    ~Impl()
    {
        detach_noexcept();
    }

    auto attach(void* context, application::HudCallback callback)
        -> std::expected<
            application::CallbackId,
            application::CallbackPortError>
    {
        if (context == nullptr || callback == nullptr)
        {
            return std::unexpected(
                application::CallbackPortError::Registration);
        }

        try
        {
            const auto resolved = resolve_hud_contracts();
            if (!resolved)
            {
                return std::unexpected(
                    application::CallbackPortError::Registration);
            }

            {
                const auto lock = std::scoped_lock{mutex_};
                if (hook_ids_ || detaching_)
                {
                    return std::unexpected(
                        application::CallbackPortError::
                            Registration);
                }
                hud_contracts_ = *resolved;
                callback_context_ = context;
                callback_ = callback;
            }

            const auto ids = UObjectGlobals::RegisterHook(
                resolved->receive_draw_hud,
                &Impl::pre_hook,
                &Impl::post_hook,
                this);
            {
                const auto lock = std::scoped_lock{mutex_};
                hook_ids_ = ids;
            }
            return HudCallbackId;
        }
        catch (...)
        {
            const auto lock = std::scoped_lock{mutex_};
            clear_state_locked();
            return std::unexpected(
                application::CallbackPortError::Registration);
        }
    }

    auto detach(application::CallbackId id)
        -> std::expected<
            void,
            application::CallbackPortError>
    {
        auto function = static_cast<UFunction*>(nullptr);
        auto ids = std::pair<int, int>{};
        {
            const auto lock = std::scoped_lock{mutex_};
            if (id != HudCallbackId || !hook_ids_ ||
                !hud_contracts_ || detaching_ ||
                !canvas_textures_.empty() ||
                input_mutation_ || automatic_paint_capture_)
            {
                return std::unexpected(
                    application::CallbackPortError::
                        Unregistration);
            }
            detaching_ = true;
            function = hud_contracts_->receive_draw_hud;
            ids = *hook_ids_;
        }

        try
        {
            UObjectGlobals::UnregisterHook(function, ids);
        }
        catch (...)
        {
            const auto lock = std::scoped_lock{mutex_};
            detaching_ = false;
            return std::unexpected(
                application::CallbackPortError::Unregistration);
        }

        auto lock = std::unique_lock{mutex_};
        idle_.wait(lock, [this] { return in_flight_ == 0U; });
        clear_state_locked();
        return {};
    }

    auto resolve_contracts()
        -> std::expected<
            void,
            application::RuntimeExecutionError>
    {
        if (!IsInGameThreadRaw())
        {
            return std::unexpected(
                application::RuntimeExecutionError{
                    application::RuntimeExecutionErrorCode::
                        WrongThread,
                    std::nullopt,
                });
        }
        try
        {
            auto hud = std::optional<HudContracts>{};
            {
                const auto lock = std::scoped_lock{mutex_};
                hud = hud_contracts_;
            }
            if (!hud)
            {
                return runtime_failure(
                    application::RuntimeContractId::
                        RuntimeInitialization,
                    application::ContractFailureKind::
                        MissingObject);
            }
            auto canvas = resolve_canvas_contracts(
                hud->canvas_class);
            if (!canvas)
            {
                return std::unexpected(canvas.error());
            }
            auto input = resolve_input_contracts(
                hud->player_controller_class);
            if (!input)
            {
                return std::unexpected(input.error());
            }
            auto resolved = resolve_paint_contracts(
                hud->player_controller_class);
            if (!resolved)
            {
                return std::unexpected(resolved.error());
            }
            auto paint_scene_capture =
                resolve_paint_scene_capture_contracts(
                    canvas->kismet_rendering_library_class,
                    canvas->kismet_rendering_library_cdo);
            if (!paint_scene_capture)
            {
                return std::unexpected(
                    paint_scene_capture.error());
            }
            auto image_paint = resolve_image_paint_contracts(
                resolved->runtime_paintable_class,
                resolved->pawn_class,
                hud->world_class);
            if (!image_paint)
            {
                return std::unexpected(image_paint.error());
            }
            if (!image_paint_profiles_ ||
                image_paint_profiles_->size() !=
                    application::ImagePaintProfilePairCount)
            {
                return runtime_failure(
                    application::RuntimeContractId::
                        ImagePaintMeshProfile,
                    application::ContractFailureKind::
                        MissingObject);
            }
            const auto lock = std::scoped_lock{mutex_};
            if (detaching_)
            {
                return runtime_failure(
                    application::RuntimeContractId::
                        RuntimeInitialization,
                    application::ContractFailureKind::
                        ExecutionFailure);
            }
            canvas_contracts_ = *canvas;
            input_contracts_ = *input;
            paint_contracts_ = *resolved;
            paint_scene_capture_contracts_ =
                *paint_scene_capture;
            image_paint_contracts_ = *image_paint;
            return {};
        }
        catch (...)
        {
            return runtime_failure(
                application::RuntimeContractId::
                    RuntimeInitialization,
                application::ContractFailureKind::
                    ExecutionFailure);
        }
    }

    auto input_target()
        -> std::expected<InputTarget, ui::InputPortError>
    {
        if (!IsInGameThreadRaw())
        {
            return input_failure(
                "Input control requires the game thread.");
        }
        auto active = std::optional<ActiveFrame>{};
        auto contracts = std::optional<InputContracts>{};
        {
            const auto lock = std::scoped_lock{mutex_};
            if (detaching_)
            {
                return input_failure(
                    "Input control is detaching.");
            }
            active = active_frame_;
            contracts = input_contracts_;
        }
        if (!active || !contracts ||
            !object_is_live(
                active->controller,
                contracts->player_controller_class))
        {
            return input_failure(
                "The active PlayerController is unavailable.");
        }
        const auto identity = object_identity(active->controller);
        if (identity == 0U ||
            identity != active->identity.controller)
        {
            return input_failure(
                "The active PlayerController identity is stale.");
        }
        return InputTarget{
            *contracts,
            active->controller,
            identity,
        };
    }

    auto capture_input()
        -> std::expected<
            ui::RuntimeInputState,
            ui::InputPortError>
    {
        try
        {
            const auto target = input_target();
            if (!target)
            {
                return std::unexpected(target.error());
            }
            {
                const auto lock = std::scoped_lock{mutex_};
                if (input_mutation_)
                {
                    return input_failure(
                        "An input lease is already captured.");
                }
            }

            const auto captured = ui::RuntimeInputState{
                target->identity,
                target->contracts.show_mouse_cursor
                    ->GetPropertyValueInContainer(
                        target->controller),
                query_input_ignored(
                    target->controller,
                    target->contracts.is_look_input_ignored),
                query_input_ignored(
                    target->controller,
                    target->contracts.is_move_input_ignored),
                ui::RuntimeInputModeHandling::
                    PreserveUnchanged,
            };
            const auto lock = std::scoped_lock{mutex_};
            if (detaching_ || input_mutation_ ||
                !active_frame_ ||
                active_frame_->identity.controller !=
                    captured.owner_identity)
            {
                return input_failure(
                    "The input owner changed during capture.");
            }
            input_mutation_ = InputMutationLease{
                FWeakObjectPtr{target->controller},
                captured,
            };
            return captured;
        }
        catch (...)
        {
            return input_failure(
                "Input capture failed.");
        }
    }

    auto apply_input()
        -> std::expected<void, ui::InputPortError>
    {
        try
        {
            const auto target = input_target();
            if (!target)
            {
                return std::unexpected(target.error());
            }
            auto lease = std::optional<InputMutationLease>{};
            {
                const auto lock = std::scoped_lock{mutex_};
                lease = input_mutation_;
            }
            if (!lease ||
                lease->captured.owner_identity !=
                    target->identity ||
                lease->controller.Get() != target->controller ||
                lease->cursor_changed ||
                lease->look_changed ||
                lease->movement_changed)
            {
                return input_failure(
                    "The captured input lease is unavailable.");
            }

            if (!lease->captured.cursor_visible)
            {
                {
                    const auto lock = std::scoped_lock{mutex_};
                    input_mutation_->cursor_changed = true;
                }
                target->contracts.show_mouse_cursor
                    ->SetPropertyValueInContainer(
                        target->controller,
                        true);
                if (!target->contracts.show_mouse_cursor
                         ->GetPropertyValueInContainer(
                             target->controller))
                {
                    return input_failure(
                        "The mouse cursor state did not apply.");
                }
            }
            if (!lease->captured.look_input_ignored)
            {
                {
                    const auto lock = std::scoped_lock{mutex_};
                    input_mutation_->look_changed = true;
                }
                set_input_ignored(
                    target->controller,
                    target->contracts.set_ignore_look_input,
                    true);
                if (!query_input_ignored(
                        target->controller,
                        target->contracts
                            .is_look_input_ignored))
                {
                    return input_failure(
                        "Look-input suspension did not apply.");
                }
            }
            if (!lease->captured.movement_input_ignored)
            {
                {
                    const auto lock = std::scoped_lock{mutex_};
                    input_mutation_->movement_changed = true;
                }
                set_input_ignored(
                    target->controller,
                    target->contracts.set_ignore_move_input,
                    true);
                if (!query_input_ignored(
                        target->controller,
                        target->contracts
                            .is_move_input_ignored))
                {
                    return input_failure(
                        "Movement-input suspension did not apply.");
                }
            }
            return {};
        }
        catch (...)
        {
            return input_failure(
                "Input apply failed.");
        }
    }

    auto current_input_owner()
        -> std::expected<std::uint64_t, ui::InputPortError>
    {
        try
        {
            const auto target = input_target();
            if (!target)
            {
                return std::unexpected(target.error());
            }
            return target->identity;
        }
        catch (...)
        {
            return input_failure(
                "Input owner validation failed.");
        }
    }

    auto restore_input(
        const ui::RuntimeInputState& state)
        -> std::expected<void, ui::InputPortError>
    {
        if (!IsInGameThreadRaw())
        {
            return input_failure(
                "Input restoration requires the game thread.");
        }
        try
        {
            auto contracts = std::optional<InputContracts>{};
            auto lease = std::optional<InputMutationLease>{};
            {
                const auto lock = std::scoped_lock{mutex_};
                contracts = input_contracts_;
                lease = input_mutation_;
            }
            if (!contracts || !lease ||
                state != lease->captured ||
                state.owner_identity == 0U ||
                state.input_mode !=
                    ui::RuntimeInputModeHandling::
                        PreserveUnchanged)
            {
                return input_failure(
                    "The captured input state does not match.");
            }
            auto* controller = lease->controller.Get();
            if (!object_is_live(
                    controller,
                    contracts->player_controller_class) ||
                object_identity(controller) !=
                    state.owner_identity)
            {
                return input_failure(
                    "The captured PlayerController is stale.");
            }

            if (lease->movement_changed)
            {
                set_input_ignored(
                    controller,
                    contracts->set_ignore_move_input,
                    state.movement_input_ignored);
                if (query_input_ignored(
                        controller,
                        contracts->is_move_input_ignored) !=
                    state.movement_input_ignored)
                {
                    return input_failure(
                        "Movement-input suspension did not restore.");
                }
                const auto lock = std::scoped_lock{mutex_};
                input_mutation_->movement_changed = false;
            }
            if (lease->look_changed)
            {
                set_input_ignored(
                    controller,
                    contracts->set_ignore_look_input,
                    state.look_input_ignored);
                if (query_input_ignored(
                        controller,
                        contracts->is_look_input_ignored) !=
                    state.look_input_ignored)
                {
                    return input_failure(
                        "Look-input suspension did not restore.");
                }
                const auto lock = std::scoped_lock{mutex_};
                input_mutation_->look_changed = false;
            }
            if (lease->cursor_changed)
            {
                contracts->show_mouse_cursor
                    ->SetPropertyValueInContainer(
                        controller,
                        state.cursor_visible);
                if (contracts->show_mouse_cursor
                        ->GetPropertyValueInContainer(
                            controller) !=
                    state.cursor_visible)
                {
                    return input_failure(
                        "The mouse cursor state did not restore.");
                }
                const auto lock = std::scoped_lock{mutex_};
                input_mutation_->cursor_changed = false;
            }
            {
                const auto lock = std::scoped_lock{mutex_};
                if (!input_mutation_->cursor_changed &&
                    !input_mutation_->look_changed &&
                    !input_mutation_->movement_changed)
                {
                    input_mutation_.reset();
                }
            }
            return {};
        }
        catch (...)
        {
            return input_failure(
                "Input restore failed.");
        }
    }

    auto bind_frame(
        const application::HudFrameIdentity& identity)
        -> std::expected<
            void,
            application::RuntimeExecutionError>
    {
        if (!IsInGameThreadRaw())
        {
            return std::unexpected(
                application::RuntimeExecutionError{
                    application::RuntimeExecutionErrorCode::
                        WrongThread,
                    std::nullopt,
                });
        }
        try
        {
            auto active = std::optional<ActiveFrame>{};
            auto paint = std::optional<PaintContracts>{};
            auto previous = std::optional<BoundFrame>{};
            {
                const auto lock = std::scoped_lock{mutex_};
                active = active_frame_;
                paint = paint_contracts_;
                previous = bound_frame_;
            }
            if (!active || active->identity != identity)
            {
                return runtime_failure(
                    application::RuntimeContractId::Hud,
                    application::ContractFailureKind::
                        StaleObject);
            }
            if (!paint)
            {
                return runtime_failure(
                    application::RuntimeContractId::
                        PaintAtUvWithBrush,
                    application::ContractFailureKind::
                        MissingObject);
            }

            auto* pawn = static_cast<UObject*>(nullptr);
            auto* component = static_cast<UObject*>(nullptr);
            auto* replication_manager =
                resolve_unique_replication_manager(
                    active->world,
                    *paint);
            if (replication_manager == nullptr)
            {
                return runtime_failure(
                    application::RuntimeContractId::
                        PaintQueueObservation,
                    application::ContractFailureKind::
                        MissingObject);
            }
            if (const auto owned =
                    resolve_owned_paint_component(*active, *paint))
            {
                pawn = owned->first;
                component = owned->second;
            }
            else if (
                previous &&
                previous->identity.world == identity.world &&
                previous->identity.controller ==
                    identity.controller)
            {
                pawn = previous->pawn.Get();
                component = previous->component.Get();
                if (!object_is_live(pawn, paint->pawn_class) ||
                    !object_is_live(
                        component,
                        paint->runtime_paintable_class) ||
                    !outer_chain_contains(component, pawn))
                {
                    pawn = nullptr;
                    component = nullptr;
                }
            }
            if (pawn == nullptr || component == nullptr)
            {
                return runtime_failure(
                    application::RuntimeContractId::
                        PaintAtUvWithBrush,
                    application::ContractFailureKind::
                        MissingObject);
            }

            const auto component_id = object_identity(component);
            auto generation = std::uint64_t{1U};
            if (previous)
            {
                if (previous->component_identity == component_id &&
                    previous->component.Get() == component)
                {
                    generation = previous->component_generation;
                }
                else
                {
                    if (previous->component_generation ==
                        std::numeric_limits<std::uint64_t>::max())
                    {
                        return runtime_failure(
                            application::RuntimeContractId::
                                PaintAtUvWithBrush,
                            application::ContractFailureKind::
                                ExecutionFailure);
                    }
                    generation =
                        previous->component_generation + 1U;
                }
            }

            const auto lock = std::scoped_lock{mutex_};
            if (!active_frame_ ||
                active_frame_->identity != identity ||
                detaching_)
            {
                return runtime_failure(
                    application::RuntimeContractId::Hud,
                    application::ContractFailureKind::
                        StaleObject);
            }
            bound_frame_ = BoundFrame{
                identity,
                FWeakObjectPtr{active->world},
                FWeakObjectPtr{active->controller},
                FWeakObjectPtr{active->hud},
                FWeakObjectPtr{active->canvas},
                FWeakObjectPtr{pawn},
                FWeakObjectPtr{component},
                FWeakObjectPtr{replication_manager},
                component_id,
                generation,
            };
            return {};
        }
        catch (...)
        {
            return runtime_failure(
                application::RuntimeContractId::Hud,
                application::ContractFailureKind::
                    ExecutionFailure);
        }
    }

    auto paint(const application::PaintAtUvWithBrush& request)
        -> std::expected<
            void,
            application::RuntimeExecutionError>
    {
        if (!IsInGameThreadRaw())
        {
            return std::unexpected(
                application::RuntimeExecutionError{
                    application::RuntimeExecutionErrorCode::
                        WrongThread,
                    std::nullopt,
                });
        }
        try
        {
            auto contracts = std::optional<PaintContracts>{};
            auto bound = std::optional<BoundFrame>{};
            auto active = std::optional<ActiveFrame>{};
            {
                const auto lock = std::scoped_lock{mutex_};
                contracts = paint_contracts_;
                bound = bound_frame_;
                active = active_frame_;
            }
            if (!contracts || !bound || !active ||
                bound->identity != active->identity)
            {
                return runtime_failure(
                    application::RuntimeContractId::
                        PaintAtUvWithBrush,
                    application::ContractFailureKind::
                        StaleObject);
            }
            auto* component = bound->component.Get();
            if (!object_is_live(
                    component,
                    contracts->runtime_paintable_class) ||
                request.component.identity !=
                    bound->component_identity ||
                request.component.generation !=
                    bound->component_generation)
            {
                return runtime_failure(
                    application::RuntimeContractId::
                        PaintAtUvWithBrush,
                    application::ContractFailureKind::
                        StaleObject);
            }
            const auto parameters = encode_paint_call(request);
            if (!parameters)
            {
                return runtime_failure(
                    application::RuntimeContractId::
                        PaintAtUvWithBrush,
                    application::ContractFailureKind::
                        InvalidValue);
            }
            auto mutable_parameters = *parameters;
            component->ProcessEvent(
                contracts->paint_at_uv_with_brush,
                &mutable_parameters);
            return {};
        }
        catch (...)
        {
            return runtime_failure(
                application::RuntimeContractId::
                    PaintAtUvWithBrush,
                application::ContractFailureKind::
                    ExecutionFailure);
        }
    }

    auto restore_transient_state(std::uint64_t generation)
        -> std::expected<
            void,
            application::RuntimeExecutionError>
    {
        if (!IsInGameThreadRaw())
        {
            return std::unexpected(
                application::RuntimeExecutionError{
                    application::RuntimeExecutionErrorCode::
                        WrongThread,
                    std::nullopt,
                });
        }
        if (generation == 0U)
        {
            return runtime_failure(
                application::RuntimeContractId::InputControl,
                application::ContractFailureKind::InvalidValue);
        }
        try
        {
            restore_input_noexcept();
            if (!release_all_canvas_textures_noexcept())
            {
                return runtime_failure(
                    application::RuntimeContractId::
                        TextureMutation,
                    application::ContractFailureKind::
                        ExecutionFailure);
            }
            const auto lock = std::scoped_lock{mutex_};
            if (input_mutation_ || !canvas_textures_.empty())
            {
                return runtime_failure(
                    application::RuntimeContractId::
                        InputControl,
                    application::ContractFailureKind::
                        ExecutionFailure);
            }
            return {};
        }
        catch (...)
        {
            return runtime_failure(
                application::RuntimeContractId::InputControl,
                application::ContractFailureKind::
                    ExecutionFailure);
        }
    }

    auto observe_queues(
        application::RuntimeObjectHandle component_handle,
        application::JobGeneration generation)
        -> std::expected<
            application::PaintQueueObservation,
            application::RuntimeExecutionError>
    {
        if (!IsInGameThreadRaw())
        {
            return std::unexpected(
                application::RuntimeExecutionError{
                    application::RuntimeExecutionErrorCode::
                        WrongThread,
                    std::nullopt,
                });
        }
        try
        {
            auto contracts = std::optional<PaintContracts>{};
            auto bound = std::optional<BoundFrame>{};
            auto active = std::optional<ActiveFrame>{};
            {
                const auto lock = std::scoped_lock{mutex_};
                contracts = paint_contracts_;
                bound = bound_frame_;
                active = active_frame_;
            }
            if (!contracts || !bound || !active ||
                bound->identity != active->identity ||
                generation == 0U ||
                component_handle.identity !=
                    bound->component_identity ||
                component_handle.generation !=
                    bound->component_generation)
            {
                return runtime_failure(
                    application::RuntimeContractId::
                        PaintQueueObservation,
                    application::ContractFailureKind::
                        StaleObject);
            }

            auto* component = bound->component.Get();
            auto* manager = bound->replication_manager.Get();
            if (!object_is_live(
                    component,
                    contracts->runtime_paintable_class) ||
                !object_is_live_exact(
                    manager,
                    contracts->replication_manager_class) ||
                manager->GetWorld() != active->world)
            {
                return runtime_failure(
                    application::RuntimeContractId::
                        PaintQueueObservation,
                    application::ContractFailureKind::
                        StaleObject);
            }

            auto recorded = RecordedStrokeCountParamsAbi{};
            component->ProcessEvent(
                contracts->get_recorded_stroke_count,
                &recorded);

            auto component_count =
                QueuedStrokeCountForComponentParamsAbi{};
            component_count.paint_component = component;
            manager->ProcessEvent(
                contracts->get_queued_stroke_count_for_component,
                &component_count);

            auto manager_count = QueuedStrokeCountParamsAbi{};
            manager->ProcessEvent(
                contracts->get_queued_stroke_count,
                &manager_count);

            auto pressure = ReplicationPressureParamsAbi{};
            manager->ProcessEvent(
                contracts->get_replication_pressure,
                &pressure);

            const auto lock = std::scoped_lock{mutex_};
            if (detaching_ || !bound_frame_ ||
                bound_frame_->identity != bound->identity ||
                bound_frame_->component_identity !=
                    component_handle.identity ||
                bound_frame_->component_generation !=
                    component_handle.generation)
            {
                return runtime_failure(
                    application::RuntimeContractId::
                        PaintQueueObservation,
                    application::ContractFailureKind::
                        StaleObject);
            }
            const auto observed = queue_tracker_.observe(
                component_handle,
                generation,
                PaintQueueCounters{
                    recorded.return_value,
                    component_count.return_value,
                    manager_count.return_value,
                    pressure.return_value,
                });
            if (!observed)
            {
                return runtime_failure(
                    application::RuntimeContractId::
                        PaintQueueObservation,
                    application::ContractFailureKind::
                        InvalidValue);
            }
            return *observed;
        }
        catch (...)
        {
            return runtime_failure(
                application::RuntimeContractId::
                    PaintQueueObservation,
                application::ContractFailureKind::
                    ExecutionFailure);
        }
    }

    auto prepare_paint_capture_seed(
        const core::PaintSettings& settings,
        bool requires_preview_feedback)
        -> std::expected<
            PreparedPaintCaptureSeed,
            application::RuntimeExecutionError>
    {
        const auto pass_plan =
            build_paint_scene_capture_plan(settings);
        if (!pass_plan ||
            pass_plan->requires_preview_feedback !=
                requires_preview_feedback)
        {
            return runtime_failure(
                application::RuntimeContractId::PaintCapture,
                application::ContractFailureKind::InvalidValue);
        }

        auto paint = std::optional<PaintContracts>{};
        auto scene =
            std::optional<PaintSceneCaptureContracts>{};
        auto image = std::optional<ImagePaintContracts>{};
        auto bound = std::optional<BoundFrame>{};
        auto active = std::optional<ActiveFrame>{};
        {
            const auto lock = std::scoped_lock{mutex_};
            if (detaching_)
            {
                return runtime_failure(
                    application::RuntimeContractId::PaintCapture,
                    application::ContractFailureKind::StaleObject);
            }
            paint = paint_contracts_;
            scene = paint_scene_capture_contracts_;
            image = image_paint_contracts_;
            bound = bound_frame_;
            active = active_frame_;
        }
        if (!paint || !scene || !image || !bound || !active ||
            bound->identity != active->identity ||
            !bound->identity.valid() ||
            active->viewport_width <= 0 ||
            active->viewport_height <= 0)
        {
            return runtime_failure(
                application::RuntimeContractId::PaintCapture,
                application::ContractFailureKind::MissingObject);
        }
        auto* world = bound->world.Get();
        auto* pawn = bound->pawn.Get();
        auto* component = bound->component.Get();
        if (!object_is_live(world, image->world_class) ||
            !object_is_live(pawn, image->pawn_class) ||
            !object_is_live(
                component,
                paint->runtime_paintable_class) ||
            component->GetWorld() != world)
        {
            return runtime_failure(
                application::RuntimeContractId::PaintCapture,
                application::ContractFailureKind::StaleObject);
        }
        auto* mesh = read_weak_object(
            image->target_mesh_component,
            component);
        if (!object_is_live(
                mesh,
                image->skinned_mesh_component_class) ||
            !mesh->IsA(scene->primitive_component_class) ||
            mesh->GetWorld() != world ||
            !outer_chain_contains(mesh, pawn))
        {
            return runtime_failure(
                application::RuntimeContractId::PaintCapture,
                application::ContractFailureKind::WrongClass);
        }
        auto* asset = read_object(image->skinned_asset, mesh);
        if (!object_is_live(asset, image->skinned_asset_class))
        {
            return runtime_failure(
                application::RuntimeContractId::PaintCapture,
                application::ContractFailureKind::MissingObject);
        }
        const auto profile =
            image_paint_profiles_
                ? image_paint_profiles_
                      ->find_by_unreal_asset_path(
                          RC::to_string(asset->GetPathName()))
                : nullptr;
        if (!profile ||
            RC::to_string(asset->GetName()) !=
                profile->sampling.identity.export_name ||
            !core::validate_deformation(profile->sampling)
                 .empty() ||
            !core::validate_pair(
                 profile->sampling,
                 profile->image)
                 .empty() ||
            !ensure_paint_mesh_initialized(
                component,
                mesh,
                *paint))
        {
            return runtime_failure(
                application::RuntimeContractId::PaintCapture,
                application::ContractFailureKind::InvalidValue);
        }
        auto bone_transforms = capture_paint_bone_transforms(
            mesh,
            profile->sampling,
            *paint);
        if (!bone_transforms)
        {
            return runtime_failure(
                application::RuntimeContractId::PaintCapture,
                application::ContractFailureKind::InvalidValue);
        }

        const auto esp = resolve_esp_contracts();
        if (!esp)
        {
            return runtime_failure(
                application::RuntimeContractId::PaintCapture,
                application::ContractFailureKind::MissingObject);
        }
        auto view = capture_calibrated_esp_view(
            *active,
            *esp,
            application::RuntimeContractId::PaintCapture);
        if (!view)
        {
            return std::unexpected(view.error());
        }

        auto capture_width = static_cast<std::uint32_t>(
            active->viewport_width);
        auto capture_height = static_cast<std::uint32_t>(
            active->viewport_height);
        const auto largest =
            std::max(capture_width, capture_height);
        if (largest > core::MaximumPaintCaptureDimension)
        {
            const auto scale =
                static_cast<double>(
                    core::MaximumPaintCaptureDimension) /
                static_cast<double>(largest);
            capture_width = std::max(
                1U,
                static_cast<std::uint32_t>(std::lround(
                    static_cast<double>(capture_width) *
                    scale)));
            capture_height = std::max(
                1U,
                static_cast<std::uint32_t>(std::lround(
                    static_cast<double>(capture_height) *
                    scale)));
        }
        view->aspect_ratio =
            static_cast<double>(capture_width) /
            static_cast<double>(capture_height);
        const auto camera = encode_paint_scene_capture_camera(
            *view,
            capture_width,
            capture_height);
        const auto fingerprint =
            core::make_paint_appearance_camera_fingerprint(
                *view,
                core::EspViewport{
                    static_cast<double>(active->viewport_width),
                    static_cast<double>(active->viewport_height),
                },
                capture_width,
                capture_height);
        if (!camera || !fingerprint)
        {
            return runtime_failure(
                application::RuntimeContractId::PaintCapture,
                application::ContractFailureKind::InvalidValue);
        }
        const auto brush_plane =
            resolve_paint_brush_plane_visuals(world, *scene);
        if (!brush_plane)
        {
            return std::unexpected(brush_plane.error());
        }
        return PreparedPaintCaptureSeed{
            settings,
            std::move(*pass_plan),
            *paint,
            *scene,
            *image,
            *bound,
            *active,
            world,
            pawn,
            component,
            mesh,
            profile,
            std::move(*bone_transforms),
            *view,
            core::EspViewport{
                static_cast<double>(capture_width),
                static_cast<double>(capture_height),
            },
            *fingerprint,
            *camera,
            *brush_plane,
        };
    }

    auto current_paint_capture_fingerprint(
        const PreparedPaintCaptureSeed& seed)
        -> std::expected<
            core::PaintAppearanceCameraFingerprint,
            application::RuntimeExecutionError>
    {
        auto active = std::optional<ActiveFrame>{};
        auto bound = std::optional<BoundFrame>{};
        {
            const auto lock = std::scoped_lock{mutex_};
            if (detaching_)
            {
                return runtime_failure(
                    application::RuntimeContractId::PaintCapture,
                    application::ContractFailureKind::StaleObject);
            }
            active = active_frame_;
            bound = bound_frame_;
        }
        if (!active || !bound ||
            active->viewport_width <= 0 ||
            active->viewport_height <= 0 ||
            active->identity != seed.active.identity ||
            bound->identity != seed.bound.identity ||
            bound->component_identity !=
                seed.bound.component_identity ||
            bound->component_generation !=
                seed.bound.component_generation ||
            bound->component.Get() != seed.component ||
            bound->world.Get() != seed.world ||
            bound->pawn.Get() != seed.pawn)
        {
            return runtime_failure(
                application::RuntimeContractId::PaintCapture,
                application::ContractFailureKind::StaleObject);
        }
        const auto esp = resolve_esp_contracts();
        if (!esp)
        {
            return runtime_failure(
                application::RuntimeContractId::PaintCapture,
                application::ContractFailureKind::MissingObject);
        }
        auto view = capture_calibrated_esp_view(
            *active,
            *esp,
            application::RuntimeContractId::PaintCapture);
        if (!view)
        {
            return std::unexpected(view.error());
        }
        view->aspect_ratio = seed.view.aspect_ratio;
        const auto fingerprint =
            core::make_paint_appearance_camera_fingerprint(
                *view,
                core::EspViewport{
                    static_cast<double>(active->viewport_width),
                    static_cast<double>(active->viewport_height),
                },
                seed.camera.width,
                seed.camera.height);
        if (!fingerprint)
        {
            return runtime_failure(
                application::RuntimeContractId::PaintCapture,
                application::ContractFailureKind::InvalidValue);
        }
        return *fingerprint;
    }

    auto capture_automatic_source_pass(
        AutomaticPaintCaptureSession& session,
        const PaintSceneCapturePass& pass)
        -> std::expected<
            void,
            application::RuntimeExecutionError>
    {
        if (pass.subject !=
            PaintSceneCaptureSubject::BackgroundOnly)
        {
            return runtime_failure(
                application::RuntimeContractId::PaintCapture,
                application::ContractFailureKind::InvalidValue);
        }
        const auto fingerprint =
            current_paint_capture_fingerprint(session.seed);
        if (!fingerprint)
        {
            return std::unexpected(fingerprint.error());
        }
        const auto linear = capture_paint_scene_pass(
            static_cast<UWorld*>(session.seed.world),
            session.seed.mesh,
            session.seed.brush_plane,
            session.seed.camera,
            pass,
            session.seed.scene);
        if (!linear)
        {
            return std::unexpected(linear.error());
        }

        if (pass.kind == PaintSceneCapturePassKind::BaseColor ||
            pass.kind ==
                PaintSceneCapturePassKind::FinalColorLdr)
        {
            auto converted =
                convert_paint_capture_linear_colors_to_srgb8(
                    *linear);
            if (!converted)
            {
                return runtime_failure(
                    application::RuntimeContractId::PaintCapture,
                    application::ContractFailureKind::InvalidValue);
            }
            auto pixels = std::make_shared<
                const std::vector<core::Rgb8>>(
                std::move(*converted));
            if (pass.kind ==
                PaintSceneCapturePassKind::BaseColor)
            {
                session.evidence.base_color = {
                    *fingerprint,
                    std::move(pixels),
                };
            }
            else
            {
                session.evidence.final_ldr = {
                    *fingerprint,
                    std::move(pixels),
                };
            }
            return {};
        }
        if (pass.kind == PaintSceneCapturePassKind::SceneDepth)
        {
            auto converted =
                convert_paint_capture_linear_colors_to_depth(
                    *linear);
            if (!converted)
            {
                return runtime_failure(
                    application::RuntimeContractId::PaintCapture,
                    application::ContractFailureKind::InvalidValue);
            }
            session.evidence.scene_depth = {
                *fingerprint,
                std::make_shared<
                    const std::vector<double>>(
                    std::move(*converted)),
            };
            return {};
        }

        auto converted =
            convert_paint_capture_linear_colors_to_hdr(*linear);
        if (!converted)
        {
            return runtime_failure(
                application::RuntimeContractId::PaintCapture,
                application::ContractFailureKind::InvalidValue);
        }
        auto pixels = std::make_shared<
            const std::vector<core::AppearanceRgb>>(
            std::move(*converted));
        switch (pass.kind)
        {
        case PaintSceneCapturePassKind::FinalColorHdr:
            session.evidence.final_hdr = {
                *fingerprint,
                std::move(pixels),
            };
            return {};
        case PaintSceneCapturePassKind::IntrinsicEmissionHdr:
            session.evidence.intrinsic_emission_hdr = {
                *fingerprint,
                std::move(pixels),
            };
            return {};
        case PaintSceneCapturePassKind::FinalToneCurveHdr:
            session.evidence.tone_curve_hdr = {
                *fingerprint,
                std::move(pixels),
            };
            return {};
        case PaintSceneCapturePassKind::Normal:
            session.evidence.normal = {
                *fingerprint,
                std::move(pixels),
            };
            return {};
        case PaintSceneCapturePassKind::BaseColor:
        case PaintSceneCapturePassKind::SceneDepth:
        case PaintSceneCapturePassKind::FinalColorLdr:
            break;
        }
        return runtime_failure(
            application::RuntimeContractId::PaintCapture,
            application::ContractFailureKind::InvalidValue);
    }

    auto capture_automatic_feedback_pass(
        AutomaticPaintCaptureSession& session,
        const PaintSceneCapturePass& pass)
        -> std::expected<
            void,
            application::RuntimeExecutionError>
    {
        if (pass.subject !=
                PaintSceneCaptureSubject::TargetVisible ||
            (pass.kind !=
                 PaintSceneCapturePassKind::FinalColorHdr &&
             pass.kind !=
                 PaintSceneCapturePassKind::BaseColor &&
             pass.kind != PaintSceneCapturePassKind::
                              IntrinsicEmissionHdr))
        {
            return runtime_failure(
                application::RuntimeContractId::PaintCapture,
                application::ContractFailureKind::InvalidValue);
        }
        const auto fingerprint =
            current_paint_capture_fingerprint(session.seed);
        if (!fingerprint)
        {
            return std::unexpected(fingerprint.error());
        }
        const auto linear = capture_paint_scene_pass(
            static_cast<UWorld*>(session.seed.world),
            session.seed.mesh,
            session.seed.brush_plane,
            session.seed.camera,
            pass,
            session.seed.scene);
        if (!linear)
        {
            return std::unexpected(linear.error());
        }
        auto converted =
            convert_paint_capture_linear_colors_to_hdr(*linear);
        if (!converted)
        {
            return runtime_failure(
                application::RuntimeContractId::PaintCapture,
                application::ContractFailureKind::InvalidValue);
        }
        auto pixels = std::make_shared<
            const std::vector<core::AppearanceRgb>>(
            std::move(*converted));
        switch (pass.kind)
        {
        case PaintSceneCapturePassKind::FinalColorHdr:
            session.feedback_evidence.final_hdr = {
                *fingerprint,
                std::move(pixels),
            };
            return {};
        case PaintSceneCapturePassKind::BaseColor:
            session.feedback_evidence.base_color = {
                *fingerprint,
                std::move(pixels),
            };
            return {};
        case PaintSceneCapturePassKind::IntrinsicEmissionHdr:
            session.target_intrinsic_e0 = {
                *fingerprint,
                std::move(pixels),
            };
            return {};
        case PaintSceneCapturePassKind::FinalToneCurveHdr:
        case PaintSceneCapturePassKind::Normal:
        case PaintSceneCapturePassKind::SceneDepth:
        case PaintSceneCapturePassKind::FinalColorLdr:
            break;
        }
        return runtime_failure(
            application::RuntimeContractId::PaintCapture,
            application::ContractFailureKind::InvalidValue);
    }

    auto start_automatic_paint_candidate(
        AutomaticPaintCaptureSession& session,
        AutomaticPaintCandidatePurpose purpose,
        std::vector<double> parameters)
        -> std::expected<
            void,
            application::RuntimeExecutionError>
    {
        if (!session.model || !session.preview_snapshot ||
            !session.evidence.base_color.pixels ||
            !session.evidence.final_ldr.pixels ||
            parameters.empty() || session.preview_mutated)
        {
            return runtime_failure(
                application::RuntimeContractId::PaintCapture,
                application::ContractFailureKind::InvalidValue);
        }
        session.candidate_purpose = purpose;
        session.parameters = std::move(parameters);
        session.candidate_preview.reset();
        session.readback_references.reset();
        session.feedback_evidence = {};
        session.target_intrinsic_e0 = {};
        session.next_feedback_pass = 0U;
        session.preview_applied_ms = 0U;
        const auto started = paint_appearance_worker_.start(
            session.generation,
            application::PaintAppearanceCandidateWork{
                session.model,
                session.evidence.base_color.pixels,
                session.evidence.final_ldr.pixels,
                session.parameters,
                session.seed.settings.brush_size_texels,
                session.preview_snapshot->original.dimension,
                session.preview_snapshot->original,
            });
        if (!started)
        {
            return runtime_failure(
                application::RuntimeContractId::PaintCapture,
                application::ContractFailureKind::ExecutionFailure);
        }
        session.stage = AutomaticPaintCaptureStage::CandidatePending;
        return {};
    }

    auto capture_paint(const core::PaintSettings& settings)
        -> std::expected<
            application::CapturedPaintJob,
            application::RuntimeExecutionError>
    {
        if (!IsInGameThreadRaw())
        {
            return std::unexpected(
                application::RuntimeExecutionError{
                    application::RuntimeExecutionErrorCode::
                        WrongThread,
                    std::nullopt,
                });
        }
        try
        {
            const auto pass_plan =
                build_paint_scene_capture_plan(settings);
            if (!pass_plan ||
                pass_plan->requires_preview_feedback)
            {
                return runtime_failure(
                    application::RuntimeContractId::PaintCapture,
                    application::ContractFailureKind::
                        InvalidValue);
            }

            auto paint = std::optional<PaintContracts>{};
            auto scene =
                std::optional<PaintSceneCaptureContracts>{};
            auto image =
                std::optional<ImagePaintContracts>{};
            auto bound = std::optional<BoundFrame>{};
            auto active = std::optional<ActiveFrame>{};
            {
                const auto lock = std::scoped_lock{mutex_};
                if (detaching_)
                {
                    return runtime_failure(
                        application::RuntimeContractId::
                            PaintCapture,
                        application::ContractFailureKind::
                            StaleObject);
                }
                paint = paint_contracts_;
                scene = paint_scene_capture_contracts_;
                image = image_paint_contracts_;
                bound = bound_frame_;
                active = active_frame_;
            }
            if (!paint || !scene || !image || !bound ||
                !active ||
                bound->identity != active->identity ||
                !bound->identity.valid())
            {
                return runtime_failure(
                    application::RuntimeContractId::PaintCapture,
                    application::ContractFailureKind::
                        MissingObject);
            }
            auto* world = bound->world.Get();
            auto* pawn = bound->pawn.Get();
            auto* component = bound->component.Get();
            if (!object_is_live(
                    world,
                    image->world_class) ||
                !object_is_live(
                    pawn,
                    image->pawn_class) ||
                !object_is_live(
                    component,
                    paint->runtime_paintable_class) ||
                component->GetWorld() != world)
            {
                return runtime_failure(
                    application::RuntimeContractId::PaintCapture,
                    application::ContractFailureKind::
                        StaleObject);
            }
            auto* mesh = read_weak_object(
                image->target_mesh_component,
                component);
            if (!object_is_live(
                    mesh,
                    image->skinned_mesh_component_class) ||
                !mesh->IsA(scene->primitive_component_class) ||
                mesh->GetWorld() != world ||
                !outer_chain_contains(mesh, pawn))
            {
                return runtime_failure(
                    application::RuntimeContractId::PaintCapture,
                    application::ContractFailureKind::
                        WrongClass);
            }
            auto* asset = read_object(
                image->skinned_asset,
                mesh);
            if (!object_is_live(
                    asset,
                    image->skinned_asset_class))
            {
                return runtime_failure(
                    application::RuntimeContractId::PaintCapture,
                    application::ContractFailureKind::
                        MissingObject);
            }
            const auto profile =
                image_paint_profiles_
                    ? image_paint_profiles_
                          ->find_by_unreal_asset_path(
                              RC::to_string(
                                  asset->GetPathName()))
                    : nullptr;
            if (!profile ||
                RC::to_string(asset->GetName()) !=
                    profile->sampling.identity.export_name ||
                !core::validate_deformation(
                     profile->sampling)
                     .empty() ||
                !core::validate_pair(
                     profile->sampling,
                     profile->image)
                     .empty() ||
                !ensure_paint_mesh_initialized(
                    component,
                    mesh,
                    *paint))
            {
                return runtime_failure(
                    application::RuntimeContractId::PaintCapture,
                    application::ContractFailureKind::
                        InvalidValue);
            }
            const auto bone_transforms =
                capture_paint_bone_transforms(
                    mesh,
                    profile->sampling,
                    *paint);
            if (!bone_transforms)
            {
                return runtime_failure(
                    application::RuntimeContractId::PaintCapture,
                    application::ContractFailureKind::
                        InvalidValue);
            }

            const auto esp = resolve_esp_contracts();
            if (!esp)
            {
                return runtime_failure(
                    application::RuntimeContractId::PaintCapture,
                    application::ContractFailureKind::
                        MissingObject);
            }
            auto view = capture_calibrated_esp_view(
                *active,
                *esp,
                application::RuntimeContractId::PaintCapture);
            if (!view)
            {
                return std::unexpected(view.error());
            }

            auto capture_width = static_cast<std::uint32_t>(
                active->viewport_width);
            auto capture_height = static_cast<std::uint32_t>(
                active->viewport_height);
            const auto largest =
                std::max(capture_width, capture_height);
            if (largest >
                core::MaximumPaintCaptureDimension)
            {
                const auto scale =
                    static_cast<double>(
                        core::MaximumPaintCaptureDimension) /
                    static_cast<double>(largest);
                capture_width = std::max(
                    1U,
                    static_cast<std::uint32_t>(
                        std::lround(
                            static_cast<double>(
                                capture_width) *
                            scale)));
                capture_height = std::max(
                    1U,
                    static_cast<std::uint32_t>(
                        std::lround(
                            static_cast<double>(
                                capture_height) *
                            scale)));
            }
            view->aspect_ratio =
                static_cast<double>(capture_width) /
                static_cast<double>(capture_height);
            const auto camera =
                encode_paint_scene_capture_camera(
                    *view,
                    capture_width,
                    capture_height);
            if (!camera)
            {
                return runtime_failure(
                    application::RuntimeContractId::PaintCapture,
                    application::ContractFailureKind::
                        InvalidValue);
            }
            const auto brush_plane =
                resolve_paint_brush_plane_visuals(
                    world,
                    *scene);
            if (!brush_plane)
            {
                return std::unexpected(brush_plane.error());
            }

            auto intrinsic =
                std::shared_ptr<const std::vector<core::Rgb8>>{};
            auto scene_colors =
                std::shared_ptr<const std::vector<core::Rgb8>>{};
            for (const auto& pass : pass_plan->passes)
            {
                const auto linear = capture_paint_scene_pass(
                    static_cast<UWorld*>(world),
                    mesh,
                    *brush_plane,
                    *camera,
                    pass,
                    *scene);
                if (!linear)
                {
                    return std::unexpected(linear.error());
                }
                const auto converted =
                    convert_paint_capture_linear_colors_to_srgb8(
                        *linear);
                if (!converted)
                {
                    return runtime_failure(
                        application::RuntimeContractId::
                            PaintCapture,
                        application::ContractFailureKind::
                            InvalidValue);
                }
                auto owned =
                    std::make_shared<const std::vector<core::Rgb8>>(
                        std::move(*converted));
                if (pass.kind ==
                    PaintSceneCapturePassKind::BaseColor)
                {
                    intrinsic = std::move(owned);
                }
                else if (
                    pass.kind ==
                    PaintSceneCapturePassKind::FinalColorHdr)
                {
                    scene_colors = std::move(owned);
                }
                else
                {
                    return runtime_failure(
                        application::RuntimeContractId::
                            PaintCapture,
                        application::ContractFailureKind::
                            InvalidValue);
                }
            }
            if (!intrinsic)
            {
                return runtime_failure(
                    application::RuntimeContractId::PaintCapture,
                    application::ContractFailureKind::
                        InvalidValue);
            }
            if (!scene_colors)
            {
                scene_colors = intrinsic;
            }

            {
                const auto lock = std::scoped_lock{mutex_};
                if (detaching_ || !bound_frame_ ||
                    !active_frame_ ||
                    bound_frame_->identity != bound->identity ||
                    bound_frame_->component_identity !=
                        bound->component_identity ||
                    bound_frame_->component_generation !=
                        bound->component_generation ||
                    bound_frame_->component.Get() != component ||
                    active_frame_->identity != active->identity)
                {
                    return runtime_failure(
                        application::RuntimeContractId::
                            PaintCapture,
                        application::ContractFailureKind::
                            StaleObject);
                }
            }
            return application::CapturedPaintJob{
                application::RuntimeObjectHandle{
                    bound->component_identity,
                    bound->component_generation,
                },
                application::PaintPlanningRequest{
                    core::PaintCaptureInput{
                        profile->sampling,
                        profile->image,
                        std::move(*bone_transforms),
                        settings,
                        *view,
                        core::EspViewport{
                            static_cast<double>(capture_width),
                            static_cast<double>(capture_height),
                        },
                        core::PaintCaptureRaster{
                            capture_width,
                            capture_height,
                            std::move(intrinsic),
                            std::move(scene_colors),
                            nullptr,
                        },
                    },
                },
                core::replication_pacing_plan({}),
            };
        }
        catch (...)
        {
            return runtime_failure(
                application::RuntimeContractId::PaintCapture,
                application::ContractFailureKind::
                    ExecutionFailure);
        }
    }

    auto begin_automatic_paint_capture(
        const core::PaintSettings& settings,
        application::JobGeneration generation)
        -> std::expected<
            void,
            application::RuntimeExecutionError>
    {
        if (!IsInGameThreadRaw())
        {
            return std::unexpected(
                application::RuntimeExecutionError{
                    application::RuntimeExecutionErrorCode::
                        WrongThread,
                    std::nullopt,
                });
        }
        if (!settings.auto_material || generation == 0U ||
            automatic_paint_capture_)
        {
            return runtime_failure(
                application::RuntimeContractId::PaintCapture,
                application::ContractFailureKind::
                    InvalidValue);
        }
        try
        {
            auto seed = prepare_paint_capture_seed(settings, true);
            if (!seed)
            {
                return std::unexpected(seed.error());
            }
            automatic_paint_capture_.emplace(
                AutomaticPaintCaptureSession{
                    generation,
                    std::move(*seed),
                });
            const auto& admitted =
                automatic_paint_capture_->seed;
            const auto started = paint_appearance_worker_.start(
                generation,
                application::PaintAppearanceGeometryPrepareWork{
                    admitted.profile->sampling,
                    admitted.profile->image,
                    admitted.bone_transforms,
                    settings.brush_size_texels,
                    admitted.view,
                    admitted.capture_viewport,
                });
            if (!started)
            {
                automatic_paint_capture_.reset();
                return runtime_failure(
                    application::RuntimeContractId::PaintCapture,
                    application::ContractFailureKind::
                        ExecutionFailure);
            }
            return {};
        }
        catch (...)
        {
            automatic_paint_capture_.reset();
            return runtime_failure(
                application::RuntimeContractId::PaintCapture,
                application::ContractFailureKind::
                    ExecutionFailure);
        }
    }

    auto advance_automatic_paint_capture(
        application::JobGeneration generation)
        -> std::expected<
            std::optional<application::CapturedPaintJob>,
            application::RuntimeExecutionError>
    {
        if (!IsInGameThreadRaw())
        {
            return std::unexpected(
                application::RuntimeExecutionError{
                    application::RuntimeExecutionErrorCode::
                        WrongThread,
                    std::nullopt,
                });
        }
        if (!automatic_paint_capture_ || generation == 0U ||
            automatic_paint_capture_->generation != generation)
        {
            return runtime_failure(
                application::RuntimeContractId::PaintCapture,
                application::ContractFailureKind::StaleObject);
        }
        try
        {
            auto& session = *automatic_paint_capture_;
            const auto component_handle =
                application::RuntimeObjectHandle{
                    session.seed.bound.component_identity,
                    session.seed.bound.component_generation,
                };
            switch (session.stage)
            {
            case AutomaticPaintCaptureStage::GeometryPending:
            {
                auto completion = paint_appearance_worker_.poll();
                if (!completion)
                {
                    return std::optional<
                        application::CapturedPaintJob>{};
                }
                const auto* prepared =
                    completion->generation == generation &&
                            completion->result
                        ? std::get_if<
                              application::
                                  PaintAppearanceGeometryPrepared>(
                              &*completion->result)
                        : nullptr;
                if (prepared == nullptr ||
                    !prepared->geometry ||
                    !prepared->source_query_pixels)
                {
                    session.stage =
                        AutomaticPaintCaptureStage::Failed;
                    return runtime_failure(
                        application::RuntimeContractId::
                            PaintCapture,
                        application::ContractFailureKind::
                            InvalidValue);
                }
                const auto width = static_cast<std::size_t>(
                    session.seed.camera.width);
                const auto height = static_cast<std::size_t>(
                    session.seed.camera.height);
                if (width == 0U || height == 0U ||
                    width >
                        std::numeric_limits<std::size_t>::max() /
                            height)
                {
                    session.stage =
                        AutomaticPaintCaptureStage::Failed;
                    return runtime_failure(
                        application::RuntimeContractId::
                            PaintCapture,
                        application::ContractFailureKind::
                            InvalidValue);
                }
                auto source_pixels = std::vector<
                    core::PaintAppearanceSourcePixel>(
                    width * height);
                for (const auto pixel :
                     *prepared->source_query_pixels)
                {
                    if (pixel >= source_pixels.size())
                    {
                        session.stage =
                            AutomaticPaintCaptureStage::Failed;
                        return runtime_failure(
                            application::RuntimeContractId::
                                PaintCapture,
                            application::ContractFailureKind::
                                InvalidValue);
                    }
                    source_pixels[pixel].visible = true;
                }
                session.geometry = prepared->geometry;
                session.evidence.source_pixels =
                    std::make_shared<const std::vector<
                        core::PaintAppearanceSourcePixel>>(
                        std::move(source_pixels));
                session.stage =
                    AutomaticPaintCaptureStage::SourceCapture;
                return std::optional<
                    application::CapturedPaintJob>{};
            }
            case AutomaticPaintCaptureStage::SourceCapture:
            {
                if (session.next_source_pass >=
                    session.seed.pass_plan.passes.size())
                {
                    session.stage =
                        AutomaticPaintCaptureStage::Failed;
                    return runtime_failure(
                        application::RuntimeContractId::
                            PaintCapture,
                        application::ContractFailureKind::
                            InvalidValue);
                }
                const auto captured =
                    capture_automatic_source_pass(
                        session,
                        session.seed.pass_plan.passes[
                            session.next_source_pass]);
                if (!captured)
                {
                    session.stage =
                        AutomaticPaintCaptureStage::Failed;
                    return std::unexpected(captured.error());
                }
                ++session.next_source_pass;
                if (session.next_source_pass ==
                    session.seed.pass_plan.passes.size())
                {
                    const auto started =
                        paint_appearance_worker_.start(
                            generation,
                            application::
                                PaintAppearanceCapturePrepareWork{
                                session.geometry,
                                session.evidence,
                                session.seed.settings
                                    .include_scene_lighting,
                                std::nullopt,
                            });
                    if (!started)
                    {
                        session.stage =
                            AutomaticPaintCaptureStage::Failed;
                        return runtime_failure(
                            application::RuntimeContractId::
                                PaintCapture,
                            application::ContractFailureKind::
                                ExecutionFailure);
                    }
                    session.stage =
                        AutomaticPaintCaptureStage::
                            PreliminaryModelPending;
                }
                return std::optional<
                    application::CapturedPaintJob>{};
            }
            case AutomaticPaintCaptureStage::
                PreliminaryModelPending:
            {
                auto completion = paint_appearance_worker_.poll();
                if (!completion)
                {
                    return std::optional<
                        application::CapturedPaintJob>{};
                }
                const auto* prepared =
                    completion->generation == generation &&
                            completion->result
                        ? std::get_if<
                              application::PaintAppearancePrepared>(
                              &*completion->result)
                        : nullptr;
                if (prepared == nullptr || !prepared->model ||
                    prepared->parameters.empty())
                {
                    session.stage =
                        AutomaticPaintCaptureStage::Failed;
                    return runtime_failure(
                        application::RuntimeContractId::
                            PaintCapture,
                        application::ContractFailureKind::
                            InvalidValue);
                }
                session.model = prepared->model;
                session.parameters = prepared->parameters;
                session.stage =
                    AutomaticPaintCaptureStage::SnapshotPending;
                return std::optional<
                    application::CapturedPaintJob>{};
            }
            case AutomaticPaintCaptureStage::SnapshotPending:
            {
                if (!session.model ||
                    session.parameters.empty() ||
                    !session.evidence.base_color.pixels ||
                    !session.evidence.final_ldr.pixels)
                {
                    session.stage =
                        AutomaticPaintCaptureStage::Failed;
                    return runtime_failure(
                        application::RuntimeContractId::
                            PaintCapture,
                        application::ContractFailureKind::
                            InvalidValue);
                }
                auto snapshot = capture_preview(component_handle);
                if (!snapshot ||
                    snapshot->component != component_handle)
                {
                    session.stage =
                        AutomaticPaintCaptureStage::Failed;
                    return snapshot
                               ? runtime_failure(
                                     application::RuntimeContractId::
                                         PaintCapture,
                                     application::ContractFailureKind::
                                         InvalidValue)
                               : std::unexpected(snapshot.error());
                }
                session.preview_snapshot =
                    std::move(*snapshot);
                auto fallback_parameters =
                    core::paint_appearance_fallback_parameters(
                        *session.model);
                const auto started =
                    start_automatic_paint_candidate(
                        session,
                        AutomaticPaintCandidatePurpose::TargetE0,
                        std::move(fallback_parameters));
                if (!started)
                {
                    session.stage =
                        AutomaticPaintCaptureStage::Failed;
                    return std::unexpected(started.error());
                }
                return std::optional<
                    application::CapturedPaintJob>{};
            }
            case AutomaticPaintCaptureStage::CandidatePending:
            {
                auto completion = paint_appearance_worker_.poll();
                if (!completion)
                {
                    return std::optional<
                        application::CapturedPaintJob>{};
                }
                const auto* candidate =
                    completion->generation == generation &&
                            completion->result
                        ? std::get_if<
                              application::PaintAppearanceCandidate>(
                              &*completion->result)
                        : nullptr;
                if (candidate == nullptr ||
                    !candidate->preview ||
                    !candidate->readback_references ||
                    candidate->readback_references->empty() ||
                    candidate->parameters.empty())
                {
                    session.stage =
                        AutomaticPaintCaptureStage::Failed;
                    return runtime_failure(
                        application::RuntimeContractId::
                            PaintCapture,
                        application::ContractFailureKind::
                            InvalidValue);
                }
                session.candidate_preview = candidate->preview;
                session.readback_references =
                    candidate->readback_references;
                session.parameters = candidate->parameters;
                session.stage = AutomaticPaintCaptureStage::
                    PreviewApplyPending;
                return std::optional<
                    application::CapturedPaintJob>{};
            }
            case AutomaticPaintCaptureStage::PreviewApplyPending:
            {
                if (!session.preview_snapshot ||
                    !session.candidate_preview)
                {
                    session.stage =
                        AutomaticPaintCaptureStage::Failed;
                    return runtime_failure(
                        application::RuntimeContractId::
                            PaintCapture,
                        application::ContractFailureKind::
                            InvalidValue);
                }
                // Either channel import can mutate before reporting an
                // error, so restoration ownership starts before the call.
                session.preview_mutated = true;
                const auto applied = apply_preview(
                    component_handle,
                    *session.candidate_preview);
                if (!applied)
                {
                    session.stage =
                        AutomaticPaintCaptureStage::RestorePending;
                    return std::unexpected(applied.error());
                }
                session.preview_applied_ms = steady_milliseconds();
                session.stage =
                    AutomaticPaintCaptureStage::PreviewSettle;
                return std::optional<
                    application::CapturedPaintJob>{};
            }
            case AutomaticPaintCaptureStage::PreviewSettle:
            {
                constexpr auto PreviewSettleMilliseconds =
                    std::uint64_t{48U};
                const auto now_ms = steady_milliseconds();
                if (now_ms < session.preview_applied_ms ||
                    now_ms - session.preview_applied_ms <
                        PreviewSettleMilliseconds)
                {
                    return std::optional<
                        application::CapturedPaintJob>{};
                }
                session.stage =
                    AutomaticPaintCaptureStage::FeedbackCapture;
                return std::optional<
                    application::CapturedPaintJob>{};
            }
            case AutomaticPaintCaptureStage::FeedbackCapture:
            {
                const auto& feedback_plan =
                    paint_appearance_feedback_capture_plan();
                const auto pass_count =
                    session.candidate_purpose ==
                            AutomaticPaintCandidatePurpose::TargetE0
                        ? feedback_plan.size()
                        : std::size_t{1U};
                if (session.next_feedback_pass >=
                    pass_count)
                {
                    session.stage =
                        AutomaticPaintCaptureStage::RestorePending;
                    return runtime_failure(
                        application::RuntimeContractId::
                            PaintCapture,
                        application::ContractFailureKind::
                            InvalidValue);
                }
                const auto captured =
                    capture_automatic_feedback_pass(
                        session,
                        feedback_plan[
                            session.next_feedback_pass]);
                if (!captured)
                {
                    session.stage =
                        AutomaticPaintCaptureStage::RestorePending;
                    return std::unexpected(captured.error());
                }
                ++session.next_feedback_pass;
                if (session.next_feedback_pass ==
                    pass_count)
                {
                    session.stage = AutomaticPaintCaptureStage::
                        RestorePending;
                }
                return std::optional<
                    application::CapturedPaintJob>{};
            }
            case AutomaticPaintCaptureStage::RestorePending:
            {
                if (!session.preview_mutated ||
                    !session.preview_snapshot ||
                    !session.readback_references)
                {
                    session.stage =
                        AutomaticPaintCaptureStage::Failed;
                    return runtime_failure(
                        application::RuntimeContractId::
                            PaintCapture,
                        application::ContractFailureKind::
                            InvalidValue);
                }
                const auto restored =
                    restore_preview(*session.preview_snapshot);
                if (!restored)
                {
                    return std::unexpected(restored.error());
                }
                session.preview_mutated = false;
                session.candidate_preview.reset();
                if (session.candidate_purpose ==
                    AutomaticPaintCandidatePurpose::TargetE0)
                {
                    const auto started =
                        paint_appearance_worker_.start(
                            generation,
                            application::
                                PaintAppearanceTargetE0PrepareWork{
                                session.seed.camera_fingerprint,
                                session.readback_references,
                                session.feedback_evidence,
                                core::PaintAppearanceTargetE0Evidence{
                                    session.feedback_evidence.base_color,
                                    session.target_intrinsic_e0,
                                },
                            });
                    if (!started)
                    {
                        session.stage =
                            AutomaticPaintCaptureStage::Failed;
                        return runtime_failure(
                            application::RuntimeContractId::
                                PaintCapture,
                            application::ContractFailureKind::
                                ExecutionFailure);
                    }
                    session.stage =
                        AutomaticPaintCaptureStage::TargetE0Pending;
                    return std::optional<
                        application::CapturedPaintJob>{};
                }
                if (!session.model || !session.feedback ||
                    !session.feedback_evidence.final_hdr.pixels ||
                    !core::paint_appearance_camera_matches(
                        session.seed.camera_fingerprint,
                        session.feedback_evidence.final_hdr.camera))
                {
                    session.stage =
                        AutomaticPaintCaptureStage::Failed;
                    return runtime_failure(
                        application::RuntimeContractId::
                            PaintCapture,
                        application::ContractFailureKind::
                            InvalidValue);
                }
                const auto started =
                    paint_appearance_worker_.start(
                        generation,
                        application::PaintAppearanceEvaluateWork{
                            session.model,
                            session.feedback_evidence.final_hdr.pixels,
                            true,
                            session.feedback->readback.ok,
                            session.feedback->readback.transform,
                        });
                if (!started)
                {
                    session.stage =
                        AutomaticPaintCaptureStage::Failed;
                    return runtime_failure(
                        application::RuntimeContractId::
                            PaintCapture,
                        application::ContractFailureKind::
                            ExecutionFailure);
                }
                session.stage =
                    AutomaticPaintCaptureStage::EvaluationPending;
                return std::optional<
                    application::CapturedPaintJob>{};
            }
            case AutomaticPaintCaptureStage::TargetE0Pending:
            {
                auto completion = paint_appearance_worker_.poll();
                if (!completion)
                {
                    return std::optional<
                        application::CapturedPaintJob>{};
                }
                const auto* prepared =
                    completion->generation == generation &&
                            completion->result
                        ? std::get_if<application::
                              PaintAppearanceTargetE0Prepared>(
                              &*completion->result)
                        : nullptr;
                if (prepared == nullptr ||
                    !prepared->feedback.target_hdr ||
                    !prepared->feedback.camera_stable ||
                    !prepared->feedback.readback.ok ||
                    !prepared->target_e0.camera_stable ||
                    !prepared->target_e0.noise.ok ||
                    prepared->target_e0.paired_samples <
                        static_cast<std::size_t>(core::
                            AppearanceFitMinimumSamples))
                {
                    session.stage =
                        AutomaticPaintCaptureStage::Failed;
                    return runtime_failure(
                        application::RuntimeContractId::
                            PaintCapture,
                        application::ContractFailureKind::
                            InvalidValue);
                }
                session.feedback = prepared->feedback;
                session.target_e0 = prepared->target_e0;
                const auto started =
                    paint_appearance_worker_.start(
                        generation,
                        application::
                            PaintAppearanceCapturePrepareWork{
                            session.geometry,
                            session.evidence,
                            session.seed.settings
                                .include_scene_lighting,
                            session.target_e0->noise,
                        });
                if (!started)
                {
                    session.stage =
                        AutomaticPaintCaptureStage::Failed;
                    return runtime_failure(
                        application::RuntimeContractId::
                            PaintCapture,
                        application::ContractFailureKind::
                            ExecutionFailure);
                }
                session.stage =
                    AutomaticPaintCaptureStage::FinalModelPending;
                return std::optional<
                    application::CapturedPaintJob>{};
            }
            case AutomaticPaintCaptureStage::FinalModelPending:
            {
                auto completion = paint_appearance_worker_.poll();
                if (!completion)
                {
                    return std::optional<
                        application::CapturedPaintJob>{};
                }
                const auto* prepared =
                    completion->generation == generation &&
                            completion->result
                        ? std::get_if<
                              application::PaintAppearancePrepared>(
                              &*completion->result)
                        : nullptr;
                if (prepared == nullptr || !prepared->model ||
                    prepared->parameters.empty())
                {
                    session.stage =
                        AutomaticPaintCaptureStage::Failed;
                    return runtime_failure(
                        application::RuntimeContractId::
                            PaintCapture,
                        application::ContractFailureKind::
                            InvalidValue);
                }
                session.model = prepared->model;
                auto fallback_parameters =
                    core::paint_appearance_fallback_parameters(
                        *session.model);
                const auto started =
                    start_automatic_paint_candidate(
                        session,
                        AutomaticPaintCandidatePurpose::FitBaseline,
                        std::move(fallback_parameters));
                if (!started)
                {
                    session.stage =
                        AutomaticPaintCaptureStage::Failed;
                    return std::unexpected(started.error());
                }
                return std::optional<
                    application::CapturedPaintJob>{};
            }
            case AutomaticPaintCaptureStage::EvaluationPending:
            {
                auto completion = paint_appearance_worker_.poll();
                if (!completion)
                {
                    return std::optional<
                        application::CapturedPaintJob>{};
                }
                const auto* evaluated =
                    completion->generation == generation &&
                            completion->result
                        ? std::get_if<application::
                              PaintAppearanceEvaluated>(
                              &*completion->result)
                        : nullptr;
                if (evaluated == nullptr || !session.model)
                {
                    session.stage =
                        AutomaticPaintCaptureStage::Failed;
                    return runtime_failure(
                        application::RuntimeContractId::
                            PaintCapture,
                        application::ContractFailureKind::
                            InvalidValue);
                }
                if (session.candidate_purpose ==
                    AutomaticPaintCandidatePurpose::FitBaseline)
                {
                    auto fit = core::begin_paint_appearance_fit(
                        *session.model,
                        session.parameters,
                        evaluated->evaluation);
                    if (!fit)
                    {
                        session.stage =
                            AutomaticPaintCaptureStage::Failed;
                        return runtime_failure(
                            application::RuntimeContractId::
                                PaintCapture,
                            application::ContractFailureKind::
                                InvalidValue);
                    }
                    session.fit = std::move(*fit);
                }
                else if (
                    session.candidate_purpose ==
                        AutomaticPaintCandidatePurpose::FitTrial &&
                    session.fit)
                {
                    const auto observed =
                        core::observe_paint_appearance_trial(
                            *session.fit,
                            evaluated->evaluation);
                    if (!observed)
                    {
                        session.stage =
                            AutomaticPaintCaptureStage::Failed;
                        return runtime_failure(
                            application::RuntimeContractId::
                                PaintCapture,
                            application::ContractFailureKind::
                                InvalidValue);
                    }
                }
                else
                {
                    session.stage =
                        AutomaticPaintCaptureStage::Failed;
                    return runtime_failure(
                        application::RuntimeContractId::
                            PaintCapture,
                        application::ContractFailureKind::
                            InvalidValue);
                }

                auto trial = core::next_paint_appearance_trial(
                    *session.fit);
                if (!trial)
                {
                    session.stage =
                        AutomaticPaintCaptureStage::Failed;
                    return runtime_failure(
                        application::RuntimeContractId::
                            PaintCapture,
                        application::ContractFailureKind::
                            InvalidValue);
                }
                if (*trial)
                {
                    const auto started =
                        start_automatic_paint_candidate(
                            session,
                            AutomaticPaintCandidatePurpose::FitTrial,
                            std::move((*trial)->parameters));
                    if (!started)
                    {
                        session.stage =
                            AutomaticPaintCaptureStage::Failed;
                        return std::unexpected(started.error());
                    }
                    return std::optional<
                        application::CapturedPaintJob>{};
                }

                auto fitted =
                    core::finish_paint_appearance_fit(*session.fit);
                if (!fitted ||
                    !session.evidence.base_color.pixels ||
                    !session.evidence.final_ldr.pixels)
                {
                    session.stage =
                        AutomaticPaintCaptureStage::Failed;
                    return runtime_failure(
                        application::RuntimeContractId::
                            PaintCapture,
                        application::ContractFailureKind::
                            InvalidValue);
                }
                session.fit_result = std::move(*fitted);
                const auto started =
                    paint_appearance_worker_.start(
                        generation,
                        application::PaintAppearanceResolveWork{
                            session.model,
                            session.evidence.base_color.pixels,
                            session.evidence.final_ldr.pixels,
                            session.fit_result->parameters,
                        });
                if (!started)
                {
                    session.stage =
                        AutomaticPaintCaptureStage::Failed;
                    return runtime_failure(
                        application::RuntimeContractId::
                            PaintCapture,
                        application::ContractFailureKind::
                            ExecutionFailure);
                }
                session.stage =
                    AutomaticPaintCaptureStage::FinalResolvePending;
                return std::optional<
                    application::CapturedPaintJob>{};
            }
            case AutomaticPaintCaptureStage::FinalResolvePending:
            {
                auto completion = paint_appearance_worker_.poll();
                if (!completion)
                {
                    return std::optional<
                        application::CapturedPaintJob>{};
                }
                const auto* resolved =
                    completion->generation == generation &&
                            completion->result
                        ? std::get_if<application::
                              PaintAppearanceResolved>(
                              &*completion->result)
                        : nullptr;
                if (resolved == nullptr ||
                    !resolved->appearances ||
                    !session.fit_result ||
                    resolved->parameters !=
                        session.fit_result->parameters ||
                    resolved->appearances->size() !=
                        static_cast<std::size_t>(
                            session.seed.camera.width) *
                            session.seed.camera.height)
                {
                    session.stage =
                        AutomaticPaintCaptureStage::Failed;
                    return runtime_failure(
                        application::RuntimeContractId::
                            PaintCapture,
                        application::ContractFailureKind::
                            InvalidValue);
                }
                session.resolved_appearances =
                    resolved->appearances;
                session.stage =
                    AutomaticPaintCaptureStage::FinalResolved;
                return std::optional<
                    application::CapturedPaintJob>{};
            }
            case AutomaticPaintCaptureStage::FinalResolved:
                // Fitting is complete and every preview is restored, but
                // exact source visibility/surface identity is not installed.
                // No partial automatic appearance may reach planning.
                return runtime_failure(
                    application::RuntimeContractId::PaintCapture,
                    application::ContractFailureKind::InvalidValue);
            case AutomaticPaintCaptureStage::Failed:
                return runtime_failure(
                    application::RuntimeContractId::PaintCapture,
                    application::ContractFailureKind::InvalidValue);
            }
        }
        catch (...)
        {
            automatic_paint_capture_->stage =
                automatic_paint_capture_->preview_mutated
                    ? AutomaticPaintCaptureStage::RestorePending
                    : AutomaticPaintCaptureStage::Failed;
            return runtime_failure(
                application::RuntimeContractId::PaintCapture,
                application::ContractFailureKind::
                    ExecutionFailure);
        }
        return runtime_failure(
            application::RuntimeContractId::PaintCapture,
            application::ContractFailureKind::InvalidValue);
    }

    auto cancel_automatic_paint_capture(
        application::JobGeneration generation)
        -> std::expected<
            bool,
            application::RuntimeExecutionError>
    {
        if (!IsInGameThreadRaw())
        {
            return std::unexpected(
                application::RuntimeExecutionError{
                    application::RuntimeExecutionErrorCode::
                        WrongThread,
                    std::nullopt,
                });
        }
        if (!automatic_paint_capture_ || generation == 0U ||
            automatic_paint_capture_->generation != generation)
        {
            return runtime_failure(
                application::RuntimeContractId::PaintCapture,
                application::ContractFailureKind::StaleObject);
        }
        try
        {
            const auto stage = automatic_paint_capture_->stage;
            if (stage ==
                    AutomaticPaintCaptureStage::GeometryPending ||
                stage ==
                    AutomaticPaintCaptureStage::
                        PreliminaryModelPending ||
                stage ==
                    AutomaticPaintCaptureStage::CandidatePending ||
                stage ==
                    AutomaticPaintCaptureStage::TargetE0Pending ||
                stage ==
                    AutomaticPaintCaptureStage::FinalModelPending ||
                stage ==
                    AutomaticPaintCaptureStage::EvaluationPending ||
                stage ==
                    AutomaticPaintCaptureStage::FinalResolvePending)
            {
                const auto requested =
                    paint_appearance_worker_.request_cancel(
                        generation);
                if (requested ==
                        application::
                            PaintAppearanceWorkCancelResult::
                                StaleGeneration ||
                    requested ==
                        application::
                            PaintAppearanceWorkCancelResult::Idle)
                {
                    return runtime_failure(
                        application::RuntimeContractId::
                            PaintCapture,
                        application::ContractFailureKind::
                            ExecutionFailure);
                }
                if (!paint_appearance_worker_.poll())
                {
                    return false;
                }
            }
            if (automatic_paint_capture_->preview_mutated)
            {
                if (!automatic_paint_capture_->preview_snapshot)
                {
                    return runtime_failure(
                        application::RuntimeContractId::
                            PaintCapture,
                        application::ContractFailureKind::
                            InvalidValue);
                }
                const auto restored = restore_preview(
                    *automatic_paint_capture_->preview_snapshot);
                if (!restored)
                {
                    return std::unexpected(restored.error());
                }
                automatic_paint_capture_->preview_mutated =
                    false;
            }
            automatic_paint_capture_.reset();
            return true;
        }
        catch (...)
        {
            return runtime_failure(
                application::RuntimeContractId::PaintCapture,
                application::ContractFailureKind::
                    ExecutionFailure);
        }
    }

    auto capture_image_paint(core::BodyProfile body)
        -> std::expected<
            application::CapturedImagePaintJob,
            application::RuntimeExecutionError>
    {
        if (!IsInGameThreadRaw())
        {
            return std::unexpected(
                application::RuntimeExecutionError{
                    application::RuntimeExecutionErrorCode::
                        WrongThread,
                    std::nullopt,
                });
        }
        try
        {
            const auto pair = image_paint_profiles_
                                  ? image_paint_profiles_->find(body)
                                  : nullptr;
            if (!pair)
            {
                return runtime_failure(
                    application::RuntimeContractId::
                        ImagePaintMeshProfile,
                    application::ContractFailureKind::
                        MissingObject);
            }
            if (pair->unreal_asset_path.empty() ||
                pair->sampling.identity.export_name.empty())
            {
                return runtime_failure(
                    application::RuntimeContractId::
                        ImagePaintMeshProfile,
                    application::ContractFailureKind::
                        InvalidValue);
            }

            auto contracts =
                std::optional<ImagePaintContracts>{};
            auto bound = std::optional<BoundFrame>{};
            auto active = std::optional<ActiveFrame>{};
            {
                const auto lock = std::scoped_lock{mutex_};
                if (detaching_)
                {
                    return runtime_failure(
                        application::RuntimeContractId::
                            ImagePaintMeshProfile,
                        application::ContractFailureKind::
                            StaleObject);
                }
                contracts = image_paint_contracts_;
                bound = bound_frame_;
                active = active_frame_;
            }
            if (!contracts || !bound || !active ||
                bound->identity != active->identity ||
                !bound->identity.valid())
            {
                return runtime_failure(
                    application::RuntimeContractId::
                        ImagePaintMeshProfile,
                    application::ContractFailureKind::
                        MissingObject);
            }
            auto* world = bound->world.Get();
            auto* pawn = bound->pawn.Get();
            auto* component = bound->component.Get();
            if (!object_is_live(
                    component,
                    contracts->runtime_paintable_class) ||
                !object_is_live(pawn, contracts->pawn_class) ||
                !object_is_live(world, contracts->world_class) ||
                component->GetWorld() != world)
            {
                return runtime_failure(
                    application::RuntimeContractId::
                        ImagePaintMeshProfile,
                    application::ContractFailureKind::
                        StaleObject);
            }

            auto* mesh = read_weak_object(
                contracts->target_mesh_component,
                component);
            if (!object_is_live(
                    mesh,
                    contracts->skinned_mesh_component_class) ||
                mesh->GetWorld() != world ||
                !outer_chain_contains(mesh, pawn))
            {
                return runtime_failure(
                    application::RuntimeContractId::
                        ImagePaintMeshProfile,
                    mesh == nullptr
                        ? application::ContractFailureKind::
                              MissingObject
                        : application::ContractFailureKind::
                              WrongClass);
            }
            auto* asset = read_object(
                contracts->skinned_asset,
                mesh);
            if (!object_is_live(
                    asset,
                    contracts->skinned_asset_class))
            {
                return runtime_failure(
                    application::RuntimeContractId::
                        ImagePaintMeshProfile,
                    asset == nullptr
                        ? application::ContractFailureKind::
                              MissingObject
                        : application::ContractFailureKind::
                              WrongClass);
            }
            if (RC::to_string(asset->GetPathName()) !=
                    pair->unreal_asset_path ||
                RC::to_string(asset->GetName()) !=
                    pair->sampling.identity.export_name)
            {
                return runtime_failure(
                    application::RuntimeContractId::
                        ImagePaintMeshProfile,
                    application::ContractFailureKind::
                        InvalidValue);
            }

            {
                const auto lock = std::scoped_lock{mutex_};
                if (detaching_ || !bound_frame_ ||
                    !active_frame_ ||
                    bound_frame_->identity != bound->identity ||
                    bound_frame_->component_identity !=
                        bound->component_identity ||
                    bound_frame_->component_generation !=
                        bound->component_generation ||
                    bound_frame_->component.Get() != component ||
                    active_frame_->identity != active->identity)
                {
                    return runtime_failure(
                        application::RuntimeContractId::
                            ImagePaintMeshProfile,
                        application::ContractFailureKind::
                            StaleObject);
                }
            }
            return application::CapturedImagePaintJob{
                application::RuntimeObjectHandle{
                    bound->component_identity,
                    bound->component_generation,
                },
                pair->sampling,
                pair->image,
                core::replication_pacing_plan({}),
            };
        }
        catch (...)
        {
            return runtime_failure(
                application::RuntimeContractId::
                    ImagePaintMeshProfile,
                application::ContractFailureKind::
                    ExecutionFailure);
        }
    }

    auto capture_preview(
        application::RuntimeObjectHandle component_handle)
        -> std::expected<
            application::PaintPreviewSnapshot,
            application::RuntimeExecutionError>
    {
        if (!IsInGameThreadRaw())
        {
            return std::unexpected(
                application::RuntimeExecutionError{
                    application::RuntimeExecutionErrorCode::
                        WrongThread,
                    std::nullopt,
                });
        }
        try
        {
            const auto target =
                preview_target(component_handle);
            if (!target)
            {
                return std::unexpected(target.error());
            }

            auto albedo = export_channel(
                target->component,
                target->contracts.export_channel_to_bytes,
                RuntimePaintChannel::Albedo);
            if (!albedo)
            {
                return std::unexpected(albedo.error());
            }
            auto packed_pbr = export_channel(
                target->component,
                target->contracts.export_channel_to_bytes,
                RuntimePaintChannel::Emissive);
            if (!packed_pbr)
            {
                return std::unexpected(packed_pbr.error());
            }

            const auto dimension =
                infer_paint_texture_dimension(
                    *albedo,
                    *packed_pbr);
            if (!dimension)
            {
                return runtime_failure(
                    application::RuntimeContractId::
                        TextureMutation,
                    application::ContractFailureKind::
                        InvalidValue);
            }
            if (!preview_target(component_handle))
            {
                return runtime_failure(
                    application::RuntimeContractId::
                        TextureMutation,
                    application::ContractFailureKind::
                        StaleObject);
            }

            return application::PaintPreviewSnapshot{
                component_handle,
                application::PaintTextureImage{
                    *dimension,
                    std::make_shared<
                        const std::vector<std::byte>>(
                        std::move(*albedo)),
                    std::make_shared<
                        const std::vector<std::byte>>(
                        std::move(*packed_pbr)),
                },
            };
        }
        catch (...)
        {
            return runtime_failure(
                application::RuntimeContractId::
                    TextureMutation,
                application::ContractFailureKind::
                    ExecutionFailure);
        }
    }

    auto apply_preview(
        application::RuntimeObjectHandle component_handle,
        const application::PaintTextureImage& image)
        -> std::expected<
            void,
            application::RuntimeExecutionError>
    {
        if (!IsInGameThreadRaw())
        {
            return std::unexpected(
                application::RuntimeExecutionError{
                    application::RuntimeExecutionErrorCode::
                        WrongThread,
                    std::nullopt,
                });
        }
        try
        {
            if (image.albedo_rgba == nullptr ||
                image.packed_pbr_rgba == nullptr)
            {
                return runtime_failure(
                    application::RuntimeContractId::
                        TextureMutation,
                    application::ContractFailureKind::
                        InvalidValue);
            }
            const auto dimension =
                infer_paint_texture_dimension(
                    *image.albedo_rgba,
                    *image.packed_pbr_rgba);
            if (!dimension || *dimension != image.dimension)
            {
                return runtime_failure(
                    application::RuntimeContractId::
                        TextureMutation,
                    application::ContractFailureKind::
                        InvalidValue);
            }

            auto target = preview_target(component_handle);
            if (!target)
            {
                return std::unexpected(target.error());
            }
            const auto albedo = import_channel_verified(
                target->component,
                target->contracts.import_channel_from_bytes,
                target->contracts.export_channel_to_bytes,
                RuntimePaintChannel::Albedo,
                RuntimePaintChannel::Albedo,
                *image.albedo_rgba);
            if (!albedo)
            {
                return albedo;
            }

            target = preview_target(component_handle);
            if (!target)
            {
                return std::unexpected(target.error());
            }
            const auto packed_pbr = import_channel_verified(
                target->component,
                target->contracts.import_channel_from_bytes,
                target->contracts.export_channel_to_bytes,
                RuntimePaintChannel::
                    AlbedoMetallicRoughnessEmissive,
                RuntimePaintChannel::Emissive,
                *image.packed_pbr_rgba);
            if (!packed_pbr)
            {
                return packed_pbr;
            }
            if (!preview_target(component_handle))
            {
                return runtime_failure(
                    application::RuntimeContractId::
                        TextureMutation,
                    application::ContractFailureKind::
                        StaleObject);
            }
            return {};
        }
        catch (...)
        {
            return runtime_failure(
                application::RuntimeContractId::
                    TextureMutation,
                application::ContractFailureKind::
                    ExecutionFailure);
        }
    }

    auto restore_preview(
        const application::PaintPreviewSnapshot& snapshot)
        -> std::expected<
            void,
            application::RuntimeExecutionError>
    {
        return apply_preview(
            snapshot.component,
            snapshot.original);
    }

    auto create_canvas_texture(
        const product_ui::ImageEditorTextureUpload& upload)
        -> std::expected<
            ui::CanvasTextureHandle,
            product_ui::ImageEditorTextureRuntimeError>
    {
        if (!IsInGameThreadRaw())
        {
            return texture_failure("texture.wrong_thread");
        }
        if (upload.identity.empty() ||
            upload.identity.size() > 256U ||
            upload.revision == 0U ||
            upload.width == 0U ||
            upload.height == 0U ||
            !upload.encoded_png)
        {
            return texture_failure("texture.invalid_upload");
        }
        const auto inspected =
            core::inspect_canonical_png_rgba8(
                *upload.encoded_png);
        if (!inspected ||
            inspected->width != upload.width ||
            inspected->height != upload.height)
        {
            return texture_failure("texture.invalid_upload");
        }

        try
        {
            auto contracts = std::optional<CanvasContracts>{};
            auto hud = std::optional<HudContracts>{};
            auto active = std::optional<ActiveFrame>{};
            {
                const auto lock = std::scoped_lock{mutex_};
                if (detaching_)
                {
                    return texture_failure(
                        "texture.runtime_stopping");
                }
                contracts = canvas_contracts_;
                hud = hud_contracts_;
                active = active_frame_;
            }
            if (!contracts || !hud || !active ||
                !object_is_live(
                    active->world,
                    hud->world_class) ||
                !object_is_live(
                    contracts->kismet_rendering_library_cdo,
                    contracts
                        ->kismet_rendering_library_class))
            {
                return texture_failure(
                    "texture.runtime_unavailable");
            }
            auto parameters = encode_texture_import(
                active->world,
                *upload.encoded_png);
            if (!parameters)
            {
                return texture_failure(
                    "texture.invalid_upload");
            }
            contracts->kismet_rendering_library_cdo
                ->ProcessEvent(
                    contracts->import_buffer_as_texture2d,
                    &*parameters);
            auto* texture = static_cast<UObject*>(
                parameters->return_value);
            if (!object_is_live_exact(
                    texture,
                    contracts->texture2d_class) ||
                texture->IsRootSet())
            {
                return texture_failure(
                    "texture.import_failed");
            }
            auto rooted = RootedObjectGuard{texture};
            texture->SetRootSet();
            if (!texture->IsRootSet())
            {
                return texture_failure(
                    "texture.root_failed");
            }

            auto handle = ui::CanvasTextureHandle{};
            {
                const auto lock = std::scoped_lock{mutex_};
                if (detaching_ || !active_frame_ ||
                    active_frame_->identity !=
                        active->identity ||
                    active_frame_->world != active->world ||
                    canvas_textures_.size() >=
                        MaximumOwnedCanvasTextures ||
                    next_texture_handle_ == 0U ||
                    next_texture_handle_ ==
                        std::numeric_limits<
                            std::uint64_t>::max())
                {
                    return texture_failure(
                        "texture.registry_unavailable");
                }
                handle.identity = next_texture_handle_++;
                canvas_textures_.emplace(
                    handle.identity,
                    OwnedCanvasTexture{
                        FWeakObjectPtr{texture},
                        object_identity(texture),
                    });
            }
            rooted.dismiss();
            return handle;
        }
        catch (...)
        {
            return texture_failure(
                "texture.execution_failed");
        }
    }

    auto release_canvas_texture(ui::CanvasTextureHandle handle)
        -> std::expected<
            void,
            product_ui::ImageEditorTextureRuntimeError>
    {
        if (!IsInGameThreadRaw())
        {
            return texture_failure("texture.wrong_thread");
        }
        if (handle.identity == 0U)
        {
            return texture_failure("texture.invalid_handle");
        }
        try
        {
            auto contracts = std::optional<CanvasContracts>{};
            auto owned = std::optional<OwnedCanvasTexture>{};
            {
                const auto lock = std::scoped_lock{mutex_};
                contracts = canvas_contracts_;
                const auto found =
                    canvas_textures_.find(handle.identity);
                if (found != canvas_textures_.end())
                {
                    owned = found->second;
                }
            }
            if (!contracts || !owned)
            {
                return texture_failure("texture.invalid_handle");
            }
            auto* texture = owned->object.Get();
            if (!object_is_live_exact(
                    texture,
                    contracts->texture2d_class) ||
                object_identity(texture) !=
                    owned->object_identity ||
                !texture->IsRootSet())
            {
                return texture_failure("texture.stale_handle");
            }
            texture->ClearRootSet();
            if (texture->IsRootSet())
            {
                return texture_failure(
                    "texture.release_failed");
            }
            const auto lock = std::scoped_lock{mutex_};
            const auto found =
                canvas_textures_.find(handle.identity);
            if (found == canvas_textures_.end() ||
                found->second.object_identity !=
                    owned->object_identity)
            {
                return texture_failure("texture.stale_handle");
            }
            canvas_textures_.erase(found);
            return {};
        }
        catch (...)
        {
            return texture_failure(
                "texture.execution_failed");
        }
    }

    auto render_canvas(
        const application::HudFrameIdentity& identity,
        const ui::CanvasFrame& frame)
        -> std::expected<
            void,
            product_ui::ProductUiFrameRuntimeError>
    {
        if (!IsInGameThreadRaw())
        {
            return canvas_failure("canvas.wrong_thread");
        }
        try
        {
            auto contracts = std::optional<CanvasContracts>{};
            auto active = std::optional<ActiveFrame>{};
            {
                const auto lock = std::scoped_lock{mutex_};
                contracts = canvas_contracts_;
                active = active_frame_;
            }
            if (!contracts || !active ||
                active->identity != identity ||
                !object_is_live(
                    active->canvas,
                    contracts->canvas_class))
            {
                return canvas_failure("canvas.stale_frame");
            }
            if (active->viewport_width <= 0 ||
                active->viewport_height <= 0 ||
                !std::isfinite(frame.viewport.width) ||
                !std::isfinite(frame.viewport.height) ||
                !std::isfinite(frame.viewport.dpi_scale) ||
                frame.viewport.width !=
                    static_cast<double>(
                        active->viewport_width) ||
                frame.viewport.height !=
                    static_cast<double>(
                        active->viewport_height) ||
                frame.viewport.dpi_scale <= 0.0 ||
                frame.viewport.dpi_scale > 8.0 ||
                frame.primitives.size() >
                    ui::MaximumCanvasPrimitives)
            {
                return canvas_failure("canvas.invalid_frame");
            }

            using EncodedCall = std::variant<
                K2DrawLineParametersAbi,
                K2DrawTextureParametersAbi,
                OwnedCanvasTextCall>;
            auto calls = std::vector<EncodedCall>{};
            calls.reserve(frame.primitives.size());
            for (const auto& primitive : frame.primitives)
            {
                if (const auto* line =
                        std::get_if<
                            ui::CanvasLinePrimitive>(
                            &primitive))
                {
                    const auto encoded = encode_canvas_line(
                        CanvasLineInput{
                            {
                                line->start.x,
                                line->start.y,
                            },
                            {
                                line->end.x,
                                line->end.y,
                            },
                            {
                                line->color.red,
                                line->color.green,
                                line->color.blue,
                                line->color.alpha,
                            },
                            line->thickness,
                        });
                    if (!encoded)
                    {
                        return canvas_failure(
                            "canvas.invalid_line");
                    }
                    calls.emplace_back(*encoded);
                    continue;
                }
                if (const auto* box =
                        std::get_if<
                            ui::CanvasBoxPrimitive>(
                            &primitive))
                {
                    const auto encoded =
                        encode_canvas_filled_box(
                            CanvasBoxInput{
                                {
                                    box->rect.x,
                                    box->rect.y,
                                    box->rect.width,
                                    box->rect.height,
                                },
                                {
                                    box->color.red,
                                    box->color.green,
                                    box->color.blue,
                                    box->color.alpha,
                                },
                            });
                    if (!encoded)
                    {
                        return canvas_failure(
                            "canvas.invalid_box");
                    }
                    calls.emplace_back(*encoded);
                    continue;
                }

                if (const auto* texture =
                        std::get_if<
                            ui::CanvasTexturePrimitive>(
                            &primitive))
                {
                    auto owned =
                        std::optional<OwnedCanvasTexture>{};
                    {
                        const auto lock =
                            std::scoped_lock{mutex_};
                        const auto found =
                            canvas_textures_.find(
                                texture->texture.identity);
                        if (found != canvas_textures_.end())
                        {
                            owned = found->second;
                        }
                    }
                    auto* runtime_texture =
                        owned ? owned->object.Get() : nullptr;
                    if (!owned ||
                        !object_is_live_exact(
                            runtime_texture,
                            contracts->texture2d_class) ||
                        object_identity(runtime_texture) !=
                            owned->object_identity ||
                        !runtime_texture->IsRootSet())
                    {
                        return canvas_failure(
                            "canvas.texture_stale");
                    }
                    const auto encoded = encode_canvas_texture(
                        CanvasTextureInput{
                            runtime_texture,
                            {
                                texture->rect.x,
                                texture->rect.y,
                                texture->rect.width,
                                texture->rect.height,
                            },
                            {
                                texture->uv.left,
                                texture->uv.top,
                                texture->uv.right,
                                texture->uv.bottom,
                            },
                            {
                                texture->tint.red,
                                texture->tint.green,
                                texture->tint.blue,
                                texture->tint.alpha,
                            },
                        });
                    if (!encoded)
                    {
                        return canvas_failure(
                            "canvas.invalid_texture");
                    }
                    calls.emplace_back(*encoded);
                    continue;
                }

                if (const auto* text =
                        std::get_if<
                            ui::CanvasTextPrimitive>(
                            &primitive))
                {
                    auto* game_font =
                        contracts->game_font.Get();
                    if (!object_is_live_exact(
                            game_font,
                            contracts->font_class) ||
                        game_font->GetPathName() !=
                            RC::StringType{
                                GameFontAssetPath})
                    {
                        return canvas_failure(
                            "canvas.font_stale");
                    }
                    auto storage =
                        encode_canvas_utf16(text->utf8);
                    if (!storage)
                    {
                        return canvas_failure(
                            "canvas.invalid_text");
                    }
                    auto call = OwnedCanvasTextCall{
                        std::move(*storage),
                        {},
                    };
                    const auto count =
                        static_cast<std::int32_t>(
                            call.storage.size());
                    const auto encoded = encode_canvas_text(
                        CanvasTextInput{
                            game_font,
                            RuntimeStringAbi{
                                call.storage.data(),
                                count,
                                count,
                            },
                            {
                                text->anchor.x,
                                text->anchor.y,
                            },
                            {
                                text->color.red,
                                text->color.green,
                                text->color.blue,
                                text->color.alpha,
                            },
                            text->scale,
                        });
                    if (!encoded)
                    {
                        return canvas_failure(
                            "canvas.invalid_text");
                    }
                    call.parameters = *encoded;
                    calls.emplace_back(std::move(call));
                    continue;
                }

                return canvas_failure(
                    "canvas.resource_unbound");
            }

            {
                const auto lock = std::scoped_lock{mutex_};
                if (detaching_ || !active_frame_ ||
                    active_frame_->identity != identity ||
                    active_frame_->canvas != active->canvas)
                {
                    return canvas_failure(
                        "canvas.stale_frame");
                }
            }
            for (auto& call : calls)
            {
                std::visit(
                    [&](auto& parameters)
                    {
                        using Parameters =
                            std::decay_t<
                                decltype(parameters)>;
                        if constexpr (
                            std::is_same_v<
                                Parameters,
                                K2DrawLineParametersAbi>)
                        {
                            active->canvas->ProcessEvent(
                                contracts->draw_line,
                                &parameters);
                        }
                        else if constexpr (
                            std::is_same_v<
                                Parameters,
                                K2DrawTextureParametersAbi>)
                        {
                            active->canvas->ProcessEvent(
                                contracts->draw_texture,
                                &parameters);
                        }
                        else
                        {
                            active->canvas->ProcessEvent(
                                contracts->draw_text,
                                &parameters.parameters);
                        }
                    },
                    call);
            }
            return {};
        }
        catch (...)
        {
            return canvas_failure("canvas.execution_failed");
        }
    }

    auto capture_esp_frame()
        -> std::expected<
            application::CapturedEspFrame,
            application::RuntimeExecutionError>
    {
        if (!IsInGameThreadRaw())
        {
            return std::unexpected(
                application::RuntimeExecutionError{
                    application::RuntimeExecutionErrorCode::
                        WrongThread,
                });
        }
        try
        {
            auto active = std::optional<ActiveFrame>{};
            auto hud = std::optional<HudContracts>{};
            auto cached_directory =
                std::optional<EspAvatarDirectory>{};
            {
                const auto lock = std::scoped_lock{mutex_};
                active = active_frame_;
                hud = hud_contracts_;
                cached_directory = esp_avatar_directory_;
            }
            if (!active || !hud ||
                !active->identity.valid() ||
                active->viewport_width < 1 ||
                active->viewport_height < 1)
            {
                return runtime_failure(
                    application::RuntimeContractId::EspFrame,
                    application::ContractFailureKind::StaleObject);
            }

            const auto resolved = resolve_esp_contracts();
            if (!resolved)
            {
                return std::unexpected(resolved.error());
            }
            const auto& contracts = *resolved;
            if (!object_is_live(
                    active->world,
                    contracts.world_class) ||
                !object_is_live(
                    active->controller,
                    contracts.player_controller_class) ||
                !object_is_live(
                    active->hud,
                    hud->hud_class) ||
                !object_is_live(
                    active->canvas,
                    hud->canvas_class) ||
                object_identity(active->world) !=
                    active->identity.world ||
                object_identity(active->controller) !=
                    active->identity.controller ||
                object_identity(active->hud) !=
                    active->identity.hud ||
                object_identity(active->canvas) !=
                    active->identity.canvas ||
                active->hud->GetWorld() != active->world ||
                read_object(
                    hud->player_owner,
                    active->hud) != active->controller ||
                read_object(
                    hud->canvas,
                    active->hud) != active->canvas)
            {
                return runtime_failure(
                    application::RuntimeContractId::EspFrame,
                    application::ContractFailureKind::StaleObject);
            }

            auto* game_state = read_object(
                contracts.world_game_state,
                active->world);
            auto* local_player_state = read_object(
                contracts.controller_player_state,
                active->controller);
            auto* camera_manager = read_object(
                contracts.player_camera_manager,
                active->controller);
            if (!object_is_live_exact(
                    game_state,
                    contracts.game_state_cleon_class) ||
                !object_is_live(
                    local_player_state,
                    contracts.player_state_class) ||
                !object_is_live(
                    camera_manager,
                    contracts.player_camera_manager_class) ||
                game_state->GetWorld() != active->world ||
                camera_manager->GetWorld() != active->world)
            {
                return runtime_failure(
                    application::RuntimeContractId::EspFrame,
                    application::ContractFailureKind::MissingObject);
            }

            const auto player_states = read_object_array(
                contracts.player_array,
                game_state,
                contracts.player_state_class);
            const auto survivor_states = read_object_array(
                contracts.live_survivor_player_states,
                game_state,
                contracts.player_state_cleon_class);
            const auto hunter_states = read_object_array(
                contracts.hunter_player_states,
                game_state,
                contracts.player_state_cleon_class);
            if (!player_states || !survivor_states ||
                !hunter_states ||
                local_player_state->GetWorld() !=
                    active->world ||
                std::ranges::any_of(
                    *player_states,
                    [&](UObject* player_state)
                    {
                        return player_state->GetWorld() !=
                               active->world;
                    }))
            {
                return runtime_failure(
                    application::RuntimeContractId::EspFrame,
                    application::ContractFailureKind::InvalidValue);
            }
            const auto player_span = std::span<UObject* const>{
                player_states->data(),
                player_states->size()};
            for (auto* player_state : *survivor_states)
            {
                if (!contains_object(player_span, player_state) ||
                    contains_object(
                        std::span<UObject* const>{
                            hunter_states->data(),
                            hunter_states->size()},
                        player_state))
                {
                    return runtime_failure(
                        application::RuntimeContractId::EspFrame,
                        application::ContractFailureKind::
                            InvalidValue);
                }
            }
            for (auto* player_state : *hunter_states)
            {
                if (!contains_object(player_span, player_state))
                {
                    return runtime_failure(
                        application::RuntimeContractId::EspFrame,
                        application::ContractFailureKind::
                            InvalidValue);
                }
            }

            const auto view = capture_calibrated_esp_view(
                *active,
                contracts,
                application::RuntimeContractId::EspFrame);
            if (!view)
            {
                return std::unexpected(view.error());
            }

            struct Subject
            {
                UObject* player_state{};
                UObject* avatar{};
                core::EspRole roster_role{
                    core::EspRole::Unknown};
                core::EspRole current_pawn_role{
                    core::EspRole::Unknown};
                bool needs_role_avatar{};
            };
            auto subjects = std::vector<Subject>{};
            auto unresolved =
                std::vector<EspUnresolvedAvatar>{};
            subjects.reserve(player_states->size());
            unresolved.reserve(player_states->size());
            const auto survivor_span = std::span<UObject* const>{
                survivor_states->data(),
                survivor_states->size()};
            const auto hunter_span = std::span<UObject* const>{
                hunter_states->data(),
                hunter_states->size()};
            for (auto* player_state : *player_states)
            {
                if (player_state == local_player_state ||
                    !object_is_live(
                        player_state,
                        contracts.player_state_cleon_class))
                {
                    continue;
                }
                const auto is_survivor =
                    contains_object(
                        survivor_span,
                        player_state);
                const auto is_hunter =
                    contains_object(
                        hunter_span,
                        player_state);
                if (is_survivor && is_hunter)
                {
                    return runtime_failure(
                        application::RuntimeContractId::EspFrame,
                        application::ContractFailureKind::
                            InvalidValue);
                }
                auto roster_role =
                    is_survivor
                        ? core::EspRole::Hider
                        : is_hunter
                              ? core::EspRole::Hunter
                              : core::EspRole::Unknown;
                auto* pawn = read_object(
                    contracts.player_state_pawn,
                    player_state);
                if (!object_is_live(pawn, contracts.pawn_class))
                {
                    pawn = nullptr;
                }
                const auto current_role =
                    classify_esp_pawn(pawn, contracts);
                const auto spectator =
                    contracts.is_spectator
                        ->GetPropertyValueInContainer(
                            player_state) ||
                    contracts.only_spectator
                        ->GetPropertyValueInContainer(
                            player_state);
                if (roster_role == core::EspRole::Unknown &&
                    (spectator ||
                     current_role ==
                         core::EspRole::Spectator))
                {
                    roster_role = core::EspRole::Spectator;
                }
                if (roster_role == core::EspRole::Unknown &&
                    current_role == core::EspRole::Unknown)
                {
                    continue;
                }
                const auto active_roster_role =
                    roster_role == core::EspRole::Hider ||
                    roster_role == core::EspRole::Hunter;
                const auto needs_role_avatar =
                    active_roster_role &&
                    (current_role == core::EspRole::Unknown ||
                     current_role ==
                         core::EspRole::Spectator);
                subjects.push_back(Subject{
                    player_state,
                    needs_role_avatar ? nullptr : pawn,
                    roster_role,
                    current_role,
                    needs_role_avatar,
                });
                if (needs_role_avatar)
                {
                    unresolved.push_back(
                        EspUnresolvedAvatar{
                            player_state,
                            roster_role,
                        });
                }
            }

            const auto now_ms = steady_milliseconds();
            const auto same_directory_scope =
                cached_directory &&
                cached_directory->world_identity ==
                    active->identity.world &&
                cached_directory->hud_identity ==
                    active->identity.hud;
            const auto cached_avatar =
                [&](const EspUnresolvedAvatar& value)
                    -> UObject*
            {
                if (!same_directory_scope)
                {
                    return nullptr;
                }
                for (const auto& binding :
                     cached_directory->bindings)
                {
                    auto* cached_player_state =
                        binding.player_state.Get();
                    auto* avatar = binding.avatar.Get();
                    const auto candidate_role =
                        classify_esp_pawn(
                            avatar,
                            contracts);
                    const auto candidate_live =
                        cached_player_state ==
                            value.player_state &&
                        avatar != nullptr &&
                        avatar->GetWorld() ==
                            active->world &&
                        read_object(
                            contracts.pawn_player_state,
                            avatar) ==
                            value.player_state;
                    if (cached_player_state ==
                            value.player_state &&
                        core::esp_cached_avatar_binding_usable(
                            binding.avatar_resolved,
                            same_directory_scope,
                            true,
                            candidate_live,
                            value.role,
                            candidate_role))
                    {
                        return avatar;
                    }
                }
                return nullptr;
            };
            const auto invalid_cached_binding =
                std::ranges::any_of(
                    unresolved,
                    [&](const EspUnresolvedAvatar& value)
                    {
                        if (!same_directory_scope)
                        {
                            return false;
                        }
                        const auto player_binding =
                            std::ranges::find_if(
                                cached_directory->bindings,
                                [&](const EspAvatarBinding& candidate)
                                {
                                    return candidate.player_state.Get() ==
                                           value.player_state;
                                });
                        if (player_binding ==
                                cached_directory->bindings.end() ||
                            player_binding->role != value.role)
                        {
                            return true;
                        }
                        return player_binding->avatar_resolved &&
                               cached_avatar(value) == nullptr;
                    });
            const auto refresh_directory =
                should_refresh_esp_capture_directory(
                    !unresolved.empty(),
                    same_directory_scope,
                    invalid_cached_binding,
                    now_ms,
                    cached_directory
                        ? cached_directory->refresh_ms
                        : 0U,
                    EspAvatarRefreshIntervalMs);
            if (refresh_directory)
            {
                auto bindings = build_esp_avatar_directory(
                    active->world,
                    contracts,
                    std::span<const EspUnresolvedAvatar>{
                        unresolved});
                if (!bindings)
                {
                    return runtime_failure(
                        application::RuntimeContractId::EspFrame,
                        application::ContractFailureKind::
                            InvalidValue);
                }
                cached_directory = EspAvatarDirectory{
                    active->identity.world,
                    active->identity.hud,
                    now_ms,
                    std::move(*bindings),
                };
                {
                    const auto lock =
                        std::scoped_lock{mutex_};
                    if (!active_frame_ ||
                        active_frame_->identity !=
                            active->identity)
                    {
                        return runtime_failure(
                            application::RuntimeContractId::
                                EspFrame,
                            application::ContractFailureKind::
                                StaleObject);
                    }
                    esp_avatar_directory_ = cached_directory;
                }
            }

            for (auto& subject : subjects)
            {
                if (!subject.needs_role_avatar)
                {
                    continue;
                }
                auto* role_avatar = static_cast<UObject*>(nullptr);
                if (cached_directory)
                {
                    for (const auto& binding :
                         cached_directory->bindings)
                    {
                        if (binding.player_state.Get() ==
                                subject.player_state &&
                            binding.role ==
                                subject.roster_role)
                        {
                            role_avatar = binding.avatar.Get();
                            break;
                        }
                    }
                }
                const auto role_avatar_role =
                    classify_esp_pawn(
                        role_avatar,
                        contracts);
                const auto same_player_state =
                    role_avatar != nullptr &&
                    role_avatar->GetWorld() == active->world &&
                    read_object(
                        contracts.pawn_player_state,
                        role_avatar) == subject.player_state;
                if (core::select_esp_pawn_source(
                        subject.roster_role,
                        subject.current_pawn_role,
                        role_avatar_role,
                        same_player_state) ==
                    core::EspPawnSource::RoleRoster)
                {
                    subject.avatar = role_avatar;
                    subject.current_pawn_role =
                        role_avatar_role;
                }
            }

            auto targets =
                std::vector<core::EspTargetCapture>{};
            targets.reserve(subjects.size());
            for (const auto& subject : subjects)
            {
                auto* avatar = subject.avatar;
                if (avatar != nullptr &&
                    (avatar->GetWorld() != active->world ||
                     read_object(
                         contracts.pawn_player_state,
                         avatar) != subject.player_state))
                {
                    avatar = nullptr;
                }
                auto origin =
                    std::optional<core::EspWorldPoint>{};
                auto capsule_samples =
                    std::vector<core::EspWorldPoint>{};
                auto skeleton =
                    std::optional<core::EspSkeletonPose>{};
                const auto role =
                    core::resolve_esp_target_role(
                        subject.roster_role,
                        subject.current_pawn_role);
                if (avatar != nullptr &&
                    (role == core::EspRole::Hider ||
                     role == core::EspRole::Hunter))
                {
                    auto* capsule = read_object(
                        contracts.character_capsule,
                        avatar);
                    if (object_is_live(
                            capsule,
                            contracts.capsule_component_class) &&
                        outer_chain_contains(capsule, avatar))
                    {
                        const auto location = call_esp_vector(
                            capsule,
                            contracts.k2_get_component_location);
                        const auto samples = sample_esp_capsule(
                            location,
                            call_esp_rotator(
                                capsule,
                                contracts
                                    .k2_get_component_rotation),
                            call_esp_float(
                                capsule,
                                contracts
                                    .get_scaled_capsule_radius),
                            call_esp_float(
                                capsule,
                                contracts
                                    .get_scaled_capsule_half_height));
                        if (samples)
                        {
                            origin = core::EspWorldPoint{
                                location.x,
                                location.y,
                                location.z,
                            };
                            capsule_samples = *samples;
                        }
                    }
                    if (image_paint_profiles_)
                    {
                        skeleton = capture_esp_skeleton_pose(
                            avatar,
                            active->world,
                            contracts,
                            *image_paint_profiles_);
                    }
                }
                targets.push_back(core::EspTargetCapture{
                    object_identity(subject.player_state),
                    avatar == nullptr
                        ? 0U
                        : object_identity(avatar),
                    subject.roster_role,
                    subject.current_pawn_role,
                    read_esp_display_name(
                        contracts.custom_player_name,
                        subject.player_state),
                    origin,
                    std::move(capsule_samples),
                    std::move(skeleton),
                });
            }

            {
                const auto lock = std::scoped_lock{mutex_};
                if (!active_frame_ ||
                    active_frame_->identity !=
                        active->identity ||
                    active_frame_->hud != active->hud ||
                    active_frame_->canvas != active->canvas)
                {
                    return runtime_failure(
                        application::RuntimeContractId::EspFrame,
                        application::ContractFailureKind::
                            StaleObject);
                }
            }
            return application::CapturedEspFrame{
                active->identity,
                *view,
                core::EspViewport{
                    static_cast<double>(
                        active->viewport_width),
                    static_cast<double>(
                        active->viewport_height),
                },
                std::move(targets),
            };
        }
        catch (...)
        {
            return runtime_failure(
                application::RuntimeContractId::EspFrame,
                application::ContractFailureKind::
                    ExecutionFailure);
        }
    }

    auto draw_esp_frame(
        const application::HudFrameIdentity& identity,
        const core::EspPrimitiveFrame& frame)
        -> std::expected<
            void,
            application::RuntimeExecutionError>
    {
        if (!IsInGameThreadRaw())
        {
            return std::unexpected(
                application::RuntimeExecutionError{
                    application::RuntimeExecutionErrorCode::
                        WrongThread,
                });
        }

        auto active = std::optional<ActiveFrame>{};
        {
            const auto lock = std::scoped_lock{mutex_};
            active = active_frame_;
        }
        if (!active || active->identity != identity ||
            active->viewport_width <= 0 ||
            active->viewport_height <= 0)
        {
            return runtime_failure(
                application::RuntimeContractId::EspFrame,
                application::ContractFailureKind::StaleObject);
        }

        const auto encoded = ui::encode_esp_canvas_frame(
            ui::CanvasViewport{
                static_cast<double>(active->viewport_width),
                static_cast<double>(active->viewport_height),
                1.0,
            },
            frame);
        if (!encoded)
        {
            return runtime_failure(
                application::RuntimeContractId::EspFrame,
                application::ContractFailureKind::InvalidValue);
        }

        const auto rendered = render_canvas(identity, *encoded);
        if (!rendered)
        {
            return runtime_failure(
                application::RuntimeContractId::EspFrame,
                application::ContractFailureKind::
                    ExecutionFailure);
        }
        return {};
    }

    auto capture_product_ui_frame(
        const application::HudFrameIdentity& identity)
        -> std::expected<
            product_ui::ProductUiFrameInput,
            product_ui::ProductUiFrameRuntimeError>
    {
        if (!IsInGameThreadRaw())
        {
            return canvas_failure("input.wrong_thread");
        }
        if (!input_queue_)
        {
            return canvas_failure("input.missing_queue");
        }

        try
        {
            auto active = std::optional<ActiveFrame>{};
            {
                const auto lock = std::scoped_lock{mutex_};
                active = active_frame_;
            }
            if (!active || active->identity != identity ||
                active->viewport_width <= 0 ||
                active->viewport_height <= 0)
            {
                return canvas_failure("input.stale_frame");
            }

            const auto foreground = GetForegroundWindow();
            auto window = input_window_;
            const auto cached_window_is_valid =
                is_current_process_unreal_window(window);
            if (!cached_window_is_valid)
            {
                if (!is_current_process_unreal_window(foreground))
                {
                    return canvas_failure(
                        "input.game_window_unavailable");
                }
                window = foreground;
            }

            auto client = RECT{};
            if (!GetClientRect(window, &client))
            {
                return canvas_failure("input.client_rect_failed");
            }
            const auto client_width = client.right - client.left;
            const auto client_height = client.bottom - client.top;
            if (client_width <= 0 || client_height <= 0)
            {
                return canvas_failure("input.invalid_client_rect");
            }

            auto cursor = POINT{};
            if (!GetCursorPos(&cursor) ||
                !ScreenToClient(window, &cursor))
            {
                return canvas_failure("input.pointer_query_failed");
            }
            const auto dpi = GetDpiForWindow(window);
            if (dpi == 0U)
            {
                return canvas_failure("input.dpi_query_failed");
            }

            const auto pointer = pointer_capture_.update(
                product_ui::ProductUiPointerObservation{
                    static_cast<double>(
                        active->viewport_width),
                    static_cast<double>(
                        active->viewport_height),
                    static_cast<double>(client_width),
                    static_cast<double>(client_height),
                    static_cast<double>(cursor.x),
                    static_cast<double>(cursor.y),
                    static_cast<double>(dpi) / 96.0,
                    reinterpret_cast<std::uintptr_t>(window),
                    foreground == window,
                    GetAsyncKeyState(VK_LBUTTON) < 0,
                });
            if (!pointer)
            {
                return canvas_failure(
                    "input.invalid_pointer_observation");
            }
            input_window_ = window;

            if (!pointer->function_key_input_available)
            {
                input_queue_->discard();
                return product_ui::ProductUiFrameInput{
                    pointer->viewport,
                    {},
                    pointer->pointer,
                    {},
                    {},
                    {},
                    false,
                    pointer->owner_window,
                };
            }

            auto batch = input_queue_->drain();
            if (!batch)
            {
                return canvas_failure(
                    batch.error() ==
                            product_ui::ProductUiInputDrainError::
                                EventLimit
                        ? "input.event_limit"
                        : "input.queue_stopped");
            }
            return product_ui::ProductUiFrameInput{
                pointer->viewport,
                {},
                pointer->pointer,
                batch->keyboard,
                std::move(batch->text_edit_events),
                std::move(batch->function_keys),
                true,
                pointer->owner_window,
            };
        }
        catch (...)
        {
            return canvas_failure("input.capture_failed");
        }
    }

private:
    struct PreviewTarget
    {
        PaintContracts contracts{};
        BoundFrame bound{};
        UObject* component{};
    };

    auto preview_target(
        application::RuntimeObjectHandle component_handle)
        -> std::expected<
            PreviewTarget,
            application::RuntimeExecutionError>
    {
        auto contracts = std::optional<PaintContracts>{};
        auto bound = std::optional<BoundFrame>{};
        auto active = std::optional<ActiveFrame>{};
        {
            const auto lock = std::scoped_lock{mutex_};
            contracts = paint_contracts_;
            bound = bound_frame_;
            active = active_frame_;
        }
        if (!contracts || !bound || !active ||
            bound->identity != active->identity ||
            component_handle.identity !=
                bound->component_identity ||
            component_handle.generation !=
                bound->component_generation)
        {
            return runtime_failure(
                application::RuntimeContractId::TextureMutation,
                application::ContractFailureKind::StaleObject);
        }
        auto* component = bound->component.Get();
        if (!object_is_live(
                component,
                contracts->runtime_paintable_class))
        {
            return runtime_failure(
                application::RuntimeContractId::TextureMutation,
                application::ContractFailureKind::StaleObject);
        }
        return PreviewTarget{
            *contracts,
            *bound,
            component,
        };
    }

    static auto pre_hook(
        UnrealScriptFunctionCallableContext&,
        void*) -> void
    {
    }

    static auto post_hook(
        UnrealScriptFunctionCallableContext& context,
        void* custom_data) -> void
    {
        if (custom_data == nullptr)
        {
            return;
        }
        try
        {
            static_cast<Impl*>(custom_data)->dispatch(
                context);
        }
        catch (...)
        {
        }
    }

    auto dispatch(
        UnrealScriptFunctionCallableContext& context) -> void
    {
        auto* hud = context.Context;
        const auto parameters =
            context.GetParams<
                ReceiveDrawHudParametersAbi>();
        auto contracts = std::optional<HudContracts>{};
        auto callback_context = static_cast<void*>(nullptr);
        auto callback = application::HudCallback{};
        auto sequence = std::uint64_t{};
        {
            const auto lock = std::scoped_lock{mutex_};
            if (detaching_ || !hud_contracts_ ||
                callback_context_ == nullptr ||
                callback_ == nullptr)
            {
                return;
            }
            contracts = hud_contracts_;
            callback_context = callback_context_;
            callback = callback_;
            ++in_flight_;
            sequence = ++dispatch_sequence_;
        }

        auto identity = application::HudFrameIdentity{};
        auto frame = std::optional<ActiveFrame>{};
        if (IsInGameThreadRaw() &&
            object_is_live(hud, contracts->hud_class))
        {
            auto* world = hud->GetWorld();
            auto* controller =
                read_object(contracts->player_owner, hud);
            auto* canvas = read_object(contracts->canvas, hud);
            if (parameters.size_x > 0 &&
                parameters.size_y > 0 &&
                object_is_live(world, contracts->world_class) &&
                object_is_live(
                    controller,
                    contracts->player_controller_class) &&
                object_is_live(canvas, contracts->canvas_class))
            {
                identity = application::HudFrameIdentity{
                    object_identity(world),
                    object_identity(controller),
                    object_identity(hud),
                    object_identity(canvas),
                };
                frame = ActiveFrame{
                    identity,
                    world,
                    controller,
                    hud,
                    canvas,
                    parameters.size_x,
                    parameters.size_y,
                };
            }
        }
        {
            const auto lock = std::scoped_lock{mutex_};
            if (!detaching_ && frame)
            {
                active_frame_ = *frame;
                active_frame_sequence_ = sequence;
            }
        }

        try
        {
            callback(callback_context, identity);
        }
        catch (...)
        {
            finish_dispatch(sequence);
            throw;
        }
        finish_dispatch(sequence);
    }

    auto finish_dispatch(std::uint64_t sequence) noexcept -> void
    {
        const auto lock = std::scoped_lock{mutex_};
        if (active_frame_sequence_ == sequence)
        {
            active_frame_.reset();
            active_frame_sequence_ = 0U;
        }
        if (in_flight_ > 0U)
        {
            --in_flight_;
        }
        if (in_flight_ == 0U)
        {
            idle_.notify_all();
        }
    }

    auto clear_state_locked() -> void
    {
        hook_ids_.reset();
        hud_contracts_.reset();
        canvas_contracts_.reset();
        input_contracts_.reset();
        paint_contracts_.reset();
        paint_scene_capture_contracts_.reset();
        image_paint_contracts_.reset();
        active_frame_.reset();
        bound_frame_.reset();
        esp_avatar_directory_.reset();
        queue_tracker_.reset();
        input_mutation_.reset();
        automatic_paint_capture_.reset();
        callback_context_ = nullptr;
        callback_ = nullptr;
        active_frame_sequence_ = 0U;
        detaching_ = false;
    }

    auto release_all_canvas_textures_noexcept() noexcept -> bool
    {
        if (!IsInGameThreadRaw())
        {
            return false;
        }
        auto textures = std::vector<
            std::pair<std::uint64_t, OwnedCanvasTexture>>{};
        {
            const auto lock = std::scoped_lock{mutex_};
            textures.reserve(canvas_textures_.size());
            for (const auto& [identity, texture] :
                 canvas_textures_)
            {
                textures.emplace_back(identity, texture);
            }
        }
        auto released_all = true;
        for (const auto& [handle, owned] : textures)
        {
            auto released = false;
            try
            {
                auto* texture = owned.object.Get();
                if (texture == nullptr)
                {
                    released = true;
                }
                else if (
                    object_identity(texture) ==
                    owned.object_identity)
                {
                    if (texture->IsRootSet())
                    {
                        texture->ClearRootSet();
                    }
                    released = !texture->IsRootSet();
                }
            }
            catch (...)
            {
            }
            if (!released)
            {
                released_all = false;
                continue;
            }
            const auto lock = std::scoped_lock{mutex_};
            const auto found = canvas_textures_.find(handle);
            if (found != canvas_textures_.end() &&
                found->second.object_identity ==
                    owned.object_identity)
            {
                canvas_textures_.erase(found);
            }
        }
        const auto lock = std::scoped_lock{mutex_};
        return released_all && canvas_textures_.empty();
    }

    auto restore_input_noexcept() noexcept -> void
    {
        if (!IsInGameThreadRaw())
        {
            return;
        }
        for (auto attempt = 0U; attempt < 3U; ++attempt)
        {
            auto state = std::optional<ui::RuntimeInputState>{};
            {
                const auto lock = std::scoped_lock{mutex_};
                if (input_mutation_)
                {
                    state = input_mutation_->captured;
                }
            }
            if (!state)
            {
                return;
            }
            try
            {
                if (restore_input(*state))
                {
                    return;
                }
            }
            catch (...)
            {
            }
        }
    }

    auto restore_automatic_paint_noexcept() noexcept -> void
    {
        if (!IsInGameThreadRaw())
        {
            return;
        }
        for (auto attempt = 0U; attempt < 3U; ++attempt)
        {
            auto snapshot = std::optional<
                application::PaintPreviewSnapshot>{};
            auto generation = application::JobGeneration{};
            {
                const auto lock = std::scoped_lock{mutex_};
                if (!automatic_paint_capture_ ||
                    !automatic_paint_capture_->preview_mutated)
                {
                    return;
                }
                if (!automatic_paint_capture_->preview_snapshot)
                {
                    return;
                }
                generation =
                    automatic_paint_capture_->generation;
                snapshot = automatic_paint_capture_->
                    preview_snapshot;
            }
            try
            {
                if (!restore_preview(*snapshot))
                {
                    continue;
                }
                const auto lock = std::scoped_lock{mutex_};
                if (automatic_paint_capture_ &&
                    automatic_paint_capture_->generation ==
                        generation)
                {
                    automatic_paint_capture_->preview_mutated =
                        false;
                }
                return;
            }
            catch (...)
            {
            }
        }
    }

    auto detach_noexcept() noexcept -> void
    {
        auto function = static_cast<UFunction*>(nullptr);
        auto ids = std::optional<std::pair<int, int>>{};
        {
            const auto lock = std::scoped_lock{mutex_};
            if (hud_contracts_)
            {
                function =
                    hud_contracts_->receive_draw_hud;
            }
            ids = hook_ids_;
            detaching_ = true;
        }
        if (function != nullptr && ids)
        {
            try
            {
                UObjectGlobals::UnregisterHook(function, *ids);
            }
            catch (...)
            {
            }
        }

        auto lock = std::unique_lock{mutex_};
        idle_.wait(lock, [this] { return in_flight_ == 0U; });
        lock.unlock();
        paint_appearance_worker_.shutdown();
        restore_automatic_paint_noexcept();
        restore_input_noexcept();
        for (auto attempt = 0U; attempt < 3U; ++attempt)
        {
            if (release_all_canvas_textures_noexcept())
            {
                break;
            }
        }
        lock.lock();
        clear_state_locked();
    }

    std::mutex mutex_{};
    std::condition_variable idle_{};
    std::optional<HudContracts> hud_contracts_{};
    std::optional<CanvasContracts> canvas_contracts_{};
    std::optional<InputContracts> input_contracts_{};
    std::optional<PaintContracts> paint_contracts_{};
    std::optional<PaintSceneCaptureContracts>
        paint_scene_capture_contracts_{};
    std::optional<ImagePaintContracts> image_paint_contracts_{};
    std::optional<ActiveFrame> active_frame_{};
    std::optional<BoundFrame> bound_frame_{};
    std::optional<EspAvatarDirectory> esp_avatar_directory_{};
    PaintQueueObservationTracker queue_tracker_{};
    std::unordered_map<
        std::uint64_t,
        OwnedCanvasTexture>
        canvas_textures_{};
    std::optional<std::pair<int, int>> hook_ids_{};
    std::optional<InputMutationLease> input_mutation_{};
    std::shared_ptr<product_ui::ProductUiInputQueue>
        input_queue_{};
    std::shared_ptr<
        const application::ImagePaintProfileCatalog>
        image_paint_profiles_{};
    application::PaintAppearanceWorker
        paint_appearance_worker_{};
    std::optional<AutomaticPaintCaptureSession>
        automatic_paint_capture_{};
    product_ui::ProductUiPointerCapture pointer_capture_{};
    HWND input_window_{};
    void* callback_context_{};
    application::HudCallback callback_{};
    std::size_t in_flight_{};
    std::uint64_t dispatch_sequence_{};
    std::uint64_t active_frame_sequence_{};
    std::uint64_t next_texture_handle_{1U};
    bool detaching_{};
};

UnrealRuntimeAdapter::UnrealRuntimeAdapter(
    std::shared_ptr<
        product_ui::ProductUiInputQueue> input_queue,
    std::shared_ptr<
        const application::ImagePaintProfileCatalog>
        image_paint_profiles)
    : impl_{std::make_unique<Impl>(
          std::move(input_queue),
          std::move(image_paint_profiles))}
{
}

UnrealRuntimeAdapter::~UnrealRuntimeAdapter() = default;

auto UnrealRuntimeAdapter::register_hud_callback(
    void* context,
    application::HudCallback callback)
    -> std::expected<
        application::CallbackId,
        application::CallbackPortError>
{
    return impl_->attach(context, callback);
}

auto UnrealRuntimeAdapter::unregister_hud_callback(
    application::CallbackId id)
    -> std::expected<
        void,
        application::CallbackPortError>
{
    return impl_->detach(id);
}

auto UnrealRuntimeAdapter::is_game_thread() const noexcept -> bool
{
    return RC::Unreal::IsInGameThreadRaw();
}

auto UnrealRuntimeAdapter::resolve_initial_contracts()
    -> std::expected<
        void,
        application::RuntimeExecutionError>
{
    return impl_->resolve_contracts();
}

auto UnrealRuntimeAdapter::rebind_hud_frame(
    const application::HudFrameIdentity& identity)
    -> std::expected<
        void,
        application::RuntimeExecutionError>
{
    return impl_->bind_frame(identity);
}

auto UnrealRuntimeAdapter::paint_at_uv_with_brush(
    const application::PaintAtUvWithBrush& request)
    -> std::expected<
        void,
        application::RuntimeExecutionError>
{
    return impl_->paint(request);
}

auto UnrealRuntimeAdapter::restore_transient_state(
    std::uint64_t generation)
    -> std::expected<
        void,
        application::RuntimeExecutionError>
{
    return impl_->restore_transient_state(generation);
}

auto UnrealRuntimeAdapter::observe_paint_queues(
    application::RuntimeObjectHandle component,
    application::JobGeneration generation)
    -> std::expected<
        application::PaintQueueObservation,
        application::RuntimeExecutionError>
{
    return impl_->observe_queues(component, generation);
}

auto UnrealRuntimeAdapter::capture(
    application::RuntimeObjectHandle component)
    -> std::expected<
        application::PaintPreviewSnapshot,
        application::RuntimeExecutionError>
{
    return impl_->capture_preview(component);
}

auto UnrealRuntimeAdapter::apply(
    application::RuntimeObjectHandle component,
    const application::PaintTextureImage& image)
    -> std::expected<
        void,
        application::RuntimeExecutionError>
{
    return impl_->apply_preview(component, image);
}

auto UnrealRuntimeAdapter::restore(
    const application::PaintPreviewSnapshot& snapshot)
    -> std::expected<
        void,
        application::RuntimeExecutionError>
{
    return impl_->restore_preview(snapshot);
}

auto UnrealRuntimeAdapter::capture(
    const core::PaintSettings& settings)
    -> std::expected<
        application::CapturedPaintJob,
        application::RuntimeExecutionError>
{
    return impl_->capture_paint(settings);
}

auto UnrealRuntimeAdapter::begin_automatic_capture(
    const core::PaintSettings& settings,
    application::JobGeneration generation)
    -> std::expected<
        void,
        application::RuntimeExecutionError>
{
    return impl_->begin_automatic_paint_capture(
        settings,
        generation);
}

auto UnrealRuntimeAdapter::advance_automatic_capture(
    application::JobGeneration generation)
    -> std::expected<
        std::optional<application::CapturedPaintJob>,
        application::RuntimeExecutionError>
{
    return impl_->advance_automatic_paint_capture(
        generation);
}

auto UnrealRuntimeAdapter::cancel_automatic_capture(
    application::JobGeneration generation)
    -> std::expected<
        bool,
        application::RuntimeExecutionError>
{
    return impl_->cancel_automatic_paint_capture(
        generation);
}

auto UnrealRuntimeAdapter::capture(core::BodyProfile body)
    -> std::expected<
        application::CapturedImagePaintJob,
        application::RuntimeExecutionError>
{
    return impl_->capture_image_paint(body);
}

auto UnrealRuntimeAdapter::observe_queues(
    application::RuntimeObjectHandle component,
    application::JobGeneration generation)
    -> std::expected<
        application::PaintQueueObservation,
        application::RuntimeExecutionError>
{
    return impl_->observe_queues(component, generation);
}

auto UnrealRuntimeAdapter::capture_esp_frame()
    -> std::expected<
        application::CapturedEspFrame,
        application::RuntimeExecutionError>
{
    return impl_->capture_esp_frame();
}

auto UnrealRuntimeAdapter::draw_esp_frame(
    const application::HudFrameIdentity& frame_identity,
    const core::EspPrimitiveFrame& frame)
    -> std::expected<
        void,
        application::RuntimeExecutionError>
{
    return impl_->draw_esp_frame(frame_identity, frame);
}

auto UnrealRuntimeAdapter::create_texture(
    const product_ui::ImageEditorTextureUpload& upload)
    -> std::expected<
        ui::CanvasTextureHandle,
        product_ui::ImageEditorTextureRuntimeError>
{
    return impl_->create_canvas_texture(upload);
}

auto UnrealRuntimeAdapter::release_texture(
    ui::CanvasTextureHandle handle)
    -> std::expected<
        void,
        product_ui::ImageEditorTextureRuntimeError>
{
    return impl_->release_canvas_texture(handle);
}

auto UnrealRuntimeAdapter::capture(
    const application::HudFrameIdentity& identity)
    -> std::expected<
        product_ui::ProductUiFrameInput,
        product_ui::ProductUiFrameRuntimeError>
{
    return impl_->capture_product_ui_frame(identity);
}

auto UnrealRuntimeAdapter::render(
    const application::HudFrameIdentity& identity,
    const ui::CanvasFrame& frame)
    -> std::expected<
        void,
        product_ui::ProductUiFrameRuntimeError>
{
    return impl_->render_canvas(identity, frame);
}

auto UnrealRuntimeAdapter::capture()
    -> std::expected<
        ui::RuntimeInputState,
        ui::InputPortError>
{
    return impl_->capture_input();
}

auto UnrealRuntimeAdapter::apply_panel_controls()
    -> std::expected<void, ui::InputPortError>
{
    return impl_->apply_input();
}

auto UnrealRuntimeAdapter::current_owner()
    -> std::expected<std::uint64_t, ui::InputPortError>
{
    return impl_->current_input_owner();
}

auto UnrealRuntimeAdapter::restore(
    const ui::RuntimeInputState& state)
    -> std::expected<void, ui::InputPortError>
{
    return impl_->restore_input(state);
}
} // namespace meccha::runtime
