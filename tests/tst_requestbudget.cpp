#include <QtTest>
#include "requestbudget.h"

class TestRequestBudget : public QObject
{
    Q_OBJECT
private slots:
    void allowsThirtyPerWindow();
    void windowRolls();
    void refusalsDoNotCount();
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

QTEST_APPLESS_MAIN(TestRequestBudget)
#include "tst_requestbudget.moc"
