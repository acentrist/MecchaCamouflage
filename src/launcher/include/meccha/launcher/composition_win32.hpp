#pragma once

#include <meccha/launcher/execution_win32.hpp>
#include <meccha/launcher/observation_win32.hpp>
#include <meccha/launcher/runtime_storage.hpp>
#include <meccha/launcher/workflow.hpp>

#include <expected>
#include <filesystem>
#include <string>
#include <string_view>
#include <variant>

namespace meccha::launcher
{
#ifdef _WIN32
struct Win32LauncherCompositionInputs
{
    LauncherInvocationMode mode{};
    const PayloadManifest& manifest;
    std::string_view manifest_json{};
    Sha256Digest manifest_sha256{};
    RuntimePayloadSource& payload_source;
    std::filesystem::path game_directory{};
    std::filesystem::path runtime_root{};
    std::filesystem::path ownership_directory{};
    std::string runtime_nonce{};
    OriginalUserObservationPlatform& observation_platform;
    ElevatedLoaderBroker& elevated_loader_broker;
    SteamGameLauncher& steam_launcher;
};

struct Win32LauncherCompositionResult
{
    RuntimeRecoveryResult recovery{};
    LauncherWorkflowResult workflow{};
};

using Win32LauncherCompositionError = std::variant<
    LauncherObservationError,
    RuntimeTransactionError,
    LauncherWorkflowError>;

[[nodiscard]] auto run_win32_launcher_composition(
    const Win32LauncherCompositionInputs& inputs)
    -> std::expected<
        Win32LauncherCompositionResult,
        Win32LauncherCompositionError>;
#endif
} // namespace meccha::launcher
