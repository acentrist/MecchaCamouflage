#!/usr/bin/env python3

import hashlib
import os
import subprocess
import tempfile
import unittest
from pathlib import Path

from build_payload_cab import (
    PayloadCabError,
    build_payload_cab,
    verify_payload_cab,
)


class BuildPayloadCabTests(unittest.TestCase):
    def test_rejects_outputs_inside_payload_or_at_same_path(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            base = Path(temporary)
            payload = base / "payload"
            payload.mkdir()
            (payload / "UE4SS.dll").write_bytes(b"runtime")
            layout = {
                "schema_version": 1,
                "generated_paths": [],
                "files": [
                    {"path": "UE4SS.dll", "role": "runtime"},
                ],
            }

            with self.assertRaisesRegex(
                PayloadCabError,
                "outside the payload root",
            ):
                build_payload_cab(
                    payload,
                    layout,
                    base / "manifest.json",
                    payload / "payload.cab",
                    product_version="2.0.0",
                    ue4ss_commit=
                        "6c26f038751b3d96059d4a9148f5d093012d55ad",
                )
            with self.assertRaisesRegex(
                PayloadCabError,
                "distinct",
            ):
                build_payload_cab(
                    payload,
                    layout,
                    base / "same-output",
                    base / "same-output",
                    product_version="2.0.0",
                    ue4ss_commit=
                        "6c26f038751b3d96059d4a9148f5d093012d55ad",
                )

    @unittest.skipUnless(os.name == "nt", "MakeCab requires Windows")
    def test_cab_is_deterministic_and_round_trips_exact_payload(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            base = Path(temporary)
            payload = base / "payload"
            (payload / "Mods/MecchaCamouflage/dlls").mkdir(parents=True)
            (payload / "UE4SS.dll").write_bytes(b"runtime")
            mod = payload / "Mods/MecchaCamouflage/dlls/main.dll"
            mod.write_bytes(b"mod")
            layout = {
                "schema_version": 1,
                "generated_paths": ["Logs"],
                "files": [
                    {"path": "UE4SS.dll", "role": "runtime"},
                    {
                        "path": "Mods/MecchaCamouflage/dlls/main.dll",
                        "role": "mod",
                    },
                ],
            }

            first_manifest = base / "first-manifest.json"
            first_cab = base / "first.cab"
            first = build_payload_cab(
                payload,
                layout,
                first_manifest,
                first_cab,
                product_version="2.0.0",
                ue4ss_commit="6c26f038751b3d96059d4a9148f5d093012d55ad",
            )

            os.utime(payload / "UE4SS.dll", (1_700_000_000, 1_700_000_000))
            os.utime(mod, (1_750_000_000, 1_750_000_000))
            second_manifest = base / "second-manifest.json"
            second_cab = base / "nested/output/second.cab"
            second = build_payload_cab(
                payload,
                dict(reversed(layout.items())),
                second_manifest,
                second_cab,
                product_version="2.0.0",
                ue4ss_commit="6c26f038751b3d96059d4a9148f5d093012d55ad",
            )

            self.assertEqual(first, second)
            self.assertEqual(
                hashlib.sha256(first_cab.read_bytes()).digest(),
                hashlib.sha256(second_cab.read_bytes()).digest(),
            )
            self.assertEqual(
                first_manifest.read_bytes(),
                second_manifest.read_bytes(),
            )

            extracted = base / "extracted"
            extracted.mkdir()
            completed = subprocess.run(
                [
                    "expand.exe",
                    "-F:*",
                    str(first_cab),
                    str(extracted),
                ],
                check=False,
                capture_output=True,
                text=True,
            )
            self.assertEqual(
                completed.returncode,
                0,
                completed.stdout + completed.stderr,
            )
            extracted_files = sorted(
                path.relative_to(extracted).as_posix()
                for path in extracted.rglob("*")
                if path.is_file()
            )
            self.assertEqual(
                extracted_files,
                [
                    "Mods/MecchaCamouflage/dlls/main.dll",
                    "UE4SS.dll",
                ],
            )
            self.assertEqual(
                (extracted / "UE4SS.dll").read_bytes(),
                b"runtime",
            )
            self.assertEqual(
                (
                    extracted /
                    "Mods/MecchaCamouflage/dlls/main.dll"
                ).read_bytes(),
                b"mod",
            )

            corrupt_cab = base / "corrupt.cab"
            corrupt_cab.write_bytes(first_cab.read_bytes()[:-8])
            with self.assertRaises(PayloadCabError):
                verify_payload_cab(
                    corrupt_cab,
                    layout,
                    first,
                )


if __name__ == "__main__":
    unittest.main()
