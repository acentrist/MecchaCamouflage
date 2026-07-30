#!/usr/bin/env python3

import argparse
import json
import os
import shutil
import stat
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Any

from generate_payload_manifest import (
    PayloadManifestError,
    generate_payload_manifest,
    load_layout,
    write_payload_manifest,
)


class PayloadCabError(ValueError):
    pass


_FIXED_TIMESTAMP = 946_684_800


def _quoted(value: str) -> str:
    if '"' in value or "\r" in value or "\n" in value:
        raise PayloadCabError("MakeCab value contains an unsafe character")
    return f'"{value}"'


def _makecab_directives(
    staging_root: Path,
    files: list[dict[str, Any]],
) -> str:
    lines = [
        ".OPTION EXPLICIT",
        ".Set Cabinet=on",
        ".Set Compress=on",
        ".Set CompressionType=LZX",
        ".Set CompressionMemory=21",
        ".Set CabinetFileCountThreshold=0",
        ".Set FolderFileCountThreshold=0",
        ".Set FolderSizeThreshold=0",
        ".Set MaxCabinetSize=0",
        ".Set MaxDiskFileCount=0",
        ".Set MaxDiskSize=0",
        ".Set CabinetNameTemplate=payload.cab",
        ".Set DiskDirectoryTemplate=.",
        ".Set DiskDirectory1=.",
        ".Set RptFileName=nul",
        ".Set InfFileName=nul",
    ]
    current_directory: str | None = None
    for entry in files:
        relative = entry["path"]
        destination = relative.rsplit("/", maxsplit=1)
        destination_directory = (
            destination[0].replace("/", "\\")
            if len(destination) == 2
            else ""
        )
        if destination_directory != current_directory:
            lines.append(
                ".Set DestinationDir=" +
                (
                    _quoted(destination_directory)
                    if destination_directory
                    else ""
                )
            )
            current_directory = destination_directory
        source = staging_root / Path(*relative.split("/"))
        lines.append(
            _quoted(str(source)) + " " +
            _quoted(Path(relative).name) +
            " /date=2000-01-01 /time=00:00:00 /attr="
        )
    return "\r\n".join(lines) + "\r\n"


def _normalise_staged_file(path: Path) -> None:
    os.chmod(path, stat.S_IREAD | stat.S_IWRITE)
    os.utime(path, (_FIXED_TIMESTAMP, _FIXED_TIMESTAMP))
    if os.name == "nt":
        import ctypes

        if not ctypes.windll.kernel32.SetFileAttributesW(
            str(path),
            0x80,
        ):
            raise PayloadCabError(
                f"Could not normalize staged file attributes: {path}"
            )


def _copy_exact_payload(
    payload_root: Path,
    staging_root: Path,
    manifest: dict[str, Any],
) -> None:
    for entry in manifest["files"]:
        relative = entry["path"]
        source = payload_root / Path(*relative.split("/"))
        destination = staging_root / Path(*relative.split("/"))
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, destination)
        _normalise_staged_file(destination)


def _run_makecab(workspace: Path, directives: str) -> Path:
    if os.name != "nt":
        raise PayloadCabError("MakeCab payload assembly requires Windows")
    try:
        directives.encode("ascii")
    except UnicodeEncodeError as error:
        raise PayloadCabError(
            "MakeCab staging paths must be ASCII-representable"
        ) from error
    ddf = workspace / "payload.ddf"
    ddf.write_bytes(directives.encode("ascii"))
    completed = subprocess.run(
        ["makecab.exe", "/V0", "/F", str(ddf)],
        cwd=workspace,
        check=False,
        capture_output=True,
        text=True,
    )
    if completed.returncode != 0:
        detail = (completed.stdout + completed.stderr).strip()
        raise PayloadCabError(
            "MakeCab failed" + (f": {detail}" if detail else "")
        )
    cab = workspace / "payload.cab"
    if not cab.is_file() or cab.stat().st_size == 0:
        raise PayloadCabError("MakeCab did not produce payload.cab")
    return cab


def verify_payload_cab(
    cab: Path,
    layout: dict[str, Any],
    expected_manifest: dict[str, Any],
) -> None:
    if os.name != "nt":
        raise PayloadCabError("CAB verification requires Windows")
    if not cab.is_file():
        raise PayloadCabError("Payload CAB is missing")
    try:
        product_version = expected_manifest["product_version"]
        ue4ss_commit = expected_manifest["ue4ss_commit"]
    except (KeyError, TypeError) as error:
        raise PayloadCabError(
            "Expected payload manifest identity is invalid"
        ) from error
    with tempfile.TemporaryDirectory(
        prefix=".meccha-payload-expand-",
        dir=cab.parent,
    ) as temporary:
        extracted = Path(temporary)
        completed = subprocess.run(
            [
                "expand.exe",
                "-F:*",
                str(cab),
                str(extracted),
            ],
            check=False,
            capture_output=True,
            text=True,
        )
        if completed.returncode != 0:
            detail = (completed.stdout + completed.stderr).strip()
            raise PayloadCabError(
                "Payload CAB expansion failed" +
                (f": {detail}" if detail else "")
            )
        actual_manifest = generate_payload_manifest(
            extracted,
            layout,
            product_version=product_version,
            ue4ss_commit=ue4ss_commit,
        )
        if actual_manifest != expected_manifest:
            raise PayloadCabError(
                "Expanded payload CAB does not match the manifest"
            )


def build_payload_cab(
    payload_root: Path,
    layout: dict[str, Any],
    manifest_output: Path,
    cab_output: Path,
    *,
    product_version: str,
    ue4ss_commit: str,
) -> dict[str, Any]:
    payload_path = payload_root.resolve()
    manifest_path = manifest_output.resolve()
    cab_path = cab_output.resolve()
    if (
        manifest_path == payload_path
        or manifest_path.is_relative_to(payload_path)
        or cab_path == payload_path
        or cab_path.is_relative_to(payload_path)
    ):
        raise PayloadCabError(
            "Manifest and CAB outputs must remain outside the payload root"
        )
    if os.path.normcase(manifest_path) == os.path.normcase(cab_path):
        raise PayloadCabError(
            "Manifest and CAB outputs must use distinct paths"
        )
    source_manifest = generate_payload_manifest(
        payload_root,
        layout,
        product_version=product_version,
        ue4ss_commit=ue4ss_commit,
    )
    cab_output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(
        prefix=".meccha-payload-cab-",
        dir=cab_output.parent,
    ) as temporary:
        workspace = Path(temporary)
        staging_root = workspace / "staging"
        staging_root.mkdir()
        _copy_exact_payload(
            payload_root,
            staging_root,
            source_manifest,
        )
        staged_manifest = generate_payload_manifest(
            staging_root,
            layout,
            product_version=product_version,
            ue4ss_commit=ue4ss_commit,
        )
        if staged_manifest != source_manifest:
            raise PayloadCabError(
                "Payload changed while the CAB staging snapshot was created"
            )
        cab = _run_makecab(
            workspace,
            _makecab_directives(
                staging_root,
                staged_manifest["files"],
            ),
        )
        verify_payload_cab(
            cab,
            layout,
            staged_manifest,
        )
        with cab.open("r+b") as cab_file:
            cab_file.flush()
            os.fsync(cab_file.fileno())
        os.replace(cab, cab_output)

        temporary_manifest = workspace / "payload-manifest.json"
        write_payload_manifest(
            staging_root,
            layout,
            temporary_manifest,
            product_version=product_version,
            ue4ss_commit=ue4ss_commit,
        )
        manifest_output.parent.mkdir(parents=True, exist_ok=True)
        os.replace(temporary_manifest, manifest_output)
    return source_manifest


def main(arguments: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Build deterministic MecchaCamouflage v2 payload outputs"
    )
    parser.add_argument("--payload-root", required=True, type=Path)
    parser.add_argument("--layout", required=True, type=Path)
    parser.add_argument("--manifest-output", required=True, type=Path)
    parser.add_argument("--cab-output", required=True, type=Path)
    parser.add_argument("--product-version", required=True)
    parser.add_argument("--ue4ss-commit", required=True)
    options = parser.parse_args(arguments)
    try:
        layout = load_layout(options.layout)
        manifest = build_payload_cab(
            options.payload_root,
            layout,
            options.manifest_output,
            options.cab_output,
            product_version=options.product_version,
            ue4ss_commit=options.ue4ss_commit,
        )
        print(
            json.dumps(
                {
                    "cab": str(options.cab_output),
                    "files": len(manifest["files"]),
                },
                separators=(",", ":"),
            )
        )
    except (PayloadManifestError, PayloadCabError, OSError) as error:
        print(f"payload CAB error: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
