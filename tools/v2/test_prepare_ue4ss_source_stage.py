#!/usr/bin/env python3
"""Behavior tests for the immutable UE4SS build-source stage."""

from __future__ import annotations

import hashlib
import json
import subprocess
import tempfile
import unittest
from pathlib import Path

from prepare_ue4ss_source_stage import (
    Ue4ssSourceStageError,
    prepare_ue4ss_source_stage,
    verify_ue4ss_source_stage,
)


def run_git(directory: Path, *arguments: str) -> str:
    completed = subprocess.run(
        ["git", "-c", "core.autocrlf=false", "-C", str(directory), *arguments],
        check=True,
        capture_output=True,
        text=True,
    )
    return completed.stdout.strip()


def sha256(contents: bytes) -> str:
    return hashlib.sha256(contents).hexdigest()


def make_simple_fixture(root: Path) -> tuple[Path, Path, Path]:
    source = root / "source"
    source.mkdir()
    run_git(source, "init", "--quiet")
    run_git(source, "config", "user.name", "Meccha test")
    run_git(source, "config", "user.email", "test@example.invalid")
    target = source / "deps/first/patternsleuth_bind/Cargo.lock"
    target.parent.mkdir(parents=True)
    upstream = b"version = 4\n\n[[package]]\nname = \"unused\"\n"
    target.write_bytes(upstream)
    run_git(source, "add", ".")
    run_git(source, "commit", "--quiet", "-m", "fixture")
    source_commit = run_git(source, "rev-parse", "HEAD")
    overlay = root / "canonical.Cargo.lock"
    canonical = b"version = 4\n"
    overlay.write_bytes(canonical)
    reference = root / "reference"
    run_git(root, "clone", "--quiet", "--no-hardlinks", source.name, reference.name)
    (reference / target.relative_to(source)).write_bytes(canonical)
    expected_diff = subprocess.run(
        [
            "git",
            "-c",
            "core.autocrlf=false",
            "-C",
            str(reference),
            "diff",
            "--binary",
            "HEAD",
            "--",
            target.relative_to(source).as_posix(),
        ],
        check=True,
        capture_output=True,
    ).stdout
    policy = root / "policy.json"
    policy.write_text(
        json.dumps(
            {
                "schema_version": 1,
                "ue4ss_commit": source_commit,
                "overlay": {
                    "source": overlay.name,
                    "target": target.relative_to(source).as_posix(),
                    "upstream_sha256": sha256(upstream),
                    "overlay_sha256": sha256(canonical),
                    "staged_diff_sha256": sha256(expected_diff),
                },
                "nested_gitlinks": [],
            }
        ),
        encoding="utf-8",
    )
    return source, policy, target.relative_to(source)


class Ue4ssSourceStageTest(unittest.TestCase):
    def test_prepares_one_exact_overlay_without_mutating_upstream(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / "source"
            source.mkdir()
            run_git(source, "init", "--quiet")
            run_git(source, "config", "user.name", "Meccha test")
            run_git(source, "config", "user.email", "test@example.invalid")
            target = source / "deps/first/patternsleuth_bind/Cargo.lock"
            target.parent.mkdir(parents=True)
            upstream = b"version = 4\n\n[[package]]\nname = \"unused\"\n"
            target.write_bytes(upstream)
            (source / "CMakeLists.txt").write_text(
                "cmake_minimum_required(VERSION 3.25)\n",
                encoding="utf-8",
            )
            run_git(source, "add", ".")
            run_git(source, "commit", "--quiet", "-m", "fixture")
            source_commit = run_git(source, "rev-parse", "HEAD")

            overlay = root / "canonical.Cargo.lock"
            canonical = b"version = 4\n"
            overlay.write_bytes(canonical)

            reference = root / "reference"
            run_git(
                root,
                "clone",
                "--quiet",
                "--no-hardlinks",
                source.name,
                reference.name,
            )
            reference_target = reference / target.relative_to(source)
            reference_target.write_bytes(canonical)
            expected_diff = subprocess.run(
                [
                    "git",
                    "-c",
                    "core.autocrlf=false",
                    "-C",
                    str(reference),
                    "diff",
                    "--binary",
                    "HEAD",
                    "--",
                    target.relative_to(source).as_posix(),
                ],
                check=True,
                capture_output=True,
            ).stdout

            policy = root / "policy.json"
            policy.write_text(
                json.dumps(
                    {
                        "schema_version": 1,
                        "ue4ss_commit": source_commit,
                        "overlay": {
                            "source": overlay.name,
                            "target": target.relative_to(source).as_posix(),
                            "upstream_sha256": sha256(upstream),
                            "overlay_sha256": sha256(canonical),
                            "staged_diff_sha256": sha256(expected_diff),
                        },
                        "nested_gitlinks": [],
                    }
                ),
                encoding="utf-8",
            )
            stage = root / "stage"
            manifest = root / "stage-manifest.json"

            result = prepare_ue4ss_source_stage(
                policy_path=policy,
                source_root=source,
                output_root=stage,
                manifest_path=manifest,
            )

            self.assertEqual(target.read_bytes(), upstream)
            self.assertEqual(run_git(source, "status", "--porcelain"), "")
            self.assertEqual(
                (stage / target.relative_to(source)).read_bytes(),
                canonical,
            )
            self.assertEqual(
                run_git(stage, "diff", "--name-only", "HEAD", "--"),
                target.relative_to(source).as_posix(),
            )
            self.assertEqual(result["ue4ss_commit"], source_commit)
            self.assertEqual(
                result["overlay"]["staged_diff_sha256"],
                sha256(expected_diff),
            )
            self.assertEqual(
                json.loads(manifest.read_text(encoding="utf-8")),
                result,
            )

    def test_clones_only_the_accepted_initialized_nested_gitlinks(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            nested_sources: list[tuple[str, Path, str]] = []
            for name, relative in (
                ("Unreal", "deps/first/Unreal"),
                ("patternsleuth", "deps/first/patternsleuth"),
            ):
                nested = root / f"{name}-source"
                nested.mkdir()
                run_git(nested, "init", "--quiet")
                run_git(nested, "config", "user.name", "Meccha test")
                run_git(nested, "config", "user.email", "test@example.invalid")
                (nested / "identity.txt").write_text(name, encoding="utf-8")
                run_git(nested, "add", ".")
                run_git(nested, "commit", "--quiet", "-m", name)
                nested_sources.append(
                    (relative, nested, run_git(nested, "rev-parse", "HEAD"))
                )

            source = root / "source"
            source.mkdir()
            run_git(source, "init", "--quiet")
            run_git(source, "config", "user.name", "Meccha test")
            run_git(source, "config", "user.email", "test@example.invalid")
            for relative, nested, _ in nested_sources:
                run_git(
                    source,
                    "-c",
                    "protocol.file.allow=always",
                    "submodule",
                    "add",
                    "--quiet",
                    str(nested),
                    relative,
                )
            target = source / "deps/first/patternsleuth_bind/Cargo.lock"
            target.parent.mkdir(parents=True)
            upstream = b"version = 4\n\n[[package]]\nname = \"unused\"\n"
            target.write_bytes(upstream)
            run_git(source, "add", ".")
            run_git(source, "commit", "--quiet", "-m", "fixture")
            source_commit = run_git(source, "rev-parse", "HEAD")

            overlay = root / "canonical.Cargo.lock"
            canonical = b"version = 4\n"
            overlay.write_bytes(canonical)
            reference = root / "reference"
            run_git(
                root,
                "clone",
                "--quiet",
                "--no-hardlinks",
                source.name,
                reference.name,
            )
            reference_target = reference / target.relative_to(source)
            reference_target.write_bytes(canonical)
            expected_diff = subprocess.run(
                [
                    "git",
                    "-c",
                    "core.autocrlf=false",
                    "-C",
                    str(reference),
                    "diff",
                    "--binary",
                    "HEAD",
                    "--",
                    target.relative_to(source).as_posix(),
                ],
                check=True,
                capture_output=True,
            ).stdout
            policy = root / "policy.json"
            policy.write_text(
                json.dumps(
                    {
                        "schema_version": 1,
                        "ue4ss_commit": source_commit,
                        "overlay": {
                            "source": overlay.name,
                            "target": target.relative_to(source).as_posix(),
                            "upstream_sha256": sha256(upstream),
                            "overlay_sha256": sha256(canonical),
                            "staged_diff_sha256": sha256(expected_diff),
                        },
                        "nested_gitlinks": [
                            {"path": relative, "commit": commit}
                            for relative, _, commit in nested_sources
                        ],
                    }
                ),
                encoding="utf-8",
            )
            stage = root / "stage"

            result = prepare_ue4ss_source_stage(
                policy_path=policy,
                source_root=source,
                output_root=stage,
                manifest_path=root / "stage-manifest.json",
            )

            self.assertEqual(
                result["nested_gitlinks"],
                [
                    {"path": relative, "commit": commit}
                    for relative, _, commit in nested_sources
                ],
            )
            for relative, _, commit in nested_sources:
                staged_nested = stage / relative
                self.assertEqual(run_git(staged_nested, "rev-parse", "HEAD"), commit)
                self.assertEqual(run_git(staged_nested, "status", "--porcelain"), "")
            self.assertEqual(
                run_git(
                    stage,
                    "status",
                    "--porcelain",
                    "--untracked-files=all",
                    "--ignore-submodules=none",
                ),
                f"M {target.relative_to(source).as_posix()}",
            )

    def test_reuses_one_verified_stage_without_generation_accumulation(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source, policy, _ = make_simple_fixture(root)
            stage = root / "stage"
            manifest = root / "stage-manifest.json"
            first = prepare_ue4ss_source_stage(
                policy_path=policy,
                source_root=source,
                output_root=stage,
                manifest_path=manifest,
            )

            second = prepare_ue4ss_source_stage(
                policy_path=policy,
                source_root=source,
                output_root=stage,
                manifest_path=manifest,
            )

            self.assertEqual(second, first)
            self.assertEqual(
                sorted(path.name for path in root.glob("stage*")),
                ["stage", "stage-manifest.json"],
            )

    def test_accepts_clean_crlf_source_checkout_using_git_blob_identity(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source, policy, target = make_simple_fixture(root)
            crlf_source = root / "source-crlf"
            subprocess.run(
                [
                    "git",
                    "-c",
                    "core.autocrlf=true",
                    "clone",
                    "--quiet",
                    "--no-hardlinks",
                    str(source),
                    str(crlf_source),
                ],
                check=True,
            )
            subprocess.run(
                [
                    "git",
                    "-C",
                    str(crlf_source),
                    "config",
                    "core.autocrlf",
                    "true",
                ],
                check=True,
            )
            subprocess.run(
                [
                    "git",
                    "-C",
                    str(crlf_source),
                    "checkout",
                    "--quiet",
                    "--force",
                    "HEAD",
                ],
                check=True,
            )
            self.assertIn(
                b"\r\n",
                (crlf_source / target).read_bytes(),
            )
            self.assertEqual(
                subprocess.check_output(
                    [
                        "git",
                        "-C",
                        str(crlf_source),
                        "status",
                        "--porcelain",
                    ],
                    text=True,
                ).strip(),
                "",
            )

            result = prepare_ue4ss_source_stage(
                policy_path=policy,
                source_root=crlf_source,
                output_root=root / "stage",
                manifest_path=root / "stage-manifest.json",
            )

            self.assertEqual(
                result["ue4ss_commit"],
                run_git(source, "rev-parse", "HEAD"),
            )
            self.assertEqual(
                (root / "stage" / target).read_bytes(),
                b"version = 4\n",
            )

    def test_refuses_existing_stage_with_any_unapproved_change(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source, policy, _ = make_simple_fixture(root)
            stage = root / "stage"
            manifest = root / "stage-manifest.json"
            prepare_ue4ss_source_stage(
                policy_path=policy,
                source_root=source,
                output_root=stage,
                manifest_path=manifest,
            )
            unexpected = stage / "unapproved.txt"
            unexpected.write_text("not part of the overlay", encoding="utf-8")

            with self.assertRaisesRegex(
                Ue4ssSourceStageError,
                "one-file overlay",
            ):
                prepare_ue4ss_source_stage(
                    policy_path=policy,
                    source_root=source,
                    output_root=stage,
                    manifest_path=manifest,
                )

            self.assertEqual(
                unexpected.read_text(encoding="utf-8"),
                "not part of the overlay",
            )

    def test_post_build_verification_rechecks_the_exact_published_stage(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source, policy, target = make_simple_fixture(root)
            stage = root / "stage"
            manifest = root / "stage-manifest.json"
            prepared = prepare_ue4ss_source_stage(
                policy_path=policy,
                source_root=source,
                output_root=stage,
                manifest_path=manifest,
            )

            self.assertEqual(
                verify_ue4ss_source_stage(
                    policy_path=policy,
                    output_root=stage,
                    manifest_path=manifest,
                ),
                prepared,
            )
            (stage / target).write_text("changed after configure\n", encoding="utf-8")
            with self.assertRaisesRegex(
                Ue4ssSourceStageError,
                "overlay bytes",
            ):
                verify_ue4ss_source_stage(
                    policy_path=policy,
                    output_root=stage,
                    manifest_path=manifest,
                )

    def test_refuses_any_output_beneath_the_accepted_source_checkout(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source, policy, _ = make_simple_fixture(root)

            with self.assertRaisesRegex(
                Ue4ssSourceStageError,
                "outside the accepted source",
            ):
                prepare_ue4ss_source_stage(
                    policy_path=policy,
                    source_root=source,
                    output_root=source / "build-stage",
                    manifest_path=root / "stage-manifest.json",
                )

            self.assertEqual(run_git(source, "status", "--porcelain"), "")


if __name__ == "__main__":
    unittest.main()
