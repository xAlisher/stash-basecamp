# Why Every Attempt to Use Logos Storage Has Failed

> Written 2026-04-16. Cold retrospective across all attempts in logos-notes and stash-basecamp.
> Not a plan. Not optimistic. Just what happened and why.

---

## The Short Version

There have been three distinct attempts to use Logos Storage from a third-party module.
Every one hit a different wall. The walls are not bugs — they are structural properties of
the platform. Without an upstream change or a protocol-level workaround, storage integration
from a third-party core module is not possible.

---

## Attempt 1: Logos IPC (March 2026)

**Plan:** Use `LogosAPIClient::invokeRemoteMethod("storage_module", "uploadUrl", ...)` from
the notes plugin. The SDK is designed for exactly this. Implemented `StorageClient` with an
abstract `StorageTransport` interface. Wired to `LogosStorageTransport`. All unit tests pass.

**What happened:** Every call blocks for ~40 seconds and returns an invalid `QVariant`.
Nothing reaches `storage_module`. No error is logged anywhere — not in the plugin, not in
the host, not in Basecamp. The call simply times out and dies silently.

**Root cause (confirmed — issue #77):**

Every `invokeRemoteMethod` call is gated by a capability token system. The flow is:

```
plugin calls invokeRemoteMethod("storage_module", ...)
    → SDK checks: do we have a token for "storage_module"?
    → No → request token from capability_module
    → capability_module.requestModule("notes", "storage_module")
    → capability_module checks: is "notes" authorized?
    → No entry → request denied
    → Token never issued
    → Call waits ~40s for a token that never arrives
    → Returns invalid QVariant
```

The bootstrap problem: to request a capability token, you call `capability_module`.
But calling `capability_module` also requires a token. The bootstrap token is only
pre-provisioned for `main_ui` and modules bundled inside the AppImage itself.
Third-party core plugins like `notes` never receive one.

**What was ruled out:**

- Adding `storage_module` to the manifest `dependencies[]` field — has no effect on token
  provisioning. It affects load order only.
- Calling `capability_module.requestModule()` directly from `initLogos()` — causes a
  ~40s deadlock (same problem, different call site).
- Using `logosAPI->getProvider()->requestObject("storage_module")` — returns NULL after
  timeout. Same token check.

**What actually works:**

The Storage UI (compiled into `main_ui`, which IS bundled in the AppImage) can call
`storage_module.uploadUrl` and it succeeds. We confirmed this by manually uploading a
backup file through the storage UI. The IPC path is functional — just not accessible to
us.

**Verified ghost:** During testing, a CID appeared in the Storage UI manifests list that
looked like our upload had worked. It was a manual upload Alisher had done earlier of an
old backup file from `~/Downloads/`. None of our programmatic `uploadUrl` calls ever
reached `storage_module`. Confirmed by file inspection.

**Status:** Permanently blocked unless upstream provisions the capability token for
third-party plugins, or exposes a different bootstrapping mechanism.

---

## Attempt 2: Embed the Storage Node Directly (April 2026)

**Plan:** Skip Logos IPC entirely. Statically link `libstorage.a` (the Nim storage node)
directly into the plugin. No capability tokens. No IPC. Just a C FFI call to initialize
a local storage node in-process.

Implemented `LibStorageTransport`:
- Calls `libstorageNimMain()` to initialize the Nim runtime
- Manages a `void* m_ctx` storage node context
- Upload/download via C callbacks into the Nim runtime
- Qt signals to marshal Nim thread callbacks to Qt main thread

Unit tests pass. The `libstorageNimMain()` initialization succeeds in isolation.

**What happened:** The plugin loads and tests pass in the `logos-notes` repo (where it
was first developed). But when the same approach was applied to `stash-basecamp` and
installed into the AppImage, the stash module silently failed to appear. No error in log.
Module simply not visible in sidebar.

**Root cause:**

The Logos Basecamp AppImage already ships `storage_module_plugin.so`, which dynamically
loads `libstorage.so` (the Nim storage node, same underlying code). When `stash_plugin.so`
also loads (via the static `libstorage.a`), there are now two copies of the Nim runtime
in the same process:

```
logos_host process:
  → loads storage_module_plugin.so
      → dlopen libstorage.so
          → Nim runtime init #1 (libstorageNimMain via global ctor)
  → loads stash_plugin.so
      → libstorageNimMain() called explicitly in LibStorageTransport::start()
          → Nim runtime init #2
          → conflict / crash / undefined behavior
  → stash module never appears
```

The Nim runtime has global state and is not designed to be initialized twice in the
same process. The AppImage's storage_module already owns it.

**What this means for notes too:**

The notes plugin used `LibStorageTransport` in `feature/stash-integration`. In theory
it works as long as `storage_module_plugin.so` is NOT loaded in the same `logos_host`
process as `notes_plugin.so`. In practice, modules run in separate `logos_host` child
processes, so notes and storage_module don't share a process. The notes case might
actually work. But stash does share the process when storage_module is present.

**Status:** Permanently blocked for the stash architecture. Possible for notes in a
separate logos_host process, but untested at runtime (only tested headlessly with mocks).

---

## Attempt 3: Stash as Orchestrator, Talking to storage_module via IPC (April 2026)

**Plan:** Stash module sits between notes and storage_module. Notes calls stash via the
stash protocol (`getFileForStash` / `setBackupCid`). Stash calls storage_module via IPC.
Stash is a "proper" Logos module that the platform knows about, so maybe it gets tokens.

This plan hit both previous walls simultaneously:

- **Wall 1 (IPC tokens):** Stash is also a third-party core plugin. It has exactly the
  same capability token situation as notes. `logosAPI->getClient("storage_module")` from
  inside stash_plugin.so would hit the same 40s timeout.

- **Wall 2 (Nim runtime):** To avoid IPC, stash embedded `libstorage.a`. But now stash
  coexists with storage_module in the AppImage and hits the two-Nim-runtime problem.

The orchestrator idea is architecturally sound — it just can't be implemented on the
current platform without an upstream fix.

**Status:** Blocked by the same two walls as Attempts 1 and 2.

---

## What Was Never Tried

### REST HTTP

The Logos Storage node exposes a REST API on port 8080:

```
POST /api/storage/v1/data          → upload file → CID
GET  /api/storage/v1/data/{cid}    → download
GET  /api/storage/v1/debug/info    → node status
```

`QNetworkAccessManager` could call these directly. No IPC. No Nim runtime conflict.
No capability tokens. The port is local (127.0.0.1).

**Why it was not tried:**
- Smells like HTTP. User flagged this as a concern early in the stash design session.
  "could we remove http? smells bad." (direct quote, session 2026-04-16)
- Adds a runtime dependency on the storage node having started and bound to port 8080
  before the plugin's first upload attempt
- Requires polling or retry logic to detect when the node is ready
- Port conflicts if the user has something else on 8080

**Whether it would work:** Almost certainly yes. The storage UI (which does work) calls
the storage node through IPC, but the underlying Nim node responds to HTTP. The HTTP API
is documented and tested.

### QML Routing (Issue #78)

Route storage calls through QML, which runs in the `main_ui` context (which has tokens):

```
saveNote() → notes plugin → signal → QML
QML → logos.callModule("storage_module", "uploadUrl", [...])
CID returned → QML → logos.callModule("notes", "setBackupCid", [cid, ts])
```

**Why it was not tried:** Architectural refactor. The storage upload currently lives in
`NotesBackend` (C++). Moving it to QML means the backup logic splits across C++ and QML,
the CID callback has to cross the QML bridge twice, and the flow becomes harder to test.
Also adds latency (every save bounces through QML).

**Whether it would work:** Almost certainly yes. `main_ui` has capability tokens. The
storage UI proves this works. It's just messy.

---

## The Structural Diagnosis

Three attempts, three different failure modes, all caused by the same underlying gap:

**The Logos platform capability token system was designed for first-party bundled modules.
Third-party developer modules were not provisioned in the current AppImage builds.**

The evidence:
1. Everything works from `main_ui` (bundled, token-provisioned)
2. Nothing works from `logos_host` child processes (third-party, no token)
3. The `capability_module` exists and is documented but there is no documented path for
   a third-party plugin to bootstrap its own token
4. Issues #141 and #142 filed upstream — no response as of 2026-04-16

The Nim runtime conflict is a secondary issue that only arose because we tried to
work around the token gap by embedding the storage node directly.

---

## What Would Actually Fix It

**Option A — Upstream fix (out of our control)**
Logos team provisions capability tokens for third-party core plugins, or documents
the bootstrap path. One change on their side, everything works.

**Option B — REST HTTP (self-contained, pragmatic)**
Use `QNetworkAccessManager` to call Logos Storage on port 8080 directly. Avoids IPC,
tokens, Nim runtime entirely. The dirty feeling is real but the risk is low — it's
localhost, it's the same node we'd be calling through IPC anyway.

**Option C — QML routing (works, architecturally messy)**
Move backup trigger into QML where `main_ui` tokens are available. Works but splits
the backup flow across C++ and QML layers, harder to test and reason about.

**Option D — Accept local-only backup for now**
The export-to-file flow works. The import-from-file flow works. Users can manually
copy `.imnotes` backup files. Not ideal but functional. Wait for upstream to fix
tokens before investing more in storage integration.

---

## Current Code State

All the code is correct at the unit level. `StorageClient`, `LibStorageTransport`,
`StashPlugin`, `StashBackend`, and the stash protocol bridge (`getFileForStash`,
`setBackupCid`) are all implemented and tested with mocks. The only thing that doesn't
work is the last mile: getting bytes from our process to the Logos Storage node.

Nothing needs to be deleted. If upstream fixes the token gap, Attempt 1 (IPC path) would
work with no code changes on our side. If we go REST, `LibStorageTransport` gets swapped
for an `HttpStorageTransport` that implements the same `StorageTransport` interface.

The abstraction held. The platform didn't.
