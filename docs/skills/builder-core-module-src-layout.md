---
id: builder-core-module-src-layout
title: logos-module-builder core module — metadata.json must be at src root
tags: ["builder", "nix"]
phase: setup
type: pitfall
severity: high
created: "2026-04-21"
last_used: "2026-04-21"
status: active
---

## Problem

`logos-cpp-generator` (run during the builder's configure phase) looks for
`metadata.json` in the current working directory, which is `src = ./.;` in
the flake. If `metadata.json` is in a subdirectory, the build fails with:

```
Metadata file does not exist: metadata.json
```

## Rule

`metadata.json` must live in the **same directory** as the `src = ./.;` path
in your flake.nix. For a monorepo where C++ sources are at the repo root,
place `flake.nix` and `metadata.json` at the repo root too.

## Recipe for monorepo (core + UI in one repo)

```
my-module/                  ← repo root
├── flake.nix               ← mkLogosModule, src = ./.;
├── metadata.json           ← core module builder config (has nix section)
├── CMakeLists.txt          ← uses logos_module()
├── src/
│   ├── MyPlugin.cpp
│   └── ...
├── third_party/            ← vendored external libs
└── plugins/
    └── my_ui/
        ├── flake.nix       ← mkLogosQmlModule, src = ./.;
        ├── metadata.json   ← UI builder config
        └── Main.qml
```

**Do NOT** put the core flake in a subdirectory with `src = ../..;` —
that only works if metadata.json is also at `src` root (i.e. repo root).

## CMakeLists.txt pattern

```cmake
cmake_minimum_required(VERSION 3.14)
project(MyPlugin LANGUAGES CXX)

if(DEFINED ENV{LOGOS_MODULE_BUILDER_ROOT})
    include($ENV{LOGOS_MODULE_BUILDER_ROOT}/cmake/LogosModule.cmake)
else()
    message(FATAL_ERROR "LOGOS_MODULE_BUILDER_ROOT not set")
endif()

logos_module(
    NAME my_module
    SOURCES src/MyPlugin.h src/MyPlugin.cpp
)
```
