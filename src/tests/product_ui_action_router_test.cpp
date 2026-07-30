#include <meccha/application/input_command_router.hpp>

#include <array>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

namespace
{
auto expect(bool condition, std::string_view message) -> bool
{
    if (!condition)
    {
        std::cerr << "FAIL product_ui_action_router: "
                  << message << '\n';
    }
    return condition;
}

constexpr auto ProjectId =
    std::string_view{"0123456789abcdef0123456789abcdef"};

auto ready_snapshot()
    -> meccha::application::ApplicationSnapshot
{
    using namespace meccha::application;

    auto snapshot = ApplicationSnapshot{};
    snapshot.revision = 17U;
    snapshot.runtime_phase =
        ApplicationRuntimePhase::Compatible;
    snapshot.compatibility.status =
        CompatibilityStatus::Compatible;
    snapshot.command_queue = CommandQueueSnapshot{
        0U,
        8U,
        true,
    };
    snapshot.runtime_queue = QueueSnapshot{
        0U,
        16U,
        true,
    };
    snapshot.image_editor.document =
        ImageEditorDocumentSnapshot{
            std::string{ProjectId},
            "Project",
            9U,
        };
    snapshot.image_editor.pipeline =
        ImageEditorPipelineSnapshot{
            ImageEditorPipelinePhase::Ready,
            4U,
            std::string{ProjectId},
            9U,
        };
    return snapshot;
}

template <typename Action>
auto envelope(Action action)
    -> meccha::application::ProductUiActionEnvelope
{
    return {
        17U,
        meccha::application::ProductUiAction{
            std::move(action)},
    };
}
} // namespace

auto main() -> int
{
    using namespace meccha;
    using namespace meccha::application;

    auto passed = true;
    auto snapshot = ready_snapshot();
    auto router = InputCommandRouter{100U};

    const auto hotkey = router.route(
        snapshot,
        std::array{
            FunctionKeyEvent{
                core::FunctionKey::F9,
                FunctionKeyEventKind::Pressed,
            },
        });
    passed &= expect(
        hotkey && hotkey->commands.size() == 1U &&
            std::get<ToggleUi>(hotkey->commands.front()).id ==
                100U,
        "hotkey did not establish the shared command sequence");

    snapshot.settings.paint.brush_size_texels = 8.0;
    const auto paint = router.route_ui_actions(
        snapshot,
        std::array{
            envelope(UiPaintAction{
                FeatureUiAction::Start,
            }),
        });
    passed &= expect(
        paint && paint->commands.size() == 1U &&
            paint->rejections.empty() &&
            std::get<StartPaint>(paint->commands.front()).id ==
                101U &&
            std::get<StartPaint>(paint->commands.front())
                    .settings.brush_size_texels == 8.0,
        "Paint action did not bind the immutable snapshot");

    const auto image = router.route_ui_actions(
        snapshot,
        std::array{
            envelope(UiImagePaintAction{
                FeatureUiAction::Preview,
            }),
        });
    passed &= expect(
        image && image->commands.size() == 1U &&
            std::get<PreviewImagePaint>(
                image->commands.front()).id == 102U &&
            std::get<PreviewImagePaint>(
                image->commands.front()).project_id ==
                ProjectId &&
            std::get<PreviewImagePaint>(
                image->commands.front()).project_revision ==
                9U,
        "Image Paint action did not bind project identity");

    auto changed = snapshot.settings;
    changed.ui.scale = 1.25;
    const auto settings = router.route_ui_actions(
        snapshot,
        std::array{
            envelope(UiApplySettings{
                changed,
            }),
        });
    passed &= expect(
        settings && settings->commands.size() == 1U &&
            std::get<ApplyValidatedSettings>(
                settings->commands.front()).id == 103U &&
            std::get<ApplyValidatedSettings>(
                settings->commands.front()).settings ==
                changed,
        "settings action did not retain validated values");

    const auto save = router.route_ui_actions(
        snapshot,
        std::array{
            envelope(UiSaveCurrentImageProject{}),
        });
    passed &= expect(
        save && save->commands.size() == 1U &&
            std::get<SaveImageProject>(
                save->commands.front()).id == 104U &&
            std::get<SaveImageProject>(
                save->commands.front()).project_id ==
                ProjectId &&
            std::get<SaveImageProject>(
                save->commands.front()).expected_revision ==
                9U,
        "save action trusted caller-owned project identity");

    const auto rename = router.route_ui_actions(
        snapshot,
        std::array{
            envelope(UiRenameCurrentImageProject{
                "Renamed",
            }),
        });
    passed &= expect(
        rename && rename->commands.size() == 1U &&
            std::get<RenameImageProject>(
                rename->commands.front()).id == 105U &&
            std::get<RenameImageProject>(
                rename->commands.front()).new_name ==
                "Renamed",
        "rename action did not bind current project");

    auto source_layer = core::ImageLayer{
        "asset",
        "image.png",
        core::ImageMime::Png,
        128U,
    };
    snapshot.image_editor.document->layers = {source_layer};
    auto replacement = source_layer;
    replacement.center_x = 0.6;
    const auto mutate = router.route_ui_actions(
        snapshot,
        std::array{
            envelope(UiMutateCurrentImageProject{
                ReplaceImageLayerMutation{
                    0U,
                    "asset",
                    replacement,
                },
            }),
        });
    passed &= expect(
        mutate && mutate->commands.size() == 1U &&
            std::get<MutateImageProject>(
                mutate->commands.front()).id == 106U &&
            std::get<MutateImageProject>(
                mutate->commands.front()).project_id ==
                ProjectId &&
            std::get<MutateImageProject>(
                mutate->commands.front()).expected_revision ==
                9U,
        "editor mutation did not bind current revision");

    auto removal_snapshot = snapshot;
    removal_snapshot.image_editor.document->layers.push_back(
        core::ImageLayer{
            "asset-2",
            "overlay.png",
            core::ImageMime::Png,
            64U,
        });
    auto removal_router = InputCommandRouter{500U};
    const auto layer_remove = removal_router.route_ui_actions(
        removal_snapshot,
        std::array{
            envelope(UiMutateCurrentImageProject{
                RemoveImageLayerMutation{
                    1U,
                    "asset-2",
                },
            }),
        });
    const auto* remove_command =
        layer_remove && layer_remove->commands.size() == 1U
            ? std::get_if<MutateImageProject>(
                  &layer_remove->commands.front())
            : nullptr;
    const auto* remove_mutation =
        remove_command
            ? std::get_if<RemoveImageLayerMutation>(
                  &remove_command->mutation)
            : nullptr;
    passed &= expect(
        remove_mutation &&
            remove_command->id == 500U &&
            remove_command->project_id == ProjectId &&
            remove_command->expected_revision == 9U &&
            remove_mutation->layer_index == 1U &&
            remove_mutation->expected_asset_id == "asset-2",
        "layer removal did not bind current project identity");

    auto import_bytes =
        std::make_shared<const std::vector<std::byte>>(
            std::initializer_list<std::byte>{
                std::byte{0x44},
            });
    const auto import_asset_id = std::string(64U, 'a');
    auto import_router = InputCommandRouter{600U};
    const auto layer_import = import_router.route_ui_actions(
        removal_snapshot,
        std::array{
            envelope(UiMutateCurrentImageProject{
                AddImageLayersMutation{
                    {core::ImageLayer{
                        import_asset_id,
                        "picked.png",
                        core::ImageMime::Png,
                        import_bytes->size(),
                    }},
                    {core::ImageSourceAsset{
                        import_asset_id,
                        core::ImageMime::Png,
                        import_bytes,
                    }},
                },
            }),
        });
    const auto* import_command =
        layer_import && layer_import->commands.size() == 1U
            ? std::get_if<MutateImageProject>(
                  &layer_import->commands.front())
            : nullptr;
    const auto* import_mutation =
        import_command
            ? std::get_if<AddImageLayersMutation>(
                  &import_command->mutation)
            : nullptr;
    passed &= expect(
        import_mutation &&
            import_command->id == 600U &&
            import_command->project_id == ProjectId &&
            import_command->expected_revision == 9U &&
            import_mutation->layers.size() == 1U &&
            import_mutation->sources.size() == 1U &&
            import_mutation->sources.front().bytes ==
                import_bytes,
        "image import did not retain immutable bytes and project guards");

    const auto preset_bytes =
        std::make_shared<const std::vector<std::byte>>(
            std::initializer_list<std::byte>{
                std::byte{0x55},
            });
    auto preset_router = InputCommandRouter{650U};
    const auto preset_import =
        preset_router.route_ui_actions(
            snapshot,
            std::array{
                envelope(UiImportImageProject{
                    preset_bytes,
                }),
            });
    const auto* preset_command =
        preset_import &&
                preset_import->commands.size() == 1U
            ? std::get_if<ImportImageProject>(
                  &preset_import->commands.front())
            : nullptr;
    passed &= expect(
        preset_command &&
            preset_command->id == 650U &&
            preset_command->bytes == preset_bytes,
        "project import did not retain immutable preset bytes");
    passed &= expect(
        preset_router.route_ui_actions(
            snapshot,
            std::array{
                envelope(UiImportImageProject{}),
            }) ==
            std::unexpected(
                InputCommandRouterError::InvalidUiAction),
        "project import accepted missing preset bytes");

    const auto toggle = router.route_ui_actions(
        snapshot,
        std::array{
            envelope(UiToggleEsp{}),
        });
    passed &= expect(
        toggle && toggle->commands.size() == 1U &&
            std::get<ToggleEsp>(
                toggle->commands.front()).id == 107U,
        "ESP action did not use the shared sequence");

    constexpr auto OtherProjectId =
        std::string_view{"fedcba9876543210fedcba9876543210"};
    const auto load = router.route_ui_actions(
        snapshot,
        std::array{
            envelope(UiLoadImageProject{
                std::string{OtherProjectId},
            }),
        });
    passed &= expect(
        load && load->commands.size() == 1U &&
            std::get<LoadImageProject>(
                load->commands.front()).id == 108U &&
            std::get<LoadImageProject>(
                load->commands.front()).project_id ==
                OtherProjectId,
        "load action did not retain a validated project ID");

    const auto remove = router.route_ui_actions(
        snapshot,
        std::array{
            envelope(UiDeleteCurrentImageProject{}),
        });
    passed &= expect(
        remove && remove->commands.size() == 1U &&
            std::get<DeleteImageProject>(
                remove->commands.front()).id == 109U &&
            std::get<DeleteImageProject>(
                remove->commands.front()).project_id ==
                ProjectId,
        "delete action trusted caller-owned project identity");

    const auto panel = router.route_ui_actions(
        snapshot,
        std::array{
            envelope(UiToggleProductPanel{}),
        });
    passed &= expect(
        panel && panel->commands.size() == 1U &&
            std::get<ToggleUi>(
                panel->commands.front()).id == 110U,
        "panel action did not use the shared sequence");

    auto no_document = snapshot;
    no_document.image_editor.document.reset();
    no_document.preview = PreviewLeaseSnapshot{
        1U,
        Feature::ImagePaint,
        44U,
        2U,
    };
    const auto restore_without_document =
        router.route_ui_actions(
            no_document,
            std::array{
                envelope(UiImagePaintAction{
                    FeatureUiAction::Restore,
                }),
            });
    passed &= expect(
        restore_without_document &&
            restore_without_document->commands.size() == 1U &&
            std::get<RestoreImagePaintPreview>(
                restore_without_document->commands.front()).id ==
                111U,
        "Image Paint restore incorrectly required an editor document");

    auto image_running = no_document;
    image_running.preview = {};
    image_running.job = JobSnapshot{
        1U,
        JobPhase::Dispatching,
        Feature::ImagePaint,
        4U,
        2U,
        JobProgress{
            0U,
            2U,
            2U,
            1U,
            0U,
            0U,
            0.0,
        },
    };
    const auto cancel_without_document =
        router.route_ui_actions(
            image_running,
            std::array{
                envelope(UiImagePaintAction{
                    FeatureUiAction::Cancel,
                }),
            });
    passed &= expect(
        cancel_without_document &&
            cancel_without_document->commands.size() == 1U &&
            std::get<CancelImagePaint>(
                cancel_without_document->commands.front()).id ==
                112U,
        "active Image Paint cancellation required an editor document");

    const auto before_stale = router.snapshot();
    auto stale = envelope(UiToggleProductPanel{});
    stale.expected_snapshot_revision = 16U;
    passed &= expect(
        router.route_ui_actions(
            snapshot,
            std::array{stale}) ==
                std::unexpected(
                    InputCommandRouterError::StaleSnapshot) &&
            router.snapshot() == before_stale,
        "stale UI action mutated router state");

    auto running = snapshot;
    running.job = JobSnapshot{
        1U,
        JobPhase::Dispatching,
        Feature::Paint,
        4U,
        2U,
        JobProgress{
            0U,
            2U,
            2U,
            1U,
            0U,
            0U,
            0.0,
        },
    };
    const auto disabled = router.route_ui_actions(
        running,
        std::array{
            envelope(UiPaintAction{
                FeatureUiAction::Start,
            }),
        });
    passed &= expect(
        disabled && disabled->commands.empty() &&
            disabled->rejections ==
                std::vector<ProductUiActionRejection>{
                    ProductUiActionRejection{
                        0U,
                        ProductUiActionRejectionReason::
                            Unavailable,
                    },
                } &&
            router.snapshot().next_command_id == 113U,
        "disabled UI action consumed a command ID");

    const auto before_invalid = router.snapshot();
    auto redirected_settings = changed;
    redirected_settings.active_image_project =
        core::ActiveImageProjectReference{
            core::ImageProjectReferenceKind::NamedProject,
            std::string{OtherProjectId},
        };
    passed &= expect(
        router.route_ui_actions(
            snapshot,
            std::array{
                envelope(UiRenameCurrentImageProject{
                    std::string(257U, 'x'),
                }),
            }) ==
                std::unexpected(
                    InputCommandRouterError::InvalidUiAction) &&
            router.route_ui_actions(
                snapshot,
                std::array{
                    envelope(UiApplySettings{
                        redirected_settings,
                    }),
                }) ==
                std::unexpected(
                    InputCommandRouterError::InvalidUiAction) &&
            router.route_ui_actions(
                snapshot,
                std::array{
                    envelope(UiPaintAction{
                        static_cast<FeatureUiAction>(255U),
                    }),
                }) ==
                std::unexpected(
                    InputCommandRouterError::InvalidUiAction) &&
            router.snapshot() == before_invalid,
        "invalid UI payload mutated router state");

    const auto before_oversized = router.snapshot();
    passed &= expect(
        router.route_ui_actions(
            snapshot,
            std::array{
                envelope(UiToggleEsp{}),
                envelope(UiToggleProductPanel{}),
            }) ==
                std::unexpected(
                    InputCommandRouterError::UiActionLimit) &&
            router.snapshot() == before_oversized,
        "multiple same-frame UI actions entered one snapshot transaction");

    auto concurrent_ids =
        std::array<std::optional<CommandId>, 8U>{};
    auto threads = std::vector<std::jthread>{};
    threads.reserve(concurrent_ids.size());
    for (std::size_t index = 0U;
         index < concurrent_ids.size();
         ++index)
    {
        threads.emplace_back(
            [&router, &snapshot, &concurrent_ids, index]
            {
                const auto result = router.route_ui_actions(
                    snapshot,
                    std::array{
                        envelope(UiToggleEsp{}),
                    });
                if (result &&
                    result->commands.size() == 1U)
                {
                    concurrent_ids[index] =
                        std::get<ToggleEsp>(
                            result->commands.front()).id;
                }
            });
    }
    threads.clear();
    auto sorted_ids = std::vector<CommandId>{};
    for (const auto id : concurrent_ids)
    {
        if (id)
        {
            sorted_ids.push_back(*id);
        }
    }
    std::ranges::sort(sorted_ids);
    passed &= expect(
        sorted_ids ==
            std::vector<CommandId>{
                113U,
                114U,
                115U,
                116U,
                117U,
                118U,
                119U,
                120U,
            } &&
            router.snapshot().next_command_id == 121U,
        "concurrent UI callbacks duplicated or skipped command IDs");

    auto overflow = InputCommandRouter{
        std::numeric_limits<CommandId>::max()};
    const auto final_command = overflow.route_ui_actions(
        snapshot,
        std::array{
            envelope(UiToggleEsp{}),
        });
    passed &= expect(
        final_command &&
            std::get<ToggleEsp>(
                final_command->commands.front()).id ==
                std::numeric_limits<CommandId>::max() &&
            overflow.route_ui_actions(
                snapshot,
                std::array{
                    envelope(UiToggleEsp{}),
                }) ==
                std::unexpected(
                    InputCommandRouterError::CommandOverflow),
        "shared UI command sequence wrapped");

    router.shutdown();
    passed &= expect(
        router.snapshot().stopped &&
            router.snapshot().held_key_count == 0U &&
            router.route_ui_actions(
                snapshot,
                std::array{
                    envelope(UiToggleEsp{}),
                }) ==
                std::unexpected(
                    InputCommandRouterError::Stopped),
        "shared router shutdown retained UI admission or held keys");

    if (passed)
    {
        std::cout << "PASS product_ui_action_router\n";
    }
    return passed ? 0 : 1;
}
