# Retro Log

## win 2026-04-22 (session 7)
CID delivery fixed by flipping responsibility: Stash stores upload result in m_latestLogosFile/Cid/Ts, exposes via getLatestLogosResult(). Notes QML polls and calls notes.setBackupCid() on itself. Root cause of original failure: QRO back-call (A→B→A pattern) times out even with fresh LogosAPIClient* — the QRO source itself is unavailable post-IPC-call, not the pointer. Stale pointer was a red herring. Rule: never attempt QRO back-call after incoming IPC; use store-and-poll. Extracted as ipc-back-call-use-poll-push in basecamp-skills.

## win 2026-04-22 (session 6)
stash_ui UI polish sprint complete — PR #15 merged. Key lessons: (1) `background:null` on `QtQuick.TextEdit` silently blocks module load — root cause of stash_ui not opening at session start; (2) moving QML assets to `qml/` subdir triggers `cp -r` in logos-module-builder instead of single-file copy — all components now bundled; (3) QML circular width dependency in transport pill: `Rectangle.width: row.implicitWidth + 20` + `Row { anchors.centerIn }` resolves to 0 — fix: use `implicitWidth` + `RowLayout` anchored by `left + leftMargin`; (4) `Qt.openUrlExternally` silently blocked in sandbox — use `clipHelper` copy pattern instead.

## win 2026-04-22
stash.uploadViaLogos() confirmed working end-to-end: file → storage_module IPC → CID `zDv...` in 61s. Two root causes found and fixed: (1) installed storage_module_plugin.so was too old — missing `StorageModulePlugin::uploadUrl` (new binary uses Qt metaobject dispatch, not custom ProviderObject switch); (2) storage_module_api.cpp was passing QVariant(QString) instead of QVariant(QUrl) to invokeRemoteMethod. Nix build unblocked via wrapper flake that overrides transitive `logos-storage` input whose narHash had changed upstream.

## win 2026-04-21
stash core module migrated to logos-module-builder. Key learnings: (1) use #dual bundler — lgpm needs linux-amd64-dev, Basecamp needs linux-amd64; (2) logos-storage-nim was dead code — stash uses Kubo HTTP, not Nim binary; (3) lgpm install writes hashes + variant file correctly, Basecamp loads via main["linux-amd64"] key.
