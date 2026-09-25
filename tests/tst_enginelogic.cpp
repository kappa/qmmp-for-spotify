#include <QtTest>
#include "enginelogic.h"

using namespace Qt::StringLiterals;

namespace {

class FakeSink : public EngineSink
{
public:
    QStringList calls;
    quint64 lastGeneration = 0;
    QString lastError;

    void apiPlay(const QString &uri, qint64 positionMs, quint64 generation) override
    {
        calls << u"play %1 %2"_s.arg(uri).arg(positionMs);
        lastGeneration = generation;
    }
    void apiPause() override { calls << u"pause"_s; }
    void apiResume() override { calls << u"resume"_s; }
    void apiSeek(qint64 positionMs) override { calls << u"seek %1"_s.arg(positionMs); }
    void apiQueue(const QString &uri) override { calls << u"queue "_s + uri; }
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
    void enqueueDoesNotQueueImmediately();
    void tickQueuesOnceAtLeadTime();
    void tickDoesNotQueueWhilePaused();
    void switchingBeforeLeadTimeNeverQueuesAbandonedNext();
    void finishesWhenTrackEnds();
    void stopPausesOwnTrack();
    void stopLeavesForeignTrackAlone();
    void ignoresOtherDevices();
    void interpolatesElapsed();
    void playFailureClearsTracks();
    void newTrackReportsWithinStaleCommandGrace();
    void restartPlaysFromStartWithoutPause();
    void restartWhilePausedPlaysFromStart();
    void nextSendsNoPause();
    void stopPausesRightAfterLocalResume();
    void stopDoesNotPausePausedDevice();
    void adoptsAfterStopWasFlushed();
    void ignoresFailureAfterStop();
    void ignoresFailureOfSupersededPlay();
    void failsWhenDeviceDoesNotStart();
    void startTimeoutIgnoredOnceStarted();

private:
    void startPlaying(const QString &trackId);

    qint64 m_now = 0;
    FakeSink m_sink;
    EngineLogic *m_logic = nullptr;
};

void TestEngineLogic::init()
{
    m_now = 1000;
    m_sink.calls.clear();
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
    QCOMPARE(m_sink.calls.mid(1), (QStringList { u"finished"_s, u"state Stopped"_s,
                                          u"state Buffering"_s, u"play spotify:track:b 0"_s }));
    QVERIFY(m_logic->isAwaitingStart());
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
    m_logic->pause();
    m_sink.calls.clear();
    m_now += 199000;
    m_logic->tick();
    QVERIFY(m_sink.calls.isEmpty());
}

void TestEngineLogic::switchingBeforeLeadTimeNeverQueuesAbandonedNext()
{
    startPlaying(u"a"_s);
    QVERIFY(m_logic->enqueue(u"spotify://track/b"_s));
    m_now += 195000; // inside the old ~7 s announce window but before the 1.5 s lead time
    m_logic->tick();
    m_logic->stop();
    QVERIFY(!m_sink.calls.contains(u"queue spotify:track:b"_s));
}

void TestEngineLogic::finishesWhenTrackEnds()
{
    startPlaying(u"a"_s);
    m_now += 198500;
    m_logic->updateState(deviceState(u"dev"_s, u"a"_s, 0, false));
    QCOMPARE(m_sink.calls, (QStringList { u"finished"_s, u"state Stopped"_s }));
    QVERIFY(m_logic->trackIds().isEmpty());
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

void TestEngineLogic::stopLeavesForeignTrackAlone()
{
    startPlaying(u"a"_s);
    m_logic->updateState(deviceState(u"dev"_s, u"x"_s, 1000));
    m_logic->stop();
    m_logic->flushPendingPause();
    QCOMPARE(m_sink.calls, QStringList { u"state Stopped"_s });
}

void TestEngineLogic::ignoresOtherDevices()
{
    startPlaying(u"a"_s);
    m_logic->updateState(deviceState(u"phone"_s, u"z"_s, 1000, false));
    QVERIFY(m_sink.calls.isEmpty());
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

void TestEngineLogic::playFailureClearsTracks()
{
    QVERIFY(m_logic->enqueue(u"spotify://track/a"_s));
    m_logic->play();
    m_logic->playFailed(m_sink.lastGeneration, u"gone"_s);
    QCOMPARE(m_sink.calls.last(), u"fatal"_s);
    QCOMPARE(m_sink.lastError, u"gone"_s);
    QVERIFY(m_logic->trackIds().isEmpty());
    QVERIFY(!m_logic->isAwaitingStart());
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
    m_logic->playFailed(generation, u"late"_s);
    QVERIFY(m_sink.calls.isEmpty());
}

void TestEngineLogic::ignoresFailureOfSupersededPlay()
{
    QVERIFY(m_logic->enqueue(u"spotify://track/a"_s));
    m_logic->play();
    const quint64 first = m_sink.lastGeneration;
    m_logic->stop();
    QVERIFY(m_logic->enqueue(u"spotify://track/b"_s));
    m_logic->play();
    const quint64 second = m_sink.lastGeneration;
    QVERIFY(first != second);
    m_sink.calls.clear();
    m_logic->playFailed(first, u"late"_s);
    QVERIFY(m_sink.calls.isEmpty());
    QCOMPARE(m_logic->trackIds(), QStringList { u"b"_s });
    m_logic->playFailed(second, u"now"_s);
    QCOMPARE(m_sink.calls, QStringList { u"fatal"_s });
    QVERIFY(m_logic->trackIds().isEmpty());
}

void TestEngineLogic::failsWhenDeviceDoesNotStart()
{
    QVERIFY(m_logic->enqueue(u"spotify://track/a"_s));
    m_logic->play();
    m_sink.calls.clear();
    m_now += EngineLogic::kStartTimeoutMs - 1;
    m_logic->checkStartTimeout();
    QVERIFY(m_sink.calls.isEmpty());
    m_now += 1;
    m_logic->checkStartTimeout();
    QCOMPARE(m_sink.calls, QStringList { u"fatal"_s });
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

QTEST_APPLESS_MAIN(TestEngineLogic)
#include "tst_enginelogic.moc"
