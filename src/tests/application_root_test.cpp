#include <meccha/application/application_root.hpp>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
using namespace meccha::application;

auto expect(bool condition, std::string_view message) -> bool
{
    if (!condition)
    {
        std::cerr << "FAIL application_root: " << message << '\n';
    }
    return condition;
}

class FakeStorage final : public AtomicTextStorage
{
public:
    auto read_text(std::string_view, std::size_t)
        -> std::expected<std::optional<std::string>, TextStorageError>
        override
    {
        ++read_count;
        if (fail_read)
        {
            return std::unexpected(TextStorageError{
                TextStorageErrorCode::Io,
                "injected config read failure",
            });
        }
        return text;
    }

    auto write_text_atomic(std::string_view, std::string_view)
        -> std::expected<void, TextStorageError> override
    {
        ++write_count;
        return {};
    }

    std::optional<std::string> text{};
    std::size_t read_count{};
    std::size_t write_count{};
    bool fail_read{};
};

class RecordingExecutor final : public GameThreadExecutor
{
public:
    [[nodiscard]] auto is_game_thread() const noexcept -> bool override
    {
        return true;
    }

    auto execute(const GameThreadOperation& operation)
        -> std::expected<void, RuntimeExecutionError> override
    {
        if (fail_after != 0U && operations.size() == fail_after)
        {
            return std::unexpected(RuntimeExecutionError{
                RuntimeExecutionErrorCode::OperationFailure,
                failure,
            });
        }
        operations.push_back(operation);
        return {};
    }

    std::vector<GameThreadOperation> operations{};
    std::size_t fail_after{};
    std::optional<CompatibilityFailure> failure{};
};

class FakeCallbacks final : public RuntimeCallbackPort
{
public:
    auto register_hud_callback(
        void* callback_context,
        HudCallback callback_function)
        -> std::expected<CallbackId, CallbackPortError> override
    {
        ++register_count;
        if (fail_registration)
        {
            return std::unexpected(CallbackPortError::Registration);
        }
        context = callback_context;
        callback = callback_function;
        return CallbackId{77U};
    }

    auto unregister_hud_callback(CallbackId id)
        -> std::expected<void, CallbackPortError> override
    {
        ++unregister_count;
        unregistered_id = id;
        callback = nullptr;
        context = nullptr;
        return {};
    }

    auto invoke(const HudFrameIdentity& identity) -> void
    {
        if (callback != nullptr)
        {
            callback(context, identity);
        }
    }

    void* context{};
    HudCallback callback{};
    std::size_t register_count{};
    std::size_t unregister_count{};
    CallbackId unregistered_id{};
    bool fail_registration{};
};

class FakeFrameExtension final : public RuntimeFrameExtensionPort
{
public:
    auto on_hud_frame(const HudFrameIdentity& identity) noexcept
        -> std::expected<void, RuntimeFrameExtensionError> override
    {
        ++frame_count;
        last_identity = identity;
        if (fail_frame)
        {
            return std::unexpected(RuntimeFrameExtensionError{
                RuntimeFrameExtensionStage::Frame,
                CompatibilityFailure{
                    RuntimeContractId::Canvas,
                    ContractFailureKind::ExecutionFailure,
                    "error.operation.failed",
                },
            });
        }
        return {};
    }

    auto restore_and_stop() noexcept
        -> std::expected<void, RuntimeFrameExtensionError> override
    {
        ++shutdown_count;
        if (fail_shutdown)
        {
            return std::unexpected(RuntimeFrameExtensionError{
                RuntimeFrameExtensionStage::Shutdown,
                CompatibilityFailure{
                    RuntimeContractId::InputControl,
                    ContractFailureKind::ExecutionFailure,
                    "error.operation.failed",
                },
            });
        }
        stopped = true;
        return {};
    }

    std::optional<HudFrameIdentity> last_identity{};
    std::size_t frame_count{};
    std::size_t shutdown_count{};
    bool fail_frame{};
    bool fail_shutdown{};
    bool stopped{};
};
} // namespace

auto main() -> int
{
    using namespace meccha::application;

    auto passed = true;
    FakeStorage storage{};
    FakeCallbacks callbacks{};
    RecordingExecutor executor{};
    ApplicationRoot root{
        callbacks,
        executor,
        storage,
        4U,
        4U,
    };
    FakeFrameExtension frames{};

    passed &= expect(
        root.snapshot()->runtime_phase == ApplicationRuntimePhase::Cold &&
            root.attach_frame_extension(frames).has_value(),
        "a new root was not cold or refused its frame extension");
    passed &= expect(
        root.initialize().has_value() &&
            storage.read_count == 1U &&
            callbacks.register_count == 1U &&
            root.snapshot()->runtime_phase ==
                ApplicationRuntimePhase::Initializing &&
            root.snapshot()->settings ==
                meccha::core::ApplicationConfig{},
        "initialization did not load defaults and register callbacks");

    root.on_update();
    root.on_update();
    passed &= expect(
        executor.operations.empty(),
        "the root update path touched Unreal");

    callbacks.invoke(HudFrameIdentity{10U, 0U, 30U, 40U});
    passed &= expect(
        root.snapshot()->runtime_phase ==
                ApplicationRuntimePhase::Incompatible &&
            root.snapshot()->compatibility.failure &&
            root.snapshot()->compatibility.failure->contract ==
                RuntimeContractId::Canvas &&
            root.snapshot()->diagnostics.size() == 1U &&
            executor.operations.empty(),
        "an invalid frame did not fail closed at the composition root");

    constexpr auto Frame =
        HudFrameIdentity{10U, 20U, 30U, 40U};
    callbacks.invoke(Frame);
    const auto compatible = root.snapshot();
    passed &= expect(
        compatible->runtime_phase ==
                ApplicationRuntimePhase::Compatible &&
            compatible->compatibility.status ==
                CompatibilityStatus::Compatible &&
            !compatible->compatibility.failure &&
            compatible->diagnostics.size() == 1U &&
            compatible->frame_identity == Frame &&
            frames.frame_count == 1U &&
            frames.last_identity == Frame &&
            executor.operations ==
                std::vector<GameThreadOperation>{
                    ResolveInitialContracts{},
                    RebindHudFrame{Frame},
                },
        "the first valid frame did not publish a compatible snapshot");

    passed &= expect(
        root.request_shutdown(9U).has_value() &&
            root.snapshot()->runtime_phase ==
                ApplicationRuntimePhase::ShuttingDown,
        "shutdown did not close the root");
    const auto premature = root.finalize_shutdown();
    passed &= expect(
        !premature &&
            premature.error() ==
                ApplicationRootError::PendingGameThreadRestore &&
            callbacks.unregister_count == 0U,
        "the root finalized before game-thread restoration");
    frames.fail_shutdown = true;
    callbacks.invoke(Frame);
    passed &= expect(
        frames.shutdown_count == 1U &&
            !frames.stopped &&
            callbacks.unregister_count == 0U &&
            executor.operations.back() !=
                GameThreadOperation{RestoreTransientState{9U}},
        "failed frame-extension restoration advanced lifecycle shutdown");
    frames.fail_shutdown = false;
    callbacks.invoke(Frame);
    passed &= expect(
        frames.shutdown_count == 2U &&
            frames.stopped &&
            callbacks.unregister_count == 0U &&
            executor.operations.back() !=
                GameThreadOperation{RestoreTransientState{9U}},
        "successful frame-extension restoration did not request lifecycle "
        "shutdown in order");
    callbacks.invoke(Frame);
    passed &= expect(
        executor.operations.back() ==
            GameThreadOperation{RestoreTransientState{9U}},
        "the composition root did not restore transient state after its "
        "frame extension");
    passed &= expect(
        root.finalize_shutdown().has_value() &&
            callbacks.unregister_count == 1U &&
            callbacks.unregistered_id == 77U &&
            root.snapshot()->runtime_phase ==
                ApplicationRuntimePhase::Stopped,
        "the composition root did not stop after exact unregistration");

    FakeStorage broken_storage{};
    broken_storage.fail_read = true;
    FakeCallbacks unused_callbacks{};
    RecordingExecutor unused_executor{};
    ApplicationRoot config_failure{
        unused_callbacks,
        unused_executor,
        broken_storage,
        2U,
        2U,
    };
    const auto failed_config = config_failure.initialize();
    const auto config_snapshot = config_failure.snapshot();
    passed &= expect(
        !failed_config &&
            failed_config.error() ==
                ApplicationRootError::ConfigurationLoad &&
            config_snapshot->runtime_phase ==
                ApplicationRuntimePhase::Incompatible &&
            config_snapshot->compatibility.failure &&
            config_snapshot->compatibility.failure->contract ==
                RuntimeContractId::RuntimeInitialization &&
            config_snapshot->diagnostics.size() == 1U &&
            unused_callbacks.register_count == 0U,
        "configuration failure did not fail closed before callbacks");

    FakeStorage valid_storage{};
    FakeCallbacks broken_callbacks{};
    broken_callbacks.fail_registration = true;
    RecordingExecutor callback_executor{};
    ApplicationRoot callback_failure{
        broken_callbacks,
        callback_executor,
        valid_storage,
        2U,
        2U,
    };
    const auto failed_callback = callback_failure.initialize();
    const auto callback_snapshot = callback_failure.snapshot();
    passed &= expect(
        !failed_callback &&
            failed_callback.error() ==
                ApplicationRootError::RuntimeInitialization &&
            callback_snapshot->runtime_phase ==
                ApplicationRuntimePhase::Incompatible &&
            callback_snapshot->compatibility.failure &&
            callback_snapshot->compatibility.failure->contract ==
                RuntimeContractId::HudCallback &&
            callback_snapshot->diagnostics.size() == 1U,
        "callback failure did not publish structured incompatibility");

    FakeStorage contract_storage{};
    FakeCallbacks contract_callbacks{};
    RecordingExecutor contract_executor{};
    contract_executor.fail_after = 1U;
    contract_executor.failure = CompatibilityFailure{
        RuntimeContractId::PaintAtUvWithBrush,
        ContractFailureKind::ParameterSizeMismatch,
        "error.operation.failed",
    };
    ApplicationRoot contract_failure{
        contract_callbacks,
        contract_executor,
        contract_storage,
        2U,
        2U,
    };
    passed &= expect(
        contract_failure.initialize().has_value(),
        "contract-failure root did not initialize");
    contract_callbacks.invoke(Frame);
    const auto contract_snapshot = contract_failure.snapshot();
    passed &= expect(
        contract_snapshot->runtime_phase ==
                ApplicationRuntimePhase::Incompatible &&
            contract_snapshot->compatibility.failure ==
                contract_executor.failure &&
            contract_snapshot->diagnostics.back()
                    .compatibility_failure ==
                contract_executor.failure,
        "executor contract context was lost before snapshot publication");

    FakeStorage extension_storage{};
    FakeCallbacks extension_callbacks{};
    RecordingExecutor extension_executor{};
    ApplicationRoot extension_failure{
        extension_callbacks,
        extension_executor,
        extension_storage,
        2U,
        2U,
    };
    FakeFrameExtension broken_extension{};
    broken_extension.fail_frame = true;
    passed &= expect(
        extension_failure.attach_frame_extension(broken_extension)
                .has_value() &&
            extension_failure.initialize().has_value(),
        "frame-extension failure fixture did not initialize");
    extension_callbacks.invoke(Frame);
    const auto extension_snapshot = extension_failure.snapshot();
    passed &= expect(
        broken_extension.frame_count == 1U &&
            extension_snapshot->runtime_phase ==
                ApplicationRuntimePhase::Incompatible &&
            extension_snapshot->compatibility.failure &&
            extension_snapshot->compatibility.failure->contract ==
                RuntimeContractId::Canvas &&
            extension_snapshot->diagnostics.size() == 1U &&
            !extension_failure
                 .attach_frame_extension(broken_extension)
                 .has_value(),
        "frame-extension failure was not contained and published");

    if (passed)
    {
        std::cout << "PASS application_root\n";
        return 0;
    }
    return 1;
}
