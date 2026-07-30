#include <meccha/launcher/ownership.hpp>
#include <meccha/launcher/policy.hpp>

#include <cstddef>
#include <iostream>
#include <optional>
#include <string_view>

namespace
{
auto expect(bool condition, std::string_view message) -> bool
{
    if (!condition)
    {
        std::cerr << "FAIL launcher_policy: " << message << '\n';
    }
    return condition;
}

auto digest(std::byte value) -> meccha::launcher::Sha256Digest
{
    meccha::launcher::Sha256Digest result{};
    result.bytes.fill(value);
    return result;
}
} // namespace

auto main() -> int
{
    using namespace meccha::launcher;

    const OwnedFileExpectation expected{
        "2.0.0",
        digest(std::byte{0x11}),
        ManifestFile{"dwmapi.dll", FileRole::Proxy, 1024, digest(std::byte{0x22})},
    };
    const FileMeasurement matching_file{1024, digest(std::byte{0x22})};
    const OwnershipRecord matching_record{
        "2.0.0",
        digest(std::byte{0x11}),
        ManifestFile{"dwmapi.dll", FileRole::Proxy, 1024, digest(std::byte{0x22})},
    };

    bool passed = true;
    passed &= expect(
        classify_owned_file(expected, std::nullopt, std::nullopt) ==
            ArtifactState::Missing,
        "an absent file was not classified as missing");
    passed &= expect(
        classify_owned_file(expected, matching_file, std::nullopt) ==
            ArtifactState::ExactUnowned,
        "an exact unrecorded file was not classified as unowned");
    passed &= expect(
        classify_owned_file(expected, matching_file, matching_record) ==
            ArtifactState::ExactOwned,
        "an exact recorded file was not classified as owned");

    auto wrong_record = matching_record;
    wrong_record.product_version = "1.6.7";
    passed &= expect(
        classify_owned_file(expected, matching_file, wrong_record) ==
            ArtifactState::Conflict,
        "a mismatched ownership record was trusted");

    auto changed_file = matching_file;
    changed_file.sha256 = digest(std::byte{0x33});
    passed &= expect(
        classify_owned_file(expected, changed_file, matching_record) ==
            ArtifactState::Conflict,
        "changed owned content was trusted");
    passed &= expect(
        classify_owned_file(expected, std::nullopt, matching_record) ==
            ArtifactState::Missing,
        "a stale record made an absent file conflict");

    const DeploymentObservation clean{
        LaunchOptionState::Absent,
        ArtifactState::Missing,
        ArtifactState::Missing,
        RuntimeState::Missing,
        SettingsState::Missing,
        ArtifactState::Missing,
    };
    const auto managed = select_deployment(clean);
    passed &= expect(
        managed.mode == DeploymentMode::Managed,
        "a clean tree did not select managed mode");
    passed &= expect(
        managed.proxy == ArtifactDisposition::CreateOwned &&
            managed.override_file == ArtifactDisposition::CreateOwned &&
            managed.mod == ArtifactDisposition::CreateOwned,
        "managed mode proposed the wrong mutation set");

    auto reused_proxy = clean;
    reused_proxy.proxy = ArtifactState::ExactUnowned;
    const auto managed_reuse = select_deployment(reused_proxy);
    passed &= expect(
        managed_reuse.mode == DeploymentMode::Managed &&
            managed_reuse.proxy == ArtifactDisposition::ReuseUnowned,
        "an exact unowned proxy was not safely reused");

    auto unowned_override = clean;
    unowned_override.override_file = ArtifactState::ExactUnowned;
    const auto override_conflict = select_deployment(unowned_override);
    passed &= expect(
        override_conflict.mode == DeploymentMode::Conflict &&
            override_conflict.conflict == ConflictReason::UnownedOverride,
        "managed mode attempted to take ownership of an existing override");

    const DeploymentObservation shared{
        LaunchOptionState::PinnedRuntime,
        ArtifactState::ExactUnowned,
        ArtifactState::ExactUnowned,
        RuntimeState::SharedCompatible,
        SettingsState::Compatible,
        ArtifactState::Missing,
    };
    const auto shared_decision = select_deployment(shared);
    passed &= expect(
        shared_decision.mode == DeploymentMode::Shared,
        "an exact compatible loader did not select shared mode");
    passed &= expect(
        shared_decision.proxy == ArtifactDisposition::None &&
            shared_decision.override_file == ArtifactDisposition::None &&
            shared_decision.mod == ArtifactDisposition::CreateOwned,
        "shared mode proposed a loader mutation");

    auto shared_existing_mod = shared;
    shared_existing_mod.mod = ArtifactState::ExactUnowned;
    const auto shared_reuse = select_deployment(shared_existing_mod);
    passed &= expect(
        shared_reuse.mode == DeploymentMode::Shared &&
            shared_reuse.mod == ArtifactDisposition::ReuseUnowned,
        "an exact shared mod was not left untouched");

    auto unknown_proxy = clean;
    unknown_proxy.proxy = ArtifactState::Conflict;
    const auto proxy_conflict = select_deployment(unknown_proxy);
    passed &= expect(
        proxy_conflict.mode == DeploymentMode::Conflict &&
            proxy_conflict.conflict == ConflictReason::Proxy,
        "an unknown proxy did not fail closed");

    auto incompatible_runtime = shared;
    incompatible_runtime.runtime = RuntimeState::Incompatible;
    const auto runtime_conflict = select_deployment(incompatible_runtime);
    passed &= expect(
        runtime_conflict.mode == DeploymentMode::Conflict &&
            runtime_conflict.conflict == ConflictReason::Runtime,
        "an incompatible runtime did not fail closed");

    const DeploymentObservation existing_managed{
        LaunchOptionState::Absent,
        ArtifactState::ExactOwned,
        ArtifactState::ExactOwned,
        RuntimeState::ManagedCompatible,
        SettingsState::Compatible,
        ArtifactState::ExactOwned,
    };
    const auto managed_existing = select_deployment(existing_managed);
    passed &= expect(
        managed_existing.mode == DeploymentMode::Managed &&
            managed_existing.proxy == ArtifactDisposition::ReuseOwned &&
            managed_existing.override_file == ArtifactDisposition::ReuseOwned &&
            managed_existing.mod == ArtifactDisposition::ReuseOwned,
        "an existing managed runtime was not reused");

    auto unresolved_launch_option = clean;
    unresolved_launch_option.launch_option = LaunchOptionState::Unresolved;
    const auto launch_option_conflict = select_deployment(unresolved_launch_option);
    passed &= expect(
        launch_option_conflict.mode == DeploymentMode::Conflict &&
            launch_option_conflict.conflict == ConflictReason::LaunchOption,
        "an unresolved launch option did not fail closed");

    if (passed)
    {
        std::cout << "PASS launcher_policy\n";
        return 0;
    }
    return 1;
}
