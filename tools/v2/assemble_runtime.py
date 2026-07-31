#!/usr/bin/env python3
"""Assemble the exact trusted MecchaCamouflage v2 runtime payload tree."""

from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import stat
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable

from verify_fallback_glyph_atlas import (
    GlyphAtlasVerificationError,
    verify as verify_fallback_glyph_atlas,
)


class RuntimeAssemblyError(ValueError):
    pass


@dataclass
class RuntimeAssemblyInputs:
    project_root: Path
    ue4ss_binary: Path
    proxy_binary: Path
    mod_binary: Path
    ue4ss_settings: Path
    member_variable_layout: Path
    ue4ss_license: Path
    dependency_notices: Path
    output_root: Path
    layout_output: Path


_PROFILE_NAMES = {
    "paintman.image-profile-v2.json",
    "paintman.mesh-profile-v2.json",
    "paintman_cube.image-profile-v2.json",
    "paintman_cube.mesh-profile-v2.json",
    "paintman_hukuyoka.image-profile-v2.json",
    "paintman_hukuyoka.mesh-profile-v2.json",
}
_FONT_NAMES = {
    "fallback-glyph-atlas.json",
    "fallback-glyph-atlas.png",
}
_SETTING_OVERRIDES = {
    "EnableHotReloadSystem": "0",
    "EnableAutoReloadingLuaMods": "0",
    "UseCache": "1",
    "bUseUObjectArrayCache": "false",
    "ConsoleEnabled": "0",
    "GuiConsoleEnabled": "0",
    "GuiConsoleVisible": "0",
    "EnableDumping": "0",
    "FullMemoryDump": "0",
}
_GENERATED_PATHS = ["UE4SS.log", "cache"]


def _is_link_or_reparse(path: Path) -> bool:
    try:
        metadata = path.lstat()
    except OSError as error:
        raise RuntimeAssemblyError(
            f"Runtime assembly input could not be inspected: {path}"
        ) from error
    return (
        path.is_symlink()
        or (
            hasattr(path, "is_junction")
            and path.is_junction()
        )
        or bool(
            getattr(metadata, "st_file_attributes", 0)
            & getattr(stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0)
        )
    )


def _require_plain_file(path: Path, *, allow_empty: bool = False) -> None:
    if _is_link_or_reparse(path):
        raise RuntimeAssemblyError(
            f"Runtime assembly input is a link or reparse point: {path}"
        )
    try:
        metadata = path.stat()
    except OSError as error:
        raise RuntimeAssemblyError(
            f"Runtime assembly input could not be measured: {path}"
        ) from error
    if not stat.S_ISREG(metadata.st_mode):
        raise RuntimeAssemblyError(
            f"Runtime assembly input is not a regular file: {path}"
        )
    if not allow_empty and metadata.st_size == 0:
        raise RuntimeAssemblyError(
            f"Runtime assembly input is empty: {path}"
        )


def _directory_files(
    directory: Path,
    expected_names: set[str],
) -> dict[str, Path]:
    if _is_link_or_reparse(directory) or not directory.is_dir():
        raise RuntimeAssemblyError(
            f"Runtime resource directory is invalid: {directory}"
        )
    try:
        entries = list(directory.iterdir())
    except OSError as error:
        raise RuntimeAssemblyError(
            f"Runtime resource directory could not be read: {directory}"
        ) from error
    actual_names = {entry.name for entry in entries}
    if actual_names != expected_names:
        missing = sorted(expected_names - actual_names)
        extra = sorted(actual_names - expected_names)
        raise RuntimeAssemblyError(
            "Runtime resource inventory changed: "
            f"missing={missing}, extra={extra}"
        )
    result: dict[str, Path] = {}
    for entry in entries:
        _require_plain_file(entry)
        result[entry.name] = entry
    return result


def _paths_overlap(left: Path, right: Path) -> bool:
    left = left.resolve(strict=False)
    right = right.resolve(strict=False)
    return left == right or left in right.parents or right in left.parents


def _validate_destinations(inputs: RuntimeAssemblyInputs) -> None:
    output_root = inputs.output_root.resolve(strict=False)
    layout_output = inputs.layout_output.resolve(strict=False)
    if inputs.output_root.exists():
        raise RuntimeAssemblyError(
            f"Runtime payload output already exists: {inputs.output_root}"
        )
    if inputs.layout_output.exists():
        raise RuntimeAssemblyError(
            f"Runtime layout output already exists: {inputs.layout_output}"
        )
    if _paths_overlap(output_root, layout_output):
        raise RuntimeAssemblyError(
            "Runtime payload and layout outputs overlap."
        )
    if output_root == inputs.project_root.resolve(strict=False):
        raise RuntimeAssemblyError(
            "Runtime payload output cannot replace the project root."
        )
    for source in (
        inputs.ue4ss_binary,
        inputs.proxy_binary,
        inputs.mod_binary,
        inputs.ue4ss_settings,
        inputs.member_variable_layout,
        inputs.ue4ss_license,
        inputs.dependency_notices,
    ):
        if _paths_overlap(output_root, source) or output_root == source:
            raise RuntimeAssemblyError(
                "Runtime payload output overlaps an input."
            )


def _release_settings(source: Path) -> bytes:
    _require_plain_file(source)
    try:
        text = source.read_text(encoding="utf-8")
    except (OSError, UnicodeDecodeError) as error:
        raise RuntimeAssemblyError(
            "UE4SS settings are not readable strict UTF-8."
        ) from error
    for key, value in _SETTING_OVERRIDES.items():
        pattern = re.compile(
            rf"(?m)^[ \t]*{re.escape(key)}[ \t]*=.*$"
        )
        matches = pattern.findall(text)
        if len(matches) != 1:
            raise RuntimeAssemblyError(
                f"UE4SS release setting {key!r} has "
                f"{len(matches)} definitions; expected one."
            )
        text = pattern.sub(f"{key} = {value}", text, count=1)
    return text.encode("utf-8")


def _write_file(
    root: Path,
    relative_path: str,
    source: Path | bytes,
    role: str,
    inventory: list[dict[str, str]],
) -> None:
    destination = root / Path(relative_path)
    destination.parent.mkdir(parents=True, exist_ok=True)
    if isinstance(source, bytes):
        destination.write_bytes(source)
    else:
        _require_plain_file(source)
        shutil.copyfile(source, destination)
    inventory.append({"path": relative_path, "role": role})


def _assemble_staging(
    inputs: RuntimeAssemblyInputs,
    staging_root: Path,
) -> dict[str, object]:
    resources = inputs.project_root / "resources"
    localization = _directory_files(
        resources / "localization",
        {"catalog.json"},
    )
    profiles = _directory_files(
        resources / "mesh-profiles",
        _PROFILE_NAMES,
    )
    fonts = _directory_files(
        resources / "fonts/fallback",
        _FONT_NAMES | {"Noto-CJK-OFL.txt"},
    )
    try:
        verify_fallback_glyph_atlas(
            catalog_path=localization["catalog.json"],
            atlas_directory=resources / "fonts/fallback",
        )
    except GlyphAtlasVerificationError as error:
        raise RuntimeAssemblyError(
            f"Fallback glyph atlas validation failed: {error}"
        ) from error
    _require_plain_file(inputs.project_root / "LICENSE.txt")
    _require_plain_file(
        resources / "licenses/libwebp-COPYING.txt"
    )
    _require_plain_file(inputs.ue4ss_license)
    _require_plain_file(inputs.member_variable_layout)
    _require_plain_file(inputs.dependency_notices)

    inventory: list[dict[str, str]] = []
    _write_file(
        staging_root,
        "dwmapi.dll",
        inputs.proxy_binary,
        "proxy",
        inventory,
    )
    _write_file(
        staging_root,
        "UE4SS.dll",
        inputs.ue4ss_binary,
        "runtime",
        inventory,
    )
    _write_file(
        staging_root,
        "UE4SS-settings.ini",
        _release_settings(inputs.ue4ss_settings),
        "config",
        inventory,
    )
    _write_file(
        staging_root,
        "MemberVariableLayout.ini",
        inputs.member_variable_layout,
        "config",
        inventory,
    )
    _write_file(
        staging_root,
        "Mods/MecchaCamouflage/dlls/main.dll",
        inputs.mod_binary,
        "mod",
        inventory,
    )
    _write_file(
        staging_root,
        "Mods/MecchaCamouflage/enabled.txt",
        b"",
        "config",
        inventory,
    )
    _write_file(
        staging_root,
        "Mods/MecchaCamouflage/resources/localization/catalog.json",
        localization["catalog.json"],
        "localization",
        inventory,
    )
    for name, source in sorted(profiles.items()):
        _write_file(
            staging_root,
            f"Mods/MecchaCamouflage/resources/mesh-profiles/{name}",
            source,
            "profile",
            inventory,
        )
    for name, source in sorted(fonts.items()):
        if name == "Noto-CJK-OFL.txt":
            continue
        _write_file(
            staging_root,
            f"Mods/MecchaCamouflage/resources/fonts/{name}",
            source,
            "font",
            inventory,
        )
    for relative_path, source in (
        (
            "Licenses/MecchaCamouflage-LICENSE.txt",
            inputs.project_root / "LICENSE.txt",
        ),
        (
            "Licenses/UE4SS-LICENSE.txt",
            inputs.ue4ss_license,
        ),
        (
            "Licenses/libwebp-COPYING.txt",
            resources / "licenses/libwebp-COPYING.txt",
        ),
        (
            "Licenses/Noto-CJK-OFL.txt",
            fonts["Noto-CJK-OFL.txt"],
        ),
        (
            "Licenses/THIRD-PARTY-NOTICES.txt",
            inputs.dependency_notices,
        ),
    ):
        _write_file(
            staging_root,
            relative_path,
            source,
            "license",
            inventory,
        )

    inventory.sort(key=lambda item: item["path"].lower())
    return {
        "schema_version": 1,
        "generated_paths": list(_GENERATED_PATHS),
        "files": inventory,
    }


def assemble_runtime(
    inputs: RuntimeAssemblyInputs,
) -> dict[str, object]:
    inputs.project_root = Path(os.path.abspath(inputs.project_root))
    inputs.ue4ss_binary = Path(os.path.abspath(inputs.ue4ss_binary))
    inputs.proxy_binary = Path(os.path.abspath(inputs.proxy_binary))
    inputs.mod_binary = Path(os.path.abspath(inputs.mod_binary))
    inputs.ue4ss_settings = Path(
        os.path.abspath(inputs.ue4ss_settings)
    )
    inputs.member_variable_layout = (
        Path(os.path.abspath(inputs.member_variable_layout))
    )
    inputs.ue4ss_license = Path(
        os.path.abspath(inputs.ue4ss_license)
    )
    inputs.dependency_notices = (
        Path(os.path.abspath(inputs.dependency_notices))
    )
    inputs.output_root = Path(os.path.abspath(inputs.output_root))
    inputs.layout_output = Path(os.path.abspath(inputs.layout_output))
    _validate_destinations(inputs)

    inputs.output_root.parent.mkdir(parents=True, exist_ok=True)
    inputs.layout_output.parent.mkdir(parents=True, exist_ok=True)
    workspace = Path(
        tempfile.mkdtemp(
            prefix=".meccha-runtime-assembly-",
            dir=inputs.output_root.parent,
        )
    )
    staged_payload = workspace / "payload"
    staged_layout = workspace / "payload-layout.json"
    payload_published = False
    try:
        staged_payload.mkdir()
        layout = _assemble_staging(
            inputs,
            staged_payload,
        )
        encoded_layout = (
            json.dumps(
                layout,
                ensure_ascii=True,
                indent=2,
                separators=(",", ": "),
            )
            + "\n"
        ).encode("utf-8")
        staged_layout.write_bytes(encoded_layout)

        os.replace(staged_payload, inputs.output_root)
        payload_published = True
        try:
            os.replace(staged_layout, inputs.layout_output)
        except OSError:
            shutil.rmtree(inputs.output_root)
            payload_published = False
            raise
        return layout
    except RuntimeAssemblyError:
        raise
    except OSError as error:
        raise RuntimeAssemblyError(
            f"Runtime assembly publication failed: {error}"
        ) from error
    finally:
        if payload_published and not inputs.layout_output.exists():
            shutil.rmtree(inputs.output_root, ignore_errors=True)
        shutil.rmtree(workspace, ignore_errors=True)


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Assemble the exact MecchaCamouflage v2 runtime tree"
    )
    parser.add_argument("--project-root", required=True, type=Path)
    parser.add_argument("--ue4ss-dll", required=True, type=Path)
    parser.add_argument("--proxy-dll", required=True, type=Path)
    parser.add_argument("--mod-dll", required=True, type=Path)
    parser.add_argument("--ue4ss-settings", required=True, type=Path)
    parser.add_argument(
        "--member-variable-layout",
        required=True,
        type=Path,
    )
    parser.add_argument("--ue4ss-license", required=True, type=Path)
    parser.add_argument(
        "--dependency-notices",
        required=True,
        type=Path,
    )
    parser.add_argument("--output-root", required=True, type=Path)
    parser.add_argument("--layout-output", required=True, type=Path)
    return parser


def main(arguments: Iterable[str] | None = None) -> int:
    options = _parser().parse_args(arguments)
    try:
        layout = assemble_runtime(
            RuntimeAssemblyInputs(
                project_root=options.project_root,
                ue4ss_binary=options.ue4ss_dll,
                proxy_binary=options.proxy_dll,
                mod_binary=options.mod_dll,
                ue4ss_settings=options.ue4ss_settings,
                member_variable_layout=options.member_variable_layout,
                ue4ss_license=options.ue4ss_license,
                dependency_notices=options.dependency_notices,
                output_root=options.output_root,
                layout_output=options.layout_output,
            )
        )
    except RuntimeAssemblyError as error:
        print(f"runtime assembly error: {error}", file=sys.stderr)
        return 1
    print(
        "PASS runtime assembly: "
        f"{len(layout['files'])} immutable files, "
        f"{len(layout['generated_paths'])} generated paths"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
