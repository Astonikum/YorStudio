"""Validate the checked-in YOR project template with Python's stdlib only.

This is repository tooling, not YorStudio product/runtime code. The product
implementation remains C++.
"""

from __future__ import annotations

import json
import sys
import uuid
from pathlib import Path


REQUIRED_KEYS = {
    "schema_version",
    "project_guid",
    "name",
    "engine",
    "toolchain",
    "startup_scene",
    "target_platforms",
    "modules",
    "content_roots",
    "editor",
}


def fail(message: str) -> None:
    raise ValueError(message)


def validate(project_dir: Path) -> None:
    manifest_path = project_dir / "project.yorproject"
    if not manifest_path.is_file():
        fail(f"missing manifest: {manifest_path}")

    try:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as error:
        fail(f"invalid JSON: {error}")

    if not isinstance(manifest, dict):
        fail("manifest root must be an object")
    missing = REQUIRED_KEYS - manifest.keys()
    if missing:
        fail(f"missing required keys: {', '.join(sorted(missing))}")
    if manifest["schema_version"] != 1:
        fail("unsupported schema_version; expected 1")

    try:
        uuid.UUID(manifest["project_guid"])
    except (ValueError, AttributeError, TypeError) as error:
        fail(f"project_guid must be a UUID: {error}")

    name = manifest["name"]
    if not isinstance(name, str) or not name.strip() or any(char in name for char in "\\/\0"):
        fail("name must be a non-empty path-safe string")

    engine = manifest["engine"]
    if not isinstance(engine, dict) or not engine.get("repository") or not engine.get("version"):
        fail("engine must contain repository and version")
    if engine["repository"] != "https://github.com/Astonikum/YorEngine.git":
        fail("engine.repository must use the canonical YorEngine URL")

    toolchain = manifest["toolchain"]
    if not isinstance(toolchain, dict) or toolchain.get("cxx_standard") != "c++20":
        fail("toolchain.cxx_standard must be c++20")

    startup_scene = manifest["startup_scene"]
    if not isinstance(startup_scene, str) or Path(startup_scene).is_absolute() or ".." in Path(startup_scene).parts:
        fail("startup_scene must be a relative path without parent traversal")

    for field in ("target_platforms", "modules", "content_roots"):
        if not isinstance(manifest[field], list) or any(not isinstance(item, str) or not item for item in manifest[field]):
            fail(f"{field} must be a list of non-empty strings")
    editor = manifest["editor"]
    if not isinstance(editor, dict) or editor.get("ui_adapter") != "imgui":
        fail("editor.ui_adapter must be imgui in the initial contract")


def main() -> int:
    project_dir = Path(sys.argv[1] if len(sys.argv) > 1 else "templates/empty-project").resolve()
    try:
        validate(project_dir)
    except ValueError as error:
        print(f"YOR project validation failed: {error}", file=sys.stderr)
        return 1
    print(f"YOR project is valid: {project_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
