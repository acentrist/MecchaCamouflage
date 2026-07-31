#!/usr/bin/env python3

from __future__ import annotations

import hashlib
import json
import sys
import tempfile
import unittest
from pathlib import Path


sys.path.insert(0, str(Path(__file__).resolve().parent))

from build_dependency_notices import (  # noqa: E402
    DependencyNoticeError,
    DependencyNoticeInputs,
    build_dependency_notices,
)


UE4SS_COMMIT = "6c26f038751b3d96059d4a9148f5d093012d55ad"


def canonical_json(value: object) -> bytes:
    return (
        json.dumps(
            value,
            ensure_ascii=True,
            indent=2,
            separators=(",", ": "),
        )
        + "\n"
    ).encode("utf-8")


class DependencyNoticeTests(unittest.TestCase):
    def make_fixture(self, root: Path) -> DependencyNoticeInputs:
        roots = {
            "ue4ss": root / "ue4ss",
            "project": root / "project",
            "build": root / "build",
            "cargo": root / "cargo",
        }
        for path in roots.values():
            path.mkdir()
        licenses = {
            ("ue4ss", "LICENSE"): (
                b"UE4SS MIT license\n"
            ),
            ("build", "_deps/fmt-src/LICENSE.rst"): (
                b"fmt MIT license\n"
            ),
            (
                "cargo",
                "registry/src/example/serde-1.0.0/LICENSE-MIT",
            ): b"serde MIT license\n",
        }
        for (root_name, relative), contents in licenses.items():
            path = roots[root_name] / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(contents)
        components = [
            {
                "name": "fmt",
                "version": "11.2.0",
                "source_identity": "git:40626af88bd7df9a5fb80be7b25ac85b122d6c21",
            },
            {
                "name": "serde",
                "version": "1.0.0",
                "source_identity": "cargo:" + "2" * 64,
            },
            {
                "name": "UE4SS",
                "version": "6c26f038",
                "source_identity": f"git:{UE4SS_COMMIT}",
            },
        ]
        evidence = {
            "schema_version": 1,
            "ue4ss_commit": UE4SS_COMMIT,
            "configuration": "Game__Shipping__Win64",
            "root_targets": ["release-root@project:src"],
            "target_graph": [
                {
                    "key": "release-root@project:src",
                    "name": "release-root",
                    "type": "SHARED_LIBRARY",
                    "source": "project:src",
                    "dependencies": [],
                }
            ],
            "components": components,
        }
        evidence_bytes = canonical_json(evidence)
        evidence_path = root / "dependency-evidence.json"
        evidence_path.write_bytes(evidence_bytes)
        license_by_component = {
            "fmt": [
                {
                    "root": "build",
                    "path": "_deps/fmt-src/LICENSE.rst",
                    "sha256": hashlib.sha256(
                        licenses[
                            ("build", "_deps/fmt-src/LICENSE.rst")
                        ]
                    ).hexdigest(),
                }
            ],
            "serde": [
                {
                    "root": "cargo",
                    "path":
                        "registry/src/example/serde-1.0.0/LICENSE-MIT",
                    "sha256": hashlib.sha256(
                        licenses[
                            (
                                "cargo",
                                "registry/src/example/"
                                "serde-1.0.0/LICENSE-MIT",
                            )
                        ]
                    ).hexdigest(),
                }
            ],
            "UE4SS": [
                {
                    "root": "ue4ss",
                    "path": "LICENSE",
                    "sha256": hashlib.sha256(
                        licenses[("ue4ss", "LICENSE")]
                    ).hexdigest(),
                }
            ],
        }
        expressions = {
            "fmt": "MIT",
            "serde": "MIT OR Apache-2.0",
            "UE4SS": "MIT",
        }
        audit = {
            "schema_version": 1,
            "ue4ss_commit": UE4SS_COMMIT,
            "dependency_evidence_sha256": hashlib.sha256(
                evidence_bytes
            ).hexdigest(),
            "components": [
                {
                    **component,
                    "license_expression": expressions[component["name"]],
                    "license_files":
                        license_by_component[component["name"]],
                }
                for component in components
            ],
        }
        audit_path = root / "approved-license-audit.json"
        audit_path.write_bytes(canonical_json(audit))
        return DependencyNoticeInputs(
            evidence=evidence_path,
            approved_audit=audit_path,
            roots=roots,
            notice_output=root / "THIRD-PARTY-NOTICES.txt",
            report_output=root / "dependency-license-report.json",
            ue4ss_commit=UE4SS_COMMIT,
        )

    def test_builds_deterministic_notice_from_exact_approved_graph(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            inputs = self.make_fixture(Path(temporary))

            report = build_dependency_notices(inputs)

            notice = inputs.notice_output.read_text(encoding="utf-8")
            self.assertIn("Component: fmt 11.2.0", notice)
            self.assertIn("Component: serde 1.0.0", notice)
            self.assertIn("Component: UE4SS 6c26f038", notice)
            self.assertIn("fmt MIT license", notice)
            self.assertIn("serde MIT license", notice)
            self.assertIn("UE4SS MIT license", notice)
            self.assertEqual(
                report["notice_sha256"],
                hashlib.sha256(
                    inputs.notice_output.read_bytes()
                ).hexdigest(),
            )
            self.assertEqual(
                json.loads(
                    inputs.report_output.read_text(encoding="utf-8")
                ),
                report,
            )

    def test_refuses_missing_extra_or_changed_evidence_components(
        self,
    ) -> None:
        for mutation in ("missing", "extra", "changed"):
            with self.subTest(mutation=mutation):
                with tempfile.TemporaryDirectory() as temporary:
                    inputs = self.make_fixture(Path(temporary))
                    audit = json.loads(
                        inputs.approved_audit.read_text(encoding="utf-8")
                    )
                    if mutation == "missing":
                        audit["components"].pop()
                    elif mutation == "extra":
                        audit["components"].append(
                            {
                                "name": "unknown",
                                "version": "1.0.0",
                                "source_identity": "cargo:" + "3" * 64,
                                "license_expression": "MIT",
                                "license_files": [],
                            }
                        )
                    else:
                        audit["components"][0]["version"] = "changed"
                    inputs.approved_audit.write_bytes(
                        canonical_json(audit)
                    )
                    with self.assertRaisesRegex(
                        DependencyNoticeError,
                        "component",
                    ):
                        build_dependency_notices(inputs)

    def test_refuses_license_hash_link_and_empty_text(self) -> None:
        cases = ("hash", "link", "parent_link", "empty")
        for mutation in cases:
            with self.subTest(mutation=mutation):
                with tempfile.TemporaryDirectory() as temporary:
                    root = Path(temporary)
                    inputs = self.make_fixture(root)
                    license_path = (
                        inputs.roots["build"]
                        / "_deps/fmt-src/LICENSE.rst"
                    )
                    if mutation == "hash":
                        license_path.write_text(
                            "changed\n",
                            encoding="utf-8",
                        )
                    elif mutation == "empty":
                        license_path.write_bytes(b"")
                    elif mutation == "parent_link":
                        license_directory = license_path.parent
                        original = root / "original-license-directory"
                        license_directory.rename(original)
                        try:
                            license_directory.symlink_to(
                                original,
                                target_is_directory=True,
                            )
                        except OSError:
                            self.skipTest("symbolic links are unavailable")
                    else:
                        original = root / "original-license"
                        license_path.rename(original)
                        try:
                            license_path.symlink_to(original)
                        except OSError:
                            self.skipTest("symbolic links are unavailable")
                    with self.assertRaisesRegex(
                        DependencyNoticeError,
                        "license",
                    ):
                        build_dependency_notices(inputs)

    def test_failure_preserves_prior_outputs(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            inputs = self.make_fixture(root)
            inputs.notice_output.write_text("old notice", encoding="utf-8")
            inputs.report_output.write_text("old report", encoding="utf-8")
            inputs.approved_audit.write_bytes(b"{")

            with self.assertRaises(DependencyNoticeError):
                build_dependency_notices(inputs)

            self.assertEqual(
                inputs.notice_output.read_text(encoding="utf-8"),
                "old notice",
            )
            self.assertEqual(
                inputs.report_output.read_text(encoding="utf-8"),
                "old report",
            )


if __name__ == "__main__":
    unittest.main()
