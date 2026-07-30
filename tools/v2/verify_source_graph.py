#!/usr/bin/env python3
"""Verify the public v2 source graph without initializing restricted sources."""

from __future__ import annotations

import configparser
import re
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
UE4SS_PATH = Path("third_party/RE-UE4SS")
UE4SS_COMMIT = "6c26f038751b3d96059d4a9148f5d093012d55ad"
UEPSEUDO_COMMIT = "b2e876da82b17254c04304746341c8fde0ddb37c"
PATTERNSLEUTH_COMMIT = "da8bfe4c5a464be0ef225c2c9a6ccaa2d9284018"
DIRECT_FETCH_COMMITS = {
    "glaze": "3a850807501d98d23bab4bdc5af64d8d4e83e6bc",
    "glfw": "e2c92645460f680fd272fd2eed591efb2be7dc31",
    "imgui": "5d4126876bc10396d4c6511853ff10964414c776",
    "imgui-text-edit": "6d943aba9f7cef05da80b86dbb0253b63818f95c",
    "icon-font-cpp-headers": "210b5a399a64270674560d633638952d1e8d804d",
    "zydis": "a2278f1d254e492f6a6b39f6cb5d1f5d515659dc",
    "polyhook2": "298d56210b9d9e66cde8f96481d6053925c6ae15",
    "raw-pdb": "8c6a7146393c83d27fa101e8bc8017f2a7f151df",
    "corrosion": "52844733e14f095c947577627e367ee5f6458af7",
    "fmt": "40626af88bd7df9a5fb80be7b25ac85b122d6c21",
    "tracy": "37aff70dfa50cf6307b3fee6074d627dc2929143",
}


def fail(message: str) -> None:
    raise RuntimeError(message)


def git(*arguments: str, cwd: Path = ROOT) -> str:
    result = subprocess.run(
        [
            "git",
            "-c",
            f"safe.directory={cwd}",
            "-c",
            "core.filemode=false",
            *arguments,
        ],
        cwd=cwd,
        check=False,
        capture_output=True,
        text=True,
    )
    if result.returncode != 0:
        fail(f"git {' '.join(arguments)} failed: {result.stderr.strip()}")
    return result.stdout.strip()


def verify_root_gitlink() -> None:
    modules = configparser.ConfigParser()
    modules.read(ROOT / ".gitmodules", encoding="utf-8")
    section = 'submodule "third_party/RE-UE4SS"'
    if not modules.has_section(section):
        fail("third_party/RE-UE4SS is not declared in .gitmodules")
    if modules.get(section, "path", fallback="") != UE4SS_PATH.as_posix():
        fail("RE-UE4SS submodule path is not canonical")
    if modules.get(section, "url", fallback="") != "https://github.com/UE4SS-RE/RE-UE4SS.git":
        fail("RE-UE4SS must use its public HTTPS URL")

    fields = git("ls-files", "--stage", UE4SS_PATH.as_posix()).split()
    if len(fields) < 2 or fields[0] != "160000" or fields[1] != UE4SS_COMMIT:
        fail(f"RE-UE4SS gitlink is not pinned to {UE4SS_COMMIT}")

    checkout = ROOT / UE4SS_PATH
    if checkout.exists():
        if git("rev-parse", "HEAD", cwd=checkout) != UE4SS_COMMIT:
            fail("checked-out RE-UE4SS commit differs from the gitlink")
        if git("status", "--porcelain", "--untracked-files=no", cwd=checkout):
            fail("RE-UE4SS contains a source patch or tracked modification")


def verify_nested_gitlinks() -> None:
    checkout = ROOT / UE4SS_PATH
    if not checkout.exists():
        fail("public RE-UE4SS checkout is required for the source-graph test")
    entries = git(
        "ls-tree",
        "HEAD",
        "deps/first/Unreal",
        "deps/first/patternsleuth",
        cwd=checkout,
    ).splitlines()
    parsed: dict[str, str] = {}
    for entry in entries:
        match = re.fullmatch(r"160000 commit ([0-9a-f]{40})\t(.+)", entry)
        if match:
            parsed[match.group(2)] = match.group(1)
    expected = {
        "deps/first/Unreal": UEPSEUDO_COMMIT,
        "deps/first/patternsleuth": PATTERNSLEUTH_COMMIT,
    }
    if parsed != expected:
        fail(f"unexpected UE4SS nested gitlinks: {parsed}")


def verify_build_pin() -> None:
    cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
    if UE4SS_COMMIT not in cmake:
        fail("CMake build identity does not contain the accepted UE4SS commit")
    if "MECCHA_WITH_UE4SS" not in cmake:
        fail("secret-free/full-build boundary option is missing")
    if "MECCHA_WITH_UE4SS requires Windows x64 with MSVC" not in cmake:
        fail("full-build compiler/platform guard is missing")
    pins = (
        (ROOT / "cmake/ProjectDependencies.cmake").read_text(encoding="utf-8")
        + (ROOT / "cmake/Ue4ssDependencyPins.cmake").read_text(encoding="utf-8")
    )
    missing = sorted(
        f"{name}={commit}"
        for name, commit in DIRECT_FETCH_COMMITS.items()
        if commit not in pins
    )
    if missing:
        fail(f"direct FetchContent graph is not immutable: {missing}")


def verify_dependency_record() -> None:
    path = ROOT / "docs/v2/dependency-lock.md"
    if not path.exists():
        fail("docs/v2/dependency-lock.md is missing")
    text = path.read_text(encoding="utf-8")
    required_fragments = {
        UE4SS_COMMIT,
        UEPSEUDO_COMMIT,
        PATTERNSLEUTH_COMMIT,
        *DIRECT_FETCH_COMMITS.values(),
        "MIT License",
        "MIT OR Apache-2.0",
        "restricted",
        "No UE4SS source patch",
    }
    missing = sorted(fragment for fragment in required_fragments if fragment not in text)
    if missing:
        fail(f"dependency lock lacks: {missing}")


def main() -> int:
    try:
        verify_root_gitlink()
        verify_nested_gitlinks()
        verify_build_pin()
        verify_dependency_record()
    except (RuntimeError, OSError, configparser.Error) as error:
        print(f"FAIL source_graph: {error}", file=sys.stderr)
        return 1
    print(
        "PASS source_graph: "
        f"ue4ss={UE4SS_COMMIT}, uepseudo={UEPSEUDO_COMMIT}, "
        f"patternsleuth={PATTERNSLEUTH_COMMIT}, patches=0"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
