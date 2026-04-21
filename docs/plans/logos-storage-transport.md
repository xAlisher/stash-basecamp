# Plan: Logos storage_module IPC Transport

**Issue:** #12
**Branch:** `feature/logos-storage-transport`
**Date:** 2026-04-21

---

## Goal

Wire `storage_module` typed-SDK IPC as a real storage transport option in stash,
replacing the existing stub `LogosStorageTransport` (declared, never implemented).

---

## Assumptions Register

| # | Assumption | Risk if wrong |
|---|-----------|--------------|
| A1 | `logos-module-builder`'s `logos_module()` already applies `--whole-archive` to `liblogos_sdk.a` | Token delivery broken; 20s IPC timeout on every call. Verify post-build with `nm libyolo_board_module.so \| grep -i ModuleProxy` |
| A2 | The AppImage's `storage_module` does NOT detach `start()` to a thread (no fork) | `initStorage` blocks main thread ~30 s; handled by retry-until-ready pattern |
| A3 | `storageUploadDone` payload shape is `[bool ok, sessionId, cid]` (3 elements) | Upload completion dispatch fails; fall back to first-pending heuristic |
| A4 | `storage_module_api.h` and `.cpp` are available at `$LOGOS_STORAGE_ROOT/include/` from nix store | Build fails; need to find correct path |
| A5 | `StashBackend` upload/download can be redirected to Logos transport without API changes | May need a new method or setter on StashBackend |

---

## Execution Order

### Phase 1 — Build wiring (no C++ code changes yet; verify it compiles)

1. **`flake.nix`** — add `logos-storage-module` input, wire `LOGOS_STORAGE_ROOT` via cmakeFlags
2. **`CMakeLists.txt`** (builder guard block) — conditionally add `storage_module_api.cpp` + include dir
3. **`metadata.json`** — add `"storage_module"` to `dependencies[]`
4. `nix build` dry-run; confirm it finds the storage headers

### Phase 2 — LogosStorageTransport implementation

5. **`src/core/StorageClient.h`** — update `LogosStorageTransport` constructor: `LogosAPIClient*` → `LogosAPI*`
6. **`src/core/LogosStorageTransport.cpp`** (new file) — full typed-SDK implementation:
   - Ctor: `StorageModule* m_storage = new StorageModule(api)`
   - `isConnected()`: `m_storage != nullptr` (connection is always assumed once constructed)
   - `uploadUrl()`: call `m_storage->uploadUrl(QVariant::fromValue(url), chunkSize)` — sync, returns sessionId via `LogosResult.value`; store session for event matching
   - `downloadToUrl()`: call `m_storage->downloadToUrl(cid, destPath)`
   - `subscribeEventResponse()`: call `m_storage->on("storageUploadDone", ...)` + `m_storage->on("storageDownloadDone", ...)`; forward to unified `EventCallback`
7. Add `src/core/LogosStorageTransport.cpp` to CMakeLists.txt sources (builder guard block)

### Phase 3 — Plugin wiring

8. **`StashPlugin.h`** — add:
   - `StorageModule* m_logosStorage = nullptr`
   - `bool m_logosStorageReady = false`
   - `Q_INVOKABLE QString getStorageInfo()`
   - `Q_INVOKABLE QString setActiveTransport(const QString& provider)`
9. **`StashPlugin.cpp`** — in `initLogos`:
   - Construct `LogosStorageTransport(api)` (deferred: don't block initLogos)
   - Subscribe storage events; flip `m_logosStorageReady` on `storageStart`
   - Store as active transport if "logos" selected in settings
10. Wire `m_backend.setTransport(...)` or add a `getLogosClient()` accessor so upload/download methods use it

### Phase 4 — init / start sequence

11. Add `initStorage(const QString& dataDir)` private method (parallel to yolo-board pattern):
    - Call `m_storage->init(cfgJson)` synchronously
    - Call `m_storage->start()` — may block ~30 s without the fork; run via `QTimer::singleShot(0, ...)`
    - Guard with `m_storageStarting` flag
12. Add retry pattern in `upload()` for when `m_logosStorageReady` is still false (same as `startUploadWhenReady` in yolo-board)

### Phase 5 — Settings / transport selection

13. Persist active transport choice in `QSettings` (`"activeTransport"` key)
14. `setActiveTransport("logos"|"kubo"|"pinata")` sets backend transport + saves
15. `getStorageInfo()` returns JSON: `{ready, peerId, spr, addrs}`

### Phase 6 — QML UI

16. Add "Logos Storage" radio option in `plugins/stash_ui/Main.qml` provider section
17. Show `storageReady` status badge + peer ID / SPR in settings panel
18. Wire `setActiveTransport` call from QML

### Phase 7 — Tests + verify

19. Update/add unit tests for `LogosStorageTransport` with new constructor signature
20. `nix build` — full build; verify `.so` contains `ModuleProxy` symbols
21. Manual smoke test: install to `LogosBasecampDev`, verify storage tab appears, upload a file

---

## Files to Create / Modify

| File | Change |
|------|--------|
| `flake.nix` | Add `logos-storage-module` input, `cmakeFlags += -DLOGOS_STORAGE_ROOT` |
| `metadata.json` | Add `"storage_module"` to `dependencies[]` |
| `CMakeLists.txt` | Guard block: add storage API source + include + `LogosStorageTransport.cpp` |
| `src/core/StorageClient.h` | Update `LogosStorageTransport` ctor signature |
| `src/core/LogosStorageTransport.cpp` | **New** — typed SDK implementation |
| `src/plugin/StashPlugin.h` | Add `m_logosStorage`, `getStorageInfo`, `setActiveTransport` |
| `src/plugin/StashPlugin.cpp` | initLogos wiring, initStorage, upload retry |
| `plugins/stash_ui/Main.qml` | Add Logos Storage option |

---

## Design Decisions

**D1: Keep `StorageTransport` interface unchanged**
The existing virtual interface (`uploadUrl`, `downloadToUrl`, `subscribeEventResponse`)
maps cleanly to typed SDK calls. No need to redesign the abstraction.

**D2: Constructor takes `LogosAPI*`, not `StorageModule*`**
Consistent with how logos-irc-module and yolo-board-module construct their typed wrappers.
`StorageModule` is an implementation detail internal to `LogosStorageTransport`.

**D3: Logos Storage is opt-in alongside existing transports**
Default transport stays Kubo. User selects via `setActiveTransport`. Avoids breaking
existing users who have Kubo set up.

**D4: No fork of storage_module for now**
Accept the ~30 s blocking start(). Mitigate with:
- `startUploadWhenReady` retry loop (same as yolo-board)
- Status indicator in UI ("Starting storage…")
- If too painful, revisit; fork is the nuclear option.

---

## Risk Flags

- **`--whole-archive`**: Must be verified post-build (assumption A1). If missing, all
  storage IPC silently fails at 20 s. Check: `nm libstash_plugin.so | grep ModuleProxy`.
- **Binary alignment**: If the storage_module installed in the AppImage was built from a
  different revision than our flake input, typed calls return empty silently.
  Fix: `nm -D storage_module_plugin.so | grep uploadUrl`.
- **`LogosResult` serialization**: Only use typed SDK. Never `invokeRemoteMethod` for
  storage calls.

---

## Success Criteria

- [ ] `nix build` succeeds
- [ ] `nm libstash_plugin.so | grep ModuleProxy` — non-empty
- [ ] Stash loads in Basecamp without crash
- [ ] `setActiveTransport("logos")` persists across restarts
- [ ] File upload via `uploadViaLogos(filePath)` returns a CID
- [ ] `getStorageInfo()` returns non-empty `peerId` once storage is ready
- [ ] Existing Kubo upload path still works
