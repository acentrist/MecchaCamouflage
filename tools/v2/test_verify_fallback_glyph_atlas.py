#!/usr/bin/env python3

from __future__ import annotations

import json
import shutil
import sys
import tempfile
import unittest
from pathlib import Path


sys.path.insert(0, str(Path(__file__).resolve().parent))

from verify_fallback_glyph_atlas import (  # noqa: E402
    GlyphAtlasVerificationError,
    verify,
)


class FallbackGlyphAtlasTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.project_root = Path(__file__).resolve().parents[2]
        cls.catalog = (
            cls.project_root / "resources/localization/catalog.json"
        )
        cls.atlas = cls.project_root / "resources/fonts/fallback"

    def test_shipped_atlas_exactly_covers_catalog(self) -> None:
        verify(
            catalog_path=self.catalog,
            atlas_directory=self.atlas,
        )

    def test_manifest_hash_and_inventory_tampering_fail_closed(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            copied = Path(temporary) / "fallback"
            shutil.copytree(self.atlas, copied)
            manifest_path = copied / "fallback-glyph-atlas.json"
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
            manifest["glyphs"].pop()
            manifest_path.write_text(
                json.dumps(manifest),
                encoding="utf-8",
            )
            with self.assertRaises(GlyphAtlasVerificationError):
                verify(
                    catalog_path=self.catalog,
                    atlas_directory=copied,
                )

    def test_png_tampering_fails_closed(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            copied = Path(temporary) / "fallback"
            shutil.copytree(self.atlas, copied)
            atlas_path = copied / "fallback-glyph-atlas.png"
            atlas_path.write_bytes(atlas_path.read_bytes() + b"tamper")
            with self.assertRaises(GlyphAtlasVerificationError):
                verify(
                    catalog_path=self.catalog,
                    atlas_directory=copied,
                )


if __name__ == "__main__":
    unittest.main()
