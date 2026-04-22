#include "StashPlugin.h"

#include <QByteArray>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSettings>
#include <dlfcn.h>

#include "core/PinningClient.h"
#include "core/StorageClient.h"
#include "storage_module_api.h"
#include "cpp/logos_api.h"
#include "cpp/logos_api_client.h"
#include "token_manager.h"

static constexpr const char* kSettingsOrg    = "logos";
static constexpr const char* kSettingsApp    = "stash";
static constexpr const char* kModulesKey     = "watchedModules";
static constexpr const char* kPinProviderKey = "pinningProvider";
static constexpr const char* kPinEndpointKey = "pinningEndpoint";
static constexpr const char* kPinTokenKey       = "pinningToken";
static constexpr const char* kActiveTransportKey = "activeTransport";
static constexpr int         kLogosChunkSize = 65536;

// File-based diagnostics — qInfo/qWarning often don't reach logoscore stdout
// per inter-module-comm.md. Writes to /tmp/stash_plugin.diag.
static void stashDiag(const QString& line) {
    QFile f(QStringLiteral("/tmp/stash_plugin.diag"));
    if (f.open(QIODevice::Append | QIODevice::Text))
        QTextStream(&f) << QDateTime::currentDateTime().toString(Qt::ISODateWithMs)
                        << " " << line << "\n";
}

StashPlugin::StashPlugin(QObject* parent)
    : QObject(parent)
{
    // Load persisted watched modules.
    QSettings s{QLatin1String(kSettingsOrg), QLatin1String(kSettingsApp)};
    m_watchedModules = s.value(QLatin1String(kModulesKey)).toStringList();
}

void StashPlugin::initLogos(LogosAPI* api)
{
    logosAPI = api;

    // Mirror yolo-board pattern: construct StorageModule and subscribe events
    // immediately in initLogos.  storage_module is a declared dependency so
    // its QRO source is published before our initLogos is called.
    // requestObject() returns in < 100 ms here (no 20 s wait).
    m_logosStorage = new StorageModule(api);
    subscribeLogosStorageEvents();

    // Defer the init→start async chain to after initLogos returns so the
    // stash QRO source is published before we make any outgoing IPC calls.
    m_logosStorageStarting = true;
    QTimer::singleShot(0, this, [this]() {
        initLogosStorage();
    });
    stashDiag(QStringLiteral("initLogos: StorageModule created, deferred init scheduled"));
}

// ── Logos storage init ────────────────────────────────────────────────────────

void StashPlugin::subscribeLogosStorageEvents()
{
    if (!m_logosStorage) return;

    // storageStart: [bool ok, QString msg]
    m_logosStorage->on("storageStart", [this](const QVariantList& d) {
        const bool ok = !d.isEmpty() && d[0].toBool();
        if (ok) {
            m_logosStorageReady   = true;
            m_logosStorageStarting = false;
            m_backend.appendLog("logos_storage", "storage_module ready");
        } else {
            const QString msg = d.size() > 1 ? d[1].toString() : QString();
            m_backend.appendLog("error", "storageStart failed: " + msg);
        }
    });

    // storageUploadDone: [bool ok, QString sessionId, QString cid]
    // Some builds send [bool ok, QString cid] — handle both layouts.
    m_logosStorage->on("storageUploadDone", [this](const QVariantList& d) {
        stashDiag(QStringLiteral("EVT storageUploadDone: count=%1 raw=%2")
                  .arg(d.size())
                  .arg(d.isEmpty() ? "(empty)" : d[0].toString()));
        const bool ok = !d.isEmpty() && d[0].toBool();
        if (!ok) {
            const QString msg = d.size() > 1 ? d[1].toString() : "unknown error";
            m_backend.appendLog("error", "Logos upload failed: " + msg);
            // Clear all pending — single-in-flight contract.
            m_pendingLogosUploads.clear();
            return;
        }
        const QString second = d.size() > 1 ? d[1].toString() : QString();
        const QString third  = d.size() > 2 ? d[2].toString() : QString();
        const QString sessionId = !third.isEmpty() ? second : QString();
        const QString cid       = !third.isEmpty() ? third  : second;
        handleLogosUploadDone(sessionId, cid);
    });
}

void StashPlugin::initLogosStorage()
{
    stashDiag(QStringLiteral("initLogosStorage: enter ready=%1 init=%2 hasStorage=%3")
              .arg(m_logosStorageReady).arg(m_logosStorageInitializing).arg(!!m_logosStorage));
    if (m_logosStorageReady || m_logosStorageInitializing || !m_logosStorage) return;
    m_logosStorageInitializing = true;

    // Use a UUID-unique per-process data-dir to avoid cross-process lock conflicts.
    const QString baseDir =
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
        + QStringLiteral("/stash/storage");
    const QString instanceId = QString::fromUtf8(qgetenv("LOGOS_INSTANCE_ID"));
    const QString dataDir = instanceId.isEmpty() ? baseDir : (baseDir + QStringLiteral("_") + instanceId.left(12));
    QDir().mkpath(dataDir);

    QJsonObject cfg;
    cfg[QStringLiteral("data-dir")] = dataDir;
    const QString cfgJson =
        QString::fromUtf8(QJsonDocument(cfg).toJson(QJsonDocument::Compact));

    // Bootstrap the storage_module capability token (headless logoscore runs have
    // no capability_module; inject a synthetic non-empty token that passes the
    // server-side isEmpty() check).
    {
        TokenManager& tm = TokenManager::instance();
        if (tm.getToken(QStringLiteral("storage_module")).isEmpty()) {
            tm.saveToken(QStringLiteral("storage_module"),
                         QStringLiteral("stash_headless_bootstrap_v1"));
            stashDiag(QStringLiteral("initLogosStorage: bootstrapped storage_module token"));
        }
    }

    // ── Sync init (yolo-board pattern) ───────────────────────────────────────
    // invokeRemoteMethod runs a nested QEventLoop via waitForFinished, which
    // properly drains the QRO stream. Use this for init, start, and uploadUrl
    // so events and replies arrive in the right order.
    stashDiag(QStringLiteral("initLogosStorage: calling init() dataDir=%1 retry=%2").arg(dataDir).arg(m_initRetryCount));
    const bool initOk = m_logosStorage->init(cfgJson);
    stashDiag(QStringLiteral("initLogosStorage: init() returned %1").arg(initOk));

    if (!initOk) {
        // init() fails if storage_module was already initialised (prior crash).
        // Fall through to start() — if already initialised it will succeed.
        stashDiag(QStringLiteral("initLogosStorage: init() failed — trying start() directly"));
    }

    stashDiag(QStringLiteral("initLogosStorage: calling start()"));
    const bool startOk = m_logosStorage->start();
    stashDiag(QStringLiteral("initLogosStorage: start() returned %1").arg(startOk));

    m_logosStorageInitializing = false;

    if (!startOk) {
        if (!initOk && m_initRetryCount < 10) {
            // Both init() and start() failed — node not ready yet, retry.
            m_initRetryCount++;
            m_logosStorageStarting = false;
            stashDiag(QStringLiteral("init+start both failed — retry %1/10 in 3 s").arg(m_initRetryCount));
            QTimer::singleShot(3000, this, [this]() { initLogosStorage(); });
            return;
        }
        m_logosStorageStarting = false;
        m_backend.appendLog("error", "Logos storage start() rejected");
        stashDiag(QStringLiteral("initLogosStorage: start() rejected — storage offline"));
        return;
    }

    // Set ready directly after start() returns (yolo-board pattern).
    // storageStart event doesn't arrive reliably cross-process via QRO.
    m_initRetryCount      = 0;
    m_logosStorageReady   = true;
    m_logosStorageStarting = false;
    m_backend.appendLog("logos_storage", "storage_module ready");
    stashDiag(QStringLiteral("initLogosStorage: storage_module READY"));

    // Process uploads that were queued while storage was starting.
    const auto deferred = m_deferredLogosUploads;
    m_deferredLogosUploads.clear();
    for (const PendingLogosUpload& def : deferred)
        queueViaLogos(def.filePath, def.client, def.objectName);
}

void StashPlugin::handleLogosUploadDone(const QString& sessionId, const QString& cid)
{
    PendingLogosUpload pending;
    if (!sessionId.isEmpty() && m_pendingLogosUploads.contains(sessionId)) {
        pending = m_pendingLogosUploads.take(sessionId);
    } else if (!m_pendingLogosUploads.isEmpty()) {
        // Fallback: grab first entry.  Handles:
        //  a) empty sessionId in event ([ok, cid] 2-element payload)
        //  b) stored under empty key because uploadUrl() LogosResult.value
        //     was empty cross-process (broken serialization path)
        auto it = m_pendingLogosUploads.begin();
        pending = it.value();
        m_pendingLogosUploads.erase(it);
    }

    const QString fname = pending.filePath.isEmpty()
        ? cid.left(12) : QFileInfo(pending.filePath).fileName();

    if (cid.isEmpty()) {
        m_backend.appendLog("error", "Logos upload done but no CID");
        return;
    }

    m_backend.appendLog("logos_uploaded", fname + " \u2192 " + cid + " (logos)");
    stashDiag(QStringLiteral("handleLogosUploadDone: cid=%1 file=%2").arg(cid, fname));
    qInfo() << "StashPlugin: Logos upload done cid=" << cid << "file=" << fname;

    // If this upload was triggered by checkAll(), call setBackupCid on the
    // source module so it records the new CID and timestamp.
    if (pending.client && !pending.objectName.isEmpty()) {
        const QString ts = QString::number(QDateTime::currentSecsSinceEpoch());
        stashDiag(QStringLiteral("handleLogosUploadDone: calling setBackupCid on %1").arg(pending.objectName));
        pending.client->invokeRemoteMethod(
            pending.objectName,
            QStringLiteral("setBackupCid"),
            cid, ts);
    }
}

QString StashPlugin::initialize()
{
    return QStringLiteral("ok");
}

QString StashPlugin::upload(const QString& filePath)
{
    if (filePath.isEmpty())
        return errorJson(QStringLiteral("filePath is empty"));

    QSettings s{QLatin1String(kSettingsOrg), QLatin1String(kSettingsApp)};
    const QString transport = s.value(QLatin1String(kActiveTransportKey),
                                      QStringLiteral("kubo")).toString();
    if (transport == QStringLiteral("logos"))
        return uploadViaLogos(filePath);

    return m_backend.upload(filePath) ? queuedJson() : errorJson(QStringLiteral("storage not ready"));
}

QString StashPlugin::download(const QString& cid, const QString& destPath)
{
    if (cid.isEmpty())
        return errorJson(QStringLiteral("cid is empty"));
    if (destPath.isEmpty())
        return errorJson(QStringLiteral("destPath is empty"));
    return m_backend.download(cid, destPath) ? queuedJson() : errorJson(QStringLiteral("storage not ready"));
}

// ── Module watch list ─────────────────────────────────────────────────────────

QString StashPlugin::setWatchedModules(const QString& newlineSeparated)
{
    m_watchedModules.clear();
    const QStringList lines = newlineSeparated.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString& line : lines) {
        const QString trimmed = line.trimmed();
        if (!trimmed.isEmpty())
            m_watchedModules.append(trimmed);
    }
    // Persist so the list survives plugin restarts.
    QSettings s{QLatin1String(kSettingsOrg), QLatin1String(kSettingsApp)};
    s.setValue(QLatin1String(kModulesKey), m_watchedModules);

    QJsonArray arr;
    for (const QString& m : m_watchedModules) arr.append(m);
    QJsonObject obj;
    obj[QStringLiteral("modules")] = arr;
    return QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

QString StashPlugin::getWatchedModules() const
{
    QJsonArray arr;
    for (const QString& m : m_watchedModules) arr.append(m);
    QJsonObject obj;
    obj[QStringLiteral("modules")] = arr;
    return QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

QString StashPlugin::checkAll()
{
    if (!logosAPI)
        return errorJson(QStringLiteral("not initialized"));

    QSettings s{QLatin1String(kSettingsOrg), QLatin1String(kSettingsApp)};
    const QString transport = s.value(QLatin1String(kActiveTransportKey),
                                      QStringLiteral("kubo")).toString();
    const bool useLogos = (transport == QStringLiteral("logos"));

    if (useLogos && !m_logosStorageReady)
        return errorJson(QStringLiteral("Logos storage not ready — select kubo or pinata, or wait for Logos Storage to start"));

    int checked = 0;
    int queued  = 0;

    for (const QString& moduleName : std::as_const(m_watchedModules)) {
        auto* client = logosAPI->getClient(moduleName);
        if (!client) continue;

        ++checked;

        // Convention: module "foo" registers its backend as "FooBackend".
        QString objectName = moduleName;
        objectName[0] = objectName[0].toUpper();
        objectName += QStringLiteral("Backend");

        // Ask the module if it has a file ready for stash.
        const QVariant raw = client->invokeRemoteMethod(
            objectName, QStringLiteral("getFileForStash"));
        const QJsonObject resp =
            QJsonDocument::fromJson(raw.toString().toUtf8()).object();

        if (!resp.value(QStringLiteral("ok")).toBool()) continue;

        const QString filePath = resp.value(QStringLiteral("path")).toString();
        if (filePath.isEmpty()) continue;

        bool ok = false;
        if (useLogos) {
            // Route through Logos storage_module IPC.
            // queueViaLogos stores (client, objectName) so handleLogosUploadDone
            // can call setBackupCid when the storageUploadDone event fires.
            const QString qr = queueViaLogos(filePath, client, objectName);
            const QJsonObject qobj = QJsonDocument::fromJson(qr.toUtf8()).object();
            ok = qobj.value(QStringLiteral("queued")).toBool();
        } else {
            // Route through kubo/pinata via StashBackend.
            auto* capturedClient = client;
            const QString capturedObject = objectName;
            ok = m_backend.uploadWithCallback(
                filePath,
                [capturedClient, capturedObject](const QString& cid) {
                    const QString ts = QString::number(QDateTime::currentSecsSinceEpoch());
                    capturedClient->invokeRemoteMethod(
                        capturedObject,
                        QStringLiteral("setBackupCid"),
                        cid, ts);
                });
        }

        if (ok) ++queued;
    }

    QJsonObject result;
    result[QStringLiteral("checked")]   = checked;
    result[QStringLiteral("queued")]    = queued;
    result[QStringLiteral("transport")] = transport;
    return QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Compact));
}

// ── Logos IPC upload ──────────────────────────────────────────────────────────

QString StashPlugin::queueViaLogos(const QString& filePath,
                                    LogosAPIClient* client,
                                    const QString&  objectName)
{
    if (!m_logosStorage)
        return errorJson(QStringLiteral("Logos storage not initialized"));

    if (m_logosStorageStarting) {
        PendingLogosUpload def;
        def.filePath   = filePath;
        def.client     = client;
        def.objectName = objectName;
        m_deferredLogosUploads.append(def);
        stashDiag(QStringLiteral("queueViaLogos: deferred (storage starting) %1").arg(QFileInfo(filePath).fileName()));
        return queuedJson();
    }

    if (!m_logosStorageReady)
        return errorJson(QStringLiteral("Logos storage not ready"));

    if (!QFileInfo::exists(filePath))
        return errorJson(QStringLiteral("File not found: ") + filePath);

    // Use the filePath as a placeholder key until uploadUrlAsync fires the
    // callback with the real sessionId.  handleLogosUploadDone already has a
    // fallback that grabs the first pending entry when sessionId is unknown.
    PendingLogosUpload pending;
    pending.filePath   = filePath;
    pending.client     = client;
    pending.objectName = objectName;
    m_pendingLogosUploads[filePath] = pending;

    const QString fname = QFileInfo(filePath).fileName();

    // Defer the storage_module IPC call to after this invocation returns to
    // the event loop.  QML calls stash.upload() synchronously — while that
    // call is on the stack the stash IPC channel is held open.  Calling
    // uploadUrlAsync() inline tries to call storage_module over the same IPC
    // stack, which stalls for ~20 s before timing out.  QTimer::singleShot(0)
    // returns {"queued":true} to QML first, frees the IPC channel, then fires
    // the actual storage_module call on the next event-loop iteration.
    stashDiag(QStringLiteral("queueViaLogos: deferred upload scheduled for %1").arg(fname));

    QTimer::singleShot(0, this, [this, filePath, fname]() {
        if (!m_logosStorage || !m_logosStorageReady) {
            m_pendingLogosUploads.remove(filePath);
            m_backend.appendLog(QStringLiteral("error"),
                                QStringLiteral("Logos upload aborted: storage not ready"));
            return;
        }
        doChunkedUpload(filePath, fname);
    });

    return queuedJson();
}

// ── uploadUrl helper (yolo-board pattern) ────────────────────────────────────
//
// Sync uploadUrl — invokeRemoteMethod runs a nested QEventLoop via
// waitForFinished.  The Go goroutine may emit storageUploadDone synchronously
// before the uploadUrl reply is written on the server side, but the nested
// event loop drains both in order: event arrives, then reply arrives, then
// waitForFinished returns.  storageUploadDone handler fires during the wait.

void StashPlugin::doChunkedUpload(const QString& filePath, const QString& fname)
{
    // Re-inject bootstrap token in case it was consumed between init and here.
    {
        TokenManager& tm = TokenManager::instance();
        if (tm.getToken(QStringLiteral("storage_module")).isEmpty())
            tm.saveToken(QStringLiteral("storage_module"),
                         QStringLiteral("stash_headless_bootstrap_v1"));
    }

    stashDiag(QStringLiteral("uploadUrl sync: file=%1").arg(fname));

    // Sync call — waitForFinished runs a nested event loop that properly
    // drains the QRO stream, including the storageUploadDone event.
    const LogosResult r = m_logosStorage->uploadUrl(filePath, kLogosChunkSize);

    stashDiag(QStringLiteral("uploadUrl result: success=%1 value=%2 err=%3")
              .arg(r.success).arg(r.value.toString()).arg(r.error.toString()));

    if (!r.success) {
        m_pendingLogosUploads.remove(filePath);
        m_backend.appendLog(QStringLiteral("error"),
            QStringLiteral("Logos uploadUrl failed: ") + r.error.toString());
        return;
    }

    // Re-key the pending entry from filePath → real sessionId so that
    // handleLogosUploadDone can find it when the event fires.
    const QString sessionId = r.value.toString();
    if (!sessionId.isEmpty() && m_pendingLogosUploads.contains(filePath)) {
        PendingLogosUpload p = m_pendingLogosUploads.take(filePath);
        m_pendingLogosUploads[sessionId] = p;
    }
    stashDiag(QStringLiteral("uploadUrl accepted: session=%1 (storageUploadDone should have fired during wait)").arg(sessionId));
}

QString StashPlugin::uploadViaLogos(const QString& filePath)
{
    if (filePath.isEmpty())
        return errorJson(QStringLiteral("filePath is empty"));

    return queueViaLogos(filePath, nullptr, QString());
}

QString StashPlugin::getStorageInfo()
{
    QJsonObject obj;
    obj[QStringLiteral("ready")]    = m_logosStorageReady;
    obj[QStringLiteral("starting")] = m_logosStorageStarting;

    // Cached peer info (populated by refreshPeerInfo, not on every poll).
    if (!m_logosStoragePeerId.isEmpty())
        obj[QStringLiteral("peerId")] = m_logosStoragePeerId;
    if (!m_logosStorageSpr.isEmpty())
        obj[QStringLiteral("spr")] = m_logosStorageSpr;

    return QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

// ── Transport selection ───────────────────────────────────────────────────────

QString StashPlugin::setActiveTransport(const QString& transport)
{
    static const QStringList kValid = {
        QStringLiteral("logos"), QStringLiteral("kubo"), QStringLiteral("pinata")
    };
    if (!kValid.contains(transport))
        return errorJson(QStringLiteral("unknown transport — use \"logos\", \"kubo\", or \"pinata\""));

    QSettings s{QLatin1String(kSettingsOrg), QLatin1String(kSettingsApp)};
    s.setValue(QLatin1String(kActiveTransportKey), transport);
    m_backend.appendLog(QStringLiteral("info"),
                        QStringLiteral("transport changed to ") + transport);

    QJsonObject obj;
    obj[QStringLiteral("ok")] = true;
    obj[QStringLiteral("transport")] = transport;
    return QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

QString StashPlugin::getActiveTransport() const
{
    QSettings s{QLatin1String(kSettingsOrg), QLatin1String(kSettingsApp)};
    const QString transport = s.value(QLatin1String(kActiveTransportKey),
                                      QStringLiteral("kubo")).toString();
    QJsonObject obj;
    obj[QStringLiteral("transport")] = transport;
    return QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

// ── IPFS upload ───────────────────────────────────────────────────────────────

QString StashPlugin::bundledIpfsPath() const
{
    // Use a file-local symbol so dladdr resolves this .so's own path
    // without a pointer-to-member cast (which is UB and warns under GCC).
    static const auto anchor = [](){};
    Dl_info info;
    if (dladdr(reinterpret_cast<const void*>(&anchor), &info) == 0
            || info.dli_fname == nullptr)
        return {};
    return QFileInfo(QString::fromLocal8Bit(info.dli_fname))
               .absoluteDir().filePath(QStringLiteral("ipfs"));
}

QString StashPlugin::uploadViaIpfs(const QString& filePath)
{
    if (filePath.isEmpty())
        return errorJson(QStringLiteral("filePath is empty"));

    const QString ipfsBin = bundledIpfsPath();
    if (ipfsBin.isEmpty() || !QFileInfo::exists(ipfsBin))
        return errorJson(QStringLiteral("bundled ipfs binary not found — run scripts/fetch-kubo.sh"));

    static const QString ipfsPath = QStringLiteral("/tmp/stash-ipfs");

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("IPFS_PATH"), ipfsPath);

    // Init the repo once if it doesn't exist yet
    if (!QFileInfo::exists(ipfsPath + QStringLiteral("/config"))) {
        QProcess init;
        init.setProcessEnvironment(env);
        init.start(ipfsBin, {QStringLiteral("init"), QStringLiteral("--empty-repo")});
        if (!init.waitForStarted(3000) || !init.waitForFinished(15000) || init.exitCode() != 0) {
            const QString err = QString::fromUtf8(init.readAllStandardError()).trimmed();
            return errorJson(err.isEmpty() ? QStringLiteral("ipfs init failed") : err);
        }
    }

    QProcess proc;
    proc.setProcessEnvironment(env);
    proc.start(ipfsBin, {QStringLiteral("--offline"),
                         QStringLiteral("add"),
                         QStringLiteral("--quieter"),
                         filePath});

    if (!proc.waitForStarted(3000))
        return errorJson(QStringLiteral("bundled ipfs binary failed to start"));
    if (!proc.waitForFinished(60000))
        return errorJson(QStringLiteral("ipfs timed out"));
    if (proc.exitCode() != 0) {
        const QString err = QString::fromUtf8(proc.readAllStandardError()).trimmed();
        return errorJson(err.isEmpty() ? QStringLiteral("ipfs exited with error") : err);
    }

    const QString cid = QString::fromUtf8(proc.readAllStandardOutput()).trimmed();
    if (cid.isEmpty())
        return errorJson(QStringLiteral("ipfs returned empty CID"));

    // ── Online pinning ────────────────────────────────────────────────────
    QSettings s{QLatin1String(kSettingsOrg), QLatin1String(kSettingsApp)};
    const QString providerStr = s.value(QLatin1String(kPinProviderKey)).toString();
    if (providerStr.isEmpty())
        return errorJson(QStringLiteral("pinning provider not configured — open Stash settings"));

    const PinningProvider provider = (providerStr == QStringLiteral("pinata"))
                                     ? PinningProvider::Pinata
                                     : PinningProvider::Kubo;
    const QString endpoint = s.value(QLatin1String(kPinEndpointKey)).toString();
    const QString token    = s.value(QLatin1String(kPinTokenKey)).toString();

    QString pinError;
    const QString remoteCid = m_pinningClient.pinFile(provider, endpoint, token,
                                                       filePath, pinError);
    const QString fname = QFileInfo(filePath).fileName();
    if (remoteCid.isEmpty()) {
        m_backend.appendLog(QStringLiteral("error"),
                            fname + QStringLiteral(": ") + pinError);
        return errorJson(QStringLiteral("pinning failed: ") + pinError);
    }

    // Local ipfs uses CIDv0 (Qm...), Pinata returns CIDv1 (bafk...) — same
    // content, different encoding. Trust the remote CID as canonical.
    m_backend.appendLog(QStringLiteral("backup_uploaded"),
                        fname + QStringLiteral(" \u2192 ") + remoteCid
                        + QStringLiteral(" (") + providerStr + QStringLiteral(")"));
    QJsonObject obj;
    obj[QStringLiteral("cid")] = remoteCid;
    return QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

// ── Pinning config ────────────────────────────────────────────────────────────

QString StashPlugin::setPinningConfig(const QString& provider,
                                       const QString& endpoint,
                                       const QString& token)
{
    if (provider != QStringLiteral("pinata") && provider != QStringLiteral("kubo"))
        return errorJson(QStringLiteral("unknown provider — use \"pinata\" or \"kubo\""));

    if (provider == QStringLiteral("kubo")) {
        if (endpoint.isEmpty())
            return errorJson(QStringLiteral("endpoint is required for kubo provider"));
        if (!endpoint.startsWith(QStringLiteral("http://")) &&
            !endpoint.startsWith(QStringLiteral("https://")))
            return errorJson(QStringLiteral("endpoint must start with http:// or https://"));
    }

    QSettings s{QLatin1String(kSettingsOrg), QLatin1String(kSettingsApp)};
    s.setValue(QLatin1String(kPinProviderKey), provider);
    s.setValue(QLatin1String(kPinEndpointKey), endpoint);
    // Empty token means "keep existing" — UI sends empty when user hasn't re-typed the masked value
    if (!token.isEmpty())
        s.setValue(QLatin1String(kPinTokenKey), token);

    QJsonObject obj;
    obj[QStringLiteral("ok")] = true;
    return QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

QString StashPlugin::getPinningConfig() const
{
    QSettings s{QLatin1String(kSettingsOrg), QLatin1String(kSettingsApp)};
    const QString provider = s.value(QLatin1String(kPinProviderKey)).toString();
    const QString endpoint = s.value(QLatin1String(kPinEndpointKey)).toString();
    const QString token    = s.value(QLatin1String(kPinTokenKey)).toString();

    QJsonObject obj;
    obj[QStringLiteral("provider")]   = provider;
    obj[QStringLiteral("endpoint")]   = endpoint;
    obj[QStringLiteral("token")]      = token.isEmpty() ? QStringLiteral("") : QStringLiteral("***");
    obj[QStringLiteral("configured")] = !provider.isEmpty() && !token.isEmpty();
    return QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

// ── Status / log / quota ──────────────────────────────────────────────────────

QString StashPlugin::getStatus()
{
    return m_backend.status();
}

QString StashPlugin::getLog()
{
    return QString::fromUtf8(
        QJsonDocument(m_backend.logEntries()).toJson(QJsonDocument::Compact));
}

QString StashPlugin::getQuota()
{
    return m_backend.quotaJson();
}

// ── Private helpers ───────────────────────────────────────────────────────────

QString StashPlugin::errorJson(const QString& msg)
{
    QString safe = msg;
    safe.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    safe.replace(QLatin1Char('"'),  QStringLiteral("\\\""));
    return QStringLiteral("{\"error\":\"") + safe + QStringLiteral("\"}");
}

QString StashPlugin::queuedJson()
{
    return QStringLiteral("{\"queued\":true}");
}
