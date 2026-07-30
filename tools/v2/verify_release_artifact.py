#!/usr/bin/env python3
"""Verify the exact native MecchaCamouflage v2 release artifact."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import stat
import struct
import sys
import tempfile
import xml.etree.ElementTree as ElementTree
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Iterable


class ReleaseArtifactError(ValueError):
    pass


@dataclass
class ReleaseArtifactInputs:
    artifact_directory: Path
    executable_name: str
    payload_manifest: Path
    payload_cab: Path
    payload_layout: Path
    provenance_report: Path
    report_output: Path
    checksum_output: Path
    product_version: str
    ue4ss_commit: str
    signing_policy: str


_MAXIMUM_BINARY_BYTES = 1024 * 1024 * 1024
_MAXIMUM_METADATA_BYTES = 4 * 1024 * 1024
_MAXIMUM_CAB_BYTES = 768 * 1024 * 1024
_VERSION_PATTERN = re.compile(
    r"[0-9]+\.[0-9]+\.[0-9]+(?:-[0-9A-Za-z.-]+)?"
)
_COMMIT_PATTERN = re.compile(r"[0-9a-f]{40}")
_SHA256_PATTERN = re.compile(r"[0-9a-f]{64}")
_IMPORT_PATTERN = re.compile(r"[A-Za-z0-9._-]+\.dll")
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
_ALLOWED_IMPORTS = {
    "advapi32.dll",
    "bcrypt.dll",
    "comctl32.dll",
    "kernel32.dll",
    "msvcp140.dll",
    "ole32.dll",
    "setupapi.dll",
    "shell32.dll",
    "vcruntime140.dll",
    "vcruntime140_1.dll",
    "api-ms-win-crt-filesystem-l1-1-0.dll",
    "api-ms-win-crt-heap-l1-1-0.dll",
    "api-ms-win-crt-locale-l1-1-0.dll",
    "api-ms-win-crt-math-l1-1-0.dll",
    "api-ms-win-crt-runtime-l1-1-0.dll",
    "api-ms-win-crt-stdio-l1-1-0.dll",
    "api-ms-win-crt-string-l1-1-0.dll",
}
_REQUIRED_IMPORTS = {
    "bcrypt.dll",
    "comctl32.dll",
    "kernel32.dll",
    "setupapi.dll",
    "shell32.dll",
    "vcruntime140.dll",
}
_FORBIDDEN_BINARY_TOKENS = (
    "webview2loader.dll",
    "system.windows.forms",
    "bridgeclient",
    "runtimebridgeservice",
    "bridgestartv1",
    "minhook",
    "d3d11.dll",
    "d3d12.dll",
    "dxgi.dll",
    "winhttp.dll",
    "wininet.dll",
    "ws2_32.dll",
)
_REQUIRED_PAYLOAD_ROLES = {
    "dwmapi.dll": "proxy",
    "UE4SS.dll": "runtime",
    "UE4SS-settings.ini": "config",
    "MemberVariableLayout.ini": "config",
    "Mods/MecchaCamouflage/dlls/main.dll": "mod",
    "Mods/MecchaCamouflage/enabled.txt": "config",
    "Mods/MecchaCamouflage/resources/localization/catalog.json":
        "localization",
    "Licenses/MecchaCamouflage-LICENSE.txt": "license",
    "Licenses/UE4SS-LICENSE.txt": "license",
    "Licenses/libwebp-COPYING.txt": "license",
    "Licenses/D-DIN-OFL.txt": "license",
    "Licenses/THIRD-PARTY-NOTICES.txt": "license",
}
_REQUIRED_ROLE_COUNTS = {
    "proxy": 1,
    "runtime": 1,
    "mod": 1,
    "localization": 1,
    "profile": 1,
    "font": 1,
    "license": 5,
}
_WINDOWS_10_SUPPORTED_OS = (
    "{8e0f7a12-bfb3-4fe8-b9a5-48fd50a15a9a}"
)
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
        raise ReleaseArtifactError(
            f"Release path could not be inspected: {path}"
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


def _plain_file(
    path: Path,
    *,
    maximum_size: int,
    allow_empty: bool = False,
) -> bytes:
    if _is_link_or_reparse(path):
        raise ReleaseArtifactError(
            f"Release input is a link or reparse point: {path}"
        )
    try:
        metadata = path.stat()
        if not stat.S_ISREG(metadata.st_mode):
            raise ReleaseArtifactError(
                f"Release input is not a regular file: {path}"
            )
        if (
            (not allow_empty and metadata.st_size == 0)
            or metadata.st_size > maximum_size
        ):
            raise ReleaseArtifactError(
                f"Release input size is outside the supported range: {path}"
            )
        return path.read_bytes()
    except ReleaseArtifactError:
        raise
    except OSError as error:
        raise ReleaseArtifactError(
            f"Release input could not be read: {path}"
        ) from error


def _reject_json_constant(value: str) -> None:
    raise ReleaseArtifactError(
        f"Release metadata contains an invalid number: {value}"
    )


def _load_json(path: Path) -> tuple[dict[str, Any], bytes]:
    encoded = _plain_file(
        path,
        maximum_size=_MAXIMUM_METADATA_BYTES,
    )

    def reject_duplicate_keys(
        pairs: list[tuple[str, Any]],
    ) -> dict[str, Any]:
        result: dict[str, Any] = {}
        for key, value in pairs:
            if key in result:
                raise ReleaseArtifactError(
                    f"Release metadata has a duplicate JSON key: {key}"
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
        raise ReleaseArtifactError(
            f"Release metadata is not strict UTF-8 JSON: {path}"
        ) from error
    if not isinstance(value, dict):
        raise ReleaseArtifactError(
            f"Release metadata root is not an object: {path}"
        )
    return value, encoded


def _canonical_relative_path(value: object) -> str:
    if (
        not isinstance(value, str)
        or not value
        or len(value) > 1024
        or not value.isascii()
        or value.startswith("/")
        or value.endswith("/")
        or "\\" in value
        or ":" in value
    ):
        raise ReleaseArtifactError(
            f"Payload path is not canonical: {value!r}"
        )
    for segment in value.split("/"):
        if (
            not segment
            or segment in {".", ".."}
            or segment.startswith(" ")
            or segment.endswith((" ", "."))
            or any(character in '<>"|?*' for character in segment)
            or any(ord(character) < 0x20 for character in segment)
        ):
            raise ReleaseArtifactError(
                f"Payload path is not canonical: {value}"
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
            raise ReleaseArtifactError(
                f"Payload path uses a reserved Windows device: {value}"
            )
    return value


def _validate_layout_and_manifest(
    layout: dict[str, Any],
    manifest: dict[str, Any],
    *,
    product_version: str,
    ue4ss_commit: str,
) -> dict[str, dict[str, Any]]:
    if list(layout) != ["schema_version", "generated_paths", "files"]:
        raise ReleaseArtifactError(
            "Payload layout keys or canonical order are invalid."
        )
    if (
        type(layout["schema_version"]) is not int
        or layout["schema_version"] != 1
        or not isinstance(layout["generated_paths"], list)
        or not isinstance(layout["files"], list)
        or not layout["files"]
        or len(layout["generated_paths"]) > 64
        or len(layout["files"]) > 4096
    ):
        raise ReleaseArtifactError("Payload layout schema is invalid.")
    if list(manifest) != [
        "schema_version",
        "product_version",
        "ue4ss_commit",
        "generated_paths",
        "files",
    ]:
        raise ReleaseArtifactError(
            "Payload manifest keys or canonical order are invalid."
        )
    if (
        type(manifest["schema_version"]) is not int
        or manifest["schema_version"] != 1
        or manifest["product_version"] != product_version
        or manifest["ue4ss_commit"] != ue4ss_commit
        or not isinstance(manifest["generated_paths"], list)
        or not isinstance(manifest["files"], list)
        or len(manifest["generated_paths"]) > 64
        or not manifest["files"]
        or len(manifest["files"]) > 4096
    ):
        raise ReleaseArtifactError(
            "Payload manifest identity or schema is invalid."
        )

    layout_generated = [
        _canonical_relative_path(path)
        for path in layout["generated_paths"]
    ]
    manifest_generated = [
        _canonical_relative_path(path)
        for path in manifest["generated_paths"]
    ]
    if (
        len({path.lower() for path in layout_generated})
        != len(layout_generated)
        or manifest_generated
        != sorted(layout_generated, key=str.lower)
    ):
        raise ReleaseArtifactError(
            "Payload generated paths do not match the layout."
        )

    layout_files: dict[str, tuple[str, str]] = {}
    for entry in layout["files"]:
        if (
            not isinstance(entry, dict)
            or list(entry) != ["path", "role"]
        ):
            raise ReleaseArtifactError(
                "Payload layout file entry is invalid."
            )
        path = _canonical_relative_path(entry["path"])
        role = entry["role"]
        if not isinstance(role, str) or role not in _ROLES:
            raise ReleaseArtifactError(
                f"Payload layout role is invalid: {role!r}"
            )
        if path.lower() in layout_files:
            raise ReleaseArtifactError(
                f"Payload layout path is duplicated: {path}"
            )
        layout_files[path.lower()] = (path, role)
    if list(layout_files) != sorted(layout_files):
        raise ReleaseArtifactError(
            "Payload layout files are not in canonical order."
        )

    manifest_files: dict[str, dict[str, Any]] = {}
    previous_key = ""
    for entry in manifest["files"]:
        if (
            not isinstance(entry, dict)
            or list(entry) != ["path", "role", "size", "sha256"]
        ):
            raise ReleaseArtifactError(
                "Payload manifest file entry is invalid."
            )
        path = _canonical_relative_path(entry["path"])
        role = entry["role"]
        size = entry["size"]
        digest = entry["sha256"]
        key = path.lower()
        if (
            not isinstance(role, str)
            or role not in _ROLES
            or type(size) is not int
            or size < 0
            or not isinstance(digest, str)
            or not _SHA256_PATTERN.fullmatch(digest)
            or key in manifest_files
            or (previous_key and key < previous_key)
        ):
            raise ReleaseArtifactError(
                f"Payload manifest entry is invalid: {path}"
            )
        previous_key = key
        manifest_files[key] = entry
    if {
        key: (entry["path"], entry["role"])
        for key, entry in manifest_files.items()
    } != layout_files:
        raise ReleaseArtifactError(
            "Payload manifest files do not match the canonical layout."
        )

    role_counts: dict[str, int] = {}
    for entry in manifest_files.values():
        role_counts[entry["role"]] = (
            role_counts.get(entry["role"], 0) + 1
        )
        suffix = Path(entry["path"]).suffix.lower()
        if suffix in {".pdb", ".ilk", ".exp", ".lib"}:
            raise ReleaseArtifactError(
                f"Payload contains a forbidden build artifact: "
                f"{entry['path']}"
            )
    for path, expected_role in _REQUIRED_PAYLOAD_ROLES.items():
        entry = manifest_files.get(path.lower())
        if entry is None or entry["role"] != expected_role:
            raise ReleaseArtifactError(
                f"Payload is missing required {expected_role}: {path}"
            )
    for role, minimum in _REQUIRED_ROLE_COUNTS.items():
        if role_counts.get(role, 0) < minimum:
            raise ReleaseArtifactError(
                f"Payload does not contain the required {role} inventory."
            )
    return manifest_files


@dataclass(frozen=True)
class _Section:
    name: str
    virtual_size: int
    virtual_address: int
    raw_size: int
    raw_offset: int


class _PeImage:
    def __init__(self, encoded: bytes) -> None:
        self.encoded = encoded
        self.machine = 0
        self.subsystem = 0
        self.dll_characteristics = 0
        self.symbol_table_pointer = 0
        self.symbol_count = 0
        self.directories: list[tuple[int, int]] = []
        self.sections: list[_Section] = []
        self._size_of_headers = 0
        self._parse_headers()

    def _range(self, offset: int, size: int, label: str) -> memoryview:
        if (
            offset < 0
            or size < 0
            or offset > len(self.encoded)
            or size > len(self.encoded) - offset
        ):
            raise ReleaseArtifactError(
                f"PE {label} is outside the binary."
            )
        return memoryview(self.encoded)[offset:offset + size]

    def _unpack(
        self,
        format_string: str,
        offset: int,
        label: str,
    ) -> tuple[Any, ...]:
        size = struct.calcsize(format_string)
        self._range(offset, size, label)
        return struct.unpack_from(format_string, self.encoded, offset)

    def _parse_headers(self) -> None:
        if len(self.encoded) < 0x40 or self.encoded[:2] != b"MZ":
            raise ReleaseArtifactError("Release executable is not a PE file.")
        pe_offset = self._unpack(
            "<I", 0x3C, "DOS header"
        )[0]
        if bytes(self._range(pe_offset, 4, "signature")) != b"PE\0\0":
            raise ReleaseArtifactError("Release PE signature is invalid.")
        (
            self.machine,
            section_count,
            _timestamp,
            self.symbol_table_pointer,
            self.symbol_count,
            optional_size,
            characteristics,
        ) = self._unpack(
            "<HHIIIHH",
            pe_offset + 4,
            "COFF header",
        )
        if (
            section_count == 0
            or section_count > 96
            or optional_size < 240
            or not characteristics & 0x0002
        ):
            raise ReleaseArtifactError("Release PE header is invalid.")
        optional = pe_offset + 24
        if self._unpack("<H", optional, "optional header")[0] != 0x20B:
            raise ReleaseArtifactError(
                "Release executable is not PE32+."
            )
        self._size_of_headers = self._unpack(
            "<I", optional + 60, "header size"
        )[0]
        self.subsystem = self._unpack(
            "<H", optional + 68, "subsystem"
        )[0]
        self.dll_characteristics = self._unpack(
            "<H", optional + 70, "DLL characteristics"
        )[0]
        directory_count = self._unpack(
            "<I", optional + 108, "directory count"
        )[0]
        if directory_count < 16:
            raise ReleaseArtifactError(
                "Release PE data-directory table is incomplete."
            )
        self.directories = [
            self._unpack(
                "<II",
                optional + 112 + index * 8,
                "data directory",
            )
            for index in range(16)
        ]
        section_table = optional + optional_size
        seen_names: set[str] = set()
        for index in range(section_count):
            offset = section_table + index * 40
            raw_name = bytes(
                self._range(offset, 8, "section name")
            ).split(b"\0", 1)[0]
            try:
                name = raw_name.decode("ascii")
            except UnicodeDecodeError as error:
                raise ReleaseArtifactError(
                    "Release PE section name is invalid."
                ) from error
            (
                virtual_size,
                virtual_address,
                raw_size,
                raw_offset,
            ) = self._unpack(
                "<IIII",
                offset + 8,
                "section header",
            )
            if not name or name in seen_names:
                raise ReleaseArtifactError(
                    "Release PE sections are ambiguous."
                )
            seen_names.add(name)
            if raw_size:
                self._range(raw_offset, raw_size, "section data")
            self.sections.append(
                _Section(
                    name,
                    virtual_size,
                    virtual_address,
                    raw_size,
                    raw_offset,
                )
            )

    def rva_offset(self, rva: int, size: int, label: str) -> int:
        if rva < self._size_of_headers:
            self._range(rva, size, label)
            return rva
        for section in self.sections:
            span = max(section.virtual_size, section.raw_size)
            if (
                rva >= section.virtual_address
                and rva - section.virtual_address <= span
            ):
                relative = rva - section.virtual_address
                if relative > section.raw_size:
                    break
                offset = section.raw_offset + relative
                if size > section.raw_size - relative:
                    break
                self._range(offset, size, label)
                return offset
        raise ReleaseArtifactError(
            f"PE {label} RVA is not backed by file data."
        )

    def c_string(self, rva: int, label: str) -> str:
        offset = self.rva_offset(rva, 1, label)
        end = self.encoded.find(b"\0", offset, min(len(self.encoded), offset + 260))
        if end < 0:
            raise ReleaseArtifactError(
                f"PE {label} is not a bounded string."
            )
        try:
            return self.encoded[offset:end].decode("ascii")
        except UnicodeDecodeError as error:
            raise ReleaseArtifactError(
                f"PE {label} is not ASCII."
            ) from error

    def imports(self) -> list[str]:
        rva, size = self.directories[1]
        if not rva or not size or size > 64 * 1024:
            raise ReleaseArtifactError(
                "Release PE import directory is invalid."
            )
        offset = self.rva_offset(rva, size, "import directory")
        imports: list[str] = []
        descriptor_count = size // 20
        if descriptor_count < 2 or size % 20:
            raise ReleaseArtifactError(
                "Release PE import directory size is invalid."
            )
        for index in range(min(descriptor_count, 257)):
            descriptor = self._unpack(
                "<IIIII",
                offset + index * 20,
                "import descriptor",
            )
            if descriptor == (0, 0, 0, 0, 0):
                break
            name = self.c_string(descriptor[3], "import name")
            if not _IMPORT_PATTERN.fullmatch(name):
                raise ReleaseArtifactError(
                    f"Release dependency name is invalid: {name!r}"
                )
            imports.append(name.lower())
        else:
            raise ReleaseArtifactError(
                "Release PE import count exceeds the bound."
            )
        if not imports or len(set(imports)) != len(imports):
            raise ReleaseArtifactError(
                "Release PE dependencies are empty or duplicated."
            )
        return sorted(imports)

    def debug_types(self) -> list[int]:
        rva, size = self.directories[6]
        if not rva and not size:
            return []
        if not rva or not size or size % 28 or size > 28 * 64:
            raise ReleaseArtifactError(
                "Release PE debug directory is invalid."
            )
        offset = self.rva_offset(rva, size, "debug directory")
        return [
            self._unpack(
                "<IIHHIIII",
                offset + index,
                "debug entry",
            )[4]
            for index in range(0, size, 28)
        ]

    def resources(self) -> dict[tuple[object, ...], bytes]:
        root_rva, size = self.directories[2]
        if not root_rva or not size or size > len(self.encoded):
            raise ReleaseArtifactError(
                "Release PE resource directory is invalid."
            )
        root_offset = self.rva_offset(
            root_rva,
            size,
            "resource directory",
        )
        resources: dict[tuple[object, ...], bytes] = {}
        active: set[int] = set()

        def resource_name(value: int) -> object:
            if not value & 0x80000000:
                return value & 0xFFFF
            relative = value & 0x7FFFFFFF
            if relative > size - 2:
                raise ReleaseArtifactError(
                    "PE resource name offset is invalid."
                )
            length = self._unpack(
                "<H",
                root_offset + relative,
                "resource name",
            )[0]
            encoded_name = bytes(
                self._range(
                    root_offset + relative + 2,
                    length * 2,
                    "resource name",
                )
            )
            if relative + 2 + length * 2 > size:
                raise ReleaseArtifactError(
                    "PE resource name extends outside the directory."
                )
            try:
                return encoded_name.decode("utf-16-le")
            except UnicodeDecodeError as error:
                raise ReleaseArtifactError(
                    "PE resource name is invalid."
                ) from error

        def visit(
            relative: int,
            path: tuple[object, ...],
            depth: int,
        ) -> None:
            if (
                depth > 3
                or relative in active
                or relative > size - 16
            ):
                raise ReleaseArtifactError(
                    "PE resource directory graph is invalid."
                )
            active.add(relative)
            directory = root_offset + relative
            named, ids = self._unpack(
                "<HH",
                directory + 12,
                "resource directory count",
            )
            count = named + ids
            if count > 4096 or relative + 16 + count * 8 > size:
                raise ReleaseArtifactError(
                    "PE resource entry count is invalid."
                )
            for index in range(count):
                name_value, child_value = self._unpack(
                    "<II",
                    directory + 16 + index * 8,
                    "resource entry",
                )
                child_path = path + (resource_name(name_value),)
                child_relative = child_value & 0x7FFFFFFF
                if child_value & 0x80000000:
                    visit(child_relative, child_path, depth + 1)
                    continue
                if (
                    len(child_path) != 3
                    or child_relative > size - 16
                ):
                    raise ReleaseArtifactError(
                        "PE resource data depth is invalid."
                    )
                data_rva, data_size, _codepage, reserved = (
                    self._unpack(
                        "<IIII",
                        root_offset + child_relative,
                        "resource data entry",
                    )
                )
                if reserved or data_size > _MAXIMUM_CAB_BYTES:
                    raise ReleaseArtifactError(
                        "PE resource data entry is invalid."
                    )
                data_offset = self.rva_offset(
                    data_rva,
                    data_size,
                    "resource data",
                )
                if child_path in resources:
                    raise ReleaseArtifactError(
                        "PE resource path is duplicated."
                    )
                resources[child_path] = bytes(
                    self._range(
                        data_offset,
                        data_size,
                        "resource data",
                    )
                )
            active.remove(relative)

        visit(0, (), 0)
        return resources


def _single_resource(
    resources: dict[tuple[object, ...], bytes],
    type_id: int,
    name_id: int,
    label: str,
) -> bytes:
    matches = [
        contents
        for path, contents in resources.items()
        if len(path) == 3 and path[:2] == (type_id, name_id)
    ]
    if len(matches) != 1 or not matches[0]:
        raise ReleaseArtifactError(
            f"Release {label} resource is missing or ambiguous."
        )
    return matches[0]


def _local_name(tag: str) -> str:
    return tag.rsplit("}", 1)[-1]


def _validate_application_manifest(encoded: bytes) -> None:
    if not encoded or len(encoded) > _MAXIMUM_METADATA_BYTES:
        raise ReleaseArtifactError(
            "Release application manifest size is invalid."
        )
    try:
        root = ElementTree.fromstring(encoded)
    except ElementTree.ParseError as error:
        raise ReleaseArtifactError(
            "Release application manifest XML is invalid."
        ) from error
    identities = [
        node
        for node in root.iter()
        if _local_name(node.tag) == "assemblyIdentity"
    ]
    product = [
        node
        for node in identities
        if node.attrib.get("name") == "MecchaCamouflage.Launcher"
    ]
    common_controls = [
        node
        for node in identities
        if (
            node.attrib.get("name")
            == "Microsoft.Windows.Common-Controls"
            and node.attrib.get("version") == "6.0.0.0"
        )
    ]
    execution = [
        node
        for node in root.iter()
        if _local_name(node.tag) == "requestedExecutionLevel"
    ]
    supported = [
        node.attrib.get("Id", "").lower()
        for node in root.iter()
        if _local_name(node.tag) == "supportedOS"
    ]
    if (
        len(product) != 1
        or product[0].attrib.get("processorArchitecture") != "amd64"
        or len(common_controls) != 1
        or len(execution) != 1
        or execution[0].attrib.get("level") != "asInvoker"
        or execution[0].attrib.get("uiAccess") != "false"
        or _WINDOWS_10_SUPPORTED_OS not in supported
    ):
        raise ReleaseArtifactError(
            "Release application manifest contract is invalid."
        )


def _validate_provenance(
    provenance: dict[str, Any],
    manifest_files: dict[str, dict[str, Any]],
    *,
    product_version: str,
    ue4ss_commit: str,
) -> str:
    required_identity = {
        "schema_version": 1,
        "product_version": product_version,
        "ue4ss_commit": ue4ss_commit,
        "configuration": "Game__Shipping__Win64",
        "architecture": "x64",
        "msvc_runtime": "MultiThreadedDLL",
    }
    for key, expected in required_identity.items():
        if provenance.get(key) != expected:
            raise ReleaseArtifactError(
                f"Trusted provenance {key} does not match the release."
            )
    source_commit = provenance.get("source_commit")
    if (
        not isinstance(source_commit, str)
        or not _COMMIT_PATTERN.fullmatch(source_commit)
    ):
        raise ReleaseArtifactError(
            "Trusted provenance source commit is invalid."
        )
    for record_name, payload_path in (
        ("proxy", "dwmapi.dll"),
        ("main", "Mods/MecchaCamouflage/dlls/main.dll"),
        ("ue4ss", "UE4SS.dll"),
    ):
        record = provenance.get(record_name)
        entry = manifest_files[payload_path.lower()]
        if (
            not isinstance(record, dict)
            or record.get("size") != entry["size"]
            or record.get("sha256") != entry["sha256"]
            or not isinstance(record.get("dependents"), list)
        ):
            raise ReleaseArtifactError(
                f"Trusted {record_name} provenance does not match "
                "the embedded payload."
            )
    main_dependencies = {
        str(value).lower()
        for value in provenance["main"]["dependents"]
    }
    ue4ss_dependencies = {
        str(value).lower()
        for value in provenance["ue4ss"]["dependents"]
    }
    if (
        "ue4ss.dll" not in main_dependencies
        or "vcruntime140.dll" not in main_dependencies
        or "vcruntime140.dll" not in ue4ss_dependencies
    ):
        raise ReleaseArtifactError(
            "Trusted provenance does not bind the expected UE4SS/MSVC ABI."
        )
    return source_commit


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


def verify_release_artifact(
    inputs: ReleaseArtifactInputs,
) -> dict[str, Any]:
    for field in (
        "artifact_directory",
        "payload_manifest",
        "payload_cab",
        "payload_layout",
        "provenance_report",
        "report_output",
        "checksum_output",
    ):
        setattr(
            inputs,
            field,
            Path(os.path.abspath(getattr(inputs, field))),
        )
    if (
        not _VERSION_PATTERN.fullmatch(inputs.product_version)
        or not _COMMIT_PATTERN.fullmatch(inputs.ue4ss_commit)
        or inputs.executable_name
        != f"meccha-camouflage-v{inputs.product_version}.exe"
        or inputs.signing_policy != "unsigned"
    ):
        raise ReleaseArtifactError(
            "Release identity or signing policy is invalid."
        )
    if (
        inputs.report_output == inputs.checksum_output
        or inputs.artifact_directory in inputs.report_output.parents
        or inputs.artifact_directory in inputs.checksum_output.parents
    ):
        raise ReleaseArtifactError(
            "Release evidence outputs must be distinct and outside "
            "the single-artifact directory."
        )
    if (
        _is_link_or_reparse(inputs.artifact_directory)
        or not inputs.artifact_directory.is_dir()
    ):
        raise ReleaseArtifactError(
            "Release artifact directory is invalid."
        )
    try:
        entries = list(inputs.artifact_directory.iterdir())
    except OSError as error:
        raise ReleaseArtifactError(
            "Release artifact directory could not be enumerated."
        ) from error
    if len(entries) != 1 or entries[0].name != inputs.executable_name:
        raise ReleaseArtifactError(
            "Release artifact directory must contain exactly one "
            "versioned EXE."
        )
    executable = entries[0]
    encoded_executable = _plain_file(
        executable,
        maximum_size=_MAXIMUM_BINARY_BYTES,
    )
    manifest, manifest_bytes = _load_json(inputs.payload_manifest)
    layout, layout_bytes = _load_json(inputs.payload_layout)
    provenance, provenance_bytes = _load_json(
        inputs.provenance_report
    )
    cabinet_bytes = _plain_file(
        inputs.payload_cab,
        maximum_size=_MAXIMUM_CAB_BYTES,
    )
    if not cabinet_bytes.startswith(b"MSCF"):
        raise ReleaseArtifactError(
            "Release payload CAB signature is invalid."
        )
    canonical_manifest = (
        json.dumps(
            manifest,
            ensure_ascii=True,
            indent=2,
            separators=(",", ": "),
        )
        + "\n"
    ).encode("utf-8")
    canonical_layout = (
        json.dumps(
            layout,
            ensure_ascii=True,
            indent=2,
            separators=(",", ": "),
        )
        + "\n"
    ).encode("utf-8")
    if manifest_bytes != canonical_manifest:
        raise ReleaseArtifactError(
            "Release payload manifest encoding is not canonical."
        )
    if layout_bytes != canonical_layout:
        raise ReleaseArtifactError(
            "Release payload layout encoding is not canonical."
        )
    manifest_files = _validate_layout_and_manifest(
        layout,
        manifest,
        product_version=inputs.product_version,
        ue4ss_commit=inputs.ue4ss_commit,
    )
    source_commit = _validate_provenance(
        provenance,
        manifest_files,
        product_version=inputs.product_version,
        ue4ss_commit=inputs.ue4ss_commit,
    )

    pe = _PeImage(encoded_executable)
    if pe.machine != 0x8664:
        raise ReleaseArtifactError(
            "Release executable architecture is not x64."
        )
    if pe.subsystem != 2:
        raise ReleaseArtifactError(
            "Release executable subsystem is not Windows GUI."
        )
    if pe.symbol_table_pointer or pe.symbol_count:
        raise ReleaseArtifactError(
            "Release executable contains a COFF symbol table."
        )
    required_characteristics = 0x0020 | 0x0040 | 0x0100
    if (
        pe.dll_characteristics & required_characteristics
        != required_characteristics
    ):
        raise ReleaseArtifactError(
            "Release executable is missing PE memory protections."
        )
    if pe.directories[0] != (0, 0):
        raise ReleaseArtifactError(
            "Release launcher unexpectedly exports symbols."
        )
    if pe.directories[14] != (0, 0):
        raise ReleaseArtifactError(
            "Release launcher contains a .NET COM descriptor."
        )
    certificate_offset, certificate_size = pe.directories[4]
    if certificate_offset or certificate_size:
        raise ReleaseArtifactError(
            "The unsigned release policy refuses an Authenticode "
            "certificate."
        )
    imports = pe.imports()
    unexpected_imports = sorted(set(imports) - _ALLOWED_IMPORTS)
    missing_imports = sorted(_REQUIRED_IMPORTS - set(imports))
    if unexpected_imports or missing_imports:
        raise ReleaseArtifactError(
            "Release dependency allowlist mismatch: "
            f"unexpected={unexpected_imports}, missing={missing_imports}"
        )
    debug_types = pe.debug_types()
    if 2 in debug_types:
        raise ReleaseArtifactError(
            "Release executable contains CodeView/PDB debug data."
        )
    lower_binary = encoded_executable.lower()
    for token in _FORBIDDEN_BINARY_TOKENS:
        if (
            token.encode("ascii") in lower_binary
            or token.encode("utf-16-le") in lower_binary
        ):
            raise ReleaseArtifactError(
                f"Release executable contains forbidden legacy "
                f"artifact token: {token}"
            )

    resources = pe.resources()
    rcdata_names = {
        path[1]
        for path in resources
        if len(path) == 3 and path[0] == 10
    }
    if rcdata_names != {101, 102}:
        raise ReleaseArtifactError(
            "Release RCDATA inventory is not exactly 101/102."
        )
    embedded_manifest = _single_resource(
        resources,
        10,
        101,
        "payload manifest",
    )
    embedded_cabinet = _single_resource(
        resources,
        10,
        102,
        "payload CAB",
    )
    if embedded_manifest != manifest_bytes:
        raise ReleaseArtifactError(
            "Release embedded payload manifest differs from the "
            "verified input."
        )
    if embedded_cabinet != cabinet_bytes:
        raise ReleaseArtifactError(
            "Release embedded payload CAB differs from the verified input."
        )
    application_manifest = _single_resource(
        resources,
        24,
        1,
        "application manifest",
    )
    _validate_application_manifest(application_manifest)
    _single_resource(resources, 14, 201, "application icon")
    if not any(
        len(path) == 3 and path[0] == 3
        for path in resources
    ):
        raise ReleaseArtifactError(
            "Release application icon image resource is missing."
        )

    digest = hashlib.sha256(encoded_executable).hexdigest()
    payload_roles: dict[str, int] = {}
    for entry in manifest_files.values():
        payload_roles[entry["role"]] = (
            payload_roles.get(entry["role"], 0) + 1
        )
    report: dict[str, Any] = {
        "schema_version": 1,
        "product_version": inputs.product_version,
        "source_commit": source_commit,
        "ue4ss_commit": inputs.ue4ss_commit,
        "executable": inputs.executable_name,
        "size": len(encoded_executable),
        "sha256": digest,
        "architecture": "x64",
        "subsystem": "Windows GUI",
        "memory_protections": [
            "high-entropy-va",
            "dynamic-base",
            "nx-compatible",
        ],
        "imports": imports,
        "debug_directory_types": sorted(set(debug_types)),
        "signing_policy": inputs.signing_policy,
        "payload": {
            "manifest_size": len(manifest_bytes),
            "manifest_sha256": hashlib.sha256(
                manifest_bytes
            ).hexdigest(),
            "cab_size": len(cabinet_bytes),
            "cab_sha256": hashlib.sha256(cabinet_bytes).hexdigest(),
            "file_count": len(manifest_files),
            "role_counts": {
                role: payload_roles[role]
                for role in sorted(payload_roles)
            },
        },
        "provenance_report_sha256": hashlib.sha256(
            provenance_bytes
        ).hexdigest(),
    }
    encoded_report = (
        json.dumps(
            report,
            ensure_ascii=True,
            indent=2,
            separators=(",", ": "),
        )
        + "\n"
    ).encode("utf-8")
    encoded_checksum = (
        f"{digest}  {inputs.executable_name}\n"
    ).encode("ascii")
    try:
        _atomic_write(inputs.report_output, encoded_report)
        _atomic_write(inputs.checksum_output, encoded_checksum)
    except OSError as error:
        raise ReleaseArtifactError(
            f"Release evidence publication failed: {error}"
        ) from error
    return report


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Verify the exact MecchaCamouflage v2 release EXE"
    )
    parser.add_argument(
        "--artifact-directory",
        required=True,
        type=Path,
    )
    parser.add_argument("--executable-name", required=True)
    parser.add_argument("--payload-manifest", required=True, type=Path)
    parser.add_argument("--payload-cab", required=True, type=Path)
    parser.add_argument("--payload-layout", required=True, type=Path)
    parser.add_argument(
        "--provenance-report",
        required=True,
        type=Path,
    )
    parser.add_argument("--report-output", required=True, type=Path)
    parser.add_argument("--checksum-output", required=True, type=Path)
    parser.add_argument("--product-version", required=True)
    parser.add_argument("--ue4ss-commit", required=True)
    parser.add_argument(
        "--signing-policy",
        choices=("unsigned",),
        default="unsigned",
    )
    return parser


def main(arguments: Iterable[str] | None = None) -> int:
    options = _parser().parse_args(arguments)
    try:
        report = verify_release_artifact(
            ReleaseArtifactInputs(
                artifact_directory=options.artifact_directory,
                executable_name=options.executable_name,
                payload_manifest=options.payload_manifest,
                payload_cab=options.payload_cab,
                payload_layout=options.payload_layout,
                provenance_report=options.provenance_report,
                report_output=options.report_output,
                checksum_output=options.checksum_output,
                product_version=options.product_version,
                ue4ss_commit=options.ue4ss_commit,
                signing_policy=options.signing_policy,
            )
        )
    except ReleaseArtifactError as error:
        print(f"release verification error: {error}", file=sys.stderr)
        return 1
    print(
        "PASS release artifact: "
        f"{report['executable']} {report['sha256']}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
