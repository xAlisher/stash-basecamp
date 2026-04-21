# Retro Log

## win 2026-04-21
stash core module migrated to logos-module-builder. Key learnings: (1) use #dual bundler — lgpm needs linux-amd64-dev, Basecamp needs linux-amd64; (2) logos-storage-nim was dead code — stash uses Kubo HTTP, not Nim binary; (3) lgpm install writes hashes + variant file correctly, Basecamp loads via main["linux-amd64"] key.
