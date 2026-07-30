#include <meccha/application/product_ui_effect_executor.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <expected>
#include <iostream>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace
{
using namespace meccha;
using namespace meccha::application;

constexpr auto ProjectId =
    std::string_view{"0123456789abcdef0123456789abcdef"};

auto expect(bool condition, std::string_view message) -> bool
{
    if (!condition)
    {
        std::cerr << "FAIL product_ui_effect_executor: "
                  << message << '\n';
    }
    return condition;
}

auto immutable_bytes(std::byte value)
    -> std::shared_ptr<const std::vector<std::byte>>
{
    return std::make_shared<const std::vector<std::byte>>(
        std::initializer_list<std::byte>{value});
}

class TestHasher final : public PresetHasher
{
public:
    auto hash(std::span<const std::byte> bytes)
        -> std::expected<
            common::Sha256Digest,
            PresetHashError> override
    {
        auto digest = common::Sha256Digest{};
        digest.bytes.front() = bytes.front();
        return digest;
    }
};

class SnapshotSequence final : public ApplicationSnapshotPort
{
public:
    explicit SnapshotSequence(
        std::vector<ApplicationSnapshot> values)
    {
        for (auto& value : values)
        {
            values_.push_back(
                std::make_shared<const ApplicationSnapshot>(
                    std::move(value)));
        }
    }

    auto snapshot() const
        -> std::shared_ptr<const ApplicationSnapshot> override
    {
        if (values_.empty())
        {
            return nullptr;
        }
        const auto index = std::min(
            reads_,
            values_.size() - 1U);
        ++reads_;
        return values_[index];
    }

    [[nodiscard]] auto reads() const -> std::size_t
    {
        return reads_;
    }

private:
    std::vector<
        std::shared_ptr<const ApplicationSnapshot>> values_{};
    mutable std::size_t reads_{};
};

class FakePicker final : public ImageFilePickerPort
{
public:
    auto pick_images(std::uintptr_t owner_window)
        -> ImageFilePickerResult<
            std::vector<PickedImageFile>> override
    {
        ++image_calls;
        last_owner = owner_window;
        return images;
    }

    auto pick_image_project(std::uintptr_t owner_window)
        -> ImageFilePickerResult<
            PickedImageProjectFile> override
    {
        ++project_calls;
        last_owner = owner_window;
        return project;
    }

    ImageFilePickerResult<std::vector<PickedImageFile>>
        images{std::nullopt};
    ImageFilePickerResult<PickedImageProjectFile>
        project{std::nullopt};
    std::size_t image_calls{};
    std::size_t project_calls{};
    std::uintptr_t last_owner{};
};

auto snapshot(
    std::uint64_t revision,
    std::uint64_t project_revision = 9U)
    -> ApplicationSnapshot
{
    auto value = ApplicationSnapshot{};
    value.revision = revision;
    value.runtime_phase = ApplicationRuntimePhase::Compatible;
    value.compatibility.status =
        CompatibilityStatus::Compatible;
    value.command_queue = {0U, 8U, true};
    value.runtime_queue = {0U, 8U, true};
    value.image_editor.document =
        ImageEditorDocumentSnapshot{
            std::string{ProjectId},
            "Project",
            project_revision,
            {},
            {core::ImageLayer{
                std::string(64U, '1'),
                "existing.png",
                core::ImageMime::Png,
                1U,
            }},
        };
    value.image_editor.pipeline =
        ImageEditorPipelineSnapshot{
            ImageEditorPipelinePhase::Ready,
            4U,
            std::string{ProjectId},
            project_revision,
        };
    return value;
}
} // namespace

auto main() -> int
{
    using namespace meccha;
    using namespace meccha::application;

    auto passed = true;
    auto hasher = TestHasher{};
    const auto picked_bytes =
        immutable_bytes(std::byte{0x21});

    auto add_snapshots = SnapshotSequence{
        {snapshot(17U), snapshot(18U)}};
    auto add_picker = FakePicker{};
    add_picker.images =
        std::optional<std::vector<PickedImageFile>>{
            {PickedImageFile{
                "picked.png",
                core::ImageMime::Png,
                picked_bytes,
            }},
        };
    auto add_executor = ProductUiEffectExecutor{
        add_snapshots,
        add_picker,
        hasher,
    };
    const auto added = add_executor.execute(
        ProductUiEffectEnvelope{
            17U,
            UiPickImageFiles{
                std::string{ProjectId},
                9U,
            },
        },
        44U);
    const auto* added_action =
        added && added->action
            ? std::get_if<UiMutateCurrentImageProject>(
                  &added->action->action)
            : nullptr;
    const auto* added_mutation =
        added_action
            ? std::get_if<AddImageLayersMutation>(
                  &added_action->mutation)
            : nullptr;
    passed &= expect(
        added && !added->cancelled && added_mutation &&
            added->action->expected_snapshot_revision == 18U &&
            added_mutation->layers.size() == 1U &&
            added_mutation->sources.size() == 1U &&
            added_mutation->sources.front().bytes ==
                picked_bytes &&
            add_picker.image_calls == 1U &&
            add_picker.last_owner == 44U &&
            add_snapshots.reads() == 2U,
        "Add-images did not rebind its action to the latest matching snapshot");

    auto changed_snapshots = SnapshotSequence{
        {snapshot(20U), snapshot(21U, 10U)}};
    auto changed_picker = FakePicker{};
    changed_picker.images = add_picker.images;
    auto changed_executor = ProductUiEffectExecutor{
        changed_snapshots,
        changed_picker,
        hasher,
    };
    const auto changed = changed_executor.execute(
        ProductUiEffectEnvelope{
            20U,
            UiPickImageFiles{
                std::string{ProjectId},
                9U,
            },
        },
        0U);
    passed &= expect(
        !changed &&
            changed.error().code ==
                ProductUiEffectErrorCode::StaleEffect &&
            changed_picker.image_calls == 1U &&
            changed_snapshots.reads() == 2U,
        "Add-images admitted a project revision changed by the modal dialog");

    auto stale_snapshots = SnapshotSequence{{snapshot(30U)}};
    auto stale_picker = FakePicker{};
    auto stale_executor = ProductUiEffectExecutor{
        stale_snapshots,
        stale_picker,
        hasher,
    };
    const auto stale = stale_executor.execute(
        ProductUiEffectEnvelope{
            29U,
            UiPickImageProject{},
        },
        0U);
    passed &= expect(
        !stale &&
            stale.error().code ==
                ProductUiEffectErrorCode::StaleEffect &&
            stale_picker.project_calls == 0U,
        "a stale picker effect opened a native dialog");

    auto cancel_snapshots = SnapshotSequence{{snapshot(40U)}};
    auto cancel_picker = FakePicker{};
    auto cancel_executor = ProductUiEffectExecutor{
        cancel_snapshots,
        cancel_picker,
        hasher,
    };
    const auto cancelled = cancel_executor.execute(
        ProductUiEffectEnvelope{
            40U,
            UiPickImageFiles{
                std::string{ProjectId},
                9U,
            },
        },
        0U);
    passed &= expect(
        cancelled && cancelled->cancelled &&
            !cancelled->action &&
            cancel_snapshots.reads() == 1U,
        "picker cancellation was not a non-mutating terminal result");

    auto preset_snapshots = SnapshotSequence{
        {snapshot(50U), snapshot(51U)}};
    auto preset_picker = FakePicker{};
    const auto preset_bytes =
        immutable_bytes(std::byte{0x51});
    preset_picker.project =
        std::optional<PickedImageProjectFile>{
            PickedImageProjectFile{
                "project.mcpreset",
                preset_bytes,
            },
        };
    auto preset_executor = ProductUiEffectExecutor{
        preset_snapshots,
        preset_picker,
        hasher,
    };
    const auto preset = preset_executor.execute(
        ProductUiEffectEnvelope{
            50U,
            UiPickImageProject{},
        },
        88U);
    const auto* preset_action =
        preset && preset->action
            ? std::get_if<UiImportImageProject>(
                  &preset->action->action)
            : nullptr;
    passed &= expect(
        preset && !preset->cancelled &&
            preset_action &&
            preset_action->bytes == preset_bytes &&
            preset->action->expected_snapshot_revision == 51U &&
            preset_picker.project_calls == 1U &&
            preset_picker.last_owner == 88U,
        "project picker did not retain immutable bytes on the latest snapshot");

    auto failed_snapshots = SnapshotSequence{{snapshot(60U)}};
    auto failed_picker = FakePicker{};
    failed_picker.project =
        std::unexpected(ImageFilePickerError{
            ImageFilePickerErrorCode::DialogFailure,
            -1,
            "fake dialog failure",
        });
    auto failed_executor = ProductUiEffectExecutor{
        failed_snapshots,
        failed_picker,
        hasher,
    };
    const auto failed = failed_executor.execute(
        ProductUiEffectEnvelope{
            60U,
            UiPickImageProject{},
        },
        0U);
    passed &= expect(
        !failed &&
            failed.error().code ==
                ProductUiEffectErrorCode::Picker &&
            failed.error().picker ==
                std::optional{ImageFilePickerError{
                    ImageFilePickerErrorCode::DialogFailure,
                    -1,
                    "fake dialog failure",
                }},
        "native picker failure detail was not retained");

    return passed ? 0 : 1;
}
