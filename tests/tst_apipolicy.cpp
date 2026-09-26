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
    void retryAfterParsing_data();
    void retryAfterParsing();
    void blockExtendsNeverShortens();
    void rateLimitMessages();
    void rateLimitDecisionUsesWholeBlock();
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
    QVERIFY(withHeader.rateLimited);
    const ApiDecision again = classifyReply(429, QString(), "3", QByteArray(), true);
    QCOMPARE(again.action, ApiDecision::Fail);
    QVERIFY(again.rateLimited);
    QCOMPARE(again.message, u"Spotify: rate limited, try again in 3 s"_s);
}

void TestApiPolicy::longRateLimitFailsFast()
{
    QCOMPARE(classifyReply(429, QString(), "10", QByteArray(), false).action, ApiDecision::RetryAfter);
    const ApiDecision longer = classifyReply(429, QString(), "11", QByteArray(), false);
    QCOMPARE(longer.action, ApiDecision::Fail);
    QCOMPARE(longer.retryAfterMs, 11000);
    QVERIFY(longer.rateLimited);
    QCOMPARE(classifyReply(429, QString(), "121", QByteArray(), false).message, u"Spotify: rate limited, try again in 3 min"_s);
    const ApiDecision missing = classifyReply(429, QString(), QByteArray(), QByteArray(), false);
    QCOMPARE(missing.action, ApiDecision::Fail);
    QCOMPARE(missing.retryAfterMs, 30000);
}

void TestApiPolicy::retryAfterParsing_data()
{
    QTest::addColumn<QByteArray>("header");
    QTest::addColumn<int>("seconds");
    QTest::newRow("seconds") << QByteArray("18540") << 18540;
    QTest::newRow("spaces") << QByteArray(" 7 ") << 7;
    QTest::newRow("zero") << QByteArray("0") << 0;
    QTest::newRow("missing") << QByteArray() << 30;
    QTest::newRow("garbage") << QByteArray("soon") << 30;
    QTest::newRow("http date") << QByteArray("Fri, 25 Sep 2026 23:00:00 GMT") << 30;
    QTest::newRow("negative") << QByteArray("-5") << 30;
}

void TestApiPolicy::retryAfterParsing()
{
    QFETCH(QByteArray, header);
    QFETCH(int, seconds);
    QCOMPARE(parseRetryAfterSeconds(header), seconds);
}

void TestApiPolicy::blockExtendsNeverShortens()
{
    const qint64 blocked = extendBlock(0, 1000000, 300);
    QCOMPARE(blocked, 1300000);
    QCOMPARE(extendBlock(blocked, 1001000, 5), blocked);
    QCOMPARE(extendBlock(blocked, 1001000, 600), 1601000);
    QVERIFY(isBlocked(blocked, 1299999));
    QVERIFY(!isBlocked(blocked, 1300000));
    QVERIFY(!isBlocked(0, 1000000));
}

void TestApiPolicy::rateLimitMessages()
{
    QCOMPARE(rateLimitMessage(1), u"Spotify: rate limited, try again in 1 s"_s);
    QCOMPARE(rateLimitMessage(59000), u"Spotify: rate limited, try again in 59 s"_s);
    QCOMPARE(rateLimitMessage(59001), u"Spotify: rate limited, try again in 1 min"_s);
    QCOMPARE(rateLimitMessage(18540000), u"Spotify: rate limited, try again in 309 min"_s);
    QVERIFY(isRateLimitMessage(rateLimitMessage(5000)));
    QVERIFY(!isRateLimitMessage(u"Spotify: request failed (HTTP 500)"_s));
}

void TestApiPolicy::rateLimitDecisionUsesWholeBlock()
{
    const ApiDecision shortWait = rateLimitDecision(10000, false);
    QCOMPARE(shortWait.action, ApiDecision::RetryAfter);
    QCOMPARE(shortWait.retryAfterMs, 10000);
    const ApiDecision longWait = rateLimitDecision(10001, false);
    QCOMPARE(longWait.action, ApiDecision::Fail);
    QVERIFY(longWait.rateLimited);
    QCOMPARE(longWait.message, u"Spotify: rate limited, try again in 11 s"_s);
    QCOMPARE(rateLimitDecision(2000, true).action, ApiDecision::Fail);
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
