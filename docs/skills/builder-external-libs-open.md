---
id: builder-external-libs-open
title: logos-module-builder EXTERNAL_LIBS — staging works but CMake path unknown
tags: ["builder", "nix"]
phase: setup
type: open-question
severity: high
created: "2026-04-21"
last_used: "2026-04-21"
status: active
---

## Status: UNRESOLVED

The builder stages vendor libraries correctly (confirmed in build log) but
`logos_module(EXTERNAL_LIBS logos_storage_nim)` still fails with:

```
External library logos_storage_nim not found in CMakeLists.txt
```

And the C++ compile then fails with `libstorage.h: No such file or directory`.

## What we know

- `"external_libraries": [{ "name": "logos_storage_nim", "vendor_path": "third_party/logos-storage-nim" }]`
  in metadata.json → builder log confirms: "Staging vendor library logos_storage_nim from third_party/logos-storage-nim..."
- The staged library is available during the build but we don't know the CMake variable name.
- `${LOGOS_STORAGE_NIM_LIB}` in `extra_link_libraries` is NOT correct (variable not set).

## What to investigate next

1. Fetch `cmake/LogosModule.cmake` from `github:logos-co/logos-module-builder` to read the
   EXTERNAL_LIBS implementation and find the exact variable names it sets.
2. Check if the builder sets `LOGOS_<UPPERCASED_NAME>_DIR` or `LOGOS_<NAME>_VENDOR_DIR` etc.
3. Check the `external-lib-module` template's CMakeLists.txt for the correct linking pattern.
4. Ask dlipicar/helium — the builder team.

## Likely fix

The `logos_module()` macro probably auto-handles include + link when EXTERNAL_LIBS is set,
and you should NOT add manual paths in `extra_link_libraries`. The extra_link_libraries
entry for the vendored libs should probably be empty or use a builder-defined variable.

## Workaround (if builder EXTERNAL_LIBS can't handle multiple .a files)

Explicitly set library path via a known env variable and link manually:

```cmake
# The builder likely sets something like:
# LOGOS_STORAGE_NIM_DIR or LOGOS_EXTERNAL_LOGOS_STORAGE_NIM_DIR
# Check with: message(STATUS "vars: ${CMAKE_CACHE_VARIABLES}")
```
