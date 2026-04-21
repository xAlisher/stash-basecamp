---
id: builder-lgx-install-recipe
title: logos-module-builder — full lgx build + install recipe for dev testing
tags: ["builder", "nix", "lgpm"]
phase: setup
type: technique
severity: high
created: "2026-04-21"
last_used: "2026-04-21"
status: active
---

## Verified recipe (stash core module)

### 1. Build the lgx (dual variant — required for Basecamp + lgpm compatibility)

```bash
nix build                             # build the .so (optional separate step)
nix bundle --bundler github:logos-co/nix-bundle-lgx#dual \
  .#packages.x86_64-linux.default    # produces logos-<name>-module-lgx-0.1.0/
```

Use `#dual` not `#portable` — the standalone lgpm CLI expects `linux-amd64-dev`
and Basecamp's loader expects `linux-amd64`. Dual gives both.

| Bundler | Variant in lgx | Works with lgpm | Works with Basecamp |
|---------|---------------|-----------------|---------------------|
| `#portable` | `linux-amd64` | ✗ (needs `-dev`) | ✓ |
| `#default` | `linux-amd64-dev` | ✓ | ✗ (needs `linux-amd64`) |
| `#dual` | both | ✓ | ✓ |

### 2. Install via lgpm

```bash
lgpm=/nix/store/l2kcbdg9hn7lb053lx111smrvi88jl38-logos-package-manager-cli-1.0.0/bin/lgpm

$lgpm \
  --modules-dir ~/.local/share/Logos/LogosBasecamp/modules \
  --ui-plugins-dir ~/.local/share/Logos/LogosBasecamp/plugins \
  --allow-unsigned \
  install --file ./logos-<name>-module-lgx-0.1.0/logos-<name>-module.lgx
```

lgpm extracts `linux-amd64-dev` variant (preferred), writes manifest with full
Merkle hashes for both variants, creates `variant` file = `linux-amd64-dev`.

### 3. Result layout in modules dir

```
modules/<name>/
  manifest.json   ← includes hashes for both variants
  <name>_plugin.so
  variant         ← "linux-amd64-dev"
```

Basecamp reads `main["linux-amd64"]` from manifest → resolves to `<name>_plugin.so` at root.

### 4. Restart Basecamp

Kill all instances (`kill -9 $(pgrep -f "LogosBasecamp\.elf")`), relaunch once.
Module appears in sidebar and loads on click.

## Runtime dep: kubo ipfs binary

`bundledIpfsPath()` in `StashPlugin.cpp` uses `dladdr` to find the `.so`'s own
directory and looks for an `ipfs` binary next to it.

lgpm does NOT bundle `ipfs` — it only installs what's in the lgx. After every
lgpm install, manually copy the binary:

```bash
cp third_party/kubo/bin/ipfs ~/.local/share/Logos/LogosBasecamp/modules/stash/ipfs
```

**TODO**: add kubo to the nix build so the lgx bundles `ipfs` automatically
(via `extra_sources` or a post-install hook in the builder).

---

## Why logos-storage-nim EXTERNAL_LIBS was a red herring

The EXTERNAL_LIBS issue was from a stale experiment. Stash uses HTTP (Kubo),
not the Nim library. `LibStorageTransport.cpp` is dead code —
`StashPlugin.cpp` comment: "No embedded transport — libstorage.a removed to fix Nim runtime conflict".
Strip all logos_storage_nim references before attempting builder migration.
