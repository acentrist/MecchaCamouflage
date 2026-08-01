    auto begin_projective_paint_capture(
        const core::PaintSettings& settings,
        application::JobGeneration generation)
        -> std::expected<void, application::RuntimeExecutionError>
    {
        if (!IsInGameThreadRaw())
        {
            return std::unexpected(application::RuntimeExecutionError{
                application::RuntimeExecutionErrorCode::WrongThread,
                std::nullopt,
            });
        }
        if (generation == 0U || projective_paint_capture_)
        {
            return runtime_failure(
                application::RuntimeContractId::PaintCapture,
                application::ContractFailureKind::InvalidValue);
        }
        try
        {
            auto seed = prepare_paint_capture_seed(settings, true);
            if (!seed)
            {
                return std::unexpected(seed.error());
            }
            projective_paint_capture_.emplace(
                ProjectivePaintCaptureSession{generation, std::move(*seed)});
            const auto& admitted = projective_paint_capture_->seed;
            const auto started = paint_appearance_worker_.start(
                generation,
                application::PaintAppearanceGeometryPrepareWork{
                    admitted.profile->sampling,
                    admitted.profile->image,
                    admitted.bone_transforms,
                    settings.brush_size_texels,
                    admitted.view,
                    admitted.capture_viewport,
                });
            if (!started)
            {
                projective_paint_capture_.reset();
                return runtime_failure(
                    application::RuntimeContractId::PaintCapture,
                    application::ContractFailureKind::ExecutionFailure);
            }
            return {};
        }
        catch (...)
        {
            projective_paint_capture_.reset();
            return runtime_failure(
                application::RuntimeContractId::PaintCapture,
                application::ContractFailureKind::ExecutionFailure);
        }
    }

    auto advance_projective_paint_capture(
        application::JobGeneration generation)
        -> std::expected<
            std::optional<application::CapturedPaintJob>,
            application::RuntimeExecutionError>
    {
        using Result = std::optional<application::CapturedPaintJob>;
        if (!IsInGameThreadRaw())
        {
            return std::unexpected(application::RuntimeExecutionError{
                application::RuntimeExecutionErrorCode::WrongThread,
                std::nullopt,
            });
        }
        if (!projective_paint_capture_ || generation == 0U ||
            projective_paint_capture_->generation != generation)
        {
            return runtime_failure(
                application::RuntimeContractId::PaintCapture,
                application::ContractFailureKind::StaleObject);
        }

        try
        {
            auto& session = *projective_paint_capture_;
            const auto component_handle = application::RuntimeObjectHandle{
                session.seed.bound.component_identity,
                session.seed.bound.component_generation,
            };
            switch (session.stage)
            {
            case ProjectivePaintCaptureStage::GeometryPending:
            {
                auto completion = paint_appearance_worker_.poll();
                if (!completion)
                {
                    return Result{};
                }
                const auto* prepared =
                    completion->generation == generation && completion->result
                        ? std::get_if<application::
                              PaintAppearanceGeometryPrepared>(
                              &*completion->result)
                        : nullptr;
                if (prepared == nullptr || !prepared->geometry ||
                    prepared->geometry->empty() ||
                    !prepared->source_queries ||
                    prepared->source_queries->empty() ||
                    prepared->replay_samples == 0U ||
                    prepared->calibration_samples == 0U)
                {
                    session.stage = ProjectivePaintCaptureStage::Failed;
                    return runtime_failure(
                        application::RuntimeContractId::PaintCapture,
                        application::ContractFailureKind::InvalidValue);
                }
                session.geometry = prepared->geometry;
                session.source_queries = prepared->source_queries;
                session.source_samples.assign(
                    session.geometry->size(),
                    core::PaintAppearanceSourceSample{});
                session.stage = ProjectivePaintCaptureStage::SourceQuery;
                return Result{};
            }
            case ProjectivePaintCaptureStage::SourceQuery:
            {
                if (!session.geometry || !session.source_queries ||
                    session.source_queries->empty() ||
                    session.source_samples.size() !=
                        session.geometry->size() ||
                    session.next_source_query >
                        session.source_queries->size() ||
                    !object_is_live(
                        session.seed.component,
                        session.seed.paint.runtime_paintable_class) ||
                    !object_is_live(
                        session.seed.mesh,
                        session.seed.paint.mesh_component_class) ||
                    !object_is_live(
                        session.seed.active.controller,
                        session.seed.paint.player_controller_class))
                {
                    session.stage = ProjectivePaintCaptureStage::Failed;
                    return runtime_failure(
                        application::RuntimeContractId::PaintCapture,
                        application::ContractFailureKind::StaleObject);
                }
                const auto fingerprint =
                    current_paint_capture_fingerprint(session.seed);
                if (!fingerprint ||
                    !core::paint_appearance_camera_matches(
                        session.seed.camera_fingerprint,
                        *fingerprint))
                {
                    session.stage = ProjectivePaintCaptureStage::Failed;
                    return fingerprint
                               ? runtime_failure(
                                     application::RuntimeContractId::
                                         PaintCapture,
                                     application::ContractFailureKind::
                                         InvalidValue)
                               : std::unexpected(fingerprint.error());
                }
                const auto end = std::min(
                    session.source_queries->size(),
                    session.next_source_query +
                        PaintAppearanceSourceQueriesPerFrame);
                for (; session.next_source_query < end;
                     ++session.next_source_query)
                {
                    const auto& query = (*session.source_queries)
                        [session.next_source_query];
                    if (query.geometry_index >=
                        session.source_samples.size())
                    {
                        session.stage =
                            ProjectivePaintCaptureStage::Failed;
                        return runtime_failure(
                            application::RuntimeContractId::PaintCapture,
                            application::ContractFailureKind::InvalidValue);
                    }
                    const auto screen = RuntimeVector2d{
                        query.screen.x * fingerprint->viewport_width /
                            static_cast<double>(fingerprint->width),
                        query.screen.y * fingerprint->viewport_height /
                            static_cast<double>(fingerprint->height),
                    };
                    auto parameters = encode_paint_hit_test(
                        session.seed.mesh,
                        session.seed.active.controller,
                        screen);
                    if (!parameters ||
                        screen.x >= fingerprint->viewport_width ||
                        screen.y >= fingerprint->viewport_height)
                    {
                        session.stage =
                            ProjectivePaintCaptureStage::Failed;
                        return runtime_failure(
                            application::RuntimeContractId::PaintCapture,
                            application::ContractFailureKind::InvalidValue);
                    }
                    session.seed.component->ProcessEvent(
                        session.seed.paint.hit_test_at_screen_position,
                        &*parameters);
                    const auto& hit = parameters->return_value;
                    if (!hit.success)
                    {
                        continue;
                    }
                    const auto normal_length_squared =
                        hit.hit_normal.x * hit.hit_normal.x +
                        hit.hit_normal.y * hit.hit_normal.y +
                        hit.hit_normal.z * hit.hit_normal.z;
                    if (!std::isfinite(hit.hit_uv.x) ||
                        !std::isfinite(hit.hit_uv.y) ||
                        !std::isfinite(hit.hit_normal.x) ||
                        !std::isfinite(hit.hit_normal.y) ||
                        !std::isfinite(hit.hit_normal.z) ||
                        !std::isfinite(normal_length_squared) ||
                        normal_length_squared <= 1.0e-12)
                    {
                        session.stage =
                            ProjectivePaintCaptureStage::Failed;
                        return runtime_failure(
                            application::RuntimeContractId::PaintCapture,
                            application::ContractFailureKind::InvalidValue);
                    }
                    const auto resolved =
                        core::resolve_paint_appearance_source_hit(
                            query,
                            core::PaintAppearanceSourceHit{
                                true,
                                hit.hit_uv.x,
                                hit.hit_uv.y,
                                core::Vector3d{
                                    hit.hit_world_position.x,
                                    hit.hit_world_position.y,
                                    hit.hit_world_position.z,
                                },
                            });
                    if (!resolved)
                    {
                        session.stage =
                            ProjectivePaintCaptureStage::Failed;
                        return runtime_failure(
                            application::RuntimeContractId::PaintCapture,
                            application::ContractFailureKind::InvalidValue);
                    }
                    session.source_samples[query.geometry_index] = *resolved;
                }
                if (session.next_source_query ==
                    session.source_queries->size())
                {
                    session.evidence.source_samples =
                        std::make_shared<const std::vector<
                            core::PaintAppearanceSourceSample>>(
                            std::move(session.source_samples));
                    session.stage =
                        ProjectivePaintCaptureStage::SourceCapture;
                }
                return Result{};
            }
            case ProjectivePaintCaptureStage::SourceCapture:
            {
                if (session.next_source_pass >=
                    session.seed.pass_plan.passes.size())
                {
                    session.stage = ProjectivePaintCaptureStage::Failed;
                    return runtime_failure(
                        application::RuntimeContractId::PaintCapture,
                        application::ContractFailureKind::InvalidValue);
                }
                const auto captured = capture_projective_source_pass(
                    session,
                    session.seed.pass_plan
                        .passes[session.next_source_pass]);
                if (!captured)
                {
                    session.stage = ProjectivePaintCaptureStage::Failed;
                    return std::unexpected(captured.error());
                }
                ++session.next_source_pass;
                if (session.next_source_pass ==
                    session.seed.pass_plan.passes.size())
                {
                    const auto started = paint_appearance_worker_.start(
                        generation,
                        application::PaintAppearanceCapturePrepareWork{
                            session.geometry,
                            session.evidence,
                            session.seed.settings,
                        });
                    if (!started)
                    {
                        session.stage =
                            ProjectivePaintCaptureStage::Failed;
                        return runtime_failure(
                            application::RuntimeContractId::PaintCapture,
                            application::ContractFailureKind::
                                ExecutionFailure);
                    }
                    session.stage =
                        ProjectivePaintCaptureStage::ModelPending;
                }
                return Result{};
            }
            case ProjectivePaintCaptureStage::ModelPending:
            {
                auto completion = paint_appearance_worker_.poll();
                if (!completion)
                {
                    return Result{};
                }
                const auto* prepared =
                    completion->generation == generation && completion->result
                        ? std::get_if<application::
                              PaintAppearancePrepared>(
                              &*completion->result)
                        : nullptr;
                if (prepared == nullptr || !prepared->model ||
                    !prepared->baseline ||
                    prepared->baseline->appearances.empty())
                {
                    session.stage = ProjectivePaintCaptureStage::Failed;
                    return runtime_failure(
                        application::RuntimeContractId::PaintCapture,
                        application::ContractFailureKind::InvalidValue);
                }
                session.model = prepared->model;
                session.baseline = prepared->baseline;
                session.stage =
                    ProjectivePaintCaptureStage::SnapshotPending;
                return Result{};
            }
            case ProjectivePaintCaptureStage::SnapshotPending:
            {
                if (!session.model || !session.baseline)
                {
                    session.stage = ProjectivePaintCaptureStage::Failed;
                    return runtime_failure(
                        application::RuntimeContractId::PaintCapture,
                        application::ContractFailureKind::InvalidValue);
                }
                auto snapshot = capture_preview(component_handle);
                if (!snapshot || snapshot->component != component_handle)
                {
                    session.stage = ProjectivePaintCaptureStage::Failed;
                    return snapshot
                               ? runtime_failure(
                                     application::RuntimeContractId::
                                         PaintCapture,
                                     application::ContractFailureKind::
                                         InvalidValue)
                               : std::unexpected(snapshot.error());
                }
                session.preview_snapshot = std::move(*snapshot);
                const auto started = start_projective_paint_candidate(
                    session,
                    ProjectivePaintCandidatePhase::Baseline,
                    session.baseline);
                if (!started)
                {
                    session.stage = ProjectivePaintCaptureStage::Failed;
                    return std::unexpected(started.error());
                }
                return Result{};
            }
            case ProjectivePaintCaptureStage::CandidatePending:
            {
                auto completion = paint_appearance_worker_.poll();
                if (!completion)
                {
                    return Result{};
                }
                const auto* candidate =
                    completion->generation == generation && completion->result
                        ? std::get_if<application::
                              PaintAppearanceCandidate>(
                              &*completion->result)
                        : nullptr;
                if (candidate == nullptr || !candidate->preview ||
                    !candidate->readback_references ||
                    candidate->readback_references->empty())
                {
                    session.stage = ProjectivePaintCaptureStage::Failed;
                    return runtime_failure(
                        application::RuntimeContractId::PaintCapture,
                        application::ContractFailureKind::InvalidValue);
                }
                session.candidate_preview = candidate->preview;
                session.readback_references =
                    candidate->readback_references;
                session.stage =
                    ProjectivePaintCaptureStage::PreviewApplyPending;
                return Result{};
            }
            case ProjectivePaintCaptureStage::PreviewApplyPending:
            {
                if (!session.preview_snapshot || !session.candidate_preview)
                {
                    session.stage = ProjectivePaintCaptureStage::Failed;
                    return runtime_failure(
                        application::RuntimeContractId::PaintCapture,
                        application::ContractFailureKind::InvalidValue);
                }
                session.preview_mutated = true;
                const auto applied = apply_preview(
                    component_handle,
                    *session.candidate_preview);
                if (!applied)
                {
                    session.stage =
                        ProjectivePaintCaptureStage::RestorePending;
                    return std::unexpected(applied.error());
                }
                session.current_packed_b_verified = true;
                session.preview_applied_ms = steady_milliseconds();
                session.stage =
                    ProjectivePaintCaptureStage::PreviewSettle;
                return Result{};
            }
            case ProjectivePaintCaptureStage::PreviewSettle:
            {
                constexpr auto PreviewSettleMilliseconds =
                    std::uint64_t{48U};
                const auto now_ms = steady_milliseconds();
                if (now_ms < session.preview_applied_ms ||
                    now_ms - session.preview_applied_ms <
                        PreviewSettleMilliseconds)
                {
                    return Result{};
                }
                session.stage =
                    ProjectivePaintCaptureStage::FeedbackCapture;
                return Result{};
            }
            case ProjectivePaintCaptureStage::FeedbackCapture:
            {
                const auto& plan =
                    paint_appearance_feedback_capture_plan();
                const auto pass_count =
                    session.candidate_phase ==
                            ProjectivePaintCandidatePhase::Baseline
                        ? plan.size()
                        : std::size_t{1U};
                if (session.next_feedback_pass >= pass_count)
                {
                    session.stage =
                        ProjectivePaintCaptureStage::RestorePending;
                    return runtime_failure(
                        application::RuntimeContractId::PaintCapture,
                        application::ContractFailureKind::InvalidValue);
                }
                const auto captured = capture_projective_feedback_pass(
                    session,
                    plan[session.next_feedback_pass]);
                if (!captured)
                {
                    session.stage =
                        ProjectivePaintCaptureStage::RestorePending;
                    return std::unexpected(captured.error());
                }
                ++session.next_feedback_pass;
                if (session.next_feedback_pass == pass_count)
                {
                    session.stage =
                        ProjectivePaintCaptureStage::RestorePending;
                }
                return Result{};
            }
            case ProjectivePaintCaptureStage::RestorePending:
            {
                if (!session.preview_mutated ||
                    !session.preview_snapshot ||
                    !session.readback_references)
                {
                    session.stage = ProjectivePaintCaptureStage::Failed;
                    return runtime_failure(
                        application::RuntimeContractId::PaintCapture,
                        application::ContractFailureKind::InvalidValue);
                }
                const auto restored =
                    restore_preview(*session.preview_snapshot);
                if (!restored)
                {
                    return std::unexpected(restored.error());
                }
                session.preview_mutated = false;
                session.candidate_preview.reset();
                if (!session.current_packed_b_verified)
                {
                    session.stage = ProjectivePaintCaptureStage::Failed;
                    return runtime_failure(
                        application::RuntimeContractId::PaintCapture,
                        application::ContractFailureKind::InvalidValue);
                }
                session.all_packed_b_verified =
                    session.all_packed_b_verified &&
                    session.current_packed_b_verified;
                if (session.candidate_phase ==
                    ProjectivePaintCandidatePhase::Baseline)
                {
                    const auto started = paint_appearance_worker_.start(
                        generation,
                        application::
                            PaintAppearanceBaselineCalibrateWork{
                                session.model,
                                session.seed.camera_fingerprint,
                                session.readback_references,
                                session.feedback_evidence,
                                core::PaintAppearanceTargetE0Evidence{
                                    session.feedback_evidence.base_color,
                                    session.target_intrinsic_e0,
                                },
                                session.seed.settings,
                                session.all_packed_b_verified,
                            });
                    if (!started)
                    {
                        session.stage =
                            ProjectivePaintCaptureStage::Failed;
                        return runtime_failure(
                            application::RuntimeContractId::PaintCapture,
                            application::ContractFailureKind::
                                ExecutionFailure);
                    }
                    session.stage = ProjectivePaintCaptureStage::
                        BaselineCalibrationPending;
                    return Result{};
                }
                if (!session.calibration ||
                    !session.baseline_feedback || !session.target_e0 ||
                    !session.feedback_evidence.final_hdr.pixels)
                {
                    session.stage = ProjectivePaintCaptureStage::Failed;
                    return runtime_failure(
                        application::RuntimeContractId::PaintCapture,
                        application::ContractFailureKind::InvalidValue);
                }
                const auto started = paint_appearance_worker_.start(
                    generation,
                    application::PaintAppearanceFinalizeWork{
                        session.model,
                        session.calibration,
                        *session.baseline_feedback,
                        session.seed.camera_fingerprint,
                        session.feedback_evidence.final_hdr,
                        session.target_e0->noise,
                        session.seed.settings,
                        session.all_packed_b_verified,
                    });
                if (!started)
                {
                    session.stage = ProjectivePaintCaptureStage::Failed;
                    return runtime_failure(
                        application::RuntimeContractId::PaintCapture,
                        application::ContractFailureKind::ExecutionFailure);
                }
                session.stage =
                    ProjectivePaintCaptureStage::FinalResolvePending;
                return Result{};
            }
            case ProjectivePaintCaptureStage::BaselineCalibrationPending:
            {
                auto completion = paint_appearance_worker_.poll();
                if (!completion)
                {
                    return Result{};
                }
                const auto* calibrated =
                    completion->generation == generation && completion->result
                        ? std::get_if<application::
                              PaintAppearanceBaselineCalibrated>(
                              &*completion->result)
                        : nullptr;
                if (calibrated == nullptr ||
                    !calibrated->calibration ||
                    calibrated->calibration->endpoint.appearances.empty() ||
                    !calibrated->target_e0.noise.ok)
                {
                    session.stage = ProjectivePaintCaptureStage::Failed;
                    return runtime_failure(
                        application::RuntimeContractId::PaintCapture,
                        application::ContractFailureKind::InvalidValue);
                }
                session.baseline_feedback = calibrated->feedback;
                session.target_e0 = calibrated->target_e0;
                session.calibration = calibrated->calibration;
                const auto endpoint =
                    std::make_shared<const core::PaintProjectiveRaster>(
                        session.calibration->endpoint);
                const auto started = start_projective_paint_candidate(
                    session,
                    ProjectivePaintCandidatePhase::Endpoint,
                    std::move(endpoint));
                if (!started)
                {
                    session.stage = ProjectivePaintCaptureStage::Failed;
                    return std::unexpected(started.error());
                }
                return Result{};
            }
            case ProjectivePaintCaptureStage::FinalResolvePending:
            {
                auto completion = paint_appearance_worker_.poll();
                if (!completion)
                {
                    return Result{};
                }
                const auto* resolved =
                    completion->generation == generation && completion->result
                        ? std::get_if<application::
                              PaintAppearanceResolved>(
                              &*completion->result)
                        : nullptr;
                const auto expected =
                    static_cast<std::size_t>(session.seed.camera.width) *
                    session.seed.camera.height;
                if (resolved == nullptr || !resolved->appearances ||
                    !resolved->available ||
                    resolved->appearances->size() != expected ||
                    resolved->available->size() != expected)
                {
                    session.stage = ProjectivePaintCaptureStage::Failed;
                    return runtime_failure(
                        application::RuntimeContractId::PaintCapture,
                        application::ContractFailureKind::InvalidValue);
                }
                session.resolved_appearances = resolved->appearances;
                session.resolved_available = resolved->available;
                session.stage =
                    ProjectivePaintCaptureStage::FinalResolved;
                return Result{};
            }
            case ProjectivePaintCaptureStage::FinalResolved:
            {
                if (session.preview_mutated ||
                    !session.resolved_appearances ||
                    !session.resolved_available || !session.seed.profile ||
                    !object_is_live(
                        session.seed.world,
                        session.seed.image.world_class) ||
                    !object_is_live(
                        session.seed.component,
                        session.seed.paint.runtime_paintable_class) ||
                    session.seed.component->GetWorld() != session.seed.world)
                {
                    session.stage = ProjectivePaintCaptureStage::Failed;
                    return runtime_failure(
                        application::RuntimeContractId::PaintCapture,
                        application::ContractFailureKind::InvalidValue);
                }
                auto completed = application::CapturedPaintJob{
                    application::RuntimeObjectHandle{
                        session.seed.bound.component_identity,
                        session.seed.bound.component_generation,
                    },
                    application::PaintPlanningRequest{
                        core::PaintCaptureInput{
                            session.seed.profile->sampling,
                            session.seed.profile->image,
                            session.seed.bone_transforms,
                            session.seed.settings,
                            session.seed.view,
                            session.seed.capture_viewport,
                            core::PaintCaptureRaster{
                                session.seed.camera.width,
                                session.seed.camera.height,
                                session.resolved_appearances,
                                session.resolved_available,
                            },
                        },
                    },
                    core::replication_pacing_plan({}),
                };
                projective_paint_capture_.reset();
                return Result{std::move(completed)};
            }
            case ProjectivePaintCaptureStage::Failed:
                return runtime_failure(
                    application::RuntimeContractId::PaintCapture,
                    application::ContractFailureKind::InvalidValue);
            }
        }
        catch (...)
        {
            projective_paint_capture_->stage =
                projective_paint_capture_->preview_mutated
                    ? ProjectivePaintCaptureStage::RestorePending
                    : ProjectivePaintCaptureStage::Failed;
            return runtime_failure(
                application::RuntimeContractId::PaintCapture,
                application::ContractFailureKind::ExecutionFailure);
        }
        return runtime_failure(
            application::RuntimeContractId::PaintCapture,
            application::ContractFailureKind::InvalidValue);
    }

    auto cancel_projective_paint_capture(
        application::JobGeneration generation)
        -> std::expected<bool, application::RuntimeExecutionError>
    {
        if (!IsInGameThreadRaw())
        {
            return std::unexpected(application::RuntimeExecutionError{
                application::RuntimeExecutionErrorCode::WrongThread,
                std::nullopt,
            });
        }
        if (!projective_paint_capture_ || generation == 0U ||
            projective_paint_capture_->generation != generation)
        {
            return runtime_failure(
                application::RuntimeContractId::PaintCapture,
                application::ContractFailureKind::StaleObject);
        }
        try
        {
            const auto stage = projective_paint_capture_->stage;
            if (stage == ProjectivePaintCaptureStage::GeometryPending ||
                stage == ProjectivePaintCaptureStage::ModelPending ||
                stage == ProjectivePaintCaptureStage::CandidatePending ||
                stage == ProjectivePaintCaptureStage::
                             BaselineCalibrationPending ||
                stage == ProjectivePaintCaptureStage::FinalResolvePending)
            {
                const auto requested =
                    paint_appearance_worker_.request_cancel(generation);
                if (requested == application::
                                     PaintAppearanceWorkCancelResult::
                                         StaleGeneration ||
                    requested == application::
                                     PaintAppearanceWorkCancelResult::Idle)
                {
                    return runtime_failure(
                        application::RuntimeContractId::PaintCapture,
                        application::ContractFailureKind::ExecutionFailure);
                }
                if (!paint_appearance_worker_.poll())
                {
                    return false;
                }
            }
            if (projective_paint_capture_->preview_mutated)
            {
                if (!projective_paint_capture_->preview_snapshot)
                {
                    return runtime_failure(
                        application::RuntimeContractId::PaintCapture,
                        application::ContractFailureKind::InvalidValue);
                }
                const auto restored = restore_preview(
                    *projective_paint_capture_->preview_snapshot);
                if (!restored)
                {
                    return std::unexpected(restored.error());
                }
                projective_paint_capture_->preview_mutated = false;
            }
            projective_paint_capture_.reset();
            return true;
        }
        catch (...)
        {
            return runtime_failure(
                application::RuntimeContractId::PaintCapture,
                application::ContractFailureKind::ExecutionFailure);
        }
    }

