#include <QtTest>
#include "requestbudget.h"

class TestRequestBudget : public QObject
{
    Q_OBJECT
private slots:
    void allowsThirtyPerWindow();
    void windowRolls();
    void refusalsDoNotCount();
    void waitsForTheOldest();
    void countsFreeSlots();
    void budgetsAreSeparate();
};

void TestRequestBudget::allowsThirtyPerWindow()
{
    RequestBudget budget(kInteractiveBudget);
    for(int i = 0; i < 30; i++)
        QVERIFY(budget.take(1000 + i));
    QVERIFY(!budget.take(1030));
    QVERIFY(!budget.take(10999));
    QVERIFY(budget.take(11000));
    QVERIFY(!budget.take(11000));
}

void TestRequestBudget::windowRolls()
{
    RequestBudget budget(kInteractiveBudget);
    for(int i = 0; i < 30; i++)
        QVERIFY(budget.take(i * 300));
    QVERIFY(!budget.take(9999));
    QVERIFY(budget.take(10000));
    QVERIFY(!budget.take(10299));
    QVERIFY(budget.take(10300));
}

void TestRequestBudget::refusalsDoNotCount()
{
    RequestBudget budget(kInteractiveBudget);
    for(int i = 0; i < 30; i++)
        QVERIFY(budget.take(0));
    for(int i = 0; i < 100; i++)
        QVERIFY(!budget.take(5000));
    for(int i = 0; i < 30; i++)
        QVERIFY(budget.take(10000));
}

void TestRequestBudget::waitsForTheOldest()
{
    RequestBudget budget(kBulkBudget);
    QCOMPARE(budget.waitMs(0), 0);
    for(int i = 0; i < 20; i++)
        QVERIFY(budget.take(1000 + i * 100));
    QVERIFY(!budget.take(5000));
    QCOMPARE(budget.waitMs(5000), 6000);
    QCOMPARE(budget.waitMs(11000), 0);
    QVERIFY(budget.take(11000));
}

void TestRequestBudget::countsFreeSlots()
{
    RequestBudget budget(kBulkBudget);
    QCOMPARE(budget.available(0), 20);
    for(int i = 0; i < 20; i++)
        QVERIFY(budget.take(i < 5 ? 0 : 1000));
    QCOMPARE(budget.available(5000), 0);
    QCOMPARE(budget.available(10000), 5);
    QCOMPARE(budget.available(11000), 20);
}

void TestRequestBudget::budgetsAreSeparate()
{
    QCOMPARE(kInteractiveBudget, 30);
    QCOMPARE(kBulkBudget, 20);
    RequestBudget interactive(kInteractiveBudget);
    RequestBudget bulk(kBulkBudget);
    for(int i = 0; i < 20; i++)
        QVERIFY(bulk.take(0));
    QVERIFY(!bulk.take(0));
    for(int i = 0; i < 30; i++)
        QVERIFY(interactive.take(0));
}

QTEST_APPLESS_MAIN(TestRequestBudget)
#include "tst_requestbudget.moc"
