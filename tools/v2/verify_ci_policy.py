#!/usr/bin/env python3
"""Verify that public and credentialed v2 CI remain separate."""

from __future__ import annotations

import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
PUBLIC_WORKFLOW = ROOT / ".github/workflows/v2-ci.yml"
TRUSTED_WORKFLOW = ROOT / ".github/workflows/v2-full-build.yml"
RELEASE_WORKFLOW = ROOT / ".github/workflows/v2-release-candidate.yml"
RELEASE_SCRIPT = ROOT / "tools/v2/build-release-candidate.ps1"


def fail(message: str) -> None:
    raise RuntimeError(message)


def require_fragments(text: str, fragments: set[str], label: str) -> None:
    missing = sorted(fragment for fragment in fragments if fragment not in text)
    if missing:
        fail(f"{label} lacks required policy fragments: {missing}")


def forbid_fragments(text: str, fragments: set[str], label: str) -> None:
    present = sorted(fragment for fragment in fragments if fragment in text)
    if present:
        fail(f"{label} contains forbidden policy fragments: {present}")


def require_ordered_fragments(
    text: str,
    fragments: tuple[str, ...],
    label: str,
) -> None:
    offset = 0
    for fragment in fragments:
        position = text.find(fragment, offset)
        if position < 0:
            fail(
                f"{label} lacks ordered policy fragment: {fragment}"
            )
        offset = position + len(fragment)


def verify_public_workflow() -> None:
    if not PUBLIC_WORKFLOW.exists():
        fail(f"{PUBLIC_WORKFLOW.relative_to(ROOT)} is missing")
    text = PUBLIC_WORKFLOW.read_text(encoding="utf-8")
    require_fragments(
        text,
        {
            "pull_request:",
            "ubuntu-24.04",
            "windows-2022",
            "submodules: false",
            "git submodule update --init third_party/RE-UE4SS",
            "-DMECCHA_WITH_UE4SS=OFF",
            "-DMECCHA_ENABLE_SANITIZERS=ON",
            "ASAN_OPTIONS:",
            "UBSAN_OPTIONS:",
            "ctest",
        },
        "public workflow",
    )
    forbid_fragments(
        text,
        {
            "secrets.",
            "--recursive",
            "MECCHA_WITH_UE4SS=ON",
            "UE4SS_GITHUB_TOKEN",
        },
        "public workflow",
    )


def verify_trusted_workflow() -> None:
    if not TRUSTED_WORKFLOW.exists():
        fail(f"{TRUSTED_WORKFLOW.relative_to(ROOT)} is missing")
    text = TRUSTED_WORKFLOW.read_text(encoding="utf-8")
    require_fragments(
        text,
        {
            "workflow_dispatch:",
            "environment: ue4ss-full-build",
            "github.repository == 'acentrist/MecchaCamouflage'",
            "secrets.UE4SS_GITHUB_TOKEN",
            "submodule update --init --recursive third_party/RE-UE4SS",
            "cmake --preset full-windows",
            "cmake --build --preset full-windows",
            "generate_dependency_audit_template.py",
            "dependency-audit-template.json",
        },
        "trusted workflow",
    )
    forbid_fragments(text, {"pull_request:"}, "trusted workflow")


def verify_release_workflow() -> None:
    if not RELEASE_WORKFLOW.exists():
        fail(f"{RELEASE_WORKFLOW.relative_to(ROOT)} is missing")
    text = RELEASE_WORKFLOW.read_text(encoding="utf-8")
    require_fragments(
        text,
        {
            "workflow_dispatch:",
            "contents: read",
            "environment: ue4ss-full-build",
            "github.repository == 'acentrist/MecchaCamouflage'",
            "persist-credentials: false",
            "secrets.UE4SS_GITHUB_TOKEN",
            "submodule update --init --recursive third_party/RE-UE4SS",
            "docs\\v2\\approved-dependency-license-audit.json",
            "cmake --preset full-windows",
            "cmake --build --preset full-windows",
            ".\\tools\\v2\\build-release-candidate.ps1",
            ".build/v2/release-candidate/artifact/"
            "meccha-camouflage-v2.0.0.exe",
            ".build/v2/release-candidate/evidence/"
            "meccha-camouflage-v2.0.0.exe.sha256",
            ".build/v2/release-candidate/evidence",
            "if-no-files-found: error",
        },
        "release workflow",
    )
    forbid_fragments(
        text,
        {
            "pull_request:",
            "push:",
            "contents: write",
            "releases: write",
            "softprops/action-gh-release",
            "actions/create-release",
            "ncipollo/release-action",
            "gh release create",
        },
        "release workflow",
    )


def verify_release_script() -> None:
    if not RELEASE_SCRIPT.exists():
        fail(f"{RELEASE_SCRIPT.relative_to(ROOT)} is missing")
    text = RELEASE_SCRIPT.read_text(encoding="utf-8")
    require_fragments(
        text,
        {
            '[string]$ApprovedLicenseAudit',
            'throw "Release candidate root already exists:',
            '$DependencyNotices = Join-Path $Evidence '
            '"THIRD-PARTY-NOTICES.txt"',
            '$ApprovedAuditCopy = Join-Path $Evidence '
            '"approved-dependency-license-audit.json"',
            'Copy-Item -LiteralPath $ResolvedAudit '
            '-Destination $ApprovedAuditCopy',
            'Get-ChildItem -LiteralPath $Artifact -Force |',
            'Remove-Item -Force -Recurse',
        },
        "release candidate script",
    )
    require_ordered_fragments(
        text,
        (
            "verify-full-build.ps1",
            "cargo metadata",
            "collect_dependency_evidence.py",
            "build_dependency_notices.py",
            "assemble_runtime.py",
            "build_payload_cab.py",
            '"-DMECCHA_PAYLOAD_MANIFEST=$PayloadManifest"',
            '"--target", "meccha_camouflage_launcher"',
            '"--test-dir", $ResolvedBuildRoot',
            "verify_release_artifact.py",
        ),
        "release candidate script",
    )
    forbid_fragments(
        text,
        {
            "gh release",
            "Invoke-WebRequest",
            "Start-BitsTransfer",
            "Add-MpPreference",
            "Set-MpPreference",
        },
        "release candidate script",
    )


def main() -> int:
    try:
        verify_public_workflow()
        verify_trusted_workflow()
        verify_release_workflow()
        verify_release_script()
    except (RuntimeError, OSError) as error:
        print(f"FAIL ci_policy: {error}", file=sys.stderr)
        return 1
    print(
        "PASS ci_policy: public secrets=0, recursive checkout=trusted-only, "
        "release candidate=manual+protected+non-publishing"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
