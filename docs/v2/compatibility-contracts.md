# MecchaCamouflage v2 Game Compatibility Contracts

This is the Phase 1 inventory of game-specific runtime inputs used by v1. It is
not permission to copy raw offsets into v2. Phase 4 must express the accepted
subset in `UnrealRuntimeAdapter`, resolve it through the pinned UE4SS APIs where
possible, validate every type/layout/lifetime, and fail closed.

## Contract policy

- UE4SS commit candidate:
  `6c26f038751b3d96059d4a9148f5d093012d55ad`.
- Game target: Steam App ID `4704690`, current UE 5.6 Windows x64 build.
- A name match alone is insufficient. Validate owner/class, inheritance,
  property kind, parameter direction/offset/size, total parameter size, return
  semantics, and object validity immediately before use.
- Candidate aliases below document v1 discovery behavior. Phase 4 must reduce
  aliases to the smallest live-validated contract; it must not try candidates
  after a partially matching schema.
- No raw UObject pointer survives its validated game-thread operation/frame.
- Unsupported game/runtime contracts disable the affected operation before
  mutation and report the exact contract ID.

## v1 raw field-offset inventory

These offsets are the current v1 negative baseline. They must be replaced by
validated UE4SS/reflected access or explicitly reapproved only where reflection
cannot represent the shipping field.

| Contract | v1 offset | v2 requirement |
| --- | ---: | --- |
| `UWorld.PersistentLevel` | `0x0030` | Resolve/validate level when actor enumeration is still required |
| `ULevel.Actors` | `0x00A0` | Prefer UE4SS object/actor facilities |
| `UWorld.OwningGameInstance` | `0x0228` | Resolve reflected property |
| `UGameInstance.LocalPlayers` | `0x0038` | Resolve reflected array and element kind |
| `UPlayer.PlayerController` | `0x0030` | Resolve reflected object property |
| `Controller.ControlRotation` | `0x0320` | Resolve reflected rotator |
| `PlayerController.PlayerCameraManager` | `0x0360` | Resolve reflected object property |
| `SkeletalMeshComponent.CachedComponentSpaceTransforms` | `0x05F0` | Architecture gate must prove a validated pose API/layout |
| `BP_FirstPersonCharacter.RuntimePaintable` | `0x0B68` | Replace with owned-component discovery and class validation |
| `RuntimePaintable.CurrentBrushSettings` | `0x0170` | Resolve reflected struct or use validated function data |
| `SceneCapture2D.CaptureComponent2D` | `0x02B8` | Resolve reflected object property |
| `SceneCaptureComponent.CaptureSource` | `0x0241` | Resolve enum property |
| `SceneCaptureComponent.CaptureFlags` | `0x0242` | Remove if not required by accepted capture path |
| `SceneCaptureComponent.bAlwaysPersistRenderingState` | `0x0243` | Resolve reflected bool |
| `SceneCaptureComponent2D.ProjectionType` | `0x0328` | Resolve reflected enum |
| `SceneCaptureComponent2D.FOVAngle` | `0x032C` | Resolve reflected float |
| `SceneCaptureComponent2D.TextureTarget` | `0x0350` | Resolve reflected object property |
| `PlayerCameraManager` camera cache | `0x1540` in Bridge ESP | Must be replaced by a validated UE4SS/reflected view source |

Any offset retained after Phase 4 requires its own architecture decision,
current-build live evidence, size/type/lifetime guard, and compatibility error.

## Typed struct/layout baseline

v1 declares these shipping-layout assumptions in `src/native/include/sdk.hpp`.
Phase 4 validates the reflected equivalents before any call.

| Type/schema | v1 size/critical offsets |
| --- | --- |
| `FVector2D` | `0x10` |
| `FVector` | `0x18` |
| `FLinearColor` | `0x10` |
| `FColor` | `0x04` |
| `FQuat` | `0x20` |
| `FRotator` | `0x18` |
| `FTransform` | `0x60`; Translation `0x20`; Scale3D `0x40` |
| `FRuntimeBrushSettings` | `0x28`; BrushTexture `0x18` |
| `FPaintChannelData` | `0x24`; Metallic `0x10`; Roughness `0x14`; Emissive `0x1C`; ApplyMode `0x20` |
| `FPaintStroke` | `0xE0`; BrushSettings `0x68`; ChannelData `0x90`; TargetChannel `0xB4` |
| `PaintAtUVWithBrush` parameters | `0x68`: UV, ChannelData, BrushSettings, Channel |
| `FScreenSpacePaintResult` | `0x48`: success, UV, world position, normal |
| `HitTestAtScreenPosition` parameters | `0x70` |
| `CreateRenderTarget2D` parameters | current build `0x38`; Slices `0x10`; Format `0x14`; ClearColor `0x18`; ReturnValue `0x30` |
| `ReadRenderTargetPixel` parameters | `0x20`; ReturnValue `0x18` |
| `ReadRenderTargetRaw` parameters | current build `0x28`; output `TArray<LinearColor>` `0x10`; Normalize `0x20`; ReturnValue `0x21` |
| `BeginDeferredActorSpawnFromClass` parameters | `0x90`; transform `0x10`; owner `0x78`; return `0x88` |
| `FinishSpawningActor` parameters | `0x80` |

The v2 adapter must not expose these structs outside the runtime layer.
Project-owned requests/results contain semantic fields and are serialized into
the validated reflected parameters only inside the game-thread call.

## World, controller, and paintable-body resolution

| Contract ID | Required owner/member or candidate | Purpose |
| --- | --- | --- |
| GAME-WORLD-001 | `GameEngine` / `Engine.GameViewport` / `GameViewportClient.World` | Prefer active viewport world |
| GAME-WORLD-002 | `World.OwningGameInstance` / `GameInstance.LocalPlayers` | Local game context |
| GAME-WORLD-003 | `Player.PlayerController` | Local controller |
| GAME-WORLD-004 | `PlayerController.AcknowledgedPawn`, `Controller.K2_GetPawn` | Distinguish controlled/spectator pawn |
| GAME-WORLD-005 | owned component candidates `Mesh`, `MeshComponent`, `SkeletalMeshComponent`, `TargetMeshComponent`, `TargetMesh` | Prove paintable mesh ownership |
| GAME-WORLD-006 | `RuntimePaintableComponent` associated with acknowledged local body | Paint target |
| GAME-WORLD-007 | controller `RuntimePaintRelay` or `RuntimePaintRelayComponent` instance | Observe only if still needed by accepted game API |
| GAME-WORLD-008 | `Actor.K2_GetActorLocation` | Body/world position |

The adapter must prove that the selected paint component belongs to the local
acknowledged role body. Camera targets, other player controllers, and
unassociated components are not candidates.

## Paint and preview UFunctions

| Contract ID | Owner/function | Required schema/semantics |
| --- | --- | --- |
| GAME-PAINT-001 | Runtime paintable `IsInitialized` | No-arg bool result |
| GAME-PAINT-002 | Runtime paintable `InitializePaint` | exact `0x10`: input `MeshComponent` object at `0x00`; bool return at `0x08`; the v1 zero-filled no-argument call is rejected |
| GAME-PAINT-003 | Runtime paintable `GetInitializedPaintMesh` | No-arg object result associated with target |
| GAME-PAINT-004 | Runtime paintable `HitTestAtScreenPosition` | Exact `0x70` baseline semantics: mesh, screen point, controller, cache flag, structured result |
| GAME-PAINT-005 | Runtime paintable `GetDominantPaintMaterialPatterns` | inputs `MaxPatterns`, `SampleStep`, `AlphaThreshold`; bounded `OutPatterns`; bool return |
| GAME-PAINT-006 | Runtime paintable `ExportChannelToBytes` | channel input and bounded output byte array for preview lease |
| GAME-PAINT-007 | Runtime paintable `ImportChannelFromBytes` | channel input and exact bounded byte array; preview restore only |
| GAME-PAINT-008 | Runtime paintable `PaintAtUVWithBrush` | exact UV/channel-data/brush/channel layout; sole production multiplayer sender |
| GAME-PAINT-009 | Runtime paintable `GetRecordedStrokeCount` | monotonic per-component local submission observation |
| GAME-PAINT-010 | `RuntimePaintReplicationManager.GetQueuedStrokeCountForComponent` | per-target queue drain |
| GAME-PAINT-011 | `RuntimePaintReplicationManager.GetQueuedStrokeCount` | manager queue observation |
| GAME-PAINT-012 | `RuntimePaintReplicationManager.GetReplicationPressure` | queue-pressure snapshot |

The production observer resolves the manager by exact class and requires
exactly one live non-CDO/non-archetype instance whose `GetWorld()` equals the
active HUD World. A first-global-object lookup is forbidden. Completion uses
only `GetRecordedStrokeCount` and
`GetQueuedStrokeCountForComponent`; the global count and pressure are
validated diagnostics/pacing inputs and cannot hold one job open for unrelated
players.

Observed replication-manager properties that may contribute validated pacing:

- `QueuedOutgoingBatches`
- `MaxOutgoingNetworkBatchesPerSecond`
- `MaxOutgoingStrokesPerBatch`
- `MinRemotePaintFramesAfterLocalPaint`
- `MaxAdaptiveRemotePaintFrameInterval`
- `MaxReplicatedPaintRenderTargetWritesPerFrame`
- `bEnableAdaptiveRemotePaintInterval`

These properties are optional inputs to a conservative project-owned pacing
plan. Missing properties must not cause a faster unsafe fallback. Queue-drain
proof remains mandatory.

The v1 research-only functions `MulticastSyncChannelData`,
`MulticastSyncCompressedChannelData`, `RelayTextureSyncToServer`, and
`ServerRelayTextureSync` are not production senders and are not ported.

## Scene capture, projection, and texture operations

| Contract ID | Owner/function | Required use |
| --- | --- | --- |
| GAME-RENDER-001 | `KismetRenderingLibrary.CreateRenderTarget2D` | Exact current-build `0x38` schema; create a bounded, single-slice transient render target on the game thread |
| GAME-RENDER-002 | `KismetRenderingLibrary.ReadRenderTargetRaw` | Exact current-build `0x28` schema; bounded finite `TArray<LinearColor>` readback with Unreal-owned storage cleanup |
| GAME-RENDER-003 | `GameplayStatics.BeginDeferredActorSpawnFromClass` | Retain only if the accepted capture path still needs a scene-capture actor |
| GAME-RENDER-004 | `GameplayStatics.FinishSpawningActor` | Pair with validated deferred spawn |
| GAME-RENDER-005 | `Actor.K2_SetActorLocation` | Position retained capture actor |
| GAME-RENDER-006 | `Actor.K2_SetActorRotation` | Orient retained capture actor |
| GAME-RENDER-007 | `PlayerController.GetViewportSize` | Validated viewport dimensions |
| GAME-RENDER-008 | `PlayerController.DeprojectScreenPositionToWorld` | Validated screen ray |
| GAME-RENDER-009 | `PlayerController.ProjectWorldLocationToScreen` | Calibration/contract check |

Phase 4/8 must determine the minimal accepted subset. Debug/research capture,
arbitrary actor spawning, and texture probing are deleted when they are not
needed for product Paint/Image Paint.

## HUD and UCanvas contracts

| Contract ID | Owner/member | Required use |
| --- | --- | --- |
| GAME-HUD-001 | current HUD callback: `ReceiveDrawHUD` or validated equivalent `DrawHUD` | Frame-scoped game-thread boundary |
| GAME-HUD-002 | HUD `Canvas` | Product UI and ESP canvas |
| GAME-HUD-003 | HUD `DebugCanvas` | Not a product fallback; use only if explicitly validated/required |
| GAME-HUD-004 | Canvas `SizeX`, `SizeY` | Viewport bounds |
| GAME-HUD-005 | Canvas `K2_DrawLine` | Lines, boxes, skeletons, snaplines, UI |
| GAME-HUD-006 | Canvas `K2_DrawText` | Product/localized text |
| GAME-HUD-007 | Validated Canvas texture primitive | Image/editor rendering |
| GAME-HUD-008 | Validated clipping/input functions/properties | Architecture-gate interaction |

The source contract now freezes `K2_DrawLine` at `0x38`,
`K2_DrawTexture` at `0x70`, and `K2_DrawText` at `0x88` parameter bytes,
including every property offset/direction and the exact `Vector2D`,
`LinearColor`, and `FString` property kinds. `UnrealRuntimeAdapter` validates
those records before retaining them and currently admits only complete frames
containing validated lines, filled white-texture tiles, and text backed by the
exact `/Game/UI/NotoFonts/MainFont.MainFont` cooked `Font` asset. That asset
path/export type comes from opt-in CUE4Parse inventory of the current game.
Resolution/load occurs only on the game thread; the adapter retains a weak
generation identity and rejects stale or wrong-class font state before frame
mutation. Opaque texture handles remain fail-closed unless they resolve one
adapter-owned, rooted, generation-checked `Texture2D`. Missing-glyph fallback
remains open. These are compiled contracts and cooked-file inventory, not
live-game compatibility evidence.

The source contract also freezes
`KismetRenderingLibrary.ImportBufferAsTexture2D` at `0x20` parameter bytes
with exact World-context, byte-array, and `Texture2D` return properties. Its
input codec accepts only bounded nonempty byte storage. A deterministic
project-owned RGBA8 PNG encoder provides the worker-side source bytes without
accessing UObjects. The production adapter validates the exact Kismet library
CDO/function/return class, imports only on the game thread against the active
World, roots only a newly returned exact `Texture2D`, publishes a bounded
monotonic opaque handle, and revalidates its weak UObject generation and root
ownership before complete-frame dispatch. Release and terminal teardown clear
only matching project-owned roots. Visible Canvas output and lifetime through
travel/HUD replacement remain open live work under GAME-HUD-007.

v1 also probes `PostRender` and `ReceivePostRender`. v2 must choose one
documented callback path for the pinned runtime/game, store its UE4SS callback
ID, and reject an unvalidated fallback. `DrawLine`, `DrawText`, and `Project`
aliases are inventory only, not a best-effort chain.

## ESP object and property inventory

| Contract ID | Candidate owner/member | Purpose |
| --- | --- | --- |
| GAME-ESP-001 | World `GameState` | Match roster root |
| GAME-ESP-002 | GameState `PlayerArray` | Authoritative player-state set |
| GAME-ESP-003 | Hider roster candidates: `Survivors`, `LiveSurvivors_PlayerState`, `Hiders`, `HiderPlayers`, `HiderPlayerStates`, `HiderPawns`, `HiderCharacters`, `CurrentHider` | Resolve hider role |
| GAME-ESP-004 | Hunter roster candidates: `Hunters`, `HuntersPlayerState`, `HunterPlayers`, `HunterPlayerStates`, `HunterPawns`, `HunterCharacters`, `CurrentHunter` | Resolve hunter role |
| GAME-ESP-005 | PlayerState/pawn links: `PlayerState`, `Pawn`, `PawnPrivate` and validated equivalents | Resolve current avatar |
| GAME-ESP-006 | Pawn mesh candidates including `Mesh` | Skeletal mesh |
| GAME-ESP-007 | `SceneComponent.ComponentToWorld` | Current component transform |
| GAME-ESP-008 | Validated component-space pose accessor/array | Skeleton pose |
| GAME-ESP-009 | Validated display-name property/function | Name label |
| GAME-ESP-010 | Validated spectator/role evidence | Exclude spectators before geometry |
| GAME-ESP-011 | `PlayerController.PlayerCameraManager` plus validated camera/view function/property | Projection view |

Phase 4/10 must collapse roster aliases based on live class/property-kind
evidence. A candidate list is not a license to accept whichever memory happens
to look plausible.

## Profile inputs

All six current files use schema version 2 and texture size 1024.

| Body | Raw mesh profile | Derived image profile | Asset/export | Counts |
| --- | --- | --- | --- | --- |
| round | `paintman.mesh-profile-v2.json` | `paintman.image-profile-v2.json` | `.../skeltal/paintman.uasset` / `paintman` | 1,660 vertices; 8,352 indices |
| cube | `paintman_cube.mesh-profile-v2.json` | `paintman_cube.image-profile-v2.json` | `.../skeltal_cube/paintman_cube.uasset` / `paintman_cube` | 452 vertices; 1,080 indices |
| fukuyoka | `paintman_hukuyoka.mesh-profile-v2.json` | `paintman_hukuyoka.image-profile-v2.json` | `.../skeltal/paintman_hukuyoka.uasset` / `paintman_hukuyoka` | 1,508 vertices; 7,584 indices |

Required validation includes profile/schema ID, source asset identity, export,
LOD, vertex/index counts, topology/hash fields, texture size, region mapping,
skin/pose inputs, derived `BaseProfileId`, and all index bounds. The persisted
and UI body name is `fukuyoka`; only the game asset alias uses `hukuyoka`.

The derived Image profile and editor guide must remain tied to the exact raw
profile. Phase 1 treats the separate UnrealMappingsDumper workflow as retained
research tooling until UE4SS mapping equivalence is proven.

Production Image Paint uses these exact packaged profile pairs as its
project-owned UV/topology source. The raw decoder must retain validated ordered
vertex UVs, triangle indices, and UV-island identities; the paired
ImageReference index order must match exactly. Sampling and barycentric-anchor
expansion run only on the owned planning worker and remain bounded by the core
sample limit.

The live game-thread gate validates
`RuntimePaintableComponent.TargetMeshComponent` as the expected weak object
property, follows it only while its object identity is live, validates the
target as a `SkinnedMeshComponent`, and validates
`SkinnedMeshComponent.SkinnedAsset` as the expected object property. The live
asset path and export must exactly match the selected profile entry. This gate
does not read an unreflected runtime triangle cache, scan component memory,
guess offsets/strides, or accept a count/UV resemblance as identity evidence.
A missing or mismatched reflected link is an actionable compatibility failure
and performs no mutation.

## Phase 4 compatibility-gate output

Phase 4 must replace this inventory with a machine-readable compatibility table
and tests that report at least:

- contract ID
- expected owner/class
- accepted game/engine version
- function/property kind
- total parameter size and each parameter's name/kind/offset/direction
- object generation/lifetime validation
- resolved source (UE4SS/reflection)
- status: compatible, unsupported game build, or runtime contract error
- actionable failure with no mutation

No contract may be marked compatible from source inspection alone. The live
gate supplies the final current-game evidence.
