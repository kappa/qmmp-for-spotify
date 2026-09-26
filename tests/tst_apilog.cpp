#include <QtTest>
#include "apilog.h"

using namespace Qt::StringLiterals;

class TestApiLog : public QObject
{
    Q_OBJECT
private slots:
    void line();
    void notes();
};

void TestApiLog::line()
{
    const QDateTime time(QDate(2026, 9, 25), QTime(14, 3, 7, 42));
    QCOMPARE(apiLogLine(time, "GET", u"/v1/me/player?market=from_token"_s, 200, 153, QString()),
             u"2026-09-25T14:03:07.042\tGET\t/v1/me/player?market=from_token\t200\t153\t"_s);
}

void TestApiLog::notes()
{
    QCOMPARE(apiLogNote(200, QByteArray(), false, QString()), QString());
    QCOMPARE(apiLogNote(429, " 309 ", false, QString()), u"retry-after=309"_s);
    QCOMPARE(apiLogNote(429, QByteArray(), true, QString()), u"retry retry-after=30"_s);
    QCOMPARE(apiLogNote(204, QByteArray(), true, QString()), u"retry"_s);
    QCOMPARE(apiLogNote(0, QByteArray(), false, u"Host not found"_s), u"Host not found"_s);
}

QTEST_APPLESS_MAIN(TestApiLog)
#include "tst_apilog.moc"
