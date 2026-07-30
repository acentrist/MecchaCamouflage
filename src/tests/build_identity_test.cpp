#include <meccha/build_identity.hpp>

#include <iostream>
#include <string_view>

namespace
{
constexpr std::string_view AcceptedUe4ssCommit{
    "6c26f038751b3d96059d4a9148f5d093012d55ad"};

auto expect(bool condition, std::string_view message) -> bool
{
    if (!condition)
    {
        std::cerr << "FAIL build_identity: " << message << '\n';
    }
    return condition;
}
} // namespace

auto main() -> int
{
    bool passed = true;
    passed &= expect(meccha::build::ProductName == "MecchaCamouflage",
                     "unexpected product name");
    passed &= expect(meccha::build::ProductVersion == "2.0.0",
                     "unexpected product version");
    passed &= expect(meccha::build::Ue4ssCommit == AcceptedUe4ssCommit,
                     "UE4SS commit is not the accepted architecture candidate");
    passed &= expect(meccha::build::ConfigSchemaVersion == 1,
                     "unexpected configuration schema");
    passed &= expect(meccha::build::PresetSchemaVersion == 1,
                     "unexpected preset schema");
    passed &= expect(meccha::build::PayloadManifestSchemaVersion == 1,
                     "unexpected payload manifest schema");

    if (passed)
    {
        std::cout << "PASS build_identity\n";
        return 0;
    }
    return 1;
}
