---
id: builder-ui-qml-adoption
title: Adopt logos-module-builder for a ui_qml plugin — what works
tags: ["builder", "nix"]
phase: setup
type: technique
severity: medium
created: "2026-04-21"
last_used: "2026-04-21"
status: active
---

## What works

`mkLogosQmlModule` builds a ui_qml plugin cleanly in ~9 lines of flake.nix.
Tested on stash_ui (stash-basecamp/plugins/stash_ui/).

## Recipe

### flake.nix (place in the plugin subdirectory)

```nix
{
  description = "My UI plugin";
  inputs.logos-module-builder.url = "github:logos-co/logos-module-builder";
  outputs = inputs@{ logos-module-builder, ... }:
    logos-module-builder.lib.mkLogosQmlModule {
      src = ./.;
      configFile = ./metadata.json;
      flakeInputs = inputs;
    };
}
```

### metadata.json (builder format — replaces both manifest.json and metadata.json)

```json
{
  "name": "my_ui",
  "version": "0.1.0",
  "type": "ui_qml",
  "category": "...",
  "description": "...",
  "view": "Main.qml",
  "icon": "icons/icon.png",
  "dependencies": ["my_core_module"],
  "nix": {
    "packages": { "build": [], "runtime": [] },
    "external_libraries": [],
    "cmake": { "find_packages": [], "extra_sources": [], "extra_include_dirs": [], "extra_link_libraries": [] }
  }
}
```

No CMakeLists.txt needed — pure QML, builder handles everything.

### Build

```bash
# All files must be git-tracked (nix reads from git tree)
git add flake.nix metadata.json icons/
nix build                                              # produces result/lib/{Main.qml, metadata.json, icons/}
nix bundle --bundler github:logos-co/nix-bundle-lgx#portable .#packages.x86_64-linux.default
# produces logos-<name>-module-lgx-<version>/ in nix store with content hashes ✓
```

## Gotchas

- All files referenced by metadata.json `icon` path must be git-tracked and exist
  inside the flake directory — builder errors if `icons/foo.png` is untracked.
- `nix bundle ... .` (no explicit attr) fails for QML modules — the default output
  is an `app`, not a derivation. Use `.#packages.x86_64-linux.default` explicitly.
- The `nix` section in metadata.json is required even if empty — omit it and the
  builder will error during evaluation.
- Old `manifest.json` fields like `pluginType`, `capabilities`, `author` are NOT
  needed and can be dropped — builder generates a clean manifest.json in the lgx.
