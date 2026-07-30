#!/usr/bin/env python3

from __future__ import annotations

import json
import sys
import tempfile
import unittest
from pathlib import Path


sys.path.insert(0, str(Path(__file__).resolve().parent))

from assemble_runtime import (  # noqa: E402
    RuntimeAssemblyError,
    RuntimeAssemblyInputs,
    assemble_runtime,
)


class RuntimeAssemblyTests(unittest.TestCase):
    def make_fixture(self, root: Path) -> RuntimeAssemblyInputs:
        profile_names = {
            "paintman.image-profile-v2.json",
            "paintman.mesh-profile-v2.json",
            "paintman_cube.image-profile-v2.json",
            "paintman_cube.mesh-profile-v2.json",
            "paintman_hukuyoka.image-profile-v2.json",
            "paintman_hukuyoka.mesh-profile-v2.json",
        }
        font_names = {
            "D-DIN-Bold.otf",
            "D-DIN-Italic.otf",
            "D-DIN.otf",
            "D-DINCondensed-Bold.otf",
            "D-DINCondensed.otf",
            "D-DINExp-Bold.otf",
            "D-DINExp-Italic.otf",
            "D-DINExp.otf",
        }
        project = root / "project"
        resources = project / "resources"
        (resources / "localization").mkdir(parents=True)
        (resources / "mesh-profiles").mkdir()
        (resources / "fonts/d-din").mkdir(parents=True)
        (resources / "licenses").mkdir()
        (resources / "localization/catalog.json").write_text(
            '{"locales":["en"]}\n',
            encoding="utf-8",
        )
        for name in profile_names:
            (resources / "mesh-profiles" / name).write_text(
                '{"schema_version":1}\n',
                encoding="utf-8",
            )
        for name in font_names:
            (resources / "fonts/d-din" / name).write_bytes(
                b"font"
            )
        (resources / "fonts/d-din/SIL Open Font License.txt").write_text(
            "OFL license\n",
            encoding="utf-8",
        )
        (resources / "licenses/libwebp-COPYING.txt").write_text(
            "libwebp license\n",
            encoding="utf-8",
        )
        (project / "LICENSE.txt").write_text(
            "project license\n",
            encoding="utf-8",
        )

        ue4ss = root / "ue4ss"
        ue4ss.mkdir()
        (ue4ss / "UE4SS.dll").write_bytes(b"ue4ss")
        (ue4ss / "dwmapi.dll").write_bytes(b"proxy")
        (ue4ss / "LICENSE").write_text(
            "ue4ss license\n",
            encoding="utf-8",
        )
        (ue4ss / "MemberVariableLayout.ini").write_text(
            "[Game]\n",
            encoding="utf-8",
        )
        (ue4ss / "UE4SS-settings.ini").write_text(
            "\n".join(
                (
                    "[General]",
                    "EnableHotReloadSystem = 1",
                    "EnableAutoReloadingLuaMods = 1",
                    "UseCache = 1",
                    "bUseUObjectArrayCache = true",
                    "[Debug]",
                    "ConsoleEnabled = 1",
                    "GuiConsoleEnabled = 1",
                    "GuiConsoleVisible = 1",
                    "[CrashDump]",
                    "EnableDumping = 1",
                    "FullMemoryDump = 1",
                    "",
                )
            ),
            encoding="utf-8",
        )

        mod = root / "main.dll"
        mod.write_bytes(b"mod")
        notices = root / "THIRD-PARTY-NOTICES.txt"
        notices.write_text(
            "Dependency notices\n",
            encoding="utf-8",
        )
        return RuntimeAssemblyInputs(
            project_root=project,
            ue4ss_binary=ue4ss / "UE4SS.dll",
            proxy_binary=ue4ss / "dwmapi.dll",
            mod_binary=mod,
            ue4ss_settings=ue4ss / "UE4SS-settings.ini",
            member_variable_layout=ue4ss / "MemberVariableLayout.ini",
            ue4ss_license=ue4ss / "LICENSE",
            dependency_notices=notices,
            output_root=root / "payload",
            layout_output=root / "payload-layout.json",
        )

    def test_assembles_exact_minimal_runtime_and_layout(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            inputs = self.make_fixture(root)

            layout = assemble_runtime(inputs)

            self.assertEqual(
                layout["generated_paths"],
                ["UE4SS.log", "cache"],
            )
            roles = {
                item["path"]: item["role"]
                for item in layout["files"]
            }
            expected_roles = {
                    "Licenses/D-DIN-OFL.txt": "license",
                    "Licenses/MecchaCamouflage-LICENSE.txt": "license",
                    "Licenses/THIRD-PARTY-NOTICES.txt": "license",
                    "Licenses/UE4SS-LICENSE.txt": "license",
                    "Licenses/libwebp-COPYING.txt": "license",
                    "MemberVariableLayout.ini": "config",
                    "Mods/MecchaCamouflage/dlls/main.dll": "mod",
                    "Mods/MecchaCamouflage/enabled.txt": "config",
                    "Mods/MecchaCamouflage/resources/localization/catalog.json":
                        "localization",
                    "UE4SS-settings.ini": "config",
                    "UE4SS.dll": "runtime",
                    "dwmapi.dll": "proxy",
            }
            expected_roles.update(
                {
                    "Mods/MecchaCamouflage/resources/fonts/"
                    f"{name}": "font"
                    for name in {
                        "D-DIN-Bold.otf",
                        "D-DIN-Italic.otf",
                        "D-DIN.otf",
                        "D-DINCondensed-Bold.otf",
                        "D-DINCondensed.otf",
                        "D-DINExp-Bold.otf",
                        "D-DINExp-Italic.otf",
                        "D-DINExp.otf",
                    }
                }
            )
            expected_roles.update(
                {
                    "Mods/MecchaCamouflage/resources/mesh-profiles/"
                    f"{name}": "profile"
                    for name in {
                        "paintman.image-profile-v2.json",
                        "paintman.mesh-profile-v2.json",
                        "paintman_cube.image-profile-v2.json",
                        "paintman_cube.mesh-profile-v2.json",
                        "paintman_hukuyoka.image-profile-v2.json",
                        "paintman_hukuyoka.mesh-profile-v2.json",
                    }
                }
            )
            self.assertEqual(
                roles,
                expected_roles,
            )
            self.assertEqual(
                sorted(
                    path.relative_to(inputs.output_root).as_posix()
                    for path in inputs.output_root.rglob("*")
                    if path.is_file()
                ),
                sorted(roles),
            )
            self.assertEqual(
                (inputs.output_root / "UE4SS.dll").read_bytes(),
                b"ue4ss",
            )
            self.assertEqual(
                (
                    inputs.output_root
                    / "Mods/MecchaCamouflage/dlls/main.dll"
                ).read_bytes(),
                b"mod",
            )
            self.assertEqual(
                (
                    inputs.output_root
                    / "Mods/MecchaCamouflage/enabled.txt"
                ).read_bytes(),
                b"",
            )
            settings = (
                inputs.output_root / "UE4SS-settings.ini"
            ).read_text(encoding="utf-8")
            for expected in (
                "EnableHotReloadSystem = 0",
                "EnableAutoReloadingLuaMods = 0",
                "UseCache = 1",
                "bUseUObjectArrayCache = false",
                "ConsoleEnabled = 0",
                "GuiConsoleEnabled = 0",
                "GuiConsoleVisible = 0",
                "EnableDumping = 0",
                "FullMemoryDump = 0",
            ):
                self.assertIn(expected, settings)
            self.assertIn(
                "EnableHotReloadSystem = 1",
                inputs.ue4ss_settings.read_text(encoding="utf-8"),
            )
            self.assertEqual(
                json.loads(
                    inputs.layout_output.read_text(encoding="utf-8")
                ),
                layout,
            )
            self.assertTrue(
                inputs.layout_output.read_bytes().endswith(b"\n")
            )

    def test_refuses_missing_or_ambiguous_settings(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            inputs = self.make_fixture(root)
            inputs.ue4ss_settings.write_text(
                "[Debug]\nConsoleEnabled = 1\n",
                encoding="utf-8",
            )

            with self.assertRaisesRegex(
                RuntimeAssemblyError,
                "setting",
            ):
                assemble_runtime(inputs)

            self.assertFalse(inputs.output_root.exists())
            self.assertFalse(inputs.layout_output.exists())

    def test_refuses_existing_outputs_and_linked_inputs(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            inputs = self.make_fixture(root)
            inputs.output_root.mkdir()
            (inputs.output_root / "unknown").write_text(
                "keep",
                encoding="utf-8",
            )

            with self.assertRaisesRegex(
                RuntimeAssemblyError,
                "already exists",
            ):
                assemble_runtime(inputs)
            self.assertEqual(
                (inputs.output_root / "unknown").read_text(
                    encoding="utf-8"
                ),
                "keep",
            )

            inputs.output_root.rename(root / "preserved")
            inputs.mod_binary = root / "linked-main.dll"
            try:
                inputs.mod_binary.symlink_to(root / "main.dll")
            except OSError:
                self.skipTest("symbolic links are unavailable")
            with self.assertRaisesRegex(
                RuntimeAssemblyError,
                "link or reparse",
            ):
                assemble_runtime(inputs)

    def test_failure_preserves_no_partial_publication(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            inputs = self.make_fixture(root)
            inputs.dependency_notices.write_bytes(b"")

            with self.assertRaisesRegex(
                RuntimeAssemblyError,
                "empty",
            ):
                assemble_runtime(inputs)

            self.assertFalse(inputs.output_root.exists())
            self.assertFalse(inputs.layout_output.exists())
            self.assertFalse(
                any(
                    path.name.startswith(".meccha-runtime-assembly-")
                    for path in root.iterdir()
                )
            )


if __name__ == "__main__":
    unittest.main()
