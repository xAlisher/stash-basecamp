#include "StashBackend.h"

#include <QJsonObject>
#include <QJsonDocument>
#include <QFileInfo>

StashBackend::StashBackend(QObject* parent)
    : QObject(parent)
{
}

StashBackend::~StashBackend()
{
    // StorageClient::~StorageClient fires any pending upload/download callbacks
    // synchronously. Those callbacks capture `this` and call appendLog(), which
    // accesses m_log. We must destroy m_client BEFORE m_log is torn down.
    // (Member destruction order is reverse-declaration, so without this explicit
    // reset m_log would be gone first and appendLog() would corrupt memory.)
    m_client.reset();
}

void StashBackend::setStorageClient(std::unique_ptr<StorageClient> client)
{
    m_client = std::move(client);
}

QString StashBackend::status() const
{
    if (!m_client)
        return QStringLiteral("offline");
    if (!m_client->isAvailable())
        return QStringLiteral("starting");
    return QStringLiteral("ready");
}

bool StashBackend::upload(const QString& filePath)
{
    if (!m_client || !m_client->isAvailable()) {
        appendLog(QStringLiteral("offline"), {});
        return false;
    }

    const QString name = QFileInfo(filePath).fileName();
    appendLog(QStringLiteral("uploading"), name);

    m_client->uploadFile(filePath,
        [this, name](const QString& cid, const QString& error) {
            if (!error.isEmpty()) {
                appendLog(QStringLiteral("error"), error);
            } else {
                appendLog(QStringLiteral("uploaded"), cid);
            }
        });

    return true;
}

bool StashBackend::download(const QString& cid, const QString& destPath)
{
    if (!m_client || !m_client->isAvailable()) {
        appendLog(QStringLiteral("offline"), {});
        return false;
    }

    appendLog(QStringLiteral("downloading"), cid);

    m_client->downloadToFile(cid, destPath,
        [this, destPath](const QString& error) {
            if (!error.isEmpty()) {
                appendLog(QStringLiteral("error"), error);
            } else {
                appendLog(QStringLiteral("downloaded"), destPath);
            }
        });

    return true;
}

QString StashBackend::quotaJson() const
{
    // storage_space() FFI not yet wired — return placeholder.
    return QStringLiteral("{\"used\":0,\"total\":0}");
}

QJsonArray StashBackend::logEntries() const
{
    QJsonArray arr;
    for (const auto& e : m_log) {
        QJsonObject obj;
        obj[QStringLiteral("type")]      = e.type;
        obj[QStringLiteral("detail")]    = e.detail;
        obj[QStringLiteral("timestamp")] = e.timestamp;
        arr.append(obj);
    }
    return arr;
}

void StashBackend::appendLog(const QString& type, const QString& detail)
{
    if (m_log.size() >= kMaxLog)
        m_log.removeFirst();
    m_log.append({type, detail, QDateTime::currentMSecsSinceEpoch()});
    emit logEvent(type, detail);
}
