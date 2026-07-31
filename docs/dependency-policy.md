# Dependency policy

The three YOR repositories are independent release units:

```text
YorStudio --Git tag/SHA--> YorEngine --Git tag/SHA--> YorGL
```

Each repository builds and tests from a clean checkout. A dependency is added
only when a public contract is consumed, not to reserve a future integration
point. YorStudio's first native contract dependency is `nlohmann/json`, fetched
by CMake at the immutable commit
`9cca280a4d0ccf0c08f47a99aa71d1b0e52f8d03` (the v3.11.3 tag). The revision is
declared in the root `CMakeLists.txt` and is tested by native contract CI.

When a dependency is introduced:

1. use the canonical HTTPS repository URL;
2. pin a release tag or immutable commit in the build configuration and lock
   file;
3. test the pinned revision in CI on every supported platform;
4. update the dependency in a dedicated branch with release notes;
5. never use a moving `main` branch for a release or reproducible build.

C++ remains authoritative in every product. JVM code, when present, is a
secondary binding or tooling integration and cannot become a hidden runtime
dependency.
