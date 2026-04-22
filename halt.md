# Halt — 2026-04-22 (session 3)

## ✅ RESOLVED: uploadViaLogos works end-to-end

CID confirmed: `zDvZRwzm7pWh2HfUMS44dXe4TLkzyMzBBG4DGoki7Mrpd3MobS3n`

Full pipeline:
1. `stash.uploadViaLogos(file)` → `{"queued":true}`
2. storage_module init+start (~40s)
3. deferred upload dispatched via `QTimer::singleShot(0, ...)`
4. `uploadUrl(QUrl, chunkSize)` → `success=1 session=0`
5. `storageUploadDone` event fires
6. CID extracted: `zDv...MobS3n`

## What was fixed this session

### Fix 1 — Wrong storage_module binary (CRITICAL, now resolved)
The installed binary was missing `StorageModulePlugin::uploadUrl`. Built correct binary from rev `321283bb` via wrapper flake at `~/basecamp/scratch/build-storage-module/flake.nix` (overrides `logos-storage` input hash to match what GitHub actually serves now).

New binary: `/nix/store/kfzdxij0mcf4v3q6sn9ba173n5v54vlg-logos-storage_module-module/lib/storage_module_plugin.so`
Installed to: `~/.local/share/Logos/LogosBasecamp/modules/storage_module/storage_module_plugin.so`

Note: new binary uses `StorageModulePlugin` (Qt metaobject dispatch) not `StorageModuleProviderObject` (custom switch dispatch). `QtProviderObject::callMethod` finds `uploadUrl` via Qt metaobject on `StorageModulePlugin`.

### Fix 2 — Wrong argument type (already applied session 2)
`storage_module_api.cpp`: passes `QVariant::fromValue(QUrl::fromLocalFile(filePath))` not `QVariant(QString)`.

## Current state

- Branch: `feature/logos-storage-transport`
- Last commit: `05fbdee fix: replace blocking init()/start() with async chain to fix spinner`
- Build: clean (`ninja` in `build-new/`)
- **Uncommitted changes**: `src/plugin/StashPlugin.cpp`, `src/generated/storage_module_api.cpp`, `src/plugin/StashPlugin.h`
- Test: PASSING — CID arrives within 61s in headless logoscore run

## What is next

1. **Commit current working changes** on `feature/logos-storage-transport`
2. **Wire `checkAll()` to use Logos transport** when `m_activeTransport == "logos"` — call `queueViaLogos(filePath, client, objectName)` for each watched module
3. **`handleLogosUploadDone` → `setBackupCid`**: when `m_pendingLogosUploads[sessionId].client != nullptr`, call `client->callMethod("setBackupCid", [cid, timestamp])`
4. **Rebuild and retest** with full `checkAll()` flow

## Context hard to re-derive

- **sessionId=0**: `uploadUrl` returns `LogosResult{success=1, value=0}` — the integer `0` is the session ID (not an error). `storageUploadDone` fires with count=3 args `[sessionId, manifestCid, treeCid]` — `args[1]` is the CID.
- **storageUploadDone args**: `[sessionId(int), manifestCid(QString), treeCid(QString)]` confirmed from `count=3 raw=true` and `cid=` extraction in `handleLogosUploadDone`.
- **Wrapper flake**: `~/basecamp/scratch/build-storage-module/flake.nix` — use this to rebuild storage_module if needed. It overrides `logos-storage` input to use `sha256-wH1GK...` hash (what GitHub currently serves for that rev).
- **Build dir**: `/home/alisher/basecamp/modules/stash-basecamp/build-new/`
- **Test script**: `/tmp/test_with_cap.sh` (150s timeout, loads capability_module + storage_module + stash)
- **Grep for CID**: use `grep "cid="` in `/tmp/stash_plugin.diag`, NOT `grep "bafy"` (CIDs are `zDv...` format)
