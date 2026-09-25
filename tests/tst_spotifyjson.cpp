#include <QtTest>
#include "spotifyjson.h"

using namespace Qt::StringLiterals;

class TestSpotifyJson : public QObject
{
    Q_OBJECT
private slots:
    void devices();
    void playerStatePlaying();
    void playerStateNothingPlaying();
    void playerStateEpisode();
    void playlists();
    void trackItemsSkipUnplayable();
    void album();
    void displayName();
    void userId();
};

void TestSpotifyJson::devices()
{
    const QList<SpotifyDevice> devices = SpotifyJson::parseDevices(R"({"devices":[
        {"id":"d1","is_active":true,"name":"Kitchen","type":"Speaker"},
        {"id":null,"is_active":false,"name":"Restricted","type":"Computer"},
        {"id":"d2","is_active":false,"name":"Phone","type":"Smartphone"}]})");
    QCOMPARE(devices.size(), 2);
    QCOMPARE(devices.at(0).name, u"Kitchen"_s);
    QVERIFY(devices.at(0).isActive);
    QCOMPARE(devices.at(1).id, u"d2"_s);
}

void TestSpotifyJson::playerStatePlaying()
{
    const PlayerState s = SpotifyJson::parsePlayerState(R"({
        "device":{"id":"d1","name":"Kitchen"},"is_playing":true,"progress_ms":42000,
        "currently_playing_type":"track",
        "item":{"type":"track","id":"t1","name":"Song","duration_ms":200000,"track_number":3,
                "artists":[{"name":"A"},{"name":"B"}],"album":{"name":"LP"}}})");
    QVERIFY(s.hasPlayback);
    QCOMPARE(s.deviceId, u"d1"_s);
    QVERIFY(s.isPlaying);
    QCOMPARE(s.progressMs, qint64(42000));
    QCOMPARE(s.track.id, u"t1"_s);
    QCOMPARE(s.track.artists, (QStringList { u"A"_s, u"B"_s }));
    QCOMPARE(s.track.album, u"LP"_s);
    QCOMPARE(s.track.trackNumber, 3);
    QCOMPARE(s.track.durationMs, qint64(200000));
}

void TestSpotifyJson::playerStateNothingPlaying()
{
    QVERIFY(!SpotifyJson::parsePlayerState(QByteArray()).hasPlayback);
}

void TestSpotifyJson::playerStateEpisode()
{
    const PlayerState s = SpotifyJson::parsePlayerState(R"({
        "device":{"id":"d1","name":"Kitchen"},"is_playing":true,"progress_ms":1,
        "item":{"type":"episode","id":"e1","name":"Podcast"}})");
    QVERIFY(s.hasPlayback);
    QVERIFY(!s.track.isValid());
}

void TestSpotifyJson::playlists()
{
    const SpotifyPage<SpotifyPlaylist> page = SpotifyJson::parsePlaylists(R"({
        "items":[{"id":"p1","name":"Old style","tracks":{"total":12},"owner":{"id":"alex123"},"collaborative":false},
                 {"id":"p2","name":"New style","items":{"total":7},"owner":{"id":"friend"},"collaborative":true}],
        "next":"https://api.spotify.com/v1/me/playlists?offset=50&limit=50"})");
    QCOMPARE(page.items.size(), 2);
    QCOMPARE(page.items.at(0).trackCount, 12);
    QCOMPARE(page.items.at(0).ownerId, u"alex123"_s);
    QVERIFY(!page.items.at(0).collaborative);
    QCOMPARE(page.items.at(1).trackCount, 7);
    QCOMPARE(page.items.at(1).ownerId, u"friend"_s);
    QVERIFY(page.items.at(1).collaborative);
    QCOMPARE(page.next, u"https://api.spotify.com/v1/me/playlists?offset=50&limit=50"_s);
}

void TestSpotifyJson::trackItemsSkipUnplayable()
{
    const SpotifyPage<SpotifyTrack> page = SpotifyJson::parseTrackItems(R"({"items":[
        {"track":{"type":"track","id":"t1","name":"One","duration_ms":1000,"artists":[],"album":{"name":"X"}}},
        {"item":{"type":"track","id":"t2","name":"Two","duration_ms":2000,"artists":[],"album":{"name":"Y"}}},
        {"track":{"type":"track","id":null,"name":"Local","is_local":true}},
        {"track":{"type":"episode","id":"e1","name":"Episode"}},
        {"track":null},
        {"track":{"type":"track","id":"t3","name":"Gone","is_playable":false}}],
        "next":null})");
    QCOMPARE(page.items.size(), 2);
    QCOMPARE(page.items.at(0).id, u"t1"_s);
    QCOMPARE(page.items.at(1).album, u"Y"_s);
    QVERIFY(page.next.isEmpty());
}

void TestSpotifyJson::album()
{
    QString name;
    const SpotifyPage<SpotifyTrack> page = SpotifyJson::parseAlbum(R"({"name":"LP","tracks":{
        "items":[{"type":"track","id":"t1","name":"One","track_number":1,"duration_ms":1000,"artists":[{"name":"A"}]}],
        "next":"https://api.spotify.com/v1/albums/a1/tracks?offset=50"}})", &name);
    QCOMPARE(name, u"LP"_s);
    QCOMPARE(page.items.size(), 1);
    QCOMPARE(page.items.at(0).album, u"LP"_s);
    QCOMPARE(page.next, u"https://api.spotify.com/v1/albums/a1/tracks?offset=50"_s);
}

void TestSpotifyJson::displayName()
{
    QCOMPARE(SpotifyJson::parseDisplayName(R"({"id":"alex123","display_name":"Alex"})"), u"Alex"_s);
    QCOMPARE(SpotifyJson::parseDisplayName(R"({"id":"alex123","display_name":null})"), u"alex123"_s);
}

void TestSpotifyJson::userId()
{
    QCOMPARE(SpotifyJson::parseUserId(R"({"id":"alex123","display_name":"Alex"})"), u"alex123"_s);
}

QTEST_APPLESS_MAIN(TestSpotifyJson)
#include "tst_spotifyjson.moc"
