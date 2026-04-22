# Stash

Decentralised backup for [Logos Basecamp](https://github.com/logos-co/logos-basecamp) modules.
Any module can add one-click backup to IPFS / Logos Storage by exposing two C++ methods and dropping in a QML component.

---

## Features

- **Three transports** — Logos Storage (P2P, no account needed), Kubo (local IPFS node), Pinata (hosted pinning)
- **Tested with** [`vpavlin/logos-storage-module`](https://github.com/vpavlin/logos-storage-module) at `9552adf` (`v0.3.2-25-g9552adf`)
- **Activity log** — timestamped entries for every upload with filename → CID
- **StashButton** — drop-in QML component: reads active transport, exports file, uploads, delivers CID back to your module
- **Scheduled backups** — `setWatchedModules` + `checkAll` for background polling
- **Sidebar UI** — transport selector, settings, live log panel

---

## Quick start — add backup to your module

**1. Expose two methods in your C++ plugin:**

```cpp
Q_INVOKABLE QString getFileForStash();
// → {"ok":true,"path":"/path/to/file"}

Q_INVOKABLE QString setBackupCid(const QString& cid, const QString& timestamp);
// → {"ok":true}
```

**2. Copy `integration/StashButton.qml` into your plugin directory.**

**3. Drop the component into your QML:**

```qml
StashButton {
    moduleName: "your-module-name"
}
```

Done. The button handles transport selection, upload, and CID delivery across all transports.

See [`docs/stash-button-integration.md`](docs/stash-button-integration.md) for the full guide.

---

## Install

Download from [Releases](https://github.com/xAlisher/stash-basecamp/releases).

```bash
LGPM=/nix/store/.../lgpm    # path to lgpm binary
MDIR=~/.local/share/Logos/LogosBasecamp/modules

# Core module
$LGPM --modules-dir $MDIR \
      --ui-plugins-dir ${MDIR%modules}ui-plugins \
      --allow-unsigned install --file logos-stash-module.lgx

# UI plugin
$LGPM --modules-dir $MDIR \
      --ui-plugins-dir ${MDIR%modules}ui-plugins \
      --allow-unsigned install --file logos-stash_ui-module.lgx
```

Restart Logos Basecamp. Stash appears in the sidebar.

---

## Build from source

```bash
# Fetch bundled Kubo binary
bash scripts/fetch-kubo.sh

# Build core module + UI plugin (produces LGX files)
nix bundle --bundler github:logos-co/nix-bundle-lgx#dual \
  .#packages.x86_64-linux.default

cd plugins/stash_ui
nix bundle --bundler github:logos-co/nix-bundle-lgx#dual \
  .#packages.x86_64-linux.default
```

---

## Module API

| Method | Returns | Description |
|--------|---------|-------------|
| `upload(filePath, callerModule?)` | `{"queued":true}` | Upload via active transport |
| `getLatestLogosResult()` | `{"file","cid","ts"}` | Last successful Logos Storage result |
| `uploadViaIpfs(filePath)` | `{"cid":"..."}` | Upload via Kubo + pin online |
| `download(cid, destPath)` | `{"queued":true}` | Download CID to path |
| `setActiveTransport(t)` | `{"ok":true}` | Set `"logos"` \| `"kubo"` \| `"pinata"` |
| `getActiveTransport()` | `{"transport":"..."}` | Current active transport |
| `setWatchedModules(names)` | `{"modules":[...]}` | Newline-separated module names |
| `checkAll()` | `{"checked":N,"queued":M}` | Poll watched modules + upload |
| `getStorageInfo()` | `{"ready":bool,...}` | Logos Storage node status |
| `setPinningConfig(...)` | `{"ok":true}` | Configure Kubo/Pinata endpoint + token |
| `getStatus()` | `"ready"\|"starting"\|"offline"` | Overall status |
| `getLog()` | JSON array | Activity log entries |

---

## Repository layout

```
src/plugin/          StashPlugin — Logos plugin entry point + IPC methods
src/core/            StashBackend, StorageClient (Kubo HTTP), PinningClient (Pinata)
plugins/stash_ui/    Sidebar UI — transport selector, settings, activity log
integration/         StashButton.qml + Stash.png — copy into your module
docs/                stash-button-integration.md — integration guide
blog/                Release notes and write-ups
third_party/kubo/    Bundled Kubo binary (fetch via scripts/fetch-kubo.sh)
```

---

## Blog

- [Stash 0.1.0 — Decentralised Backup for Logos Basecamp](blog/2026-04-22-stash-0.1.0.md)
