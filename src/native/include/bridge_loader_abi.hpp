#pragma once

#include <cstdint>
#include <windows.h>

extern "C" {

constexpr std::uint32_t McLoaderAbiMajor = 1;
constexpr std::uint32_t McLoaderAbiMinor = 0;

enum McResult : std::uint32_t
{
    MC_OK = 0,
    MC_E_INVALID_ARGUMENT = 1,
    MC_E_ABI_INCOMPATIBLE = 2,
    MC_E_ALREADY_STARTED = 3,
    MC_E_NOT_STARTED = 4,
    MC_E_START_FAILED = 5,
    MC_E_STOP_TIMED_OUT = 6,
    MC_E_UNLOAD_BLOCKED = 7,
    MC_E_INTERNAL = 8
};

enum McBridgeRunState : std::uint32_t
{
    MC_BRIDGE_CREATED = 0,
    MC_BRIDGE_STARTING = 1,
    MC_BRIDGE_RUNNING_NOT_LISTENING = 2,
    MC_BRIDGE_RUNNING_LISTENING = 3,
    MC_BRIDGE_STOPPING = 4,
    MC_BRIDGE_STOPPED = 5,
    MC_BRIDGE_UNLOADABLE = 6,
    MC_BRIDGE_FAILED = 7
};

enum McUnloadBlockers : std::uint32_t
{
    MC_BLOCK_NONE = 0,
    MC_BLOCK_LISTENER = 1 << 0,
    MC_BLOCK_CLIENTS = 1 << 1,
    MC_BLOCK_WORKERS = 1 << 2,
    MC_BLOCK_HOOKS = 1 << 3,
    MC_BLOCK_HOOK_CALLBACKS = 1 << 4,
    MC_BLOCK_UE_CALLS = 1 << 5,
    MC_BLOCK_PREVIEW_STATE = 1 << 6,
    MC_BLOCK_PAINT_QUEUE = 1 << 7,
    MC_BLOCK_UNKNOWN_REFCOUNT = 1 << 8
};

using McBridgeHandle = void*;

struct McBridgeStartInfo
{
    std::uint32_t size;
    std::uint32_t flags;
    const wchar_t* bridgePath;
    const wchar_t* runtimeDir;
    const wchar_t* logDir;
    const wchar_t* statusDir;
    const char* bridgeBuildIdUtf8;
    const char* expectedBridgeSha256Utf8;
    const char* expectedGameHashUtf8;
    const char* tcpBindHostUtf8;
    std::uint16_t tcpPortHint;
    std::uint16_t reserved0;
    std::uint32_t appProtocolMajor;
    std::uint32_t appProtocolMinor;
};

struct McBridgeStatus
{
    std::uint32_t size;
    McBridgeRunState state;
    McResult lastResult;
    std::uint32_t lastWin32;
    std::uint32_t unloadBlockers;
    std::uint32_t activeHookCallbacks;
    std::uint32_t activeUeCalls;
    std::uint32_t activeWorkers;
    std::uint32_t activeClients;
    std::uint32_t queuedCommands;
    std::uint32_t queuedPaintBatches;
    std::uint16_t tcpPort;
    std::uint16_t reserved0;
    char bridgeBuildIdUtf8[96];
    char lastStepUtf8[128];
    char lastErrorUtf8[512];
};

struct McBridgeHostApi
{
    std::uint32_t size;
    std::uint32_t abiMajor;
    std::uint32_t abiMinor;
    void* hostContext;
    void(WINAPI* EmitLog)(void* hostContext, std::uint32_t level, const char* messageUtf8, std::uint32_t messageBytes);
    void(WINAPI* PublishStatus)(void* hostContext, const McBridgeStatus* status);
};

struct McBridgeApi
{
    std::uint32_t size;
    std::uint32_t abiMajor;
    std::uint32_t abiMinor;
    std::uint64_t capabilities;
    McResult(WINAPI* Create)(const McBridgeStartInfo* startInfo, const McBridgeHostApi* hostApi, McBridgeHandle* outHandle);
    McResult(WINAPI* Start)(McBridgeHandle handle);
    McResult(WINAPI* RequestStop)(McBridgeHandle handle, std::uint32_t flags);
    McResult(WINAPI* JoinStop)(McBridgeHandle handle, std::uint32_t timeoutMs);
    McResult(WINAPI* GetStatus)(McBridgeHandle handle, McBridgeStatus* outStatus);
    McResult(WINAPI* Destroy)(McBridgeHandle handle);
};

using McBridgeGetApiFn = McResult(WINAPI*)(std::uint32_t loaderAbiMajor, std::uint32_t loaderAbiMinor, McBridgeApi* outApi);
using McLoaderRemoteMainFn = DWORD(WINAPI*)(void* remoteUtf16ConfigPath);

}
