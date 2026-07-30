#pragma once

#include <cstdint>

namespace meccha::launcher
{
enum class DirectiveState : std::uint8_t
{
    Absent,
    Owned,
    Unowned,
    Invalid,
};

enum class CandidateIdentity : std::uint8_t
{
    Missing,
    Pinned,
    Incompatible,
    Unreadable,
};

enum class LoaderResolutionState : std::uint8_t
{
    Missing,
    ManagedCompatible,
    SharedCompatible,
    Conflict,
};

enum class LoaderSource : std::uint8_t
{
    None,
    CommandLine,
    Override,
    ConventionalSubdirectory,
    ConventionalRoot,
};

enum class LoaderConflict : std::uint8_t
{
    None,
    CommandLine,
    Override,
    FallbackCandidate,
    Observation,
};

struct LoaderChainObservation
{
    DirectiveState command_line{};
    CandidateIdentity command_target{};
    DirectiveState override_file{};
    CandidateIdentity override_target{};
    CandidateIdentity conventional_subdirectory{};
    CandidateIdentity conventional_root{};

    auto operator==(const LoaderChainObservation&) const -> bool = default;
};

struct LoaderResolution
{
    LoaderResolutionState state{LoaderResolutionState::Conflict};
    LoaderSource source{LoaderSource::None};
    LoaderConflict conflict{LoaderConflict::Observation};

    auto operator==(const LoaderResolution&) const -> bool = default;
};

[[nodiscard]] auto resolve_loader_chain(
    const LoaderChainObservation& observation) -> LoaderResolution;
} // namespace meccha::launcher
