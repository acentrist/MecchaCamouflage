#include <meccha/launcher/game_discovery.hpp>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace meccha::launcher
{
namespace
{
namespace fs = std::filesystem;

constexpr std::size_t MaxVdfBytes = 4U * 1024U * 1024U;
constexpr std::size_t MaxVdfStringBytes = 32U * 1024U;
constexpr std::size_t MaxVdfEntries = 10000U;
constexpr std::size_t MaxVdfDepth = 16U;

enum class TokenKind : std::uint8_t
{
    String,
    Open,
    Close,
    End,
};

struct Token
{
    TokenKind kind{};
    std::string text{};
};

struct FlatValue
{
    std::vector<std::string> path{};
    std::string value{};
};

auto error(GameDiscoveryErrorCode code, std::string detail)
    -> std::unexpected<GameDiscoveryError>
{
    return std::unexpected(GameDiscoveryError{code, std::move(detail)});
}

auto ascii_lower(std::string_view value) -> std::string
{
    auto lowered = std::string{value};
    std::ranges::transform(
        lowered,
        lowered.begin(),
        [](unsigned char character)
        {
            return static_cast<char>(std::tolower(character));
        });
    return lowered;
}

auto ascii_equal(std::string_view left, std::string_view right) -> bool
{
    return ascii_lower(left) == ascii_lower(right);
}

auto is_decimal(std::string_view value) -> bool
{
    return !value.empty() &&
           std::ranges::all_of(
               value,
               [](unsigned char character)
               {
                   return std::isdigit(character) != 0;
               });
}

class VdfLexer
{
public:
    explicit VdfLexer(std::string_view input) : input_{input}
    {
    }

    auto next() -> std::expected<Token, GameDiscoveryError>
    {
        while (position_ < input_.size())
        {
            const auto current = input_[position_];
            if (std::isspace(static_cast<unsigned char>(current)) != 0)
            {
                ++position_;
                continue;
            }
            if (current == '/' && position_ + 1U < input_.size() &&
                input_[position_ + 1U] == '/')
            {
                position_ += 2U;
                while (position_ < input_.size() &&
                       input_[position_] != '\n')
                {
                    ++position_;
                }
                continue;
            }
            break;
        }

        if (position_ == input_.size())
        {
            return Token{TokenKind::End, {}};
        }

        const auto current = input_[position_++];
        if (current == '{')
        {
            return Token{TokenKind::Open, {}};
        }
        if (current == '}')
        {
            return Token{TokenKind::Close, {}};
        }
        if (current != '"')
        {
            return error(
                GameDiscoveryErrorCode::MalformedVdf,
                "VDF contains an unquoted token");
        }

        auto text = std::string{};
        text.reserve(32U);
        while (position_ < input_.size())
        {
            const auto character = input_[position_++];
            if (character == '"')
            {
                return Token{TokenKind::String, std::move(text)};
            }
            if (character == '\0' || character == '\r' ||
                character == '\n')
            {
                return error(
                    GameDiscoveryErrorCode::MalformedVdf,
                    "VDF string contains a forbidden control character");
            }
            if (character == '\\')
            {
                if (position_ == input_.size())
                {
                    return error(
                        GameDiscoveryErrorCode::MalformedVdf,
                        "VDF ends inside an escape sequence");
                }
                const auto escaped = input_[position_++];
                if (escaped == '\\' || escaped == '"')
                {
                    text.push_back(escaped);
                }
                else
                {
                    text.push_back('\\');
                    text.push_back(escaped);
                }
            }
            else
            {
                text.push_back(character);
            }
            if (text.size() > MaxVdfStringBytes)
            {
                return error(
                    GameDiscoveryErrorCode::MalformedVdf,
                    "VDF string exceeds the size limit");
            }
        }
        return error(
            GameDiscoveryErrorCode::MalformedVdf,
            "VDF contains an unterminated string");
    }

private:
    std::string_view input_{};
    std::size_t position_{};
};

class VdfParser
{
public:
    explicit VdfParser(std::string_view input) : lexer_{input}
    {
    }

    auto parse() -> std::expected<std::vector<FlatValue>, GameDiscoveryError>
    {
        auto path = std::vector<std::string>{};
        const auto result = parse_object(path, false, 0U);
        if (!result)
        {
            return std::unexpected(result.error());
        }
        return std::move(values_);
    }

private:
    auto parse_object(
        std::vector<std::string>& path,
        bool requires_close,
        std::size_t depth) -> std::expected<void, GameDiscoveryError>
    {
        if (depth > MaxVdfDepth)
        {
            return error(
                GameDiscoveryErrorCode::MalformedVdf,
                "VDF nesting exceeds the depth limit");
        }

        while (true)
        {
            const auto key = lexer_.next();
            if (!key)
            {
                return std::unexpected(key.error());
            }
            if (key->kind == TokenKind::End)
            {
                if (requires_close)
                {
                    return error(
                        GameDiscoveryErrorCode::MalformedVdf,
                        "VDF object is missing its closing brace");
                }
                return {};
            }
            if (key->kind == TokenKind::Close)
            {
                if (!requires_close)
                {
                    return error(
                        GameDiscoveryErrorCode::MalformedVdf,
                        "VDF contains an unexpected closing brace");
                }
                return {};
            }
            if (key->kind != TokenKind::String)
            {
                return error(
                    GameDiscoveryErrorCode::MalformedVdf,
                    "VDF key must be a quoted string");
            }

            const auto value = lexer_.next();
            if (!value)
            {
                return std::unexpected(value.error());
            }
            path.push_back(key->text);
            if (value->kind == TokenKind::String)
            {
                if (++entry_count_ > MaxVdfEntries)
                {
                    return error(
                        GameDiscoveryErrorCode::MalformedVdf,
                        "VDF contains too many values");
                }
                values_.push_back(FlatValue{path, value->text});
            }
            else if (value->kind == TokenKind::Open)
            {
                const auto nested =
                    parse_object(path, true, depth + 1U);
                if (!nested)
                {
                    return nested;
                }
            }
            else
            {
                return error(
                    GameDiscoveryErrorCode::MalformedVdf,
                    "VDF key has no string or object value");
            }
            path.pop_back();
        }
    }

    VdfLexer lexer_;
    std::vector<FlatValue> values_{};
    std::size_t entry_count_{};
};

auto parse_vdf(std::string_view input)
    -> std::expected<std::vector<FlatValue>, GameDiscoveryError>
{
    if (input.size() > MaxVdfBytes)
    {
        return error(
            GameDiscoveryErrorCode::MalformedVdf,
            "VDF exceeds the size limit");
    }
    return VdfParser{input}.parse();
}

auto utf8_path(std::string_view value) -> fs::path
{
    auto encoded = std::u8string{};
    encoded.reserve(value.size());
    for (const auto character : value)
    {
        encoded.push_back(static_cast<char8_t>(
            static_cast<unsigned char>(character)));
    }
    return fs::path{encoded};
}

auto read_bounded_file(const fs::path& path)
    -> std::expected<std::string, GameDiscoveryError>
{
    std::error_code size_error{};
    const auto size = fs::file_size(path, size_error);
    if (size_error)
    {
        return error(
            GameDiscoveryErrorCode::Io,
            "could not inspect VDF file size");
    }
    if (size > MaxVdfBytes ||
        size > static_cast<std::uintmax_t>(
                   std::numeric_limits<std::size_t>::max()))
    {
        return error(
            GameDiscoveryErrorCode::MalformedVdf,
            "VDF exceeds the size limit");
    }

    std::ifstream input{path, std::ios::binary};
    if (!input)
    {
        return error(
            GameDiscoveryErrorCode::Io,
            "could not open VDF file");
    }
    auto contents = std::string(static_cast<std::size_t>(size), '\0');
    input.read(contents.data(), static_cast<std::streamsize>(contents.size()));
    if (!input || input.peek() != std::char_traits<char>::eof())
    {
        return error(
            GameDiscoveryErrorCode::Io,
            "could not read the complete VDF file");
    }
    return contents;
}

auto canonical_directory(const fs::path& path)
    -> std::expected<fs::path, GameDiscoveryError>
{
    std::error_code status_error{};
    if (!fs::is_directory(path, status_error) || status_error)
    {
        return error(
            GameDiscoveryErrorCode::MissingGame,
            "the selected game directory does not exist");
    }
    std::error_code canonical_error{};
    auto canonical = fs::canonical(path, canonical_error);
    if (canonical_error)
    {
        return error(
            GameDiscoveryErrorCode::Io,
            "the selected game directory could not be canonicalized");
    }
    return canonical;
}

auto infer_library(const fs::path& install_directory) -> fs::path
{
    const auto common = install_directory.parent_path();
    const auto steamapps = common.parent_path();
    if (ascii_equal(common.filename().string(), "common") &&
        ascii_equal(steamapps.filename().string(), "steamapps"))
    {
        return steamapps.parent_path();
    }
    return {};
}

auto installation_from_root(const fs::path& install_directory)
    -> std::expected<GameInstallation, GameDiscoveryError>
{
    const auto canonical_install =
        canonical_directory(install_directory);
    if (!canonical_install)
    {
        return std::unexpected(canonical_install.error());
    }

    const auto binaries =
        *canonical_install / "Chameleon" / "Binaries" / "Win64";
    const auto executable = binaries / TargetGameExecutable;
    std::error_code file_error{};
    if (!fs::is_regular_file(executable, file_error) || file_error)
    {
        return error(
            GameDiscoveryErrorCode::MissingGame,
            "the expected MECCHA CHAMELEON executable is missing");
    }

    std::error_code canonical_error{};
    const auto canonical_executable =
        fs::canonical(executable, canonical_error);
    if (canonical_error)
    {
        return error(
            GameDiscoveryErrorCode::Io,
            "the game executable could not be canonicalized");
    }
    return GameInstallation{
        infer_library(*canonical_install),
        *canonical_install,
        canonical_executable.parent_path(),
        canonical_executable,
    };
}

auto normalized_key(const fs::path& path) -> std::string
{
    auto key = path.generic_string();
#ifdef _WIN32
    key = ascii_lower(key);
#endif
    return key;
}

auto append_unique_path(
    std::vector<fs::path>& paths,
    const fs::path& candidate) -> void
{
    std::error_code canonical_error{};
    const auto canonical = fs::canonical(candidate, canonical_error);
    if (canonical_error)
    {
        return;
    }
    const auto key = normalized_key(canonical);
    const auto exists = std::ranges::any_of(
        paths,
        [&key](const auto& value)
        {
            return normalized_key(value) == key;
        });
    if (!exists)
    {
        paths.push_back(canonical);
    }
}
} // namespace

auto parse_steam_library_folders(std::string_view vdf)
    -> std::expected<std::vector<std::string>, GameDiscoveryError>
{
    const auto flat = parse_vdf(vdf);
    if (!flat)
    {
        return std::unexpected(flat.error());
    }

    auto paths = std::vector<std::string>{};
    for (const auto& item : *flat)
    {
        const auto current =
            item.path.size() == 3U &&
            ascii_equal(item.path[0], "libraryfolders") &&
            is_decimal(item.path[1]) &&
            ascii_equal(item.path[2], "path");
        const auto legacy =
            item.path.size() == 2U &&
            ascii_equal(item.path[0], "libraryfolders") &&
            is_decimal(item.path[1]);
        if (!current && !legacy)
        {
            continue;
        }
        if (item.value.empty())
        {
            return error(
                GameDiscoveryErrorCode::MalformedVdf,
                "Steam library path is empty");
        }
        const auto lowered = ascii_lower(item.value);
        if (std::ranges::none_of(
                paths,
                [&lowered](const auto& path)
                {
                    return ascii_lower(path) == lowered;
                }))
        {
            paths.push_back(item.value);
        }
    }
    return paths;
}

auto parse_app_install_directory(std::string_view vdf)
    -> std::expected<std::string, GameDiscoveryError>
{
    const auto flat = parse_vdf(vdf);
    if (!flat)
    {
        return error(
            GameDiscoveryErrorCode::InvalidAppManifest,
            flat.error().detail);
    }

    auto app_id = std::optional<std::string>{};
    auto install_directory = std::optional<std::string>{};
    for (const auto& item : *flat)
    {
        if (item.path.size() != 2U ||
            !ascii_equal(item.path[0], "appstate"))
        {
            continue;
        }
        if (ascii_equal(item.path[1], "appid"))
        {
            if (app_id)
            {
                return error(
                    GameDiscoveryErrorCode::InvalidAppManifest,
                    "Steam app manifest contains duplicate appid fields");
            }
            app_id = item.value;
        }
        else if (ascii_equal(item.path[1], "installdir"))
        {
            if (install_directory)
            {
                return error(
                    GameDiscoveryErrorCode::InvalidAppManifest,
                    "Steam app manifest contains duplicate installdir fields");
            }
            install_directory = item.value;
        }
    }

    if (!app_id || *app_id != TargetSteamAppId)
    {
        return error(
            GameDiscoveryErrorCode::InvalidAppManifest,
            "Steam app manifest has the wrong or missing appid");
    }
    if (!install_directory || install_directory->empty() ||
        *install_directory == "." || *install_directory == ".." ||
        install_directory->find('/') != std::string::npos ||
        install_directory->find('\\') != std::string::npos ||
        install_directory->find(':') != std::string::npos ||
        std::ranges::any_of(
            *install_directory,
            [](unsigned char character)
            {
                return character < 0x20U;
            }))
    {
        return error(
            GameDiscoveryErrorCode::InvalidInstallDirectory,
            "Steam app manifest contains an unsafe installdir");
    }
    return *install_directory;
}

auto discover_game_installation(std::span<const fs::path> steam_roots)
    -> std::expected<GameInstallation, GameDiscoveryError>
{
    auto libraries = std::vector<fs::path>{};
    for (const auto& root : steam_roots)
    {
        append_unique_path(libraries, root);
        const auto library_file =
            root / "steamapps" / "libraryfolders.vdf";
        std::error_code exists_error{};
        const auto exists = fs::exists(library_file, exists_error);
        if (exists_error)
        {
            return error(
                GameDiscoveryErrorCode::Io,
                "Steam libraryfolders.vdf could not be inspected");
        }
        if (!exists)
        {
            continue;
        }
        const auto contents = read_bounded_file(library_file);
        if (!contents)
        {
            return std::unexpected(contents.error());
        }
        const auto parsed = parse_steam_library_folders(*contents);
        if (!parsed)
        {
            return std::unexpected(parsed.error());
        }
        for (const auto& path : *parsed)
        {
            append_unique_path(libraries, utf8_path(path));
        }
    }

    auto installations = std::vector<GameInstallation>{};
    for (const auto& library : libraries)
    {
        const auto manifest =
            library / "steamapps" /
            ("appmanifest_" + std::string{TargetSteamAppId} + ".acf");
        std::error_code exists_error{};
        const auto exists = fs::exists(manifest, exists_error);
        if (exists_error)
        {
            return error(
                GameDiscoveryErrorCode::Io,
                "Steam app manifest could not be inspected");
        }
        if (!exists)
        {
            continue;
        }
        const auto contents = read_bounded_file(manifest);
        if (!contents)
        {
            return std::unexpected(contents.error());
        }
        const auto install_name = parse_app_install_directory(*contents);
        if (!install_name)
        {
            return std::unexpected(install_name.error());
        }
        auto installation = installation_from_root(
            library / "steamapps" / "common" / utf8_path(*install_name));
        if (!installation)
        {
            return std::unexpected(installation.error());
        }
        installation->steam_library = library;

        const auto key = normalized_key(installation->executable);
        const auto duplicate = std::ranges::any_of(
            installations,
            [&key](const auto& existing)
            {
                return normalized_key(existing.executable) == key;
            });
        if (!duplicate)
        {
            installations.push_back(std::move(*installation));
        }
    }

    if (installations.empty())
    {
        return error(
            GameDiscoveryErrorCode::MissingGame,
            "Steam App ID 4704690 was not found in the discovered libraries");
    }
    if (installations.size() != 1U)
    {
        return error(
            GameDiscoveryErrorCode::AmbiguousGame,
            "multiple Steam installations of App ID 4704690 were found");
    }
    return std::move(installations.front());
}

auto validate_game_directory(const fs::path& selected_directory)
    -> std::expected<GameInstallation, GameDiscoveryError>
{
    const auto canonical = canonical_directory(selected_directory);
    if (!canonical)
    {
        return std::unexpected(canonical.error());
    }

    if (ascii_equal(canonical->filename().string(), "Win64") &&
        ascii_equal(
            canonical->parent_path().filename().string(),
            "Binaries") &&
        ascii_equal(
            canonical->parent_path().parent_path().filename().string(),
            "Chameleon"))
    {
        return installation_from_root(
            canonical->parent_path().parent_path().parent_path());
    }
    return installation_from_root(*canonical);
}
} // namespace meccha::launcher
