from __future__ import annotations

import argparse
import json
import sys
import tempfile
import unittest
from pathlib import Path


sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import discover_examples  # noqa: E402


class DiscoverExamplesTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.repo = Path(self.temporary.name)
        (self.repo / "config").mkdir()
        (self.repo / "config/ci.json").write_text(
            json.dumps(
                {
                    "schema_version": 1,
                    "board": {"target": "esp32s3"},
                    "esp_idf": {"versions": ["v5.5.5", "v6.0.2"]},
                    "arduino": {
                        "cli": "1.5.1",
                        "core": "3.3.11",
                        "fqbn": "esp32:esp32:esp32s3:PSRAM=opi",
                    },
                }
            ),
            encoding="utf-8",
        )

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def args(self, surface: str, selector: str = "all") -> argparse.Namespace:
        return argparse.Namespace(
            repo=str(self.repo),
            config="config/ci.json",
            surface=surface,
            selector=selector,
            idf_versions=None,
            arduino_core=None,
            fqbn=None,
            target=None,
            expect_examples=None,
            allow_empty_selector=False,
        )

    def test_idf_discovery_only_accepts_direct_projects(self) -> None:
        (self.repo / "examples/esp-idf/01_first").mkdir(parents=True)
        (self.repo / "examples/esp-idf/01_first/CMakeLists.txt").touch()
        (self.repo / "examples/esp-idf/01_first/components/vendor/example").mkdir(parents=True)
        (self.repo / "examples/esp-idf/01_first/components/vendor/example/CMakeLists.txt").touch()
        (self.repo / "examples/esp-idf/not_a_project").mkdir()

        matrix, count = discover_examples.build_matrix(self.args("esp-idf"))

        self.assertEqual(count, 1)
        self.assertEqual(len(matrix["include"]), 2)
        self.assertEqual({item["path"] for item in matrix["include"]}, {"examples/esp-idf/01_first"})
        self.assertEqual({item["idf"] for item in matrix["include"]}, {"v5.5.5", "v6.0.2"})

    def test_arduino_discovery_excludes_libraries_and_nested_ino_files(self) -> None:
        sketch = self.repo / "examples/arduino/01_first"
        sketch.mkdir(parents=True)
        (sketch / "01_first.ino").touch()
        nested = self.repo / "examples/arduino/02_nested/src"
        nested.mkdir(parents=True)
        (nested / "not_a_top_level_sketch.ino").touch()
        vendor = self.repo / "examples/arduino/libraries/vendor/examples/demo"
        vendor.mkdir(parents=True)
        (vendor / "demo.ino").touch()

        matrix, count = discover_examples.build_matrix(self.args("arduino"))

        self.assertEqual(count, 1)
        self.assertEqual([item["name"] for item in matrix["include"]], ["01_first"])
        self.assertEqual(matrix["include"][0]["cli"], "1.5.1")
        self.assertIn("PSRAM=opi", matrix["include"][0]["fqbn"])

    def test_selector_matches_name_or_complete_path(self) -> None:
        for name in ("01_first", "02_second"):
            path = self.repo / "examples/esp-idf" / name
            path.mkdir(parents=True)
            (path / "CMakeLists.txt").touch()

        by_name, _ = discover_examples.build_matrix(self.args("esp-idf", "02_second"))
        by_path, _ = discover_examples.build_matrix(
            self.args("esp-idf", "examples/esp-idf/01_first")
        )

        self.assertEqual({item["name"] for item in by_name["include"]}, {"02_second"})
        self.assertEqual({item["name"] for item in by_path["include"]}, {"01_first"})

    def test_unknown_selector_is_rejected(self) -> None:
        path = self.repo / "examples/esp-idf/01_first"
        path.mkdir(parents=True)
        (path / "CMakeLists.txt").touch()

        with self.assertRaisesRegex(ValueError, "matched no esp-idf examples"):
            discover_examples.build_matrix(self.args("esp-idf", "missing"))

    def test_arduino_main_file_must_match_directory_case(self) -> None:
        sketch = self.repo / "examples/arduino/01_first"
        sketch.mkdir(parents=True)
        (sketch / "01_FIRST.ino").touch()

        with self.assertRaisesRegex(ValueError, "case-exact"):
            discover_examples.build_matrix(self.args("arduino"))

    def test_empty_surface_can_be_allowed_after_cross_surface_validation(self) -> None:
        sketch = self.repo / "examples/arduino/01_first"
        sketch.mkdir(parents=True)
        (sketch / "01_first.ino").touch()
        args = self.args("esp-idf", "01_first")
        args.allow_empty_selector = True

        matrix, count = discover_examples.build_matrix(args)

        self.assertEqual(count, 0)
        self.assertEqual(matrix, {"include": []})

    def configure_source_only(self) -> None:
        excluded = []
        for surface in ("esp-idf", "arduino"):
            for name in ("06_pingpong", "10_lorawan"):
                project = self.repo / "examples" / surface / name
                project.mkdir(parents=True)
                marker = "CMakeLists.txt" if surface == "esp-idf" else f"{name}.ino"
                (project / marker).touch()
            excluded.append(f"examples/{surface}/10_lorawan")
        path = self.repo / "config/ci.json"
        config = json.loads(path.read_text())
        config["source_only_examples"] = excluded
        path.write_text(json.dumps(config))

    def test_source_only_projects_are_counted_but_not_built(self) -> None:
        self.configure_source_only()
        for surface in ("esp-idf", "arduino"):
            with self.subTest(surface=surface):
                args = self.args(surface)
                args.expect_examples = 2
                matrix, count = discover_examples.build_matrix(args)
                self.assertEqual(count, 2)
                self.assertEqual({item["name"] for item in matrix["include"]}, {"06_pingpong"})

    def test_manual_selector_cannot_build_source_only_projects(self) -> None:
        self.configure_source_only()
        for surface in ("esp-idf", "arduino"):
            for selector in ("10_lorawan", f"examples/{surface}/10_lorawan"):
                with self.subTest(surface=surface, selector=selector):
                    args = self.args(surface, selector)
                    with self.assertRaisesRegex(ValueError, "source-only.*build locally"):
                        discover_examples.build_matrix(args)
                    args.allow_empty_selector = True
                    matrix, _ = discover_examples.build_matrix(args)
                    self.assertEqual(matrix, {"include": []})

    def test_source_only_policy_rejects_stale_paths(self) -> None:
        self.configure_source_only()
        (self.repo / "examples/arduino/10_lorawan/10_lorawan.ino").unlink()
        with self.assertRaisesRegex(ValueError, "unknown source-only examples"):
            discover_examples.build_matrix(self.args("esp-idf"))

    def test_source_only_policy_rejects_string_instead_of_list(self) -> None:
        path = self.repo / "config/ci.json"
        config = json.loads(path.read_text())
        config["source_only_examples"] = "examples/esp-idf/10_lorawan"
        path.write_text(json.dumps(config))
        with self.assertRaisesRegex(ValueError, "must be a list"):
            discover_examples.build_matrix(self.args("esp-idf"))

    def test_source_only_projects_are_excluded_from_experimental_matrix(self) -> None:
        self.configure_source_only()
        args = self.configure_experimental()
        matrix, count = discover_examples.build_matrix(args)
        self.assertEqual(count, 3)
        self.assertEqual({item["name"] for item in matrix["include"]},
                         {"06_pingpong", "09_demo"})

    def configure_experimental(self, revision="08e0d30a74ad0bfd5a34933142b80f45619ee410"):
        path = self.repo / "config/ci.json"
        config = json.loads(path.read_text())
        config["esp_idf"]["experimental_versions"] = [
            {"version": "v6.2.0-dev", "ref": revision}
        ]
        path.write_text(json.dumps(config))
        project = self.repo / "examples/esp-idf/09_demo"
        project.mkdir(parents=True)
        (project / "CMakeLists.txt").touch()
        args = self.args("esp-idf")
        args.idf_experimental = True
        return args

    def test_experimental_sdk_is_separate_and_pinned(self):
        args = self.configure_experimental()
        matrix, count = discover_examples.build_matrix(args)
        self.assertEqual(count, 1)
        self.assertEqual(len(matrix["include"]), 1)
        entry = matrix["include"][0]
        self.assertEqual(entry["idf"], "v6.2.0-dev")
        self.assertEqual(entry["idf_ref"], "08e0d30a74ad0bfd5a34933142b80f45619ee410")
        stable, _ = discover_examples.build_matrix(self.args("esp-idf"))
        self.assertFalse(any("idf_ref" in row for row in stable["include"]))

    def test_experimental_sdk_rejects_floating_revision(self):
        args = self.configure_experimental("master")
        with self.assertRaisesRegex(ValueError, "full commit SHA"):
            discover_examples.build_matrix(args)

    def test_experimental_sdk_rejects_unpinned_cli_override(self):
        args = self.configure_experimental()
        args.idf_versions = "v6.2.0"
        with self.assertRaisesRegex(ValueError, "must come from config"):
            discover_examples.build_matrix(args)


if __name__ == "__main__":
    unittest.main()
