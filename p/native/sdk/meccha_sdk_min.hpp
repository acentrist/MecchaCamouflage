#pragma once

#include <cstddef>
#include <cstdint>

namespace meccha_sdk
{
    namespace Offsets
    {
        constexpr std::uintptr_t GObjects = 0x09F23DD0;
        constexpr std::uintptr_t GWorld = 0x09C6D620;
        constexpr std::uintptr_t ProcessEvent = 0x015D09F0;
        constexpr int ProcessEventIdx = 0x0000004C;
    }

    namespace FieldOffsets
    {
        constexpr std::uintptr_t UWorld_OwningGameInstance = 0x0228;
        constexpr std::uintptr_t UGameInstance_LocalPlayers = 0x0038;
        constexpr std::uintptr_t UPlayer_PlayerController = 0x0030;
        constexpr std::uintptr_t BP_PlayerController_RuntimePaintRelay = 0x0770;
        constexpr std::uintptr_t BP_FirstPersonCharacter_RuntimePaintable = 0x0B68;
        constexpr std::uintptr_t RuntimePaintable_CurrentBrushSettings = 0x0170;
        constexpr std::uintptr_t RuntimePaintable_MaxReplicatedPaintStrokesPerTick = 0x01A8;
        constexpr std::uintptr_t SceneCapture2D_CaptureComponent2D = 0x02B8;
        constexpr std::uintptr_t SceneCaptureComponent_CaptureSource = 0x0241;
        constexpr std::uintptr_t SceneCaptureComponent_CaptureFlags = 0x0242;
        constexpr std::uintptr_t SceneCaptureComponent_bAlwaysPersistRenderingState = 0x0243;
        constexpr std::uintptr_t SceneCaptureComponent2D_ProjectionType = 0x0328;
        constexpr std::uintptr_t SceneCaptureComponent2D_FOVAngle = 0x032C;
        constexpr std::uintptr_t SceneCaptureComponent2D_TextureTarget = 0x0350;
    }

    enum class ECameraProjectionMode : std::uint8_t
    {
        Perspective = 0,
        Orthographic = 1,
        Max = 2,
    };

    enum class ESceneCaptureSource : std::uint8_t
    {
        SceneColorHDR = 0,
        SceneColorHDRNoAlpha = 1,
        FinalColorLDR = 2,
        SceneColorSceneDepth = 3,
        SceneDepth = 4,
        DeviceDepth = 5,
        Normal = 6,
        BaseColor = 7,
        FinalColorHDR = 8,
        FinalToneCurveHDR = 9,
        Max = 10,
    };

    enum class ESpawnActorCollisionHandlingMethod : std::uint8_t
    {
        Undefined = 0,
        AlwaysSpawn = 1,
        AdjustIfPossibleButAlwaysSpawn = 2,
        AdjustIfPossibleButDontSpawnIfColliding = 3,
        DontSpawnIfColliding = 4,
        Max = 5,
    };

    enum class ESpawnActorScaleMethod : std::uint8_t
    {
        OverrideRootScale = 0,
        MultiplyWithRoot = 1,
        SelectDefaultAtRuntime = 2,
    };

    enum class ETextureRenderTargetFormat : std::uint8_t
    {
        RTF_R8 = 0,
        RTF_RG8 = 1,
        RTF_RGBA8 = 2,
        RTF_RGBA8_SRGB = 3,
        RTF_R16f = 4,
        RTF_RG16f = 5,
        RTF_RGBA16f = 6,
        RTF_R32f = 7,
        RTF_RG32f = 8,
        RTF_RGBA32f = 9,
        RTF_RGB10A2 = 10,
        RTF_MAX = 11,
    };

    enum class EPaintChannel : std::uint8_t
    {
        Albedo = 0,
        Metallic = 1,
        Roughness = 2,
        Height = 3,
        All = 4,
        AlbedoMetallicRoughness = 5,
        Max = 6,
    };

    enum class EPaintChannelApplyMode : std::uint8_t
    {
        Override = 0,
        AlphaBlend = 1,
        Additive = 2,
        Max = 3,
    };

    enum class EBrushFalloff : std::uint8_t
    {
        Linear = 0,
        Smooth = 1,
        Spherical = 2,
        Tip = 3,
        Max = 4,
    };

    enum class EPaintBlendMode : std::uint8_t
    {
        Normal = 0,
        Additive = 1,
        Multiply = 2,
        Max = 3,
    };

    template <typename T>
    struct TArray
    {
        T* Data{nullptr};
        std::int32_t Num{0};
        std::int32_t Max{0};
    };

    struct RuntimePaintableComponent_ExportChannelToBytes
    {
        EPaintChannel Channel{EPaintChannel::Albedo};
        std::uint8_t Pad_1[0x7]{};
        TArray<std::uint8_t> OutData{};
        bool ReturnValue{false};
        std::uint8_t Pad_19[0x7]{};
    };

    struct RuntimePaintableComponent_ImportChannelFromBytes
    {
        EPaintChannel Channel{EPaintChannel::Albedo};
        std::uint8_t Pad_1[0x7]{};
        TArray<std::uint8_t> Data{};
        bool ReturnValue{false};
        std::uint8_t Pad_19[0x7]{};
    };

    struct RuntimePaintableComponent_GetRenderTarget
    {
        EPaintChannel Channel{EPaintChannel::Albedo};
        std::uint8_t Pad_1[0x7]{};
        void* ReturnValue{nullptr};
    };

    struct FVector2D
    {
        double X{0.0};
        double Y{0.0};
    };

    struct FVector
    {
        double X{0.0};
        double Y{0.0};
        double Z{0.0};
    };

    struct FScreenSpacePaintResult
    {
        bool bSuccess{false};
        std::uint8_t Pad_1[0x7]{};
        FVector2D HitUV{};
        FVector HitWorldPosition{};
        FVector HitNormal{};
    };
    static_assert(sizeof(FScreenSpacePaintResult) == 0x48, "FScreenSpacePaintResult layout mismatch");

    struct RuntimePaintableComponent_HitTestAtScreenPosition
    {
        void* MeshComponent{nullptr};
        FVector2D ScreenPosition{};
        void* PlayerController{nullptr};
        bool bUseCachedTriangles{true};
        std::uint8_t Pad_21[0x7]{};
        FScreenSpacePaintResult ReturnValue{};
    };
    static_assert(sizeof(RuntimePaintableComponent_HitTestAtScreenPosition) == 0x70, "HitTestAtScreenPosition params layout mismatch");

    struct FLinearColor
    {
        float R{0.0f};
        float G{0.0f};
        float B{0.0f};
        float A{1.0f};
    };

    struct FColor
    {
        std::uint8_t B{0};
        std::uint8_t G{0};
        std::uint8_t R{0};
        std::uint8_t A{255};
    };

    struct FQuat
    {
        double X{0.0};
        double Y{0.0};
        double Z{0.0};
        double W{1.0};
    };

    struct FRotator
    {
        double Pitch{0.0};
        double Yaw{0.0};
        double Roll{0.0};
    };

    struct FTransform
    {
        FQuat Rotation{};
        FVector Translation{};
        std::uint8_t Pad_38[0x8]{};
        FVector Scale3D{1.0, 1.0, 1.0};
        std::uint8_t Pad_58[0x8]{};
    };

    struct FGuid
    {
        std::uint32_t A{0};
        std::uint32_t B{0};
        std::uint32_t C{0};
        std::uint32_t D{0};
    };

    struct FRuntimeBrushSettings
    {
        float Radius{0.02f};
        float Hardness{1.0f};
        float Opacity{1.0f};
        float Spacing{0.25f};
        EBrushFalloff Falloff{EBrushFalloff::Spherical};
        EPaintBlendMode BlendMode{EPaintBlendMode::Normal};
        std::uint8_t Pad_12[0x6]{};
        void* BrushTexture{nullptr};
        float Rotation{0.0f};
        std::uint8_t Pad_24[0x4]{};
    };

    struct FPaintChannelData
    {
        FLinearColor AlbedoColor{};
        float Metallic{0.0f};
        float Roughness{0.65f};
        float Height{0.0f};
        EPaintChannelApplyMode ApplyMode{EPaintChannelApplyMode::Override};
        std::uint8_t Pad_1D[0x3]{};
    };

    struct FPaintStroke
    {
        FVector2D Uv{};
        FVector WorldPosition{};
        bool bHasWorldPosition{false};
        std::uint8_t Pad_29[0x7]{};
        FVector LocalPosition{};
        bool bHasLocalPosition{false};
        bool bHasSkeletalTriangleAnchor{false};
        std::uint8_t Pad_4A[0x2]{};
        std::int32_t SkeletalTriangleIndex{0};
        FVector SkeletalTriangleBarycentric{};
        FRuntimeBrushSettings BrushSettings{};
        FPaintChannelData ChannelData{};
        EPaintChannel TargetChannel{EPaintChannel::AlbedoMetallicRoughness};
        std::uint8_t Pad_B1[0x3]{};
        float EffectiveBrushWorldRadius{0.02f};
        std::int32_t EffectiveSubdivisionLevel{0};
        float EffectiveSubdivisionPixelSize{1.0f};
        std::int32_t EffectiveTemplateResolution{0};
        std::int32_t EffectiveMaxGeneratedBrushTriangles{0};
        std::int32_t EffectiveGutterExpandPixels{0};
        FGuid ReplicationSourceId{};
        std::uint8_t Pad_DC[0x4]{};
    };

    struct FPaintStrokeBatch
    {
        TArray<FPaintStroke> Strokes{};
    };

    struct RuntimePaintableComponent_ServerPaintBatch
    {
        FPaintStrokeBatch Batch{};
    };

    struct RuntimePaintableComponent_ServerSendStrokeBatch
    {
        FPaintStrokeBatch Batch{};
    };

    struct RuntimePaintableComponent_ServerSendPaint
    {
        FPaintStroke Stroke{};
    };

    struct RuntimePaintableComponent_ServerPaint
    {
        FPaintStroke Stroke{};
    };

    struct RuntimePaintableComponent_PaintAtUVWithBrush
    {
        FVector2D Uv{};
        FPaintChannelData ChannelData{};
        FRuntimeBrushSettings BrushSettings{};
        EPaintChannel Channel{EPaintChannel::AlbedoMetallicRoughness};
        std::uint8_t Pad_59[0x7]{};
    };

    struct RuntimePaintRelayComponent_RelayPaintToServer
    {
        void* PaintComponent{nullptr};
        FPaintStroke Stroke{};
    };

    struct RuntimePaintRelayComponent_RelayStrokeBatchToServer
    {
        void* PaintComponent{nullptr};
        FPaintStrokeBatch Batch{};
    };

    struct Controller_K2_GetPawn
    {
        void* ReturnValue{nullptr};
    };

    struct Actor_K2_GetActorLocation
    {
        FVector ReturnValue{};
    };

    struct KismetRenderingLibrary_CreateRenderTarget2D
    {
        void* WorldContextObject{nullptr};
        std::int32_t Width{0};
        std::int32_t Height{0};
        ETextureRenderTargetFormat Format{ETextureRenderTargetFormat::RTF_RGBA8_SRGB};
        std::uint8_t Pad_11[0x3]{};
        FLinearColor ClearColor{};
        bool bAutoGenerateMipMaps{false};
        bool bSupportUAVs{false};
        std::uint8_t Pad_26[0x2]{};
        void* ReturnValue{nullptr};
    };

    struct KismetRenderingLibrary_ReadRenderTargetPixel
    {
        void* WorldContextObject{nullptr};
        void* TextureRenderTarget{nullptr};
        std::int32_t X{0};
        std::int32_t Y{0};
        FColor ReturnValue{};
        std::uint8_t Pad_1C[0x4]{};
    };

    struct GameplayStatics_BeginDeferredActorSpawnFromClass
    {
        const void* WorldContextObject{nullptr};
        void* ActorClass{nullptr};
        FTransform SpawnTransform{};
        ESpawnActorCollisionHandlingMethod CollisionHandlingOverride{ESpawnActorCollisionHandlingMethod::AlwaysSpawn};
        std::uint8_t Pad_71[0x7]{};
        void* Owner{nullptr};
        ESpawnActorScaleMethod TransformScaleMethod{ESpawnActorScaleMethod::SelectDefaultAtRuntime};
        std::uint8_t Pad_81[0x7]{};
        void* ReturnValue{nullptr};
    };

    struct GameplayStatics_FinishSpawningActor
    {
        void* Actor{nullptr};
        std::uint8_t Pad_8[0x8]{};
        FTransform SpawnTransform{};
        ESpawnActorScaleMethod TransformScaleMethod{ESpawnActorScaleMethod::SelectDefaultAtRuntime};
        std::uint8_t Pad_71[0x7]{};
        void* ReturnValue{nullptr};
    };

    struct Actor_K2_SetActorLocation
    {
        FVector NewLocation{};
        bool bSweep{false};
        std::uint8_t Pad_19[0x7]{};
        std::uint8_t SweepHitResult[0x100]{};
        bool bTeleport{true};
        bool ReturnValue{false};
        std::uint8_t Pad_122[0x6]{};
    };

    struct Actor_K2_SetActorRotation
    {
        FRotator NewRotation{};
        bool bTeleportPhysics{true};
        bool ReturnValue{false};
        std::uint8_t Pad_1A[0x6]{};
    };

    static_assert(sizeof(TArray<std::uint8_t>) == 0x10, "Dumper7 TArray layout mismatch");
    static_assert(sizeof(FVector2D) == 0x10, "FVector2D layout mismatch");
    static_assert(sizeof(FVector) == 0x18, "FVector layout mismatch");
    static_assert(sizeof(FLinearColor) == 0x10, "FLinearColor layout mismatch");
    static_assert(sizeof(FColor) == 0x04, "FColor layout mismatch");
    static_assert(sizeof(FQuat) == 0x20, "FQuat layout mismatch");
    static_assert(sizeof(FRotator) == 0x18, "FRotator layout mismatch");
    static_assert(sizeof(FTransform) == 0x60, "FTransform layout mismatch");
    static_assert(offsetof(FTransform, Translation) == 0x20, "FTransform Translation offset mismatch");
    static_assert(offsetof(FTransform, Scale3D) == 0x40, "FTransform Scale3D offset mismatch");
    static_assert(sizeof(FRuntimeBrushSettings) == 0x28, "RuntimeBrushSettings layout mismatch");
    static_assert(offsetof(FRuntimeBrushSettings, BrushTexture) == 0x18, "RuntimeBrushSettings BrushTexture offset mismatch");
    static_assert(sizeof(FPaintChannelData) == 0x20, "PaintChannelData layout mismatch");
    static_assert(offsetof(FPaintChannelData, Metallic) == 0x10, "PaintChannelData Metallic offset mismatch");
    static_assert(offsetof(FPaintChannelData, Roughness) == 0x14, "PaintChannelData Roughness offset mismatch");
    static_assert(offsetof(FPaintChannelData, ApplyMode) == 0x1C, "PaintChannelData ApplyMode offset mismatch");
    static_assert(sizeof(FPaintStroke) == 0xE0, "PaintStroke layout mismatch");
    static_assert(offsetof(FPaintStroke, BrushSettings) == 0x68, "PaintStroke BrushSettings offset mismatch");
    static_assert(offsetof(FPaintStroke, ChannelData) == 0x90, "PaintStroke ChannelData offset mismatch");
    static_assert(offsetof(FPaintStroke, TargetChannel) == 0xB0, "PaintStroke TargetChannel offset mismatch");
    static_assert(sizeof(FPaintStrokeBatch) == 0x10, "PaintStrokeBatch layout mismatch");
    static_assert(sizeof(RuntimePaintableComponent_ExportChannelToBytes) == 0x20, "ExportChannelToBytes params layout mismatch");
    static_assert(offsetof(RuntimePaintableComponent_ExportChannelToBytes, OutData) == 0x08, "Export OutData offset mismatch");
    static_assert(offsetof(RuntimePaintableComponent_ExportChannelToBytes, ReturnValue) == 0x18, "Export ReturnValue offset mismatch");
    static_assert(sizeof(RuntimePaintableComponent_ImportChannelFromBytes) == 0x20, "ImportChannelFromBytes params layout mismatch");
    static_assert(offsetof(RuntimePaintableComponent_ImportChannelFromBytes, Data) == 0x08, "Import Data offset mismatch");
    static_assert(offsetof(RuntimePaintableComponent_ImportChannelFromBytes, ReturnValue) == 0x18, "Import ReturnValue offset mismatch");
    static_assert(sizeof(RuntimePaintableComponent_GetRenderTarget) == 0x10, "GetRenderTarget params layout mismatch");
    static_assert(sizeof(RuntimePaintableComponent_ServerPaintBatch) == 0x10, "ServerPaintBatch params layout mismatch");
    static_assert(sizeof(RuntimePaintableComponent_ServerSendStrokeBatch) == 0x10, "ServerSendStrokeBatch params layout mismatch");
    static_assert(sizeof(RuntimePaintableComponent_ServerSendPaint) == 0xE0, "ServerSendPaint params layout mismatch");
    static_assert(sizeof(RuntimePaintableComponent_ServerPaint) == 0xE0, "ServerPaint params layout mismatch");
    static_assert(sizeof(RuntimePaintableComponent_PaintAtUVWithBrush) == 0x60, "PaintAtUVWithBrush params layout mismatch");
    static_assert(sizeof(RuntimePaintRelayComponent_RelayPaintToServer) == 0xE8, "RelayPaintToServer params layout mismatch");
    static_assert(sizeof(RuntimePaintRelayComponent_RelayStrokeBatchToServer) == 0x18, "RelayStrokeBatchToServer params layout mismatch");
    static_assert(offsetof(RuntimePaintRelayComponent_RelayPaintToServer, Stroke) == 0x08, "RelayPaintToServer Stroke offset mismatch");
    static_assert(offsetof(RuntimePaintRelayComponent_RelayStrokeBatchToServer, Batch) == 0x08, "RelayStrokeBatchToServer Batch offset mismatch");
    static_assert(sizeof(Actor_K2_GetActorLocation) == 0x18, "K2_GetActorLocation params layout mismatch");
    static_assert(sizeof(KismetRenderingLibrary_CreateRenderTarget2D) == 0x30, "CreateRenderTarget2D params layout mismatch");
    static_assert(offsetof(KismetRenderingLibrary_CreateRenderTarget2D, Format) == 0x10, "CreateRenderTarget2D Format offset mismatch");
    static_assert(offsetof(KismetRenderingLibrary_CreateRenderTarget2D, ClearColor) == 0x14, "CreateRenderTarget2D ClearColor offset mismatch");
    static_assert(offsetof(KismetRenderingLibrary_CreateRenderTarget2D, ReturnValue) == 0x28, "CreateRenderTarget2D ReturnValue offset mismatch");
    static_assert(sizeof(KismetRenderingLibrary_ReadRenderTargetPixel) == 0x20, "ReadRenderTargetPixel params layout mismatch");
    static_assert(offsetof(KismetRenderingLibrary_ReadRenderTargetPixel, ReturnValue) == 0x18, "ReadRenderTargetPixel ReturnValue offset mismatch");
    static_assert(sizeof(GameplayStatics_BeginDeferredActorSpawnFromClass) == 0x90, "BeginDeferredActorSpawnFromClass params layout mismatch");
    static_assert(offsetof(GameplayStatics_BeginDeferredActorSpawnFromClass, SpawnTransform) == 0x10, "BeginDeferred SpawnTransform offset mismatch");
    static_assert(offsetof(GameplayStatics_BeginDeferredActorSpawnFromClass, Owner) == 0x78, "BeginDeferred Owner offset mismatch");
    static_assert(offsetof(GameplayStatics_BeginDeferredActorSpawnFromClass, ReturnValue) == 0x88, "BeginDeferred ReturnValue offset mismatch");
    static_assert(sizeof(GameplayStatics_FinishSpawningActor) == 0x80, "FinishSpawningActor params layout mismatch");
    static_assert(offsetof(GameplayStatics_FinishSpawningActor, SpawnTransform) == 0x10, "FinishSpawningActor SpawnTransform offset mismatch");
    static_assert(offsetof(GameplayStatics_FinishSpawningActor, ReturnValue) == 0x78, "FinishSpawningActor ReturnValue offset mismatch");
    static_assert(sizeof(Actor_K2_SetActorLocation) == 0x128, "K2_SetActorLocation params layout mismatch");
    static_assert(sizeof(Actor_K2_SetActorRotation) == 0x20, "K2_SetActorRotation params layout mismatch");
}
