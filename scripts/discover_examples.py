#!/usr/bin/env python3
"""Discover first-party ESP-IDF and Arduino examples for CI."""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path
from typing import Any


IDF_ROOT = Path("examples/esp-idf")
ARDUINO_ROOT = Path("examples/arduino")
DEFAULT_CONFIG = Path("config/ci.json")
DEFAULT_IDF_VERSIONS = ("v5.5.6",)
DEFAULT_ARDUINO_CLI = "1.5.1"
DEFAULT_ARDUINO_CORE = "3.3.11"
DEFAULT_TARGET = "esp32s3"
DEFAULT_FQBN = "esp32:esp32:esp32s3"
SAFE_NAME = re.compile(r"^[A-Za-z0-9._-]+$")
SAFE_VERSION = re.compile(r"^v?\d+\.\d+\.\d+$")
SAFE_TARGET = re.compile(r"^esp32[a-z0-9]+$")
SAFE_FQBN = re.compile(r"^[A-Za-z0-9:.,=_-]+$")


def normalize(value: str) -> str:
    return value.replace("\\", "/").strip("/")


def slugify(value: str) -> str:
    value = re.sub(r"[^A-Za-z0-9._-]+", "-", value)
    value = re.sub(r"-+", "-", value).strip("-")
    return "example" if value in ("", ".", "..") else value


def selector_matches(entry: dict[str, str], selector: str) -> bool:
    if not selector or selector == "all":
        return True
    selector = normalize(selector)
    path = normalize(entry["path"])
    return selector in {entry["name"], path} or path.startswith(selector + "/")


def discover_esp_idf(repo: Path) -> list[dict[str, str]]:
    """Return direct first-party projects, excluding nested component examples."""
    root = repo / IDF_ROOT
    if not root.is_dir():
        return []
    entries: list[dict[str, str]] = []
    for project in sorted(root.iterdir(), key=lambda item: item.name.lower()):
        if not project.is_dir() or not (project / "CMakeLists.txt").is_file():
            continue
        if not SAFE_NAME.fullmatch(project.name):
            raise ValueError(f"unsafe ESP-IDF project directory name: {project.name!r}")
        entries.append(
            {
                "name": project.name,
                "slug": slugify(project.name),
                "path": project.relative_to(repo).as_posix(),
            }
        )
    return entries


def discover_arduino(repo: Path) -> list[dict[str, str]]:
    """Return direct first-party sketches, never vendored library examples."""
    root = repo / ARDUINO_ROOT
    if not root.is_dir():
        return []
    entries: list[dict[str, str]] = []
    for sketch in sorted(root.iterdir(), key=lambda item: item.name.lower()):
        if not sketch.is_dir():
            continue
        ino_files = sorted(sketch.glob("*.ino"), key=lambda item: item.name.lower())
        if not ino_files:
            continue
        if not SAFE_NAME.fullmatch(sketch.name):
            raise ValueError(f"unsafe Arduino sketch directory name: {sketch.name!r}")
        expected_name = f"{sketch.name}.ino"
        if expected_name not in {path.name for path in ino_files}:
            raise ValueError(
                f"Arduino sketch {sketch.name!r} must contain a case-exact {expected_name!r}"
            )
        entries.append(
            {
                "name": sketch.name,
                "slug": slugify(sketch.name),
                "path": sketch.relative_to(repo).as_posix(),
            }
        )
    return entries


def load_config(repo: Path, config_path: str) -> dict[str, Any]:
    path = Path(config_path)
    if not path.is_absolute():
        path = repo / path
    if not path.is_file():
        raise FileNotFoundError(f"CI configuration not found: {path}")
    data = json.loads(path.read_text(encoding="utf-8"))
    if data.get("schema_version") != 1:
        raise ValueError(f"unsupported CI configuration schema in {path}")
    return data


def source_only_projects(repo: Path, config: dict[str, Any]) -> set[str]:
    """Use the same explicit exclusions for CI matrices and release coverage."""
    paths = config.get("source_only_examples", [])
    if not isinstance(paths, list) or any(not isinstance(path, str) for path in paths):
        raise ValueError("source_only_examples must be a list of example paths")
    excluded = set(paths)
    known = {entry["path"] for entry in discover_esp_idf(repo) + discover_arduino(repo)}
    unknown = excluded - known
    if unknown:
        raise ValueError("unknown source-only examples: " + ", ".join(sorted(unknown)))
    return excluded


def split_versions(value: str | None, configured: Any) -> list[str]:
    if value is not None:
        versions = [item.strip() for item in value.split(",") if item.strip()]
    elif isinstance(configured, list):
        versions = [str(item).strip() for item in configured if str(item).strip()]
    else:
        versions = list(DEFAULT_IDF_VERSIONS)
    if not versions:
        raise ValueError("at least one ESP-IDF version is required")
    if any(not SAFE_VERSION.fullmatch(version) for version in versions):
        raise ValueError("ESP-IDF versions must be release tags such as v5.5.6")
    return versions


def build_matrix(args: argparse.Namespace) -> tuple[dict[str, list[dict[str, str]]], int]:
    repo = Path(args.repo).resolve()
    config = load_config(repo, args.config)
    board = config.get("board", {})
    selector = normalize(args.selector)
    excluded = source_only_projects(repo, config)

    if args.surface == "esp-idf":
        discovered = discover_esp_idf(repo)
        selected = [entry for entry in discovered
                    if entry["path"] not in excluded and selector_matches(entry, selector)]
        idf_config = config.get("esp_idf", {})
        experimental = getattr(args, "idf_experimental", False)
        if experimental:
            if args.idf_versions:
                raise ValueError("experimental SDK refs must come from config/ci.json")
            versions = idf_config.get("experimental_versions", [])
            if not versions:
                raise ValueError("no experimental ESP-IDF version configured")
            for version in versions:
                if (not isinstance(version, dict)
                        or not re.fullmatch(r"v\d+\.\d+\.\d+-dev", str(version.get("version", "")))
                        or not re.fullmatch(r"[0-9a-f]{40}", str(version.get("ref", "")))):
                    raise ValueError("experimental SDKs require a dev label and full commit SHA")
        else:
            versions = split_versions(args.idf_versions, idf_config.get("versions"))
        target = args.target or str(board.get("target") or DEFAULT_TARGET)
        if not SAFE_TARGET.fullmatch(target):
            raise ValueError(f"invalid ESP target: {target!r}")
        if experimental:
            include = [entry | {"idf": version["version"], "idf_ref": version["ref"],
                                "target": target} for entry in selected for version in versions]
        else:
            include = [entry | {"idf": version, "target": target} for entry in selected for version in versions]
    else:
        discovered = discover_arduino(repo)
        selected = [entry for entry in discovered
                    if entry["path"] not in excluded and selector_matches(entry, selector)]
        arduino = config.get("arduino", {})
        cli = str(arduino.get("cli") or DEFAULT_ARDUINO_CLI)
        core = args.arduino_core or str(arduino.get("core") or DEFAULT_ARDUINO_CORE)
        fqbn = args.fqbn or str(arduino.get("fqbn") or DEFAULT_FQBN)
        if not SAFE_VERSION.fullmatch(cli):
            raise ValueError(f"invalid Arduino CLI version: {cli!r}")
        if not SAFE_VERSION.fullmatch(core):
            raise ValueError(f"invalid Arduino core version: {core!r}")
        if not SAFE_FQBN.fullmatch(fqbn):
            raise ValueError("Arduino FQBN contains unsupported characters")
        include = [entry | {"cli": cli, "core": core, "fqbn": fqbn} for entry in selected]

    if args.expect_examples is not None and len(discovered) != args.expect_examples:
        raise ValueError(
            f"expected {args.expect_examples} {args.surface} examples, discovered {len(discovered)}"
        )
    if selector not in ("", "all") and not selected and not args.allow_empty_selector:
        if any(selector_matches(entry, selector) for entry in discovered):
            raise ValueError(
                f"selector {args.selector!r} only matches source-only examples; "
                "configure credentials and build locally"
            )
        raise ValueError(f"selector {args.selector!r} matched no {args.surface} examples")
    return {"include": include}, len(discovered)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", default=".")
    parser.add_argument("--config", default=DEFAULT_CONFIG.as_posix())
    parser.add_argument("--surface", choices=("esp-idf", "arduino"), required=True)
    parser.add_argument("--selector", default="all")
    parser.add_argument("--idf-versions", help="Comma-separated override for config/ci.json.")
    parser.add_argument("--idf-experimental", action="store_true",
                        help="Use pinned development SDKs, separately from the stable release matrix.")
    parser.add_argument("--arduino-core", help="Override the configured Arduino core version.")
    parser.add_argument("--fqbn", help="Override the configured Arduino FQBN.")
    parser.add_argument("--target", help="Override the configured ESP-IDF target.")
    parser.add_argument("--expect-examples", type=int)
    parser.add_argument(
        "--allow-empty-selector",
        action="store_true",
        help="Allow no match on this surface after a separate cross-surface selector check.",
    )
    parser.add_argument("--github-output")
    args = parser.parse_args()

    try:
        matrix, discovered_count = build_matrix(args)
        output = json.dumps(matrix, separators=(",", ":"))
        if args.github_output:
            with open(args.github_output, "a", encoding="utf-8", newline="\n") as output_file:
                output_file.write(f"matrix={output}\n")
                output_file.write(f"count={len(matrix['include'])}\n")
                output_file.write(f"example_count={discovered_count}\n")
        else:
            print(output)
    except (OSError, ValueError, json.JSONDecodeError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
