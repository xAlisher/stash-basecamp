#pragma once

#include <QMap>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariantList>

#include "interface.h"
#include "core/StashBackend.h"
#include "core/PinningClient.h"

class StorageModule;  // from src/generated/storage_module_api.h

class StashPlugin : public QObject, public PluginInterface
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.logos.StashModuleInterface" FILE "plugin_metadata.json")
    Q_INTERFACES(PluginInterface)

public:
    explicit StashPlugin(QObject* parent = nullptr);

    QString name()    const override { return QStringLiteral("stash"); }
    QString version() const override { return QStringLiteral("0.1.0"); }

    Q_INVOKABLE void    initLogos(LogosAPI* api);
    Q_INVOKABLE QString initialize();

    // Upload a local file. Returns {"queued":true} or {"error":"..."}.
    Q_INVOKABLE QString upload(const QString& filePath);

    // Download a CID to a local path. Returns {"queued":true} or {"error":"..."}.
    Q_INVOKABLE QString download(const QString& cid, const QString& destPath);

    // Upload via Logos storage_module IPC (typed SDK).
    // Returns {"queued":true} if accepted, {"error":"..."} if not ready or rejected.
    Q_INVOKABLE QString uploadViaLogos(const QString& filePath);

    // Returns {"ready":bool,"starting":bool,"peerId":"...","spr":"..."}.
    Q_INVOKABLE QString getStorageInfo();

    // Set the active upload transport: "logos" | "kubo" | "pinata".
    // upload() and checkAll() will route through the selected transport.
    // Returns {"ok":true} or {"error":"..."}.
    Q_INVOKABLE QString setActiveTransport(const QString& transport);

    // Returns {"transport":"logos"|"kubo"|"pinata"}.
    Q_INVOKABLE QString getActiveTransport() const;

    // ── Module watch list ────────────────────────────────────────────────────
    // Newline-separated module names. Stash will call getFileForStash() on each.
    Q_INVOKABLE QString setWatchedModules(const QString& newlineSeparated);

    // Returns {"modules":["notes","keycard",...]} or {"modules":[]}.
    Q_INVOKABLE QString getWatchedModules() const;

    // Poll all watched modules: call getFileForStash(), upload found files,
    // and call setBackupCid(cid, timestamp) on each module that returned a file.
    // Returns {"checked":N,"queued":M} or {"error":"..."}.
    Q_INVOKABLE QString checkAll();

    // Upload a file via local IPFS daemon, then pin it online.
    // Returns {"cid":"..."} or {"error":"..."}.
    Q_INVOKABLE QString uploadViaIpfs(const QString& filePath);

    // Configure the pinning provider. provider: "pinata"|"kubo".
    // Returns {"ok":true} or {"error":"..."}.
    Q_INVOKABLE QString setPinningConfig(const QString& provider,
                                         const QString& endpoint,
                                         const QString& token);

    // Returns {"provider":"...","endpoint":"...","configured":true|false}.
    // Token is masked as "***" if non-empty.
    Q_INVOKABLE QString getPinningConfig() const;

    // "ready" | "starting" | "offline"
    Q_INVOKABLE QString getStatus();

    // Recent log entries as JSON array [{type,detail,timestamp}].
    Q_INVOKABLE QString getLog();

    // Storage space: {"used":N,"total":N} or {"error":"..."}.
    Q_INVOKABLE QString getQuota();

signals:
    void eventResponse(const QString& eventName, const QVariantList& data);

protected:
    virtual QString bundledIpfsPath() const;

private:
    static QString errorJson(const QString& msg);
    static QString queuedJson();

    // Logos storage_module IPC (typed SDK)
    void initLogosStorage();
    void subscribeLogosStorageEvents();
    void handleLogosUploadDone(const QString& sessionId, const QString& cid);

    StashBackend   m_backend;
    QStringList    m_watchedModules;
    PinningClient  m_pinningClient;

    // Logos storage state
    StorageModule*            m_logosStorage          = nullptr;
    bool                      m_logosStorageReady     = false;
    bool                      m_logosStorageStarting  = false;
    bool                      m_logosStorageInitializing = false; // async init in flight
    QMap<QString, QString>    m_pendingLogosUploads;  // sessionId → filePath
};
