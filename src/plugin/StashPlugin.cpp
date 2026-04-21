#include "StashPlugin.h"

#include <QDateTime>
#include <QDir>
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

static constexpr const char* kSettingsOrg    = "logos";
static constexpr const char* kSettingsApp    = "stash";
static constexpr const char* kModulesKey     = "watchedModules";
static constexpr const char* kPinProviderKey = "pinningProvider";
static constexpr const char* kPinEndpointKey = "pinningEndpoint";
static constexpr const char* kPinTokenKey       = "pinningToken";
static constexpr const char* kActiveTransportKey = "activeTransport";
static constexpr int         kLogosChunkSize = 65536;

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

    // Construct typed storage wrapper (cheap — just getClient + stores ptr).
    // storage_module is declared in metadata.json dependencies, so its QRO
    // source is published before initLogos is called.
    m_logosStorage = new StorageModule(api);
    subscribeLogosStorageEvents();

    // Defer init() + start() — both are sync IPC that would block initLogos.
    // start() may take ~30 s (libstorage discovery + transport bind) without
    // the detached-start fork of storage_module.
    m_logosStorageStarting = true;
    QTimer::singleShot(0, this, [this]() { initLogosStorage(); });
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
    if (m_logosStorageReady || m_logosStorageInitializing || !m_logosStorage) return;
    m_logosStorageInitializing = true;

    const QString dataDir =
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
        + QStringLiteral("/stash/storage");
    QDir().mkpath(dataDir);

    QJsonObject cfg;
    cfg[QStringLiteral("data-dir")] = dataDir;
    const QString cfgJson =
        QString::fromUtf8(QJsonDocument(cfg).toJson(QJsonDocument::Compact));

    // initAsync returns immediately — no nested event loop, no spinner.
    m_logosStorage->initAsync(cfgJson, [this](bool ok) {
        if (!ok) {
            m_logosStorageStarting    = false;
            m_logosStorageInitializing = false;
            m_backend.appendLog("error", "Logos storage init() failed");
            return;
        }
        // startAsync returns immediately — storage_module starts in background.
        // Actual readiness is signalled by the "storageStart" event (subscribed
        // in subscribeLogosStorageEvents), which sets m_logosStorageReady.
        m_logosStorage->startAsync([this](bool accepted) {
            m_logosStorageInitializing = false;
            if (!accepted) {
                m_logosStorageStarting = false;
                m_backend.appendLog("error", "Logos storage start() rejected");
            }
            // If accepted, wait for the "storageStart" event to confirm readiness.
            // m_logosStorageReady is set there, not here.
        });
    });
}

void StashPlugin::handleLogosUploadDone(const QString& sessionId, const QString& cid)
{
    QString filePath;
    if (!sessionId.isEmpty() && m_pendingLogosUploads.contains(sessionId)) {
        filePath = m_pendingLogosUploads.take(sessionId);
    } else if (!m_pendingLogosUploads.isEmpty()) {
        // Fallback: grab first entry.  Handles:
        //  a) empty sessionId in event ([ok, cid] 2-element payload)
        //  b) stored under empty key because uploadUrl() LogosResult.value
        //     was empty cross-process (broken serialization path)
        auto it = m_pendingLogosUploads.begin();
        filePath = it.value();
        m_pendingLogosUploads.erase(it);
    }

    const QString fname = filePath.isEmpty()
        ? cid.left(12) : QFileInfo(filePath).fileName();

    if (cid.isEmpty()) {
        m_backend.appendLog("error", "Logos upload done but no CID");
        return;
    }

    m_backend.appendLog("logos_uploaded", fname + " \u2192 " + cid);
    qInfo() << "StashPlugin: Logos upload done cid=" << cid << "file=" << fname;
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

    int checked = 0;
    int queued  = 0;

    for (const QString& moduleName : std::as_const(m_watchedModules)) {
        auto* client = logosAPI->getClient(moduleName);
        if (!client) continue;

        ++checked;

        // Convention: module "foo" registers its backend as "FooBackend".
        // This lets checkAll() work without knowing module internals.
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

        // Capture for the async callback.
        auto* capturedClient = client;
        const QString capturedObject = objectName;

        const bool ok = m_backend.uploadWithCallback(
            filePath,
            [capturedClient, capturedObject](const QString& cid) {
                const QString ts = QString::number(QDateTime::currentSecsSinceEpoch());
                capturedClient->invokeRemoteMethod(
                    capturedObject,
                    QStringLiteral("setBackupCid"),
                    cid, ts);
            });

        if (ok) ++queued;
    }

    QJsonObject result;
    result[QStringLiteral("checked")] = checked;
    result[QStringLiteral("queued")]  = queued;
    return QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Compact));
}

// ── Logos IPC upload ──────────────────────────────────────────────────────────

QString StashPlugin::uploadViaLogos(const QString& filePath)
{
    if (filePath.isEmpty())
        return errorJson(QStringLiteral("filePath is empty"));

    if (!m_logosStorage)
        return errorJson(QStringLiteral("Logos storage not initialized"));

    if (m_logosStorageStarting)
        return errorJson(QStringLiteral("Logos storage is starting — try again in a moment"));

    if (!m_logosStorageReady)
        return errorJson(QStringLiteral("Logos storage not ready"));

    if (!QFileInfo::exists(filePath))
        return errorJson(QStringLiteral("File not found: ") + filePath);

    LogosResult r = m_logosStorage->uploadUrl(filePath, kLogosChunkSize);
    if (!r.success)
        return errorJson(QStringLiteral("Upload rejected: ") + r.error.toString());

    const QString sessionId = r.value.toString();
    m_pendingLogosUploads[sessionId] = filePath;
    m_backend.appendLog("logos_upload_queued",
                        QFileInfo(filePath).fileName()
                        + QStringLiteral(" (session=") + sessionId.left(8) + QStringLiteral("…)"));

    return queuedJson();
}

QString StashPlugin::getStorageInfo()
{
    QJsonObject obj;
    obj[QStringLiteral("ready")]    = m_logosStorageReady;
    obj[QStringLiteral("starting")] = m_logosStorageStarting;

    if (m_logosStorage && m_logosStorageReady) {
        LogosResult r = m_logosStorage->debug();
        if (r.success) {
            obj[QStringLiteral("peerId")] = r.getString("id");
            obj[QStringLiteral("spr")]    = r.getString("spr");
            QJsonArray addrs;
            const auto addrList = r.getValue<QStringList>("addrs");
            for (const QString& a : addrList) addrs.append(a);
            obj[QStringLiteral("addrs")] = addrs;
        }
    }
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
                        fname + QStringLiteral(" → ") + remoteCid);
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
