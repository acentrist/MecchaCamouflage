from __future__ import annotations

import argparse
import ctypes
import ctypes.wintypes
import json
import os
import signal
import time
import traceback
from concurrent.futures import ThreadPoolExecutor, TimeoutError as FutureTimeoutError
from uuid import uuid4
from pathlib import Path
from time import perf_counter, sleep
from typing import Any, Callable

from src.adapters import NoopAdapter, Ue4ssStubAdapter, XenosBridgeAdapter
from src.adapters.base import ApplyResult
from src import __version__
from src.core import (
    PlanConfig,
    build_color_sampler_from_args,
    compose_plan,
)
from src.core.algorithms import (
    estimate_readback_cost_ms,
    simulate_paint_distribution,
)
from src.diagnostics import RuntimeDiagnostics
from src.native_bridge import (
    DEFAULT_BRIDGE_HOST,
    NativeBridgeClient,
    bridge_progress_file,
    choose_bridge_port,
    inject_bridge,
    native_assets,
)
from src.plan_io import read_plan_json, write_plan_json
from src.plan_io import plan_to_dict
from src.process import DEFAULT_GAME_PROCESS_NAME, ProcessInfo, find_process_by_name


class _StopState:
    running = True


class _KeyEdgeTrigger:
    def __init__(self, predicate: Callable[[], bool]) -> None:
        self._predicate = predicate
        self._was_pressed = False

    def consume(self) -> bool:
        pressed = bool(self._predicate())
        triggered = pressed and not self._was_pressed
        self._was_pressed = pressed
        return triggered

    def close(self) -> None:
        return None


class _WindowsRegisteredHotkey:
    def __init__(self, vk_code: int, hotkey_id: int = 0x4D43) -> None:
        self.vk_code = vk_code
        self.hotkey_id = hotkey_id
        self._registered = False
        self.last_error = 0
        self._user32 = ctypes.windll.user32
        self._register()

    @property
    def registered(self) -> bool:
        return self._registered

    def _register(self) -> None:
        mod_norepeat = 0x4000
        self._registered = bool(self._user32.RegisterHotKey(None, self.hotkey_id, mod_norepeat, self.vk_code))
        if not self._registered:
            self.last_error = int(ctypes.get_last_error())

    def consume(self) -> bool:
        if not self._registered:
            return False
        msg = ctypes.wintypes.MSG()
        pm_remove = 0x0001
        wm_hotkey = 0x0312
        while self._user32.PeekMessageW(ctypes.byref(msg), None, 0, 0, pm_remove):
            if msg.message == wm_hotkey and msg.wParam == self.hotkey_id:
                return True
            self._user32.TranslateMessage(ctypes.byref(msg))
            self._user32.DispatchMessageW(ctypes.byref(msg))
        return False

    def close(self) -> None:
        if self._registered:
            self._user32.UnregisterHotKey(None, self.hotkey_id)
            self._registered = False


class _CompositeTrigger:
    def __init__(self, triggers: list[Any], backend: str) -> None:
        self.triggers = triggers
        self.backend = backend

    def consume(self) -> bool:
        return any(bool(trigger.consume()) for trigger in self.triggers)

    def close(self) -> None:
        for trigger in self.triggers:
            close = getattr(trigger, "close", None)
            if close:
                close()


def _log(message: str) -> None:
    print(message, flush=True)


def _stop_signal(_sig: int, _frame: Any) -> None:
    _StopState.running = False


def parse_color(value: str) -> tuple[float, float, float]:
    parts = [float(v.strip()) for v in value.split(",")]
    if len(parts) != 3:
        raise argparse.ArgumentTypeError("color must be r,g,b")
    return tuple(max(0.0, min(1.0, v / 255.0 if v > 1.0 else v)) for v in parts)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Python runtime for MecchaCamouflage painting.")
    parser.add_argument(
        "--mode",
        choices=["generate", "apply", "loop", "service"],
        default="service",
    )
    parser.add_argument("--input-plan", help="Load existing plan from JSON.")
    parser.add_argument("--out-plan", default="out/paint_plan.json", help="Output path for generated plan")
    parser.add_argument("--out-dir", default="out", help="Artifact directory for non-apply mode")
    parser.add_argument("--input-image", help="Optional reference image used for sample colors")
    parser.add_argument("--sample-count", type=int, default=2048)
    parser.add_argument("--viewport-width", type=int, default=1920)
    parser.add_argument("--viewport-height", type=int, default=1080)
    parser.add_argument("--texture-width", type=int, default=2048)
    parser.add_argument("--texture-height", type=int, default=2048)
    parser.add_argument("--seed", type=int, default=1234)
    parser.add_argument("--seed-step", type=int, default=1, help="Seed increment per loop frame.")
    parser.add_argument("--jitter", type=float, default=0.5)
    parser.add_argument("--include-side", action="store_true")
    parser.add_argument("--include-back", action="store_true")
    parser.add_argument("--roughness", type=float, default=0.65)
    parser.add_argument("--metallic", type=float, default=0.0)
    parser.add_argument(
        "--base-color",
        type=parse_color,
        default=(0.42, 0.42, 0.36),
        help="r,g,b with 0-1 or 0-255 range",
    )
    parser.add_argument("--adapter", choices=["noop", "ue4ss", "xenos"], default="xenos")
    parser.add_argument("--game-process-name", default=DEFAULT_GAME_PROCESS_NAME)
    parser.add_argument("--process-poll-seconds", type=float, default=1.0)
    parser.add_argument("--status-interval-seconds", type=float, default=2.0)
    parser.add_argument("--log-dir", default="")
    parser.add_argument("--bridge-host", default=DEFAULT_BRIDGE_HOST)
    parser.add_argument(
        "--bridge-port",
        type=int,
        default=0,
        help="Native bridge TCP port. 0 chooses a free local port for this runtime.",
    )
    parser.add_argument("--native-root", default="")
    parser.add_argument(
        "--queue-dir",
        default="artifacts/ue4ss_stub",
        help="Queue directory for ue4ss adapter",
    )
    parser.add_argument("--bridge-path", help="Path/URL for xenos adapter transport.")
    parser.add_argument(
        "--bridge-transport",
        choices=["auto", "http", "tcp", "file"],
        default="auto",
        help="Transport override for xenos adapter.",
    )
    parser.add_argument(
        "--bridge-timeout-seconds",
        type=float,
        default=240.0,
        help="Timeout for xenos transport operations.",
    )
    parser.add_argument(
        "--native-apply-mode",
        choices=(
            "metallic_base_then_front_texture_import_diagnostic",
            "front_metallic_texture_import_diagnostic",
            "front_metallic_texture_paint_stream",
            "texture_atlas_paint_api_stream",
            "texture_import_diagnostic",
            "cpu_mesh_raycast",
            "cpu_mesh_probe_only",
            "cpu_mesh_texture_import_diagnostic",
            "cpu_mesh_texture_paint_stream",
            "sdk_deep_probe_only",
        ),
        default="metallic_base_then_front_texture_import_diagnostic",
        help="Native F10 apply mode; texture_import_diagnostic is explicit temporary diagnostics only.",
    )
    parser.add_argument(
        "--bridge-retries",
        type=int,
        default=0,
        help="Retry count for transient xenos send failures.",
    )
    parser.add_argument(
        "--bridge-retry-delay-ms",
        type=float,
        default=250.0,
        help="Delay between xenos transport retries.",
    )
    parser.add_argument(
        "--bridge-wait-response",
        action="store_true",
        help="Wait for xenos bridge ack/response before continuing.",
    )
    parser.add_argument("--print-summary", action="store_true")
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--quick", action="store_true", help="Loop/service shortcuts for diagnostics.")
    parser.add_argument("--loop-frames", type=int, default=0, help="Loop frame count; 0 uses loop-duration-seconds")
    parser.add_argument(
        "--loop-duration-seconds",
        type=float,
        default=10.0,
        help="Loop duration when --loop-frames is 0",
    )
    parser.add_argument(
        "--service-max-frames",
        type=int,
        default=0,
        help="Service mode cap; 0 means unlimited.",
    )
    parser.add_argument(
        "--service-max-duration-seconds",
        type=float,
        default=0.0,
        help="Service mode time cap; 0 means unlimited.",
    )
    parser.add_argument(
        "--service-stop-file",
        default="",
        help="If set and exists, service loop exits.",
    )
    parser.add_argument(
        "--service-stop-key",
        default="",
        help="Optional Windows global key to stop service loop, currently supports: f10",
    )
    parser.add_argument(
        "--service-trigger-key",
        default="f10",
        help="Optional Windows global key to apply one paint plan, currently supports: f10",
    )
    parser.add_argument(
        "--service-trigger-file",
        default="",
        help="If set, service applies one paint plan whenever this file exists.",
    )
    parser.add_argument(
        "--service-apply-on-start",
        action="store_true",
        help="Apply one paint plan immediately when service starts.",
    )
    parser.add_argument(
        "--service-apply-every-frame",
        action="store_true",
        help="Legacy service behavior: apply every service tick.",
    )
    parser.add_argument("--service-stop-on-failure", action="store_true")
    parser.add_argument(
        "--frame-delay-ms",
        type=float,
        default=16.0,
        help="Delay between loop/service frames",
    )
    parser.add_argument("--timeline", default="", help="Optional JSONL path for per-frame timing samples")
    parser.add_argument(
        "--timeline-interval",
        type=float,
        default=1.0,
        help="Minimum seconds between timeline writes",
    )
    parser.add_argument(
        "--loop-continue-on-error",
        action="store_true",
        help="For loop/service: continue even if an apply fails.",
    )
    args = parser.parse_args()
    if args.adapter == "xenos" and not args.bridge_path:
        args.bridge_port = choose_bridge_port(args.bridge_port, args.bridge_host)
    return args


def _make_key_predicate(raw_key: str, option_name: str) -> Callable[[], bool] | None:
    if not raw_key:
        return None
    key_name = raw_key.strip().lower()
    if os.name != "nt":
        _log(f"{option_name} is only supported on Windows. Ignoring.")
        return None
    if key_name != "f10":
        _log(f"{option_name} unsupported: {raw_key}. Supported: f10")
        return None

    vk_code = 0x79

    def is_pressed() -> bool:
        return _async_key_pressed(vk_code)

    return is_pressed


def _async_key_pressed(vk_code: int) -> bool:
    state = ctypes.windll.user32.GetAsyncKeyState(vk_code)
    return bool(state & 0x8000 or state & 0x0001)


def _make_trigger_key(raw_key: str, option_name: str) -> Any | None:
    if not raw_key:
        return None
    key_name = raw_key.strip().lower()
    if os.name != "nt":
        _log(f"{option_name} is only supported on Windows. Ignoring.")
        return None
    if key_name != "f10":
        _log(f"{option_name} unsupported: {raw_key}. Supported: f10")
        return None

    vk_code = 0x79
    registered = _WindowsRegisteredHotkey(vk_code)
    polling = _KeyEdgeTrigger(lambda: _async_key_pressed(vk_code))
    if registered.registered:
        return _CompositeTrigger([registered], "register_hotkey")
    last_error = registered.last_error
    registered.close()
    return _CompositeTrigger([polling], f"async_state(register_hotkey_failed win32={last_error})")


def _same_key(left: str, right: str) -> bool:
    return bool(left and right and left.strip().lower() == right.strip().lower())


def _consume_trigger_file(path: Path | None) -> bool:
    if path is None or not path.exists():
        return False
    try:
        path.unlink()
    except OSError:
        pass
    return True


def _build_config(args: argparse.Namespace, seed_offset: int = 0) -> PlanConfig:
    return PlanConfig(
        viewport_width=args.viewport_width,
        viewport_height=args.viewport_height,
        texture_width=args.texture_width,
        texture_height=args.texture_height,
        sample_count=args.sample_count,
        min_front_hits=max(1, int(args.sample_count * 0.2)),
        target_front_hits=max(1, int(args.sample_count * 0.4)),
        preferred_front_hits=args.sample_count,
        random_seed=args.seed + seed_offset,
        jitter=args.jitter,
        include_side=args.include_side,
        include_back=args.include_back,
    )


def _select_adapter(name: str, args: argparse.Namespace):
    if name == "noop":
        return NoopAdapter(out_dir=args.out_dir)
    if name == "ue4ss":
        return Ue4ssStubAdapter(queue_dir=args.queue_dir)
    return XenosBridgeAdapter(
        bridge_path=args.bridge_path,
        timeout_seconds=args.bridge_timeout_seconds,
        bridge_transport=args.bridge_transport,
        wait_for_response=args.bridge_wait_response,
        max_retries=args.bridge_retries,
        retry_delay_ms=args.bridge_retry_delay_ms,
    )


def _build_color_sampler(args: argparse.Namespace):
    return build_color_sampler_from_args(
        viewport=(args.viewport_width, args.viewport_height),
        image_path=args.input_image,
        fallback_color=args.base_color,
        fallback_roughness=args.roughness,
        fallback_metallic=args.metallic,
    )


def _print_plan_summary(plan) -> None:
    buckets, counts = simulate_paint_distribution(plan)
    readback_ms = estimate_readback_cost_ms(plan.total_samples, plan.config.texture_width * plan.config.texture_height)
    print(
        "summary total=%d front=%d side=%d back=%d readback_ms=%.2f" % (
            counts["front"] + counts["side"] + counts["back"],
            counts["front"],
            counts["side"],
            counts["back"],
            readback_ms,
        )
    )


def _collect_timing_entry(
    frame: int,
    plan,
    timing_ms: dict[str, float],
    result: Any | None = None,
    adapter: str = "",
) -> dict[str, Any]:
    buckets, counts = simulate_paint_distribution(plan)
    return {
        "frame": frame,
    "timestamp_utc": int(time.time()),
        "samples": {
            "total": counts["front"] + counts["side"] + counts["back"],
            "front": counts["front"],
            "side": counts["side"],
            "back": counts["back"],
        },
        "timing_ms": timing_ms,
        "result": {
            "adapter": adapter,
            "requested": result.requested if result else 0,
            "applied": result.applied if result else 0,
            "success": bool(result.success) if result else True,
            "duration_ms": float(result.duration_ms) if result else 0.0,
        },
    }


def _append_timeline(path: Path | None, entry: dict[str, Any], last_emit: float, interval: float) -> float:
    if not path:
        return last_emit
    now = perf_counter()
    if last_emit > 0.0 and interval > 0.0 and now - last_emit < interval:
        return last_emit
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("a", encoding="utf-8") as out:
        out.write(json.dumps(entry, ensure_ascii=False))
        out.write("\n")
    return now


def _generate_plan(args: argparse.Namespace, sampler, frame: int = 0):
    cfg = _build_config(args, seed_offset=frame * max(0, args.seed_step))
    start = perf_counter()
    plan = compose_plan(cfg, sampler)
    generated_ms = (perf_counter() - start) * 1000.0
    readback_ms = estimate_readback_cost_ms(plan.total_samples, cfg.texture_width * cfg.texture_height)
    return plan, generated_ms, readback_ms


def _sample_counts(plan) -> dict[str, int]:
    _, counts = simulate_paint_distribution(plan)
    return {
        "total": counts["front"] + counts["side"] + counts["back"],
        "front": counts["front"],
        "side": counts["side"],
        "back": counts["back"],
    }


def _process_status(process: ProcessInfo | None, target_name: str) -> dict[str, Any]:
    if process is None:
        return {
            "attached": False,
            "target_name": target_name,
            "pid": 0,
            "name": "",
        }
    status = process.to_status()
    status.update({"attached": True, "target_name": target_name})
    return status


def _bridge_status(args: argparse.Namespace, state: str, extra: dict[str, Any] | None = None) -> dict[str, Any]:
    status = {
        "state": state,
        "adapter": args.adapter,
        "host": args.bridge_host,
        "port": args.bridge_port,
        "bridge_path": args.bridge_path or "",
        "transport": args.bridge_transport,
    }
    if extra:
        status.update(extra)
    return status


def _build_full_route_payload(args: argparse.Namespace, plan, process: ProcessInfo | None, run_id: str) -> dict[str, Any]:
    native_apply_mode = getattr(args, "native_apply_mode", "metallic_base_then_front_texture_import_diagnostic")
    if native_apply_mode == "texture_import_diagnostic":
        route = "f10_texture_import_diagnostic"
    elif native_apply_mode == "texture_atlas_paint_api_stream":
        route = "f10_texture_atlas_paint_api_stream"
    elif native_apply_mode == "front_metallic_texture_paint_stream":
        route = "f10_front_metallic_texture_paint_stream"
    elif native_apply_mode == "front_metallic_texture_import_diagnostic":
        route = "f10_front_metallic_texture_import_diagnostic"
    elif native_apply_mode == "cpu_mesh_raycast":
        route = "f10_cpu_mesh_raycast"
    elif native_apply_mode == "cpu_mesh_probe_only":
        route = "f10_cpu_mesh_probe_only"
    elif native_apply_mode == "cpu_mesh_texture_import_diagnostic":
        route = "f10_cpu_mesh_texture_import_diagnostic"
    elif native_apply_mode == "cpu_mesh_texture_paint_stream":
        route = "f10_cpu_mesh_texture_paint_stream"
    elif native_apply_mode == "sdk_deep_probe_only":
        route = "sdk_deep_probe_only"
    else:
        route = "f10_metallic_base_then_front_texture_import_diagnostic"
    return {
        "route": route,
        "native_apply_mode": native_apply_mode,
        "temporary_diagnostic_only": native_apply_mode in {
            "texture_import_diagnostic",
            "front_metallic_texture_import_diagnostic",
            "metallic_base_then_front_texture_import_diagnostic",
            "cpu_mesh_probe_only",
            "cpu_mesh_texture_import_diagnostic",
            "sdk_deep_probe_only",
        },
        "run_id": run_id,
        "process": _process_status(process, args.game_process_name),
        "plan": plan_to_dict(plan),
        "requested": _sample_counts(plan),
    }


def _apply_via_native_bridge(args: argparse.Namespace, plan, process: ProcessInfo | None, run_id: str) -> ApplyResult:
    client = NativeBridgeClient(args.bridge_host, args.bridge_port, args.bridge_timeout_seconds)
    payload = _build_full_route_payload(args, plan, process, run_id)
    try:
        response, elapsed_ms = client.paint_full_route(payload)
    except OSError as exc:
        return ApplyResult(
            adapter="xenos",
            success=False,
            requested=plan.total_samples,
            applied=0,
            failures=1,
            message=f"native bridge connection failed: {exc}",
            duration_ms=0.0,
            timing_ms={"send_ms": 0.0, "response_ms": 0.0},
            metadata={"stage": "bridge_connect", "address": client.address, "win32_error": getattr(exc, "winerror", None)},
        )
    except Exception as exc:  # noqa: BLE001
        return ApplyResult(
            adapter="xenos",
            success=False,
            requested=plan.total_samples,
            applied=0,
            failures=1,
            message=f"native bridge request failed: {exc}",
            duration_ms=0.0,
            timing_ms={"send_ms": 0.0, "response_ms": 0.0},
            metadata={
                "stage": "bridge_exception",
                "address": client.address,
                "exception": type(exc).__name__,
                "traceback": traceback.format_exc(),
            },
        )
    result = ApplyResult(
        adapter="xenos",
        success=response.success,
        requested=plan.total_samples,
        applied=response.applied,
        failures=response.failures,
        message=response.message,
        duration_ms=elapsed_ms,
        timing_ms=response.timing_ms or {"send_ms": elapsed_ms},
        metadata={
            "stage": response.stage,
            "address": client.address,
            "bridge_response": response.to_dict(),
        },
    )
    return result


def _check_native_bridge(args: argparse.Namespace) -> tuple[bool, dict[str, Any]]:
    client = NativeBridgeClient(args.bridge_host, args.bridge_port, args.bridge_timeout_seconds)
    try:
        response, elapsed_ms = client.ping()
    except OSError as exc:
        return False, {
            "state": "not_ready",
            "address": client.address,
            "message": str(exc),
            "win32_error": getattr(exc, "winerror", None),
        }
    return response.success, {
        "state": "ready" if response.success else "not_ready",
        "address": client.address,
        "message": response.message,
        "stage": response.stage,
        "elapsed_ms": elapsed_ms,
        "response": response.to_dict(),
    }


def _sdk_probe_bridge(args: argparse.Namespace) -> tuple[bool, dict[str, Any]]:
    client = NativeBridgeClient(args.bridge_host, args.bridge_port, args.bridge_timeout_seconds)
    try:
        response, elapsed_ms = client.sdk_probe()
    except OSError as exc:
        return False, {
            "state": "not_ready",
            "address": client.address,
            "message": str(exc),
            "win32_error": getattr(exc, "winerror", None),
        }
    return response.success, {
        "state": "ready" if response.success else "failed",
        "address": client.address,
        "message": response.message,
        "stage": response.stage,
        "elapsed_ms": elapsed_ms,
        "response": response.to_dict(),
    }


def _sdk_deep_probe_bridge(args: argparse.Namespace) -> tuple[bool, dict[str, Any]]:
    client = NativeBridgeClient(args.bridge_host, args.bridge_port, args.bridge_timeout_seconds)
    try:
        response, elapsed_ms = client.sdk_deep_probe()
    except OSError as exc:
        return False, {
            "state": "not_ready",
            "address": client.address,
            "message": str(exc),
            "win32_error": getattr(exc, "winerror", None),
        }
    return response.success, {
        "state": "ready" if response.success else "failed",
        "address": client.address,
        "message": response.message,
        "stage": response.stage,
        "elapsed_ms": elapsed_ms,
        "response": response.to_dict(),
    }


def _shutdown_bridge_at(host: str, port: int, timeout_seconds: float) -> tuple[bool, dict[str, Any]]:
    client = NativeBridgeClient(host, port, min(1.0, max(0.1, timeout_seconds)))
    try:
        response, elapsed_ms = client.shutdown()
    except OSError as exc:
        return False, {
            "state": "not_ready",
            "address": client.address,
            "message": str(exc),
            "win32_error": getattr(exc, "winerror", None),
        }
    return response.success, {
        "state": "shutdown" if response.success else "shutdown_failed",
        "address": client.address,
        "message": response.message,
        "stage": response.stage,
        "elapsed_ms": elapsed_ms,
        "response": response.to_dict(),
    }


def _shutdown_previous_bridge(args: argparse.Namespace, diagnostics: RuntimeDiagnostics) -> None:
    if args.adapter != "xenos" or args.bridge_path or not diagnostics.status_path.exists():
        return
    try:
        status = json.loads(diagnostics.status_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return
    bridge = status.get("bridge") if isinstance(status, dict) else None
    if not isinstance(bridge, dict):
        return
    try:
        port = int(bridge.get("port") or 0)
    except (TypeError, ValueError):
        return
    host = str(bridge.get("host") or args.bridge_host)
    if port <= 0 or (host == args.bridge_host and port == args.bridge_port):
        return
    success, details = _shutdown_bridge_at(host, port, args.bridge_timeout_seconds)
    diagnostics.event(
        "previous_bridge_shutdown",
        level="info" if success else "warning",
        stage="shutdown",
        message="previous native bridge shutdown requested" if success else "previous native bridge was not reachable",
        details=details,
    )


def _shutdown_current_bridge(args: argparse.Namespace, diagnostics: RuntimeDiagnostics, reason: str) -> None:
    if args.adapter != "xenos" or args.bridge_path:
        return
    success, details = _shutdown_bridge_at(args.bridge_host, args.bridge_port, args.bridge_timeout_seconds)
    diagnostics.event(
        "bridge_shutdown",
        level="info" if success else "warning",
        stage="shutdown",
        message=f"native bridge shutdown requested reason={reason}" if success else f"native bridge shutdown skipped reason={reason}",
        details=details,
    )


def _resolve_loop_frames(args: argparse.Namespace) -> int:
    if args.loop_frames > 0:
        return max(1, args.loop_frames)
    if args.loop_duration_seconds <= 0:
        raise ValueError("loop-duration-seconds must be positive when loop-frames is 0")
    frame_delay = max(1.0, args.frame_delay_ms)
    return max(1, int((args.loop_duration_seconds * 1000.0) / frame_delay))


def _run_apply(
    plan,
    adapter,
    timing_prefix: dict[str, float],
    timeline_path: Path | None,
    frame: int = 0,
    print_summary: bool = False,
    write_timeline: bool = False,
):
    try:
        result = adapter.apply(plan)
    except Exception as exc:  # noqa: BLE001
        result = ApplyResult(
            adapter=getattr(adapter, "name", "unknown"),
            success=False,
            requested=plan.total_samples,
            applied=0,
            failures=1,
            message=f"adapter apply raised: {exc}",
            duration_ms=0.0,
            timing_ms={"apply_ms": 0.0},
            metadata={
                "stage": "adapter_exception",
                "exception": type(exc).__name__,
                "traceback": traceback.format_exc(),
            },
        )
    timing = dict(timing_prefix)
    for key, value in (result.timing_ms or {}).items():
        timing[key] = float(value)
    if "apply_ms" not in timing:
        timing["apply_ms"] = float(result.duration_ms)
    timing["total_ms"] = sum(v for v in timing.values() if isinstance(v, (int, float)))
    if print_summary:
        print(f"{result.summary}")
        print(f"result_ms={timing.get('total_ms', result.duration_ms):.2f}")
        if timing:
            timing_string = ", ".join(f"{key}={value:.2f}" for key, value in timing.items())
            print(f"{timing_string}")
    if write_timeline:
        _append_timeline(
            timeline_path,
            _collect_timing_entry(
                frame=frame,
                plan=plan,
                timing_ms=timing,
                result=result,
                adapter=adapter.name,
            ),
            0.0,
            0.0,
        )
    return result, timing


def _apply_via_native_bridge_with_wait_logs(
    args: argparse.Namespace,
    plan,
    process: ProcessInfo,
    run_id: str,
    diagnostics: RuntimeDiagnostics,
    run_start: float,
):
    wait_interval = 5.0
    progress_path = bridge_progress_file(native_assets(args.native_root))
    last_progress_signature = ""
    with ThreadPoolExecutor(max_workers=1, thread_name_prefix="meccha-native-paint") as executor:
        future = executor.submit(_apply_via_native_bridge, args, plan, process, run_id)
        while True:
            try:
                return future.result(timeout=wait_interval)
            except FutureTimeoutError:
                elapsed_ms = (perf_counter() - run_start) * 1000.0
                progress: dict[str, Any] = {}
                try:
                    if progress_path.exists():
                        loaded = json.loads(progress_path.read_text(encoding="utf-8"))
                        if isinstance(loaded, dict):
                            progress = loaded
                except (OSError, json.JSONDecodeError):
                    progress = {}
                progress_stage = str(progress.get("stage") or "native_call")
                progress_ratio = float(progress.get("progress") or 0.0)
                eta_ms = float(progress.get("eta_ms") or 0.0)
                signature = f"{progress_stage}:{int(progress_ratio * 100)}"
                message = (
                    f"{progress_stage} progress={progress_ratio * 100.0:.0f}% "
                    f"elapsed={elapsed_ms / 1000.0:.1f}s"
                )
                if eta_ms > 0:
                    message += f" eta={eta_ms / 1000.0:.1f}s"
                diagnostics.merge_status(
                    last_run={
                        "run_id": run_id,
                        "stage": progress_stage,
                        "success": False,
                        "elapsed_ms": elapsed_ms,
                        "progress": progress,
                        "message": message,
                    }
                )
                if signature != last_progress_signature:
                    details = {
                        "elapsed_ms": elapsed_ms,
                        "timeout_seconds": args.bridge_timeout_seconds,
                        "native_apply_mode": getattr(args, "native_apply_mode", ""),
                    }
                    details.update(progress)
                    diagnostics.event(
                        "paint_progress",
                        stage=progress_stage,
                        run_id=run_id,
                        message=message,
                        details=details,
                    )
                    last_progress_signature = signature


def _maybe_print_generate_summary(plan, generated_ms: float, readback_ms: float) -> None:
    buckets, counts = simulate_paint_distribution(plan)
    print(
        "generated total=%d front=%d side=%d back=%d plan_ms=%.2f readback_ms=%.2f" % (
            counts["front"] + counts["side"] + counts["back"],
            counts["front"],
            counts["side"],
            counts["back"],
            generated_ms,
            readback_ms,
        )
    )


def _run_loop(
    args: argparse.Namespace,
    sampler,
    adapter,
    input_plan,
    timing_path: Path | None,
) -> int:
    frames = _resolve_loop_frames(args)
    timeline_last_emit = 0.0
    for frame in range(frames):
        if not _StopState.running:
            return 0

        if input_plan is None:
            plan, gen_ms, readback_ms = _generate_plan(args, sampler, frame=frame)
            timing = {"plan_ms": gen_ms, "readback_estimate_ms": readback_ms}
            if frame == 0 and args.print_summary:
                _maybe_print_generate_summary(plan, gen_ms, readback_ms)
        else:
            plan = input_plan
            timing = {"plan_ms": 0.0, "readback_estimate_ms": estimate_readback_cost_ms(plan.total_samples, plan.config.texture_width * plan.config.texture_height)}

        if args.print_summary and frame % 10 == 0:
            print(f"frame={frame}")

        result, timing_result = _run_apply(
            plan=plan,
            adapter=adapter,
            timing_prefix=timing,
            timeline_path=timing_path,
            frame=frame,
            print_summary=False,
            write_timeline=False,
        )
        timeline_last_emit = _append_timeline(
            timing_path,
            _collect_timing_entry(frame, plan, timing_result, result, adapter.name),
            timeline_last_emit,
            args.timeline_interval,
        )

        if not result.success:
            if result.message:
                _log(result.message)
            if not args.loop_continue_on_error:
                return 1

        if args.frame_delay_ms > 0:
            sleep(args.frame_delay_ms / 1000.0)

    return 0


def _run_service(
    args: argparse.Namespace,
    sampler,
    adapter,
    input_plan,
    timeline_path: Path | None,
    diagnostics: RuntimeDiagnostics,
) -> int:
    if _same_key(args.service_stop_key, args.service_trigger_key):
        args.service_stop_key = ""
        diagnostics.event(
            "stop_key_disabled",
            level="warning",
            stage="startup",
            message="service stop key matched trigger key; disabling stop key so F10 cannot terminate the runtime",
            details={"trigger_key": args.service_trigger_key},
    )
    stop_key = _make_key_predicate(args.service_stop_key, "--service-stop-key")
    trigger_key = _make_trigger_key(args.service_trigger_key, "--service-trigger-key")
    trigger_backend = getattr(trigger_key, "backend", "disabled" if not args.service_trigger_key else "unavailable")
    trigger_file = Path(args.service_trigger_file) if args.service_trigger_file else None
    apply_every_frame = bool(args.service_apply_every_frame)
    apply_on_start = bool(args.service_apply_on_start)
    trigger_requested = bool(args.service_trigger_key) or trigger_file is not None
    if not apply_every_frame and not trigger_requested and not apply_on_start:
        apply_every_frame = True

    native_root = Path(args.native_root) if args.native_root else None
    assets = native_assets(native_root)
    diagnostics.merge_status(
        process=_process_status(None, args.game_process_name),
        bridge=_bridge_status(args, "starting", {"native_assets": assets.to_status()}),
        hotkey={
            "trigger_key": args.service_trigger_key,
            "trigger_backend": trigger_backend,
            "stop_key": args.service_stop_key,
            "trigger_file": str(trigger_file) if trigger_file else "",
        },
    )
    if not assets.injector.exists() or not assets.bridge_dll.exists():
        diagnostics.event(
            "native_assets_missing",
            level="warning",
            stage="startup",
            message="native injector or bridge DLL is not built; bridge must already be running or paint will fail",
            details=assets.to_status(),
        )
    diagnostics.event(
        "hotkey_ready",
        stage="startup",
        message=f"hotkey backend={trigger_backend}",
        details={
            "trigger_key": args.service_trigger_key,
            "trigger_backend": trigger_backend,
            "stop_key": args.service_stop_key,
        },
    )

    if args.service_max_duration_seconds > 0:
        deadline = perf_counter() + args.service_max_duration_seconds
    else:
        deadline = 0.0

    stop_file = Path(args.service_stop_file) if args.service_stop_file else None
    diagnostics.event(
        "runtime_start",
        stage="startup",
        message="service started",
        details={
            "version": __version__,
            "pid": os.getpid(),
            "log_dir": str(diagnostics.log_dir),
            "game_process_name": args.game_process_name,
            "adapter": args.adapter,
            "bridge_state": "starting",
            "bridge_host": args.bridge_host,
            "bridge_port": args.bridge_port,
            "bridge_path": args.bridge_path or "",
            "trigger_key": args.service_trigger_key,
            "hotkey_backend": trigger_backend,
            "trigger_file": str(trigger_file) if trigger_file else "",
            "apply_every_frame": apply_every_frame,
        },
    )
    frame = 0
    timeline_last_emit = 0.0
    last_heartbeat = perf_counter()
    last_process_log = 0.0
    last_bridge_check = 0.0
    heartbeat_interval_sec = max(0.5, args.status_interval_seconds)
    process_poll_seconds = max(0.1, args.process_poll_seconds)
    apply_count = 0
    attached_process: ProcessInfo | None = None
    bridge_ready = False
    inject_attempted_for_pid = 0
    sdk_probe_ready_for_pid = 0
    sdk_deep_probe_ready_for_pid = 0
    last_sdk_probe_attempt = 0.0
    last_sdk_deep_probe_attempt = 0.0
    last_bridge_log_state = ""
    last_bridge_log_time = 0.0
    signal.signal(signal.SIGINT, _stop_signal)
    signal.signal(signal.SIGTERM, _stop_signal)

    while _StopState.running:
        if stop_file is not None and stop_file.exists():
            diagnostics.event("service_stop", stage="shutdown", message="stop-file detected")
            return 0
        if stop_key is not None and stop_key():
            diagnostics.event("service_stop", stage="shutdown", message="stop-key detected")
            return 0
        if args.service_max_frames > 0 and frame >= args.service_max_frames:
            diagnostics.event("service_stop", stage="shutdown", message="service max frames reached")
            return 0
        if deadline > 0.0 and perf_counter() >= deadline:
            diagnostics.event("service_stop", stage="shutdown", message="service max duration reached")
            return 0

        now = perf_counter()
        process = find_process_by_name(args.game_process_name)
        if process is None:
            attached_process = None
            diagnostics.merge_status(process=_process_status(None, args.game_process_name))
            if now - last_process_log >= heartbeat_interval_sec:
                diagnostics.event(
                    "waiting_for_process",
                    stage="process_wait",
                    message=f"waiting for {args.game_process_name}",
                    details={"game_process_name": args.game_process_name, "poll_seconds": process_poll_seconds},
                )
                last_process_log = now
            sleep(process_poll_seconds)
            frame += 1
            continue

        if attached_process is None or attached_process.pid != process.pid:
            attached_process = process
            inject_attempted_for_pid = 0
            sdk_probe_ready_for_pid = 0
            sdk_deep_probe_ready_for_pid = 0
            last_sdk_probe_attempt = 0.0
            last_sdk_deep_probe_attempt = 0.0
            last_bridge_log_state = ""
            last_bridge_log_time = 0.0
            diagnostics.merge_status(process=_process_status(process, args.game_process_name))
            diagnostics.event(
                "process_attached",
                stage="process",
                message=f"attached to {process.name}",
                details=process.to_status(),
            )

        if args.adapter == "xenos" and not args.bridge_path and now - last_bridge_check >= heartbeat_interval_sec:
            bridge_ready, bridge_details = _check_native_bridge(args)
            diagnostics.merge_status(bridge=_bridge_status(args, "ready" if bridge_ready else "not_ready", bridge_details))
            bridge_log_state = "ready" if bridge_ready else "not_ready"
            should_log_bridge = (
                bridge_log_state != last_bridge_log_state
                or (not bridge_ready and now - last_bridge_log_time >= 30.0)
            )
            if should_log_bridge:
                diagnostics.event(
                    "bridge_ready" if bridge_ready else "bridge_waiting",
                    level="info" if bridge_ready else "warning",
                    stage="bridge",
                    message=bridge_details.get("message", ""),
                    details=bridge_details,
                )
                last_bridge_log_state = bridge_log_state
                last_bridge_log_time = now
            if not bridge_ready and inject_attempted_for_pid != process.pid:
                diagnostics.event(
                    "inject_started",
                    stage="inject",
                    message="attempting native bridge injection",
                    details={"process": process.to_status(), "native_assets": assets.to_status()},
                )
                injected, inject_details = inject_bridge(args.game_process_name, assets, args.bridge_timeout_seconds, bridge_port=args.bridge_port)
                inject_attempted_for_pid = process.pid
                diagnostics.event(
                    "inject_done" if injected else "inject_failed",
                    level="info" if injected else "error",
                    stage="inject",
                    message="native bridge injection completed" if injected else "native bridge injection failed",
                    details=inject_details,
                )
                if injected:
                    bridge_ready, bridge_details = _check_native_bridge(args)
                    diagnostics.merge_status(bridge=_bridge_status(args, "ready" if bridge_ready else "not_ready", bridge_details))
            if bridge_ready and sdk_probe_ready_for_pid != process.pid and now - last_sdk_probe_attempt >= 10.0:
                last_sdk_probe_attempt = now
                sdk_probe_ready, sdk_probe_details = _sdk_probe_bridge(args)
                bridge_details = {**bridge_details, "sdk_probe": sdk_probe_details}
                diagnostics.merge_status(bridge=_bridge_status(args, "ready" if bridge_ready else "not_ready", bridge_details))
                sdk_probe_stage = str(sdk_probe_details.get("stage", "sdk_probe"))
                sdk_probe_context_pending = sdk_probe_stage in {"world_unavailable", "local_pawn_unavailable", "paint_component_unavailable", "sdk_context_unavailable"}
                diagnostics.event(
                    "sdk_probe" if sdk_probe_ready else "sdk_probe_failed",
                    level="info" if sdk_probe_ready else ("warning" if sdk_probe_context_pending else "error"),
                    stage=sdk_probe_stage,
                    message=sdk_probe_details.get("message", ""),
                    details={**sdk_probe_details, "will_retry": not sdk_probe_ready},
                )
                if sdk_probe_ready:
                    sdk_probe_ready_for_pid = process.pid
            if bridge_ready and sdk_probe_ready_for_pid == process.pid and sdk_deep_probe_ready_for_pid != process.pid and now - last_sdk_deep_probe_attempt >= 10.0:
                    last_sdk_deep_probe_attempt = now
                    deep_probe_ready, deep_probe_details = _sdk_deep_probe_bridge(args)
                    bridge_details = {**bridge_details, "sdk_deep_probe": deep_probe_details}
                    diagnostics.merge_status(bridge=_bridge_status(args, "ready" if bridge_ready else "not_ready", bridge_details))
                    diagnostics.event(
                        "sdk_deep_probe" if deep_probe_ready else "sdk_deep_probe_failed",
                        level="info" if deep_probe_ready else "warning",
                        stage=deep_probe_details.get("stage", "sdk_deep_probe"),
                        message=deep_probe_details.get("message", ""),
                        details={**deep_probe_details, "will_retry": not deep_probe_ready},
                    )
                    if deep_probe_ready:
                        sdk_deep_probe_ready_for_pid = process.pid
            last_bridge_check = now

        should_apply = apply_every_frame
        trigger_source = "every_frame" if apply_every_frame else ""
        if apply_on_start:
            should_apply = True
            apply_on_start = False
            trigger_source = "on_start"
        if trigger_key is not None and trigger_key.consume():
            diagnostics.event("f10_triggered", stage="hotkey", message="F10 trigger detected")
            should_apply = True
            trigger_source = "f10"
        if _consume_trigger_file(trigger_file):
            diagnostics.event(
                "trigger_file_detected",
                stage="hotkey",
                message="trigger-file detected",
                details={"trigger_file": str(trigger_file)},
            )
            should_apply = True
            trigger_source = "trigger_file"

        if not should_apply:
            now = perf_counter()
            if now - last_heartbeat >= max(60.0, heartbeat_interval_sec * 15.0):
                diagnostics.event(
                    "service_idle",
                    stage="idle",
                    message="service idle",
                    details={"frame": frame, "applies": apply_count, "bridge_ready": bridge_ready},
                )
                last_heartbeat = now
            if args.frame_delay_ms > 0:
                sleep(args.frame_delay_ms / 1000.0)
            frame += 1
            continue

        if (
            getattr(args, "adapter", "xenos") == "xenos"
            and process is not None
            and getattr(args, "native_apply_mode", "") != "sdk_deep_probe_only"
            and (
                sdk_probe_ready_for_pid != process.pid
                or sdk_deep_probe_ready_for_pid != process.pid
            )
        ):
            details = {
                "trigger_source": trigger_source,
                "pid": process.pid,
                "sdk_probe_ready": sdk_probe_ready_for_pid == process.pid,
                "sdk_deep_probe_ready": sdk_deep_probe_ready_for_pid == process.pid,
                "will_retry": True,
            }
            diagnostics.event(
                "paint_ignored_sdk_not_ready",
                level="warning",
                stage="sdk_context_pending",
                message="paint trigger ignored until sdk_probe and sdk_deep_probe complete",
                details=details,
            )
            diagnostics.merge_status(
                last_run={
                    "stage": "paint_ignored_sdk_not_ready",
                    "success": False,
                    "trigger_source": trigger_source,
                    "process": _process_status(process, args.game_process_name),
                    **details,
                }
            )
            if args.frame_delay_ms > 0:
                sleep(args.frame_delay_ms / 1000.0)
            frame += 1
            continue

        run_id = uuid4().hex
        run_start = perf_counter()
        diagnostics.merge_status(
            last_run={
                "run_id": run_id,
                "stage": "triggered",
                "success": False,
                "trigger_source": trigger_source,
                "process": _process_status(process, args.game_process_name),
            }
        )
        if input_plan is None:
            plan, gen_ms, readback_ms = _generate_plan(args, sampler, frame=frame)
            timing = {"plan_ms": gen_ms, "readback_estimate_ms": readback_ms}
        else:
            plan = input_plan
            timing = {
                "plan_ms": 0.0,
                "readback_estimate_ms": estimate_readback_cost_ms(
                    plan.total_samples,
                    plan.config.texture_width * plan.config.texture_height,
                ),
            }

        diagnostics.event(
            "plan_generated",
            stage="plan",
            run_id=run_id,
            message="paint plan generated",
            details={**_sample_counts(plan), **timing},
        )
        diagnostics.merge_status(last_run={"run_id": run_id, "stage": "paint_started", "success": False})
        diagnostics.event(
            "paint_started",
            stage="paint",
            run_id=run_id,
            message="paint_full_route started",
            details={"adapter": args.adapter, "trigger_source": trigger_source},
        )

        if args.adapter == "xenos" and not args.bridge_path:
            result = _apply_via_native_bridge_with_wait_logs(args, plan, process, run_id, diagnostics, run_start)
            timing_result = dict(timing)
            timing_result.update(result.timing_ms or {})
            timing_result["total_ms"] = (perf_counter() - run_start) * 1000.0
        else:
            result, timing_result = _run_apply(
                plan=plan,
                adapter=adapter,
                timing_prefix=timing,
                timeline_path=timeline_path,
                frame=frame,
                print_summary=False,
                write_timeline=False,
            )
        timeline_last_emit = _append_timeline(
            timeline_path,
            _collect_timing_entry(frame, plan, timing_result, result, adapter.name),
            timeline_last_emit,
            args.timeline_interval,
        )
        bridge_response = result.metadata.get("bridge_response") if isinstance(result.metadata, dict) else None
        bridge_metadata = bridge_response.get("metadata") if isinstance(bridge_response, dict) else None
        bridge_events = bridge_metadata.get("bridge_events") if isinstance(bridge_metadata, dict) else None
        if isinstance(bridge_events, list):
            for bridge_event in bridge_events:
                if not isinstance(bridge_event, str) or bridge_event in {"paint_done", "paint_failed"}:
                    continue
                diagnostics.event(
                    bridge_event,
                    stage=bridge_event,
                    run_id=run_id,
                    message=bridge_event,
                    details={
                        "elapsed_ms": bridge_metadata.get("elapsed_ms") if isinstance(bridge_metadata, dict) else None,
                        "front_hits": bridge_metadata.get("front_hits") if isinstance(bridge_metadata, dict) else None,
                        "side_back_hits": bridge_metadata.get("side_back_hits") if isinstance(bridge_metadata, dict) else None,
                        "stroke_count": bridge_metadata.get("stroke_count") if isinstance(bridge_metadata, dict) else None,
                        "stroke_cap": bridge_metadata.get("stroke_cap") if isinstance(bridge_metadata, dict) else None,
                        "server_sent": bridge_metadata.get("server_sent") if isinstance(bridge_metadata, dict) else None,
                        "server_failed": bridge_metadata.get("server_failed") if isinstance(bridge_metadata, dict) else None,
                        "timing_ms": timing_result,
                    },
                )

        if not result.success:
            diagnostics.merge_status(
                last_run={
                    "run_id": run_id,
                    "stage": result.metadata.get("stage", "paint_failed"),
                    "success": False,
                    "requested": result.requested,
                    "applied": result.applied,
                    "failures": result.failures,
                    "message": result.message,
                    "timing_ms": timing_result,
                }
            )
            diagnostics.record_error(
                stage=str(result.metadata.get("stage", "paint_failed")),
                message=result.message or "paint failed",
                run_id=run_id,
                details={
                    "adapter": result.adapter,
                    "requested": result.requested,
                    "applied": result.applied,
                    "failures": result.failures,
                    "metadata": result.metadata,
                    "timing_ms": timing_result,
                },
            )
            diagnostics.event(
                "paint_failed",
                level="error",
                stage=str(result.metadata.get("stage", "paint_failed")),
                run_id=run_id,
                message=result.message or "paint failed",
                details={"metadata": result.metadata, "timing_ms": timing_result},
            )
            if args.service_stop_on_failure:
                return 1
        else:
            apply_count += 1
            diagnostics.merge_status(
                last_run={
                    "run_id": run_id,
                    "stage": "paint_done",
                    "success": True,
                    "requested": result.requested,
                    "applied": result.applied,
                    "failures": result.failures,
                    "message": result.message,
                    "metadata": result.metadata,
                    "timing_ms": timing_result,
                },
                last_error=None,
            )
            diagnostics.event(
                "paint_done",
                stage="paint_done",
                run_id=run_id,
                message=result.message or "paint done",
                details={
                    "requested": result.requested,
                    "applied": result.applied,
                    "failures": result.failures,
                    "metadata": result.metadata,
                    "timing_ms": timing_result,
                },
            )

        now = perf_counter()
        if now - last_heartbeat >= heartbeat_interval_sec:
            diagnostics.event(
                "service_heartbeat",
                stage="service",
                message="service heartbeat",
                details={
                    "frame": frame,
                    "requested": result.requested,
                    "applied": result.applied,
                    "applies": apply_count,
                    "total_ms": timing_result.get("total_ms", 0.0),
                },
            )
            last_heartbeat = now

        if args.frame_delay_ms > 0:
            sleep(args.frame_delay_ms / 1000.0)
        frame += 1

    return 0


def main() -> int:
    args = parse_args()
    if args.quick and args.loop_frames == 0:
        args.loop_frames = 5
        args.loop_duration_seconds = min(args.loop_duration_seconds, 1.0)
    diagnostics = RuntimeDiagnostics(args.log_dir or None)
    _shutdown_previous_bridge(args, diagnostics)

    sampler = _build_color_sampler(args)
    timeline_path = Path(args.timeline) if args.timeline else None
    input_plan = read_plan_json(args.input_plan) if args.input_plan else None

    if args.mode == "generate":
        if args.dry_run:
            return 0
        if input_plan is None:
            plan, gen_ms, readback_ms = _generate_plan(args, sampler, frame=0)
            out_plan = Path(args.out_plan)
            write_plan_json(plan, out_plan)
            if args.print_summary:
                _maybe_print_generate_summary(plan, gen_ms, readback_ms)
            return 0

        if args.print_summary:
            _print_plan_summary(input_plan)
        out_plan = Path(args.out_plan)
        write_plan_json(input_plan, out_plan)
        return 0

    if args.dry_run:
        _log("dry-run complete")
        return 0

    if args.mode == "apply":
        plan = input_plan
        timing: dict[str, float] = {}
        if plan is None:
            plan, gen_ms, readback_ms = _generate_plan(args, sampler, frame=0)
            write_plan_json(plan, args.out_plan)
            timing = {"plan_ms": gen_ms, "readback_estimate_ms": readback_ms}
            if args.print_summary:
                _maybe_print_generate_summary(plan, gen_ms, readback_ms)
        else:
            timing = {
                "plan_ms": 0.0,
                "readback_estimate_ms": estimate_readback_cost_ms(
                    plan.total_samples,
                    plan.config.texture_width * plan.config.texture_height,
                ),
            }
            if args.print_summary:
                _print_plan_summary(plan)

        adapter = _select_adapter(args.adapter, args)
        result, _ = _run_apply(
            plan=plan,
            adapter=adapter,
            timing_prefix=timing,
            timeline_path=timeline_path,
            frame=0,
            print_summary=args.print_summary,
            write_timeline=True,
        )
        if not result.success:
            if result.message:
                _log(result.message)
            return 1
        return 0

    adapter = _select_adapter(args.adapter, args)

    if args.mode == "loop":
        return _run_loop(args, sampler, adapter, input_plan, timeline_path)
    if args.mode == "service":
        try:
            return _run_service(args, sampler, adapter, input_plan, timeline_path, diagnostics)
        finally:
            _shutdown_current_bridge(args, diagnostics, "service_exit")

    return 0
