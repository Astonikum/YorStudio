# Contributing to YorStudio

YorStudio is the C++ launcher/editor product in the YOR ecosystem. Read the
roadmap and project-format contract before changing structure or manifest
fields.

## Development rules

- C++ is the primary product language; Kotlin/Java/JVM is secondary binding or
  tooling code only.
- Do not add empty modules, fake ImGui windows, or speculative abstractions.
  Every implementation slice needs behavior, tests, and documentation.
- Project discovery never executes C++ code, plugins, scripts, or import hooks.
- Editor models must not include Dear ImGui types. UI goes through
  `StudioUiPort`; ImGui is an adapter.
- YorStudio depends on YorEngine only through a pinned Git tag or immutable
  commit. YorEngine follows the same rule for YorGL.
- Keep runtime output independent from the editor and preserve standalone
  project builds.

## Branches and commits

Use `<version>-<task>` branches, for example `0.1-launcher-contract` or
`0.2-scene-editor`. Keep commits focused and push after each meaningful commit
so CI remains visible. Use imperative commit subjects such as
`project: validate manifest paths`.

Before opening a pull request:

```text
python tools/validate_project.py templates/empty-project
git diff --check
```

Changes to the manifest schema, dependency revisions, public editor ports, or
project layout require migration notes and focused compatibility tests.
