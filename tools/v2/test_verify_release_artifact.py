#!/usr/bin/env python3

from __future__ import annotations

import hashlib
import json
import struct
import sys
import tempfile
import unittest
from pathlib import Path


sys.path.insert(0, str(Path(__file__).resolve().parent))

from verify_release_artifact import (  # noqa: E402
    ReleaseArtifactError,
    ReleaseArtifactInputs,
    verify_release_artifact,
)


UE4SS_COMMIT = "6c26f038751b3d96059d4a9148f5d093012d55ad"
IMPORTS = (
    "ADVAPI32.dll",
    "bcrypt.dll",
    "COMCTL32.dll",
    "KERNEL32.dll",
    "MSVCP140.dll",
    "ole32.dll",
    "SETUPAPI.dll",
    "SHELL32.dll",
    "VCRUNTIME140.dll",
    "VCRUNTIME140_1.dll",
    "api-ms-win-crt-filesystem-l1-1-0.dll",
    "api-ms-win-crt-heap-l1-1-0.dll",
    "api-ms-win-crt-locale-l1-1-0.dll",
    "api-ms-win-crt-math-l1-1-0.dll",
    "api-ms-win-crt-runtime-l1-1-0.dll",
    "api-ms-win-crt-stdio-l1-1-0.dll",
    "api-ms-win-crt-string-l1-1-0.dll",
)
MANIFEST_XML = b"""<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<assembly xmlns="urn:schemas-microsoft-com:asm.v1" manifestVersion="1.0">
  <assemblyIdentity name="MecchaCamouflage.Launcher"
      processorArchitecture="amd64" type="win32" version="2.0.0.0"/>
  <trustInfo xmlns="urn:schemas-microsoft-com:asm.v3"><security>
    <requestedPrivileges><requestedExecutionLevel level="asInvoker"
        uiAccess="false"/></requestedPrivileges>
  </security></trustInfo>
  <compatibility xmlns="urn:schemas-microsoft-com:compatibility.v1">
    <application>
      <supportedOS Id="{8e0f7a12-bfb3-4fe8-b9a5-48fd50a15a9a}"/>
    </application>
  </compatibility>
  <dependency><dependentAssembly><assemblyIdentity type="win32"
      name="Microsoft.Windows.Common-Controls" version="6.0.0.0"
      processorArchitecture="*" publicKeyToken="6595b64144ccf1df"
      language="*"/></dependentAssembly></dependency>
</assembly>
"""


def align(value: int, alignment: int) -> int:
    return (value + alignment - 1) // alignment * alignment


def resource_section(resources: dict[int, dict[int, bytes]]) -> bytes:
    tree = {
        type_id: {
            resource_id: {1033: contents}
            for resource_id, contents in names.items()
        }
        for type_id, names in resources.items()
    }
    encoded = bytearray()
    data_entries: list[tuple[int, bytes]] = []

    def add_directory(node: dict[int, object]) -> int:
        offset = len(encoded)
        items = sorted(node.items())
        encoded.extend(b"\0" * (16 + 8 * len(items)))
        struct.pack_into("<H", encoded, offset + 14, len(items))
        for index, (resource_id, child) in enumerate(items):
            entry = offset + 16 + index * 8
            struct.pack_into("<I", encoded, entry, resource_id)
            if isinstance(child, dict):
                child_offset = add_directory(child)
                struct.pack_into(
                    "<I",
                    encoded,
                    entry + 4,
                    child_offset | 0x80000000,
                )
            else:
                data_offset = len(encoded)
                encoded.extend(b"\0" * 16)
                data_entries.append((data_offset, child))
                struct.pack_into("<I", encoded, entry + 4, data_offset)
        return offset

    add_directory(tree)
    for data_offset, contents in data_entries:
        while len(encoded) % 4:
            encoded.append(0)
        contents_offset = len(encoded)
        encoded.extend(contents)
        struct.pack_into(
            "<IIII",
            encoded,
            data_offset,
            0x3000 + contents_offset,
            len(contents),
            0,
            0,
        )
    return bytes(encoded)


def make_pe(
    manifest: bytes,
    cabinet: bytes,
    *,
    imports: tuple[str, ...] = IMPORTS,
    subsystem: int = 2,
    debug_type: int = 16,
    signed: bool = False,
) -> bytes:
    headers = bytearray(0x200)
    headers[:2] = b"MZ"
    struct.pack_into("<I", headers, 0x3C, 0x80)
    headers[0x80:0x84] = b"PE\0\0"
    struct.pack_into(
        "<HHIIIHH",
        headers,
        0x84,
        0x8664,
        3,
        0,
        0,
        0,
        0xF0,
        0x22,
    )
    optional = 0x98
    struct.pack_into("<H", headers, optional, 0x20B)
    struct.pack_into("<I", headers, optional + 16, 0x1000)
    struct.pack_into("<I", headers, optional + 20, 0x1000)
    struct.pack_into("<Q", headers, optional + 24, 0x140000000)
    struct.pack_into("<II", headers, optional + 32, 0x1000, 0x200)
    struct.pack_into("<I", headers, optional + 56, 0x5000)
    struct.pack_into("<I", headers, optional + 60, 0x200)
    struct.pack_into("<H", headers, optional + 68, subsystem)
    struct.pack_into("<H", headers, optional + 70, 0x8160)
    struct.pack_into("<I", headers, optional + 108, 16)

    rdata = bytearray(0x800)
    descriptor_size = 20 * (len(imports) + 1)
    name_offset = descriptor_size
    for index, name in enumerate(imports):
        encoded_name = name.encode("ascii") + b"\0"
        rdata[name_offset:name_offset + len(encoded_name)] = encoded_name
        struct.pack_into(
            "<IIIII",
            rdata,
            index * 20,
            0,
            0,
            0,
            0x2000 + name_offset,
            0,
        )
        name_offset += len(encoded_name)
    debug_offset = align(name_offset, 4)
    struct.pack_into(
        "<IIHHIIII",
        rdata,
        debug_offset,
        0,
        0,
        0,
        0,
        debug_type,
        0,
        0,
        0,
    )
    resource = resource_section(
        {
            3: {301: b"icon"},
            10: {101: manifest, 102: cabinet},
            14: {201: b"group-icon"},
            24: {1: MANIFEST_XML},
        }
    )
    resource_raw_size = align(len(resource), 0x200)
    resource += b"\0" * (resource_raw_size - len(resource))

    directories = optional + 112
    struct.pack_into(
        "<II",
        headers,
        directories + 8,
        0x2000,
        descriptor_size,
    )
    struct.pack_into(
        "<II",
        headers,
        directories + 16,
        0x3000,
        len(resource),
    )
    if signed:
        struct.pack_into(
            "<II",
            headers,
            directories + 32,
            0x4000,
            16,
        )
    struct.pack_into(
        "<II",
        headers,
        directories + 48,
        0x2000 + debug_offset,
        28,
    )

    section_table = optional + 0xF0
    for index, values in enumerate(
        (
            (b".text", 1, 0x1000, 0x200, 0x200, 0x60000020),
            (
                b".rdata",
                len(rdata),
                0x2000,
                len(rdata),
                0x400,
                0x40000040,
            ),
            (
                b".rsrc",
                len(resource),
                0x3000,
                len(resource),
                0xC00,
                0x40000040,
            ),
        )
    ):
        name, virtual_size, rva, raw_size, raw_offset, flags = values
        offset = section_table + index * 40
        headers[offset:offset + len(name)] = name
        struct.pack_into(
            "<IIIIIIHHI",
            headers,
            offset + 8,
            virtual_size,
            rva,
            raw_size,
            raw_offset,
            0,
            0,
            0,
            0,
            flags,
        )

    result = headers + bytearray(0x200) + rdata + resource
    if signed:
        result.extend(b"\0" * 16)
    return bytes(result)


class ReleaseArtifactTests(unittest.TestCase):
    def make_fixture(
        self,
        root: Path,
        *,
        imports: tuple[str, ...] = IMPORTS,
        subsystem: int = 2,
        debug_type: int = 16,
        signed: bool = False,
    ) -> ReleaseArtifactInputs:
        artifact = root / "artifact"
        artifact.mkdir()
        cabinet = b"MSCF" + b"\0" * 64
        files = [
            ("dwmapi.dll", "proxy", b"proxy"),
            ("UE4SS.dll", "runtime", b"runtime"),
            ("UE4SS-settings.ini", "config", b"settings"),
            ("MemberVariableLayout.ini", "config", b"layout"),
            (
                "Mods/MecchaCamouflage/dlls/main.dll",
                "mod",
                b"mod",
            ),
            (
                "Mods/MecchaCamouflage/enabled.txt",
                "config",
                b"",
            ),
            (
                "Mods/MecchaCamouflage/resources/localization/catalog.json",
                "localization",
                b"catalog",
            ),
            (
                "Mods/MecchaCamouflage/resources/mesh-profiles/profile.json",
                "profile",
                b"profile",
            ),
            (
                "Mods/MecchaCamouflage/resources/fonts/font.otf",
                "font",
                b"font",
            ),
            (
                "Licenses/MecchaCamouflage-LICENSE.txt",
                "license",
                b"project license",
            ),
            (
                "Licenses/UE4SS-LICENSE.txt",
                "license",
                b"ue4ss license",
            ),
            (
                "Licenses/libwebp-COPYING.txt",
                "license",
                b"webp license",
            ),
            (
                "Licenses/D-DIN-OFL.txt",
                "license",
                b"font license",
            ),
            (
                "Licenses/THIRD-PARTY-NOTICES.txt",
                "license",
                b"notices",
            ),
        ]
        layout = {
            "schema_version": 1,
            "generated_paths": ["UE4SS.log", "cache"],
            "files": sorted(
                (
                    {"path": path, "role": role}
                    for path, role, _ in files
                ),
                key=lambda item: item["path"].lower(),
            ),
        }
        manifest = {
            "schema_version": 1,
            "product_version": "2.0.0",
            "ue4ss_commit": UE4SS_COMMIT,
            "generated_paths": ["cache", "UE4SS.log"],
            "files": sorted(
                (
                    {
                        "path": path,
                        "role": role,
                        "size": len(contents),
                        "sha256": hashlib.sha256(contents).hexdigest(),
                    }
                    for path, role, contents in files
                ),
                key=lambda item: item["path"].lower(),
            ),
        }
        layout_path = root / "payload-layout.json"
        layout_path.write_bytes(
            (json.dumps(layout, indent=2) + "\n").encode("utf-8")
        )
        manifest_path = root / "payload-manifest.json"
        manifest_bytes = (
            json.dumps(manifest, indent=2) + "\n"
        ).encode("utf-8")
        manifest_path.write_bytes(manifest_bytes)
        cabinet_path = root / "payload.cab"
        cabinet_path.write_bytes(cabinet)
        exe_name = "meccha-camouflage-v2.0.0.exe"
        (artifact / exe_name).write_bytes(
            make_pe(
                manifest_bytes,
                cabinet,
                imports=imports,
                subsystem=subsystem,
                debug_type=debug_type,
                signed=signed,
            )
        )
        by_path = {path: (contents, role) for path, role, contents in files}
        provenance = {
            "schema_version": 1,
            "product_version": "2.0.0",
            "source_commit": "1" * 40,
            "ue4ss_commit": UE4SS_COMMIT,
            "configuration": "Game__Shipping__Win64",
            "architecture": "x64",
            "msvc_runtime": "MultiThreadedDLL",
        }
        for key, path in (
            ("proxy", "dwmapi.dll"),
            ("main", "Mods/MecchaCamouflage/dlls/main.dll"),
            ("ue4ss", "UE4SS.dll"),
        ):
            contents = by_path[path][0]
            provenance[key] = {
                "path": path,
                "size": len(contents),
                "sha256": hashlib.sha256(contents).hexdigest(),
                "dependents": (
                    ["UE4SS.dll", "VCRUNTIME140.dll"]
                    if key == "main"
                    else (
                        ["VCRUNTIME140.dll"]
                        if key == "ue4ss"
                        else ["KERNEL32.dll"]
                    )
                ),
            }
        provenance_path = root / "phase2-provenance.json"
        provenance_path.write_text(
            json.dumps(provenance, indent=2) + "\n",
            encoding="utf-8",
        )
        return ReleaseArtifactInputs(
            artifact_directory=artifact,
            executable_name=exe_name,
            payload_manifest=manifest_path,
            payload_cab=cabinet_path,
            payload_layout=layout_path,
            provenance_report=provenance_path,
            report_output=root / "release-report.json",
            checksum_output=root / "release.sha256",
            product_version="2.0.0",
            ue4ss_commit=UE4SS_COMMIT,
            signing_policy="unsigned",
        )

    def test_verifies_exact_native_artifact_and_writes_evidence(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            inputs = self.make_fixture(Path(temporary))

            report = verify_release_artifact(inputs)

            executable = (
                inputs.artifact_directory / inputs.executable_name
            )
            digest = hashlib.sha256(executable.read_bytes()).hexdigest()
            self.assertEqual(report["sha256"], digest)
            self.assertEqual(report["architecture"], "x64")
            self.assertEqual(report["subsystem"], "Windows GUI")
            self.assertEqual(report["signing_policy"], "unsigned")
            self.assertEqual(
                report["payload"]["manifest_sha256"],
                hashlib.sha256(
                    inputs.payload_manifest.read_bytes()
                ).hexdigest(),
            )
            self.assertEqual(
                json.loads(
                    inputs.report_output.read_text(encoding="utf-8")
                ),
                report,
            )
            self.assertEqual(
                inputs.checksum_output.read_text(encoding="ascii"),
                f"{digest}  {inputs.executable_name}\n",
            )

    def test_refuses_extra_artifact_and_preserves_outputs(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            inputs = self.make_fixture(Path(temporary))
            (inputs.artifact_directory / "unexpected.pdb").write_bytes(
                b"pdb"
            )
            inputs.report_output.write_text("keep", encoding="utf-8")

            with self.assertRaisesRegex(
                ReleaseArtifactError,
                "exactly one",
            ):
                verify_release_artifact(inputs)

            self.assertEqual(
                inputs.report_output.read_text(encoding="utf-8"),
                "keep",
            )
            self.assertFalse(inputs.checksum_output.exists())

    def test_refuses_forbidden_import_codeview_and_console_binary(self) -> None:
        cases = (
            (
                {"imports": IMPORTS + ("WebView2Loader.dll",)},
                "dependency",
            ),
            ({"debug_type": 2}, "CodeView"),
            ({"subsystem": 3}, "subsystem"),
        )
        for options, expected in cases:
            with self.subTest(expected=expected):
                with tempfile.TemporaryDirectory() as temporary:
                    inputs = self.make_fixture(
                        Path(temporary),
                        **options,
                    )
                    with self.assertRaisesRegex(
                        ReleaseArtifactError,
                        expected,
                    ):
                        verify_release_artifact(inputs)

    def test_refuses_resource_provenance_and_signing_mismatch(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            inputs = self.make_fixture(root)
            inputs.payload_cab.write_bytes(b"MSCFdifferent")
            with self.assertRaisesRegex(
                ReleaseArtifactError,
                "CAB",
            ):
                verify_release_artifact(inputs)

        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            inputs = self.make_fixture(root)
            provenance = json.loads(
                inputs.provenance_report.read_text(encoding="utf-8")
            )
            provenance["main"]["sha256"] = "0" * 64
            inputs.provenance_report.write_text(
                json.dumps(provenance),
                encoding="utf-8",
            )
            with self.assertRaisesRegex(
                ReleaseArtifactError,
                "provenance",
            ):
                verify_release_artifact(inputs)

        with tempfile.TemporaryDirectory() as temporary:
            inputs = self.make_fixture(
                Path(temporary),
                signed=True,
            )
            with self.assertRaisesRegex(
                ReleaseArtifactError,
                "unsigned",
            ):
                verify_release_artifact(inputs)


if __name__ == "__main__":
    unittest.main()
