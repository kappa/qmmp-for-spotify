#include <QtTest>
#include "pollpolicy.h"
#include "spotifytypes.h"

namespace {

PollInputs inputs(bool visible, bool engineActive, bool selectedPlaying)
{
    PollInputs in;
    in.visible = visible;
    in.engineActive = engineActive;
    in.selectedPlaying = selectedPlaying;
    in.nowMs = 1000000;
    return in;
}

PollInputs hiddenPlaying(qint64 msToEnd)
{
    PollInputs in = inputs(false, true, true);
    in.predictedEndMs = in.nowMs + msToEnd;
    return in;
}

}

class TestPollPolicy : public QObject
{
    Q_OBJECT
private slots:
    void visibleActivePollsFast();
    void idleOrHiddenPollsSlowly();
    void awaitingEnginePollsFast();
    void hiddenPollsBeforeTrackEnd();
    void predictedEndFromPoll();
    void noPlaybackStopsUnlessEngineActive();
    void errorsBackOffThenStop();
    void interactionThrottle();
    void waitsForRateLimitBlock();
};

void TestPollPolicy::visibleActivePollsFast()
{
    QCOMPARE(nextPollDelayMs(inputs(true, true, false)), 2000);
    QCOMPARE(nextPollDelayMs(inputs(true, false, true)), 2000);
}

void TestPollPolicy::idleOrHiddenPollsSlowly()
{
    QCOMPARE(nextPollDelayMs(inputs(true, false, false)), 60000);
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
    in.visible = true;
    QCOMPARE(nextPollDelayMs(in), 2000);
}

void TestPollPolicy::hiddenPollsBeforeTrackEnd()
{
    QCOMPARE(nextPollDelayMs(hiddenPlaying(30000)), 25000);
    QCOMPARE(nextPollDelayMs(hiddenPlaying(200000)), 60000);
    QCOMPARE(nextPollDelayMs(hiddenPlaying(5000)), 60000);
    QCOMPARE(nextPollDelayMs(hiddenPlaying(3000)), 60000);
    QCOMPARE(nextPollDelayMs(hiddenPlaying(7000)), 2000);
    QCOMPARE(nextPollDelayMs(hiddenPlaying(6999)), 60000);

    PollInputs visible = hiddenPlaying(30000);
    visible.visible = true;
    QCOMPARE(nextPollDelayMs(visible), 2000);

    PollInputs paused = hiddenPlaying(30000);
    paused.selectedPlaying = false;
    QCOMPARE(nextPollDelayMs(paused), 60000);
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
    in.visible = false;
    QCOMPARE(nextPollDelayMs(in), kStopPolling);

    in.engineActive = true;
    QCOMPARE(nextPollDelayMs(in), 60000);
    in.visible = true;
    QCOMPARE(nextPollDelayMs(in), 2000);
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
