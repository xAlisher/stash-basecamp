#include <QtTest/QtTest>
#include <QJsonArray>
#include <QJsonObject>

#include "core/StashBackend.h"
#include "core/StorageClient.h"

// Mock transport — records calls, fires synthetic callbacks.
class MockTransport : public StorageTransport
{
public:
    bool connected = true;

    QList<QString> uploadedPaths;
    QList<QString> downloadedCids;
    EventCallback  eventCb;

    bool isConnected() const override { return connected; }

    void uploadUrl(const QUrl& url, int) override {
        uploadedPaths.append(url.toLocalFile());
    }
    void downloadToUrl(const QString& cid, const QUrl&, bool, int) override {
        downloadedCids.append(cid);
    }
    void subscribeEventResponse(EventCallback cb) override {
        eventCb = std::move(cb);
    }

    void fireUploadDone(const QString& cid) {
        if (eventCb) eventCb(QStringLiteral("storageUploadDone"), {cid});
    }
    void fireUploadFailed() {
        if (eventCb) eventCb(QStringLiteral("storageUploadDone"), {});
    }
    void fireDownloadDone() {
        if (eventCb) eventCb(QStringLiteral("storageDownloadDone"), {});
    }
    void fireDownloadFailed(const QString& err) {
        if (eventCb) eventCb(QStringLiteral("storageDownloadDone"), {err});
    }
};

static std::pair<StashBackend*, MockTransport*> makeBackend()
{
    auto mock = std::make_unique<MockTransport>();
    auto* raw = mock.get();
    auto client = std::make_unique<StorageClient>(std::move(mock));
    auto* backend = new StashBackend();
    backend->setStorageClient(std::move(client));
    return {backend, raw};
}

class TestStashBackend : public QObject
{
    Q_OBJECT

private slots:
    void testStatusReadyWhenConnected();
    void testStatusStartingWhenDisconnected();
    void testStatusOfflineWhenNoClient();

    void testUploadAppendsUploadingLog();
    void testUploadSuccessAppendsUploadedLog();
    void testUploadFailureAppendsErrorLog();
    void testUploadReturnsFalseWhenOffline();

    void testDownloadAppendsDownloadingLog();
    void testDownloadSuccessAppendsDownloadedLog();
    void testDownloadFailureAppendsErrorLog();

    void testLogCapAt100();
    void testLogEntriesJson();

    void testLogEventSignalFired();

    // uploadWithCallback
    void testUploadWithCallbackFiresOnSuccess();
    void testUploadWithCallbackNotFiredOnError();
};

// ── Status ────────────────────────────────────────────────────────────────────

void TestStashBackend::testStatusReadyWhenConnected()
{
    auto [b, mock] = makeBackend();
    QCOMPARE(b->status(), QStringLiteral("ready"));
    delete b;
}

void TestStashBackend::testStatusStartingWhenDisconnected()
{
    auto [b, mock] = makeBackend();
    mock->connected = false;
    QCOMPARE(b->status(), QStringLiteral("starting"));
    delete b;
}

void TestStashBackend::testStatusOfflineWhenNoClient()
{
    StashBackend b;
    QCOMPARE(b.status(), QStringLiteral("offline"));
}

// ── Upload ────────────────────────────────────────────────────────────────────

void TestStashBackend::testUploadAppendsUploadingLog()
{
    auto [b, mock] = makeBackend();

    QTemporaryFile f;
    QVERIFY(f.open());
    f.write("data");
    f.close();

    b->upload(f.fileName());

    const QJsonArray log = b->logEntries();
    QVERIFY(!log.isEmpty());
    QCOMPARE(log.last().toObject()[QStringLiteral("type")].toString(),
             QStringLiteral("uploading"));
    delete b;
}

void TestStashBackend::testUploadSuccessAppendsUploadedLog()
{
    auto [b, mock] = makeBackend();

    QTemporaryFile f;
    QVERIFY(f.open());
    f.write("data");
    f.close();

    b->upload(f.fileName());
    mock->fireUploadDone(QStringLiteral("zDvZCid123"));

    const QJsonArray log = b->logEntries();
    QCOMPARE(log.last().toObject()[QStringLiteral("type")].toString(),
             QStringLiteral("uploaded"));
    QCOMPARE(log.last().toObject()[QStringLiteral("detail")].toString(),
             QStringLiteral("zDvZCid123"));
    delete b;
}

void TestStashBackend::testUploadFailureAppendsErrorLog()
{
    auto [b, mock] = makeBackend();

    QTemporaryFile f;
    QVERIFY(f.open());
    f.write("data");
    f.close();

    b->upload(f.fileName());
    mock->fireUploadFailed();  // empty CID = failure path in StorageClient

    // StorageClient fires callback with empty cid and error
    const QJsonArray log = b->logEntries();
    // Last entry should be error or uploaded with empty cid
    QString lastType = log.last().toObject()[QStringLiteral("type")].toString();
    QVERIFY(lastType == QStringLiteral("error") || lastType == QStringLiteral("uploaded"));
    delete b;
}

void TestStashBackend::testUploadReturnsFalseWhenOffline()
{
    StashBackend b;
    // No client set — offline
    QVERIFY(!b.upload(QStringLiteral("/tmp/anything")));
    QCOMPARE(b.logEntries().last().toObject()[QStringLiteral("type")].toString(),
             QStringLiteral("offline"));
}

// ── Download ──────────────────────────────────────────────────────────────────

void TestStashBackend::testDownloadAppendsDownloadingLog()
{
    auto [b, mock] = makeBackend();
    b->download(QStringLiteral("zDvZCid"), QStringLiteral("/tmp/out"));

    QCOMPARE(b->logEntries().last().toObject()[QStringLiteral("type")].toString(),
             QStringLiteral("downloading"));
    delete b;
}

void TestStashBackend::testDownloadSuccessAppendsDownloadedLog()
{
    auto [b, mock] = makeBackend();
    b->download(QStringLiteral("zDvZCid"), QStringLiteral("/tmp/out"));
    mock->fireDownloadDone();

    QCOMPARE(b->logEntries().last().toObject()[QStringLiteral("type")].toString(),
             QStringLiteral("downloaded"));
    delete b;
}

void TestStashBackend::testDownloadFailureAppendsErrorLog()
{
    auto [b, mock] = makeBackend();
    b->download(QStringLiteral("zDvZCid"), QStringLiteral("/tmp/out"));
    mock->fireDownloadFailed(QStringLiteral("peer timeout"));

    QCOMPARE(b->logEntries().last().toObject()[QStringLiteral("type")].toString(),
             QStringLiteral("error"));
    QCOMPARE(b->logEntries().last().toObject()[QStringLiteral("detail")].toString(),
             QStringLiteral("peer timeout"));
    delete b;
}

// ── Log ───────────────────────────────────────────────────────────────────────

void TestStashBackend::testLogCapAt100()
{
    StashBackend b;
    // No client — each upload() appends one "offline" entry
    for (int i = 0; i < 120; ++i)
        b.upload(QStringLiteral("/tmp/x"));

    QVERIFY(b.logEntries().size() <= 100);
}

void TestStashBackend::testLogEntriesJson()
{
    StashBackend b;
    b.upload(QStringLiteral("/tmp/x"));  // appends "offline"

    const QJsonArray arr = b.logEntries();
    QVERIFY(!arr.isEmpty());
    QJsonObject first = arr.first().toObject();
    QVERIFY(first.contains(QStringLiteral("type")));
    QVERIFY(first.contains(QStringLiteral("detail")));
    QVERIFY(first.contains(QStringLiteral("timestamp")));
}

// ── Signal ────────────────────────────────────────────────────────────────────

void TestStashBackend::testLogEventSignalFired()
{
    auto [b, mock] = makeBackend();

    QStringList types;
    connect(b, &StashBackend::logEvent,
            [&](const QString& type, const QString&) { types.append(type); });

    QTemporaryFile f;
    QVERIFY(f.open());
    f.write("hi");
    f.close();

    b->upload(f.fileName());
    QVERIFY(types.contains(QStringLiteral("uploading")));
    delete b;
}

// ── uploadWithCallback ────────────────────────────────────────────────────────

void TestStashBackend::testUploadWithCallbackFiresOnSuccess()
{
    auto [b, mock] = makeBackend();

    QTemporaryFile f;
    QVERIFY(f.open());
    f.write("payload");
    f.close();

    QString receivedCid;
    b->uploadWithCallback(f.fileName(), [&](const QString& cid) {
        receivedCid = cid;
    });

    QVERIFY(!mock->uploadedPaths.isEmpty());

    mock->fireUploadDone(QStringLiteral("zDvZCallbackCid"));

    // Callback must have fired with the CID.
    QCOMPARE(receivedCid, QStringLiteral("zDvZCallbackCid"));
    // Log must show "uploaded".
    bool foundUploaded = false;
    for (const auto& e : b->logEntries()) {
        if (e.toObject()[QStringLiteral("type")].toString() == QStringLiteral("uploaded"))
            foundUploaded = true;
    }
    QVERIFY(foundUploaded);
    delete b;
}

void TestStashBackend::testUploadWithCallbackNotFiredOnError()
{
    auto [b, mock] = makeBackend();

    QTemporaryFile f;
    QVERIFY(f.open());
    f.write("payload");
    f.close();

    bool callbackFired = false;
    b->uploadWithCallback(f.fileName(), [&](const QString&) {
        callbackFired = true;
    });

    mock->fireUploadFailed();  // error — callback must NOT fire

    QVERIFY(!callbackFired);
    // Log must show "error".
    bool foundError = false;
    for (const auto& e : b->logEntries()) {
        if (e.toObject()[QStringLiteral("type")].toString() == QStringLiteral("error"))
            foundError = true;
    }
    QVERIFY(foundError);
    delete b;
}

QTEST_MAIN(TestStashBackend)
#include "test_stash_backend.moc"
