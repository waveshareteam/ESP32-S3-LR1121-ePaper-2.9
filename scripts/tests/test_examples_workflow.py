from __future__ import annotations

import re
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
WORKFLOW = REPO_ROOT / ".github/workflows/examples.yml"


class ExamplesWorkflowTests(unittest.TestCase):
    def test_container_git_command_trusts_the_workspace(self) -> None:
        workflow = WORKFLOW.read_text(encoding="utf-8")

        self.assertIn(
            'git -c safe.directory="$GITHUB_WORKSPACE" show -s --format=%ct "$SOURCE_SHA"',
            workflow,
        )

    def test_release_runs_after_matrix_failures_and_validates_coverage(self) -> None:
        workflow = WORKFLOW.read_text(encoding="utf-8")

        self.assertIn("if: always() && startsWith(github.ref, 'refs/tags/v')", workflow)
        self.assertIn("python3 releases/validate_release_artifacts.py release-artifacts", workflow)

        release_section = workflow.split("  release:\n", 1)[1]
        checkout = re.search(
            r"^      - uses: actions/checkout@[^\s]+(?:\s+#.*)?$",
            release_section,
            re.MULTILINE,
        )
        self.assertIsNotNone(checkout, "Release job must check out the repository")
        self.assertLess(
            checkout.start(),
            release_section.index("releases/validate_release_artifacts.py"),
        )

    def test_arduino_packages_the_build_path(self) -> None:
        workflow = WORKFLOW.read_text(encoding="utf-8")
        arduino_section = workflow.split("  build-arduino:\n", 1)[1].split(
            "  release:\n", 1
        )[0]

        self.assertIn('--build-path "$build_dir"', arduino_section)
        self.assertNotIn('--output-dir "$build_dir"', arduino_section)

    def test_experimental_sdk_cannot_publish_as_stable_release(self):
        workflow = WORKFLOW.read_text(encoding="utf-8")
        experimental = workflow.split("  build-esp-idf-experimental:\n", 1)[1].split(
            "  discover-arduino:\n", 1)[0]
        self.assertIn('fetch --depth 1 origin "$IDF_REVISION"', experimental)
        self.assertIn('rev-parse HEAD', experimental)
        self.assertNotIn('package_firmware.py', experimental)
        self.assertNotIn('upload-artifact', experimental)
        release = workflow.split("  release:\n", 1)[1]
        self.assertNotIn('build-esp-idf-experimental', release)

    def test_both_sdk_channels_run_observer_regression(self):
        workflow = WORKFLOW.read_text(encoding="utf-8")
        self.assertEqual(workflow.count('IDF_BUILD_DIR="$build_dir" python'), 2)


if __name__ == "__main__":
    unittest.main()
