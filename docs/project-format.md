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

The actual launcher will validate UUIDs, paths, duplicate entries, supported
versions, repository URLs, and toolchain constraints before loading project
code, plugins, scripts, or import hooks. Manifest writes are atomic and retain
unknown extension fields during migrations.

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
