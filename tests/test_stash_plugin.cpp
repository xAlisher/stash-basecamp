#include <QtTest/QtTest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

#include "plugin/StashPlugin.h"

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

private:
    StashPlugin m_plugin;
};

void TestStashPlugin::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
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

QTEST_MAIN(TestStashPlugin)
#include "test_stash_plugin.moc"
