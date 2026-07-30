#!/usr/bin/env python3

import argparse
import hashlib
import json
import os
import re
import stat
import sys
import tempfile
from pathlib import Path
from typing import Any


class PayloadManifestError(ValueError):
    pass


_LAYOUT_KEYS = {"schema_version", "generated_paths", "files"}
_FILE_KEYS = {"path", "role"}
_ROLES = {
    "proxy",
    "override",
    "runtime",
    "mod",
    "config",
    "profile",
    "localization",
    "font",
    "license",
}
_FIXED_DEVICES = {"CON", "PRN", "AUX", "NUL", "CONIN$", "CONOUT$"}
_VERSION_PATTERN = re.compile(
    r"[0-9]+\.[0-9]+\.[0-9]+(?:-[0-9A-Za-z.-]+)?"
)
_COMMIT_PATTERN = re.compile(r"[0-9a-f]{40}")
_MAXIMUM_LAYOUT_BYTES = 4 * 1024 * 1024


def _reject_json_constant(value: str) -> None:
    raise PayloadManifestError(
        f"Payload layout uses an invalid number: {value}"
    )


def _canonical_path(path: object) -> str:
    if not isinstance(path, str):
        raise PayloadManifestError("Payload path must be a string")
    if (
        not path
        or len(path.encode("ascii", errors="ignore")) != len(path)
        or len(path) > 1024
        or path.startswith("/")
        or path.endswith("/")
        or "\\" in path
        or ":" in path
    ):
        raise PayloadManifestError(f"Payload path is not canonical: {path!r}")
    for segment in path.split("/"):
        if (
            not segment
            or segment in {".", ".."}
            or segment.startswith(" ")
            or segment.endswith((" ", "."))
            or any(character in '<>"|?*' for character in segment)
            or any(ord(character) < 0x20 for character in segment)
        ):
            raise PayloadManifestError(
                f"Payload path is not canonical: {path}"
            )
        stem = segment.split(".", maxsplit=1)[0].upper()
        if (
            stem in _FIXED_DEVICES
            or (
                len(stem) == 4
                and stem[:3] in {"COM", "LPT"}
                and stem[3] in "123456789"
            )
        ):
            raise PayloadManifestError(
                f"Payload path uses a reserved device: {path}"
            )
    return path


def _paths_overlap(left: str, right: str) -> bool:
    left_key = left.lower()
    right_key = right.lower()
    return (
        left_key == right_key
        or left_key.startswith(right_key + "/")
        or right_key.startswith(left_key + "/")
    )


def _is_link_or_reparse(path: Path) -> bool:
    try:
        metadata = path.lstat()
    except OSError as error:
        raise PayloadManifestError(
            f"Payload path could not be inspected: {path}"
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


def _validate_layout(
    layout: dict[str, Any],
    product_version: str,
    ue4ss_commit: str,
) -> tuple[list[dict[str, str]], list[str]]:
    if not isinstance(layout, dict) or set(layout) != _LAYOUT_KEYS:
        raise PayloadManifestError("Payload layout keys are invalid")
    if (
        type(layout["schema_version"]) is not int
        or layout["schema_version"] != 1
    ):
        raise PayloadManifestError("Unsupported payload layout schema")
    if not _VERSION_PATTERN.fullmatch(product_version):
        raise PayloadManifestError("Product version is not canonical")
    if not _COMMIT_PATTERN.fullmatch(ue4ss_commit):
        raise PayloadManifestError("UE4SS commit is not canonical")

    raw_files = layout["files"]
    if (
        not isinstance(raw_files, list)
        or not raw_files
        or len(raw_files) > 4096
    ):
        raise PayloadManifestError("Payload file count is invalid")
    files: list[dict[str, str]] = []
    seen: set[str] = set()
    for entry in raw_files:
        if not isinstance(entry, dict) or set(entry) != _FILE_KEYS:
            raise PayloadManifestError("Payload file entry keys are invalid")
        path = _canonical_path(entry["path"])
        role = entry["role"]
        if not isinstance(role, str) or role not in _ROLES:
            raise PayloadManifestError(f"Payload role is invalid: {role!r}")
        key = path.lower()
        if key in seen:
            raise PayloadManifestError(
                f"Payload path is duplicated: {path}"
            )
        seen.add(key)
        files.append({"path": path, "role": role})

    raw_generated = layout["generated_paths"]
    if (
        not isinstance(raw_generated, list)
        or len(raw_generated) > 64
    ):
        raise PayloadManifestError("Generated path count is invalid")
    generated: list[str] = []
    for raw_path in raw_generated:
        path = _canonical_path(raw_path)
        if any(_paths_overlap(path, other) for other in generated):
            raise PayloadManifestError(
                f"Generated paths overlap: {path}"
            )
        if any(_paths_overlap(path, entry["path"]) for entry in files):
            raise PayloadManifestError(
                f"Generated path overlaps a payload file: {path}"
            )
        generated.append(path)
    return files, generated


def load_layout(path: Path) -> dict[str, Any]:
    try:
        encoded = path.read_bytes()
    except OSError as error:
        raise PayloadManifestError(
            f"Payload layout could not be read: {path}"
        ) from error
    if not encoded or len(encoded) > _MAXIMUM_LAYOUT_BYTES:
        raise PayloadManifestError(
            "Payload layout size is outside the supported range"
        )

    def reject_duplicate_keys(
        pairs: list[tuple[str, Any]],
    ) -> dict[str, Any]:
        result: dict[str, Any] = {}
        for key, value in pairs:
            if key in result:
                raise PayloadManifestError(
                    f"Payload layout has a duplicate JSON key: {key}"
                )
            result[key] = value
        return result

    try:
        decoded = encoded.decode("utf-8")
        layout = json.loads(
            decoded,
            object_pairs_hook=reject_duplicate_keys,
            parse_constant=_reject_json_constant,
        )
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise PayloadManifestError(
            "Payload layout is not strict UTF-8 JSON"
        ) from error
    if not isinstance(layout, dict):
        raise PayloadManifestError("Payload layout root must be an object")
    return layout


def generate_payload_manifest(
    root: Path,
    layout: dict[str, Any],
    *,
    product_version: str,
    ue4ss_commit: str,
) -> dict[str, Any]:
    layout_files, generated_paths = _validate_layout(
        layout,
        product_version,
        ue4ss_commit,
    )
    if not root.is_dir() or _is_link_or_reparse(root):
        raise PayloadManifestError(
            "Payload root is unavailable or a symbolic link/reparse point"
        )
    payload_entries = list(root.rglob("*"))
    for path in payload_entries:
        if _is_link_or_reparse(path):
            raise PayloadManifestError(
                "Payload tree contains a symbolic link/reparse point: " +
                path.relative_to(root).as_posix()
            )
        mode = path.lstat().st_mode
        if not stat.S_ISREG(mode) and not stat.S_ISDIR(mode):
            raise PayloadManifestError(
                "Payload entry is not a regular file or directory: " +
                path.relative_to(root).as_posix()
            )
    declared_paths = {
        entry["path"].lower(): entry["path"]
        for entry in layout_files
    }
    actual_paths: dict[str, str] = {}
    for path in payload_entries:
        if not path.is_file():
            continue
        relative_path = path.relative_to(root).as_posix()
        key = relative_path.lower()
        if key in actual_paths:
            raise PayloadManifestError(
                "Payload tree has case-colliding files: " +
                actual_paths[key] + " and " + relative_path
            )
        actual_paths[key] = relative_path
    undeclared = sorted(
        actual_paths[key]
        for key in actual_paths.keys() - declared_paths.keys()
    )
    if undeclared:
        raise PayloadManifestError(
            "Payload file is not declared by the layout: " +
            undeclared[0]
        )
    missing = sorted(
        declared_paths[key]
        for key in declared_paths.keys() - actual_paths.keys()
    )
    if missing:
        raise PayloadManifestError(
            "Declared payload file is missing: " + missing[0]
        )
    case_mismatches = sorted(
        (declared_paths[key], actual_paths[key])
        for key in declared_paths.keys() & actual_paths.keys()
        if declared_paths[key] != actual_paths[key]
    )
    if case_mismatches:
        declared, actual = case_mismatches[0]
        raise PayloadManifestError(
            "Payload path casing differs from the layout: " +
            f"{declared} != {actual}"
        )

    files = []
    for entry in layout_files:
        relative_path = entry["path"]
        try:
            contents = (
                root / Path(*relative_path.split("/"))
            ).read_bytes()
        except OSError as error:
            raise PayloadManifestError(
                "Payload file could not be read: " + relative_path
            ) from error
        files.append(
            {
                "path": relative_path,
                "role": entry["role"],
                "size": len(contents),
                "sha256": hashlib.sha256(contents).hexdigest(),
            }
        )
    files.sort(key=lambda entry: entry["path"].lower())
    return {
        "schema_version": layout["schema_version"],
        "product_version": product_version,
        "ue4ss_commit": ue4ss_commit,
        "generated_paths": sorted(
            generated_paths,
            key=str.lower,
        ),
        "files": files,
    }


def write_payload_manifest(
    root: Path,
    layout: dict[str, Any],
    output: Path,
    *,
    product_version: str,
    ue4ss_commit: str,
) -> None:
    manifest = generate_payload_manifest(
        root,
        layout,
        product_version=product_version,
        ue4ss_commit=ue4ss_commit,
    )
    encoded = (
        json.dumps(
            manifest,
            ensure_ascii=True,
            indent=2,
            separators=(",", ": "),
        )
        + "\n"
    ).encode("utf-8")
    if len(encoded) > _MAXIMUM_LAYOUT_BYTES:
        raise PayloadManifestError(
            "Generated payload manifest exceeds the supported size"
        )
    output.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary_name = tempfile.mkstemp(
        prefix=output.name + ".",
        suffix=".tmp",
        dir=output.parent,
    )
    temporary_path = Path(temporary_name)
    try:
        with os.fdopen(descriptor, "wb") as temporary_file:
            temporary_file.write(encoded)
            temporary_file.flush()
            os.fsync(temporary_file.fileno())
        os.replace(temporary_path, output)
    except BaseException:
        temporary_path.unlink(missing_ok=True)
        raise


def main(arguments: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Generate a canonical MecchaCamouflage v2 payload manifest"
    )
    parser.add_argument("--payload-root", required=True, type=Path)
    parser.add_argument("--layout", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--product-version", required=True)
    parser.add_argument("--ue4ss-commit", required=True)
    options = parser.parse_args(arguments)
    try:
        layout = load_layout(options.layout)
        write_payload_manifest(
            options.payload_root,
            layout,
            options.output,
            product_version=options.product_version,
            ue4ss_commit=options.ue4ss_commit,
        )
    except (PayloadManifestError, OSError) as error:
        print(f"payload manifest error: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
