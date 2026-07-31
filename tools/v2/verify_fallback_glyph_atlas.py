#!/usr/bin/env python3
"""Verify the shipped fallback atlas and exact localization glyph coverage."""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
from pathlib import Path

from generate_fallback_glyph_atlas import (
    ATLAS_FILE,
    CELL_SIZE,
    COLUMNS,
    LICENSE_SHA256,
    LOCALE_NATIVE_NAMES,
    MANIFEST_FILE,
    NON_RENDERING_CONTROLS,
    POINT_SIZE,
    REPLACEMENT_CHARACTER,
    SCHEMA_VERSION,
    SOURCE_COMMIT,
    SOURCE_FILE,
    SOURCE_NAME,
    SOURCE_SHA256,
    SOURCE_TAG,
    SOURCE_URL,
)


PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"


class GlyphAtlasVerificationError(ValueError):
    pass


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    try:
        with path.open("rb") as stream:
            while chunk := stream.read(1024 * 1024):
                digest.update(chunk)
    except OSError as error:
        raise GlyphAtlasVerificationError(
            f"Could not read {path}."
        ) from error
    return digest.hexdigest()


def _load_json(path: Path) -> object:
    def reject_duplicates(pairs: list[tuple[str, object]]) -> dict[str, object]:
        result: dict[str, object] = {}
        for key, value in pairs:
            if key in result:
                raise GlyphAtlasVerificationError(
                    f"Duplicate JSON key: {key}"
                )
            result[key] = value
        return result

    try:
        return json.loads(
            path.read_text(encoding="utf-8"),
            object_pairs_hook=reject_duplicates,
        )
    except GlyphAtlasVerificationError:
        raise
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise GlyphAtlasVerificationError(
            f"Could not parse {path}."
        ) from error


def _required_codepoints(catalog_path: Path) -> list[int]:
    catalog = _load_json(catalog_path)
    if not isinstance(catalog, dict):
        raise GlyphAtlasVerificationError(
            "Localization catalog root is not an object."
        )
    characters = set("".join(LOCALE_NATIVE_NAMES))
    for locale, translations in catalog.items():
        if not isinstance(locale, str) or not isinstance(translations, dict):
            raise GlyphAtlasVerificationError(
                "Localization catalog shape is invalid."
            )
        for key, value in translations.items():
            if not isinstance(key, str) or not isinstance(value, str):
                raise GlyphAtlasVerificationError(
                    "Localization entries must be strings."
                )
            characters.update(value)
    characters.add(REPLACEMENT_CHARACTER)
    characters.difference_update(NON_RENDERING_CONTROLS)
    return sorted(ord(character) for character in characters)


def _png_dimensions(path: Path) -> tuple[int, int, int, int]:
    try:
        with path.open("rb") as stream:
            header = stream.read(33)
    except OSError as error:
        raise GlyphAtlasVerificationError(
            "Could not read the fallback atlas PNG."
        ) from error
    if (
        len(header) != 33
        or header[:8] != PNG_SIGNATURE
        or header[12:16] != b"IHDR"
    ):
        raise GlyphAtlasVerificationError(
            "Fallback atlas is not a canonical PNG."
        )
    width, height, bit_depth, color_type = struct.unpack(
        ">IIBB", header[16:26]
    )
    return width, height, bit_depth, color_type


def _expect_exact_keys(
    value: object,
    expected: set[str],
    description: str,
) -> dict[str, object]:
    if not isinstance(value, dict) or set(value) != expected:
        raise GlyphAtlasVerificationError(
            f"{description} keys are invalid."
        )
    return value


def verify(
    *,
    catalog_path: Path,
    atlas_directory: Path,
) -> None:
    manifest = _expect_exact_keys(
        _load_json(atlas_directory / MANIFEST_FILE),
        {"schema_version", "source", "atlas", "generator", "glyphs"},
        "Manifest",
    )
    if manifest["schema_version"] != SCHEMA_VERSION:
        raise GlyphAtlasVerificationError(
            "Fallback atlas schema version is invalid."
        )
    source = _expect_exact_keys(
        manifest["source"],
        {
            "name",
            "tag",
            "commit",
            "file",
            "sha256",
            "url",
            "license",
            "license_sha256",
        },
        "Source",
    )
    expected_source = {
        "name": SOURCE_NAME,
        "tag": SOURCE_TAG,
        "commit": SOURCE_COMMIT,
        "file": SOURCE_FILE,
        "sha256": SOURCE_SHA256,
        "url": SOURCE_URL,
        "license": "SIL Open Font License 1.1",
        "license_sha256": LICENSE_SHA256,
    }
    if source != expected_source:
        raise GlyphAtlasVerificationError(
            "Fallback atlas source provenance drifted."
        )
    if (
        not isinstance(manifest["generator"], str)
        or not manifest["generator"].startswith("Version: ImageMagick ")
    ):
        raise GlyphAtlasVerificationError(
            "Fallback atlas generator provenance is invalid."
        )

    atlas = _expect_exact_keys(
        manifest["atlas"],
        {
            "file",
            "sha256",
            "width",
            "height",
            "cell_width",
            "cell_height",
            "columns",
            "point_size",
            "padding_x",
            "padding_y",
            "pixel_format",
        },
        "Atlas",
    )
    glyphs = manifest["glyphs"]
    if not isinstance(glyphs, list) or not glyphs:
        raise GlyphAtlasVerificationError(
            "Fallback atlas glyph inventory is empty."
        )
    required = _required_codepoints(catalog_path)
    observed: list[int] = []
    for expected_index, raw_glyph in enumerate(glyphs):
        glyph = _expect_exact_keys(
            raw_glyph,
            {"codepoint", "index", "advance"},
            "Glyph",
        )
        codepoint_text = glyph["codepoint"]
        if (
            not isinstance(codepoint_text, str)
            or not codepoint_text.startswith("U+")
        ):
            raise GlyphAtlasVerificationError(
                "Glyph codepoint syntax is invalid."
            )
        try:
            codepoint = int(codepoint_text[2:], 16)
        except ValueError as error:
            raise GlyphAtlasVerificationError(
                "Glyph codepoint syntax is invalid."
            ) from error
        if (
            glyph["index"] != expected_index
            or glyph["advance"] != CELL_SIZE
            or codepoint > 0x10FFFF
        ):
            raise GlyphAtlasVerificationError(
                "Glyph index or advance is invalid."
            )
        observed.append(codepoint)
    if observed != required:
        raise GlyphAtlasVerificationError(
            "Fallback atlas does not exactly cover shipped localization."
        )

    rows = (len(required) + COLUMNS - 1) // COLUMNS
    expected_width = COLUMNS * CELL_SIZE
    expected_height = rows * CELL_SIZE
    expected_atlas = {
        "file": ATLAS_FILE,
        "sha256": atlas["sha256"],
        "width": expected_width,
        "height": expected_height,
        "cell_width": CELL_SIZE,
        "cell_height": CELL_SIZE,
        "columns": COLUMNS,
        "point_size": POINT_SIZE,
        "padding_x": 6,
        "padding_y": 4,
        "pixel_format": "RGBA8",
    }
    if atlas != expected_atlas:
        raise GlyphAtlasVerificationError(
            "Fallback atlas geometry is invalid."
        )
    atlas_path = atlas_directory / ATLAS_FILE
    if (
        not isinstance(atlas["sha256"], str)
        or len(atlas["sha256"]) != 64
        or _sha256(atlas_path) != atlas["sha256"]
    ):
        raise GlyphAtlasVerificationError(
            "Fallback atlas hash is invalid."
        )
    width, height, bit_depth, color_type = _png_dimensions(atlas_path)
    if (
        (width, height) != (expected_width, expected_height)
        or bit_depth != 8
        or color_type != 6
    ):
        raise GlyphAtlasVerificationError(
            "Fallback atlas PNG format is invalid."
        )
    if _sha256(atlas_directory / "Noto-CJK-OFL.txt") != LICENSE_SHA256:
        raise GlyphAtlasVerificationError(
            "Fallback atlas OFL license is missing or changed."
        )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--catalog", required=True, type=Path)
    parser.add_argument("--atlas-directory", required=True, type=Path)
    arguments = parser.parse_args()
    try:
        verify(
            catalog_path=arguments.catalog,
            atlas_directory=arguments.atlas_directory,
        )
    except GlyphAtlasVerificationError as error:
        print(f"fallback glyph atlas verification failed: {error}")
        return 1
    print("PASS fallback glyph atlas")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
