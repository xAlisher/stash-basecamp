#include "StashPlugin.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSettings>
#include <dlfcn.h>

#include "core/StorageClient.h"
#include "cpp/logos_api.h"
#include "cpp/logos_api_client.h"

static constexpr const char* kSettingsOrg  = "logos";
static constexpr const char* kSettingsApp  = "stash";
static constexpr const char* kModulesKey   = "watchedModules";

StashPlugin::StashPlugin(QObject* parent)
    : QObject(parent)
{
    // Load persisted watched modules.
    QSettings s{QLatin1String(kSettingsOrg), QLatin1String(kSettingsApp)};
    m_watchedModules = s.value(QLatin1String(kModulesKey)).toStringList();

    // Auto-poll: call checkAll() every 30 minutes while the plugin is alive.
    m_pollTimer.setInterval(30 * 60 * 1000);
    m_pollTimer.setSingleShot(false);
    connect(&m_pollTimer, &QTimer::timeout, this, [this]{ checkAll(); });
    m_pollTimer.start();
}

void StashPlugin::initLogos(LogosAPI* api)
{
    logosAPI = api;
    // No embedded transport — libstorage.a removed to fix Nim runtime conflict
    // with storage_module. Storage routing goes through QML (experiment/qml-routing).
}

QString StashPlugin::initialize()
{
    return QStringLiteral("ok");
}

QString StashPlugin::upload(const QString& filePath)
{
    if (filePath.isEmpty())
        return errorJson(QStringLiteral("filePath is empty"));
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

    QJsonObject obj;
    obj[QStringLiteral("cid")] = cid;
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
