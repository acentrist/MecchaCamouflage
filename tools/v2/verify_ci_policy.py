#!/usr/bin/env python3
"""Verify that public and credentialed v2 CI remain separate."""

from __future__ import annotations

import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
LEGACY_WORKFLOW = ROOT / ".github/workflows/ci.yml"
PUBLIC_WORKFLOW = ROOT / ".github/workflows/v2-ci.yml"
TRUSTED_WORKFLOW = ROOT / ".github/workflows/v2-full-build.yml"
RELEASE_WORKFLOW = ROOT / ".github/workflows/v2-release-candidate.yml"
RELEASE_SCRIPT = ROOT / "tools/v2/build-release-candidate.ps1"
FULL_BUILD_VERIFIER = ROOT / "tools/v2/verify-full-build.ps1"
CHANGE_POLICY = ROOT / "tools/v2/ci_change_policy.py"
V2_WORKFLOWS = tuple(sorted((ROOT / ".github/workflows").glob("v2-*.yml")))


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


def require_occurrences(
    text: str,
    fragment: str,
    count: int,
    label: str,
) -> None:
    actual = text.count(fragment)
    if actual != count:
        fail(
            f"{label} requires {count} occurrences of {fragment!r}; "
            f"found {actual}"
        )


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


def verify_windows_only_v2_workflows() -> None:
    if not V2_WORKFLOWS:
        fail("no v2 workflows were found")
    for workflow in V2_WORKFLOWS:
        text = workflow.read_text(encoding="utf-8")
        runners = re.findall(r"^\s*runs-on:\s*([^#\r\n]+)", text, re.MULTILINE)
        if not runners:
            fail(f"{workflow.relative_to(ROOT)} has no explicit runner")
        unsupported = sorted(
            runner.strip() for runner in runners if runner.strip() != "windows-2022"
        )
        if unsupported:
            fail(
                f"{workflow.relative_to(ROOT)} must use only windows-2022; "
                f"found {unsupported}"
            )


def verify_public_workflow() -> None:
    if not PUBLIC_WORKFLOW.exists():
        fail(f"{PUBLIC_WORKFLOW.relative_to(ROOT)} is missing")
    text = PUBLIC_WORKFLOW.read_text(encoding="utf-8")
    require_fragments(
        text,
        {
            "pull_request:",
            "workflow_dispatch:",
            "concurrency:",
            "group: ${{ github.workflow }}-${{ github.event.pull_request.number || github.ref }}",
            "cancel-in-progress: true",
            "V2_SOURCE_COMMIT:",
            "github.event.pull_request.head.sha",
            "github.event.before",
            "ref: ${{ env.V2_SOURCE_COMMIT }}",
            "fetch-depth: 0",
            "windows-2022",
            "submodules: false",
            "policy-contracts:",
            "run_heavy: ${{ steps.change-policy.outputs.run_heavy }}",
            "py -3 tools/v2/ci_change_policy.py",
            '--repository "$env:GITHUB_REPOSITORY"',
            '--api-url "$env:GITHUB_API_URL"',
            "py -3 tools/v2/verify_phase1.py",
            "py -3 tools/v2/verify_ci_policy.py",
            "needs: policy-contracts",
            "if: needs.policy-contracts.outputs.run_heavy == 'true'",
            "git submodule update --init third_party/RE-UE4SS",
            "-DMECCHA_WITH_UE4SS=OFF",
            "windows-msvc:",
            "name: Windows MSVC Tests",
            "windows-msvc-asan:",
            "name: Windows MSVC ASan",
            "windows-clang-ubsan:",
            "name: Windows clang-cl UBSan",
            "windows-msvc-analysis:",
            "name: Windows MSVC Code Analysis",
            "github.event_name == 'workflow_dispatch'",
            "-DMECCHA_BUILD_TESTS=OFF",
            "-DMECCHA_ENABLE_MSVC_CODE_ANALYSIS=ON",
            "--target meccha_product_ui meccha_runtime_contracts",
            "meccha_launcher_core",
            "-DMECCHA_ENABLE_MSVC_ADDRESS_SANITIZER=ON",
            "-DMECCHA_ENABLE_CLANG_CL_UBSAN=ON",
            "ctest",
            "public-ci:",
            "name: Public CI Gate",
            "if: always()",
            "needs.policy-contracts.result",
        },
        "public workflow",
    )
    require_occurrences(
        text,
        "ref: ${{ env.V2_SOURCE_COMMIT }}",
        5,
        "public workflow",
    )
    require_occurrences(
        text,
        "if: needs.policy-contracts.outputs.run_heavy == 'true'",
        1,
        "public workflow",
    )
    require_occurrences(
        text,
        "github.event_name == 'workflow_dispatch'",
        3,
        "public workflow",
    )
    forbid_fragments(
        text,
        {
            "push:",
            "secrets.",
            "--recursive",
            "MECCHA_WITH_UE4SS=ON",
            "UE4SS_GITHUB_TOKEN",
            "ubuntu",
            "linux-static-analysis",
            "linux-sanitizers",
            "MECCHA_ENABLE_GCC_ANALYZER",
            "MECCHA_ENABLE_SANITIZERS",
            "-fanalyzer",
            "detect_leaks",
            "LSAN_OPTIONS",
        },
        "public workflow",
    )


def verify_public_change_policy() -> None:
    if not CHANGE_POLICY.exists():
        fail(f"{CHANGE_POLICY.relative_to(ROOT)} is missing")

    tool_directory = str(CHANGE_POLICY.parent)
    sys.path.insert(0, tool_directory)
    try:
        from ci_change_policy import classify_event, requires_heavy_ci
    finally:
        sys.path.remove(tool_directory)

    lightweight_paths = [
        "PLAN.md",
        "docs/v2/static-analysis.md",
        "src/tests/fixtures/v1/manifest.json",
    ]
    if requires_heavy_ci(lightweight_paths):
        fail("documentation/checkpoint-only changes must use lightweight CI")

    heavy_cases = {
        "empty change set": [],
        "production source": ["src/core/src/paint.cpp"],
        "fixture data": ["src/tests/fixtures/v1/paint-domain.json"],
        "workflow policy": [".github/workflows/v2-ci.yml"],
        "classifier policy": ["tools/v2/ci_change_policy.py"],
        "unsafe relative path": ["../outside.md"],
        "non-canonical separator": [r"docs\v2\static-analysis.md"],
        "control-character path": ["docs/v2/bad\nname.md"],
    }
    for label, paths in heavy_cases.items():
        if not requires_heavy_ci(paths):
            fail(f"{label} must fail closed to heavy CI")

    fail_closed_events = {
        "manual run": ("workflow_dispatch", "", "", "f" * 40),
        "initial pull request": ("pull_request", "opened", "", "f" * 40),
        "missing synchronization range": (
            "pull_request",
            "synchronize",
            "",
            "f" * 40,
        ),
    }
    for label, event in fail_closed_events.items():
        run_heavy, _, _ = classify_event(*event, repository=ROOT)
        if not run_heavy:
            fail(f"{label} must fail closed to heavy CI")

    synchronized_event = (
        "pull_request",
        "synchronize",
        "e" * 40,
        "f" * 40,
    )
    docs_reader = lambda _before, _after, _repository: ["docs/v2/status.md"]
    run_heavy, _, _ = classify_event(
        *synchronized_event,
        repository=ROOT,
        changed_paths_reader=docs_reader,
        prior_gate_reader=lambda _commit: True,
    )
    if run_heavy:
        fail("documentation-only change with prior green gate must stay lightweight")

    for prior_result in (False, None):
        run_heavy, _, _ = classify_event(
            *synchronized_event,
            repository=ROOT,
            changed_paths_reader=docs_reader,
            prior_gate_reader=lambda _commit, result=prior_result: result,
        )
        if not run_heavy:
            fail("lightweight CI must require a successful prior aggregate gate")


def verify_legacy_workflow() -> None:
    if not LEGACY_WORKFLOW.exists():
        fail(f"{LEGACY_WORKFLOW.relative_to(ROOT)} is missing")
    text = LEGACY_WORKFLOW.read_text(encoding="utf-8")
    require_fragments(
        text,
        {
            "if: github.event_name != 'pull_request' || "
            "github.head_ref != 'rewrite/ue4ss-v2'",
            "submodules: recursive",
        },
        "legacy workflow",
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
            "prepare_ue4ss_source_stage.py",
            "--output-root .build\\v2\\ue4ss-source",
            "-DMECCHA_UE4SS_SOURCE_ROOT=",
            "-DMECCHA_UE4SS_SOURCE_MANIFEST=",
            "--verify-only",
            "-ProjectCommit ${{ github.sha }}",
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
            "windows-msvc-asan:",
            "name: Pre-release MSVC ASan",
            "windows-clang-ubsan:",
            "name: Pre-release clang-cl UBSan",
            "windows-msvc-analysis:",
            "name: Pre-release MSVC Code Analysis",
            "-DMECCHA_ENABLE_MSVC_ADDRESS_SANITIZER=ON",
            "-DMECCHA_ENABLE_CLANG_CL_UBSAN=ON",
            "-DMECCHA_ENABLE_MSVC_CODE_ANALYSIS=ON",
            "--target meccha_product_ui meccha_runtime_contracts",
            "needs:",
            "- windows-msvc-asan",
            "- windows-clang-ubsan",
            "- windows-msvc-analysis",
            "environment: ue4ss-full-build",
            "github.repository == 'acentrist/MecchaCamouflage'",
            "persist-credentials: false",
            "secrets.UE4SS_GITHUB_TOKEN",
            "submodule update --init --recursive third_party/RE-UE4SS",
            "prepare_ue4ss_source_stage.py",
            "--output-root .build\\v2\\ue4ss-source",
            "-DMECCHA_UE4SS_SOURCE_ROOT=",
            "-DMECCHA_UE4SS_SOURCE_MANIFEST=",
            "--verify-only",
            "-ProjectCommit ${{ github.sha }}",
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
            '[string]$Ue4ssSourceRoot',
            '[string]$Ue4ssSourceManifest',
            '[string]$ProjectCommit',
            "prepare_ue4ss_source_stage.py",
            "--verify-only",
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
            '"--ue4ss-source-root", $ResolvedUe4ssSourceRoot',
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


def verify_full_build_verifier() -> None:
    if not FULL_BUILD_VERIFIER.exists():
        fail(f"{FULL_BUILD_VERIFIER.relative_to(ROOT)} is missing")
    text = FULL_BUILD_VERIFIER.read_text(encoding="utf-8")
    require_fragments(
        text,
        {
            '[string]$ProjectCommit',
            "CMAKE_HOME_DIRECTORY:INTERNAL=",
            "git -C $ProjectRoot",
            "--ignore-submodules=none",
            "$ProjectHead -cne $ProjectCommit",
            "CMakeCXXCompiler.cmake",
            '$ConfiguredCompilerId -cne "MSVC"',
            '$ConfiguredCompilerArchitecture -cne "x64"',
            "$ConfiguredCompilerPath -cne $CompilerPath",
            "$ConfiguredCompilerVersion -cne $CompilerVersion",
            "source_commit = $ProjectHead",
        },
        "full-build verifier",
    )


def main() -> int:
    try:
        verify_windows_only_v2_workflows()
        verify_legacy_workflow()
        verify_public_workflow()
        verify_public_change_policy()
        verify_trusted_workflow()
        verify_release_workflow()
        verify_release_script()
        verify_full_build_verifier()
    except (RuntimeError, OSError) as error:
        print(f"FAIL ci_policy: {error}", file=sys.stderr)
        return 1
    print(
        "PASS ci_policy: v2 runners=Windows-only, public secrets=0, "
        "deep gates=ASan+UBSan+/analyze, recursive checkout=trusted-only, "
        "release candidate=manual+protected+non-publishing"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
