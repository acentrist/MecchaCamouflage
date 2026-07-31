#!/usr/bin/env python3
"""Choose the public v2 CI depth for one pull-request update."""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
import urllib.error
import urllib.parse
import urllib.request
from collections.abc import Callable, Iterable
from pathlib import Path


_FULL_SHA = re.compile(r"[0-9a-fA-F]{40}")
_REPOSITORY = re.compile(r"[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+")
_ZERO_SHA = "0" * 40
_PUBLIC_GATE_NAME = "Public CI Gate"
_MAX_CHECK_RESPONSE_BYTES = 1_048_576
_LIGHTWEIGHT_EXACT_PATHS = {
    "src/tests/fixtures/v1/manifest.json",
}


def _is_lightweight_path(path: str) -> bool:
    if (
        not path
        or path.startswith("/")
        or "\\" in path
        or "\0" in path
        or any(ord(character) < 32 or ord(character) == 127 for character in path)
    ):
        return False
    parts = path.split("/")
    if any(part in {"", ".", ".."} for part in parts):
        return False
    return path.endswith(".md") or path in _LIGHTWEIGHT_EXACT_PATHS


def requires_heavy_ci(paths: Iterable[str]) -> bool:
    """Fail closed unless every changed path is documentation/checkpoint-only."""

    saw_path = False
    for path in paths:
        saw_path = True
        if not _is_lightweight_path(path):
            return True
    return not saw_path


def _valid_commit(value: str) -> bool:
    return value != _ZERO_SHA and _FULL_SHA.fullmatch(value) is not None


def _changed_paths(
    before: str,
    after: str,
    repository: Path,
) -> list[str] | None:
    try:
        result = subprocess.run(
            [
                "git",
                "diff",
                "--name-only",
                "--no-renames",
                "-z",
                before,
                after,
                "--",
            ],
            cwd=repository,
            check=False,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
    except OSError:
        return None
    if result.returncode != 0:
        return None
    try:
        return [
            item.decode("utf-8")
            for item in result.stdout.split(b"\0")
            if item
        ]
    except UnicodeDecodeError:
        return None


def _prior_public_gate_succeeded(
    repository: str,
    commit: str,
    api_url: str,
) -> bool | None:
    if _REPOSITORY.fullmatch(repository) is None or not _valid_commit(commit):
        return None
    parsed_api = urllib.parse.urlsplit(api_url)
    if (
        parsed_api.scheme != "https"
        or not parsed_api.hostname
        or parsed_api.username is not None
        or parsed_api.password is not None
        or parsed_api.query
        or parsed_api.fragment
    ):
        return None

    query = urllib.parse.urlencode(
        {
            "check_name": _PUBLIC_GATE_NAME,
            "status": "completed",
            "filter": "latest",
            "per_page": 100,
        }
    )
    url = (
        f"{api_url.rstrip('/')}/repos/{repository}/commits/{commit}/check-runs"
        f"?{query}"
    )
    request = urllib.request.Request(
        url,
        headers={
            "Accept": "application/vnd.github+json",
            "User-Agent": "MecchaCamouflage-v2-ci",
            "X-GitHub-Api-Version": "2026-03-10",
        },
    )
    try:
        with urllib.request.urlopen(request, timeout=10) as response:
            payload = response.read(_MAX_CHECK_RESPONSE_BYTES + 1)
    except (OSError, TimeoutError, urllib.error.URLError):
        return None
    if len(payload) > _MAX_CHECK_RESPONSE_BYTES:
        return None
    try:
        document = json.loads(payload)
    except (json.JSONDecodeError, UnicodeDecodeError):
        return None
    if not isinstance(document, dict):
        return None
    check_runs = document.get("check_runs")
    if not isinstance(check_runs, list):
        return None

    for check_run in check_runs:
        if not isinstance(check_run, dict):
            return None
        app = check_run.get("app")
        if (
            check_run.get("name") == _PUBLIC_GATE_NAME
            and check_run.get("head_sha") == commit
            and check_run.get("status") == "completed"
            and check_run.get("conclusion") == "success"
            and isinstance(app, dict)
            and app.get("slug") == "github-actions"
        ):
            return True
    return False


def classify_event(
    event_name: str,
    event_action: str,
    before: str,
    after: str,
    repository: Path,
    *,
    repository_name: str = "",
    api_url: str = "https://api.github.com",
    changed_paths_reader: Callable[[str, str, Path], list[str] | None] = _changed_paths,
    prior_gate_reader: Callable[[str], bool | None] | None = None,
) -> tuple[bool, str, int | None]:
    """Return (run_heavy, reason, changed_path_count)."""

    if event_name != "pull_request":
        return True, "non-pull-request", None
    if event_action != "synchronize":
        return True, "initial-or-reopened-pull-request", None
    if not _valid_commit(before) or not _valid_commit(after):
        return True, "unavailable-commit-range", None

    paths = changed_paths_reader(before, after, repository)
    if paths is None:
        return True, "unavailable-commit-range", None
    if requires_heavy_ci(paths):
        return True, "heavy-or-empty-change-set", len(paths)

    if prior_gate_reader is None:
        prior_gate_reader = lambda commit: _prior_public_gate_succeeded(
            repository_name,
            commit,
            api_url,
        )
    prior_gate = prior_gate_reader(before)
    if prior_gate is None:
        return True, "unavailable-prior-public-ci-evidence", len(paths)
    if not prior_gate:
        return True, "missing-prior-public-ci-evidence", len(paths)
    return False, "documentation-checkpoint-only", len(paths)


def _parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--event-name", required=True)
    parser.add_argument("--event-action", default="")
    parser.add_argument("--before", default="")
    parser.add_argument("--after", required=True)
    parser.add_argument("--repository", default="")
    parser.add_argument("--api-url", default="https://api.github.com")
    parser.add_argument("--github-output", required=True, type=Path)
    return parser.parse_args()


def main() -> int:
    args = _parse_args()
    run_heavy, reason, changed_path_count = classify_event(
        event_name=args.event_name,
        event_action=args.event_action,
        before=args.before,
        after=args.after,
        repository=Path.cwd(),
        repository_name=args.repository,
        api_url=args.api_url,
    )
    try:
        with args.github_output.open("a", encoding="utf-8", newline="\n") as output:
            output.write(f"run_heavy={'true' if run_heavy else 'false'}\n")
            output.write(f"classification={reason}\n")
    except OSError as error:
        print(f"FAIL ci_change_policy: cannot write GitHub output: {error}", file=sys.stderr)
        return 1

    count_text = "unknown" if changed_path_count is None else str(changed_path_count)
    print(
        "PASS ci_change_policy: "
        f"run_heavy={'true' if run_heavy else 'false'}, "
        f"classification={reason}, changed_paths={count_text}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
