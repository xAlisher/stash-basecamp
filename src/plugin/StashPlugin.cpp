#include "StashPlugin.h"

#include "core/LibStorageTransport.h"
#include "core/StorageClient.h"
#include "cpp/logos_api.h"

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

QString StashPlugin::okJson()
{
    return QStringLiteral("{\"ok\":true}");
}

QString StashPlugin::queuedJson()
{
    return QStringLiteral("{\"queued\":true}");
}

QString StashPlugin::errorJson(const QString& msg)
{
    QString safe = msg;
    safe.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    safe.replace(QLatin1Char('"'),  QStringLiteral("\\\""));
    return QStringLiteral("{\"error\":\"") + safe + QStringLiteral("\"}");
}
