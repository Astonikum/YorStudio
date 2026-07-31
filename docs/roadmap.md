# YorStudio Roadmap

YorStudio is the C++ desktop launcher/editor product in the YOR ecosystem. Its
goal is to make YorEngine practical for creating, inspecting, building, and
shipping a complete game while keeping the runtime independent of the editor.
Dear ImGui is the first UI adapter; it is not allowed to become the editor's
domain model.

## Baseline

- YorGL provides the low-level C++ renderer/API boundary.
- YorEngine provides C++ scene, components, Runtime, and RenderSnapshot
  contracts plus secondary C/JNI bindings.
- The first YorStudio desktop executable is a Windows/DirectX 11 target with a
  pinned Dear ImGui adapter; editor state and asset database are still ahead.
- C++ is authoritative; JVM/Kotlin/Java remains a secondary integration layer.

## Phase 0 - Product contracts and project format

- [x] Add a versioned `project.yorproject` manifest schema: project GUID,
  display name, engine version, toolchain, startup scene, target platforms,
  enabled modules, content roots, and editor settings.
- [x] Define the `.yor/` hidden-state policy for lock, editor state, logs,
  caches, derived assets, generated code, and recovery markers.
- [x] Define path normalization, case sensitivity, symlink policy, project move
  behavior, project identity, and atomic manifest writes.
- [x] Define the public project API used by launcher, editor, CLI, and CI;
  none of these clients may parse ad-hoc fields independently.
- [x] Define C++20 toolchain/platform support and the first Windows desktop
  target without making Studio a dependency of runtime builds.
- [x] Define `StudioUiPort`, platform window/input ports, and a headless fake UI
  port before adding Dear ImGui widgets; the contract smoke uses the fake port.

Acceptance: a C++ contract test can create, serialize, validate, migrate, and
reject bad manifests without starting a window or loading project code. This
acceptance is covered by `yorstudio_project_smoke`.

## Phase 1 - Launcher and project lifecycle

- [x] Implement workspace roots and recent-project registry with safe path
  validation and no implicit recursive scanning outside configured roots.
- [x] Implement headless C++ New, Open, Import, Duplicate, Migrate, Remove
  Recent, Reveal, Safe Mode, Reset Disposable State, and Recover flows.
- [x] Create project directories atomically; clean up partial creation after a
  failure without deleting an existing project.
- [x] Add project lock ownership, explicit local stale-lock recovery, and clear
  diagnostics for two Studio instances opening the same project.
- [x] Add read-only mode for projects that are already locked.
- [ ] Add launcher tests for corrupt JSON/manifest, moved project, missing
  engine version, invalid path, locked project, and interrupted creation.
- [x] Add the first real Windows window and ImGui adapter after the launcher
  model passed headless tests; the adapter opens/closes a real project through
  the public launcher lifecycle.
- [x] Connect the desktop launcher to the public new-project and recent-project
  flows, persisting recent roots under the native user-data directory.

Acceptance: a user can create and reopen an empty project from the launcher,
and a corrupt cache never destroys `code/`, `assets/`, or `scenes/`.

## Phase 2 - Editor shell and command model

- [x] Add the first `StudioApplication` editor lifecycle over pinned YorEngine
  APIs: an in-memory scene document, stable EntityId selection, object creation,
  rename/transform commands, and undo/redo.
- [ ] Add `StudioApplication` lifecycle, platform window, input, clipboard,
  file dialogs, logging, crash-safe shutdown, and safe mode.
- [x] Add the first dockable ImGui editor shell: scene hierarchy, inspector,
  object creation, transform/name editing, undo, and redo.
- [ ] Add project settings, console, diagnostics, and layout persistence.
- [ ] Implement editor commands with execute/undo/redo, merge/coalescing for
  continuous edits, transaction boundaries, stable entity references, and
  failure rollback.
- [ ] Add selection model, multi-selection, focus, active document, dirty
  state, and save prompts independent of UI widgets.
- [ ] Add a headless fake UI test for every launcher/editor command exposed by
  the first shell.

Acceptance: replacing the ImGui adapter with the fake UI port leaves command,
project, and document tests unchanged.

## Phase 3 - Scene editor

- [x] Create a scene document backed by YorEngine public APIs, not a duplicate
  editor ECS.
- [x] Add hierarchy view, entity create/delete/duplicate, parenting/reparenting,
  cycle errors, active state, and stable selection.
- [ ] Add a collapsible hierarchy tree, tags, and layers.
- [ ] Add inspector schemas for Transform, Mesh, Camera, Light, and custom
  components; validate edits before committing commands.
- [ ] Add viewport using RenderSnapshot, editor grid/axes, camera navigation,
  selection outline, transform gizmos, snapping, local/world modes, and resize.
- [x] Add version-1 scene save/load with GUID references, atomic writes, and
  preservation of unknown extension data.
- [ ] Add scene migrations for future schema versions.
- [ ] Add prefab/nested-scene instances with override tracking and conflict
  diagnostics.

Acceptance: create a scene, edit a hierarchy and transform, save, reopen,
undo/redo the changes, and enter play mode without editor-only state leaking
into runtime simulation.

## Phase 4 - Content browser and asset pipeline

- [ ] Define source asset GUID/meta files, importer versions, dependency graph,
  content-addressed derived cache, cancellation, and invalidation rules.
- [ ] Add asset browser tree/search/filter/tags, import queue, progress, errors,
  retry, reveal-source, and derived-output inspection.
- [ ] Add previews for meshes, textures, materials, scenes, animation, and
  audio through replaceable preview services.
- [ ] Add deterministic shader/material compilation and diagnostics routed to
  YorGL/YorEngine contracts.
- [ ] Add cache cleanup, disk budget, stale importer detection, and CI content
  validation without opening the GUI.

Acceptance: delete all derived/cache data and reproduce identical outputs from
the same source inputs and toolchain lock.

## Phase 5 - Runtime integration and play mode

- [ ] Add edit-world to play-world cloning/instantiation with explicit ownership
  of runtime-only entities and a reliable stop/discard path.
- [ ] Add play/pause/step/restart, time scale, fixed-step diagnostics, system
  failure display, and runtime log routing.
- [ ] Add game window/embedded viewport policy without coupling simulation to
  ImGui frame timing.
- [ ] Add attachable debugger data: selected entity, component inspection,
  system timings, entity counts, asset loads, and RenderSnapshot version.
- [ ] Make standalone runtime launch use the same public project build output,
  not private editor memory.

Acceptance: a project can run in Studio and as a standalone package with the
same simulation and content inputs.

## Phase 6 - C++ project code and modules

- [ ] Define project C++ target generation, include paths, compiler flags,
  native dependencies, tests, and platform targets.
- [ ] Add explicit configure/build/run tasks with captured compiler output,
  cancellation, incremental rebuilds, and reproducible toolchain metadata.
- [ ] Add module/plugin manifests, ABI/version checks, capability permissions,
  safe mode, and unload rules; never load arbitrary project code on project
  discovery.
- [ ] Add code navigation hooks only after the build database contract exists.
- [ ] Keep generated registration/binding code under `.yor/generated/` with
  schema/tool version markers and clean regeneration.

Acceptance: a sample project with C++ gameplay code builds, runs, reports a
compiler error in the console, and remains buildable outside YorStudio.

## Phase 7 - Professional editor systems

- [ ] Add animation, material, particle, audio, physics, input-map, localization,
  save-game, and prefab inspectors as modules over public engine contracts.
- [ ] Add profiler views for CPU systems, render passes, asset imports, memory,
  and frame timing; export captures for CI and bug reports.
- [ ] Add scene validation, missing-reference reports, shader errors, asset
  diagnostics, project health, and one-click report collection.
- [ ] Add high-DPI, multi-monitor, keyboard-only, controller navigation,
  localization, and accessible editor basics.
- [ ] Add crash recovery, autosave, transaction journal, and deterministic
  restore after interrupted writes.

## Phase 8 - Extensibility and ecosystem

- [ ] Add versioned editor module API and capability-scoped extensions.
- [ ] Add custom component/property schema registration without exposing private
  YorEngine storage.
- [ ] Add package/project templates, samples, documentation links, and a CLI
  for headless project creation/build/import/validation.
- [ ] Add optional source-control integration as an adapter; filesystem and
  project operations must work without a specific provider.
- [ ] Add compatibility policy for YorGL, YorEngine, project schemas, importers,
  and YorStudio extensions.

## Phase 9 - Shipping quality

- [ ] Build YorStudio from a clean checkout with pinned dependencies and no
  hidden developer machine state.
- [ ] Run headless launcher/project/content tests on every change and GUI smoke
  tests on supported desktop platforms.
- [ ] Test missing DLL/backend, device loss, corrupt project/cache, locked file,
  interrupted import, failed compile, plugin crash, window resize, high-DPI,
  sleep/resume, and disk-full recovery.
- [ ] Ship a reference game created only through public YorEngine and
  YorStudio workflows, including standalone packaging.
- [ ] Publish source, license, dependency notices, reproducible binaries, crash
  reporting policy, and migration notes.

## Definition of done

YorStudio is complete for this roadmap when it can launch and manage real YOR
projects, edit scenes and content through public YorEngine contracts, run and
debug play mode, build a standalone game, recover from common failures, and
replace the ImGui adapter without rewriting editor/project logic.
