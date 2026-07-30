#!/usr/bin/env python3
"""Build notices only from an exact, separately approved dependency audit."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import stat
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Iterable


class DependencyNoticeError(ValueError):
    pass


@dataclass
class DependencyNoticeInputs:
    evidence: Path
    approved_audit: Path
    roots: dict[str, Path]
    notice_output: Path
    report_output: Path
    ue4ss_commit: str


_MAXIMUM_METADATA_BYTES = 8 * 1024 * 1024
_MAXIMUM_LICENSE_BYTES = 2 * 1024 * 1024
_MAXIMUM_COMPONENTS = 2048
_MAXIMUM_LICENSE_FILES = 16
_COMMIT_PATTERN = re.compile(r"[0-9a-f]{40}")
_SHA256_PATTERN = re.compile(r"[0-9a-f]{64}")
_ROOT_NAMES = {"project", "build", "cargo"}
_FIXED_WINDOWS_DEVICES = {
    "CON",
    "PRN",
    "AUX",
    "NUL",
    "CONIN$",
    "CONOUT$",
}


def _is_link_or_reparse(path: Path) -> bool:
    try:
        metadata = path.lstat()
    except OSError as error:
        raise DependencyNoticeError(
            f"Dependency audit path could not be inspected: {path}"
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


def _plain_file(path: Path, maximum_size: int) -> bytes:
    if _is_link_or_reparse(path):
        raise DependencyNoticeError(
            f"Dependency license/audit input is a link or reparse point: "
            f"{path}"
        )
    try:
        metadata = path.stat()
        if (
            not stat.S_ISREG(metadata.st_mode)
            or metadata.st_size == 0
            or metadata.st_size > maximum_size
        ):
            raise DependencyNoticeError(
                f"Dependency license/audit input size is invalid: {path}"
            )
        return path.read_bytes()
    except DependencyNoticeError:
        raise
    except OSError as error:
        raise DependencyNoticeError(
            f"Dependency license/audit input could not be read: {path}"
        ) from error


def _plain_file_below(
    root: Path,
    relative_path: str,
    maximum_size: int,
) -> bytes:
    current = root
    segments = relative_path.split("/")
    for index, segment in enumerate(segments):
        current /= segment
        if _is_link_or_reparse(current):
            raise DependencyNoticeError(
                f"Dependency license path contains a link or reparse "
                f"point: {relative_path}"
            )
        if index < len(segments) - 1 and not current.is_dir():
            raise DependencyNoticeError(
                f"Dependency license parent is not a directory: "
                f"{relative_path}"
            )
    return _plain_file(current, maximum_size)


def _reject_json_constant(value: str) -> None:
    raise DependencyNoticeError(
        f"Dependency audit contains an invalid number: {value}"
    )


def _load_canonical_json(path: Path) -> tuple[dict[str, Any], bytes]:
    encoded = _plain_file(path, _MAXIMUM_METADATA_BYTES)

    def reject_duplicate_keys(
        pairs: list[tuple[str, Any]],
    ) -> dict[str, Any]:
        result: dict[str, Any] = {}
        for key, value in pairs:
            if key in result:
                raise DependencyNoticeError(
                    f"Dependency audit has a duplicate JSON key: {key}"
                )
            result[key] = value
        return result

    try:
        value = json.loads(
            encoded.decode("utf-8"),
            object_pairs_hook=reject_duplicate_keys,
            parse_constant=_reject_json_constant,
        )
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise DependencyNoticeError(
            f"Dependency audit is not strict UTF-8 JSON: {path}"
        ) from error
    if not isinstance(value, dict):
        raise DependencyNoticeError(
            f"Dependency audit root is not an object: {path}"
        )
    canonical = (
        json.dumps(
            value,
            ensure_ascii=True,
            indent=2,
            separators=(",", ": "),
        )
        + "\n"
    ).encode("utf-8")
    if encoded != canonical:
        raise DependencyNoticeError(
            f"Dependency audit encoding is not canonical: {path}"
        )
    return value, encoded


def _bounded_ascii(
    value: object,
    label: str,
    *,
    maximum: int,
) -> str:
    if (
        not isinstance(value, str)
        or not value
        or len(value) > maximum
        or not value.isascii()
        or any(ord(character) < 0x20 for character in value)
    ):
        raise DependencyNoticeError(
            f"Dependency {label} is invalid: {value!r}"
        )
    return value


def _canonical_path(value: object) -> str:
    path = _bounded_ascii(
        value,
        "license path",
        maximum=1024,
    )
    if (
        path.startswith("/")
        or path.endswith("/")
        or "\\" in path
        or ":" in path
    ):
        raise DependencyNoticeError(
            f"Dependency license path is not canonical: {path}"
        )
    for segment in path.split("/"):
        if (
            not segment
            or segment in {".", ".."}
            or segment.startswith(" ")
            or segment.endswith((" ", "."))
            or any(character in '<>"|?*' for character in segment)
        ):
            raise DependencyNoticeError(
                f"Dependency license path is not canonical: {path}"
            )
        stem = segment.split(".", maxsplit=1)[0].upper()
        if (
            stem in _FIXED_WINDOWS_DEVICES
            or (
                len(stem) == 4
                and stem[:3] in {"COM", "LPT"}
                and stem[3] in "123456789"
            )
        ):
            raise DependencyNoticeError(
                f"Dependency license path uses a reserved device: {path}"
            )
    return path


def _validate_evidence(
    evidence: dict[str, Any],
    ue4ss_commit: str,
) -> list[dict[str, str]]:
    if list(evidence) != [
        "schema_version",
        "ue4ss_commit",
        "components",
    ]:
        raise DependencyNoticeError(
            "Dependency evidence keys or canonical order are invalid."
        )
    components = evidence.get("components")
    if (
        type(evidence.get("schema_version")) is not int
        or evidence["schema_version"] != 1
        or evidence.get("ue4ss_commit") != ue4ss_commit
        or not isinstance(components, list)
        or not components
        or len(components) > _MAXIMUM_COMPONENTS
    ):
        raise DependencyNoticeError(
            "Dependency evidence schema or identity is invalid."
        )
    validated: list[dict[str, str]] = []
    seen: set[str] = set()
    previous = ""
    for component in components:
        if (
            not isinstance(component, dict)
            or list(component)
            != ["name", "version", "source_identity"]
        ):
            raise DependencyNoticeError(
                "Dependency evidence component keys are invalid."
            )
        name = _bounded_ascii(
            component["name"],
            "component name",
            maximum=128,
        )
        version = _bounded_ascii(
            component["version"],
            "component version",
            maximum=128,
        )
        source_identity = _bounded_ascii(
            component["source_identity"],
            "source identity",
            maximum=1024,
        )
        key = name.lower()
        if key in seen or (previous and key < previous):
            raise DependencyNoticeError(
                "Dependency evidence components are duplicated or "
                "not canonically sorted."
            )
        previous = key
        seen.add(key)
        validated.append(
            {
                "name": name,
                "version": version,
                "source_identity": source_identity,
            }
        )
    return validated


def _validate_audit(
    audit: dict[str, Any],
    *,
    evidence_components: list[dict[str, str]],
    evidence_sha256: str,
    ue4ss_commit: str,
) -> list[dict[str, Any]]:
    if list(audit) != [
        "schema_version",
        "ue4ss_commit",
        "dependency_evidence_sha256",
        "components",
    ]:
        raise DependencyNoticeError(
            "Approved dependency audit keys or order are invalid."
        )
    components = audit.get("components")
    if (
        type(audit.get("schema_version")) is not int
        or audit["schema_version"] != 1
        or audit.get("ue4ss_commit") != ue4ss_commit
        or audit.get("dependency_evidence_sha256")
        != evidence_sha256
        or not isinstance(components, list)
        or not components
        or len(components) > _MAXIMUM_COMPONENTS
    ):
        raise DependencyNoticeError(
            "Approved dependency audit schema or evidence identity "
            "is invalid."
        )
    audited_identities: list[dict[str, str]] = []
    validated: list[dict[str, Any]] = []
    for component in components:
        if (
            not isinstance(component, dict)
            or list(component)
            != [
                "name",
                "version",
                "source_identity",
                "license_expression",
                "license_files",
            ]
        ):
            raise DependencyNoticeError(
                "Approved dependency component keys are invalid."
            )
        identity = {
            "name": _bounded_ascii(
                component["name"],
                "component name",
                maximum=128,
            ),
            "version": _bounded_ascii(
                component["version"],
                "component version",
                maximum=128,
            ),
            "source_identity": _bounded_ascii(
                component["source_identity"],
                "source identity",
                maximum=1024,
            ),
        }
        expression = _bounded_ascii(
            component["license_expression"],
            "license expression",
            maximum=256,
        )
        license_files = component["license_files"]
        if (
            not isinstance(license_files, list)
            or not license_files
            or len(license_files) > _MAXIMUM_LICENSE_FILES
        ):
            raise DependencyNoticeError(
                f"Dependency component has no approved license files: "
                f"{identity['name']}"
            )
        validated_files: list[dict[str, str]] = []
        previous_file = ""
        seen_files: set[str] = set()
        for license_file in license_files:
            if (
                not isinstance(license_file, dict)
                or list(license_file)
                != ["root", "path", "sha256"]
            ):
                raise DependencyNoticeError(
                    "Approved dependency license record is invalid."
                )
            root = license_file["root"]
            path = _canonical_path(license_file["path"])
            digest = license_file["sha256"]
            if (
                not isinstance(root, str)
                or root not in _ROOT_NAMES
                or not isinstance(digest, str)
                or not _SHA256_PATTERN.fullmatch(digest)
            ):
                raise DependencyNoticeError(
                    f"Approved dependency license identity is invalid: "
                    f"{identity['name']}"
                )
            key = f"{root}:{path}".lower()
            if key in seen_files or (previous_file and key < previous_file):
                raise DependencyNoticeError(
                    "Approved dependency license files are duplicated "
                    "or not canonically sorted."
                )
            previous_file = key
            seen_files.add(key)
            validated_files.append(
                {
                    "root": root,
                    "path": path,
                    "sha256": digest,
                }
            )
        audited_identities.append(identity)
        validated.append(
            {
                **identity,
                "license_expression": expression,
                "license_files": validated_files,
            }
        )
    if audited_identities != evidence_components:
        raise DependencyNoticeError(
            "Approved dependency audit components do not exactly match "
            "dependency evidence."
        )
    return validated


def _validate_roots(roots: dict[str, Path]) -> dict[str, Path]:
    if set(roots) != _ROOT_NAMES:
        raise DependencyNoticeError(
            "Dependency license roots must be exactly project/build/cargo."
        )
    validated: dict[str, Path] = {}
    for name, raw_path in roots.items():
        path = Path(os.path.abspath(raw_path))
        if _is_link_or_reparse(path) or not path.is_dir():
            raise DependencyNoticeError(
                f"Dependency license root is invalid: {name}"
            )
        validated[name] = path
    return validated


def _read_approved_licenses(
    components: list[dict[str, Any]],
    roots: dict[str, Path],
) -> list[dict[str, Any]]:
    collected: list[dict[str, Any]] = []
    for component in components:
        collected_files: list[dict[str, Any]] = []
        for license_file in component["license_files"]:
            encoded = _plain_file_below(
                roots[license_file["root"]],
                license_file["path"],
                _MAXIMUM_LICENSE_BYTES,
            )
            digest = hashlib.sha256(encoded).hexdigest()
            if digest != license_file["sha256"]:
                raise DependencyNoticeError(
                    f"Dependency license hash changed: "
                    f"{component['name']} "
                    f"{license_file['root']}:{license_file['path']}"
                )
            try:
                text = encoded.decode("utf-8")
            except UnicodeDecodeError as error:
                raise DependencyNoticeError(
                    f"Dependency license is not strict UTF-8: "
                    f"{component['name']}"
                ) from error
            if "\0" in text or not text.strip():
                raise DependencyNoticeError(
                    f"Dependency license text is empty or invalid: "
                    f"{component['name']}"
                )
            collected_files.append(
                {
                    **license_file,
                    "size": len(encoded),
                    "text": text,
                }
            )
        collected.append(
            {
                **component,
                "license_files": collected_files,
            }
        )
    return collected


def _notice_bytes(
    components: list[dict[str, Any]],
    *,
    evidence_sha256: str,
    audit_sha256: str,
) -> bytes:
    lines = [
        "MecchaCamouflage v2 third-party notices",
        f"Dependency evidence SHA-256: {evidence_sha256}",
        f"Approved audit SHA-256: {audit_sha256}",
        "",
    ]
    for component in components:
        lines.extend(
            (
                "=" * 78,
                f"Component: {component['name']} {component['version']}",
                f"Source: {component['source_identity']}",
                f"License: {component['license_expression']}",
                "",
            )
        )
        for license_file in component["license_files"]:
            lines.extend(
                (
                    f"License file: {license_file['root']}:"
                    f"{license_file['path']}",
                    f"License file SHA-256: {license_file['sha256']}",
                    "-" * 78,
                    license_file["text"].rstrip("\r\n"),
                    "",
                )
            )
    return ("\n".join(lines).rstrip("\n") + "\n").encode("utf-8")


def _atomic_write(path: Path, contents: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary_name = tempfile.mkstemp(
        prefix=path.name + ".",
        suffix=".tmp",
        dir=path.parent,
    )
    temporary = Path(temporary_name)
    try:
        with os.fdopen(descriptor, "wb") as output:
            output.write(contents)
            output.flush()
            os.fsync(output.fileno())
        os.replace(temporary, path)
    except BaseException:
        temporary.unlink(missing_ok=True)
        raise


def build_dependency_notices(
    inputs: DependencyNoticeInputs,
) -> dict[str, Any]:
    if not _COMMIT_PATTERN.fullmatch(inputs.ue4ss_commit):
        raise DependencyNoticeError(
            "Dependency notice UE4SS commit is invalid."
        )
    inputs.evidence = Path(os.path.abspath(inputs.evidence))
    inputs.approved_audit = Path(
        os.path.abspath(inputs.approved_audit)
    )
    inputs.notice_output = Path(
        os.path.abspath(inputs.notice_output)
    )
    inputs.report_output = Path(
        os.path.abspath(inputs.report_output)
    )
    if inputs.notice_output == inputs.report_output:
        raise DependencyNoticeError(
            "Dependency notice and report outputs must be distinct."
        )
    roots = _validate_roots(inputs.roots)
    evidence, evidence_bytes = _load_canonical_json(inputs.evidence)
    audit, audit_bytes = _load_canonical_json(inputs.approved_audit)
    evidence_sha256 = hashlib.sha256(evidence_bytes).hexdigest()
    audit_sha256 = hashlib.sha256(audit_bytes).hexdigest()
    evidence_components = _validate_evidence(
        evidence,
        inputs.ue4ss_commit,
    )
    audited_components = _validate_audit(
        audit,
        evidence_components=evidence_components,
        evidence_sha256=evidence_sha256,
        ue4ss_commit=inputs.ue4ss_commit,
    )
    collected = _read_approved_licenses(
        audited_components,
        roots,
    )
    notice = _notice_bytes(
        collected,
        evidence_sha256=evidence_sha256,
        audit_sha256=audit_sha256,
    )
    report: dict[str, Any] = {
        "schema_version": 1,
        "ue4ss_commit": inputs.ue4ss_commit,
        "dependency_evidence_sha256": evidence_sha256,
        "approved_audit_sha256": audit_sha256,
        "component_count": len(collected),
        "components": [
            {
                "name": component["name"],
                "version": component["version"],
                "source_identity": component["source_identity"],
                "license_expression":
                    component["license_expression"],
                "license_files": [
                    {
                        "root": license_file["root"],
                        "path": license_file["path"],
                        "size": license_file["size"],
                        "sha256": license_file["sha256"],
                    }
                    for license_file in component["license_files"]
                ],
            }
            for component in collected
        ],
        "notice_size": len(notice),
        "notice_sha256": hashlib.sha256(notice).hexdigest(),
    }
    report_bytes = (
        json.dumps(
            report,
            ensure_ascii=True,
            indent=2,
            separators=(",", ": "),
        )
        + "\n"
    ).encode("utf-8")
    try:
        _atomic_write(inputs.notice_output, notice)
        _atomic_write(inputs.report_output, report_bytes)
    except OSError as error:
        raise DependencyNoticeError(
            f"Dependency notice publication failed: {error}"
        ) from error
    return report


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Build notices from approved dependency evidence"
    )
    parser.add_argument("--evidence", required=True, type=Path)
    parser.add_argument("--approved-audit", required=True, type=Path)
    parser.add_argument("--project-root", required=True, type=Path)
    parser.add_argument("--build-root", required=True, type=Path)
    parser.add_argument("--cargo-root", required=True, type=Path)
    parser.add_argument("--notice-output", required=True, type=Path)
    parser.add_argument("--report-output", required=True, type=Path)
    parser.add_argument("--ue4ss-commit", required=True)
    return parser


def main(arguments: Iterable[str] | None = None) -> int:
    options = _parser().parse_args(arguments)
    try:
        report = build_dependency_notices(
            DependencyNoticeInputs(
                evidence=options.evidence,
                approved_audit=options.approved_audit,
                roots={
                    "project": options.project_root,
                    "build": options.build_root,
                    "cargo": options.cargo_root,
                },
                notice_output=options.notice_output,
                report_output=options.report_output,
                ue4ss_commit=options.ue4ss_commit,
            )
        )
    except DependencyNoticeError as error:
        print(f"dependency notice error: {error}", file=sys.stderr)
        return 1
    print(
        "PASS dependency notices: "
        f"{report['component_count']} components, "
        f"{report['notice_sha256']}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
