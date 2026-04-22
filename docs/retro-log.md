# Retro Log

## win 2026-04-22
stash.uploadViaLogos() confirmed working end-to-end: file → storage_module IPC → CID `zDv...` in 61s. Two root causes found and fixed: (1) installed storage_module_plugin.so was too old — missing `StorageModulePlugin::uploadUrl` (new binary uses Qt metaobject dispatch, not custom ProviderObject switch); (2) storage_module_api.cpp was passing QVariant(QString) instead of QVariant(QUrl) to invokeRemoteMethod. Nix build unblocked via wrapper flake that overrides transitive `logos-storage` input whose narHash had changed upstream.

## win 2026-04-21
stash core module migrated to logos-module-builder. Key learnings: (1) use #dual bundler — lgpm needs linux-amd64-dev, Basecamp needs linux-amd64; (2) logos-storage-nim was dead code — stash uses Kubo HTTP, not Nim binary; (3) lgpm install writes hashes + variant file correctly, Basecamp loads via main["linux-amd64"] key.
