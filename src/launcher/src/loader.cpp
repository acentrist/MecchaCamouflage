#include <meccha/launcher/loader.hpp>

namespace meccha::launcher
{
namespace
{
auto conflict(LoaderConflict reason, LoaderSource source = LoaderSource::None)
    -> LoaderResolution
{
    return LoaderResolution{
        LoaderResolutionState::Conflict,
        source,
        reason,
    };
}

auto candidate_is_invalid(CandidateIdentity identity) -> bool
{
    return identity == CandidateIdentity::Incompatible ||
           identity == CandidateIdentity::Unreadable;
}

auto candidate_is_present(CandidateIdentity identity) -> bool
{
    return identity != CandidateIdentity::Missing;
}
} // namespace

auto resolve_loader_chain(const LoaderChainObservation& observation)
    -> LoaderResolution
{
    if (observation.command_line == DirectiveState::Invalid ||
        observation.command_line == DirectiveState::Owned)
    {
        return conflict(LoaderConflict::CommandLine, LoaderSource::CommandLine);
    }
    if (observation.override_file == DirectiveState::Invalid)
    {
        return conflict(LoaderConflict::Override, LoaderSource::Override);
    }
    if (observation.command_line == DirectiveState::Absent &&
        observation.command_target != CandidateIdentity::Missing)
    {
        return conflict(LoaderConflict::Observation);
    }
    if (observation.override_file == DirectiveState::Absent &&
        observation.override_target != CandidateIdentity::Missing)
    {
        return conflict(LoaderConflict::Observation);
    }

    if (observation.command_line == DirectiveState::Unowned &&
        candidate_is_invalid(observation.command_target))
    {
        return conflict(LoaderConflict::CommandLine, LoaderSource::CommandLine);
    }
    if (observation.override_file != DirectiveState::Absent &&
        candidate_is_invalid(observation.override_target))
    {
        return conflict(LoaderConflict::Override, LoaderSource::Override);
    }
    if (candidate_is_invalid(observation.conventional_subdirectory) ||
        candidate_is_invalid(observation.conventional_root))
    {
        return conflict(LoaderConflict::FallbackCandidate);
    }

    if (observation.command_line == DirectiveState::Unowned)
    {
        if (observation.command_target != CandidateIdentity::Pinned)
        {
            return conflict(
                LoaderConflict::CommandLine,
                LoaderSource::CommandLine);
        }
        return LoaderResolution{
            LoaderResolutionState::SharedCompatible,
            LoaderSource::CommandLine,
            LoaderConflict::None,
        };
    }

    if (observation.override_file == DirectiveState::Owned)
    {
        if (observation.override_target == CandidateIdentity::Pinned)
        {
            return LoaderResolution{
                LoaderResolutionState::ManagedCompatible,
                LoaderSource::Override,
                LoaderConflict::None,
            };
        }
        if (candidate_is_present(observation.conventional_subdirectory) ||
            candidate_is_present(observation.conventional_root))
        {
            return conflict(
                LoaderConflict::FallbackCandidate,
                LoaderSource::Override);
        }
        return LoaderResolution{
            LoaderResolutionState::Missing,
            LoaderSource::Override,
            LoaderConflict::None,
        };
    }

    if (observation.override_file == DirectiveState::Unowned)
    {
        if (observation.override_target != CandidateIdentity::Pinned)
        {
            return conflict(
                LoaderConflict::Override,
                LoaderSource::Override);
        }
        return LoaderResolution{
            LoaderResolutionState::SharedCompatible,
            LoaderSource::Override,
            LoaderConflict::None,
        };
    }

    if (observation.conventional_subdirectory == CandidateIdentity::Pinned)
    {
        return LoaderResolution{
            LoaderResolutionState::SharedCompatible,
            LoaderSource::ConventionalSubdirectory,
            LoaderConflict::None,
        };
    }
    if (observation.conventional_root == CandidateIdentity::Pinned)
    {
        return LoaderResolution{
            LoaderResolutionState::SharedCompatible,
            LoaderSource::ConventionalRoot,
            LoaderConflict::None,
        };
    }
    return LoaderResolution{
        LoaderResolutionState::Missing,
        LoaderSource::None,
        LoaderConflict::None,
    };
}
} // namespace meccha::launcher
