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
    project.lock            # exclusive editor/project lock
    safe-mode.json          # launcher safe-mode marker
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

## Scene v1

Scene files use the `.yorscene` extension and are versioned JSON owned by the
project. The minimum shape is:

```json
{
  "schema_version": 1,
  "objects": [
    {
      "guid": "a UUID",
      "name": "Player",
      "active": true,
      "transform": {
        "position": [0, 0, 0],
        "rotation": [0, 0, 0, 1],
        "scale": [1, 1, 1]
      },
      "parent_guid": null
    }
  ]
}
```

`guid` values identify objects independently of runtime `EntityId` values.
Parent references must resolve within the same file and may not form cycles.
The native document loader rejects malformed version-1 data before it replaces
the open scene. Scene saves use a sibling temporary file followed by an atomic
replacement. Unknown top-level and per-object fields are retained verbatim for
forward-compatible editor extensions; `.yor/editor/` state is not serialized
into the scene.

## Hidden state and safety

- Project discovery is limited to explicit workspace roots and recent projects.
- Opening a project does not execute C++ or load native plugins.
- `.yor/cache/` and `.yor/derived/` may be deleted and rebuilt at any time.
- `.yor/editor/` is local editor state and must not alter scene serialization.
- `.yor/project.lock` is a native C++ JSON record containing schema version,
  project GUID, owner id, host, process id, acquisition timestamp, and Studio
  version. `ProjectLock::acquire` creates it with an OS-level exclusive create;
  a second writer is rejected before any project state is changed.
- `ProjectLock::recoverStale` removes a lock only when its project GUID matches,
  its host is the current host, and its recorded process is no longer running.
  Locks from another host, malformed locks, and live owners require explicit
  operator intervention and are never silently deleted.
- Safe mode disables third-party modules and offers reset of disposable state.
- Move/copy operations preserve `project_guid`; a duplicate gets a new identity.

The native lifecycle API in `project_lifecycle.hpp` is the launcher contract:
`newProject` and `openProject` enforce workspace roots, `importProject` copies
user-authored files while preserving identity, and `duplicateProject` copies
them with a new identity. Both operations omit `.yor/` and `build/`, then
recreate disposable state. `migrateProject`, `revealProject`, recent-entry
removal, safe-mode markers, disposable-state reset, and stale-lock recovery are
also headless C++ operations; they never execute project code or plugins.

## Workspace roots and recent projects

Workspace roots are an explicit native C++ JSON contract, not a recursive
filesystem search:

```json
{
  "schema_version": 1,
  "roots": ["C:/YOR/Projects"]
}
```

`WorkspaceRoots::discover` inspects only each configured root and its direct
child directories. It validates a candidate manifest without executing project
code and returns invalid candidates as diagnostics. Symlink roots and symlink
project directories are rejected; directories outside the configured roots are
never eligible for launcher discovery.

The recent-project registry is also versioned JSON and is written atomically.
It stores at most 32 absolute project paths with project GUID, display name, and
UTC last-opened time. Recording a project moves it to the front, while removing
a recent entry never removes project files.

`WorkspaceRoots::openProject` requires the project to be inside a configured
root. `ProjectAccess::readWrite` acquires the exclusive project lock and is the
only mode allowed to save the manifest. `ProjectAccess::readOnly` never takes
the lock, so an already-running Studio can be inspected safely; attempting to
save through that session is rejected explicitly rather than silently falling
back to write access.

## Git dependencies

YorStudio will consume YorEngine through CMake `FetchContent` or an equivalent
Git dependency only after the public integration contract exists. The URL must
be `https://github.com/Astonikum/YorEngine.git` and the revision must be a
release tag or immutable commit. A project may override the revision through a
validated lock file, but neither CI nor released tooling may silently follow
`main`.
