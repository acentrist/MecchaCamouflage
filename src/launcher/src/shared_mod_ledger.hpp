#pragma once

#include <meccha/launcher/shared_mod.hpp>

#include <expected>
#include <string>
#include <string_view>
#include <vector>

namespace meccha::launcher::detail
{
struct SharedModLedger
{
    std::string product_version{};
    Sha256Digest manifest_sha256{};
    std::vector<ManifestFile> files{};

    auto operator==(const SharedModLedger&) const -> bool = default;
};

[[nodiscard]] auto serialize_shared_mod_ledger(
    const SharedModLedger& ledger)
    -> std::expected<std::string, SharedModError>;

[[nodiscard]] auto parse_shared_mod_ledger(
    std::string_view json)
    -> std::expected<SharedModLedger, SharedModError>;
} // namespace meccha::launcher::detail
