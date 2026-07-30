#!/usr/bin/env python3

import hashlib
import json
import os
import tempfile
import unittest
from pathlib import Path

from generate_payload_manifest import (
    PayloadManifestError,
    generate_payload_manifest,
    load_layout,
    write_payload_manifest,
)


class GeneratePayloadManifestTests(unittest.TestCase):
    def test_generates_canonical_manifest_from_exact_layout(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary) / "payload"
            (root / "Mods/MecchaCamouflage/dlls").mkdir(parents=True)
            (root / "UE4SS.dll").write_bytes(b"runtime")
            mod = root / "Mods/MecchaCamouflage/dlls/main.dll"
            mod.write_bytes(b"mod")

            layout = {
                "schema_version": 1,
                "generated_paths": ["Logs"],
                "files": [
                    {
                        "path": "UE4SS.dll",
                        "role": "runtime",
                    },
                    {
                        "path": "Mods/MecchaCamouflage/dlls/main.dll",
                        "role": "mod",
                    },
                ],
            }

            manifest = generate_payload_manifest(
                root,
                layout,
                product_version="2.0.0",
                ue4ss_commit="6c26f038751b3d96059d4a9148f5d093012d55ad",
            )

            self.assertEqual(
                manifest,
                {
                    "schema_version": 1,
                    "product_version": "2.0.0",
                    "ue4ss_commit":
                        "6c26f038751b3d96059d4a9148f5d093012d55ad",
                    "generated_paths": ["Logs"],
                    "files": [
                        {
                            "path":
                                "Mods/MecchaCamouflage/dlls/main.dll",
                            "role": "mod",
                            "size": 3,
                            "sha256": hashlib.sha256(b"mod").hexdigest(),
                        },
                        {
                            "path": "UE4SS.dll",
                            "role": "runtime",
                            "size": 7,
                            "sha256":
                                hashlib.sha256(b"runtime").hexdigest(),
                        },
                    ],
                },
            )
            first = json.dumps(
                manifest,
                ensure_ascii=True,
                indent=2,
                separators=(",", ": "),
            ) + "\n"
            second = json.dumps(
                generate_payload_manifest(
                    root,
                    dict(reversed(layout.items())),
                    product_version="2.0.0",
                    ue4ss_commit=
                        "6c26f038751b3d96059d4a9148f5d093012d55ad",
                ),
                ensure_ascii=True,
                indent=2,
                separators=(",", ": "),
            ) + "\n"
            self.assertEqual(first, second)

    def test_rejects_files_not_declared_by_layout(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary) / "payload"
            root.mkdir()
            (root / "UE4SS.dll").write_bytes(b"runtime")
            (root / "unexpected.dll").write_bytes(b"foreign")

            with self.assertRaisesRegex(
                PayloadManifestError,
                "not declared.*unexpected.dll",
            ):
                generate_payload_manifest(
                    root,
                    {
                        "schema_version": 1,
                        "generated_paths": [],
                        "files": [
                            {
                                "path": "UE4SS.dll",
                                "role": "runtime",
                            }
                        ],
                    },
                    product_version="2.0.0",
                    ue4ss_commit=
                        "6c26f038751b3d96059d4a9148f5d093012d55ad",
                )

    def test_rejects_declared_files_missing_from_payload(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary) / "payload"
            root.mkdir()

            with self.assertRaisesRegex(
                PayloadManifestError,
                "missing.*UE4SS.dll",
            ):
                generate_payload_manifest(
                    root,
                    {
                        "schema_version": 1,
                        "generated_paths": [],
                        "files": [
                            {
                                "path": "UE4SS.dll",
                                "role": "runtime",
                            }
                        ],
                    },
                    product_version="2.0.0",
                    ue4ss_commit=
                        "6c26f038751b3d96059d4a9148f5d093012d55ad",
                )

    def test_rejects_malformed_or_hostile_layouts(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary) / "payload"
            root.mkdir()
            (root / "UE4SS.dll").write_bytes(b"runtime")
            valid_file = {
                "path": "UE4SS.dll",
                "role": "runtime",
            }
            cases = {
                "unknown top-level key": {
                    "schema_version": 1,
                    "generated_paths": [],
                    "files": [valid_file],
                    "extra": True,
                },
                "unknown file key": {
                    "schema_version": 1,
                    "generated_paths": [],
                    "files": [{**valid_file, "size": 7}],
                },
                "unsupported schema": {
                    "schema_version": 2,
                    "generated_paths": [],
                    "files": [valid_file],
                },
                "unknown role": {
                    "schema_version": 1,
                    "generated_paths": [],
                    "files": [{"path": "UE4SS.dll", "role": "other"}],
                },
                "traversal path": {
                    "schema_version": 1,
                    "generated_paths": [],
                    "files": [{"path": "../UE4SS.dll", "role": "runtime"}],
                },
                "case-colliding path": {
                    "schema_version": 1,
                    "generated_paths": [],
                    "files": [
                        valid_file,
                        {"path": "ue4ss.DLL", "role": "runtime"},
                    ],
                },
                "generated/file overlap": {
                    "schema_version": 1,
                    "generated_paths": ["UE4SS.dll"],
                    "files": [valid_file],
                },
            }

            for name, layout in cases.items():
                with self.subTest(name=name):
                    with self.assertRaises(PayloadManifestError):
                        generate_payload_manifest(
                            root,
                            layout,
                            product_version="2.0.0",
                            ue4ss_commit=
                                "6c26f038751b3d96059d4a9148f5d093012d55ad",
                        )

            with self.assertRaises(PayloadManifestError):
                generate_payload_manifest(
                    root,
                    {
                        "schema_version": 1,
                        "generated_paths": [],
                        "files": [valid_file],
                    },
                    product_version="2",
                    ue4ss_commit="invalid",
                )

    def test_rejects_symbolic_links_in_payload_tree(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            temporary_root = Path(temporary)
            root = temporary_root / "payload"
            root.mkdir()
            target = temporary_root / "runtime.dll"
            target.write_bytes(b"runtime")
            try:
                (root / "UE4SS.dll").symlink_to(target)
            except OSError as error:
                self.skipTest(
                    f"symbolic-link creation is unavailable: {error}"
                )

            with self.assertRaisesRegex(
                PayloadManifestError,
                "symbolic link",
            ):
                generate_payload_manifest(
                    root,
                    {
                        "schema_version": 1,
                        "generated_paths": [],
                        "files": [
                            {
                                "path": "UE4SS.dll",
                                "role": "runtime",
                            }
                        ],
                    },
                    product_version="2.0.0",
                    ue4ss_commit=
                        "6c26f038751b3d96059d4a9148f5d093012d55ad",
                )

    def test_publishes_atomically_and_preserves_previous_output_on_error(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            temporary_root = Path(temporary)
            root = temporary_root / "payload"
            root.mkdir()
            (root / "UE4SS.dll").write_bytes(b"runtime")
            output = temporary_root / "payload-manifest.json"
            layout = {
                "schema_version": 1,
                "generated_paths": [],
                "files": [
                    {
                        "path": "UE4SS.dll",
                        "role": "runtime",
                    }
                ],
            }

            write_payload_manifest(
                root,
                layout,
                output,
                product_version="2.0.0",
                ue4ss_commit="6c26f038751b3d96059d4a9148f5d093012d55ad",
            )
            published = output.read_bytes()
            self.assertTrue(published.endswith(b"\n"))
            self.assertFalse(output.with_suffix(".json.tmp").exists())

            (root / "unexpected.dll").write_bytes(b"foreign")
            with self.assertRaises(PayloadManifestError):
                write_payload_manifest(
                    root,
                    layout,
                    output,
                    product_version="2.0.0",
                    ue4ss_commit=
                        "6c26f038751b3d96059d4a9148f5d093012d55ad",
                )
            self.assertEqual(output.read_bytes(), published)

    def test_layout_loader_rejects_duplicate_json_keys(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            layout_path = Path(temporary) / "layout.json"
            layout_path.write_text(
                '{"schema_version":1,"schema_version":1,'
                '"generated_paths":[],"files":[]}',
                encoding="utf-8",
            )
            with self.assertRaisesRegex(
                PayloadManifestError,
                "duplicate JSON key",
            ):
                load_layout(layout_path)

    def test_rejects_case_collisions_in_actual_payload_tree(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary) / "payload"
            root.mkdir()
            (root / "UE4SS.dll").write_bytes(b"runtime")
            (root / "ue4ss.DLL").write_bytes(b"collision")
            colliding_entries = [
                path
                for path in root.iterdir()
                if path.name.lower() == "ue4ss.dll"
            ]
            if len(colliding_entries) != 2:
                self.skipTest(
                    "case-colliding files are unavailable on this filesystem"
                )

            with self.assertRaisesRegex(
                PayloadManifestError,
                "case-colliding",
            ):
                generate_payload_manifest(
                    root,
                    {
                        "schema_version": 1,
                        "generated_paths": [],
                        "files": [
                            {
                                "path": "UE4SS.dll",
                                "role": "runtime",
                            }
                        ],
                    },
                    product_version="2.0.0",
                    ue4ss_commit=
                        "6c26f038751b3d96059d4a9148f5d093012d55ad",
                )

    @unittest.skipUnless(hasattr(os, "mkfifo"), "FIFO creation unavailable")
    def test_rejects_non_regular_payload_entries(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary) / "payload"
            root.mkdir()
            (root / "UE4SS.dll").write_bytes(b"runtime")
            os.mkfifo(root / "unexpected.pipe")

            with self.assertRaisesRegex(
                PayloadManifestError,
                "not a regular file or directory",
            ):
                generate_payload_manifest(
                    root,
                    {
                        "schema_version": 1,
                        "generated_paths": [],
                        "files": [
                            {
                                "path": "UE4SS.dll",
                                "role": "runtime",
                            }
                        ],
                    },
                    product_version="2.0.0",
                    ue4ss_commit=
                        "6c26f038751b3d96059d4a9148f5d093012d55ad",
                )


if __name__ == "__main__":
    unittest.main()
