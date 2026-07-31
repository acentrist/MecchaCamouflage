#!/usr/bin/env python3
"""Verify the public v2 source graph without initializing restricted sources."""

from __future__ import annotations

import configparser
import hashlib
import json
import re
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
UE4SS_PATH = Path("third_party/RE-UE4SS")
UE4SS_COMMIT = "6c26f038751b3d96059d4a9148f5d093012d55ad"
UEPSEUDO_COMMIT = "b2e876da82b17254c04304746341c8fde0ddb37c"
PATTERNSLEUTH_COMMIT = "da8bfe4c5a464be0ef225c2c9a6ccaa2d9284018"
UE4SS_OVERLAY_POLICY = Path("cmake/ue4ss-source-overlay.json")
UE4SS_OVERLAY_TARGET = "deps/first/patternsleuth_bind/Cargo.lock"
UE4SS_UPSTREAM_LOCK_SHA256 = (
    "19292c3e0a74c851eb11ad09a3b3ac5e5d8e9b80eebe34dd705df10e09dc7e50"
)
UE4SS_CANONICAL_LOCK_SHA256 = (
    "88c3718c03492cdc2650217a9d8bb2a8dbdecdbde1b4ea79e3e529e838b49bbe"
)
UE4SS_STAGED_DIFF_SHA256 = (
    "0dac25e7c79d430aca62411cddf66c17d95340e2bee174f453f17f610a839f8f"
)
DIRECT_FETCH_COMMITS = {
    "glaze": "3a850807501d98d23bab4bdc5af64d8d4e83e6bc",
    "libwebp": "4fa21912338357f89e4fd51cf2368325b59e9bd9",
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
    if (
        modules.get(section, "url", fallback="")
        != "https://github.com/UE4SS-RE/RE-UE4SS.git"
    ):
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
    if (
        "corrosion_set_cargo_flags(patternsleuth_bind --locked)"
        not in cmake
    ):
        fail("the protected Rust build does not enforce its accepted Cargo.lock")
    required_stage_fragments = {
        "MECCHA_UE4SS_SOURCE_ROOT",
        "MECCHA_UE4SS_SOURCE_MANIFEST",
        UE4SS_CANONICAL_LOCK_SHA256,
        '"${_meccha_ue4ss_root}"',
        '"${CMAKE_CURRENT_BINARY_DIR}/_ue4ss"',
        "if(NOT TARGET proxy)",
        "add_dependencies(meccha_mod proxy)",
    }
    missing_stage_fragments = sorted(
        fragment
        for fragment in required_stage_fragments
        if fragment not in cmake
    )
    if missing_stage_fragments:
        fail(
            "full build does not require the approved immutable UE4SS "
            f"source stage: {missing_stage_fragments}"
        )
    pins = (
        (ROOT / "cmake/ProjectDependencies.cmake").read_text(encoding="utf-8")
        + (ROOT / "cmake/Ue4ssDependencyPins.cmake").read_text(encoding="utf-8")
    )
    if (
        "${_meccha_ue4ss_root}/deps/third/fmt" not in pins
        or (
            "${CMAKE_CURRENT_SOURCE_DIR}/third_party/RE-UE4SS/"
            "deps/third/fmt"
        )
        in pins
    ):
        fail("the fmt patch input is not bound to the verified UE4SS stage")
    missing = sorted(
        f"{name}={commit}"
        for name, commit in DIRECT_FETCH_COMMITS.items()
        if commit not in pins
    )
    if missing:
        fail(f"direct FetchContent graph is not immutable: {missing}")


def verify_source_overlay() -> None:
    attributes = (ROOT / ".gitattributes").read_text(encoding="utf-8")
    if "*.lock text eol=lf" not in attributes.splitlines():
        fail("canonical lock files are not fixed to LF in Windows checkouts")
    policy_path = ROOT / UE4SS_OVERLAY_POLICY
    policy_bytes = policy_path.read_bytes()
    policy = json.loads(policy_bytes.decode("utf-8"))
    expected = {
        "schema_version": 1,
        "ue4ss_commit": UE4SS_COMMIT,
        "overlay": {
            "source": (
                "ue4ss-overlays/"
                f"{UE4SS_COMMIT}/patternsleuth_bind.Cargo.lock"
            ),
            "target": UE4SS_OVERLAY_TARGET,
            "upstream_sha256": UE4SS_UPSTREAM_LOCK_SHA256,
            "overlay_sha256": UE4SS_CANONICAL_LOCK_SHA256,
            "staged_diff_sha256": UE4SS_STAGED_DIFF_SHA256,
        },
        "nested_gitlinks": [
            {
                "path": "deps/first/Unreal",
                "commit": UEPSEUDO_COMMIT,
            },
            {
                "path": "deps/first/patternsleuth",
                "commit": PATTERNSLEUTH_COMMIT,
            },
        ],
    }
    if policy != expected:
        fail("the project-owned UE4SS source-overlay policy changed")
    overlay = (
        policy_path.parent
        / policy["overlay"]["source"]
    ).read_bytes()
    if hashlib.sha256(overlay).hexdigest() != UE4SS_CANONICAL_LOCK_SHA256:
        fail("the project-owned canonical Cargo lock changed")
    upstream = (
        ROOT / UE4SS_PATH / UE4SS_OVERLAY_TARGET
    ).read_bytes()
    if hashlib.sha256(upstream).hexdigest() != UE4SS_UPSTREAM_LOCK_SHA256:
        fail("the accepted upstream Cargo lock changed")


def verify_proxy_override_contract() -> None:
    source_path = (
        ROOT
        / UE4SS_PATH
        / "UE4SS/proxy_generator/main.cpp"
    )
    source = source_path.read_text(encoding="utf-8")
    required_fragments = {
        'currentPath / \\"override.txt\\"',
        "std::ifstream overrideFile(overrideFilePath)",
        "std::string overridePath",
        "std::getline(overrideFile, overridePath)",
        "fs::path ue4ssOverridePath = overridePath",
        'ue4ssOverridePath = ue4ssOverridePath / \\"UE4SS.dll\\"',
        "LoadLibrary(ue4ssOverridePath.c_str())",
    }
    missing = sorted(
        fragment
        for fragment in required_fragments
        if fragment not in source
    )
    if missing:
        fail(
            "pinned proxy override contract changed; architecture "
            f"review is required: {missing}"
        )


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
        "Approved build-only source overlay",
    }
    missing = sorted(
        fragment for fragment in required_fragments if fragment not in text
    )
    if missing:
        fail(f"dependency lock lacks: {missing}")
    webp_license = ROOT / "resources/licenses/libwebp-COPYING.txt"
    if not webp_license.exists():
        fail("packaged libwebp license is missing")
    license_text = webp_license.read_text(encoding="utf-8")
    if (
        "Copyright (c) 2010, Google Inc." not in license_text
        or "Redistribution and use in source and binary forms" not in license_text
    ):
        fail("packaged libwebp license is incomplete")


def main() -> int:
    try:
        verify_root_gitlink()
        verify_nested_gitlinks()
        verify_build_pin()
        verify_source_overlay()
        verify_proxy_override_contract()
        verify_dependency_record()
    except (RuntimeError, OSError, configparser.Error) as error:
        print(f"FAIL source_graph: {error}", file=sys.stderr)
        return 1
    print(
        "PASS source_graph: "
        f"ue4ss={UE4SS_COMMIT}, uepseudo={UEPSEUDO_COMMIT}, "
        f"patternsleuth={PATTERNSLEUTH_COMMIT}, "
        "proxy_override=narrow-line-to-native-path, "
        "approved_overlay=patternsleuth_bind/Cargo.lock"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
