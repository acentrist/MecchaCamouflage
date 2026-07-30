#!/usr/bin/env python3
"""Generate an explicitly unapproved audit template from exact evidence."""

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

from build_dependency_notices import (
    DependencyNoticeError,
    load_canonical_dependency_json,
    validate_dependency_evidence,
)


class DependencyAuditTemplateError(ValueError):
    pass


@dataclass
class DependencyAuditTemplateInputs:
    evidence: Path
    output: Path
    ue4ss_commit: str


_COMMIT_PATTERN = re.compile(r"[0-9a-f]{40}")


def _is_link_or_reparse(path: Path) -> bool:
    try:
        metadata = path.lstat()
    except OSError as error:
        raise DependencyAuditTemplateError(
            f"Dependency audit output could not be inspected: {path}"
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


def _validate_output(output: Path) -> None:
    if output.exists() or output.is_symlink():
        if _is_link_or_reparse(output) or not output.is_file():
            raise DependencyAuditTemplateError(
                f"Dependency audit output is not a plain file: {output}"
            )
    for parent in (output.parent, *output.parent.parents):
        if not parent.exists():
            continue
        if _is_link_or_reparse(parent) or not parent.is_dir():
            raise DependencyAuditTemplateError(
                "Dependency audit output parent is invalid: "
                f"{parent}"
            )


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


def generate_dependency_audit_template(
    inputs: DependencyAuditTemplateInputs,
) -> dict[str, Any]:
    if not _COMMIT_PATTERN.fullmatch(inputs.ue4ss_commit):
        raise DependencyAuditTemplateError(
            "Dependency audit template UE4SS commit is invalid."
        )
    evidence_path = Path(os.path.abspath(inputs.evidence))
    output_path = Path(os.path.abspath(inputs.output))
    if evidence_path == output_path:
        raise DependencyAuditTemplateError(
            "Dependency evidence and audit-template output must be distinct."
        )
    _validate_output(output_path)
    try:
        evidence, evidence_bytes = load_canonical_dependency_json(
            evidence_path
        )
        components = validate_dependency_evidence(
            evidence,
            inputs.ue4ss_commit,
        )
    except DependencyNoticeError as error:
        raise DependencyAuditTemplateError(str(error)) from error

    template: dict[str, Any] = {
        "schema_version": 1,
        "ue4ss_commit": inputs.ue4ss_commit,
        "dependency_evidence_sha256": hashlib.sha256(
            evidence_bytes
        ).hexdigest(),
        "components": [
            {
                **component,
                "license_expression": "",
                "license_files": [],
            }
            for component in components
        ],
    }
    encoded = (
        json.dumps(
            template,
            ensure_ascii=True,
            indent=2,
            separators=(",", ": "),
        )
        + "\n"
    ).encode("utf-8")
    try:
        _atomic_write(output_path, encoded)
    except OSError as error:
        raise DependencyAuditTemplateError(
            f"Dependency audit template publication failed: {error}"
        ) from error
    return template


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description=(
            "Generate an unapproved dependency-audit review template"
        )
    )
    parser.add_argument("--evidence", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--ue4ss-commit", required=True)
    return parser


def main(arguments: Iterable[str] | None = None) -> int:
    options = _parser().parse_args(arguments)
    try:
        template = generate_dependency_audit_template(
            DependencyAuditTemplateInputs(
                evidence=options.evidence,
                output=options.output,
                ue4ss_commit=options.ue4ss_commit,
            )
        )
    except DependencyAuditTemplateError as error:
        print(f"dependency audit template error: {error}", file=sys.stderr)
        return 1
    print(
        "PASS dependency audit template: "
        f"{len(template['components'])} unapproved components"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
