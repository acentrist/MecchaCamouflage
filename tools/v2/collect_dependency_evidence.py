#!/usr/bin/env python3
"""Collect deterministic CMake, git, and Cargo dependency evidence."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import stat
import subprocess
import sys
import tempfile
import tomllib
from collections import deque
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Iterable


class DependencyEvidenceError(ValueError):
    pass


@dataclass
class DependencyEvidenceInputs:
    project_root: Path
    build_root: Path
    cargo_root: Path
    reply_directory: Path
    configuration: str
    root_targets: tuple[str, ...]
    cargo_metadata: Path
    cargo_lock: Path
    cargo_root_package: str
    output: Path
    ue4ss_commit: str


_MAXIMUM_JSON_BYTES = 64 * 1024 * 1024
_MAXIMUM_TARGETS = 8192
_MAXIMUM_PACKAGES = 4096
_COMMIT_PATTERN = re.compile(r"[0-9a-f]{40}")
_CHECKSUM_PATTERN = re.compile(r"[0-9a-f]{64}")
_TARGET_TYPES = {
    "EXECUTABLE",
    "STATIC_LIBRARY",
    "SHARED_LIBRARY",
    "MODULE_LIBRARY",
    "OBJECT_LIBRARY",
    "INTERFACE_LIBRARY",
    "UTILITY",
}


def _is_link_or_reparse(path: Path) -> bool:
    try:
        metadata = path.lstat()
    except OSError as error:
        raise DependencyEvidenceError(
            f"Dependency evidence path could not be inspected: {path}"
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
        raise DependencyEvidenceError(
            f"Dependency evidence input is linked/reparse-routed: {path}"
        )
    try:
        metadata = path.stat()
        if (
            not stat.S_ISREG(metadata.st_mode)
            or metadata.st_size == 0
            or metadata.st_size > maximum_size
        ):
            raise DependencyEvidenceError(
                f"Dependency evidence input size is invalid: {path}"
            )
        return path.read_bytes()
    except DependencyEvidenceError:
        raise
    except OSError as error:
        raise DependencyEvidenceError(
            f"Dependency evidence input could not be read: {path}"
        ) from error


def _reject_json_constant(value: str) -> None:
    raise DependencyEvidenceError(
        f"Dependency evidence input has an invalid number: {value}"
    )


def _load_json(path: Path) -> tuple[Any, bytes]:
    encoded = _plain_file(path, _MAXIMUM_JSON_BYTES)

    def reject_duplicates(
        pairs: list[tuple[str, Any]],
    ) -> dict[str, Any]:
        result: dict[str, Any] = {}
        for key, value in pairs:
            if key in result:
                raise DependencyEvidenceError(
                    f"Dependency evidence input has a duplicate key: {key}"
                )
            result[key] = value
        return result

    try:
        return (
            json.loads(
                encoded.decode("utf-8"),
                object_pairs_hook=reject_duplicates,
                parse_constant=_reject_json_constant,
            ),
            encoded,
        )
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise DependencyEvidenceError(
            f"Dependency evidence input is not strict JSON: {path}"
        ) from error


def _plain_directory(path: Path, label: str) -> Path:
    normalized = Path(os.path.abspath(path))
    if _is_link_or_reparse(normalized) or not normalized.is_dir():
        raise DependencyEvidenceError(
            f"Dependency evidence {label} is not a plain directory."
        )
    return normalized


def _relative_to(path: Path, root: Path) -> Path | None:
    try:
        return path.relative_to(root)
    except ValueError:
        return None


def _path_alias(
    path: Path,
    roots: dict[str, Path],
    *,
    allow_cargo: bool,
) -> str:
    normalized = Path(os.path.abspath(path))
    accepted_names = ("project", "build", "cargo") if allow_cargo else (
        "project",
        "build",
    )
    for name in accepted_names:
        relative = _relative_to(normalized, roots[name])
        if relative is None:
            continue
        current = roots[name]
        for segment in relative.parts:
            current /= segment
            if _is_link_or_reparse(current):
                raise DependencyEvidenceError(
                    f"Dependency source path is linked/reparse-routed: "
                    f"{path}"
                )
        suffix = relative.as_posix()
        return name if suffix == "." else f"{name}:{suffix}"
    raise DependencyEvidenceError(
        f"Dependency source path escapes approved roots: {path}"
    )


def _git(
    directory: Path,
    arguments: list[str],
    *,
    binary: bool = False,
    required: bool = True,
) -> str | bytes | None:
    try:
        completed = subprocess.run(
            [
                "git",
                "-c",
                "core.filemode=false",
                "-C",
                str(directory),
                *arguments,
            ],
            check=False,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=not binary,
        )
    except OSError as error:
        raise DependencyEvidenceError(
            "git is unavailable for dependency evidence."
        ) from error
    if completed.returncode != 0:
        if not required:
            return None
        stderr = (
            completed.stderr.decode("utf-8", errors="replace")
            if binary
            else completed.stderr
        )
        raise DependencyEvidenceError(
            f"git dependency inspection failed: {stderr.strip()}"
        )
    return completed.stdout


def _git_identity(
    source: Path,
    roots: dict[str, Path],
) -> tuple[str, str, str] | None:
    raw_root = _git(
        source,
        ["rev-parse", "--show-toplevel"],
        required=False,
    )
    if raw_root is None:
        return None
    git_root = Path(os.path.abspath(str(raw_root).strip()))
    alias = _path_alias(git_root, roots, allow_cargo=True)
    head = str(
        _git(git_root, ["rev-parse", "HEAD"])
    ).strip()
    if not _COMMIT_PATTERN.fullmatch(head):
        raise DependencyEvidenceError(
            f"Dependency git revision is invalid: {alias}"
        )
    tracked_diff = _git(
        git_root,
        ["diff", "--binary", "HEAD", "--"],
        binary=True,
    )
    assert isinstance(tracked_diff, bytes)
    diff_digest = hashlib.sha256(tracked_diff).hexdigest()
    return (
        alias,
        head[:12],
        f"git:{head}:tracked-diff:{diff_digest}",
    )


def _codemodel_object(reply: Path) -> tuple[dict[str, Any], Path]:
    indexes = sorted(reply.glob("index-*.json"))
    if not indexes:
        raise DependencyEvidenceError(
            "CMake File API index reply is missing."
        )
    index, _ = _load_json(indexes[-1])
    if not isinstance(index, dict):
        raise DependencyEvidenceError(
            "CMake File API index root is invalid."
        )
    objects = index.get("objects")
    if not isinstance(objects, list):
        raise DependencyEvidenceError(
            "CMake File API object list is missing."
        )
    candidates = [
        item
        for item in objects
        if (
            isinstance(item, dict)
            and item.get("kind") == "codemodel"
            and isinstance(item.get("version"), dict)
            and item["version"].get("major") == 2
            and isinstance(item.get("jsonFile"), str)
        )
    ]
    if len(candidates) != 1:
        raise DependencyEvidenceError(
            "CMake File API codemodel v2 reply is missing or ambiguous."
        )
    json_file = candidates[0]["jsonFile"]
    if (
        not json_file
        or "/" in json_file
        or "\\" in json_file
        or json_file in {".", ".."}
    ):
        raise DependencyEvidenceError(
            "CMake File API codemodel filename is invalid."
        )
    codemodel, _ = _load_json(reply / json_file)
    if not isinstance(codemodel, dict):
        raise DependencyEvidenceError(
            "CMake File API codemodel root is invalid."
        )
    return codemodel, reply / json_file


def _collect_target_graph(
    inputs: DependencyEvidenceInputs,
    roots: dict[str, Path],
) -> tuple[list[str], list[dict[str, Any]], list[Path]]:
    codemodel, _ = _codemodel_object(inputs.reply_directory)
    version = codemodel.get("version")
    if (
        codemodel.get("kind") != "codemodel"
        or not isinstance(version, dict)
        or version.get("major") != 2
        or not isinstance(codemodel.get("configurations"), list)
    ):
        raise DependencyEvidenceError(
            "CMake File API codemodel version is unsupported."
        )
    configurations = [
        configuration
        for configuration in codemodel["configurations"]
        if (
            isinstance(configuration, dict)
            and configuration.get("name") == inputs.configuration
        )
    ]
    if len(configurations) != 1:
        raise DependencyEvidenceError(
            f"CMake configuration is missing or ambiguous: "
            f"{inputs.configuration}"
        )
    summaries = configurations[0].get("targets")
    if (
        not isinstance(summaries, list)
        or not summaries
        or len(summaries) > _MAXIMUM_TARGETS
    ):
        raise DependencyEvidenceError(
            "CMake target summary is invalid."
        )
    targets_by_id: dict[str, dict[str, Any]] = {}
    keys_by_id: dict[str, str] = {}
    sources_by_id: dict[str, Path] = {}
    for summary in summaries:
        if not isinstance(summary, dict):
            raise DependencyEvidenceError(
                "CMake target summary entry is invalid."
            )
        target_id = summary.get("id")
        json_file = summary.get("jsonFile")
        if (
            not isinstance(target_id, str)
            or not target_id
            or target_id in targets_by_id
            or not isinstance(json_file, str)
            or not json_file
            or "/" in json_file
            or "\\" in json_file
        ):
            raise DependencyEvidenceError(
                "CMake target identity is invalid."
            )
        target, _ = _load_json(inputs.reply_directory / json_file)
        if not isinstance(target, dict) or target.get("id") != target_id:
            raise DependencyEvidenceError(
                f"CMake target document is invalid: {target_id}"
            )
        name = target.get("name")
        target_type = target.get("type")
        paths = target.get("paths")
        if (
            not isinstance(name, str)
            or not name
            or not isinstance(target_type, str)
            or target_type not in _TARGET_TYPES
            or not isinstance(paths, dict)
            or not isinstance(paths.get("source"), str)
        ):
            raise DependencyEvidenceError(
                f"CMake target contract is invalid: {target_id}"
            )
        source = Path(os.path.abspath(paths["source"]))
        if not source.is_dir():
            raise DependencyEvidenceError(
                f"CMake target source is unavailable: {name}"
            )
        source_alias = _path_alias(
            source,
            roots,
            allow_cargo=False,
        )
        key = f"{name}@{source_alias}"
        if key in keys_by_id.values():
            raise DependencyEvidenceError(
                f"CMake target key is duplicated: {key}"
            )
        targets_by_id[target_id] = target
        keys_by_id[target_id] = key
        sources_by_id[target_id] = source

    root_ids: list[str] = []
    for root_name in inputs.root_targets:
        matches = [
            target_id
            for target_id, target in targets_by_id.items()
            if target.get("name") == root_name
        ]
        if len(matches) != 1:
            raise DependencyEvidenceError(
                f"CMake root target is missing or ambiguous: {root_name}"
            )
        root_ids.append(matches[0])

    reachable: set[str] = set()
    queue: deque[str] = deque(root_ids)
    while queue:
        target_id = queue.popleft()
        if target_id in reachable:
            continue
        target = targets_by_id.get(target_id)
        if target is None:
            raise DependencyEvidenceError(
                f"CMake target dependency is missing: {target_id}"
            )
        reachable.add(target_id)
        dependencies = target.get("dependencies", [])
        if not isinstance(dependencies, list):
            raise DependencyEvidenceError(
                f"CMake target dependencies are invalid: {target_id}"
            )
        for dependency in dependencies:
            if (
                not isinstance(dependency, dict)
                or not isinstance(dependency.get("id"), str)
                or dependency["id"] not in targets_by_id
            ):
                raise DependencyEvidenceError(
                    f"CMake target dependency is missing: {target_id}"
                )
            queue.append(dependency["id"])

    graph: list[dict[str, Any]] = []
    for target_id in reachable:
        target = targets_by_id[target_id]
        dependency_keys = sorted(
            {
                keys_by_id[dependency["id"]]
                for dependency in target.get("dependencies", [])
            },
            key=str.lower,
        )
        graph.append(
            {
                "key": keys_by_id[target_id],
                "name": target["name"],
                "type": target["type"],
                "source": keys_by_id[target_id].split("@", 1)[1],
                "dependencies": dependency_keys,
            }
        )
    graph.sort(key=lambda item: item["key"].lower())
    root_keys = sorted(
        {keys_by_id[target_id] for target_id in root_ids},
        key=str.lower,
    )
    sources = sorted(
        {sources_by_id[target_id] for target_id in reachable},
        key=lambda path: str(path).lower(),
    )
    return root_keys, graph, sources


def _cmake_components(
    graph: list[dict[str, Any]],
    sources: list[Path],
    roots: dict[str, Path],
) -> list[dict[str, str]]:
    components: dict[str, dict[str, str]] = {}
    graph_by_source: dict[str, list[dict[str, Any]]] = {}
    for target in graph:
        graph_by_source.setdefault(target["source"], []).append(target)
    for source in sources:
        source_alias = _path_alias(
            source,
            roots,
            allow_cargo=False,
        )
        identity = _git_identity(source, roots)
        if identity is not None:
            git_alias, version, source_identity = identity
            if git_alias == "project":
                continue
            name = f"git:{git_alias}"
        else:
            version = "unversioned"
            name = f"cmake:{source_alias}"
            source_identity = (
                "cmake-targets:"
                + hashlib.sha256(
                    json.dumps(
                        graph_by_source[source_alias],
                        sort_keys=True,
                        separators=(",", ":"),
                    ).encode("utf-8")
                ).hexdigest()
            )
        key = name.lower()
        candidate = {
            "name": name,
            "version": version,
            "source_identity": source_identity,
        }
        existing = components.get(key)
        if existing is not None and existing != candidate:
            raise DependencyEvidenceError(
                f"CMake dependency component is ambiguous: {name}"
            )
        components[key] = candidate
    return list(components.values())


def _cargo_lock_packages(
    path: Path,
) -> dict[tuple[str, str, str | None], dict[str, Any]]:
    encoded = _plain_file(path, _MAXIMUM_JSON_BYTES)
    try:
        lock = tomllib.loads(encoded.decode("utf-8"))
    except (UnicodeDecodeError, tomllib.TOMLDecodeError) as error:
        raise DependencyEvidenceError(
            "Cargo.lock is not valid UTF-8 TOML."
        ) from error
    packages = lock.get("package")
    if (
        not isinstance(packages, list)
        or not packages
        or len(packages) > _MAXIMUM_PACKAGES
    ):
        raise DependencyEvidenceError(
            "Cargo.lock package inventory is invalid."
        )
    result: dict[tuple[str, str, str | None], dict[str, Any]] = {}
    for package in packages:
        if not isinstance(package, dict):
            raise DependencyEvidenceError(
                "Cargo.lock package entry is invalid."
            )
        name = package.get("name")
        version = package.get("version")
        source = package.get("source")
        if (
            not isinstance(name, str)
            or not name
            or not isinstance(version, str)
            or not version
            or (source is not None and not isinstance(source, str))
        ):
            raise DependencyEvidenceError(
                "Cargo.lock package identity is invalid."
            )
        key = (name, version, source)
        if key in result:
            raise DependencyEvidenceError(
                f"Cargo.lock package is duplicated: {name} {version}"
            )
        result[key] = package
    return result


def _cargo_components(
    inputs: DependencyEvidenceInputs,
    roots: dict[str, Path],
) -> list[dict[str, str]]:
    metadata, _ = _load_json(inputs.cargo_metadata)
    if not isinstance(metadata, dict):
        raise DependencyEvidenceError(
            "Cargo metadata root is invalid."
        )
    packages = metadata.get("packages")
    resolve = metadata.get("resolve")
    if (
        not isinstance(packages, list)
        or not packages
        or len(packages) > _MAXIMUM_PACKAGES
        or not isinstance(resolve, dict)
        or not isinstance(resolve.get("nodes"), list)
    ):
        raise DependencyEvidenceError(
            "Cargo metadata package/resolve graph is invalid."
        )
    packages_by_id: dict[str, dict[str, Any]] = {}
    for package in packages:
        if (
            not isinstance(package, dict)
            or not isinstance(package.get("id"), str)
            or package["id"] in packages_by_id
        ):
            raise DependencyEvidenceError(
                "Cargo metadata package identity is invalid."
            )
        packages_by_id[package["id"]] = package
    root_packages = [
        package
        for package in packages
        if package.get("name") == inputs.cargo_root_package
    ]
    if len(root_packages) != 1:
        raise DependencyEvidenceError(
            "Cargo root package is missing or ambiguous."
        )
    root_id = root_packages[0]["id"]
    if resolve.get("root") not in {None, root_id}:
        raise DependencyEvidenceError(
            "Cargo resolve root does not match the requested package."
        )
    nodes: dict[str, dict[str, Any]] = {}
    for node in resolve["nodes"]:
        if (
            not isinstance(node, dict)
            or not isinstance(node.get("id"), str)
            or node["id"] in nodes
        ):
            raise DependencyEvidenceError(
                "Cargo resolve node is invalid."
            )
        nodes[node["id"]] = node
    reachable: set[str] = set()
    queue: deque[str] = deque([root_id])
    while queue:
        package_id = queue.popleft()
        if package_id in reachable:
            continue
        package = packages_by_id.get(package_id)
        node = nodes.get(package_id)
        if package is None or node is None:
            raise DependencyEvidenceError(
                "Cargo resolve graph is not closed."
            )
        reachable.add(package_id)
        dependencies = node.get("deps", [])
        if not isinstance(dependencies, list):
            raise DependencyEvidenceError(
                "Cargo resolve dependencies are invalid."
            )
        for dependency in dependencies:
            if (
                not isinstance(dependency, dict)
                or not isinstance(dependency.get("pkg"), str)
            ):
                raise DependencyEvidenceError(
                    "Cargo resolve dependency is invalid."
                )
            queue.append(dependency["pkg"])

    lock_packages = _cargo_lock_packages(inputs.cargo_lock)
    components: list[dict[str, str]] = []
    names: set[str] = set()
    for package_id in reachable:
        package = packages_by_id[package_id]
        node = nodes[package_id]
        name = package.get("name")
        version = package.get("version")
        source = package.get("source")
        manifest_path = package.get("manifest_path")
        if (
            not isinstance(name, str)
            or not name
            or not isinstance(version, str)
            or not version
            or (source is not None and not isinstance(source, str))
            or not isinstance(manifest_path, str)
        ):
            raise DependencyEvidenceError(
                "Cargo package contract is invalid."
            )
        lock_package = lock_packages.get((name, version, source))
        if lock_package is None:
            raise DependencyEvidenceError(
                f"Cargo package is absent from Cargo.lock: "
                f"{name} {version}"
            )
        manifest = Path(os.path.abspath(manifest_path))
        _plain_file(manifest, _MAXIMUM_JSON_BYTES)
        raw_features = node.get("features", [])
        if (
            not isinstance(raw_features, list)
            or any(
                not isinstance(feature, str) or not feature
                for feature in raw_features
            )
        ):
            raise DependencyEvidenceError(
                f"Cargo resolved features are invalid: {name}"
            )
        feature_identity = ",".join(
            sorted(set(raw_features))
        ) or "none"
        if source is None:
            alias = _path_alias(
                manifest.parent,
                roots,
                allow_cargo=False,
            )
            identity = _git_identity(manifest.parent, roots)
            if identity is None:
                source_identity = f"path:{alias}"
            else:
                _git_alias, _git_version, git_identity = identity
                source_identity = f"path:{alias}@{git_identity}"
        elif source.startswith("registry+"):
            checksum = lock_package.get("checksum")
            if (
                not isinstance(checksum, str)
                or not _CHECKSUM_PATTERN.fullmatch(checksum)
            ):
                raise DependencyEvidenceError(
                    f"Cargo registry checksum is invalid: {name}"
                )
            _path_alias(manifest, roots, allow_cargo=True)
            checksum_file = manifest.parent / ".cargo-checksum.json"
            checksum_data, _ = _load_json(checksum_file)
            if (
                not isinstance(checksum_data, dict)
                or checksum_data.get("package") != checksum
            ):
                raise DependencyEvidenceError(
                    f"Cargo registry checksum evidence changed: {name}"
                )
            source_identity = f"cargo:{checksum}"
        elif source.startswith("git+"):
            alias = _path_alias(
                manifest.parent,
                roots,
                allow_cargo=True,
            )
            identity = _git_identity(manifest.parent, roots)
            if identity is None:
                raise DependencyEvidenceError(
                    f"Cargo git package has no repository: {name}"
                )
            source_identity = (
                f"cargo-git:{source}@{alias}@{identity[2]}"
            )
        else:
            raise DependencyEvidenceError(
                f"Cargo source kind is unsupported: {source}"
            )
        source_identity += f":features:{feature_identity}"
        component_name = f"cargo:{name}@{version}"
        key = component_name.lower()
        if key in names:
            suffix = hashlib.sha256(
                source_identity.encode("utf-8")
            ).hexdigest()[:12]
            component_name += f"#{suffix}"
            key = component_name.lower()
        if key in names:
            raise DependencyEvidenceError(
                f"Cargo component identity is ambiguous: {name}"
            )
        names.add(key)
        components.append(
            {
                "name": component_name,
                "version": version,
                "source_identity": source_identity,
            }
        )
    return components


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


def collect_dependency_evidence(
    inputs: DependencyEvidenceInputs,
) -> dict[str, Any]:
    if inputs.configuration != "Game__Shipping__Win64":
        raise DependencyEvidenceError(
            "Dependency evidence configuration is invalid."
        )
    if (
        not _COMMIT_PATTERN.fullmatch(inputs.ue4ss_commit)
        or not inputs.root_targets
        or len(set(inputs.root_targets)) != len(inputs.root_targets)
        or not inputs.cargo_root_package
    ):
        raise DependencyEvidenceError(
            "Dependency evidence collection identity is invalid."
        )
    roots = {
        "project": _plain_directory(
            inputs.project_root,
            "project root",
        ),
        "build": _plain_directory(inputs.build_root, "build root"),
        "cargo": _plain_directory(inputs.cargo_root, "Cargo root"),
    }
    inputs.reply_directory = _plain_directory(
        inputs.reply_directory,
        "CMake File API reply",
    )
    inputs.cargo_metadata = Path(
        os.path.abspath(inputs.cargo_metadata)
    )
    inputs.cargo_lock = Path(os.path.abspath(inputs.cargo_lock))
    inputs.output = Path(os.path.abspath(inputs.output))

    root_targets, target_graph, sources = _collect_target_graph(
        inputs,
        roots,
    )
    components = _cmake_components(
        target_graph,
        sources,
        roots,
    )
    components.extend(_cargo_components(inputs, roots))
    components.sort(key=lambda component: component["name"].lower())
    if len(
        {component["name"].lower() for component in components}
    ) != len(components):
        raise DependencyEvidenceError(
            "Resolved dependency component names collide."
        )
    evidence: dict[str, Any] = {
        "schema_version": 1,
        "ue4ss_commit": inputs.ue4ss_commit,
        "configuration": inputs.configuration,
        "root_targets": root_targets,
        "target_graph": target_graph,
        "components": components,
    }
    encoded = (
        json.dumps(
            evidence,
            ensure_ascii=True,
            indent=2,
            separators=(",", ": "),
        )
        + "\n"
    ).encode("utf-8")
    try:
        _atomic_write(inputs.output, encoded)
    except OSError as error:
        raise DependencyEvidenceError(
            f"Dependency evidence publication failed: {error}"
        ) from error
    return evidence


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Collect exact CMake/git/Cargo dependency evidence"
    )
    parser.add_argument("--project-root", required=True, type=Path)
    parser.add_argument("--build-root", required=True, type=Path)
    parser.add_argument("--cargo-root", required=True, type=Path)
    parser.add_argument(
        "--reply-directory",
        required=True,
        type=Path,
    )
    parser.add_argument("--configuration", required=True)
    parser.add_argument(
        "--root-target",
        action="append",
        required=True,
        dest="root_targets",
    )
    parser.add_argument("--cargo-metadata", required=True, type=Path)
    parser.add_argument("--cargo-lock", required=True, type=Path)
    parser.add_argument("--cargo-root-package", required=True)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--ue4ss-commit", required=True)
    return parser


def main(arguments: Iterable[str] | None = None) -> int:
    options = _parser().parse_args(arguments)
    try:
        evidence = collect_dependency_evidence(
            DependencyEvidenceInputs(
                project_root=options.project_root,
                build_root=options.build_root,
                cargo_root=options.cargo_root,
                reply_directory=options.reply_directory,
                configuration=options.configuration,
                root_targets=tuple(options.root_targets),
                cargo_metadata=options.cargo_metadata,
                cargo_lock=options.cargo_lock,
                cargo_root_package=options.cargo_root_package,
                output=options.output,
                ue4ss_commit=options.ue4ss_commit,
            )
        )
    except DependencyEvidenceError as error:
        print(f"dependency evidence error: {error}", file=sys.stderr)
        return 1
    print(
        "PASS dependency evidence: "
        f"{len(evidence['target_graph'])} CMake targets, "
        f"{len(evidence['components'])} components"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
