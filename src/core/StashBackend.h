#pragma once

#include <QObject>
#include <QString>
#include <QJsonArray>
#include <QDateTime>
#include <functional>

#include "StorageClient.h"

/**
 * StashBackend — thin wrapper around StorageClient that adds an in-memory
 * event log. All upload/download calls are async; results arrive via
 * logEvent() which is also emitted as a Qt signal.
 *
 * Log entry types:
 *   uploading  — upload started     detail = filename
 *   uploaded   — upload complete    detail = CID
 *   error      — upload/download    detail = error string
 *   downloading — download started  detail = CID
 *   downloaded — download complete  detail = dest path
 *   offline    — node not ready     detail = ""
 */
class StashBackend : public QObject
{
    Q_OBJECT

public:
    explicit StashBackend(QObject* parent = nullptr);
    ~StashBackend() override;

    // Wire the storage client. Must be called before upload/download.
    void setStorageClient(std::unique_ptr<StorageClient> client);

    // "ready" | "starting" | "offline"
    QString status() const;

    // Async upload. Returns false immediately if unavailable.
    bool upload(const QString& filePath);

    // Async upload with a success callback fired (on the Qt thread) when the CID is ready.
    bool uploadWithCallback(const QString& filePath,
                            std::function<void(const QString& cid)> onSuccess);

    // Async download. Returns false immediately if unavailable.
    bool download(const QString& cid, const QString& destPath);

    // Storage space JSON or empty string if unavailable.
    // (Placeholder — libstorage storage_space not yet wired.)
    QString quotaJson() const;

    // All log entries as a QJsonArray (capped at kMaxLog).
    QJsonArray logEntries() const;

signals:
    // Fired for every log event — UI polls getLog() but can also connect here.
    void logEvent(const QString& type, const QString& detail);

private:
    void appendLog(const QString& type, const QString& detail);

    std::unique_ptr<StorageClient> m_client;

    struct LogEntry {
        QString   type;
        QString   detail;
        qint64    timestamp;  // ms since epoch
    };
    QList<LogEntry> m_log;

    static constexpr int kMaxLog = 100;
};
