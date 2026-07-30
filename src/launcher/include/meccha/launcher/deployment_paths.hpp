#pragma once

#include <meccha/launcher/execution.hpp>

#include <expected>
#include <filesystem>

namespace meccha::launcher
{
class SharedRuntimeDirectorySource
{
public:
    SharedRuntimeDirectorySource() = default;
    SharedRuntimeDirectorySource(
        const SharedRuntimeDirectorySource&) = delete;
    auto operator=(const SharedRuntimeDirectorySource&)
        -> SharedRuntimeDirectorySource& = delete;
    SharedRuntimeDirectorySource(
        SharedRuntimeDirectorySource&&) = delete;
    auto operator=(SharedRuntimeDirectorySource&&)
        -> SharedRuntimeDirectorySource& = delete;
    virtual ~SharedRuntimeDirectorySource() = default;

    virtual auto selected_shared_runtime_directory() const
        -> std::expected<
            std::filesystem::path,
            LauncherEffectError> = 0;
};
} // namespace meccha::launcher
