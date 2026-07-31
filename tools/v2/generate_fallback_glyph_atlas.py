#!/usr/bin/env python3
"""Generate the reviewed v2 fallback glyph atlas from the pinned Noto font."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
import shutil
import subprocess
import tempfile
from pathlib import Path


SCHEMA_VERSION = 1
SOURCE_NAME = "Noto Sans CJK"
SOURCE_TAG = "Sans2.004"
SOURCE_COMMIT = "523d033d6cb47f4a80c58a35753646f5c3608a78"
SOURCE_FILE = "NotoSansCJK-Regular.ttc"
SOURCE_SHA256 = (
    "b76b0433203017ca80401b2ee0dd69350349871c4b19d504c34dbdd80541690a"
)
SOURCE_URL = (
    "https://github.com/notofonts/noto-cjk/blob/"
    f"{SOURCE_COMMIT}/Sans/OTC/{SOURCE_FILE}"
)
LICENSE_SHA256 = (
    "6a73f9541c2de74158c0e7cf6b0a58ef774f5a780bf191f2d7ec9cc53efe2bf2"
)
ATLAS_FILE = "fallback-glyph-atlas.png"
MANIFEST_FILE = "fallback-glyph-atlas.json"
CELL_SIZE = 48
COLUMNS = 32
POINT_SIZE = 32
PADDING_X = 6
PADDING_Y = 4
REPLACEMENT_CHARACTER = "\N{REPLACEMENT CHARACTER}"
NON_RENDERING_CONTROLS = {"\n", "\r", "\t"}
LOCALE_NATIVE_NAMES = (
    "English",
    "Bahasa Indonesia",
    "Deutsch",
    "Español",
    "Français",
    "Italiano",
    "Nederlands",
    "Polski",
    "Português (Brasil)",
    "Tiếng Việt",
    "Türkçe",
    "Русский",
    "日本語",
    "한국어",
    "简体中文",
    "繁體中文",
)


class GlyphAtlasGenerationError(ValueError):
    pass


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        while chunk := stream.read(1024 * 1024):
            digest.update(chunk)
    return digest.hexdigest()


def _codepoints(catalog_path: Path) -> list[int]:
    try:
        catalog = json.loads(catalog_path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise GlyphAtlasGenerationError(
            "The localization catalog could not be read."
        ) from error
    if not isinstance(catalog, dict):
        raise GlyphAtlasGenerationError(
            "The localization catalog root must be an object."
        )
    characters = set("".join(LOCALE_NATIVE_NAMES))
    for translations in catalog.values():
        if not isinstance(translations, dict):
            raise GlyphAtlasGenerationError(
                "Every localization locale must be an object."
            )
        for value in translations.values():
            if not isinstance(value, str):
                raise GlyphAtlasGenerationError(
                    "Every localized value must be a string."
                )
            characters.update(value)
    characters.add(REPLACEMENT_CHARACTER)
    characters.difference_update(NON_RENDERING_CONTROLS)
    return sorted(ord(character) for character in characters)


def _imagemagick_binary() -> str:
    binary = shutil.which("magick")
    if binary:
        return binary
    binary = shutil.which("convert")
    if binary:
        return binary
    raise GlyphAtlasGenerationError(
        "ImageMagick 'magick' or 'convert' was not found."
    )


def _imagemagick_version(binary: str) -> str:
    completed = subprocess.run(
        [binary, "-version"],
        check=True,
        capture_output=True,
        text=True,
    )
    first_line = completed.stdout.splitlines()
    if not first_line:
        raise GlyphAtlasGenerationError(
            "ImageMagick did not report its version."
        )
    return first_line[0]


def _annotation_text(character: str) -> str:
    # ImageMagick expands percent escapes in annotation strings.
    return character.replace("%", "%%")


def generate(
    *,
    catalog_path: Path,
    source_font: Path,
    output_directory: Path,
) -> None:
    if not source_font.is_file() or _sha256(source_font) != SOURCE_SHA256:
        raise GlyphAtlasGenerationError(
            "The supplied Noto source font does not match the frozen hash."
        )
    codepoints = _codepoints(catalog_path)
    rows = math.ceil(len(codepoints) / COLUMNS)
    width = COLUMNS * CELL_SIZE
    height = rows * CELL_SIZE
    binary = _imagemagick_binary()
    generator_version = _imagemagick_version(binary)

    output_directory.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(
        prefix=".fallback-glyph-atlas-",
        dir=output_directory,
    ) as temporary:
        temporary_root = Path(temporary)
        atlas_path = temporary_root / ATLAS_FILE
        command = [
            binary,
            "-size",
            f"{width}x{height}",
            "xc:none",
            "-font",
            str(source_font),
            "-pointsize",
            str(POINT_SIZE),
            "-fill",
            "white",
            "-gravity",
            "NorthWest",
        ]
        glyphs: list[dict[str, int | str]] = []
        for index, codepoint in enumerate(codepoints):
            column = index % COLUMNS
            row = index // COLUMNS
            command.extend(
                (
                    "-annotate",
                    f"+{column * CELL_SIZE + PADDING_X}"
                    f"+{row * CELL_SIZE + PADDING_Y}",
                    _annotation_text(chr(codepoint)),
                )
            )
            glyphs.append(
                {
                    "codepoint": f"U+{codepoint:04X}",
                    "index": index,
                    "advance": CELL_SIZE,
                }
            )
        command.append(f"PNG32:{atlas_path}")
        try:
            subprocess.run(command, check=True)
        except (OSError, subprocess.CalledProcessError) as error:
            raise GlyphAtlasGenerationError(
                "ImageMagick could not render the fallback atlas."
            ) from error

        manifest = {
            "schema_version": SCHEMA_VERSION,
            "source": {
                "name": SOURCE_NAME,
                "tag": SOURCE_TAG,
                "commit": SOURCE_COMMIT,
                "file": SOURCE_FILE,
                "sha256": SOURCE_SHA256,
                "url": SOURCE_URL,
                "license": "SIL Open Font License 1.1",
                "license_sha256": LICENSE_SHA256,
            },
            "atlas": {
                "file": ATLAS_FILE,
                "sha256": _sha256(atlas_path),
                "width": width,
                "height": height,
                "cell_width": CELL_SIZE,
                "cell_height": CELL_SIZE,
                "columns": COLUMNS,
                "point_size": POINT_SIZE,
                "padding_x": PADDING_X,
                "padding_y": PADDING_Y,
                "pixel_format": "RGBA8",
            },
            "generator": generator_version,
            "glyphs": glyphs,
        }
        manifest_path = temporary_root / MANIFEST_FILE
        manifest_path.write_text(
            json.dumps(
                manifest,
                ensure_ascii=False,
                indent=2,
                sort_keys=True,
            )
            + "\n",
            encoding="utf-8",
            newline="\n",
        )
        os.replace(atlas_path, output_directory / ATLAS_FILE)
        os.replace(manifest_path, output_directory / MANIFEST_FILE)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--catalog", required=True, type=Path)
    parser.add_argument("--source-font", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    arguments = parser.parse_args()
    try:
        generate(
            catalog_path=arguments.catalog,
            source_font=arguments.source_font,
            output_directory=arguments.output,
        )
    except GlyphAtlasGenerationError as error:
        print(f"fallback glyph atlas generation failed: {error}")
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
