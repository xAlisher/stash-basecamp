#include "StashPlugin.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "core/LibStorageTransport.h"
#include "core/StorageClient.h"
#include "cpp/logos_api.h"
#include "cpp/logos_api_client.h"

StashPlugin::StashPlugin(QObject* parent)
    : QObject(parent)
{
}

void StashPlugin::initLogos(LogosAPI* api)
{
    logosAPI = api;

    auto* t = new LibStorageTransport();
    t->start();
    auto client = std::make_unique<StorageClient>(
        std::unique_ptr<StorageTransport>(t));
    m_backend.setStorageClient(std::move(client));
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

        // Ask the module if it has a file ready for stash.
        const QVariant raw = client->invokeRemoteMethod(
            QStringLiteral("NotesBackend"), QStringLiteral("getFileForStash"));
        const QJsonObject resp =
            QJsonDocument::fromJson(raw.toString().toUtf8()).object();

        if (!resp.value(QStringLiteral("ok")).toBool()) continue;

        const QString filePath = resp.value(QStringLiteral("path")).toString();
        if (filePath.isEmpty()) continue;

        // Capture stable pointer and module name for the callback.
        auto* capturedClient = client;
        const QString capturedModule = moduleName;

        const bool ok = m_backend.uploadWithCallback(
            filePath,
            [capturedClient, capturedModule](const QString& cid) {
                const QString ts = QString::number(QDateTime::currentSecsSinceEpoch());
                capturedClient->invokeRemoteMethod(
                    QStringLiteral("NotesBackend"),
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
