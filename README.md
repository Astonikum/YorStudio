# YorStudio

YorStudio is the C++ desktop launcher and editor for projects built with the
YOR ecosystem. It will provide a Unity/Unreal-style authoring workflow while
keeping the shipped game independent from the editor.

The first tested C++20 desktop slice is now available on Windows: a native
Win32/DX11 window with a dockable Dear ImGui launcher can create, reopen, and
close real YOR projects through the same manifest, recent-registry, and lock
lifecycle used by headless CI. Opening a project now exposes a real YorEngine
scene document with hierarchy selection, object creation, inspector edits,
delete/duplicate/reparent, undo/redo, active-state editing, and versioned scene
save/load.
The editor model does not depend on ImGui, so the adapter can be replaced later.

## YOR repositories

- [YorGL](https://github.com/Astonikum/YorGL) — low-level C++ rendering API
  and backend implementations.
- [YorEngine](https://github.com/Astonikum/YorEngine) — C++ runtime, scenes,
  entities, components, assets, and rendering integration.
- [YorStudio](https://github.com/Astonikum/YorStudio) — this C++ launcher and
  editor.

The dependency direction is `YorStudio -> YorEngine -> YorGL`. Cross-repository
dependencies are Git dependencies pinned to a release tag or immutable commit;
no product consumes another repository's moving `main` branch.

## Project format

Every game is a real project directory with a versioned `project.yorproject`
manifest. The editor creates and validates the full layout, including hidden
`.yor/` state. See [docs/project-format.md](docs/project-format.md) and the
[empty project template](templates/empty-project/project.yorproject).

## Language and UI policy

C++ is the source of truth. Kotlin/Java/JVM integrations are secondary and may
only wrap a tested native contract. Dear ImGui is the first UI adapter, not the
editor model: project, scene, command, and content code must depend on a small
`StudioUiPort`, so a future native or Qt adapter can replace ImGui.

## Current verification

```powershell
cmake -S . -B build-native
cmake --build build-native --config Release --parallel
ctest --test-dir build-native -C Release --output-on-failure
build-native\Release\yorstudio_project_validate.exe templates/empty-project
```

On Windows, the same build produces `build-native\Release\yorstudio.exe`.
Run it with no arguments to choose a project in the native folder dialog, or
pass a project directory directly:

```powershell
build-native\Release\yorstudio.exe templates\empty-project
```

Linux CI continues to build the portable headless contracts; the first desktop
backend is intentionally Windows/DX11 and is isolated behind the UI port.

## License

YorStudio is released under the MIT License. See [LICENSE](LICENSE).
