#include <QtTest>
#include "volumelogic.h"

using namespace Qt::StringLiterals;

namespace {

// setQmmpVolume echoes back like SoundCore::setVolume: it unmutes, then reports the volume,
// both synchronously
class FakeSink : public VolumeSink
{
public:
    QStringList calls;
    VolumeLogic *logic = nullptr;

    void apiSetVolume(int percent) override { calls << u"api %1"_s.arg(percent); }
    void setQmmpVolume(int percent) override
    {
        calls << u"qmmp %1"_s.arg(percent);
        logic->qmmpMutedChanged(false);
        logic->qmmpVolumeChanged(percent);
    }
    void reportVolumeUnsupported() override { calls << u"unsupported"_s; }
};

PlayerState deviceState(const QString &device, int volume, bool supportsVolume = true)
{
    PlayerState state;
    state.hasPlayback = true;
    state.deviceId = device;
    state.isPlaying = true;
    state.volumePercent = volume;
    state.supportsVolume = supportsVolume;
    return state;
}

}

class TestVolumeLogic : public QObject
{
    Q_OBJECT
private slots:
    void init();
    void cleanup();
    void debounceSendsOnlyLastValue();
    void inactiveIgnoresQmmpChanges();
    void muteSendsZeroAndUnmuteRestores();
    void sliderWhileMutedSendsNewLevel();
    void burstBackToDeviceValueSendsNothing();
    void deviceChangeMovesQmmpWithoutEcho();
    void deviceChangeUnmutesQmmpWithoutEcho();
    void pollsBeforeOwnChangeLandsAreIgnored();
    void deviceWinsAfterEchoGrace();
    void pollDuringPendingChangeIsIgnored();
    void otherDeviceIsIgnored();
    void unknownDeviceVolumeLeavesQmmp();
    void inactiveDoesNotMoveQmmp();
    void unsupportedDeviceNoticesOnceAndSnapsBack();
    void disallowedRejectionStopsSending();
    void newDeviceClearsRejection();
    void failedSendLetsNextPollWin();

private:
    void settle(int deviceVolume);

    qint64 m_now = 0;
    FakeSink m_sink;
    VolumeLogic *m_logic = nullptr;
};

void TestVolumeLogic::init()
{
    m_now = 1000;
    m_sink.calls.clear();
    m_logic = new VolumeLogic(&m_sink, [this] { return m_now; });
    m_sink.logic = m_logic;
    m_logic->setSelectedDevice(u"dev"_s);
    m_logic->setQmmpState(50, false);
    m_logic->setActive(true);
}

void TestVolumeLogic::cleanup()
{
    delete m_logic;
}

// the device reports the volume qmmp already shows
void TestVolumeLogic::settle(int deviceVolume)
{
    m_logic->updateState(deviceState(u"dev"_s, deviceVolume));
    QCOMPARE(m_sink.calls, QStringList());
}

void TestVolumeLogic::debounceSendsOnlyLastValue()
{
    m_logic->qmmpVolumeChanged(55);
    m_now += 100;
    m_logic->qmmpVolumeChanged(60);
    m_now += 100;
    m_logic->qmmpVolumeChanged(65);
    QVERIFY(m_logic->hasPendingSend());
    m_now += VolumeLogic::kDebounceMs - 1;
    m_logic->tick();
    QCOMPARE(m_sink.calls, QStringList());
    m_now += 1;
    m_logic->tick();
    QCOMPARE(m_sink.calls, QStringList { u"api 65"_s });
    QVERIFY(!m_logic->hasPendingSend());
    m_now += 1000;
    m_logic->tick();
    QCOMPARE(m_sink.calls, QStringList { u"api 65"_s });
}

void TestVolumeLogic::inactiveIgnoresQmmpChanges()
{
    m_logic->setActive(false);
    m_logic->qmmpVolumeChanged(70);
    m_logic->qmmpMutedChanged(true);
    QVERIFY(!m_logic->hasPendingSend());
    m_now += 1000;
    m_logic->tick();
    QCOMPARE(m_sink.calls, QStringList());
}

void TestVolumeLogic::muteSendsZeroAndUnmuteRestores()
{
    settle(50);
    m_logic->qmmpMutedChanged(true);
    m_now += VolumeLogic::kDebounceMs;
    m_logic->tick();
    QCOMPARE(m_sink.calls, QStringList { u"api 0"_s });
    m_logic->qmmpMutedChanged(false);
    m_now += VolumeLogic::kDebounceMs;
    m_logic->tick();
    QCOMPARE(m_sink.calls, (QStringList { u"api 0"_s, u"api 50"_s }));
}

void TestVolumeLogic::sliderWhileMutedSendsNewLevel()
{
    settle(50);
    m_logic->qmmpMutedChanged(true);
    m_now += VolumeLogic::kDebounceMs;
    m_logic->tick();
    m_sink.calls.clear();
    // SoundCore::setVolume unmutes first
    m_logic->qmmpMutedChanged(false);
    m_logic->qmmpVolumeChanged(70);
    m_now += VolumeLogic::kDebounceMs;
    m_logic->tick();
    QCOMPARE(m_sink.calls, QStringList { u"api 70"_s });
}

void TestVolumeLogic::burstBackToDeviceValueSendsNothing()
{
    settle(50);
    m_logic->qmmpVolumeChanged(60);
    m_now += 100;
    m_logic->qmmpVolumeChanged(50);
    m_now += VolumeLogic::kDebounceMs;
    m_logic->tick();
    QCOMPARE(m_sink.calls, QStringList());
}

void TestVolumeLogic::deviceChangeMovesQmmpWithoutEcho()
{
    settle(50);
    m_logic->updateState(deviceState(u"dev"_s, 30));
    QCOMPARE(m_sink.calls, QStringList { u"qmmp 30"_s });
    QVERIFY(!m_logic->hasPendingSend());
    // a delayed volumeChanged from qmmp's mixer (e.g. after SoundCore::stop reloads it)
    m_logic->qmmpVolumeChanged(30);
    m_now += 1000;
    m_logic->tick();
    m_logic->updateState(deviceState(u"dev"_s, 30));
    QCOMPARE(m_sink.calls, QStringList { u"qmmp 30"_s });
}

void TestVolumeLogic::deviceChangeUnmutesQmmpWithoutEcho()
{
    settle(50);
    m_logic->qmmpMutedChanged(true);
    m_now += VolumeLogic::kDebounceMs;
    m_logic->tick();
    m_logic->updateState(deviceState(u"dev"_s, 0));
    m_now += 1000;
    m_logic->updateState(deviceState(u"dev"_s, 40));
    QCOMPARE(m_sink.calls, (QStringList { u"api 0"_s, u"qmmp 40"_s }));
    QVERIFY(!m_logic->hasPendingSend());
}

void TestVolumeLogic::pollsBeforeOwnChangeLandsAreIgnored()
{
    settle(50);
    m_logic->qmmpVolumeChanged(65);
    m_now += VolumeLogic::kDebounceMs;
    m_logic->tick();
    m_now += 500;
    m_logic->updateState(deviceState(u"dev"_s, 50));
    m_now += 500;
    m_logic->updateState(deviceState(u"dev"_s, 65));
    m_now += VolumeLogic::kEchoGraceMs;
    m_logic->updateState(deviceState(u"dev"_s, 65));
    QCOMPARE(m_sink.calls, QStringList { u"api 65"_s });
}

void TestVolumeLogic::deviceWinsAfterEchoGrace()
{
    settle(50);
    m_logic->qmmpVolumeChanged(65);
    m_now += VolumeLogic::kDebounceMs;
    m_logic->tick();
    m_now += VolumeLogic::kEchoGraceMs;
    m_logic->updateState(deviceState(u"dev"_s, 50));
    QCOMPARE(m_sink.calls, (QStringList { u"api 65"_s, u"qmmp 50"_s }));
}

void TestVolumeLogic::pollDuringPendingChangeIsIgnored()
{
    settle(50);
    m_logic->qmmpVolumeChanged(60);
    m_logic->updateState(deviceState(u"dev"_s, 50));
    m_now += VolumeLogic::kDebounceMs;
    m_logic->tick();
    QCOMPARE(m_sink.calls, QStringList { u"api 60"_s });
}

void TestVolumeLogic::otherDeviceIsIgnored()
{
    m_logic->updateState(deviceState(u"other"_s, 20, false));
    QCOMPARE(m_sink.calls, QStringList());
    m_logic->qmmpVolumeChanged(60);
    m_now += VolumeLogic::kDebounceMs;
    m_logic->tick();
    QCOMPARE(m_sink.calls, QStringList { u"api 60"_s });
}

void TestVolumeLogic::unknownDeviceVolumeLeavesQmmp()
{
    m_logic->updateState(deviceState(u"dev"_s, -1));
    m_logic->updateState(PlayerState());
    QCOMPARE(m_sink.calls, QStringList());
}

void TestVolumeLogic::inactiveDoesNotMoveQmmp()
{
    m_logic->setActive(false);
    m_logic->updateState(deviceState(u"dev"_s, 30));
    QCOMPARE(m_sink.calls, QStringList());
    m_logic->setActive(true);
    m_logic->updateState(deviceState(u"dev"_s, 30));
    QCOMPARE(m_sink.calls, QStringList { u"qmmp 30"_s });
}

void TestVolumeLogic::unsupportedDeviceNoticesOnceAndSnapsBack()
{
    m_logic->updateState(deviceState(u"dev"_s, 50, false));
    m_logic->qmmpVolumeChanged(60);
    m_logic->qmmpVolumeChanged(70);
    QVERIFY(!m_logic->hasPendingSend());
    m_now += VolumeLogic::kDebounceMs;
    m_logic->tick();
    QCOMPARE(m_sink.calls, QStringList { u"unsupported"_s });
    m_logic->updateState(deviceState(u"dev"_s, 50, false));
    QCOMPARE(m_sink.calls, (QStringList { u"unsupported"_s, u"qmmp 50"_s }));
}

void TestVolumeLogic::disallowedRejectionStopsSending()
{
    settle(50);
    m_logic->qmmpVolumeChanged(65);
    m_now += VolumeLogic::kDebounceMs;
    m_logic->tick();
    m_logic->setVolumeFailed(true);
    QCOMPARE(m_sink.calls, (QStringList { u"api 65"_s, u"unsupported"_s }));
    m_logic->qmmpVolumeChanged(70);
    m_now += VolumeLogic::kDebounceMs;
    m_logic->tick();
    m_logic->updateState(deviceState(u"dev"_s, 50));
    QCOMPARE(m_sink.calls, (QStringList { u"api 65"_s, u"unsupported"_s, u"qmmp 50"_s }));
}

void TestVolumeLogic::newDeviceClearsRejection()
{
    m_logic->qmmpVolumeChanged(65);
    m_now += VolumeLogic::kDebounceMs;
    m_logic->tick();
    m_logic->setVolumeFailed(true);
    m_sink.calls.clear();
    m_logic->setSelectedDevice(u"dev2"_s);
    m_logic->qmmpVolumeChanged(70);
    m_now += VolumeLogic::kDebounceMs;
    m_logic->tick();
    QCOMPARE(m_sink.calls, QStringList { u"api 70"_s });
}

void TestVolumeLogic::failedSendLetsNextPollWin()
{
    settle(50);
    m_logic->qmmpVolumeChanged(65);
    m_now += VolumeLogic::kDebounceMs;
    m_logic->tick();
    m_logic->setVolumeFailed(false);
    m_logic->updateState(deviceState(u"dev"_s, 50));
    QCOMPARE(m_sink.calls, (QStringList { u"api 65"_s, u"qmmp 50"_s }));
    m_logic->qmmpVolumeChanged(60);
    m_now += VolumeLogic::kDebounceMs;
    m_logic->tick();
    QCOMPARE(m_sink.calls, (QStringList { u"api 65"_s, u"qmmp 50"_s, u"api 60"_s }));
}

QTEST_APPLESS_MAIN(TestVolumeLogic)
#include "tst_volumelogic.moc"
