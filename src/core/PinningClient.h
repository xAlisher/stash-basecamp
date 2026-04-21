#pragma once
#include <QObject>
#include <QString>

class QNetworkAccessManager;

enum class PinningProvider { Pinata, Kubo };

class PinningClient : public QObject {
    Q_OBJECT
public:
    explicit PinningClient(QObject* parent = nullptr);

    // Upload file to provider. Returns remote CID on success, empty on failure.
    QString pinFile(PinningProvider provider,
                    const QString&  endpoint,
                    const QString&  token,
                    const QString&  filePath,
                    QString&        errorOut,
                    int             timeoutMs = 60000);

private:
    QString pinViaPinata(const QString& token, const QString& filePath,
                         QString& errorOut, int timeoutMs);
    QString pinViaKubo  (const QString& endpoint, const QString& token,
                         const QString& filePath, QString& errorOut, int timeoutMs);

    QNetworkAccessManager* m_nam;
};
