# Stash Pinning Lessons

Learned during online IPFS pinning implementation (2026-04-16).

---

## Pinata API: v3 Scoped JWT vs v2 Endpoint

Pinata has two auth systems that are NOT compatible:

| Endpoint | Auth |
|----------|------|
| `/pinning/pinFileToIPFS` (v2) | Old API keys only |
| `uploads.pinata.cloud/v3/files` (v3) | Scoped JWT only |

Using a v3 scoped JWT with the v2 endpoint returns `401 {"error":{"reason":"INVALID_CREDENTIALS","details":"No Authentication method provided"}}`.

**Rule:** Always use `/v3/files` with scoped JWTs. Never mix v2 endpoints with v3 keys.

---

## Pinata Public vs Private Uploads

Files uploaded to `/v3/files` are private by default (not publicly pinned to IPFS network — only accessible through Pinata's authenticated gateway).

**To pin publicly:** add a `network: "public"` multipart form field:

```cpp
QHttpPart networkPart;
networkPart.setHeader(QNetworkRequest::ContentDispositionHeader,
                      QStringLiteral("form-data; name=\"network\""));
networkPart.setBody(QByteArray("public"));
multiPart->append(networkPart);
```

Files then appear under the PUBLIC tab in Pinata and are accessible via any IPFS gateway (`ipfs.io/ipfs/<cid>`, `cloudflare-ipfs.com/ipfs/<cid>`, etc.).

---

## CIDv0 vs CIDv1 — Don't Compare Them

Local `ipfs --offline add` returns CIDv0 (`Qm...` — base58btc, SHA2-256).
Pinata `/v3/files` returns CIDv1 (`bafk...` — base32, SHA2-256).

They refer to the **same content** but are different encodings of the same multihash. String equality `localCid == remoteCid` always fails.

**Rule:** Trust the remote CID as canonical. Don't compare local and remote CIDs.

---

## Token Masking Re-Save Trap

`getPinningConfig` returns `token: "***"` (masked) to avoid exposing the JWT in QML.

If the UI clears the token field when the panel opens (to show it's masked), the user may re-save without re-entering — sending an empty token to `setPinningConfig` which overwrites the stored JWT with `""`.

**Fix pattern:**
- C++ `setPinningConfig`: skip `setValue` if token is empty (`if (!token.isEmpty()) s.setValue(...)`)
- QML: set `placeholderText = "Token saved — leave blank to keep"` when `cfg.token === "***"`, leave field blank

---

## StashBackend.appendLog Must Be Called Explicitly from Plugin Side

`StashBackend.appendLog()` is only called from within backend-internal operations (upload/download callbacks). Direct plugin methods like `uploadViaIpfs` that bypass the backend don't auto-log.

`getLog()` returns `m_backend.logEntries()` — which is empty if only plugin-side calls happened.

**Fix:** Make `appendLog` public. Call it explicitly from `uploadViaIpfs` after pin success and failure:

```cpp
m_backend.appendLog("backup_uploaded", fname + " → " + remoteCid);
// or on failure:
m_backend.appendLog("error", fname + ": " + pinError);
```
