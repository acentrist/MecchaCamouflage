#include <meccha/application/config_store.hpp>
#include <meccha/application/image_editor_services_win32.hpp>
#include <meccha/application/image_project_codec.hpp>
#include <meccha/common/hash.hpp>
#include <meccha/core/image_project.hpp>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace
{
using namespace meccha;
using namespace meccha::application;
using namespace std::chrono_literals;

constexpr auto ProjectId =
    std::string_view{"0123456789abcdef0123456789abcdef"};
constexpr auto RedWebP = std::array<std::uint8_t, 36U>{
    0x52, 0x49, 0x46, 0x46, 0x1c, 0x00, 0x00, 0x00, 0x57,
    0x45, 0x42, 0x50, 0x56, 0x50, 0x38, 0x4c, 0x0f, 0x00,
    0x00, 0x00, 0x2f, 0x01, 0x00, 0x00, 0x00, 0x07, 0x10,
    0xfd, 0x8f, 0xfe, 0x07, 0x22, 0xa2, 0xff, 0x01, 0x00,
};

auto expect(bool condition, std::string_view message) -> bool
{
    if (!condition)
    {
        std::cerr << "FAIL image_editor_services_win32: "
                  << message << '\n';
    }
    return condition;
}

class TemporaryDirectory
{
public:
    TemporaryDirectory()
    {
        auto root = std::array<wchar_t, MAX_PATH + 1U>{};
        const auto length = GetTempPathW(
            static_cast<DWORD>(root.size()),
            root.data());
        if (length == 0U || length >= root.size())
        {
            return;
        }
        path_ = std::filesystem::path{root.data()} /
                (L"MecchaImageEditorServices-" +
                 std::to_wstring(GetCurrentProcessId()) + L"-" +
                 std::to_wstring(GetTickCount64()));
        std::error_code error{};
        std::filesystem::create_directories(path_, error);
        if (error)
        {
            path_.clear();
        }
    }

    TemporaryDirectory(const TemporaryDirectory&) = delete;
    auto operator=(const TemporaryDirectory&)
        -> TemporaryDirectory& = delete;

    ~TemporaryDirectory()
    {
        if (!path_.empty())
        {
            auto error = std::error_code{};
            std::filesystem::remove_all(path_, error);
        }
    }

    [[nodiscard]] auto path() const
        -> const std::filesystem::path&
    {
        return path_;
    }

private:
    std::filesystem::path path_{};
};

auto project() -> core::ImageProject
{
    const auto encoded =
        std::as_bytes(std::span{RedWebP});
    const auto digest = common::sha256_bytes(encoded).value();
    const auto asset_id = common::sha256_hex(digest);
    auto bytes = std::make_shared<const std::vector<std::byte>>(
        encoded.begin(),
        encoded.end());
    return core::ImageProject{
        core::ImageProjectSchemaVersion,
        std::string{ProjectId},
        "Native project",
        1U,
        {},
        {core::ImageLayer{
            asset_id,
            "red.webp",
            core::ImageMime::WebP,
            bytes->size(),
        }},
        {core::ImageSourceAsset{
            asset_id,
            core::ImageMime::WebP,
            std::move(bytes),
        }},
        std::make_shared<const std::vector<std::byte>>(
            core::CanonicalAtlasByteLength,
            std::byte{}),
    };
}

auto wait_for_ready(
    ImageEditorSessionPort& session,
    std::string_view project_id,
    std::uint64_t revision)
    -> std::shared_ptr<const ImageEditorReadyContent>
{
    const auto deadline =
        std::chrono::steady_clock::now() + 5s;
    while (std::chrono::steady_clock::now() < deadline)
    {
        session.update();
        if (const auto ready =
                session.ready_content(project_id, revision))
        {
            return ready;
        }
        std::this_thread::sleep_for(1ms);
    }
    return {};
}
} // namespace

auto main() -> int
{
    auto passed = true;
    auto temporary = TemporaryDirectory{};
    passed &= expect(
        !temporary.path().empty(),
        "temporary LocalAppData root could not be created");
    if (temporary.path().empty())
    {
        return 1;
    }

    passed &= expect(
        Win32ImageEditorServices::create(
            std::filesystem::path{"relative"},
            1ms) ==
            std::unexpected(
                Win32ImageEditorServicesError{
                    Win32ImageEditorServicesErrorCode::
                        InvalidLocalAppData,
                }),
        "a relative LocalAppData root reached service construction");

    const auto source = project();
    auto hasher = NativePresetHasher{};
    const auto encoded = encode_image_project(source, hasher);
    passed &= expect(
        encoded.has_value(),
        "native preset hashing could not encode the import fixture");
    if (!encoded)
    {
        return 1;
    }

    auto loaded_config = core::ApplicationConfig{};
    {
        auto services =
            Win32ImageEditorServices::create(
                temporary.path(),
                30s);
        passed &= expect(
            services.has_value() &&
                (*services)->data_root() ==
                    temporary.path() /
                        "MecchaCamouflage" / "v2" &&
                (*services)->image_projects_root() ==
                    temporary.path() /
                        "MecchaCamouflage" / "v2" /
                        "image-projects",
            "service roots do not match the isolated v2 contract");
        if (!services)
        {
            return 1;
        }

        auto& session = (*services)->image_editor();
        const auto startup =
            session.recover_startup(loaded_config);
        const auto import = session.import_project(
            1U,
            std::make_shared<const std::vector<std::byte>>(
                *encoded),
            loaded_config);
        auto completion =
            std::optional<ImageEditorSessionCompletion>{};
        const auto deadline =
            std::chrono::steady_clock::now() + 5s;
        while (!completion &&
               std::chrono::steady_clock::now() < deadline)
        {
            session.update();
            completion = session.poll_completion();
            if (!completion)
            {
                std::this_thread::sleep_for(1ms);
            }
        }
        const auto ready =
            wait_for_ready(session, ProjectId, 1U);
        auto config_store =
            ConfigStore{(*services)->config_storage()};
        const auto configured = config_store.load();
        if (configured)
        {
            loaded_config = configured->config;
        }
        passed &= expect(
            startup &&
                startup->source ==
                    RecoveredImageProjectSource::Blank &&
                import && completion && completion->result &&
                ready && ready->project &&
                ready->project->project_id == ProjectId &&
                ready->decoded_sources &&
                ready->decoded_sources->size() == 1U &&
                ready->decoded_sources->front().width == 2U &&
                ready->decoded_sources->front().height == 1U &&
                configured &&
                configured->config.active_image_project &&
                configured->config.active_image_project->kind ==
                    core::ImageProjectReferenceKind::NamedProject &&
                configured->config.active_image_project->project_id ==
                    ProjectId &&
                std::filesystem::is_regular_file(
                    (*services)->image_projects_root() /
                    (std::string{ProjectId} + ".mcpreset")),
            "the native service graph did not import, compose, and "
            "publish one project");
        session.shutdown(false);
    }

    {
        auto services =
            Win32ImageEditorServices::create(
                temporary.path(),
                1ms);
        if (!services)
        {
            return 1;
        }
        auto& session = (*services)->image_editor();
        const auto recovered =
            session.recover_startup(loaded_config);
        const auto ready =
            wait_for_ready(session, ProjectId, 1U);
        passed &= expect(
            recovered &&
                recovered->source ==
                    RecoveredImageProjectSource::NamedProject &&
                ready && ready->project &&
                ready->project->display_name == "Native project",
            "a reconstructed native service graph did not recover "
            "the named project");
        session.shutdown(false);
    }

    if (passed)
    {
        std::cout << "PASS image_editor_services_win32\n";
        return 0;
    }
    return 1;
}
