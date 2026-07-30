#include <meccha/launcher/loader.hpp>

#include <iostream>
#include <string_view>

namespace
{
auto expect(bool condition, std::string_view message) -> bool
{
    if (!condition)
    {
        std::cerr << "FAIL loader_resolution: " << message << '\n';
    }
    return condition;
}
} // namespace

auto main() -> int
{
    using namespace meccha::launcher;

    bool passed = true;
    const LoaderChainObservation empty{
        DirectiveState::Absent,
        CandidateIdentity::Missing,
        DirectiveState::Absent,
        CandidateIdentity::Missing,
        CandidateIdentity::Missing,
        CandidateIdentity::Missing,
    };
    passed &= expect(
        resolve_loader_chain(empty) ==
            LoaderResolution{
                LoaderResolutionState::Missing,
                LoaderSource::None,
                LoaderConflict::None,
            },
        "an empty chain did not resolve as missing");

    auto command_line = empty;
    command_line.command_line = DirectiveState::Unowned;
    command_line.command_target = CandidateIdentity::Pinned;
    command_line.override_file = DirectiveState::Unowned;
    command_line.override_target = CandidateIdentity::Pinned;
    command_line.conventional_subdirectory = CandidateIdentity::Pinned;
    passed &= expect(
        resolve_loader_chain(command_line) ==
            LoaderResolution{
                LoaderResolutionState::SharedCompatible,
                LoaderSource::CommandLine,
                LoaderConflict::None,
            },
        "the command-line path did not win");

    auto missing_command = command_line;
    missing_command.command_target = CandidateIdentity::Missing;
    passed &= expect(
        resolve_loader_chain(missing_command).conflict ==
            LoaderConflict::CommandLine,
        "a configured but missing command-line path fell through");

    auto owned_override = empty;
    owned_override.override_file = DirectiveState::Owned;
    passed &= expect(
        resolve_loader_chain(owned_override) ==
            LoaderResolution{
                LoaderResolutionState::Missing,
                LoaderSource::Override,
                LoaderConflict::None,
            },
        "a missing owned override target could not be repaired");

    owned_override.override_target = CandidateIdentity::Pinned;
    passed &= expect(
        resolve_loader_chain(owned_override).state ==
            LoaderResolutionState::ManagedCompatible,
        "an owned pinned override was not managed");

    auto shared_override = owned_override;
    shared_override.override_file = DirectiveState::Unowned;
    passed &= expect(
        resolve_loader_chain(shared_override).state ==
            LoaderResolutionState::SharedCompatible,
        "an unowned pinned override was not shared");

    auto invalid_override = empty;
    invalid_override.override_file = DirectiveState::Invalid;
    invalid_override.conventional_subdirectory = CandidateIdentity::Pinned;
    passed &= expect(
        resolve_loader_chain(invalid_override).conflict ==
            LoaderConflict::Override,
        "an invalid override fell through to a conventional runtime");

    auto conventional = empty;
    conventional.conventional_subdirectory = CandidateIdentity::Pinned;
    conventional.conventional_root = CandidateIdentity::Pinned;
    passed &= expect(
        resolve_loader_chain(conventional).source ==
            LoaderSource::ConventionalSubdirectory,
        "the ue4ss subdirectory did not win over the legacy root");

    conventional.conventional_root = CandidateIdentity::Incompatible;
    passed &= expect(
        resolve_loader_chain(conventional).conflict ==
            LoaderConflict::FallbackCandidate,
        "an incompatible fallback candidate was ignored");

    auto root_only = empty;
    root_only.conventional_root = CandidateIdentity::Pinned;
    passed &= expect(
        resolve_loader_chain(root_only) ==
            LoaderResolution{
                LoaderResolutionState::SharedCompatible,
                LoaderSource::ConventionalRoot,
                LoaderConflict::None,
            },
        "the legacy root candidate did not resolve");

    if (passed)
    {
        std::cout << "PASS loader_resolution\n";
        return 0;
    }
    return 1;
}
