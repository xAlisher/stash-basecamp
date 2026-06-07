# stash-basecamp

Decentralised file storage for [Logos Basecamp](https://github.com/logos-co/logos-app) modules.

Drop a file, get a CID back. Any module can add one-click backup to IPFS / Logos Storage by exposing two C++ methods and dropping in a QML component.

<img width="1025" height="806" alt="Stash UI" src="https://github.com/user-attachments/assets/8087a99e-4680-4ce1-b5e1-ca0496041ec0" />

---

## What it does

- **Three transports** — Logos Storage (P2P, no account needed), Kubo (local IPFS node), Pinata (hosted pinning)
- **Activity log** — timestamped entries for every upload with filename → CID
- **StashButton** — drop-in QML component: reads active transport, uploads, delivers CID back to your module
- **Scheduled backups** — `setWatchedModules` + `checkAll` for automatic background polling
- **Sidebar UI** — transport selector, settings, live log panel

---

## How it works

```
Your module (C++)          stash-basecamp              storage_module / Kubo / Pinata
  getFileForStash() ─────► StashPlugin                        IPFS network
  setBackupCid(cid) ◄───── upload(filePath, caller)  ──────► upload → CID
                           getLatestLogosResult()             pin CID
                           getLog()             ─────────►  Beacon auto-inscribes
```

1. **Upload** — call `logos.callModule("stash", "upload", [path, callerModule])` from QML
2. **Poll** — call `logos.callModule("stash", "getLatestLogosResult", [])` every 2s until `cid` matches
3. **Deliver** — call back into your module with the CID, or let Beacon auto-inscribe via `getLog()`

---

## Dependencies

| Module | Installed name | Repo | Role |
|--------|---------------|------|------|
| **stash** (this) | `stash` | [stash-basecamp](https://github.com/xAlisher/stash-basecamp) | C++ core plugin |
| **stash-ui** (this) | `stash_ui` (plugin) | [stash-basecamp](https://github.com/xAlisher/stash-basecamp) | QML sidebar UI |
| **storage_module** | `storage_module` | shipped with Basecamp AppImage | Logos Storage (P2P IPFS) backend |
| **Kubo** | bundled binary | fetched via `scripts/fetch-kubo.sh` | local IPFS node (optional transport) |
| **beacon** | `logos_beacon` | [beacon-basecamp](https://github.com/xAlisher/beacon-basecamp) | auto-inscribes uploaded CIDs on-chain (optional) |

### Runtime environment

- [Logos Basecamp](https://github.com/logos-co/logos-app) AppImage — tested against [`vpavlin/logos-storage-module`](https://github.com/vpavlin/logos-storage-module) at `v0.3.2` (`9552adf`)
- Linux x86-64

> **Storage module version matters.** Use `v0.3.2` (`9552adf`) — newer versions on the `vpavlin` fork deadlock `uploadUrl`. See [`storage-module-version-deadlock`](https://github.com/xAlisher/basecamp-skills/blob/master/skills/storage-module-version-deadlock.md) in basecamp-skills.

---

## Build

### Nix (recommended)

Uses [logos-module-builder](https://github.com/logos-co/logos-module-builder).

```bash
git clone https://github.com/xAlisher/stash-basecamp
cd stash-basecamp

# Fetch bundled Kubo binary first
bash scripts/fetch-kubo.sh

# Build portable installable
nix build .#packages.x86_64-linux.install-portable
```

Output at `result/`. Check RPATH:

```bash
patchelf --print-rpath result/modules/stash/stash_plugin.so
# Must show: $ORIGIN/.
```

### CMake (local dev)

Requires Qt 6.9.3 at `~/Qt/6.9.3/gcc_64/` and Logos C++ SDK in the Nix store.

```bash
bash scripts/fetch-kubo.sh   # fetch Kubo binary into third_party/kubo/
cmake -B build
cmake --build build -j$(nproc)
cmake --install build
```

---

## Install

### One-shot install (from Nix build output)

```bash
INSTALL_DIR=~/.local/share/Logos/LogosBasecamp/modules/stash

chmod -R u+w "$INSTALL_DIR" 2>/dev/null; rm -rf "$INSTALL_DIR"
cp -r result/modules/stash/. "$INSTALL_DIR/"
cp metadata.json "$INSTALL_DIR/"

# Strip hashes from manifest
chmod u+w "$INSTALL_DIR/manifest.json"
python3 -c "
import json
with open('$INSTALL_DIR/manifest.json') as f: m = json.load(f)
m.pop('hashes', None)
m['main']['linux-amd64'] = 'stash_plugin.so'
with open('$INSTALL_DIR/manifest.json', 'w') as f: json.dump(m, f, indent=2)"

echo "linux-amd64" > "$INSTALL_DIR/variant"

# Install QML UI plugin
UI_DIR=~/.local/share/Logos/LogosBasecamp/plugins/stash_ui
mkdir -p "$UI_DIR/qml"
cp plugins/stash_ui/qml/Main.qml "$UI_DIR/qml/Main.qml"
cp plugins/stash_ui/manifest.json "$UI_DIR/manifest.json"
cp plugins/stash_ui/metadata.json "$UI_DIR/metadata.json"
cp plugins/stash_ui/variant "$UI_DIR/variant"

rm -rf ~/.cache/Logos/LogosBasecamp/qmlcache
```

### Install via lgpm (from LGX package)

```bash
LGPM=/nix/store/.../lgpm   # path to lgpm binary from AppImage
MDIR=~/.local/share/Logos/LogosBasecamp/modules

$LGPM --modules-dir "$MDIR" \
      --ui-plugins-dir "${MDIR%modules}plugins" \
      --allow-unsigned install --file logos-stash-module.lgx

$LGPM --modules-dir "$MDIR" \
      --ui-plugins-dir "${MDIR%modules}plugins" \
      --allow-unsigned install --file logos-stash_ui-module-lgx-0.1.0
```

### Install paths

```
~/.local/share/Logos/LogosBasecamp/
├── modules/stash/
│   ├── stash_plugin.so
│   ├── kubo                    ← bundled IPFS binary
│   ├── manifest.json
│   ├── metadata.json
│   └── variant
└── plugins/stash_ui/
    ├── qml/Main.qml
    ├── manifest.json
    ├── metadata.json
    └── variant
```

---

## Module API

Call from QML via `logos.callModule("stash", method, args)`:

| Method | Args | Returns | Description |
|--------|------|---------|-------------|
| `upload` | `[filePath, callerModule?]` | `{"queued":true}` | Upload via active transport |
| `getLatestLogosResult` | `[]` | `{"file","cid","ts"}` | Last successful Logos Storage result |
| `uploadViaIpfs` | `[filePath]` | `{"cid":"..."}` | Upload via Kubo + pin online |
| `download` | `[cid, destPath]` | `{"queued":true}` | Download CID to path |
| `setActiveTransport` | `[transport]` | `{"ok":true}` | Set `"logos"` \| `"kubo"` \| `"pinata"` |
| `getActiveTransport` | `[]` | `{"transport":"..."}` | Current active transport |
| `setWatchedModules` | `[names]` | `{"modules":[...]}` | Newline-separated module names to poll |
| `checkAll` | `[]` | `{"checked":N,"queued":M}` | Poll watched modules and upload changed files |
| `getStorageInfo` | `[]` | `{"ready":bool,...}` | Logos Storage node status |
| `setPinningConfig` | `[provider, endpoint, token]` | `{"ok":true}` | Configure Kubo/Pinata endpoint + token |
| `getStatus` | `[]` | `"ready"\|"starting"\|"offline"` | Overall status |
| `getLog` | `[]` | JSON array | Activity log entries (all uploads) |

### getLog entry format

```json
{
  "ts": "1748300000",
  "file": "notes-export.enc",
  "cid": "bafyreib...",
  "transport": "logos",
  "caller": "logos_notes"
}
```

---

## Quick start — add backup to your module

**1. Expose two methods in your C++ plugin:**

```cpp
Q_INVOKABLE QString getFileForStash();
// Returns: {"ok":true,"path":"/absolute/path/to/file"}
//      or: {"ok":false,"error":"reason"}

Q_INVOKABLE QString setBackupCid(const QString& cid, const QString& timestamp);
// Returns: {"ok":true}
```

**2. Add `"stash"` to your `metadata.json` dependencies:**

```json
{ "dependencies": ["stash"] }
```

**3. Copy `integration/StashButton.qml` and `integration/Stash.png` into your plugin directory.**

**4. Drop the component into your QML:**

```qml
StashButton {
    moduleName: "your_module_name"
}
```

Done. The button handles transport selection, upload, and CID delivery across all transports.

See [`docs/stash-button-integration.md`](docs/stash-button-integration.md) for the full guide.

---

## Polling pattern (QML-mediated upload)

For modules that need the CID back in C++ without a direct back-call (e.g. keeper-basecamp):

```qml
// 1. Trigger upload
logos.callModule("stash", "upload", [localPath, "my_module"])

// 2. Poll for result every 2s
Timer {
    interval: 2000; repeat: true
    onTriggered: {
        var res = callModuleParse(logos.callModule("stash", "getLatestLogosResult", []))
        if (res && res.cid && res.file === pendingFileName) {
            // 3. Deliver CID back to C++
            logos.callModule("my_module", "onUploadResult", [id, res.file, res.cid])
            stop()
        }
    }
}
```

Match by `res.file === pendingFileName` — not by CID — to handle stale results from previous sessions.

---

## Tests

### Unit tests (Qt Test, no network)

```bash
cmake -B build && cmake --build build -j$(nproc)
cd build && ctest --output-on-failure
```

Covers: upload queueing, Kubo HTTP mock, pinning config, watched modules, `getLog` format.

### Headless smoke test (no AppImage required)

```bash
bash tests/headless_test.sh

# Custom logoscore path:
LOGOSCORE=/path/to/logoscore bash tests/headless_test.sh
```

Patches out `storage_module` dependency so logoscore loads stash in isolation. Tests offline behaviour only.

### Live upload test

```bash
bash tests/test_logos_upload.sh
```

Requires Basecamp running with `storage_module` loaded. Uploads a real file and asserts a CID is returned.

---

## Persistence

All state lives under `QStandardPaths::AppDataLocation`:

```
~/.local/share/stash/           (or platform equivalent)
├── stash-activity-log.json     ← timestamped upload history
└── stash-settings.json         ← active transport, watched modules
```

QSettings under `logos/stash` namespace stores:
- `watchedModules` — newline-separated list of module names
- `pinningProvider` / `pinningEndpoint` / `pinningToken` — Kubo/Pinata config
- `activeTransport` — `"logos"` | `"kubo"` | `"pinata"`

---

## Repository layout

```
stash-basecamp/
├── src/
│   ├── plugin/         StashPlugin — Q_INVOKABLE entry points
│   └── core/           StashBackend, StorageClient (Logos), PinningClient (Kubo/Pinata)
├── plugins/stash_ui/   Sidebar UI — transport selector, settings, activity log
├── modules/stash/      Installed module files (manifest, metadata, variant)
├── integration/        StashButton.qml + Stash.png — copy into your module
├── docs/               stash-button-integration.md — full integration guide
├── tests/              Unit tests + headless smoke test
├── scripts/            fetch-kubo.sh
├── third_party/kubo/   Bundled Kubo binary (after fetch-kubo.sh)
└── blog/               Release notes
```

---

## Status

`v0.1.0` — working on Linux x86-64 with Logos Basecamp. Confirmed integrations: logos-notes, keeper-basecamp.

Known limitations:
- Storage module version pinned to `v0.3.2` (`9552adf`) — newer vpavlin fork deadlocks `uploadUrl`
- No upload retry on transient storage_module errors
- Kubo transport requires manual `kubo daemon` or the bundled binary (started automatically)
