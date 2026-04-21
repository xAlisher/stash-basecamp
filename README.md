# stash-basecamp

Lean decentralised storage for Logos Basecamp — drop a file, get a CID back.

## What it does

- Upload files to IPFS via a local Kubo node (offline-capable) or Pinata
- Download by CID
- Pin uploaded content to Pinata for persistence
- Activity log visible in the sidebar UI

## Structure

```
flake.nix                  ← core module (mkLogosModule)
metadata.json              ← builder config
CMakeLists.txt             ← legacy cmake build (for tests)
src/
  plugin/StashPlugin.cpp   ← Logos plugin entry point
  core/StashBackend.cpp    ← upload/download logic + event log
  core/StorageClient.cpp   ← Kubo HTTP API client
  core/PinningClient.cpp   ← Pinata v3 pinning API client
plugins/stash_ui/
  flake.nix                ← UI plugin (mkLogosQmlModule)
  Main.qml                 ← sidebar UI
third_party/kubo/bin/ipfs  ← Kubo binary (fetch via scripts/fetch-kubo.sh)
```

## Build & install

### Prerequisites

```bash
bash scripts/fetch-kubo.sh   # downloads kubo v0.33.0 → third_party/kubo/bin/ipfs
```

### Nix (recommended)

```bash
# Build + bundle (dual variant — works with both lgpm and Basecamp)
nix bundle --bundler github:logos-co/nix-bundle-lgx#dual \
  .#packages.x86_64-linux.default

# Install via lgpm
lgpm \
  --modules-dir ~/.local/share/Logos/LogosBasecamp/modules \
  --ui-plugins-dir ~/.local/share/Logos/LogosBasecamp/plugins \
  --allow-unsigned \
  install --file ./logos-stash-module-lgx-0.1.0/logos-stash-module.lgx

# Copy kubo binary (until bundled in lgx)
cp third_party/kubo/bin/ipfs ~/.local/share/Logos/LogosBasecamp/modules/stash/ipfs
```

### CMake (dev / tests)

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
cmake --install build --prefix ~/.local/share/Logos/LogosApp
```

## Plugin API

| Method | Description |
|--------|-------------|
| `upload(filePath)` | Upload via Pinata, returns `{"queued":true}` |
| `uploadViaIpfs(filePath)` | Upload via local Kubo node |
| `download(cid, destPath)` | Download CID to local path |
| `setPinningConfig(provider, ...)` | Configure `"pinata"` or `"kubo"` provider |
| `getPinningConfig()` | Current pinning config |
| `getStatus()` | Node status |
| `getLog()` | Activity log (JSON array) |
| `getQuota()` | Storage quota info |

## UI plugin

The `stash_ui` sidebar plugin lives in `plugins/stash_ui/`. Build separately:

```bash
cd plugins/stash_ui
nix bundle --bundler github:logos-co/nix-bundle-lgx#dual \
  .#packages.x86_64-linux.default
```
