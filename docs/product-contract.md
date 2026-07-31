# YorStudio

YorStudio is the C++ desktop launcher and editor for YOR projects. It is a
separate product above YorEngine and YorGL, designed for Unity-like authoring
workflows while keeping the runtime usable without the editor.

Status: inception. The repository currently contains the contracts and
roadmap, not a fake executable or a placeholder ImGui window. The first
working Studio milestone starts only after the project manifest, launcher
lifecycle, platform window contract, and native render integration are tested.

## Product surfaces

- **Project launcher:** create, open, import, duplicate, migrate, validate,
  recover, and safely remove projects; maintain recent projects and workspace
  roots.
- **Editor shell:** dockable layout, menu/toolbar, play/pause/step controls,
  project settings, diagnostics, logs, and safe mode.
- **Scene editor:** hierarchy, entity creation, component inspector, transform
  editing, selection, multi-select, gizmos, cameras, lights, prefabs, and
  undo/redo.
- **Viewport:** YorEngine RenderSnapshot input, editor overlays, grid, axes,
  selection outlines, camera controls, resize, and frame diagnostics.
- **Content browser:** assets, import status, dependencies, previews, search,
  tags, source/derived separation, and deterministic rebuilds.
- **Build/run tools:** configure C++ project code, import content, build a
  standalone runtime package, launch play mode, capture logs, and report
  failures without making Studio part of the shipped game.

## Target module structure

```text
yorstudio/
  CMakeLists.txt
  app/
    main.cpp
    studio_application.*
  core/
    studio_context.*
    command.*
    command_history.*
    selection.*
    notifications.*
  project/
    project_manifest.*
    project_workspace.*
    project_paths.*
    project_registry.*
    project_validator.*
    project_lock.*
    launcher_model.*
  editor/
    editor_module.*
    hierarchy.*
    inspector.*
    viewport.*
    asset_browser.*
    scene_document.*
    prefab_document.*
    console.*
  content/
    asset_database.*
    importer.*
    dependency_graph.*
    derived_cache.*
    shader_build.*
  platform/
    window.*
    input.*
    file_dialog.*
    clipboard.*
  ui/
    studio_ui_port.*
    imgui/
      imgui_ui_port.*
      imgui_platform.*
      imgui_renderer.*
  tests/
```

This is the target structure for the first implementation slices. It is not a
reason to create empty files now; each module enters the tree with a runnable
behavior and a focused test.

## Project launcher contract

The launcher is the first user-visible feature and the first Studio milestone.
It must:

- discover projects only from explicit workspace roots and recent-project data;
- parse and validate `project.yorproject` schema/version/toolchain constraints;
- show actionable errors for missing folders, unsupported versions, bad paths,
  locked projects, missing modules, and corrupt manifests;
- create the complete directory layout atomically, including `.yor` state;
- open projects without executing C++ code, plugins, scripts, or import hooks;
- support safe mode, reset disposable state, reveal project folder, and remove
  recent entries without deleting project data by accident;
- preserve a project identity GUID across moves, copies, and migrations.

## ImGui replacement boundary

The editor model never calls ImGui directly. Editor modules receive a small UI
port and publish their state through ordinary C++ values. The first adapter
maps the port to Dear ImGui docking/viewports/tables/popups. Platform window and
input code is kept beside the adapter, not in scene or project code.

The replacement test is a headless fake UI port that can render launcher/editor
commands into test events. If replacing ImGui requires changing project,
scene-document, or command-history code, the boundary is wrong.

## Project source and hidden state

User-authored C++ belongs under `code/` and is compiled by the project build.
`.yor/generated/` may contain generated bindings or registration code, but it is
always disposable and must include a generator/schema version. `.yor/cache/`
and `.yor/derived/` are content-addressed or versioned rebuild outputs, never
the source of truth. Editor layout and selection remain local state under
`.yor/editor/` and must not change scene serialization.

## Definition of a usable first Studio

An initial Studio release is not a screenshot or an ImGui demo. It is usable
when a user can create a project, close and reopen it, create a scene/entity,
edit a transform and component, save, reopen, enter play mode, inspect a render
snapshot, undo/redo the edit, build a standalone runtime, and recover from one
deliberately corrupted cache or manifest without losing source content.
