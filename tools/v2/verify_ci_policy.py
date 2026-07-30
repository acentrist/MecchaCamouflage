#!/usr/bin/env python3
"""Verify that public and credentialed v2 CI remain separate."""

from __future__ import annotations

import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
PUBLIC_WORKFLOW = ROOT / ".github/workflows/v2-ci.yml"
TRUSTED_WORKFLOW = ROOT / ".github/workflows/v2-full-build.yml"


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


def verify_public_workflow() -> None:
    if not PUBLIC_WORKFLOW.exists():
        fail(f"{PUBLIC_WORKFLOW.relative_to(ROOT)} is missing")
    text = PUBLIC_WORKFLOW.read_text(encoding="utf-8")
    require_fragments(
        text,
        {
            "pull_request:",
            "windows-2022",
            "submodules: false",
            "git submodule update --init third_party/RE-UE4SS",
            "-DMECCHA_WITH_UE4SS=OFF",
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
        },
        "trusted workflow",
    )
    forbid_fragments(text, {"pull_request:"}, "trusted workflow")


def main() -> int:
    try:
        verify_public_workflow()
        verify_trusted_workflow()
    except (RuntimeError, OSError) as error:
        print(f"FAIL ci_policy: {error}", file=sys.stderr)
        return 1
    print("PASS ci_policy: public secrets=0, recursive checkout=trusted-only")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
