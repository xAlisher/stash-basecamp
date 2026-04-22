# Stash Button — Integration Guide

Add a one-click backup button to your Logos module that routes files through
the Stash transport (Logos Storage, Kubo, or Pinata) selected by the user.

---

## What you get

A `StashButton` QML component that:

- Matches the style used in **Immutable Notes** (shovel icon + "Stash" label)
- Reads the active transport from the Stash module automatically
- Shows a busy/spinner state while the upload is in progress
- Calls `setBackupCid` on your module when the CID is available (all transports)

---

## Prerequisites

### 1. C++ plugin methods (Q_INVOKABLE)

Your module's C++ plugin must expose two methods:

```cpp
// Export the file you want backed up.
// Stash calls this; return a stable path — typically to an encrypted
// export written into your module's data directory.
Q_INVOKABLE QString getFileForStash();
// Returns JSON: {"ok":true,"path":"/absolute/path/to/file"}
//           or: {"ok":false,"error":"reason"}

// Stash calls this after a successful upload to record the CID.
Q_INVOKABLE QString setBackupCid(const QString& cid, const QString& timestamp);
// Returns JSON: {"ok":true}
```

### 2. Stash module must be running

Add `"stash"` to your module's dependency list in `metadata.json`:

```json
{
  "dependencies": ["stash"]
}
```

---

## Integration steps

### Step 1 — Copy files into your plugin directory

```
your-plugin/
├── Main.qml
├── StashButton.qml   ← copy from stash-basecamp/integration/
└── Stash.png         ← copy from stash-basecamp/integration/
```

### Step 2 — Drop the component into your QML

```qml
// Main.qml (or any screen QML file)
StashButton {
    moduleName: "your-module-name"   // must match your IPC module name
}
```

The `moduleName` must match the name Logos uses to route `callModule` calls to
your plugin — the same string you use in `logos.callModule("your-module-name", ...)`.

### Step 3 — Size and position

`StashButton` has `implicitWidth: 72` and `implicitHeight: 36`. Place it wherever
fits your layout. Notes puts it as a right-side half of the sidebar bottom bar:

```qml
// Notes-style split button bar
Rectangle {
    height: 44
    // left half: New Note button
    // divider
    // right half:
    StashButton {
        id: stashBtn
        anchors { top: parent.top; bottom: parent.bottom; right: parent.right }
        width: 72
        moduleName: "notes"
    }
}
```

---

## Visual customisation

All colours and sizes are exposed as properties:

| Property      | Default     | Description                        |
|---------------|-------------|------------------------------------|
| `iconSource`  | `"Stash.png"` | Path to the shovel icon            |
| `iconSize`    | `16`        | Icon width/height in px            |
| `fontSize`    | `13`        | Label font size                    |
| `colorText`   | `"#FFFFFF"` | Label colour (idle)                |
| `colorMuted`  | `"#5D5D5D"` | Label colour (busy)                |
| `colorHover`  | `"#333333"` | Background on mouse-over           |
| `colorBusy`   | `"#1A1A1A"` | Background while uploading         |

---

## IPC flow

```
StashButton.trigger()
    │
    ├─ logos.callModule(moduleName, "getFileForStash", [])
    │       → {"ok":true, "path":"/tmp/.../backup.bin"}
    │
    ├─ logos.callModule("stash", "getActiveTransport", [])
    │       → {"transport":"logos" | "kubo" | "pinata"}
    │
    ├─ [logos transport]
    │   ├─ logos.callModule("stash", "upload", [path, moduleName])
    │   │       → {"queued":true}
    │   │
    │   └─ StashButton polls stash.getLatestLogosResult() every 2s
    │           → {"file":"...","cid":"...","ts":"..."}
    │           → logos.callModule(moduleName, "setBackupCid", [cid, ts])
    │
    └─ [kubo / pinata transport]
        ├─ logos.callModule("stash", "uploadViaIpfs", [path])
        │       → {"ok":true, "cid":"Qm..."}
        └─ logos.callModule(moduleName, "setBackupCid", [cid, timestamp])
```

### Logos Storage transport — async delivery

When the active transport is **Logos Storage**, the upload is asynchronous.
`StashButton` stays busy and polls `stash.getLatestLogosResult()` every 2 s
until the CID for the queued file appears, then calls `setBackupCid` on your
module and clears busy. Timeout: 2 minutes.

**`moduleName` is required.** If empty, the upload proceeds but `setBackupCid`
is never called (no CID recorded on your module).

> **Why polling instead of a callback?** The Logos QRO framework cannot
> back-call into a module that is currently handling an inbound IPC call
> (`requestObject` times out). Polling `getLatestLogosResult()` from the
> caller's side is the reliable alternative.
> See `ipc-back-call-use-poll-push` in basecamp-skills for details.

---

## Activity log entries

When your module triggers an upload via Logos Storage, Stash automatically
appends these entries to its activity log (visible in the Stash panel):

```
[HH:MM:SS] <moduleName>: <filename>           ← file received from your module
[HH:MM:SS] Logos Storage: uploading <filename> ← upload starting
[HH:MM:SS] Logos Storage: <filename> → <cid>  ← upload complete
```

For example, with `moduleName: "keycard"`:
```
[14:03:11] keycard: keycard-backup-2026-04-22.bin
[14:03:11] Logos Storage: uploading keycard-backup-2026-04-22.bin
[14:03:32] Logos Storage: keycard-backup-2026-04-22.bin → zDvZ...
```

No extra code needed — these entries are logged by Stash as long as
`moduleName` is passed to `upload()`, which `StashButton` does automatically.

---

## State you can read

```qml
StashButton {
    id: stashBtn
    moduleName: "your-module"

    onBusyChanged: {
        if (!busy && lastError !== "")
            console.warn("stash error:", lastError)
    }
}

// elsewhere in your QML:
if (stashBtn.busy) { /* show spinner overlay */ }
```

| Property    | Type     | Description                              |
|-------------|----------|------------------------------------------|
| `busy`      | `bool`   | `true` while upload is in progress       |
| `lastError` | `string` | Last error message, `""` on success      |

---

## Reference implementation

`Immutable Notes` (`logos-notes`) is the canonical consumer. See:

- `plugins/notes_ui/Main.qml` — `doStashBackup()` function (~line 569)
- `src/plugin/NotesPlugin.h` — `getFileForStash` / `setBackupCid` declarations
- `src/core/NotesBackend.cpp` — export logic (writes encrypted `.imnotes` file)
