# YorStudio

YorStudio is the C++ desktop launcher and editor for projects built with the
YOR ecosystem. It will provide a Unity/Unreal-style authoring workflow while
keeping the shipped game independent from the editor.

This repository is intentionally at the headless project-contract stage. It
contains a tested C++20 manifest library and validation CLI, the public project
format, launcher/editor rules, template, roadmap, and native CI. Product code
is added only with a tested vertical slice; an empty ImGui window would not
prove the editor architecture.

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

The first implementation milestone is the tested, headless project/launcher
contract. The desktop executable and ImGui adapter begin after that contract
is stable.

## License

YorStudio is released under the MIT License. See [LICENSE](LICENSE).
