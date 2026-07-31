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
from generate_dependency_audit_template import (  # noqa: E402
    DependencyAuditTemplateError,
    DependencyAuditTemplateInputs,
    generate_dependency_audit_template,
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


def evidence_value() -> dict[str, object]:
    return {
        "schema_version": 1,
        "ue4ss_commit": UE4SS_COMMIT,
        "configuration": "Game__Shipping__Win64",
        "root_targets": ["UE4SS@project:third_party/RE-UE4SS"],
        "target_graph": [
            {
                "key": "UE4SS@project:third_party/RE-UE4SS",
                "name": "UE4SS",
                "type": "SHARED_LIBRARY",
                "source": "project:third_party/RE-UE4SS",
                "dependencies": [],
            }
        ],
        "components": [
            {
                "name": "cargo:serde@1.0.0",
                "version": "1.0.0",
                "source_identity": (
                    "cargo:"
                    + "2" * 64
                    + ":features:derive,std"
                ),
            },
            {
                "name": "git:project:third_party/RE-UE4SS",
                "version": UE4SS_COMMIT[:12],
                "source_identity": f"git:{UE4SS_COMMIT}:diff:none",
            },
        ],
    }


class DependencyAuditTemplateTests(unittest.TestCase):
    def test_generates_exact_unapproved_template(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            evidence = root / "dependency-evidence.json"
            output = root / "dependency-audit-template.json"
            encoded_evidence = canonical_json(evidence_value())
            evidence.write_bytes(encoded_evidence)

            template = generate_dependency_audit_template(
                DependencyAuditTemplateInputs(
                    evidence=evidence,
                    output=output,
                    ue4ss_commit=UE4SS_COMMIT,
                )
            )

            self.assertEqual(
                template,
                {
                    "schema_version": 1,
                    "ue4ss_commit": UE4SS_COMMIT,
                    "dependency_evidence_sha256": hashlib.sha256(
                        encoded_evidence
                    ).hexdigest(),
                    "components": [
                        {
                            **component,
                            "license_expression": "",
                            "license_files": [],
                        }
                        for component in evidence_value()["components"]
                    ],
                },
            )
            self.assertEqual(output.read_bytes(), canonical_json(template))

            ue4ss = root / "ue4ss"
            project = root / "project"
            build = root / "build"
            cargo = root / "cargo"
            for path in (ue4ss, project, build, cargo):
                path.mkdir()
            with self.assertRaisesRegex(
                DependencyNoticeError,
                "license expression",
            ):
                build_dependency_notices(
                    DependencyNoticeInputs(
                        evidence=evidence,
                        approved_audit=output,
                        roots={
                            "ue4ss": ue4ss,
                            "project": project,
                            "build": build,
                            "cargo": cargo,
                        },
                        notice_output=root / "notices.txt",
                        report_output=root / "report.json",
                        ue4ss_commit=UE4SS_COMMIT,
                    )
                )

    def test_is_deterministic_and_binds_changed_evidence(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            evidence = root / "dependency-evidence.json"
            output = root / "dependency-audit-template.json"
            value = evidence_value()
            evidence.write_bytes(canonical_json(value))
            inputs = DependencyAuditTemplateInputs(
                evidence=evidence,
                output=output,
                ue4ss_commit=UE4SS_COMMIT,
            )

            first = generate_dependency_audit_template(inputs)
            first_bytes = output.read_bytes()
            output.unlink()
            second = generate_dependency_audit_template(inputs)

            self.assertEqual(first, second)
            self.assertEqual(first_bytes, output.read_bytes())

            value["components"][0]["source_identity"] = (
                "cargo:" + "3" * 64 + ":features:derive,std"
            )
            evidence.write_bytes(canonical_json(value))
            changed = generate_dependency_audit_template(inputs)
            self.assertNotEqual(
                first["dependency_evidence_sha256"],
                changed["dependency_evidence_sha256"],
            )
            self.assertEqual(
                changed["components"][0]["source_identity"],
                value["components"][0]["source_identity"],
            )

    def test_refuses_invalid_input_without_replacing_output(self) -> None:
        mutations = ("encoding", "commit", "overlap")
        for mutation in mutations:
            with self.subTest(mutation=mutation):
                with tempfile.TemporaryDirectory() as temporary:
                    root = Path(temporary)
                    evidence = root / "dependency-evidence.json"
                    output = root / "dependency-audit-template.json"
                    value = evidence_value()
                    if mutation == "encoding":
                        evidence.write_bytes(
                            json.dumps(value).encode("utf-8")
                        )
                    else:
                        evidence.write_bytes(canonical_json(value))
                    output.write_bytes(b"previous\n")
                    inputs = DependencyAuditTemplateInputs(
                        evidence=evidence,
                        output=(
                            evidence if mutation == "overlap" else output
                        ),
                        ue4ss_commit=(
                            "0" * 40
                            if mutation == "commit"
                            else UE4SS_COMMIT
                        ),
                    )

                    with self.assertRaises(
                        DependencyAuditTemplateError
                    ):
                        generate_dependency_audit_template(inputs)

                    if mutation != "overlap":
                        self.assertEqual(
                            output.read_bytes(),
                            b"previous\n",
                        )


if __name__ == "__main__":
    unittest.main()
