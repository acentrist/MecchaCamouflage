#!/usr/bin/env python3
"""Verify the frozen v1 behavior inventory used by the v2 rewrite."""

from __future__ import annotations

import hashlib
import json
import re
import sys
from collections import Counter
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
TRACEABILITY = ROOT / "docs/v2/requirements-traceability.md"
TEST_LEDGER = ROOT / "docs/v2/test-migration.md"
TEST_REGISTRY = ROOT / "src/csharp/MecchaCamouflage.Tests/Program.cs"
FIXTURE_ROOT = ROOT / "src/tests/fixtures/v1"
LOCALES = ROOT / "src/csharp/MecchaCamouflage.Core/Localization/Strings.json"

EXPECTED_REQUIREMENTS = {
    "RUNTIME": 12,
    "PAINT": 19,
    "IMAGE": 20,
    "ESP": 10,
    "UI": 12,
    "I18N": 4,
    "PERSIST": 7,
    "LAUNCH": 12,
    "RELEASE": 10,
    "RETIRE": 12,
}
EXPECTED_LOCALES = {
    "en",
    "id",
    "de",
    "es",
    "fr",
    "it",
    "nl",
    "pl",
    "pt-BR",
    "vi",
    "tr",
    "ru",
    "ja",
    "ko",
    "zh-Hans",
    "zh-Hant",
}
EXPECTED_REGISTRY_FINGERPRINT = (
    "e92b43e7e943956251d77ac9c3745e5122b09cfea88e9169f4f27e44db0c6ac7"
)
DISPOSITIONS = {"PORT", "CHARACTERIZE", "REWRITE", "RETIRE"}


def fail(message: str) -> None:
    raise RuntimeError(message)


def load_json(path: Path) -> object:
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        fail(f"{path.relative_to(ROOT)} is not valid UTF-8 JSON: {error}")


def verify_requirements() -> int:
    text = TRACEABILITY.read_text(encoding="utf-8")
    rows = re.findall(
        r"^\| ((?:RUNTIME|PAINT|IMAGE|ESP|UI|I18N|PERSIST|LAUNCH|RELEASE|RETIRE)-\d{3}) \|(.+)\|$",
        text,
        re.MULTILINE,
    )
    identifiers = [identifier for identifier, _ in rows]
    duplicates = [key for key, count in Counter(identifiers).items() if count > 1]
    if duplicates:
        fail(f"duplicate requirement IDs: {', '.join(sorted(duplicates))}")

    for prefix, expected_count in EXPECTED_REQUIREMENTS.items():
        actual = sorted(
            int(identifier.rsplit("-", 1)[1])
            for identifier in identifiers
            if identifier.startswith(f"{prefix}-")
        )
        expected = list(range(1, expected_count + 1))
        if actual != expected:
            fail(f"{prefix} requirement sequence is {actual}, expected {expected}")

    for identifier, body in rows:
        cells = [cell.strip() for cell in body.split("|")]
        if identifier.startswith("RETIRE-"):
            if len(cells) != 2 or not all(cells):
                fail(f"{identifier} must name the retirement and its replacement")
            continue
        if len(cells) != 5 or not all(cells):
            fail(f"{identifier} must provide contract, authority, owner, evidence, deletion")
        if not re.search(r"\bT[0-6]\b", cells[3]):
            fail(f"{identifier} has no planned evidence tier")

    return len(identifiers)


def read_v1_test_names() -> list[str]:
    text = TEST_REGISTRY.read_text(encoding="utf-8")
    start = text.index("var tests =")
    end = text.index("};", start)
    return re.findall(
        r'^\s*\("([^"]+)",\s*[A-Za-z0-9_]+\),?$',
        text[start:end],
        re.MULTILINE,
    )


def verify_test_ledger() -> Counter[str]:
    names = read_v1_test_names()
    fingerprint = hashlib.sha256(
        ("\n".join(names) + "\n").encode("utf-8")
    ).hexdigest()
    if len(names) != 175:
        fail(f"v1 registry contains {len(names)} tests, expected 175")
    if fingerprint != EXPECTED_REGISTRY_FINGERPRINT:
        fail(
            "v1 registry fingerprint changed: "
            f"{fingerprint}, expected {EXPECTED_REGISTRY_FINGERPRINT}"
        )

    text = TEST_LEDGER.read_text(encoding="utf-8")
    if EXPECTED_REGISTRY_FINGERPRINT not in text:
        fail("test migration ledger does not record the registry fingerprint")
    rows = re.findall(
        r"^\| (\d{3})-(\d{3}) \| ([A-Z]+) \| (.+?) \| (.+?) \|$",
        text,
        re.MULTILINE,
    )
    coverage: dict[int, str] = {}
    counts: Counter[str] = Counter()
    for raw_start, raw_end, disposition, source_anchor, evidence in rows:
        start = int(raw_start)
        end = int(raw_end)
        if disposition not in DISPOSITIONS:
            fail(f"unknown test disposition {disposition}")
        if start < 1 or end < start or end > len(names):
            fail(f"invalid test range {start:03d}-{end:03d}")
        if not source_anchor.strip() or not evidence.strip():
            fail(f"test range {start:03d}-{end:03d} lacks an audit anchor")
        for index in range(start, end + 1):
            if index in coverage:
                fail(f"test {index:03d} is classified more than once")
            coverage[index] = disposition
            counts[disposition] += 1

    missing = sorted(set(range(1, len(names) + 1)) - set(coverage))
    if missing:
        fail("unclassified test numbers: " + ", ".join(f"{item:03d}" for item in missing))
    return counts


def verify_localization() -> tuple[int, int]:
    catalogs = load_json(LOCALES)
    if not isinstance(catalogs, dict):
        fail("localization root must be an object")
    if set(catalogs) != EXPECTED_LOCALES:
        fail(f"locale codes are {sorted(catalogs)}, expected {sorted(EXPECTED_LOCALES)}")
    english = catalogs.get("en")
    if not isinstance(english, dict):
        fail("English localization catalog is not an object")
    english_keys = set(english)
    for locale, catalog in catalogs.items():
        if not isinstance(catalog, dict):
            fail(f"{locale} localization catalog is not an object")
        if set(catalog) != english_keys:
            missing = sorted(english_keys - set(catalog))
            extra = sorted(set(catalog) - english_keys)
            fail(f"{locale} localization key mismatch; missing={missing}, extra={extra}")
        if any(not isinstance(value, str) for value in catalog.values()):
            fail(f"{locale} contains a non-string translation")
    return len(catalogs), len(english_keys)


def verify_fixtures(locale_count: int, locale_keys: int) -> int:
    manifest = load_json(FIXTURE_ROOT / "manifest.json")
    if not isinstance(manifest, dict) or manifest.get("schema_version") != 1:
        fail("fixture manifest schema_version must be 1")
    fixture_names = manifest.get("fixtures")
    if not isinstance(fixture_names, list) or not all(
        isinstance(name, str) for name in fixture_names
    ):
        fail("fixture manifest fixtures must be a string array")
    expected_files = sorted(path.name for path in FIXTURE_ROOT.glob("*.json") if path.name != "manifest.json")
    if sorted(fixture_names) != expected_files:
        fail(f"fixture manifest lists {sorted(fixture_names)}, actual files are {expected_files}")

    required_fixture_keys = {
        "paint-domain.json": {"settings", "replay_plan_cases", "adaptive_compression_cases", "preview_disposition_cases"},
        "image-mapping.json": {"canvas", "normalized_atlas_cases", "canonical_projection_cases"},
        "runtime-pacing.json": {"pacing_cases", "terminal_drain_cases"},
        "esp-domain.json": {"scope_cases", "avatar_source_cases", "geometry_cases", "screen_bounds_cases", "projection_scale_cases"},
    }
    for name in fixture_names:
        fixture = load_json(FIXTURE_ROOT / name)
        if not isinstance(fixture, dict) or fixture.get("schema_version") != 1:
            fail(f"{name} schema_version must be 1")
        missing = required_fixture_keys[name] - set(fixture)
        if missing:
            fail(f"{name} lacks fixture groups: {sorted(missing)}")

    profile_hashes = manifest.get("profile_sha256")
    if not isinstance(profile_hashes, dict) or len(profile_hashes) != 6:
        fail("fixture manifest must record all six profile hashes")
    for relative, expected_hash in profile_hashes.items():
        path = ROOT / relative
        actual_hash = hashlib.sha256(path.read_bytes()).hexdigest()
        if actual_hash != expected_hash:
            fail(f"profile hash changed for {relative}: {actual_hash}")

    localization = manifest.get("localization")
    if localization != {
        "catalog_count": locale_count,
        "required_keys_per_catalog": locale_keys,
    }:
        fail("fixture localization inventory does not match the source catalogs")
    return len(fixture_names)


def main() -> int:
    try:
        requirement_count = verify_requirements()
        dispositions = verify_test_ledger()
        locale_count, locale_keys = verify_localization()
        fixture_count = verify_fixtures(locale_count, locale_keys)
    except (RuntimeError, OSError, ValueError) as error:
        print(f"FAIL phase1: {error}", file=sys.stderr)
        return 1

    disposition_text = ", ".join(
        f"{name.lower()}={dispositions[name]}" for name in sorted(DISPOSITIONS)
    )
    print(
        "PASS phase1: "
        f"requirements={requirement_count}, tests=175 ({disposition_text}), "
        f"locales={locale_count}x{locale_keys}, fixtures={fixture_count}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
