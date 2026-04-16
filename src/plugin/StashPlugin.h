#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariantList>

#include "interface.h"
#include "core/StashBackend.h"

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

    // ── Module watch list ────────────────────────────────────────────────────
    // Newline-separated module names. Stash will call getFileForStash() on each.
    Q_INVOKABLE QString setWatchedModules(const QString& newlineSeparated);

    // Returns {"modules":["notes","keycard",...]} or {"modules":[]}.
    Q_INVOKABLE QString getWatchedModules() const;

    // Poll all watched modules: call getFileForStash(), upload found files,
    // and call setBackupCid(cid, timestamp) on each module that returned a file.
    // Returns {"checked":N,"queued":M} or {"error":"..."}.
    Q_INVOKABLE QString checkAll();

    // Upload a file via local IPFS daemon. Returns {"cid":"..."} or {"error":"..."}.
    Q_INVOKABLE QString uploadViaIpfs(const QString& filePath);

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

    StashBackend  m_backend;
    QStringList   m_watchedModules;
    QTimer        m_pollTimer;   // auto-calls checkAll() on interval
};
