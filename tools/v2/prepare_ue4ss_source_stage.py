#!/usr/bin/env python3
"""Prepare and verify an immutable UE4SS build-only source stage."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import stat
import subprocess
import sys
import tempfile
from pathlib import Path, PurePosixPath
from typing import Any, Iterable


class Ue4ssSourceStageError(ValueError):
    pass


_MAXIMUM_POLICY_BYTES = 64 * 1024
_MAXIMUM_OVERLAY_BYTES = 4 * 1024 * 1024
_COMMIT_LENGTH = 40
_DIGEST_LENGTH = 64


def _sha256(contents: bytes) -> str:
    return hashlib.sha256(contents).hexdigest()


def _is_link_or_reparse(path: Path) -> bool:
    try:
        metadata = path.lstat()
    except OSError as error:
        raise Ue4ssSourceStageError(
            f"UE4SS source-stage path could not be inspected: {path}"
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
        raise Ue4ssSourceStageError(
            f"UE4SS source-stage input is linked or reparse-routed: {path}"
        )
    try:
        metadata = path.stat()
        if (
            not stat.S_ISREG(metadata.st_mode)
            or metadata.st_size == 0
            or metadata.st_size > maximum_size
        ):
            raise Ue4ssSourceStageError(
                f"UE4SS source-stage input size is invalid: {path}"
            )
        return path.read_bytes()
    except Ue4ssSourceStageError:
        raise
    except OSError as error:
        raise Ue4ssSourceStageError(
            f"UE4SS source-stage input could not be read: {path}"
        ) from error


def _strict_json(path: Path) -> tuple[dict[str, Any], bytes]:
    encoded = _plain_file(path, _MAXIMUM_POLICY_BYTES)

    def reject_duplicates(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
        result: dict[str, Any] = {}
        for key, value in pairs:
            if key in result:
                raise Ue4ssSourceStageError(
                    f"UE4SS source-stage policy duplicates key: {key}"
                )
            result[key] = value
        return result

    try:
        value = json.loads(
            encoded.decode("utf-8"),
            object_pairs_hook=reject_duplicates,
            parse_constant=lambda item: (_ for _ in ()).throw(
                Ue4ssSourceStageError(
                    f"UE4SS source-stage policy has invalid number: {item}"
                )
            ),
        )
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise Ue4ssSourceStageError(
            "UE4SS source-stage policy is not strict UTF-8 JSON."
        ) from error
    if not isinstance(value, dict):
        raise Ue4ssSourceStageError(
            "UE4SS source-stage policy root is invalid."
        )
    return value, encoded


def _plain_directory(path: Path, label: str) -> Path:
    normalized = Path(os.path.abspath(path))
    if not normalized.is_dir() or _is_link_or_reparse(normalized):
        raise Ue4ssSourceStageError(
            f"UE4SS source-stage {label} is not a plain directory."
        )
    return normalized


def _relative_path(value: Any, label: str) -> PurePosixPath:
    if not isinstance(value, str) or not value or "\\" in value:
        raise Ue4ssSourceStageError(
            f"UE4SS source-stage {label} is not a canonical relative path."
        )
    path = PurePosixPath(value)
    if (
        path.is_absolute()
        or any(part in ("", ".", "..") for part in path.parts)
        or path.as_posix() != value
    ):
        raise Ue4ssSourceStageError(
            f"UE4SS source-stage {label} is not a canonical relative path."
        )
    return path


def _identity(value: Any, length: int, label: str) -> str:
    if (
        not isinstance(value, str)
        or len(value) != length
        or any(character not in "0123456789abcdef" for character in value)
    ):
        raise Ue4ssSourceStageError(
            f"UE4SS source-stage {label} is invalid."
        )
    return value


def _git(
    directory: Path,
    *arguments: str,
    binary: bool = False,
) -> str | bytes:
    try:
        completed = subprocess.run(
            [
                "git",
                "-c",
                "core.autocrlf=false",
                "-c",
                "core.filemode=false",
                "-C",
                str(directory),
                *arguments,
            ],
            check=False,
            capture_output=True,
            text=not binary,
        )
    except OSError as error:
        raise Ue4ssSourceStageError(
            "git is unavailable for UE4SS source staging."
        ) from error
    if completed.returncode != 0:
        stderr = (
            completed.stderr.decode("utf-8", errors="replace")
            if binary
            else completed.stderr
        )
        raise Ue4ssSourceStageError(
            f"git {' '.join(arguments)} failed: {stderr.strip()}"
        )
    return completed.stdout


def _clone(source: Path, destination: Path, commit: str) -> None:
    try:
        completed = subprocess.run(
            [
                "git",
                "-c",
                "protocol.file.allow=always",
                "clone",
                "--quiet",
                "--no-hardlinks",
                "--no-checkout",
                str(source),
                str(destination),
            ],
            check=False,
            capture_output=True,
            text=True,
        )
    except OSError as error:
        raise Ue4ssSourceStageError(
            "git is unavailable for UE4SS source staging."
        ) from error
    if completed.returncode != 0:
        raise Ue4ssSourceStageError(
            f"UE4SS source clone failed: {completed.stderr.strip()}"
        )
    _git(destination, "config", "core.autocrlf", "false")
    _git(destination, "checkout", "--quiet", "--detach", "--force", commit)


def _atomic_write(path: Path, contents: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary_name = tempfile.mkstemp(
        prefix=f".{path.name}.",
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


def _load_policy(
    policy_path: Path,
) -> tuple[
    dict[str, Any],
    bytes,
    PurePosixPath,
    Path,
    list[tuple[PurePosixPath, str]],
]:
    policy, encoded = _strict_json(policy_path)
    if set(policy) != {
        "schema_version",
        "ue4ss_commit",
        "overlay",
        "nested_gitlinks",
    }:
        raise Ue4ssSourceStageError(
            "UE4SS source-stage policy fields are invalid."
        )
    if policy["schema_version"] != 1:
        raise Ue4ssSourceStageError(
            "UE4SS source-stage policy schema is unsupported."
        )
    _identity(policy["ue4ss_commit"], _COMMIT_LENGTH, "commit")
    overlay = policy["overlay"]
    if (
        not isinstance(overlay, dict)
        or set(overlay) != {
            "source",
            "target",
            "upstream_sha256",
            "overlay_sha256",
            "staged_diff_sha256",
        }
    ):
        raise Ue4ssSourceStageError(
            "UE4SS source-stage overlay policy is invalid."
        )
    target = _relative_path(overlay["target"], "overlay target")
    overlay_source = _relative_path(
        overlay["source"],
        "overlay source",
    )
    for field in (
        "upstream_sha256",
        "overlay_sha256",
        "staged_diff_sha256",
    ):
        _identity(overlay[field], _DIGEST_LENGTH, field)
    if not isinstance(policy["nested_gitlinks"], list):
        raise Ue4ssSourceStageError(
            "UE4SS source-stage nested gitlink policy is invalid."
        )
    nested_gitlinks: list[tuple[PurePosixPath, str]] = []
    nested_paths: set[str] = set()
    for item in policy["nested_gitlinks"]:
        if not isinstance(item, dict) or set(item) != {"path", "commit"}:
            raise Ue4ssSourceStageError(
                "UE4SS source-stage nested gitlink entry is invalid."
            )
        nested_path = _relative_path(item["path"], "nested gitlink path")
        nested_commit = _identity(
            item["commit"],
            _COMMIT_LENGTH,
            "nested gitlink commit",
        )
        if (
            nested_path.as_posix() in nested_paths
            or nested_path == target
            or nested_path in target.parents
        ):
            raise Ue4ssSourceStageError(
                "UE4SS source-stage nested gitlink paths collide."
            )
        nested_paths.add(nested_path.as_posix())
        nested_gitlinks.append((nested_path, nested_commit))
    if nested_gitlinks != sorted(
        nested_gitlinks,
        key=lambda item: item[0].as_posix(),
    ):
        raise Ue4ssSourceStageError(
            "UE4SS source-stage nested gitlinks are not canonically ordered."
        )
    overlay_path = policy_path.parent / Path(*overlay_source.parts)
    return policy, encoded, target, overlay_path, nested_gitlinks


def _stage_manifest(
    policy: dict[str, Any],
    policy_bytes: bytes,
    target: PurePosixPath,
    nested_gitlinks: list[tuple[PurePosixPath, str]],
) -> tuple[dict[str, Any], bytes]:
    overlay_policy = policy["overlay"]
    assert isinstance(overlay_policy, dict)
    result: dict[str, Any] = {
        "schema_version": 1,
        "owner": "MecchaCamouflage",
        "ue4ss_commit": policy["ue4ss_commit"],
        "policy_sha256": _sha256(policy_bytes),
        "overlay": {
            "target": target.as_posix(),
            "upstream_sha256": overlay_policy["upstream_sha256"],
            "overlay_sha256": overlay_policy["overlay_sha256"],
            "staged_diff_sha256": overlay_policy["staged_diff_sha256"],
        },
        "nested_gitlinks": [
            {
                "path": nested_path.as_posix(),
                "commit": nested_commit,
            }
            for nested_path, nested_commit in nested_gitlinks
        ],
    }
    encoded = (
        json.dumps(
            result,
            ensure_ascii=True,
            indent=2,
            separators=(",", ": "),
        )
        + "\n"
    ).encode("utf-8")
    return result, encoded


def _verify_stage_tree(
    stage_root: Path,
    policy: dict[str, Any],
    target: PurePosixPath,
    overlay: bytes,
    nested_gitlinks: list[tuple[PurePosixPath, str]],
) -> None:
    stage_root = _plain_directory(stage_root, "output root")
    commit = policy["ue4ss_commit"]
    assert isinstance(commit, str)
    if str(_git(stage_root, "rev-parse", "HEAD")).strip() != commit:
        raise Ue4ssSourceStageError(
            "UE4SS source-stage checkout has the wrong commit."
        )
    staged_target = stage_root / Path(*target.parts)
    if _plain_file(
        staged_target,
        _MAXIMUM_OVERLAY_BYTES,
    ) != overlay:
        raise Ue4ssSourceStageError(
            "UE4SS source-stage overlay bytes do not match policy."
        )
    changed = str(
        _git(
            stage_root,
            "diff",
            "--name-only",
            "HEAD",
            "--",
        )
    ).splitlines()
    if changed != [target.as_posix()]:
        raise Ue4ssSourceStageError(
            "UE4SS source stage contains an unexpected tracked change."
        )
    diff = _git(
        stage_root,
        "diff",
        "--binary",
        "HEAD",
        "--",
        target.as_posix(),
        binary=True,
    )
    assert isinstance(diff, bytes)
    overlay_policy = policy["overlay"]
    assert isinstance(overlay_policy, dict)
    staged_diff_sha256 = _sha256(diff)
    if staged_diff_sha256 != overlay_policy["staged_diff_sha256"]:
        raise Ue4ssSourceStageError(
            "UE4SS source-stage diff identity does not match policy: "
            f"{staged_diff_sha256}"
        )
    staged_status = str(
        _git(
            stage_root,
            "status",
            "--porcelain",
            "--untracked-files=all",
            "--ignore-submodules=none",
        )
    ).splitlines()
    if staged_status != [f" M {target.as_posix()}"]:
        raise Ue4ssSourceStageError(
            "UE4SS source stage is not the approved one-file overlay."
        )
    for nested_path, nested_commit in nested_gitlinks:
        nested_stage = _plain_directory(
            stage_root / Path(*nested_path.parts),
            f"staged nested source {nested_path.as_posix()}",
        )
        nested_head = str(_git(nested_stage, "rev-parse", "HEAD")).strip()
        nested_status = str(
            _git(
                nested_stage,
                "status",
                "--porcelain",
                "--untracked-files=all",
            )
        ).strip()
        if nested_head != nested_commit or nested_status:
            raise Ue4ssSourceStageError(
                "UE4SS source-stage nested checkout is not the accepted "
                f"clean commit: {nested_path.as_posix()}"
            )


def verify_ue4ss_source_stage(
    *,
    policy_path: Path,
    output_root: Path,
    manifest_path: Path,
) -> dict[str, Any]:
    policy_path = Path(os.path.abspath(policy_path))
    output_root = Path(os.path.abspath(output_root))
    manifest_path = Path(os.path.abspath(manifest_path))
    (
        policy,
        policy_bytes,
        target,
        overlay_path,
        nested_gitlinks,
    ) = _load_policy(policy_path)
    overlay = _plain_file(overlay_path, _MAXIMUM_OVERLAY_BYTES)
    overlay_policy = policy["overlay"]
    assert isinstance(overlay_policy, dict)
    if _sha256(overlay) != overlay_policy["overlay_sha256"]:
        raise Ue4ssSourceStageError(
            "UE4SS canonical Cargo lock hash does not match policy."
        )
    result, encoded_manifest = _stage_manifest(
        policy,
        policy_bytes,
        target,
        nested_gitlinks,
    )
    existing_manifest = _plain_file(
        manifest_path,
        _MAXIMUM_POLICY_BYTES,
    )
    if existing_manifest != encoded_manifest:
        raise Ue4ssSourceStageError(
            "UE4SS source-stage manifest does not match policy."
        )
    _verify_stage_tree(
        output_root,
        policy,
        target,
        overlay,
        nested_gitlinks,
    )
    return result


def prepare_ue4ss_source_stage(
    *,
    policy_path: Path,
    source_root: Path,
    output_root: Path,
    manifest_path: Path,
) -> dict[str, Any]:
    policy_path = Path(os.path.abspath(policy_path))
    source_root = _plain_directory(source_root, "source root")
    output_root = Path(os.path.abspath(output_root))
    manifest_path = Path(os.path.abspath(manifest_path))
    for publication_path in (output_root, manifest_path):
        try:
            publication_path.relative_to(source_root)
        except ValueError:
            continue
        raise Ue4ssSourceStageError(
            "UE4SS stage output and manifest must remain outside the "
            "accepted source checkout."
        )
    if not output_root.exists() and manifest_path.exists():
        raise Ue4ssSourceStageError(
            "UE4SS source-stage manifest exists without its owned stage."
        )
    (
        policy,
        policy_bytes,
        target,
        overlay_path,
        nested_gitlinks,
    ) = _load_policy(policy_path)
    commit = policy["ue4ss_commit"]
    assert isinstance(commit, str)
    head = str(_git(source_root, "rev-parse", "HEAD")).strip()
    if head != commit:
        raise Ue4ssSourceStageError(
            "UE4SS source checkout does not match the accepted commit."
        )
    source_status = str(
        _git(
            source_root,
            "status",
            "--porcelain",
            "--untracked-files=all",
            "--ignore-submodules=none",
        )
    ).strip()
    if source_status:
        raise Ue4ssSourceStageError(
            "UE4SS source checkout is not clean."
        )
    for nested_path, nested_commit in nested_gitlinks:
        tree_entry = str(
            _git(
                source_root,
                "ls-tree",
                "HEAD",
                "--",
                nested_path.as_posix(),
            )
        ).strip()
        expected_entry = (
            f"160000 commit {nested_commit}\t{nested_path.as_posix()}"
        )
        if tree_entry != expected_entry:
            raise Ue4ssSourceStageError(
                "UE4SS source nested gitlink does not match policy: "
                f"{nested_path.as_posix()}"
            )
        nested_source = _plain_directory(
            source_root / Path(*nested_path.parts),
            f"nested source {nested_path.as_posix()}",
        )
        nested_head = str(_git(nested_source, "rev-parse", "HEAD")).strip()
        nested_status = str(
            _git(
                nested_source,
                "status",
                "--porcelain",
                "--untracked-files=all",
            )
        ).strip()
        if nested_head != nested_commit or nested_status:
            raise Ue4ssSourceStageError(
                "UE4SS initialized nested source is not the accepted clean "
                f"commit: {nested_path.as_posix()}"
            )
    source_target = source_root / Path(*target.parts)
    upstream = _plain_file(source_target, _MAXIMUM_OVERLAY_BYTES)
    overlay = _plain_file(overlay_path, _MAXIMUM_OVERLAY_BYTES)
    overlay_policy = policy["overlay"]
    assert isinstance(overlay_policy, dict)
    if _sha256(upstream) != overlay_policy["upstream_sha256"]:
        raise Ue4ssSourceStageError(
            "UE4SS upstream Cargo lock hash does not match policy."
        )
    if _sha256(overlay) != overlay_policy["overlay_sha256"]:
        raise Ue4ssSourceStageError(
            "UE4SS canonical Cargo lock hash does not match policy."
        )
    result, encoded_manifest = _stage_manifest(
        policy,
        policy_bytes,
        target,
        nested_gitlinks,
    )
    if output_root.exists():
        return verify_ue4ss_source_stage(
            policy_path=policy_path,
            output_root=output_root,
            manifest_path=manifest_path,
        )

    output_root.parent.mkdir(parents=True, exist_ok=True)
    temporary = Path(
        tempfile.mkdtemp(
            prefix=f".{output_root.name}.staging-",
            dir=output_root.parent,
        )
    )
    temporary.rmdir()
    published = False
    try:
        _clone(source_root, temporary, commit)
        for nested_path, nested_commit in nested_gitlinks:
            nested_destination = temporary / Path(*nested_path.parts)
            if nested_destination.exists():
                try:
                    nested_destination.rmdir()
                except OSError as error:
                    raise Ue4ssSourceStageError(
                        "UE4SS source-stage nested destination is not empty: "
                        f"{nested_path.as_posix()}"
                    ) from error
            _clone(
                source_root / Path(*nested_path.parts),
                nested_destination,
                nested_commit,
            )
        staged_target = temporary / Path(*target.parts)
        staged_target.write_bytes(overlay)
        _verify_stage_tree(
            temporary,
            policy,
            target,
            overlay,
            nested_gitlinks,
        )
        os.replace(temporary, output_root)
        published = True
        _atomic_write(manifest_path, encoded_manifest)
        return result
    except BaseException:
        if temporary.exists():
            shutil.rmtree(temporary)
        if published and output_root.exists() and not manifest_path.exists():
            shutil.rmtree(output_root)
        raise


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Prepare an immutable one-overlay UE4SS build source"
    )
    parser.add_argument("--policy", required=True, type=Path)
    parser.add_argument("--source-root", type=Path)
    parser.add_argument("--output-root", required=True, type=Path)
    parser.add_argument("--manifest", required=True, type=Path)
    parser.add_argument("--verify-only", action="store_true")
    return parser


def main(arguments: Iterable[str] | None = None) -> int:
    options = _parser().parse_args(arguments)
    try:
        if options.verify_only:
            result = verify_ue4ss_source_stage(
                policy_path=options.policy,
                output_root=options.output_root,
                manifest_path=options.manifest,
            )
        else:
            if options.source_root is None:
                raise Ue4ssSourceStageError(
                    "--source-root is required when preparing a stage."
                )
            result = prepare_ue4ss_source_stage(
                policy_path=options.policy,
                source_root=options.source_root,
                output_root=options.output_root,
                manifest_path=options.manifest,
            )
    except (Ue4ssSourceStageError, OSError) as error:
        print(f"UE4SS source-stage error: {error}", file=sys.stderr)
        return 1
    print(
        "PASS UE4SS source stage: "
        f"commit={result['ue4ss_commit']}, "
        f"overlay={result['overlay']['target']}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
