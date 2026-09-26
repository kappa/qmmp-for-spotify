#include <QtTest>
#include "pollpolicy.h"
#include "spotifytypes.h"

using namespace Qt::StringLiterals;

namespace {

PollInputs inputs(bool focused, bool engineActive, bool selectedPlaying)
{
    PollInputs in;
    in.focused = focused;
    in.engineActive = engineActive;
    in.selectedPlaying = selectedPlaying;
    in.nowMs = 1000000;
    return in;
}

PollInputs playing(bool focused, qint64 msToEnd)
{
    PollInputs in = inputs(focused, true, true);
    in.predictedEndMs = in.nowMs + msToEnd;
    return in;
}

}

class TestPollPolicy : public QObject
{
    Q_OBJECT
private slots:
    void focusedPlayingPolls5s();
    void everythingElsePollsSlowly();
    void awaitingEnginePollsFast();
    void pollsAroundTrackEnd_data();
    void pollsAroundTrackEnd();
    void pausedTrackHasNoTrackEndPolls();
    void trackEndPollsFireOncePerTrack_data();
    void trackEndPollsFireOncePerTrack();
    void trackEndCycle();
    void predictedEndFromPoll();
    void noPlaybackStopsUnlessEngineActive();
    void errorsBackOffThenStop();
    void automaticPollKeepsBackoff();
    void interactionThrottle();
    void waitsForRateLimitBlock();
};

void TestPollPolicy::focusedPlayingPolls5s()
{
    QCOMPARE(nextPollDelayMs(inputs(true, true, true)), 5000);
    QCOMPARE(nextPollDelayMs(inputs(true, false, true)), 5000);
}

void TestPollPolicy::everythingElsePollsSlowly()
{
    QCOMPARE(nextPollDelayMs(inputs(true, false, false)), 60000);
    QCOMPARE(nextPollDelayMs(inputs(true, true, false)), 60000);
    QCOMPARE(nextPollDelayMs(inputs(false, false, false)), 60000);
    QCOMPARE(nextPollDelayMs(inputs(false, true, false)), 60000);
    QCOMPARE(nextPollDelayMs(inputs(false, true, true)), 60000);
}

void TestPollPolicy::awaitingEnginePollsFast()
{
    PollInputs in = inputs(false, true, false);
    in.engineAwaiting = true;
    in.awaitingSinceMs = in.nowMs - 19999;
    QCOMPARE(nextPollDelayMs(in), 2000);
    in.awaitingSinceMs = in.nowMs - 20000;
    QCOMPARE(nextPollDelayMs(in), 60000);
    in.focused = true;
    QCOMPARE(nextPollDelayMs(in), 60000);
    in.selectedPlaying = true;
    QCOMPARE(nextPollDelayMs(in), 5000);
}

void TestPollPolicy::pollsAroundTrackEnd_data()
{
    QTest::addColumn<bool>("focused");
    QTest::addColumn<qint64>("msToEnd");
    QTest::addColumn<int>("delay");

    QTest::newRow("unfocused, far") << false << qint64(200000) << 60000;
    QTest::newRow("unfocused, 5 s before") << false << qint64(30000) << 25000;
    QTest::newRow("unfocused, 2 s floor") << false << qint64(7000) << 2000;
    QTest::newRow("unfocused, after the end") << false << qint64(6999) << 8499;
    QTest::newRow("unfocused, from the pre-end poll") << false << qint64(5000) << 6500;
    QTest::newRow("unfocused, near the end") << false << qint64(1000) << 2500;
    QTest::newRow("unfocused, after-end floor") << false << qint64(400) << 60000;
    QTest::newRow("unfocused, past the end") << false << qint64(-1000) << 60000;
    QTest::newRow("focused, far") << true << qint64(30000) << 5000;
    QTest::newRow("focused, 5 s before") << true << qint64(8000) << 3000;
    QTest::newRow("focused, 2 s floor") << true << qint64(6999) << 5000;
    QTest::newRow("focused, after the end") << true << qint64(1000) << 2500;
    QTest::newRow("focused, after-end floor") << true << qint64(400) << 5000;
}

void TestPollPolicy::pollsAroundTrackEnd()
{
    QFETCH(bool, focused);
    QFETCH(qint64, msToEnd);
    QFETCH(int, delay);
    QCOMPARE(nextPollDelayMs(playing(focused, msToEnd)), delay);
}

void TestPollPolicy::pausedTrackHasNoTrackEndPolls()
{
    PollInputs paused = playing(true, 8000);
    paused.selectedPlaying = false;
    QCOMPARE(nextPollDelayMs(paused), 60000);
    paused.focused = false;
    QCOMPARE(nextPollDelayMs(paused), 60000);
}

void TestPollPolicy::trackEndPollsFireOncePerTrack_data()
{
    QTest::addColumn<bool>("focused");
    QTest::addColumn<qint64>("msToEnd");
    QTest::addColumn<bool>("beforeDone");
    QTest::addColumn<bool>("afterDone");
    QTest::addColumn<int>("delay");
    QTest::addColumn<int>("kind");

    QTest::newRow("before end due") << false << qint64(7000) << false << false << 2000 << int(PollKind::BeforeEnd);
    QTest::newRow("before end done") << false << qint64(7000) << true << false << 8500 << int(PollKind::AfterEnd);
    QTest::newRow("after end due") << false << qint64(1000) << true << false << 2500 << int(PollKind::AfterEnd);
    QTest::newRow("both done") << false << qint64(7000) << true << true << 60000 << int(PollKind::Regular);
    QTest::newRow("after end done, frozen") << false << qint64(1000) << true << true << 60000 << int(PollKind::Regular);
    QTest::newRow("focused, before end due") << true << qint64(8000) << false << false << 3000 << int(PollKind::BeforeEnd);
    QTest::newRow("focused, before end done") << true << qint64(8000) << true << false << 5000 << int(PollKind::Regular);
    QTest::newRow("focused, after end due") << true << qint64(1000) << true << false << 2500 << int(PollKind::AfterEnd);
    QTest::newRow("focused, both done") << true << qint64(1000) << true << true << 5000 << int(PollKind::Regular);
    QTest::newRow("interval sooner") << false << qint64(200000) << false << false << 60000 << int(PollKind::Regular);
}

void TestPollPolicy::trackEndPollsFireOncePerTrack()
{
    QFETCH(bool, focused);
    QFETCH(qint64, msToEnd);
    QFETCH(bool, beforeDone);
    QFETCH(bool, afterDone);
    QFETCH(int, delay);
    QFETCH(int, kind);
    PollInputs in = playing(focused, msToEnd);
    in.beforeEndPolled = beforeDone;
    in.afterEndPolled = afterDone;
    const PollPlan plan = planNextPoll(in);
    QCOMPARE(plan.delayMs, delay);
    QCOMPARE(int(plan.kind), kind);
}

void TestPollPolicy::trackEndCycle()
{
    PlayerState state;
    state.hasPlayback = true;
    state.isPlaying = true;
    state.track.id = u"a"_s;
    state.progressMs = 60000;
    QVERIFY(!startsTrackEndCycle(u"a"_s, 50000, state));
    QVERIFY(!startsTrackEndCycle(u"a"_s, 60000, state));
    QVERIFY(!startsTrackEndCycle(u"a"_s, 61500, state));
    QVERIFY(startsTrackEndCycle(u"a"_s, 190000, state));
    QVERIFY(startsTrackEndCycle(u"b"_s, 50000, state));
    QVERIFY(startsTrackEndCycle(QString(), 0, state));
    QVERIFY(!startsTrackEndCycle(u"a"_s, 50000, PlayerState()));
}

void TestPollPolicy::predictedEndFromPoll()
{
    PlayerState state;
    state.hasPlayback = true;
    state.isPlaying = true;
    state.progressMs = 60000;
    state.track.durationMs = 200000;
    QCOMPARE(predictedTrackEndMs(state, 1000000), 1140000);

    state.progressMs = 190000;
    QCOMPARE(predictedTrackEndMs(state, 1000000), 1010000);

    state.isPlaying = false;
    QCOMPARE(predictedTrackEndMs(state, 1000000), 0);
    QCOMPARE(predictedTrackEndMs(PlayerState(), 1000000), 0);
}

void TestPollPolicy::noPlaybackStopsUnlessEngineActive()
{
    PollInputs in = inputs(true, false, false);
    in.hadPlayback = false;
    QCOMPARE(nextPollDelayMs(in), kStopPolling);
    in.focused = false;
    QCOMPARE(nextPollDelayMs(in), kStopPolling);

    in.engineActive = true;
    QCOMPARE(nextPollDelayMs(in), 60000);
    in.focused = true;
    QCOMPARE(nextPollDelayMs(in), 60000);
}

void TestPollPolicy::errorsBackOffThenStop()
{
    PollInputs in = inputs(true, true, true);
    QList<int> delays;
    for(in.failures = 1; in.failures <= 8; in.failures++)
        delays << nextPollDelayMs(in);
    QCOMPARE(delays, QList<int>({ 2000, 4000, 8000, 16000, 32000, 64000, 128000, 256000 }));
    QVERIFY(!isPollHardFail(8));

    in.failures = 9;
    QCOMPARE(nextPollDelayMs(in), kStopPolling);
    QVERIFY(isPollHardFail(9));

    PollInputs idle = inputs(false, false, false);
    idle.hadPlayback = false;
    idle.failures = 3;
    QCOMPARE(nextPollDelayMs(idle), 8000);
}

void TestPollPolicy::automaticPollKeepsBackoff()
{
    QCOMPARE(pollSoonDelayMs(0, -1), kPollSoonMs);
    QCOMPARE(pollSoonDelayMs(0, 50000), kPollSoonMs);
    QCOMPARE(pollSoonDelayMs(1, 1500), kKeepTimer);
    QCOMPARE(pollSoonDelayMs(3, 7000), kKeepTimer);
    QCOMPARE(pollSoonDelayMs(3, -1), 8000);
    QCOMPARE(pollSoonDelayMs(9, -1), kKeepTimer);
}

void TestPollPolicy::interactionThrottle()
{
    QCOMPARE(interactionDelayMs(0, 1000000, 60000, 2000), kPollSoonMs);
    QCOMPARE(interactionDelayMs(995000, 1000000, -1, kStopPolling), kPollSoonMs);
    QCOMPARE(interactionDelayMs(995000, 1000000, 100, 2000), kKeepTimer);

    QCOMPARE(interactionDelayMs(996000, 1000000, 58000, 2000), 2000);
    QCOMPARE(interactionDelayMs(996000, 1000000, 1500, 2000), kKeepTimer);
    QCOMPARE(interactionDelayMs(996000, 1000000, 58000, 60000), kKeepTimer);
    QCOMPARE(interactionDelayMs(996000, 1000000, 58000, kStopPolling), kKeepTimer);

    QCOMPARE(interactionDelayMs(996000, 1000000, -1, kStopPolling), 1000);
    QCOMPARE(interactionDelayMs(1000000, 1000000, -1, kStopPolling), 5000);
}

void TestPollPolicy::waitsForRateLimitBlock()
{
    QCOMPARE(rateLimitedPollDelayMs(1300000, 1000000), 301000);
    QCOMPARE(rateLimitedPollDelayMs(1000000, 1300000), 1000);
    QCOMPARE(rateLimitedPollDelayMs(Q_INT64_C(1) << 40, 0), std::numeric_limits<int>::max());
}

QTEST_APPLESS_MAIN(TestPollPolicy)
#include "tst_pollpolicy.moc"
