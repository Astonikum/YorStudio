# YOR project format

The project format is a versioned public contract shared by YorStudio, the
headless project CLI, CI, and future runtime tooling. A project must remain
buildable without YorStudio.

## Layout

```text
MyGame/
  project.yorproject        # versioned, human-owned manifest
  code/
    include/                # public project headers
    src/                    # C++ gameplay and application code
    tests/                  # project tests
  assets/                   # source assets and import metadata
  scenes/                   # YorEngine scenes and prefabs
  shaders/                  # shader source and includes
  config/                   # versioned game configuration
  plugins/                  # explicitly enabled native extensions
  build/                    # local output, never source content
  .yor/
    project.lock            # schema/toolchain lock
    editor/                 # local layout and selection state
    cache/                  # disposable import/shader cache
    derived/                # disposable derived asset database
    logs/                   # editor/import/build logs
    generated/              # disposable generated code
    recovery/               # disposable recovery markers
```

The user-owned inputs are the manifest, `code/`, `assets/`, `scenes/`,
`shaders/`, `config/`, and explicitly enabled `plugins/`. `build/` and `.yor/`
are machine-local or reproducible output and must never be the only source of
an asset or scene.

## Manifest v1

`project.yorproject` is UTF-8 JSON. The first contract requires these fields:

```json
{
  "schema_version": 1,
  "project_guid": "a UUID generated for this project",
  "name": "A human-readable project name",
  "engine": {
    "repository": "https://github.com/Astonikum/YorEngine.git",
    "version": "v0.1.0"
  },
  "toolchain": { "cxx_standard": "c++20" },
  "startup_scene": "scenes/main.yorscene",
  "target_platforms": ["windows-x64"],
  "modules": [],
  "content_roots": ["assets", "scenes", "shaders"],
  "editor": { "ui_adapter": "imgui" }
}
```

The native C++ contract in `include/yorstudio/project_manifest.hpp` is the sole
parser/validator surface for the launcher, editor, CLI, and CI. `ProjectManifest`
creates, parses, migrates, serializes, and atomically writes manifests;
`createProject` publishes a complete project directory through a sibling
temporary directory; `validateProject` optionally validates the on-disk layout.
No project code, plugin, script, or import hook is loaded by these operations.

Manifest paths are stored with `/`, preserve case, reject absolute paths,
parent/current-directory segments, drive/stream separators, empty segments,
and control characters. Content roots must be unique and the startup scene must
be inside one of them. Project roots and layout paths containing symbolic links
are rejected during layout validation. The engine revision must be a `v`-release
tag or a full immutable commit id; moving projects preserve `project_guid`.
Manifest writes retain unknown top-level and nested extension fields during
migrations and replace the destination only after the temporary file is fully
written.

## Hidden state and safety

- Project discovery is limited to explicit workspace roots and recent projects.
- Opening a project does not execute C++ or load native plugins.
- `.yor/cache/` and `.yor/derived/` may be deleted and rebuilt at any time.
- `.yor/editor/` is local editor state and must not alter scene serialization.
- A lock records owner and tool version; stale locks require explicit recovery.
- Safe mode disables third-party modules and offers reset of disposable state.
- Move/copy operations preserve `project_guid`; a duplicate gets a new identity.

## Git dependencies

YorStudio will consume YorEngine through CMake `FetchContent` or an equivalent
Git dependency only after the public integration contract exists. The URL must
be `https://github.com/Astonikum/YorEngine.git` and the revision must be a
release tag or immutable commit. A project may override the revision through a
validated lock file, but neither CI nor released tooling may silently follow
`main`.
