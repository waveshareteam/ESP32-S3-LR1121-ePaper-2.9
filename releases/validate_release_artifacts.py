#!/usr/bin/env python3
"""Validate that release archives cover every CI-enabled first-party example."""

from __future__ import annotations

import argparse
import json
import sys
import zipfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from validate_firmware import validate_zip

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from discover_examples import (
    DEFAULT_CONFIG,
    discover_arduino,
    discover_esp_idf,
    load_config,
    source_only_projects,
)


def expected_projects(repo: Path) -> set[str]:
    config = load_config(repo, DEFAULT_CONFIG.as_posix())
    excluded = source_only_projects(repo, config)
    return {entry["path"] for entry in discover_esp_idf(repo) + discover_arduino(repo)
            if entry["path"] not in excluded}


def read_manifest(path: Path) -> dict:
    with zipfile.ZipFile(path) as archive:
        manifests = [name for name in archive.namelist() if name.endswith("/manifest.json")]
        if len(manifests) != 1:
            raise ValueError(f"{path.name} must contain exactly one manifest.json")
        return json.loads(archive.read(manifests[0]).decode("utf-8"))


def validate_release(repo: Path, artifact_dir: Path) -> tuple[int, int]:
    archives = sorted(artifact_dir.glob("*.zip"))
    if not archives:
        raise ValueError(f"no firmware ZIP archives found in {artifact_dir}")
    if len({archive.name for archive in archives}) != len(archives):
        raise ValueError("release contains duplicate archive names")

    expected = expected_projects(repo)
    covered: set[str] = set()
    for archive in archives:
        validate_zip(archive)
        manifest = read_manifest(archive)
        project = str(manifest.get("project_path", ""))
        framework = str(manifest.get("framework", ""))
        expected_prefix = f"examples/{framework}/"
        if not project.startswith(expected_prefix):
            raise ValueError(f"{archive.name} has inconsistent framework and project_path")
        if project not in expected:
            raise ValueError(f"{archive.name} references an unknown or source-only example: {project}")
        covered.add(project)

    missing = expected - covered
    if missing:
        raise ValueError("missing firmware for examples: " + ", ".join(sorted(missing)))
    return len(archives), len(covered)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("artifact_dir")
    parser.add_argument("--repo", default=".")
    args = parser.parse_args()
    try:
        archive_count, project_count = validate_release(
            Path(args.repo).resolve(), Path(args.artifact_dir).resolve()
        )
        print(f"valid release: {archive_count} archives cover {project_count} examples")
    except (OSError, ValueError, KeyError, json.JSONDecodeError, zipfile.BadZipFile) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
