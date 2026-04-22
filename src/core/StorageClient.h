#pragma once

#include <QObject>
#include <QString>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <functional>
#include <memory>
#include <optional>

class LogosAPI;
class LogosAPIClient;

/**
 * StorageTransport — minimal abstraction over the storage_module IPC
 * surface. Allows StorageClient to be tested without linking the full
 * Logos SDK and without needing a running storage_module.
 *
 * The real implementation (LogosStorageTransport) wraps LogosAPIClient.
 * Tests provide their own implementation that records calls and fires
 * synthetic events.
 */
class StorageTransport
{
public:
    using EventCallback =
        std::function<void(const QString& eventName, const QVariantList& args)>;

    virtual ~StorageTransport() = default;

    /** True if the underlying client is connected to storage_module. */
    virtual bool isConnected() const = 0;

    /** Invoke StorageModulePlugin::uploadUrl(QUrl, int chunkSize). */
    virtual void uploadUrl(const QUrl& fileUrl, int chunkSize) = 0;

    /** Invoke StorageModulePlugin::downloadToUrl(cid, destUrl, local, chunkSize). */
    virtual void downloadToUrl(const QString& cid,
                               const QUrl& destUrl,
                               bool localOnly,
                               int chunkSize) = 0;

    /** Subscribe to the storage_module's eventResponse signal. */
    virtual void subscribeEventResponse(EventCallback cb) = 0;
};

/**
 * Real transport — uses the typed StorageModule SDK (generated from
 * storage_module_api.h) for upload/download and event subscription.
 * Owns the StorageModule wrapper. Call init() + start() once before
 * the first upload; both are synchronous (start may block ~30 s without
 * the detached-start fork of storage_module).
 */
class StorageModule;  // from src/generated/storage_module_api.h

class LogosStorageTransport : public StorageTransport
{
public:
    explicit LogosStorageTransport(LogosAPI* api);
    ~LogosStorageTransport() override;

    /**
     * Call once from a deferred context (QTimer::singleShot) to bring up
     * the storage node. Returns false if init or start fails.
     * dataDir: writable directory for storage node data.
     */
    bool initAndStart(const QString& dataDir);

    bool isConnected() const override;
    void uploadUrl(const QUrl& fileUrl, int chunkSize) override;

    /**
     * Download via Logos IPC is not yet fully implemented (downloadChunks
     * has no dest-path parameter in current API). This stub fires an
     * immediate error via the event callback.
     */
    void downloadToUrl(const QString& cid,
                       const QUrl& destUrl,
                       bool localOnly,
                       int chunkSize) override;
    void subscribeEventResponse(EventCallback cb) override;

private:
    StorageModule*  m_storage;  // owned
    bool            m_ready = false;
    EventCallback   m_eventCb;
};

/**
 * StorageClient — high-level wrapper around the storage_module IPC
 * surface. Uses one-shot uploadUrl / downloadToUrl (not manual chunk
 * orchestration) — the storage module wraps libstorage.so's
 * storage_upload_file internally (verified by the UploadFileCallbackCtx
 * symbol in storage_module_plugin.so).
 *
 * ## Concurrency contract (v2.0)
 * At most ONE upload and ONE download may be in flight at a time.
 * Concurrent calls to uploadFile() or downloadToFile() while a request
 * of the same type is pending are REJECTED synchronously with a "busy"
 * error. v2.0 Phase 2 debounces auto-backup to 30s intervals, so
 * overlap is not expected in practice; this restriction makes the
 * FIFO-by-event-name completion routing safe.
 *
 * ## Timeout contract
 * Every pending request has a QTimer. If the corresponding
 * storage*Done event does not arrive within the timeout (default
 * 120s), the callback fires with a timeout error and the pending slot
 * is freed. The timeout is configurable via setTimeoutMs() for tests.
 *
 * ## Event name assumptions
 * Known event names from symbol inspection: storageUploadDone,
 * storageDownloadDone, storageUploadProgress, storageDownloadProgress.
 * v2.0 ignores progress events.
 *
 * The exact shape of eventResponse args is NOT verified from symbols
 * alone. When storage_module is installed in LogosBasecamp, the shape
 * must be confirmed and this class may need adjustment. Extraction
 * logic in StorageClient.cpp::onEventResponse documents assumptions.
 */
class StorageClient : public QObject
{
    Q_OBJECT

public:
    using UploadCallback   = std::function<void(const QString& cidOrEmpty,
                                                const QString& errorOrEmpty)>;
    using DownloadCallback = std::function<void(const QString& errorOrEmpty)>;

    /**
     * @param transport  Transport to use. Takes ownership. Pass nullptr
     *                   (or a transport that returns isConnected()==false)
     *                   to simulate "storage_module not available".
     */
    explicit StorageClient(std::unique_ptr<StorageTransport> transport,
                           QObject* parent = nullptr);
    ~StorageClient() override;

    /** True if the transport reports it is connected to storage_module. */
    bool isAvailable() const;

    /**
     * Upload a local file. One-shot — storage module chunks internally.
     * Callback fires exactly once: CID on success, error on failure
     * (unavailable, missing file, busy, timeout, or storage-reported error).
     */
    void uploadFile(const QString& filePath, UploadCallback cb);

    /**
     * Download a blob by CID to a local file. Callback fires exactly
     * once with an error string (empty = success).
     */
    void downloadToFile(const QString& cid,
                        const QString& destPath,
                        DownloadCallback cb);

    /**
     * Set the per-request timeout in milliseconds. Default: 120000 (2min).
     * Zero disables the timeout entirely (useful for tests that assert
     * nothing times out). Applies to requests started AFTER this call.
     */
    void setTimeoutMs(int ms);

    int timeoutMs() const { return m_timeoutMs; }

private:
    struct PendingUpload {
        QString                 filePath;
        UploadCallback          cb;
        std::unique_ptr<QTimer> timer;
    };
    struct PendingDownload {
        QString                 cid;
        QString                 destPath;
        DownloadCallback        cb;
        std::unique_ptr<QTimer> timer;
    };

    void subscribeEventsIfNeeded();
    void onEventResponse(const QString& eventName, const QVariantList& args);
    void onUploadTimeout();
    void onDownloadTimeout();
    void failPendingUpload(const QString& cidOrEmpty, const QString& error);
    void failPendingDownload(const QString& error);

    std::unique_ptr<StorageTransport>    m_transport;
    std::optional<PendingUpload>         m_pendingUpload;
    std::optional<PendingDownload>       m_pendingDownload;
    bool                                 m_subscribed = false;
    int                                  m_timeoutMs  = 120 * 1000;  // 2min

    static constexpr int kChunkSize = 64 * 1024;      // 64 KiB default

    // Events (from symbol inspection of storage_module_plugin.so)
    static constexpr const char* kEventUploadDone   = "storageUploadDone";
    static constexpr const char* kEventDownloadDone = "storageDownloadDone";
};
