#include <meccha/runtime/unreal_runtime_adapter.hpp>

#include <meccha/runtime/canvas_call_codec.hpp>
#include <meccha/runtime/input_control_codec.hpp>
#include <meccha/runtime/paint_call_codec.hpp>
#include <meccha/runtime/paint_preview_codec.hpp>
#include <meccha/runtime/paint_queue_codec.hpp>
#include <meccha/runtime/texture_import_codec.hpp>
#include <meccha/runtime/unreal_contracts.hpp>
#include <meccha/core/png_encoder.hpp>
#include <meccha/product_ui/product_ui_pointer_capture.hpp>
#include <meccha/ui/esp_canvas_frame.hpp>

#include "unreal_reflection_validation.hpp"

#include <Unreal/CoreUObject/UObject/Class.hpp>
#include <Unreal/CoreUObject/UObject/UnrealType.hpp>
#include <Unreal/FMemory.hpp>
#include <Unreal/FSoftObjectPath.hpp>
#include <Unreal/FWeakObjectPtr.hpp>
#include <Unreal/UFunctionStructs.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UObjectGlobals.hpp>
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
constexpr auto ControllerClassPath =
    STR("/Script/Engine.Controller");
constexpr auto PawnClassPath = STR("/Script/Engine.Pawn");
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
constexpr auto RuntimePaintableClassPath =
    STR("/Script/PenguinHotel.RuntimePaintableComponent");
constexpr auto MeshComponentClassPath =
    STR("/Script/Engine.MeshComponent");
constexpr auto SkinnedMeshComponentClassPath =
    STR("/Script/Engine.SkinnedMeshComponent");
constexpr auto SkinnedAssetClassPath =
    STR("/Script/Engine.SkinnedAsset");
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
constexpr auto PaintChannelDataPath =
    STR("/Script/PenguinHotel.PaintChannelData");
constexpr auto RuntimeBrushSettingsPath =
    STR("/Script/PenguinHotel.RuntimeBrushSettings");
constexpr auto ReceiveDrawHudParameterBytes = 8;
constexpr auto FailureMessage = "error.operation.failed";
constexpr auto MaximumOwnedCanvasTextures = std::size_t{1024U};

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
    UClass* runtime_paintable_class{};
    UClass* replication_manager_class{};
    FObjectPropertyBase* acknowledged_pawn{};
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
    contracts.runtime_paintable_class =
        find_class(RuntimePaintableClassPath);
    contracts.replication_manager_class =
        find_class(RuntimePaintReplicationManagerClassPath);
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
                input_mutation_)
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
        return runtime_failure(
            application::RuntimeContractId::EspFrame,
            application::ContractFailureKind::InvalidValue);
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
        image_paint_contracts_.reset();
        active_frame_.reset();
        bound_frame_.reset();
        queue_tracker_.reset();
        input_mutation_.reset();
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
    std::optional<ImagePaintContracts> image_paint_contracts_{};
    std::optional<ActiveFrame> active_frame_{};
    std::optional<BoundFrame> bound_frame_{};
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
