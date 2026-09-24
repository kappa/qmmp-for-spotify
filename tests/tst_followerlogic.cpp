#include <QtTest>
#include "followerlogic.h"

using namespace Qt::StringLiterals;

namespace {

PlayerState playing(const QString &device, const QString &trackId, bool isPlaying = true)
{
    PlayerState state;
    state.hasPlayback = true;
    state.deviceId = device;
    state.isPlaying = isPlaying;
    state.track.id = trackId;
    return state;
}

const QStringList kPlaylist = { u"spotify://track/a"_s, u"spotify://track/x"_s, u"/music/local.mp3"_s, u"spotify://track/x"_s };

}

class TestFollowerLogic : public QObject
{
    Q_OBJECT
private slots:
    void ignoresWhileEngineAwaiting();
    void ignoresOtherDeviceAndPaused();
    void ignoresEngineTracks();
    void selectsNextOccurrenceAfterCurrent();
    void selectsEarlierOccurrence();
    void appendsMissingTrack();
    void expansion();
};

void TestFollowerLogic::ignoresWhileEngineAwaiting()
{
    QCOMPARE(decideFollow(playing(u"dev"_s, u"x"_s), u"dev"_s, { u"a"_s }, true, kPlaylist, 0).action, FollowDecision::None);
}

void TestFollowerLogic::ignoresOtherDeviceAndPaused()
{
    QCOMPARE(decideFollow(playing(u"phone"_s, u"x"_s), u"dev"_s, {}, false, kPlaylist, 0).action, FollowDecision::None);
    QCOMPARE(decideFollow(playing(u"dev"_s, u"x"_s, false), u"dev"_s, {}, false, kPlaylist, 0).action, FollowDecision::None);
    QCOMPARE(decideFollow(PlayerState(), u"dev"_s, {}, false, kPlaylist, 0).action, FollowDecision::None);
}

void TestFollowerLogic::ignoresEngineTracks()
{
    QCOMPARE(decideFollow(playing(u"dev"_s, u"x"_s), u"dev"_s, { u"a"_s, u"x"_s }, false, kPlaylist, 0).action, FollowDecision::None);
}

void TestFollowerLogic::selectsNextOccurrenceAfterCurrent()
{
    const FollowDecision d = decideFollow(playing(u"dev"_s, u"x"_s), u"dev"_s, { u"a"_s }, false, kPlaylist, 2);
    QCOMPARE(d.action, FollowDecision::Select);
    QCOMPARE(d.index, 3);
}

void TestFollowerLogic::selectsEarlierOccurrence()
{
    const FollowDecision d = decideFollow(playing(u"dev"_s, u"a"_s), u"dev"_s, {}, false, kPlaylist, 3);
    QCOMPARE(d.action, FollowDecision::Select);
    QCOMPARE(d.index, 0);
}

void TestFollowerLogic::appendsMissingTrack()
{
    QCOMPARE(decideFollow(playing(u"dev"_s, u"new"_s), u"dev"_s, {}, false, kPlaylist, -1).action, FollowDecision::Append);
}

void TestFollowerLogic::expansion()
{
    QVERIFY(needsExpansion(u"spotify://album/abc"_s, QString()));
    QVERIFY(needsExpansion(u"https://open.spotify.com/track/abc?si=1"_s, u"https://open.spotify.com/track/abc?si=1"_s));
    QVERIFY(needsExpansion(u"spotify://track/abc"_s, QString()));
    QVERIFY(!needsExpansion(u"spotify://track/abc"_s, u"Song"_s));
    QVERIFY(!needsExpansion(u"/music/a.mp3"_s, QString()));
}

QTEST_APPLESS_MAIN(TestFollowerLogic)
#include "tst_followerlogic.moc"
