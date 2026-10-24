#include <QtTest>
#include "enginelogic.h"
#include "pollpolicy.h"

using namespace Qt::StringLiterals;

namespace {

class FakeSink : public EngineSink
{
public:
    QStringList calls;
    quint64 lastGeneration = 0;
    QString lastError;
    // the delay of the last scheduleStartFlush(); -1: none
    qint64 startFlushMs = -1;
    int cancels = 0;
    // shared by the engines, like SpotifyClient's
    qint64 startSentAt = -1;

    void apiPlay(const QString &uri, qint64 positionMs, quint64 generation) override
    {
        calls << u"play %1 %2"_s.arg(uri).arg(positionMs);
        lastGeneration = generation;
    }
    void apiPause() override { calls << u"pause"_s; }
    void apiResume() override { calls << u"resume"_s; }
    void apiSeek(qint64 positionMs) override { calls << u"seek %1"_s.arg(positionMs); }
    void apiQueue(const QString &uri) override { calls << u"queue "_s + uri; }
    void apiNext() override { calls << u"next"_s; }
    void suppressFollow(const QString &trackId) override { calls << u"suppress "_s + trackId; }
    void reportState(EngineState state) override
    {
        static const QStringList names = { u"Stopped"_s, u"Buffering"_s, u"Playing"_s, u"Paused"_s };
        calls << u"state "_s + names.at(int(state));
    }
    void reportTrack(const QString &path, const SpotifyTrack &track) override { calls << u"track %1 %2"_s.arg(path, track.title); }
    void reportFinished() override { calls << u"finished"_s; }
    void reportError(const QString &message, bool fatal) override
    {
        calls << (fatal ? u"fatal"_s : u"error"_s);
        lastError = message;
    }
    void reportNotice(const QString &message) override { calls << u"notice "_s + message; }
    void scheduleStartFlush(qint64 delayMs) override { startFlushMs = delayMs; }
    void cancelStartRequest() override { cancels++; }
    qint64 lastStartSentAt() const override { return startSentAt; }
    void setLastStartSentAt(qint64 at) override { startSentAt = at; }
    void pollAfter(qint64 delayMs) override { calls << u"poll after %1"_s.arg(delayMs); }
};

PlayerState deviceState(const QString &device, const QString &trackId, qint64 progressMs, bool isPlaying = true)
{
    PlayerState state;
    state.hasPlayback = true;
    state.deviceId = device;
    state.isPlaying = isPlaying;
    state.progressMs = progressMs;
    state.track.id = trackId;
    state.track.title = u"Title "_s + trackId;
    state.track.durationMs = 200000;
    return state;
}

}

class TestEngineLogic : public QObject
{
    Q_OBJECT
private slots:
    void init();
    void cleanup();
    void playWithoutDeviceIsFatal();
    void adoptsTrackAlreadyPlaying();
    void startsTrackAndWaitsForDevice();
    void followsRemotePause();
    void pauseIgnoresStalePoll();
    void queuesNextAndSwitchesGaplessly();
    void startsNextWhenDeviceDidNotAdvance();
    void fallsBackToPlayWhenQueueFailed();
    void failedQueueIsNotResent();
    void fallbackAtTrackEndRunsOnce();
    void enqueueDoesNotQueueImmediately();
    void tickQueuesOnceAtLeadTime();
    void tickDoesNotQueueWhilePaused();
    void updateStateQueuesInsideWindow();
    void switchingBeforeLeadTimeNeverQueuesAbandonedNext();
    void finishPausesStoppedDevice();
    void finishPausesAutoplayTrack();
    void playAfterFinishCancelsPause();
    void finishLeavesOtherDeviceAlone();
    void stopPausesOwnTrack();
    void doubleStopStillPauses();
    void doubleStopThenRestartPlaysFromStart();
    void stopLeavesForeignTrackAlone();
    void pausesWhenDevicePlaybackEnds();
    void pausesWhenPlaybackMovesToOtherDevice();
    void followsAgainWhenDeviceReturns();
    void vanishedPlaybackIgnoredWhileAwaitingOrInGrace();
    void interpolatesElapsed();
    void seeksCoalesce();
    void pendingSeekDroppedOnStop();
    void rapidStartsCoalesce();
    void pendingStartDroppedOnStop();
    void previousBeforePollStartsTrackAgain();
    void pendingSkipDroppedWhenDeviceReachesQueuedTrack();
    void pollDuringPendingPlayKeepsWaiting();
    void startGapSpansEngines();
    void skimmingStaysUnderBudget();
    void heldNextKeyStaysUnderBudget();
    void systemicFailureStops();
    void trackFailureSkips();
    void systemicFailureOnTrackChangePausesPreviousTrack();
    void failureOnFirstPlayLeavesDeviceAlone();
    void skippedFailuresPauseOnlyWhenQmmpGivesUp();
    void newTrackReportsWithinStaleCommandGrace();
    void restartPlaysFromStartWithoutPause();
    void restartWhilePausedPlaysFromStart();
    void nextSendsNoPause();
    void stopPausesRightAfterLocalResume();
    void stopDoesNotPausePausedDevice();
    void adoptsAfterStopWasFlushed();
    void ignoresFailureAfterStop();
    void ignoresFailureOfSupersededPlay();
    void skipsWhenDeviceDoesNotStart();
    void secondConsecutiveStartTimeoutStops();
    void startTimeoutAfterSuccessfulStartSkips();
    void failedSkipToQueuedTrackStops();
    void skipToQueuedTrackTimesOut();
    void failureAfterStartSendsNoStalePause();
    void startTimeoutIgnoredOnceStarted();
    void unavailableTrackFailsWithoutRequest();
    void unavailableNextIsSkippedAtTrackEnd();
    void supersededStartIsCancelled();
    void logoutStopsQuietly();
    void logoutDropsPendingPause();

private:
    void startPlaying(const QString &trackId);
    void clickNext(const QString &trackId);
    int maxRequestsPer10s(qint64 clickGapMs, int clicks);

    qint64 m_now = 0;
    FakeSink m_sink;
    EngineLogic *m_logic = nullptr;
};

void TestEngineLogic::init()
{
    m_now = 1000;
    m_sink.calls.clear();
    m_sink.startSentAt = -1;
    m_logic = new EngineLogic(&m_sink, [this] { return m_now; });
    m_logic->setSelectedDevice(u"dev"_s);
}

void TestEngineLogic::cleanup()
{
    delete m_logic;
}

void TestEngineLogic::startPlaying(const QString &trackId)
{
    m_logic->updateState(deviceState(u"dev"_s, trackId, 0));
    QVERIFY(m_logic->enqueue(u"spotify://track/"_s + trackId));
    m_logic->play();
    QCOMPARE(m_logic->state(), EngineState::Playing);
    m_sink.calls.clear();
}

void TestEngineLogic::playWithoutDeviceIsFatal()
{
    m_logic->setSelectedDevice(QString());
    QVERIFY(m_logic->enqueue(u"spotify://track/a"_s));
    m_logic->play();
    QCOMPARE(m_sink.calls, QStringList { u"fatal"_s });
}

void TestEngineLogic::adoptsTrackAlreadyPlaying()
{
    m_logic->updateState(deviceState(u"dev"_s, u"a"_s, 5000));
    QVERIFY(m_logic->enqueue(u"spotify://track/a"_s));
    m_logic->play();
    QCOMPARE(m_sink.calls, (QStringList { u"state Playing"_s, u"track spotify://track/a Title a"_s }));
}

void TestEngineLogic::startsTrackAndWaitsForDevice()
{
    m_logic->updateState(deviceState(u"dev"_s, u"old"_s, 1000));
    QVERIFY(m_logic->enqueue(u"https://open.spotify.com/track/a?si=x"_s));
    m_logic->play();
    m_logic->updateState(deviceState(u"dev"_s, u"old"_s, 3000));
    QCOMPARE(m_sink.calls, (QStringList { u"state Buffering"_s, u"play spotify:track:a 0"_s }));
    QVERIFY(m_logic->isAwaitingStart());

    m_logic->updateState(deviceState(u"dev"_s, u"a"_s, 500));
    QCOMPARE(m_sink.calls.mid(2), (QStringList { u"state Playing"_s, u"track https://open.spotify.com/track/a?si=x Title a"_s }));
    QVERIFY(!m_logic->isAwaitingStart());
}

void TestEngineLogic::followsRemotePause()
{
    startPlaying(u"a"_s);
    m_logic->updateState(deviceState(u"dev"_s, u"a"_s, 8000, false));
    QCOMPARE(m_sink.calls, QStringList { u"state Paused"_s });
    m_logic->pause();
    QCOMPARE(m_sink.calls.mid(1), (QStringList { u"resume"_s, u"state Playing"_s }));
}

void TestEngineLogic::pauseIgnoresStalePoll()
{
    startPlaying(u"a"_s);
    m_now += 1000;
    m_logic->pause();
    m_logic->updateState(deviceState(u"dev"_s, u"a"_s, 1000, true));
    QCOMPARE(m_logic->state(), EngineState::Paused);
    m_now += 3000;
    m_logic->updateState(deviceState(u"dev"_s, u"a"_s, 1000, false));
    QCOMPARE(m_sink.calls, (QStringList { u"pause"_s, u"state Paused"_s }));
}

void TestEngineLogic::queuesNextAndSwitchesGaplessly()
{
    startPlaying(u"a"_s);
    QVERIFY(m_logic->enqueue(u"spotify://track/b"_s));
    QCOMPARE(m_logic->trackIds(), (QStringList { u"a"_s, u"b"_s }));
    m_now += 198500; // duration 200000 - kQueueLeadMs 1500
    m_logic->tick();
    QCOMPARE(m_sink.calls, QStringList { u"queue spotify:track:b"_s });
    m_logic->updateState(deviceState(u"dev"_s, u"b"_s, 300));
    QCOMPARE(m_sink.calls.mid(1), (QStringList { u"finished"_s, u"state Stopped"_s,
                                          u"state Buffering"_s, u"state Playing"_s, u"track spotify://track/b Title b"_s }));
    QCOMPARE(m_logic->trackIds(), QStringList { u"b"_s });
}

void TestEngineLogic::startsNextWhenDeviceDidNotAdvance()
{
    startPlaying(u"a"_s);
    QVERIFY(m_logic->enqueue(u"spotify://track/b"_s));
    m_now += 198500; // duration 200000 - kQueueLeadMs 1500: queued in time...
    m_logic->tick();
    QCOMPARE(m_sink.calls, QStringList { u"queue spotify:track:b"_s });
    m_now += 500; // ...but the device never advances to it
    m_logic->updateState(deviceState(u"dev"_s, u"a"_s, 0, false));
    // b is already in Spotify's queue: skip to it instead of apiPlay, which would leave
    // the queued entry to play a second time after it
    QCOMPARE(m_sink.calls.mid(1), (QStringList { u"finished"_s, u"state Stopped"_s,
                                          u"state Buffering"_s, u"next"_s }));
    QVERIFY(m_logic->isAwaitingStart());
}

void TestEngineLogic::fallsBackToPlayWhenQueueFailed()
{
    startPlaying(u"a"_s);
    QVERIFY(m_logic->enqueue(u"spotify://track/b"_s));
    m_now += 198500; // duration 200000 - kQueueLeadMs 1500
    m_logic->tick();
    QCOMPARE(m_sink.calls, QStringList { u"queue spotify:track:b"_s });
    m_logic->queueFailed(); // the queue POST failed, so b never reached the device
    m_sink.calls.clear();
    m_now += 500;
    m_logic->updateState(deviceState(u"dev"_s, u"a"_s, 0, false));
    QCOMPARE(m_sink.calls, (QStringList { u"finished"_s, u"state Stopped"_s,
                                          u"state Buffering"_s, u"play spotify:track:b 0"_s }));
    QVERIFY(m_logic->isAwaitingStart());
}

void TestEngineLogic::failedQueueIsNotResent()
{
    startPlaying(u"a"_s);
    QVERIFY(m_logic->enqueue(u"spotify://track/b"_s));
    m_now += 198500; // duration 200000 - kQueueLeadMs 1500
    m_logic->tick();
    m_logic->queueFailed();
    for(int i = 0; i < 40; i++)
    {
        m_now += 250;
        m_logic->tick();
        m_logic->updateState(deviceState(u"dev"_s, u"a"_s, 199000));
    }
    QCOMPARE(m_sink.calls.count(u"queue spotify:track:b"_s), 1);
}

void TestEngineLogic::fallbackAtTrackEndRunsOnce()
{
    startPlaying(u"a"_s);
    QVERIFY(m_logic->enqueue(u"spotify://track/b"_s));
    m_now += 198500; // duration 200000 - kQueueLeadMs 1500
    m_logic->tick();
    m_logic->queueFailed();
    m_now += 500;
    for(int i = 0; i < 10; i++)
    {
        m_now += 250;
        m_logic->tick();
        m_logic->updateState(deviceState(u"dev"_s, u"a"_s, 0, false));
    }
    QCOMPARE(m_sink.calls.count(u"play spotify:track:b 0"_s), 1);
    QCOMPARE(m_sink.calls.count(u"queue spotify:track:b"_s), 1);
    const qsizetype sent = m_sink.calls.size();
    m_logic->playFailed(m_sink.lastGeneration, u"Spotify: offline"_s, true);
    QCOMPARE(m_sink.calls.mid(sent), QStringList { u"fatal"_s });
}

void TestEngineLogic::seeksCoalesce()
{
    startPlaying(u"a"_s);
    m_logic->seek(1000);
    m_now += 100;
    m_logic->seek(2000);
    m_now += 100;
    m_logic->seek(3000);
    QCOMPARE(m_logic->elapsed(), qint64(3000));
    m_logic->flushSeek();
    m_now += 299;
    m_logic->flushSeek();
    QVERIFY(m_sink.calls.isEmpty());
    m_now += 1;
    m_logic->flushSeek();
    m_logic->flushSeek();
    QCOMPARE(m_sink.calls, QStringList { u"seek 3000"_s });
}

void TestEngineLogic::pendingSeekDroppedOnStop()
{
    startPlaying(u"a"_s);
    m_logic->seek(5000);
    m_logic->stop();
    m_now += 300;
    m_logic->flushSeek();
    QVERIFY(!m_sink.calls.contains(u"seek 5000"_s));
}

// qmmp's Next: stop, then the next track, in one call stack
void TestEngineLogic::clickNext(const QString &trackId)
{
    m_logic->stop();
    QVERIFY(m_logic->enqueue(u"spotify://track/"_s + trackId));
    m_logic->play();
}

void TestEngineLogic::rapidStartsCoalesce()
{
    startPlaying(u"a"_s);
    clickNext(u"b"_s);
    QCOMPARE(m_sink.calls.count(u"play spotify:track:b 0"_s), 1);
    m_now += 200;
    clickNext(u"c"_s);
    QCOMPARE(m_sink.startFlushMs, qint64(800));
    m_now += 200;
    clickNext(u"d"_s);
    QCOMPARE(m_logic->trackIds(), QStringList { u"d"_s });
    QCOMPARE(m_logic->state(), EngineState::Buffering);
    m_now += 599;
    m_logic->flushStart();
    QVERIFY(!m_sink.calls.contains(u"play spotify:track:d 0"_s));
    m_now += 1;
    m_logic->flushStart();
    m_logic->flushStart();
    QVERIFY(!m_sink.calls.contains(u"play spotify:track:c 0"_s));
    QCOMPARE(m_sink.calls.count(u"play spotify:track:d 0"_s), 1);
    QVERIFY(m_logic->isAwaitingStart());
}

void TestEngineLogic::pendingStartDroppedOnStop()
{
    startPlaying(u"a"_s);
    clickNext(u"b"_s);
    m_now += 200;
    clickNext(u"c"_s);
    m_logic->stop();
    m_now += 1000;
    m_logic->flushStart();
    QVERIFY(!m_sink.calls.contains(u"play spotify:track:c 0"_s));
}

// Next sends b, Previous comes before any poll: the last poll still shows a playing, but the
// device is switching to b, so a must be started again rather than adopted
void TestEngineLogic::previousBeforePollStartsTrackAgain()
{
    startPlaying(u"a"_s);
    clickNext(u"b"_s);
    m_now += 200;
    clickNext(u"a"_s);
    QCOMPARE(m_logic->state(), EngineState::Buffering);
    QVERIFY(m_logic->isAwaitingStart());
    m_now += 800;
    m_logic->flushStart();
    QCOMPARE(m_sink.calls.count(u"play spotify:track:a 0"_s), 1);
}

// a's start at a large offset ends within 1 s, so the skip to the queued b is held back; the
// device moves on to b by itself, and the held skip must not jump past it
void TestEngineLogic::pendingSkipDroppedWhenDeviceReachesQueuedTrack()
{
    QVERIFY(m_logic->enqueue(u"spotify://track/a"_s, 199000));
    m_logic->play();
    QVERIFY(m_logic->enqueue(u"spotify://track/b"_s));
    m_now += 100;
    m_logic->updateState(deviceState(u"dev"_s, u"a"_s, 199100));
    QVERIFY(m_sink.calls.contains(u"queue spotify:track:b"_s));
    m_now += 200;
    m_logic->updateState(deviceState(u"dev"_s, u"a"_s, 0, false));
    QCOMPARE(m_logic->trackIds(), QStringList { u"b"_s });
    QVERIFY(!m_sink.calls.contains(u"next"_s));
    m_now += 200;
    m_logic->updateState(deviceState(u"dev"_s, u"b"_s, 200));
    m_now += 500;
    m_logic->flushStart();
    QVERIFY(!m_sink.calls.contains(u"next"_s));
    QCOMPARE(m_logic->state(), EngineState::Playing);
    QVERIFY(!m_logic->isAwaitingStart());
}

// a poll that shows the current track while its play is held back may predate the play
// (here two restarts): it must not end the wait, the held play still goes out
void TestEngineLogic::pollDuringPendingPlayKeepsWaiting()
{
    startPlaying(u"a"_s);
    clickNext(u"a"_s);
    m_now += 200;
    clickNext(u"a"_s);
    m_logic->updateState(deviceState(u"dev"_s, u"a"_s, 5000));
    QVERIFY(m_logic->isAwaitingStart());
    QCOMPARE(m_logic->state(), EngineState::Buffering);
    m_now += 800;
    m_logic->flushStart();
    QCOMPARE(m_sink.calls.count(u"play spotify:track:a 0"_s), 2);
}

// qmmp deletes the engine for each non-Spotify item: a held Next over a playlist alternating
// Spotify and local items must still send at most one start per kStartGapMs
void TestEngineLogic::startGapSpansEngines()
{
    QVERIFY(m_logic->enqueue(u"spotify://track/a"_s));
    m_logic->play();
    QCOMPARE(m_sink.calls.count(u"play spotify:track:a 0"_s), 1);
    m_logic->stop();
    delete m_logic;
    m_now += 300;
    m_logic = new EngineLogic(&m_sink, [this] { return m_now; });
    m_logic->setSelectedDevice(u"dev"_s);
    QVERIFY(m_logic->enqueue(u"spotify://track/b"_s));
    m_logic->play();
    QVERIFY(!m_sink.calls.contains(u"play spotify:track:b 0"_s));
    QCOMPARE(m_sink.startFlushMs, qint64(700));
    m_now += 700;
    m_logic->flushStart();
    QCOMPARE(m_sink.calls.count(u"play spotify:track:b 0"_s), 1);
}

// Next clicks every clickGapMs; each play's reply (150 ms later) asks for a confirmation poll,
// and while the engine awaits a start the poller polls every 2 s: the largest number of
// interactive requests (plays and polls) in any 10 s
int TestEngineLogic::maxRequestsPer10s(qint64 clickGapMs, int clicks)
{
    startPlaying(u"a"_s);
    const qint64 start = m_now;
    const qint64 end = start + clickGapMs * clicks + 10000;
    QList<qint64> requests;
    QList<qint64> replies;
    qint64 flushAt = -1;
    qint64 pollAt = -1;
    qint64 lastPoll = -100000;
    int click = 0;
    for(; m_now <= end; m_now += 10)
    {
        const qsizetype before = m_sink.calls.size();
        if(click < clicks && m_now >= start + click * clickGapMs)
        {
            m_sink.startFlushMs = -1;
            clickNext(u"t%1"_s.arg(click++));
            if(m_sink.startFlushMs >= 0)
                flushAt = m_now + m_sink.startFlushMs;
        }
        if(flushAt >= 0 && m_now >= flushAt)
        {
            flushAt = -1;
            m_logic->flushStart();
        }
        for(qsizetype i = before; i < m_sink.calls.size(); i++)
        {
            if(m_sink.calls.at(i).startsWith(u"play "_s))
            {
                requests << m_now;
                replies << m_now + 150;
            }
        }
        while(!replies.isEmpty() && replies.first() <= m_now)
        {
            replies.removeFirst();
            const int delay = pollSoonDelayMs(0, pollAt >= 0 ? int(pollAt - m_now) : -1, lastPoll, m_now);
            if(delay != kKeepTimer)
                pollAt = m_now + delay;
        }
        if(pollAt >= 0 && m_now >= pollAt)
        {
            requests << m_now;
            lastPoll = m_now;
            pollAt = m_now + 2000;
        }
    }
    int most = 0;
    for(const qint64 from : requests)
    {
        int count = 0;
        for(const qint64 at : requests)
            count += at >= from && at < from + 10000;
        most = qMax(most, count);
    }
    return most;
}

void TestEngineLogic::skimmingStaysUnderBudget()
{
    const int requests = maxRequestsPer10s(667, 15);
    QVERIFY2(requests <= 20, qPrintable(QString::number(requests)));
}

void TestEngineLogic::heldNextKeyStaysUnderBudget()
{
    const int requests = maxRequestsPer10s(33, 150);
    QVERIFY2(requests <= 20, qPrintable(QString::number(requests)));
}

void TestEngineLogic::enqueueDoesNotQueueImmediately()
{
    startPlaying(u"a"_s);
    QVERIFY(m_logic->enqueue(u"spotify://track/b"_s));
    QVERIFY(m_sink.calls.isEmpty());
}

void TestEngineLogic::tickQueuesOnceAtLeadTime()
{
    startPlaying(u"a"_s);
    QVERIFY(m_logic->enqueue(u"spotify://track/b"_s));
    m_now += 198499; // 1 ms short of duration 200000 - kQueueLeadMs 1500
    m_logic->tick();
    QVERIFY(m_sink.calls.isEmpty());
    m_now += 1;
    m_logic->tick();
    QCOMPARE(m_sink.calls, QStringList { u"queue spotify:track:b"_s });
    m_logic->tick();
    QCOMPARE(m_sink.calls, QStringList { u"queue spotify:track:b"_s });
}

void TestEngineLogic::tickDoesNotQueueWhilePaused()
{
    startPlaying(u"a"_s);
    QVERIFY(m_logic->enqueue(u"spotify://track/b"_s));
    m_now += 198500; // duration 200000 - kQueueLeadMs 1500: inside the window while still playing
    m_logic->pause();
    m_sink.calls.clear();
    m_logic->tick();
    QVERIFY(m_sink.calls.isEmpty());
}

void TestEngineLogic::updateStateQueuesInsideWindow()
{
    startPlaying(u"a"_s);
    QVERIFY(m_logic->enqueue(u"spotify://track/b"_s));
    m_now += 198500; // duration 200000 - kQueueLeadMs 1500
    m_logic->updateState(deviceState(u"dev"_s, u"a"_s, 198500));
    QCOMPARE(m_sink.calls, QStringList { u"queue spotify:track:b"_s });
}

void TestEngineLogic::switchingBeforeLeadTimeNeverQueuesAbandonedNext()
{
    startPlaying(u"a"_s);
    QVERIFY(m_logic->enqueue(u"spotify://track/b"_s));
    m_now += 195000; // inside the old ~7 s announce window but before the 1.5 s lead time
    m_logic->tick();
    m_logic->stop();
    QVERIFY(m_logic->enqueue(u"spotify://track/c"_s));
    m_logic->play();
    QVERIFY(!m_sink.calls.contains(u"queue spotify:track:b"_s));
}

void TestEngineLogic::finishPausesStoppedDevice()
{
    startPlaying(u"a"_s);
    m_now += 198500;
    m_logic->updateState(deviceState(u"dev"_s, u"a"_s, 0, false));
    QCOMPARE(m_sink.calls, (QStringList { u"suppress a"_s, u"finished"_s, u"state Stopped"_s }));
    QVERIFY(m_logic->trackIds().isEmpty());
    m_logic->flushPendingPause();
    QCOMPARE(m_sink.calls.mid(3), QStringList { u"pause"_s });
}

void TestEngineLogic::finishPausesAutoplayTrack()
{
    startPlaying(u"a"_s);
    m_now += 198500;
    m_logic->updateState(deviceState(u"dev"_s, u"r"_s, 1000));
    QCOMPARE(m_sink.calls, (QStringList { u"suppress r"_s, u"finished"_s, u"state Stopped"_s }));
    m_logic->flushPendingPause();
    QCOMPARE(m_sink.calls.mid(3), QStringList { u"pause"_s });
}

void TestEngineLogic::playAfterFinishCancelsPause()
{
    startPlaying(u"a"_s);
    m_now += 198500;
    m_logic->updateState(deviceState(u"dev"_s, u"a"_s, 0, false));
    QVERIFY(m_logic->enqueue(u"spotify://track/b"_s));
    m_logic->play();
    m_logic->flushPendingPause();
    QCOMPARE(m_sink.calls, (QStringList { u"suppress a"_s, u"finished"_s, u"state Stopped"_s, u"state Buffering"_s,
                                          u"play spotify:track:b 0"_s }));
}

void TestEngineLogic::finishLeavesOtherDeviceAlone()
{
    startPlaying(u"a"_s);
    m_now += 198500;
    m_logic->updateState(deviceState(u"phone"_s, u"r"_s, 1000));
    m_logic->flushPendingPause();
    QCOMPARE(m_sink.calls, (QStringList { u"finished"_s, u"state Stopped"_s }));
}

void TestEngineLogic::stopPausesOwnTrack()
{
    startPlaying(u"a"_s);
    m_logic->stop();
    QCOMPARE(m_sink.calls, QStringList { u"state Stopped"_s });
    m_logic->flushPendingPause();
    m_logic->flushPendingPause();
    QCOMPARE(m_sink.calls, (QStringList { u"state Stopped"_s, u"pause"_s }));
}

void TestEngineLogic::doubleStopStillPauses()
{
    startPlaying(u"a"_s);
    m_logic->stop();
    m_logic->stop();
    m_logic->flushPendingPause();
    QCOMPARE(m_sink.calls, (QStringList { u"state Stopped"_s, u"pause"_s }));
}

void TestEngineLogic::doubleStopThenRestartPlaysFromStart()
{
    startPlaying(u"a"_s);
    m_logic->stop();
    m_logic->stop();
    QVERIFY(m_logic->enqueue(u"spotify://track/a"_s));
    m_logic->play();
    m_logic->flushPendingPause();
    QCOMPARE(m_sink.calls, (QStringList { u"state Stopped"_s, u"state Buffering"_s, u"play spotify:track:a 0"_s }));
}

void TestEngineLogic::stopLeavesForeignTrackAlone()
{
    startPlaying(u"a"_s);
    m_logic->updateState(deviceState(u"dev"_s, u"x"_s, 1000));
    m_logic->stop();
    m_logic->flushPendingPause();
    QCOMPARE(m_sink.calls, QStringList { u"state Stopped"_s });
}

void TestEngineLogic::pausesWhenDevicePlaybackEnds()
{
    PlayerState named = deviceState(u"dev"_s, u"a"_s, 0);
    named.deviceName = u"Web Player"_s;
    m_logic->updateState(named);
    QVERIFY(m_logic->enqueue(u"spotify://track/a"_s));
    m_logic->play();
    m_sink.calls.clear();
    m_now += 5000;
    m_logic->updateState(PlayerState());
    QCOMPARE(m_sink.calls, (QStringList { u"state Paused"_s, u"notice Spotify: playback stopped on Web Player"_s }));
    QCOMPARE(m_logic->elapsed(), qint64(5000));
    m_now += 5000;
    m_logic->updateState(PlayerState());
    QCOMPARE(m_sink.calls.size(), 2);
    QCOMPARE(m_logic->elapsed(), qint64(5000));
}

void TestEngineLogic::pausesWhenPlaybackMovesToOtherDevice()
{
    startPlaying(u"a"_s);
    m_logic->updateState(deviceState(u"phone"_s, u"z"_s, 1000));
    QCOMPARE(m_sink.calls, (QStringList { u"state Paused"_s, u"notice Spotify: playback stopped on the device"_s }));
    QCOMPARE(m_logic->state(), EngineState::Paused);
}

void TestEngineLogic::followsAgainWhenDeviceReturns()
{
    startPlaying(u"a"_s);
    m_logic->updateState(PlayerState());
    m_sink.calls.clear();
    m_logic->updateState(deviceState(u"dev"_s, u"a"_s, 7000));
    QCOMPARE(m_sink.calls, QStringList { u"state Playing"_s });
    QCOMPARE(m_logic->elapsed(), qint64(7000));
}

void TestEngineLogic::vanishedPlaybackIgnoredWhileAwaitingOrInGrace()
{
    QVERIFY(m_logic->enqueue(u"spotify://track/a"_s));
    m_logic->play();
    m_sink.calls.clear();
    m_logic->updateState(PlayerState());
    QVERIFY(m_sink.calls.isEmpty());

    m_logic->updateState(deviceState(u"dev"_s, u"a"_s, 0));
    m_logic->pause();
    m_logic->pause();
    m_sink.calls.clear();
    m_now += 2000;
    m_logic->updateState(PlayerState());
    QCOMPARE(m_sink.calls, QStringList { u"poll after 500"_s });
    QCOMPARE(m_logic->state(), EngineState::Playing);
}

void TestEngineLogic::interpolatesElapsed()
{
    startPlaying(u"a"_s);
    m_now += 1500;
    QCOMPARE(m_logic->elapsed(), qint64(1500));
    m_logic->pause();
    m_now += 5000;
    QCOMPARE(m_logic->elapsed(), qint64(1500));
}

void TestEngineLogic::systemicFailureStops()
{
    QVERIFY(m_logic->enqueue(u"spotify://track/a"_s));
    m_logic->play();
    m_logic->playFailed(m_sink.lastGeneration, u"offline"_s, true);
    QCOMPARE(m_sink.calls.last(), u"fatal"_s);
    QCOMPARE(m_sink.lastError, u"offline"_s);
    QVERIFY(m_logic->trackIds().isEmpty());
    QVERIFY(!m_logic->isAwaitingStart());
}

void TestEngineLogic::trackFailureSkips()
{
    QVERIFY(m_logic->enqueue(u"spotify://track/a"_s));
    m_logic->play();
    m_logic->playFailed(m_sink.lastGeneration, u"gone"_s, false);
    QCOMPARE(m_sink.calls.last(), u"error"_s);
    QCOMPARE(m_sink.lastError, u"gone"_s);
    QVERIFY(m_logic->trackIds().isEmpty());
    QVERIFY(!m_logic->isAwaitingStart());
    QCOMPARE(m_logic->state(), EngineState::Stopped);
}

// the play that replaced a's deferred pause failed: a must not keep playing after qmmp stops
void TestEngineLogic::systemicFailureOnTrackChangePausesPreviousTrack()
{
    startPlaying(u"a"_s);
    m_logic->stop();
    QVERIFY(m_logic->enqueue(u"spotify://track/b"_s));
    m_logic->play();
    m_logic->playFailed(m_sink.lastGeneration, u"rate limited"_s, true);
    m_logic->stop();
    m_logic->flushPendingPause();
    QCOMPARE(m_sink.calls, (QStringList { u"state Stopped"_s, u"state Buffering"_s, u"play spotify:track:b 0"_s,
                                          u"fatal"_s, u"pause"_s }));
}

void TestEngineLogic::failureOnFirstPlayLeavesDeviceAlone()
{
    m_logic->updateState(deviceState(u"dev"_s, u"x"_s, 5000));
    QVERIFY(m_logic->enqueue(u"spotify://track/a"_s));
    m_logic->play();
    m_logic->playFailed(m_sink.lastGeneration, u"rate limited"_s, true);
    m_logic->stop();
    m_logic->flushPendingPause();
    QVERIFY(!m_sink.calls.contains(u"pause"_s));
}

// qmmp skips to the next track (its play cancels the pause) until it gives up and stops
void TestEngineLogic::skippedFailuresPauseOnlyWhenQmmpGivesUp()
{
    startPlaying(u"a"_s);
    m_logic->stop();
    QVERIFY(m_logic->enqueue(u"spotify://track/b"_s));
    m_logic->play();
    m_logic->playFailed(m_sink.lastGeneration, u"gone"_s, false);
    m_logic->stop();
    QVERIFY(m_logic->enqueue(u"spotify://track/c"_s));
    m_logic->play();
    m_logic->flushPendingPause();
    QVERIFY(!m_sink.calls.contains(u"pause"_s));
    m_logic->playFailed(m_sink.lastGeneration, u"gone"_s, false);
    m_logic->stop();
    m_logic->flushPendingPause();
    QCOMPARE(m_sink.calls.last(), u"pause"_s);
}

void TestEngineLogic::newTrackReportsWithinStaleCommandGrace()
{
    startPlaying(u"a"_s);
    m_logic->pause();
    m_now += 200;
    m_logic->stop();
    QVERIFY(m_logic->enqueue(u"spotify://track/b"_s));
    m_logic->play();
    m_now += 200;
    m_sink.calls.clear();
    m_logic->updateState(deviceState(u"dev"_s, u"b"_s, 300));
    QCOMPARE(m_sink.calls, (QStringList { u"state Playing"_s, u"track spotify://track/b Title b"_s }));
    QCOMPARE(m_logic->state(), EngineState::Playing);
}

void TestEngineLogic::restartPlaysFromStartWithoutPause()
{
    startPlaying(u"a"_s);
    m_logic->stop();
    QVERIFY(m_logic->enqueue(u"spotify://track/a"_s));
    m_logic->play();
    m_logic->flushPendingPause();
    QCOMPARE(m_sink.calls, (QStringList { u"state Stopped"_s, u"state Buffering"_s, u"play spotify:track:a 0"_s }));
}

void TestEngineLogic::restartWhilePausedPlaysFromStart()
{
    startPlaying(u"a"_s);
    m_logic->pause();
    m_logic->stop();
    QVERIFY(m_logic->enqueue(u"spotify://track/a"_s));
    m_logic->play();
    m_logic->flushPendingPause();
    QCOMPARE(m_sink.calls, (QStringList { u"pause"_s, u"state Paused"_s, u"state Stopped"_s, u"state Buffering"_s,
                                          u"play spotify:track:a 0"_s }));
}

void TestEngineLogic::nextSendsNoPause()
{
    startPlaying(u"a"_s);
    m_logic->stop();
    QVERIFY(m_logic->enqueue(u"spotify://track/b"_s));
    m_logic->play();
    m_logic->flushPendingPause();
    QCOMPARE(m_sink.calls, (QStringList { u"state Stopped"_s, u"state Buffering"_s, u"play spotify:track:b 0"_s }));
}

void TestEngineLogic::stopPausesRightAfterLocalResume()
{
    startPlaying(u"a"_s);
    m_logic->updateState(deviceState(u"dev"_s, u"a"_s, 8000, false));
    m_logic->pause();
    m_logic->stop();
    m_logic->flushPendingPause();
    QCOMPARE(m_sink.calls, (QStringList { u"state Paused"_s, u"resume"_s, u"state Playing"_s, u"state Stopped"_s, u"pause"_s }));
}

void TestEngineLogic::stopDoesNotPausePausedDevice()
{
    startPlaying(u"a"_s);
    m_logic->pause();
    m_logic->stop();
    m_logic->flushPendingPause();
    QCOMPARE(m_sink.calls, (QStringList { u"pause"_s, u"state Paused"_s, u"state Stopped"_s }));
}

void TestEngineLogic::adoptsAfterStopWasFlushed()
{
    startPlaying(u"a"_s);
    m_logic->stop();
    m_logic->flushPendingPause();
    m_logic->updateState(deviceState(u"dev"_s, u"a"_s, 4000));
    m_sink.calls.clear();
    QVERIFY(m_logic->enqueue(u"spotify://track/a"_s));
    m_logic->play();
    QCOMPARE(m_sink.calls, (QStringList { u"state Playing"_s, u"track spotify://track/a Title a"_s }));
}

void TestEngineLogic::ignoresFailureAfterStop()
{
    QVERIFY(m_logic->enqueue(u"spotify://track/a"_s));
    m_logic->play();
    const quint64 generation = m_sink.lastGeneration;
    m_logic->stop();
    m_sink.calls.clear();
    m_logic->playFailed(generation, u"late"_s, true);
    QVERIFY(m_sink.calls.isEmpty());
}

void TestEngineLogic::ignoresFailureOfSupersededPlay()
{
    QVERIFY(m_logic->enqueue(u"spotify://track/a"_s));
    m_logic->play();
    const quint64 first = m_sink.lastGeneration;
    m_logic->stop();
    QVERIFY(m_logic->enqueue(u"spotify://track/b"_s));
    m_now += EngineLogic::kStartGapMs;
    m_logic->play();
    const quint64 second = m_sink.lastGeneration;
    QVERIFY(first != second);
    m_sink.calls.clear();
    m_logic->playFailed(first, u"late"_s, true);
    QVERIFY(m_sink.calls.isEmpty());
    QCOMPARE(m_logic->trackIds(), QStringList { u"b"_s });
    m_logic->playFailed(second, u"now"_s, true);
    QCOMPARE(m_sink.calls, QStringList { u"fatal"_s });
    QVERIFY(m_logic->trackIds().isEmpty());
}

void TestEngineLogic::skipsWhenDeviceDoesNotStart()
{
    QVERIFY(m_logic->enqueue(u"spotify://track/a"_s));
    m_logic->play();
    m_sink.calls.clear();
    m_now += EngineLogic::kStartTimeoutMs - 1;
    m_logic->checkStartTimeout();
    QVERIFY(m_sink.calls.isEmpty());
    m_now += 1;
    m_logic->checkStartTimeout();
    QCOMPARE(m_sink.calls, QStringList { u"error"_s });
    QCOMPARE(m_sink.lastError, u"Spotify: the device did not start playing"_s);
    QVERIFY(!m_logic->isAwaitingStart());
    QVERIFY(m_logic->trackIds().isEmpty());
}

void TestEngineLogic::startTimeoutIgnoredOnceStarted()
{
    QVERIFY(m_logic->enqueue(u"spotify://track/a"_s));
    m_logic->play();
    m_logic->updateState(deviceState(u"dev"_s, u"a"_s, 300));
    m_sink.calls.clear();
    m_now += EngineLogic::kStartTimeoutMs;
    m_logic->checkStartTimeout();
    QVERIFY(m_sink.calls.isEmpty());
    QCOMPARE(m_logic->state(), EngineState::Playing);
}

void TestEngineLogic::secondConsecutiveStartTimeoutStops()
{
    QVERIFY(m_logic->enqueue(u"spotify://track/a"_s));
    m_logic->play();
    m_now += EngineLogic::kStartTimeoutMs;
    m_logic->checkStartTimeout();
    m_logic->stop();
    QVERIFY(m_logic->enqueue(u"spotify://track/b"_s));
    m_logic->play();
    m_sink.calls.clear();
    m_now += EngineLogic::kStartTimeoutMs;
    m_logic->checkStartTimeout();
    QCOMPARE(m_sink.calls, QStringList { u"fatal"_s });
    QCOMPARE(m_sink.lastError, u"Spotify: the device did not start playing"_s);
}

void TestEngineLogic::startTimeoutAfterSuccessfulStartSkips()
{
    QVERIFY(m_logic->enqueue(u"spotify://track/a"_s));
    m_logic->play();
    m_now += EngineLogic::kStartTimeoutMs;
    m_logic->checkStartTimeout();
    m_logic->stop();
    QVERIFY(m_logic->enqueue(u"spotify://track/b"_s));
    m_logic->play();
    m_logic->updateState(deviceState(u"dev"_s, u"b"_s, 300));
    m_logic->stop();
    QVERIFY(m_logic->enqueue(u"spotify://track/c"_s));
    m_logic->play();
    m_sink.calls.clear();
    m_now += EngineLogic::kStartTimeoutMs;
    m_logic->checkStartTimeout();
    QCOMPARE(m_sink.calls, QStringList { u"error"_s });
}

// the skip-to-queued call is about our queue mechanism, not the track: stop, do not skip
void TestEngineLogic::failedSkipToQueuedTrackStops()
{
    startPlaying(u"a"_s);
    QVERIFY(m_logic->enqueue(u"spotify://track/b"_s));
    m_now += 198500; // duration 200000 - kQueueLeadMs 1500
    m_logic->tick();
    m_now += 500;
    m_logic->updateState(deviceState(u"dev"_s, u"a"_s, 0, false));
    QCOMPARE(m_sink.calls.last(), u"next"_s);
    m_logic->playFailed(m_logic->activeGeneration(), u"gone"_s, false);
    QCOMPARE(m_sink.calls.last(), u"fatal"_s);
    QVERIFY(m_logic->trackIds().isEmpty());
}

// the device skipped, but not to our track (lost queue entry, Autoplay): qmmp must not wait for good
void TestEngineLogic::skipToQueuedTrackTimesOut()
{
    startPlaying(u"a"_s);
    QVERIFY(m_logic->enqueue(u"spotify://track/b"_s));
    m_now += 198500; // duration 200000 - kQueueLeadMs 1500
    m_logic->tick();
    m_now += 500;
    m_logic->updateState(deviceState(u"dev"_s, u"a"_s, 0, false));
    QCOMPARE(m_sink.calls.last(), u"next"_s);
    m_logic->updateState(deviceState(u"dev"_s, u"x"_s, 300));
    QVERIFY(m_logic->isAwaitingStart());
    m_sink.calls.clear();
    m_now += EngineLogic::kStartTimeoutMs;
    m_logic->checkStartTimeout();
    QCOMPARE(m_sink.calls, QStringList { u"error"_s });
    QVERIFY(!m_logic->isAwaitingStart());
}

void TestEngineLogic::failureAfterStartSendsNoStalePause()
{
    startPlaying(u"a"_s);
    m_logic->stop();
    QVERIFY(m_logic->enqueue(u"spotify://track/b"_s));
    m_logic->play();
    m_logic->updateState(deviceState(u"dev"_s, u"b"_s, 300));
    m_logic->playFailed(m_sink.lastGeneration, u"late"_s, false);
    m_logic->stop();
    m_logic->flushPendingPause();
    QVERIFY(!m_sink.calls.contains(u"pause"_s));
}

void TestEngineLogic::unavailableTrackFailsWithoutRequest()
{
    QVERIFY(m_logic->enqueue(u"spotify://track/a?unavailable"_s));
    m_logic->play();
    QCOMPARE(m_sink.calls, (QStringList { u"state Buffering"_s, u"error"_s }));
    QVERIFY(m_sink.lastError.isEmpty());
    QVERIFY(m_logic->trackIds().isEmpty());
    QVERIFY(!m_logic->isAwaitingStart());
}

void TestEngineLogic::unavailableNextIsSkippedAtTrackEnd()
{
    startPlaying(u"a"_s);
    QVERIFY(m_logic->enqueue(u"spotify://track/b?unavailable"_s));
    m_now += 199000;
    m_logic->tick();
    m_logic->updateState(deviceState(u"dev"_s, u"a"_s, 0, false));
    QCOMPARE(m_sink.calls, (QStringList { u"finished"_s, u"state Stopped"_s, u"state Buffering"_s, u"error"_s }));
    QVERIFY(m_sink.lastError.isEmpty());
    m_logic->stop();
    m_logic->flushPendingPause();
    QCOMPARE(m_sink.calls.last(), u"pause"_s);
}

void TestEngineLogic::supersededStartIsCancelled()
{
    QVERIFY(m_logic->enqueue(u"spotify://track/a"_s));
    m_logic->play();
    m_sink.cancels = 0;
    m_logic->stop();
    QCOMPARE(m_sink.cancels, 1);

    QVERIFY(m_logic->enqueue(u"spotify://track/b"_s));
    m_logic->play();
    m_sink.cancels = 0;
    m_now += EngineLogic::kStartTimeoutMs;
    m_logic->checkStartTimeout();
    QCOMPARE(m_sink.cancels, 1);
}

void TestEngineLogic::logoutStopsQuietly()
{
    startPlaying(u"a"_s);
    m_logic->sessionEnded();
    QCOMPARE(m_sink.calls, QStringList { u"fatal"_s });
    QVERIFY(m_sink.lastError.isEmpty());
    m_logic->stop();
    m_logic->flushPendingPause();
    QCOMPARE(m_sink.calls, QStringList { u"fatal"_s });
    QVERIFY(m_logic->trackIds().isEmpty());
}

void TestEngineLogic::logoutDropsPendingPause()
{
    startPlaying(u"a"_s);
    m_logic->stop();
    m_logic->sessionEnded();
    m_logic->flushPendingPause();
    QVERIFY(!m_sink.calls.contains(u"pause"_s));
}

QTEST_APPLESS_MAIN(TestEngineLogic)
#include "tst_enginelogic.moc"
