#include <QtTest>
#include "requestbudget.h"

class TestRequestBudget : public QObject
{
    Q_OBJECT
private slots:
    void allowsThirtyPerWindow();
    void windowRolls();
    void refusalsDoNotCount();
    void bulkLeavesRoomForOthers();
    void bulkWaitsForASlot();
    void othersWaitOnlyForTheTotal();
};

void TestRequestBudget::allowsThirtyPerWindow()
{
    RequestBudget budget;
    for(int i = 0; i < 30; i++)
        QVERIFY(budget.take(1000 + i));
    QVERIFY(!budget.take(1030));
    QVERIFY(!budget.take(10999));
    QVERIFY(budget.take(11000));
    QVERIFY(!budget.take(11000));
}

void TestRequestBudget::windowRolls()
{
    RequestBudget budget;
    for(int i = 0; i < 30; i++)
        QVERIFY(budget.take(i * 300));
    QVERIFY(!budget.take(9999));
    QVERIFY(budget.take(10000));
    QVERIFY(!budget.take(10299));
    QVERIFY(budget.take(10300));
}

void TestRequestBudget::refusalsDoNotCount()
{
    RequestBudget budget;
    for(int i = 0; i < 30; i++)
        QVERIFY(budget.take(0));
    for(int i = 0; i < 100; i++)
        QVERIFY(!budget.take(5000));
    for(int i = 0; i < 30; i++)
        QVERIFY(budget.take(10000));
}

void TestRequestBudget::bulkLeavesRoomForOthers()
{
    RequestBudget budget;
    for(int i = 0; i < 20; i++)
        QVERIFY(budget.take(i, true));
    QVERIFY(!budget.take(100, true));
    for(int i = 0; i < 10; i++)
        QVERIFY(budget.take(200));
    QVERIFY(!budget.take(300));
}

void TestRequestBudget::bulkWaitsForASlot()
{
    RequestBudget budget;
    QCOMPARE(budget.waitMs(0, true), 0);
    for(int i = 0; i < 20; i++)
        QVERIFY(budget.take(1000 + i * 100, true));
    QCOMPARE(budget.waitMs(5000, true), 6000);
    QCOMPARE(budget.waitMs(5000, false), 0);
    QVERIFY(budget.take(11000, true));

    RequestBudget full;
    for(int i = 0; i < 10; i++)
        QVERIFY(full.take(i * 100));
    for(int i = 0; i < 20; i++)
        QVERIFY(full.take(2000 + i * 100, true));
    QCOMPARE(full.waitMs(4000, true), 8000);
    QCOMPARE(full.waitMs(4000, false), 6000);
}

void TestRequestBudget::othersWaitOnlyForTheTotal()
{
    RequestBudget budget;
    for(int i = 0; i < 30; i++)
        QVERIFY(budget.take(i * 100));
    QCOMPARE(budget.waitMs(5000, false), 5000);
    QCOMPARE(budget.waitMs(10000, false), 0);
}

QTEST_APPLESS_MAIN(TestRequestBudget)
#include "tst_requestbudget.moc"
