#!/usr/bin/env python3

from __future__ import annotations

import hashlib
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


sys.path.insert(0, str(Path(__file__).resolve().parent))

from collect_dependency_evidence import (  # noqa: E402
    DependencyEvidenceError,
    DependencyEvidenceInputs,
    collect_dependency_evidence,
)


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


def cargo_lock(include_unused: bool) -> str:
    lines = [
        "version = 4",
        "",
        "[[package]]",
        'name = "patternsleuth_bind"',
        'version = "0.1.0"',
        'dependencies = ["serde"]',
        "",
        "[[package]]",
        'name = "serde"',
        'version = "1.0.0"',
        'source = "registry+https://github.com/rust-lang/crates.io-index"',
        f'checksum = "{"2" * 64}"',
        "",
    ]
    if include_unused:
        lines.extend(
            (
                "[[package]]",
                'name = "unused"',
                'version = "1.0.0"',
                "",
            )
        )
    return "\n".join(lines)


class DependencyEvidenceTests(unittest.TestCase):
    def init_repository(self, path: Path, marker: str) -> str:
        path.mkdir(parents=True)
        (path / "LICENSE").write_text(
            f"{marker} license\n",
            encoding="utf-8",
        )
        subprocess.run(
            ["git", "init", "-q", str(path)],
            check=True,
        )
        subprocess.run(
            ["git", "-C", str(path), "add", "LICENSE"],
            check=True,
        )
        subprocess.run(
            [
                "git",
                "-C",
                str(path),
                "-c",
                "user.name=Evidence Test",
                "-c",
                "user.email=evidence@example.invalid",
                "commit",
                "-q",
                "-m",
                "fixture",
            ],
            check=True,
        )
        return subprocess.check_output(
            ["git", "-C", str(path), "rev-parse", "HEAD"],
            text=True,
        ).strip()

    def make_fixture(self, root: Path) -> tuple[
        DependencyEvidenceInputs,
        dict[str, str],
    ]:
        project = root / "project"
        build = root / "build"
        cargo = root / "cargo"
        project_head = self.init_repository(project, "project")
        ue4ss = project / "third_party/ue4ss"
        self.init_repository(ue4ss, "ue4ss")
        bind = ue4ss / "deps/first/patternsleuth_bind"
        bind.mkdir(parents=True)
        (bind / "Cargo.toml").write_text(
            "[package]\nname='patternsleuth_bind'\nversion='0.1.0'\n",
            encoding="utf-8",
        )
        lock_path = bind / "Cargo.lock"
        upstream_lock = cargo_lock(include_unused=True)
        lock_path.write_text(upstream_lock, encoding="utf-8")
        subprocess.run(
            ["git", "-C", str(ue4ss), "add", "."],
            check=True,
        )
        subprocess.run(
            [
                "git",
                "-C",
                str(ue4ss),
                "-c",
                "user.name=Evidence Test",
                "-c",
                "user.email=evidence@example.invalid",
                "commit",
                "-q",
                "-m",
                "source stage base",
            ],
            check=True,
        )
        ue4ss_head = subprocess.check_output(
            ["git", "-C", str(ue4ss), "rev-parse", "HEAD"],
            text=True,
        ).strip()
        overlay_lock = cargo_lock(include_unused=False)
        lock_path.write_text(overlay_lock, encoding="utf-8")
        staged_diff = subprocess.run(
            [
                "git",
                "-C",
                str(ue4ss),
                "diff",
                "--binary",
                "HEAD",
                "--",
                "deps/first/patternsleuth_bind/Cargo.lock",
            ],
            check=True,
            capture_output=True,
        ).stdout
        fmt = build / "_deps/fmt-src"
        fmt_head = self.init_repository(fmt, "fmt")
        cargo.mkdir()
        for path in (
            project / "src/mod",
            ue4ss / "proxy",
        ):
            path.mkdir(parents=True, exist_ok=True)
        serde_root = (
            cargo / "registry/src/example/serde-1.0.0"
        )
        serde_root.mkdir(parents=True)
        (serde_root / "Cargo.toml").write_text(
            "[package]\nname='serde'\nversion='1.0.0'\n",
            encoding="utf-8",
        )
        (serde_root / ".cargo-checksum.json").write_bytes(
            canonical_json({"files": {}, "package": "2" * 64})
        )

        reply = build / ".cmake/api/v1/reply"
        reply.mkdir(parents=True)
        targets = (
            (
                "meccha_mod",
                "meccha_mod::id",
                "SHARED_LIBRARY",
                project / "src/mod",
                ["UE4SS::id"],
            ),
            (
                "UE4SS",
                "UE4SS::id",
                "SHARED_LIBRARY",
                ue4ss,
                ["fmt::id"],
            ),
            (
                "proxy",
                "proxy::id",
                "SHARED_LIBRARY",
                ue4ss / "proxy",
                ["fmt::id"],
            ),
            (
                "fmt",
                "fmt::id",
                "STATIC_LIBRARY",
                fmt,
                [],
            ),
        )
        summaries = []
        for name, target_id, target_type, source, dependencies in targets:
            json_file = f"target-{name}.json"
            summaries.append(
                {
                    "name": name,
                    "id": target_id,
                    "jsonFile": json_file,
                }
            )
            (reply / json_file).write_bytes(
                canonical_json(
                    {
                        "name": name,
                        "id": target_id,
                        "type": target_type,
                        "paths": {
                            "source": str(source),
                            "build": str(build / "targets" / name),
                        },
                        "dependencies": [
                            {"id": dependency}
                            for dependency in dependencies
                        ],
                    }
                )
            )
        codemodel_file = "codemodel-v2-test.json"
        (reply / codemodel_file).write_bytes(
            canonical_json(
                {
                    "kind": "codemodel",
                    "version": {"major": 2, "minor": 8},
                    "paths": {
                        "source": str(project),
                        "build": str(build),
                    },
                    "configurations": [
                        {
                            "name": "Game__Shipping__Win64",
                            "targets": summaries,
                        }
                    ],
                }
            )
        )
        (reply / "index-test.json").write_bytes(
            canonical_json(
                {
                    "cmake": {
                        "version": {
                            "major": 4,
                            "minor": 0,
                            "patch": 0,
                            "string": "4.0.0",
                        }
                    },
                    "objects": [
                        {
                            "kind": "codemodel",
                            "version": {"major": 2, "minor": 8},
                            "jsonFile": codemodel_file,
                        }
                    ],
                }
            )
        )

        root_id = (
            "path+file://"
            + bind.as_posix()
            + "#patternsleuth_bind@0.1.0"
        )
        serde_id = (
            "registry+https://github.com/rust-lang/crates.io-index"
            "#serde@1.0.0"
        )
        cargo_metadata = {
            "packages": [
                {
                    "name": "patternsleuth_bind",
                    "version": "0.1.0",
                    "id": root_id,
                    "source": None,
                    "manifest_path":
                        str(bind / "Cargo.toml"),
                },
                {
                    "name": "serde",
                    "version": "1.0.0",
                    "id": serde_id,
                    "source":
                        "registry+https://github.com/"
                        "rust-lang/crates.io-index",
                    "manifest_path":
                        str(serde_root / "Cargo.toml"),
                },
            ],
            "resolve": {
                "root": root_id,
                "nodes": [
                    {
                        "id": root_id,
                        "deps": [
                            {"name": "serde", "pkg": serde_id}
                        ],
                    },
                    {"id": serde_id, "deps": []},
                ],
            },
        }
        metadata_path = root / "cargo-metadata.json"
        metadata_path.write_bytes(canonical_json(cargo_metadata))
        source_stage_manifest = root / "ue4ss-source-stage.json"
        source_stage = {
            "schema_version": 1,
            "owner": "MecchaCamouflage",
            "ue4ss_commit": ue4ss_head,
            "policy_sha256": "3" * 64,
            "overlay": {
                "target": "deps/first/patternsleuth_bind/Cargo.lock",
                "upstream_sha256": hashlib.sha256(
                    upstream_lock.encode("utf-8")
                ).hexdigest(),
                "overlay_sha256": hashlib.sha256(
                    overlay_lock.encode("utf-8")
                ).hexdigest(),
                "staged_diff_sha256": hashlib.sha256(
                    staged_diff
                ).hexdigest(),
            },
            "nested_gitlinks": [],
        }
        source_stage_manifest.write_bytes(canonical_json(source_stage))
        inputs = DependencyEvidenceInputs(
            project_root=project,
            build_root=build,
            cargo_root=cargo,
            reply_directory=reply,
            configuration="Game__Shipping__Win64",
            root_targets=("meccha_mod", "proxy", "UE4SS"),
            cargo_metadata=metadata_path,
            cargo_lock=lock_path,
            ue4ss_source_root=ue4ss,
            ue4ss_source_manifest=source_stage_manifest,
            cargo_root_package="patternsleuth_bind",
            output=root / "dependency-evidence.json",
            ue4ss_commit=ue4ss_head,
        )
        return inputs, {
            "project": project_head,
            "ue4ss": ue4ss_head,
            "fmt": fmt_head,
        }

    def test_collects_closed_cmake_git_and_cargo_graph(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            inputs, commits = self.make_fixture(Path(temporary))

            evidence = collect_dependency_evidence(inputs)

            self.assertEqual(
                evidence["root_targets"],
                [
                    "meccha_mod@project:src/mod",
                    "proxy@ue4ss:proxy",
                    "UE4SS@ue4ss",
                ],
            )
            self.assertEqual(len(evidence["target_graph"]), 4)
            self.assertEqual(
                evidence["ue4ss_source_stage"]["ue4ss_commit"],
                commits["ue4ss"],
            )
            self.assertEqual(
                evidence["ue4ss_source_stage"]["manifest_sha256"],
                hashlib.sha256(
                    inputs.ue4ss_source_manifest.read_bytes()
                ).hexdigest(),
            )
            components = {
                component["name"]: component
                for component in evidence["components"]
            }
            self.assertIn("git:build:_deps/fmt-src", components)
            self.assertIn(
                "git:ue4ss",
                components,
            )
            self.assertNotIn("git:project", components)
            self.assertIn(
                "cargo:patternsleuth_bind@0.1.0",
                components,
            )
            self.assertEqual(
                components["cargo:serde@1.0.0"]["source_identity"],
                "cargo:" + "2" * 64 + ":features:none",
            )
            self.assertIn(
                commits["ue4ss"],
                components[
                    "git:ue4ss"
                ]["source_identity"],
            )
            self.assertEqual(
                json.loads(inputs.output.read_text(encoding="utf-8")),
                evidence,
            )

    def test_refuses_missing_target_dependency_and_configuration(
        self,
    ) -> None:
        for mutation in ("dependency", "configuration"):
            with self.subTest(mutation=mutation):
                with tempfile.TemporaryDirectory() as temporary:
                    inputs, _ = self.make_fixture(Path(temporary))
                    if mutation == "dependency":
                        (
                            inputs.reply_directory / "target-fmt.json"
                        ).unlink()
                    else:
                        inputs.configuration = "Release"
                    with self.assertRaisesRegex(
                        DependencyEvidenceError,
                        "target|configuration",
                    ):
                        collect_dependency_evidence(inputs)

    def test_refuses_ue4ss_source_stage_lock_mutation(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            inputs, _ = self.make_fixture(Path(temporary))
            inputs.cargo_lock.write_text(
                inputs.cargo_lock.read_text(encoding="utf-8").replace(
                    "2" * 64,
                    "3" * 64,
                ),
                encoding="utf-8",
            )
            with self.assertRaisesRegex(
                DependencyEvidenceError,
                "Cargo.lock hash",
            ):
                collect_dependency_evidence(inputs)

    def test_refuses_cargo_registry_checksum_mismatch(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            inputs, _ = self.make_fixture(Path(temporary))
            checksum = (
                inputs.cargo_root
                / "registry/src/example/serde-1.0.0/.cargo-checksum.json"
            )
            checksum.write_bytes(
                canonical_json({"files": {}, "package": "3" * 64})
            )
            with self.assertRaisesRegex(
                DependencyEvidenceError,
                "checksum",
            ):
                collect_dependency_evidence(inputs)

    def test_tracked_source_change_changes_bound_identity(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            inputs, _ = self.make_fixture(Path(temporary))
            first = collect_dependency_evidence(inputs)
            inputs.output.unlink()
            (
                inputs.build_root / "_deps/fmt-src/LICENSE"
            ).write_text("patched fmt license\n", encoding="utf-8")

            second = collect_dependency_evidence(inputs)

            first_fmt = next(
                component
                for component in first["components"]
                if component["name"] == "git:build:_deps/fmt-src"
            )
            second_fmt = next(
                component
                for component in second["components"]
                if component["name"] == "git:build:_deps/fmt-src"
            )
            self.assertNotEqual(
                first_fmt["source_identity"],
                second_fmt["source_identity"],
            )


if __name__ == "__main__":
    unittest.main()
