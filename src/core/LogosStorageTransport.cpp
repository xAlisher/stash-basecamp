#include "StorageClient.h"
#include "storage_module_api.h"
#include "logos_api.h"

#include <QDebug>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>
#include <QVariantList>

// ── Construction ─────────────────────────────────────────────────────────────

LogosStorageTransport::LogosStorageTransport(LogosAPI* api)
    : m_storage(new StorageModule(api))
{
}

LogosStorageTransport::~LogosStorageTransport()
{
    delete m_storage;
}

// ── Init ─────────────────────────────────────────────────────────────────────

bool LogosStorageTransport::initAndStart(const QString& dataDir)
{
    QDir().mkpath(dataDir);

    QJsonObject cfg;
    cfg["data-dir"] = dataDir;
    const QString cfgJson =
        QString::fromUtf8(QJsonDocument(cfg).toJson(QJsonDocument::Compact));

    if (!m_storage->init(cfgJson)) {
        qWarning() << "LogosStorageTransport: storage_module init() failed";
        return false;
    }

    // start() may block ~30 s (libstorage discovery + transport bind).
    // Without a detached-start fork of storage_module, this holds the
    // caller's thread. Caller is responsible for deferring via singleShot.
    if (!m_storage->start()) {
        qWarning() << "LogosStorageTransport: storage_module start() failed";
        return false;
    }

    m_ready = true;
    return true;
}

// ── StorageTransport interface ────────────────────────────────────────────────

bool LogosStorageTransport::isConnected() const
{
    return m_storage != nullptr && m_ready;
}

void LogosStorageTransport::uploadUrl(const QUrl& fileUrl, int chunkSize)
{
    if (!m_storage) {
        qWarning() << "LogosStorageTransport::uploadUrl: no StorageModule";
        if (m_eventCb)
            m_eventCb("storageUploadDone",
                      QVariantList() << false << "" << "");
        return;
    }

    const QString filePath = fileUrl.toLocalFile();
    LogosResult r = m_storage->uploadUrl(filePath, chunkSize);
    if (!r.success) {
        qWarning() << "LogosStorageTransport::uploadUrl: rejected:"
                   << r.error.toString();
        // Fire a synthetic done event so StorageClient's pending upload
        // callback fires with the error.
        if (m_eventCb)
            m_eventCb("storageUploadDone",
                      QVariantList() << false << r.error.toString() << "");
    }
    // On success: uploadUrl returns the session id; the real completion
    // arrives via the storageUploadDone event subscribed in
    // subscribeEventResponse(). StorageClient's onEventResponse picks it up.
}

void LogosStorageTransport::downloadToUrl(const QString& /*cid*/,
                                          const QUrl& /*destUrl*/,
                                          bool /*localOnly*/,
                                          int /*chunkSize*/)
{
    // downloadChunks (current typed SDK) has no dest-path parameter.
    // Chunk reassembly to a file is not yet implemented.
    // Fire an immediate error so StorageClient's pending download resolves.
    qWarning() << "LogosStorageTransport: download via Logos IPC not yet implemented";
    if (m_eventCb)
        m_eventCb("storageDownloadDone",
                  QVariantList() << false
                                 << "Logos IPC download not yet implemented"
                                 << "");
}

void LogosStorageTransport::subscribeEventResponse(EventCallback cb)
{
    m_eventCb = cb;
    if (!m_storage) return;

    // storageUploadDone payload: [bool ok, sessionId, cid]
    m_storage->on("storageUploadDone",
        [this](const QVariantList& d) {
            if (m_eventCb) m_eventCb("storageUploadDone", d);
        });

    // storageDownloadDone payload: [bool ok, msg]
    m_storage->on("storageDownloadDone",
        [this](const QVariantList& d) {
            if (m_eventCb) m_eventCb("storageDownloadDone", d);
        });

    // storageStart payload: [bool ok, msg] — not consumed by StorageClient
    // but used by StashPlugin to flip m_logosStorageReady.
    m_storage->on("storageStart",
        [this](const QVariantList& d) {
            if (m_eventCb) m_eventCb("storageStart", d);
        });
}
