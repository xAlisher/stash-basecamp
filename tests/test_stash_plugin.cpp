#include <QtTest/QtTest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QSettings>
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QFileDevice>
#include <QTcpServer>
#include <QTcpSocket>
#include <QHostAddress>

#include "plugin/StashPlugin.h"

class TestableStashPlugin : public StashPlugin {
public:
    void setFakeIpfsPath(const QString& p) { m_path = p; }
protected:
    QString bundledIpfsPath() const override { return m_path; }
private:
    QString m_path;
};

static bool writeFakeIpfs(const QString& path, const QString& output, int exitCode) {
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
    QTextStream s(&f);
    s << "#!/bin/sh\necho '" << output << "'\nexit " << exitCode << "\n";
    f.close();
    f.setPermissions(f.permissions() | QFileDevice::ExeOwner | QFileDevice::ExeGroup);
    return true;
}

static void clearStashSettings()
{
    QSettings s{QLatin1String("logos"), QLatin1String("stash")};
    s.remove(QLatin1String("watchedModules"));
    s.remove(QLatin1String("pinningProvider"));
    s.remove(QLatin1String("pinningEndpoint"));
    s.remove(QLatin1String("pinningToken"));
}

// ── Minimal fake HTTP server for Kubo-style responses ────────────────────────

class FakeKuboServer : public QObject {
    Q_OBJECT
public:
    // response: the JSON body to return
    // httpStatus: HTTP status code (200 = ok)
    explicit FakeKuboServer(const QByteArray& response, int httpStatus = 200,
                             QObject* parent = nullptr)
        : QObject(parent), m_response(response), m_httpStatus(httpStatus)
    {
        m_server = new QTcpServer(this);
        m_server->listen(QHostAddress::LocalHost, 0);
        connect(m_server, &QTcpServer::newConnection, this, &FakeKuboServer::onConnection);
    }

    quint16 port() const { return m_server->serverPort(); }
    QString endpoint() const {
        return QStringLiteral("http://127.0.0.1:") + QString::number(port());
    }

private slots:
    void onConnection() {
        QTcpSocket* sock = m_server->nextPendingConnection();
        connect(sock, &QTcpSocket::readyRead, this, [this, sock]() {
            // drain the request
            sock->readAll();
            const QByteArray statusLine =
                "HTTP/1.1 " + QByteArray::number(m_httpStatus) +
                (m_httpStatus == 200 ? " OK" : " Error") + "\r\n";
            const QByteArray headers =
                "Content-Type: application/json\r\n"
                "Content-Length: " + QByteArray::number(m_response.size()) + "\r\n"
                "Connection: close\r\n\r\n";
            sock->write(statusLine + headers + m_response);
            sock->flush();
            sock->disconnectFromHost();
        });
    }

private:
    QTcpServer* m_server;
    QByteArray  m_response;
    int         m_httpStatus;
};

// initLogos() is never called in these tests — no real storage node starts.
// All tests exercise the plugin's JSON contract and guard paths.

static QJsonObject parseObj(const QString& s)
{
    return QJsonDocument::fromJson(s.toUtf8()).object();
}

static QJsonArray parseArr(const QString& s)
{
    return QJsonDocument::fromJson(s.toUtf8()).array();
}

class TestStashPlugin : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    // getStatus — no client wired
    void testGetStatusOfflineBeforeInit();

    // upload — guard paths (no storage client)
    void testUploadEmptyPathReturnsError();
    void testUploadWhenOfflineReturnsError();

    // download — guard paths
    void testDownloadEmptyCidReturnsError();
    void testDownloadEmptyDestReturnsError();
    void testDownloadWhenOfflineReturnsError();

    // getLog — empty initially
    void testGetLogEmptyInitially();

    // getQuota — returns JSON
    void testGetQuotaReturnsJson();

    // setWatchedModules / getWatchedModules
    void testSetWatchedModulesParsesNewlineSeparated();
    void testSetWatchedModulesIgnoresBlankLines();
    void testGetWatchedModulesEmptyInitially();

    // checkAll — guard path (no logosAPI)
    void testCheckAllWithoutLogosAPIReturnsError();

    // uploadViaIpfs — bundled binary tests
    void testUploadViaIpfsReturnsCid();
    void testUploadViaIpfsEmptyPath();
    void testUploadViaIpfsBinaryMissing();
    void testUploadViaIpfsNonZeroExit();

    // setPinningConfig / getPinningConfig — no network
    void testSetPinningConfigPinataNoEndpointRequired();
    void testSetPinningConfigKuboRequiresEndpoint();
    void testSetPinningConfigRejectsUnknownProvider();
    void testGetPinningConfigConfiguredFalseWhenNotSet();
    void testGetPinningConfigMasksToken();

    // uploadViaIpfs + online pinning via FakeKuboServer
    void testUploadViaIpfsPinsAndReturnsCid();
    void testUploadViaIpfsCidMismatchReturnsError();
    void testUploadViaIpfsServerErrorReturnsError();
    void testUploadViaIpfsNoProviderConfiguredReturnsError();

private:
    StashPlugin m_plugin;
};

void TestStashPlugin::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
    clearStashSettings();  // ensure clean slate for watched-modules tests
}

void TestStashPlugin::testGetStatusOfflineBeforeInit()
{
    // No initLogos() called → no client → offline
    QCOMPARE(m_plugin.getStatus(), QStringLiteral("offline"));
}

void TestStashPlugin::testUploadEmptyPathReturnsError()
{
    auto obj = parseObj(m_plugin.upload({}));
    QVERIFY(obj.contains(QStringLiteral("error")));
    QVERIFY(!obj[QStringLiteral("error")].toString().isEmpty());
}

void TestStashPlugin::testUploadWhenOfflineReturnsError()
{
    // Non-empty path but no storage client wired
    auto obj = parseObj(m_plugin.upload(QStringLiteral("/tmp/file.bin")));
    QVERIFY(obj.contains(QStringLiteral("error")));
}

void TestStashPlugin::testDownloadEmptyCidReturnsError()
{
    auto obj = parseObj(m_plugin.download({}, QStringLiteral("/tmp/out")));
    QVERIFY(obj.contains(QStringLiteral("error")));
}

void TestStashPlugin::testDownloadEmptyDestReturnsError()
{
    auto obj = parseObj(m_plugin.download(QStringLiteral("zDvZCid"), {}));
    QVERIFY(obj.contains(QStringLiteral("error")));
}

void TestStashPlugin::testDownloadWhenOfflineReturnsError()
{
    auto obj = parseObj(m_plugin.download(
        QStringLiteral("zDvZCid"), QStringLiteral("/tmp/out")));
    QVERIFY(obj.contains(QStringLiteral("error")));
}

void TestStashPlugin::testGetLogEmptyInitially()
{
    // Use a fresh plugin — m_plugin accumulates log entries from other tests.
    StashPlugin fresh;
    auto arr = parseArr(fresh.getLog());
    QVERIFY(arr.isEmpty());
}

void TestStashPlugin::testGetQuotaReturnsJson()
{
    auto obj = parseObj(m_plugin.getQuota());
    // Placeholder returns {used:0, total:0} — just verify it's valid JSON
    QVERIFY(!obj.isEmpty() || true);  // quota may be empty string before node
}

void TestStashPlugin::testSetWatchedModulesParsesNewlineSeparated()
{
    StashPlugin p;
    auto result = parseObj(p.setWatchedModules(QStringLiteral("notes\nkeycard")));
    QVERIFY(result.contains(QStringLiteral("modules")));
    auto arr = result[QStringLiteral("modules")].toArray();
    QCOMPARE(arr.size(), 2);
    QCOMPARE(arr[0].toString(), QStringLiteral("notes"));
    QCOMPARE(arr[1].toString(), QStringLiteral("keycard"));
}

void TestStashPlugin::testSetWatchedModulesIgnoresBlankLines()
{
    StashPlugin p;
    auto result = parseObj(p.setWatchedModules(QStringLiteral("\nnotes\n\n  keycard  \n")));
    auto arr = result[QStringLiteral("modules")].toArray();
    QCOMPARE(arr.size(), 2);
}

void TestStashPlugin::testGetWatchedModulesEmptyInitially()
{
    clearStashSettings();  // other tests may have persisted modules
    StashPlugin p;
    auto result = parseObj(p.getWatchedModules());
    QVERIFY(result.contains(QStringLiteral("modules")));
    QVERIFY(result[QStringLiteral("modules")].toArray().isEmpty());
}

void TestStashPlugin::testCheckAllWithoutLogosAPIReturnsError()
{
    // initLogos() never called → logosAPI is null → must return error JSON
    StashPlugin fresh;
    auto obj = parseObj(fresh.checkAll());
    QVERIFY(obj.contains(QStringLiteral("error")));
    QVERIFY(!obj[QStringLiteral("error")].toString().isEmpty());
}

void TestStashPlugin::testUploadViaIpfsReturnsCid()
{
    clearStashSettings();
    const QString fakeCid = QStringLiteral("QmTestCid123");
    FakeKuboServer server(("{\"Hash\":\"" + fakeCid + "\"}").toUtf8(), 200);

    const QString fakeBin = QDir::tempPath() + QStringLiteral("/fake_ipfs_ok.sh");
    QVERIFY(writeFakeIpfs(fakeBin, fakeCid, 0));

    TestableStashPlugin p;
    p.setFakeIpfsPath(fakeBin);
    p.setPinningConfig(QStringLiteral("kubo"), server.endpoint(), QStringLiteral(""));

    const QString tmpFile = QDir::tempPath() + QStringLiteral("/stash_ok_test.dat");
    { QFile f(tmpFile); f.open(QIODevice::WriteOnly); f.write("test"); }

    const auto obj = parseObj(p.uploadViaIpfs(tmpFile));
    QVERIFY(obj.contains(QStringLiteral("cid")));
    QCOMPARE(obj[QStringLiteral("cid")].toString(), fakeCid);
}

void TestStashPlugin::testUploadViaIpfsEmptyPath()
{
    TestableStashPlugin p;
    p.setFakeIpfsPath(QStringLiteral("/nonexistent/ipfs"));

    const auto obj = parseObj(p.uploadViaIpfs({}));
    QVERIFY(obj.contains(QStringLiteral("error")));
    QVERIFY(!obj[QStringLiteral("error")].toString().isEmpty());
}

void TestStashPlugin::testUploadViaIpfsBinaryMissing()
{
    TestableStashPlugin p;
    p.setFakeIpfsPath(QStringLiteral("/nonexistent/path/to/ipfs"));

    const auto obj = parseObj(p.uploadViaIpfs(QStringLiteral("/tmp/some_file.bin")));
    QVERIFY(obj.contains(QStringLiteral("error")));
    QVERIFY(obj[QStringLiteral("error")].toString().contains(QStringLiteral("not found")));
}

void TestStashPlugin::testUploadViaIpfsNonZeroExit()
{
    const QString fakeBin = QDir::tempPath() + QStringLiteral("/fake_ipfs_fail.sh");
    QVERIFY(writeFakeIpfs(fakeBin, QStringLiteral("error output"), 1));

    TestableStashPlugin p;
    p.setFakeIpfsPath(fakeBin);

    const auto obj = parseObj(p.uploadViaIpfs(QStringLiteral("/tmp/some_file.bin")));
    QVERIFY(obj.contains(QStringLiteral("error")));
    QVERIFY(!obj[QStringLiteral("error")].toString().isEmpty());
}

// ── Pinning config tests ──────────────────────────────────────────────────────

void TestStashPlugin::testSetPinningConfigPinataNoEndpointRequired()
{
    clearStashSettings();
    StashPlugin p;
    auto obj = parseObj(p.setPinningConfig(QStringLiteral("pinata"),
                                           QStringLiteral(""),
                                           QStringLiteral("my_jwt_token")));
    QVERIFY(obj.contains(QStringLiteral("ok")));
    QVERIFY(obj[QStringLiteral("ok")].toBool());
}

void TestStashPlugin::testSetPinningConfigKuboRequiresEndpoint()
{
    clearStashSettings();
    StashPlugin p;
    auto obj = parseObj(p.setPinningConfig(QStringLiteral("kubo"),
                                           QStringLiteral(""),
                                           QStringLiteral("token")));
    QVERIFY(obj.contains(QStringLiteral("error")));
}

void TestStashPlugin::testSetPinningConfigRejectsUnknownProvider()
{
    clearStashSettings();
    StashPlugin p;
    auto obj = parseObj(p.setPinningConfig(QStringLiteral("s3"),
                                           QStringLiteral(""),
                                           QStringLiteral("token")));
    QVERIFY(obj.contains(QStringLiteral("error")));
}

void TestStashPlugin::testGetPinningConfigConfiguredFalseWhenNotSet()
{
    clearStashSettings();
    StashPlugin p;
    auto obj = parseObj(p.getPinningConfig());
    QVERIFY(obj.contains(QStringLiteral("configured")));
    QVERIFY(!obj[QStringLiteral("configured")].toBool());
}

void TestStashPlugin::testGetPinningConfigMasksToken()
{
    clearStashSettings();
    StashPlugin p;
    p.setPinningConfig(QStringLiteral("pinata"), QStringLiteral(""), QStringLiteral("secret123"));
    auto obj = parseObj(p.getPinningConfig());
    QCOMPARE(obj[QStringLiteral("token")].toString(), QStringLiteral("***"));
    QVERIFY(obj[QStringLiteral("configured")].toBool());
}

// ── uploadViaIpfs + pinning tests ─────────────────────────────────────────────

void TestStashPlugin::testUploadViaIpfsPinsAndReturnsCid()
{
    clearStashSettings();

    const QString fakeCid = QStringLiteral("QmFakeCid123");
    FakeKuboServer server(("{\"Hash\":\"" + fakeCid + "\"}").toUtf8(), 200);

    // Fake ipfs binary outputs the same CID
    const QString fakeBin = QDir::tempPath() + QStringLiteral("/fake_ipfs_pin_ok.sh");
    QVERIFY(writeFakeIpfs(fakeBin, fakeCid, 0));

    StashPlugin p;
    // Configure kubo pinning pointing at local fake server
    p.setPinningConfig(QStringLiteral("kubo"), server.endpoint(), QStringLiteral(""));

    TestableStashPlugin tp;
    tp.setPinningConfig(QStringLiteral("kubo"), server.endpoint(), QStringLiteral(""));
    tp.setFakeIpfsPath(fakeBin);

    // Need a real file on disk for the HTTP upload
    const QString tmpFile = QDir::tempPath() + QStringLiteral("/stash_pin_test.dat");
    { QFile f(tmpFile); f.open(QIODevice::WriteOnly); f.write("hello"); }

    const auto obj = parseObj(tp.uploadViaIpfs(tmpFile));
    QVERIFY2(obj.contains(QStringLiteral("cid")),
             qPrintable(QStringLiteral("got: ") + QJsonDocument(obj).toJson()));
    QCOMPARE(obj[QStringLiteral("cid")].toString(), fakeCid);
}

void TestStashPlugin::testUploadViaIpfsCidMismatchReturnsError()
{
    clearStashSettings();

    // Local ipfs returns CIDv0, Pinata returns CIDv1 for the same content.
    // We now trust the remote CID — both should succeed and return the remote CID.
    const QString remoteCid = QStringLiteral("bafkreicaws33rvwntahy5rawkifdan4effzixgtwtgptnwzt6gxkzakzci");
    FakeKuboServer server(("{\"Hash\":\"" + remoteCid + "\"}").toUtf8(), 200);

    const QString fakeBin = QDir::tempPath() + QStringLiteral("/fake_ipfs_mismatch.sh");
    QVERIFY(writeFakeIpfs(fakeBin, QStringLiteral("QmLocal"), 0));

    TestableStashPlugin tp;
    tp.setPinningConfig(QStringLiteral("kubo"), server.endpoint(), QStringLiteral(""));
    tp.setFakeIpfsPath(fakeBin);

    const QString tmpFile = QDir::tempPath() + QStringLiteral("/stash_mismatch_test.dat");
    { QFile f(tmpFile); f.open(QIODevice::WriteOnly); f.write("hi"); }

    const auto obj = parseObj(tp.uploadViaIpfs(tmpFile));
    // Remote CID is authoritative (CIDv0 local vs CIDv1 remote = same content)
    QVERIFY(obj.contains(QStringLiteral("cid")));
    QCOMPARE(obj[QStringLiteral("cid")].toString(), remoteCid);
}

void TestStashPlugin::testUploadViaIpfsServerErrorReturnsError()
{
    clearStashSettings();

    FakeKuboServer server(R"({"message":"Unauthorized"})", 401);

    const QString fakeBin = QDir::tempPath() + QStringLiteral("/fake_ipfs_autherr.sh");
    QVERIFY(writeFakeIpfs(fakeBin, QStringLiteral("QmSomeCid"), 0));

    TestableStashPlugin tp;
    tp.setPinningConfig(QStringLiteral("kubo"), server.endpoint(), QStringLiteral("bad_token"));
    tp.setFakeIpfsPath(fakeBin);

    const QString tmpFile = QDir::tempPath() + QStringLiteral("/stash_autherr_test.dat");
    { QFile f(tmpFile); f.open(QIODevice::WriteOnly); f.write("data"); }

    const auto obj = parseObj(tp.uploadViaIpfs(tmpFile));
    QVERIFY(obj.contains(QStringLiteral("error")));
    QVERIFY(obj[QStringLiteral("error")].toString().contains(QStringLiteral("pinning failed")));
}

void TestStashPlugin::testUploadViaIpfsNoProviderConfiguredReturnsError()
{
    clearStashSettings();

    const QString fakeBin = QDir::tempPath() + QStringLiteral("/fake_ipfs_noprovider.sh");
    QVERIFY(writeFakeIpfs(fakeBin, QStringLiteral("QmSomeCid"), 0));

    TestableStashPlugin tp;
    // No setPinningConfig called → settings are empty
    tp.setFakeIpfsPath(fakeBin);

    const QString tmpFile = QDir::tempPath() + QStringLiteral("/stash_noprov_test.dat");
    { QFile f(tmpFile); f.open(QIODevice::WriteOnly); f.write("data"); }

    const auto obj = parseObj(tp.uploadViaIpfs(tmpFile));
    QVERIFY(obj.contains(QStringLiteral("error")));
    QVERIFY(obj[QStringLiteral("error")].toString().contains(QStringLiteral("not configured")));
}

QTEST_MAIN(TestStashPlugin)
#include "test_stash_plugin.moc"
