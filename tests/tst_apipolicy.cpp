#include <QtTest>
#include "apipolicy.h"

using namespace Qt::StringLiterals;

class TestApiPolicy : public QObject
{
    Q_OBJECT
private slots:
    void success();
    void unauthorizedRefreshesOnce();
    void rateLimitHonoursRetryAfter();
    void longRateLimitFailsFast();
    void noActiveDevice();
    void passesSpotifyMessage();
    void volumeControlDisallowed();
    void networkError();
    void pauseOfPausedDeviceIsHarmless();
    void systemicPlayFailures_data();
    void systemicPlayFailures();
};

void TestApiPolicy::success()
{
    QCOMPARE(classifyReply(204, QString(), QByteArray(), QByteArray(), false).action, ApiDecision::Success);
}

void TestApiPolicy::unauthorizedRefreshesOnce()
{
    QCOMPARE(classifyReply(401, QString(), QByteArray(), QByteArray(), false).action, ApiDecision::RefreshAndRetry);
    const ApiDecision second = classifyReply(401, QString(), QByteArray(), QByteArray(), true);
    QCOMPARE(second.action, ApiDecision::Fail);
    QVERIFY(second.message.contains(u"log in again"_s));
}

void TestApiPolicy::rateLimitHonoursRetryAfter()
{
    const ApiDecision withHeader = classifyReply(429, QString(), "3", QByteArray(), false);
    QCOMPARE(withHeader.action, ApiDecision::RetryAfter);
    QCOMPARE(withHeader.retryAfterMs, 3000);
    QCOMPARE(classifyReply(429, QString(), QByteArray(), QByteArray(), false).retryAfterMs, 5000);
    QCOMPARE(classifyReply(429, QString(), "3", QByteArray(), true).action, ApiDecision::Fail);
}

void TestApiPolicy::longRateLimitFailsFast()
{
    QCOMPARE(classifyReply(429, QString(), "10", QByteArray(), false).action, ApiDecision::RetryAfter);
    const ApiDecision minute = classifyReply(429, QString(), "11", QByteArray(), false);
    QCOMPARE(minute.action, ApiDecision::Fail);
    QCOMPARE(minute.message, u"Spotify: rate limited, try again in 1 min"_s);
    QCOMPARE(classifyReply(429, QString(), "121", QByteArray(), false).message, u"Spotify: rate limited, try again in 3 min"_s);
}

void TestApiPolicy::noActiveDevice()
{
    const ApiDecision d = classifyReply(404, QString(), QByteArray(),
        R"({"error":{"status":404,"message":"Player command failed: No active device found","reason":"NO_ACTIVE_DEVICE"}})", false);
    QCOMPARE(d.action, ApiDecision::Fail);
    QVERIFY(d.deviceProblem);
    QVERIFY(d.message.contains(u"Device"_s));
}

void TestApiPolicy::passesSpotifyMessage()
{
    const ApiDecision d = classifyReply(403, QString(), QByteArray(),
        R"({"error":{"status":403,"message":"Player command failed: Premium required","reason":"PREMIUM_REQUIRED"}})", false);
    QCOMPARE(d.message, u"Spotify: Player command failed: Premium required"_s);
    QVERIFY(!d.deviceProblem);
}

void TestApiPolicy::volumeControlDisallowed()
{
    const ApiDecision d = classifyReply(403, QString(), QByteArray(),
        R"({"error":{"status":403,"message":"Player command failed: Cannot control device volume","reason":"VOLUME_CONTROL_DISALLOW"}})", false);
    QCOMPARE(d.action, ApiDecision::Fail);
    QCOMPARE(d.reason, u"VOLUME_CONTROL_DISALLOW"_s);
    QCOMPARE(d.message, u"Spotify: this device does not allow volume control"_s);
    QVERIFY(!d.deviceProblem);
}

void TestApiPolicy::networkError()
{
    const ApiDecision d = classifyReply(0, u"Host not found"_s, QByteArray(), QByteArray(), false);
    QCOMPARE(d.action, ApiDecision::Fail);
    QVERIFY(d.message.contains(u"Host not found"_s));
}

void TestApiPolicy::pauseOfPausedDeviceIsHarmless()
{
    QVERIFY(isAlreadyPausedReply(403, u"UNKNOWN"_s));
    QVERIFY(isAlreadyPausedReply(403, u"ALREADY_PAUSED"_s));
    QVERIFY(!isAlreadyPausedReply(403, u"PREMIUM_REQUIRED"_s));
    QVERIFY(!isAlreadyPausedReply(404, u"UNKNOWN"_s));
}

void TestApiPolicy::systemicPlayFailures_data()
{
    QTest::addColumn<int>("status");
    QTest::addColumn<bool>("deviceProblem");
    QTest::addColumn<QString>("reason");
    QTest::addColumn<bool>("systemic");
    QTest::newRow("network or not logged in") << 0 << false << QString() << true;
    QTest::newRow("session expired") << 401 << false << QString() << true;
    QTest::newRow("rate limited") << 429 << false << QString() << true;
    QTest::newRow("server error") << 500 << false << QString() << true;
    QTest::newRow("bad gateway") << 502 << false << QString() << true;
    QTest::newRow("no active device") << 404 << true << u"NO_ACTIVE_DEVICE"_s << true;
    QTest::newRow("device not found") << 404 << true << QString() << true;
    QTest::newRow("premium required") << 403 << false << u"PREMIUM_REQUIRED"_s << true;
    QTest::newRow("restriction violated") << 403 << false << u"UNKNOWN"_s << false;
    QTest::newRow("forbidden") << 403 << false << QString() << false;
    QTest::newRow("not found") << 404 << false << QString() << false;
    QTest::newRow("bad request") << 400 << false << QString() << false;
}

void TestApiPolicy::systemicPlayFailures()
{
    QFETCH(int, status);
    QFETCH(bool, deviceProblem);
    QFETCH(QString, reason);
    QFETCH(bool, systemic);
    QCOMPARE(isSystemicPlayFailure(status, deviceProblem, reason), systemic);
}

QTEST_APPLESS_MAIN(TestApiPolicy)
#include "tst_apipolicy.moc"
