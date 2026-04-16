#pragma once

#include <QObject>
#include <QString>
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

    // "ready" | "starting" | "offline"
    Q_INVOKABLE QString getStatus();

    // Recent log entries as JSON array [{type,detail,timestamp}].
    Q_INVOKABLE QString getLog();

    // Storage space: {"used":N,"total":N} or {"error":"..."}.
    Q_INVOKABLE QString getQuota();

signals:
    void eventResponse(const QString& eventName, const QVariantList& data);

private:
    static QString okJson();
    static QString queuedJson();
    static QString errorJson(const QString& msg);

    StashBackend m_backend;
};
