## Summary

<!-- What behavior or contract does this change add? -->

## Verification

- [ ] `cmake -S . -B build-native && cmake --build build-native --config Release --parallel`
- [ ] `ctest --test-dir build-native -C Release --output-on-failure`
- [ ] `build-native/yorstudio_project_validate templates/empty-project`
- [ ] `git diff --check`
- [ ] C++ is the authoritative implementation language for product code.
- [ ] No project code, plugin, or import hook executes during project discovery.
- [ ] Any cross-repository dependency is pinned to a tag or immutable commit.

## Compatibility

<!-- Describe manifest, YorEngine, YorGL, or editor API compatibility impact. -->
