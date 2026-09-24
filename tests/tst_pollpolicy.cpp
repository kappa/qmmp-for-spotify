#include <QtTest>
#include "pollpolicy.h"

class TestPollPolicy : public QObject
{
    Q_OBJECT
private slots:
    void intervals();
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

QTEST_APPLESS_MAIN(TestPollPolicy)
#include "tst_pollpolicy.moc"
