#include <meccha/launcher/game_discovery.hpp>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace
{
using namespace meccha::launcher;
namespace fs = std::filesystem;

class TemporaryTree
{
public:
    TemporaryTree()
    {
        const auto nonce = std::chrono::steady_clock::now()
                               .time_since_epoch()
                               .count();
        root = fs::temp_directory_path() /
               ("meccha-v2-discovery-" + std::to_string(nonce));
        fs::create_directories(root);
    }

    TemporaryTree(const TemporaryTree&) = delete;
    auto operator=(const TemporaryTree&) -> TemporaryTree& = delete;

    ~TemporaryTree()
    {
        std::error_code ignored{};
        fs::remove_all(root, ignored);
    }

    fs::path root{};
};

auto write_text(const fs::path& path, std::string_view value) -> void
{
    fs::create_directories(path.parent_path());
    std::ofstream output{path, std::ios::binary | std::ios::trunc};
    output.write(value.data(), static_cast<std::streamsize>(value.size()));
}

auto make_game(
    const fs::path& library,
    std::string_view install_name = "MECCHA CHAMELEON") -> fs::path
{
    const auto install =
        library / "steamapps" / "common" / std::string{install_name};
    const auto binaries = install / "Chameleon" / "Binaries" / "Win64";
    fs::create_directories(binaries);
    write_text(binaries / TargetGameExecutable, "test executable");
    write_text(
        library / "steamapps" /
            ("appmanifest_" + std::string{TargetSteamAppId} + ".acf"),
        std::string{R"vdf("AppState"
{
    "appid" ")vdf"} +
            std::string{TargetSteamAppId} + R"vdf("
    "installdir" ")vdf" + std::string{install_name} + R"vdf("
}
)vdf");
    return install;
}

auto expect(bool condition, std::string_view message) -> bool
{
    if (!condition)
    {
        std::cerr << "FAIL game_discovery: " << message << '\n';
    }
    return condition;
}
} // namespace

auto main() -> int
{
    using namespace meccha::launcher;

    bool passed = true;

    const auto libraries = parse_steam_library_folders(
        R"vdf("libraryfolders"
{
    "0"
    {
        "path" "C:\\Program Files (x86)\\Steam"
        "apps" { "4704690" "1" }
    }
    "1" { "path" "D:\\SteamLibrary" }
}
)vdf");
    passed &= expect(
        libraries && libraries->size() == 2 &&
            libraries->at(0) == R"(C:\Program Files (x86)\Steam)" &&
            libraries->at(1) == R"(D:\SteamLibrary)",
        "current libraryfolders.vdf paths were not parsed");

    const auto old_libraries = parse_steam_library_folders(
        R"vdf("LibraryFolders" { "1" "E:\\Games" })vdf");
    passed &= expect(
        old_libraries && old_libraries->size() == 1 &&
            old_libraries->front() == R"(E:\Games)",
        "legacy libraryfolders.vdf path was not parsed");

    const auto malformed =
        parse_steam_library_folders(R"vdf("libraryfolders" { "0")vdf");
    passed &= expect(
        !malformed &&
            malformed.error().code ==
                GameDiscoveryErrorCode::MalformedVdf,
        "unterminated VDF was accepted");

    const auto traversal_manifest = parse_app_install_directory(
        R"vdf("AppState" { "appid" "4704690" "installdir" "..\\escape" })vdf");
    passed &= expect(
        !traversal_manifest &&
            traversal_manifest.error().code ==
                GameDiscoveryErrorCode::InvalidInstallDirectory,
        "a traversal install directory was accepted");

    TemporaryTree default_tree{};
    const auto default_install = make_game(default_tree.root);
    const std::vector default_roots{default_tree.root};
    const auto default_game = discover_game_installation(default_roots);
    passed &= expect(
        default_game &&
            default_game->install_directory ==
                fs::canonical(default_install) &&
            default_game->executable.filename() == TargetGameExecutable,
        "the default Steam library was not discovered");

    TemporaryTree split_tree{};
    const auto primary = split_tree.root / "primary";
    const auto secondary = split_tree.root / "secondary";
    fs::create_directories(primary / "steamapps");
    const auto secondary_install = make_game(secondary);
    write_text(
        primary / "steamapps" / "libraryfolders.vdf",
        std::string{R"vdf("libraryfolders" { "1" { "path" ")vdf"} +
            secondary.generic_string() + R"vdf(" } })vdf");
    const std::vector split_roots{primary};
    const auto split_game = discover_game_installation(split_roots);
    passed &= expect(
        split_game &&
            split_game->install_directory ==
                fs::canonical(secondary_install),
        "a non-default Steam library was not discovered");

    const auto from_install = validate_game_directory(secondary_install);
    const auto from_binaries = validate_game_directory(
        secondary_install / "Chameleon" / "Binaries" / "Win64");
    passed &= expect(
        from_install && from_binaries &&
            from_install->executable == from_binaries->executable,
        "explicit install and Win64 folders did not resolve identically");

    const auto wrong_folder =
        validate_game_directory(secondary_install / "Chameleon");
    passed &= expect(
        !wrong_folder &&
            wrong_folder.error().code ==
                GameDiscoveryErrorCode::MissingGame,
        "an unsupported folder-picker shape was accepted");

    TemporaryTree ambiguous_tree{};
    const auto first = ambiguous_tree.root / "first";
    const auto second = ambiguous_tree.root / "second";
    make_game(first);
    make_game(second);
    const std::vector ambiguous_roots{first, second};
    const auto ambiguous_game =
        discover_game_installation(ambiguous_roots);
    passed &= expect(
        !ambiguous_game &&
            ambiguous_game.error().code ==
                GameDiscoveryErrorCode::AmbiguousGame,
        "multiple installations were selected arbitrarily");

#ifdef _WIN32
    wchar_t module_path[32768]{};
    const auto module_length = GetModuleFileNameW(
        nullptr,
        module_path,
        static_cast<DWORD>(std::size(module_path)));
    passed &= expect(
        module_length != 0 &&
            module_length < static_cast<DWORD>(std::size(module_path)),
        "the test process path could not be read");
    if (module_length != 0 &&
        module_length < static_cast<DWORD>(std::size(module_path)))
    {
        const auto self_name = fs::path{module_path}.filename().wstring();
        const auto self_running =
            is_process_running_by_image_name(self_name);
        passed &= expect(
            self_running && *self_running,
            "the process snapshot did not find the calling test");
    }
#endif

    if (passed)
    {
        std::cout << "PASS game_discovery\n";
        return 0;
    }
    return 1;
}
