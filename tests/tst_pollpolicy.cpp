#include <QtTest>
#include "pollpolicy.h"

class TestPollPolicy : public QObject
{
    Q_OBJECT
private slots:
    void intervals();
    void waitsForRateLimitBlock();
};

void TestPollPolicy::intervals()
{
    QCOMPARE(pollIntervalMs(true, false, 0), 2000);
    QCOMPARE(pollIntervalMs(false, true, 0), 2000);
    QCOMPARE(pollIntervalMs(false, false, 0), 10000);
    QCOMPARE(pollIntervalMs(true, true, 1), 2000);
    QCOMPARE(pollIntervalMs(true, true, 3), 8000);
    QCOMPARE(pollIntervalMs(false, false, 10), 30000);
}

void TestPollPolicy::waitsForRateLimitBlock()
{
    QCOMPARE(rateLimitedPollDelayMs(1300000, 1000000), 301000);
    QCOMPARE(rateLimitedPollDelayMs(1000000, 1300000), 1000);
    QCOMPARE(rateLimitedPollDelayMs(Q_INT64_C(1) << 40, 0), std::numeric_limits<int>::max());
}

QTEST_APPLESS_MAIN(TestPollPolicy)
#include "tst_pollpolicy.moc"
