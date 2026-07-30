#pragma once

#include <meccha/launcher/shared_mod.hpp>

#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace meccha::launcher::detail
{
struct SharedModLedger
{
    std::string product_version{};
    Sha256Digest manifest_sha256{};
    std::vector<OwnershipRecord> files{};

    auto operator==(const SharedModLedger&) const -> bool = default;
};

[[nodiscard]] auto make_shared_mod_transition_ledger(
    const std::optional<SharedModLedger>& installed,
    const SharedModLedger& current) -> SharedModLedger;

[[nodiscard]] auto serialize_shared_mod_ledger(
    const SharedModLedger& ledger)
    -> std::expected<std::string, SharedModError>;

[[nodiscard]] auto parse_shared_mod_ledger(
    std::string_view json)
    -> std::expected<SharedModLedger, SharedModError>;
} // namespace meccha::launcher::detail
