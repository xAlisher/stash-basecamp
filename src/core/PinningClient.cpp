#include "PinningClient.h"

#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QHttpMultiPart>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>

PinningClient::PinningClient(QObject* parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
{
}

QString PinningClient::pinFile(PinningProvider provider,
                                const QString&  endpoint,
                                const QString&  token,
                                const QString&  filePath,
                                QString&        errorOut,
                                int             timeoutMs)
{
    if (provider == PinningProvider::Pinata)
        return pinViaPinata(token, filePath, errorOut, timeoutMs);
    else
        return pinViaKubo(endpoint, token, filePath, errorOut, timeoutMs);
}

QString PinningClient::pinViaPinata(const QString& token,
                                     const QString& filePath,
                                     QString&       errorOut,
                                     int            timeoutMs)
{
    QFile* file = new QFile(filePath);
    if (!file->open(QIODevice::ReadOnly)) {
        errorOut = QStringLiteral("cannot open file: ") + filePath;
        delete file;
        return {};
    }

    auto* multiPart = new QHttpMultiPart(QHttpMultiPart::FormDataType);

    QHttpPart filePart;
    filePart.setHeader(QNetworkRequest::ContentDispositionHeader,
                       QStringLiteral("form-data; name=\"file\"; filename=\"") +
                       QFileInfo(filePath).fileName() + QStringLiteral("\""));
    filePart.setBodyDevice(file);
    file->setParent(multiPart);
    multiPart->append(filePart);

    // /pinning/pinFileToIPFS pins publicly to the IPFS network (files show
    // under PUBLIC tab in Pinata and are accessible via public gateways).
    QNetworkRequest req(QUrl(QStringLiteral("https://api.pinata.cloud/pinning/pinFileToIPFS")));
    req.setRawHeader("Authorization", ("Bearer " + token).toUtf8());

    QNetworkReply* reply = m_nam->post(req, multiPart);
    multiPart->setParent(reply);

    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    timer.start(timeoutMs);
    loop.exec();

    if (!reply->isFinished()) {
        reply->abort();
        reply->deleteLater();
        errorOut = QStringLiteral("request timed out");
        return {};
    }

    const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray body = reply->readAll();
    reply->deleteLater();

    if (httpStatus < 200 || httpStatus >= 300) {
        errorOut = QStringLiteral("HTTP ") + QString::number(httpStatus) +
                   QStringLiteral(": ") + QString::fromUtf8(body).left(200);
        return {};
    }

    const QJsonObject obj = QJsonDocument::fromJson(body).object();
    const QString cid = obj.value(QStringLiteral("IpfsHash")).toString();
    if (cid.isEmpty()) {
        errorOut = QStringLiteral("unexpected response: ") + QString::fromUtf8(body).left(200);
        return {};
    }
    return cid;
}

QString PinningClient::pinViaKubo(const QString& endpoint,
                                   const QString& token,
                                   const QString& filePath,
                                   QString&       errorOut,
                                   int            timeoutMs)
{
    QFile* file = new QFile(filePath);
    if (!file->open(QIODevice::ReadOnly)) {
        errorOut = QStringLiteral("cannot open file: ") + filePath;
        delete file;
        return {};
    }

    auto* multiPart = new QHttpMultiPart(QHttpMultiPart::FormDataType);

    QHttpPart filePart;
    filePart.setHeader(QNetworkRequest::ContentDispositionHeader,
                       QStringLiteral("form-data; name=\"file\"; filename=\"") +
                       QFileInfo(filePath).fileName() + QStringLiteral("\""));
    filePart.setBodyDevice(file);
    file->setParent(multiPart);
    multiPart->append(filePart);

    const QString url = endpoint + QStringLiteral("/api/v0/add");
    QNetworkRequest req{QUrl(url)};
    if (!token.isEmpty())
        req.setRawHeader("Authorization", ("Bearer " + token).toUtf8());

    QNetworkReply* reply = m_nam->post(req, multiPart);
    multiPart->setParent(reply);

    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    timer.start(timeoutMs);
    loop.exec();

    if (!reply->isFinished()) {
        reply->abort();
        reply->deleteLater();
        errorOut = QStringLiteral("request timed out");
        return {};
    }

    const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray body = reply->readAll();
    reply->deleteLater();

    if (httpStatus < 200 || httpStatus >= 300) {
        errorOut = QStringLiteral("HTTP ") + QString::number(httpStatus) +
                   QStringLiteral(": ") + QString::fromUtf8(body).left(200);
        return {};
    }

    const QJsonObject obj = QJsonDocument::fromJson(body).object();
    const QString cid = obj.value(QStringLiteral("Hash")).toString();
    if (cid.isEmpty()) {
        errorOut = QStringLiteral("unexpected response: ") + QString::fromUtf8(body).left(200);
        return {};
    }
    return cid;
}
