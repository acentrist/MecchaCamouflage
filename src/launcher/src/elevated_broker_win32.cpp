#include <meccha/launcher/elevated_broker.hpp>

#include <meccha/launcher/hash.hpp>
#include <meccha/launcher/owned_file_storage.hpp>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <objbase.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace meccha::launcher
{
namespace
{
namespace fs = std::filesystem;

auto effect_error(
    std::string_view operation,
    std::string detail)
    -> std::unexpected<LauncherEffectError>
{
    return std::unexpected(LauncherEffectError{
        std::string{operation} + ": " + std::move(detail),
    });
}

auto valid_nonce(std::string_view value) -> bool
{
    return value.size() == 32U &&
           std::ranges::all_of(
               value,
               [](char character)
               {
                   return (character >= '0' &&
                           character <= '9') ||
                          (character >= 'a' &&
                           character <= 'f');
               });
}

auto measurement(const OwnedFileExpectation& expectation)
    -> FileMeasurement
{
    return FileMeasurement{
        expectation.file.size,
        expectation.file.sha256,
    };
}

auto valid_material_file(
    const OwnedFileExpectation& expectation,
    std::span<const std::byte> bytes,
    const Sha256Digest& manifest_sha256,
    std::string_view path,
    FileRole role) -> bool
{
    const auto digest = sha256_bytes(bytes);
    return expectation.manifest_sha256 == manifest_sha256 &&
           expectation.file.path == path &&
           expectation.file.role == role &&
           expectation.file.size != 0U &&
           digest &&
           bytes.size() == expectation.file.size &&
           *digest == expectation.file.sha256;
}

auto valid_material(
    const ManagedLoaderMaterial& material,
    const Sha256Digest& manifest_sha256) -> bool
{
    return valid_material_file(
               material.proxy,
               material.proxy_bytes,
               manifest_sha256,
               "dwmapi.dll",
               FileRole::Proxy) &&
           valid_material_file(
               material.override_file,
               material.override_bytes,
               manifest_sha256,
               "override.txt",
               FileRole::Override);
}

auto state_matches(
    ArtifactDisposition disposition,
    ArtifactState state) -> bool
{
    switch (disposition)
    {
    case ArtifactDisposition::CreateOwned:
        return state == ArtifactState::Missing;
    case ArtifactDisposition::ReuseOwned:
        return state == ArtifactState::ExactOwned;
    case ArtifactDisposition::ReuseUnowned:
        return state == ArtifactState::ExactUnowned;
    case ArtifactDisposition::ReplaceOwned:
        return state == ArtifactState::OwnedPrevious;
    case ArtifactDisposition::None:
        return false;
    }
    return false;
}

struct LoaderStores
{
    Win32OwnedFileStore proxy;
    Win32OwnedFileStore override_file;
};

auto stores(
    const fs::path& game_directory,
    const fs::path& ownership_directory) -> LoaderStores
{
    return LoaderStores{
        Win32OwnedFileStore{
            game_directory / "dwmapi.dll",
            ownership_directory / "dwmapi.owner.json",
            "dwmapi.dll",
            FileRole::Proxy,
        },
        Win32OwnedFileStore{
            game_directory / "override.txt",
            ownership_directory / "override.owner.json",
            "override.txt",
            FileRole::Override,
        },
    };
}

auto recover_both(LoaderStores& value)
    -> std::expected<void, LauncherEffectError>
{
    const auto proxy = value.proxy.recover();
    const auto override_file = value.override_file.recover();
    if (!proxy)
    {
        return effect_error(
            "Elevated loader receipt recovery",
            "dwmapi.dll: " + proxy.error().detail);
    }
    if (!override_file)
    {
        return effect_error(
            "Elevated loader receipt recovery",
            "override.txt: " + override_file.error().detail);
    }
    return {};
}

auto recover_after_failure(
    LoaderStores& value,
    LauncherEffectError primary)
    -> std::unexpected<LauncherEffectError>
{
    const auto recovered = recover_both(value);
    if (!recovered)
    {
        primary.detail += "; ";
        primary.detail += recovered.error().detail;
    }
    return std::unexpected(std::move(primary));
}

auto observe_both(
    LoaderStores& value,
    const ManagedLoaderMaterial& material)
    -> std::expected<
        ManagedLoaderObservation,
        LauncherEffectError>
{
    const auto proxy = value.proxy.observe(material.proxy);
    if (!proxy)
    {
        return effect_error(
            "Elevated loader observation",
            "dwmapi.dll: " + proxy.error().detail);
    }
    const auto override_file =
        value.override_file.observe(material.override_file);
    if (!override_file)
    {
        return effect_error(
            "Elevated loader observation",
            "override.txt: " + override_file.error().detail);
    }
    return ManagedLoaderObservation{
        *proxy,
        *override_file,
    };
}

auto prepare_apply_file(
    ArtifactDisposition disposition,
    Win32OwnedFileStore& store,
    const OwnedFileExpectation& expectation)
    -> std::expected<
        ElevatedLoaderFileMutation,
        LauncherEffectError>
{
    const auto desired = measurement(expectation);
    if (disposition == ArtifactDisposition::ReuseOwned ||
        disposition == ArtifactDisposition::ReuseUnowned)
    {
        return ElevatedLoaderFileMutation{
            ElevatedLoaderFileAction::Verify,
            desired,
            desired,
        };
    }
    const auto intent =
        store.prepare_external_install(expectation);
    if (!intent)
    {
        return effect_error(
            "Elevated loader receipt intent",
            expectation.file.path + ": " +
                intent.error().detail);
    }
    const auto expected_result =
        disposition == ArtifactDisposition::CreateOwned
            ? OwnedFileInstallResult::Created
            : OwnedFileInstallResult::Replaced;
    if (intent->result != expected_result &&
        !(disposition == ArtifactDisposition::ReplaceOwned &&
          intent->result == OwnedFileInstallResult::Reused))
    {
        return effect_error(
            "Elevated loader receipt intent",
            expectation.file.path +
                ": filesystem state changed after planning.");
    }
    if (!intent->mutation_required)
    {
        return ElevatedLoaderFileMutation{
            ElevatedLoaderFileAction::Verify,
            intent->desired,
            intent->desired,
        };
    }
    return ElevatedLoaderFileMutation{
        ElevatedLoaderFileAction::Install,
        intent->expected_current,
        intent->desired,
    };
}

auto finalize_apply_file(
    const ElevatedLoaderFileMutation& mutation,
    Win32OwnedFileStore& store,
    const OwnedFileExpectation& expectation)
    -> std::expected<void, LauncherEffectError>
{
    if (mutation.action != ElevatedLoaderFileAction::Install)
    {
        return {};
    }
    const auto finalized =
        store.finalize_external_install(expectation);
    if (!finalized)
    {
        return effect_error(
            "Elevated loader receipt finalization",
            expectation.file.path + ": " +
                finalized.error().detail);
    }
    return {};
}

auto prepare_remove_file(
    RemovalAction action,
    Win32OwnedFileStore& store,
    std::string_view path)
    -> std::expected<
        ElevatedLoaderFileMutation,
        LauncherEffectError>
{
    if (action == RemovalAction::None)
    {
        return ElevatedLoaderFileMutation{};
    }
    const auto intent = store.prepare_external_remove();
    if (!intent)
    {
        return effect_error(
            "Elevated loader removal intent",
            std::string{path} + ": " + intent.error().detail);
    }
    if (!*intent)
    {
        return effect_error(
            "Elevated loader removal intent",
            std::string{path} +
                ": owned target disappeared after planning.");
    }
    return ElevatedLoaderFileMutation{
        ElevatedLoaderFileAction::Remove,
        (**intent).expected_current,
        {},
    };
}

auto finalize_remove_file(
    const ElevatedLoaderFileMutation& mutation,
    Win32OwnedFileStore& store,
    std::string_view path)
    -> std::expected<void, LauncherEffectError>
{
    if (mutation.action != ElevatedLoaderFileAction::Remove)
    {
        return {};
    }
    const auto finalized = store.finalize_external_remove();
    if (!finalized)
    {
        return effect_error(
            "Elevated loader removal finalization",
            std::string{path} + ": " +
                finalized.error().detail);
    }
    return {};
}

auto client_error(const ElevatedLoaderMutationError& cause)
    -> LauncherEffectError
{
    return LauncherEffectError{
        "Elevated loader client: " + cause.detail,
    };
}

auto result_matches(
    const ElevatedLoaderMutationResult& result,
    const ElevatedLoaderMutationRequest& request) -> bool
{
    const auto mutates = [](ElevatedLoaderFileAction action)
    {
        return action == ElevatedLoaderFileAction::Install ||
               action == ElevatedLoaderFileAction::Remove;
    };
    return result.proxy_mutated ==
               mutates(request.proxy.action) &&
           result.override_mutated ==
               mutates(request.override_file.action);
}
} // namespace

auto Win32ElevatedBrokerNonceSource::next_nonce()
    -> std::expected<
        std::string,
        LauncherEffectError>
{
    GUID guid{};
    if (FAILED(CoCreateGuid(&guid)))
    {
        return std::unexpected(LauncherEffectError{
            "Elevated loader broker: a private request nonce "
            "could not be generated.",
        });
    }
    constexpr std::array<char, 16> Hex{
        '0', '1', '2', '3', '4', '5', '6', '7',
        '8', '9', 'a', 'b', 'c', 'd', 'e', 'f',
    };
    static_assert(sizeof(GUID) == 16U);
    const auto bytes =
        std::as_bytes(std::span{&guid, 1U});
    auto result = std::string{};
    result.reserve(32U);
    for (const auto value : bytes)
    {
        const auto byte =
            std::to_integer<unsigned int>(value);
        result.push_back(Hex[byte >> 4U]);
        result.push_back(Hex[byte & 0x0FU]);
    }
    return result;
}

Win32ElevatedLoaderBrokerProvider::
    Win32ElevatedLoaderBrokerProvider(
        ElevatedBrokerNonceSource& nonce_source,
        ElevatedLoaderMutationClient& client)
    : nonce_source_(nonce_source),
      client_(client)
{
}

auto Win32ElevatedLoaderBrokerProvider::bind(
    const Sha256Digest& accepted_manifest_sha256)
    -> std::expected<
        ElevatedLoaderBroker*,
        LauncherEffectError>
{
    if (bound_manifest_sha256_ &&
        *bound_manifest_sha256_ !=
            accepted_manifest_sha256)
    {
        return std::unexpected(LauncherEffectError{
            "Elevated loader broker: a provider instance cannot "
            "be rebound to a different payload manifest.",
        });
    }
    if (!broker_)
    {
        broker_ = std::make_unique<
            Win32OriginalUserElevatedLoaderBroker>(
            accepted_manifest_sha256,
            nonce_source_,
            client_);
        bound_manifest_sha256_ =
            accepted_manifest_sha256;
    }
    return broker_.get();
}

Win32OriginalUserElevatedLoaderBroker::
    Win32OriginalUserElevatedLoaderBroker(
        Sha256Digest accepted_manifest_sha256,
        ElevatedBrokerNonceSource& nonce_source,
        ElevatedLoaderMutationClient& client)
    : accepted_manifest_sha256_(
          accepted_manifest_sha256),
      nonce_source_(nonce_source),
      client_(client)
{
}

auto Win32OriginalUserElevatedLoaderBroker::apply(
    const ManagedLoaderPlan& plan,
    const fs::path& game_directory,
    const fs::path& ownership_directory,
    const ManagedLoaderMaterial& material)
    -> std::expected<void, LauncherEffectError>
{
    if (!plan.elevated ||
        !valid_material(
            material,
            accepted_manifest_sha256_))
    {
        return effect_error(
            "Elevated loader application",
            "The plan or independently verified material is invalid.");
    }
    auto loader_stores =
        stores(game_directory, ownership_directory);
    auto recovered = recover_both(loader_stores);
    if (!recovered)
    {
        return std::unexpected(recovered.error());
    }
    const auto observation =
        observe_both(loader_stores, material);
    if (!observation)
    {
        return std::unexpected(observation.error());
    }
    if (!state_matches(plan.proxy, observation->proxy) ||
        !state_matches(
            plan.override_file,
            observation->override_file))
    {
        return effect_error(
            "Elevated loader application",
            "The loader filesystem changed after planning.");
    }

    const auto proxy = prepare_apply_file(
        plan.proxy,
        loader_stores.proxy,
        material.proxy);
    if (!proxy)
    {
        return recover_after_failure(
            loader_stores,
            proxy.error());
    }
    const auto override_file = prepare_apply_file(
        plan.override_file,
        loader_stores.override_file,
        material.override_file);
    if (!override_file)
    {
        return recover_after_failure(
            loader_stores,
            override_file.error());
    }
    const auto mutates =
        proxy->action == ElevatedLoaderFileAction::Install ||
        override_file->action ==
            ElevatedLoaderFileAction::Install;
    if (!mutates)
    {
        return {};
    }

    const auto nonce = nonce_source_.next_nonce();
    if (!nonce || !valid_nonce(*nonce))
    {
        return recover_after_failure(
            loader_stores,
            LauncherEffectError{
                "Elevated loader application: " +
                (nonce
                     ? std::string{
                           "The broker nonce source returned an "
                           "invalid nonce."}
                     : nonce.error().detail),
            });
    }
    const auto request = ElevatedLoaderMutationRequest{
        ElevatedLoaderMutationSchemaVersion,
        ElevatedLoaderOperation::Apply,
        accepted_manifest_sha256_,
        *nonce,
        game_directory,
        *proxy,
        *override_file,
    };
    const auto executed = client_.execute(request);
    if (!executed)
    {
        return recover_after_failure(
            loader_stores,
            client_error(executed.error()));
    }
    if (!result_matches(*executed, request))
    {
        return recover_after_failure(
            loader_stores,
            LauncherEffectError{
                "Elevated loader application: The privileged "
                "result did not match the requested mutation set.",
            });
    }
    const auto proxy_finalized = finalize_apply_file(
        *proxy,
        loader_stores.proxy,
        material.proxy);
    const auto override_finalized = finalize_apply_file(
        *override_file,
        loader_stores.override_file,
        material.override_file);
    if (!proxy_finalized)
    {
        return recover_after_failure(
            loader_stores,
            proxy_finalized.error());
    }
    if (!override_finalized)
    {
        return recover_after_failure(
            loader_stores,
            override_finalized.error());
    }
    return {};
}

auto Win32OriginalUserElevatedLoaderBroker::remove(
    const RemovalPlan& plan,
    const fs::path& game_directory,
    const fs::path& ownership_directory)
    -> std::expected<void, LauncherEffectError>
{
    if (!plan.elevated_loader ||
        plan.runtime_cache != RemovalAction::None ||
        plan.mod != RemovalAction::None)
    {
        return effect_error(
            "Elevated loader removal",
            "The removal plan contains a non-loader action.");
    }
    auto loader_stores =
        stores(game_directory, ownership_directory);
    auto recovered = recover_both(loader_stores);
    if (!recovered)
    {
        return std::unexpected(recovered.error());
    }
    const auto proxy_removable =
        loader_stores.proxy.removable();
    const auto override_removable =
        loader_stores.override_file.removable();
    if (!proxy_removable)
    {
        return effect_error(
            "Elevated loader removal",
            "dwmapi.dll: " + proxy_removable.error().detail);
    }
    if (!override_removable)
    {
        return effect_error(
            "Elevated loader removal",
            "override.txt: " +
                override_removable.error().detail);
    }
    if ((plan.proxy == RemovalAction::RemoveOwned) !=
            *proxy_removable ||
        (plan.override_file == RemovalAction::RemoveOwned) !=
            *override_removable)
    {
        return effect_error(
            "Elevated loader removal",
            "The loader filesystem changed after removal planning.");
    }

    const auto proxy = prepare_remove_file(
        plan.proxy,
        loader_stores.proxy,
        "dwmapi.dll");
    if (!proxy)
    {
        return recover_after_failure(
            loader_stores,
            proxy.error());
    }
    const auto override_file = prepare_remove_file(
        plan.override_file,
        loader_stores.override_file,
        "override.txt");
    if (!override_file)
    {
        return recover_after_failure(
            loader_stores,
            override_file.error());
    }
    const auto mutates =
        proxy->action == ElevatedLoaderFileAction::Remove ||
        override_file->action ==
            ElevatedLoaderFileAction::Remove;
    if (!mutates)
    {
        return {};
    }

    const auto nonce = nonce_source_.next_nonce();
    if (!nonce || !valid_nonce(*nonce))
    {
        return recover_after_failure(
            loader_stores,
            LauncherEffectError{
                "Elevated loader removal: " +
                (nonce
                     ? std::string{
                           "The broker nonce source returned an "
                           "invalid nonce."}
                     : nonce.error().detail),
            });
    }
    const auto request = ElevatedLoaderMutationRequest{
        ElevatedLoaderMutationSchemaVersion,
        ElevatedLoaderOperation::Remove,
        accepted_manifest_sha256_,
        *nonce,
        game_directory,
        *proxy,
        *override_file,
    };
    const auto executed = client_.execute(request);
    if (!executed)
    {
        return recover_after_failure(
            loader_stores,
            client_error(executed.error()));
    }
    if (!result_matches(*executed, request))
    {
        return recover_after_failure(
            loader_stores,
            LauncherEffectError{
                "Elevated loader removal: The privileged result "
                "did not match the requested mutation set.",
            });
    }
    const auto proxy_finalized = finalize_remove_file(
        *proxy,
        loader_stores.proxy,
        "dwmapi.dll");
    const auto override_finalized = finalize_remove_file(
        *override_file,
        loader_stores.override_file,
        "override.txt");
    if (!proxy_finalized)
    {
        return recover_after_failure(
            loader_stores,
            proxy_finalized.error());
    }
    if (!override_finalized)
    {
        return recover_after_failure(
            loader_stores,
            override_finalized.error());
    }
    return {};
}
} // namespace meccha::launcher
