#include <QtTest>
#include "spotifylink.h"

using namespace Qt::StringLiterals;

class TestSpotifyLink : public QObject
{
    Q_OBJECT
private slots:
    void parse_data();
    void parse();
    void rejects_data();
    void rejects();
    void formats();
};

void TestSpotifyLink::parse_data()
{
    QTest::addColumn<QString>("text");
    QTest::addColumn<int>("kind");
    QTest::addColumn<QString>("id");
    QTest::newRow("qmmp path") << u"spotify://track/4uLU6hMCjMI75M1A2tKUQC"_s << int(SpotifyLink::Track) << u"4uLU6hMCjMI75M1A2tKUQC"_s;
    QTest::newRow("uri") << u"spotify:album:1DFixLWuPkv3KT3TnV35m3"_s << int(SpotifyLink::Album) << u"1DFixLWuPkv3KT3TnV35m3"_s;
    QTest::newRow("web") << u"https://open.spotify.com/playlist/37i9dQZF1DXcBWIGoYBM5M"_s << int(SpotifyLink::Playlist) << u"37i9dQZF1DXcBWIGoYBM5M"_s;
    QTest::newRow("web with query") << u"https://open.spotify.com/track/4uLU6hMCjMI75M1A2tKUQC?si=a1b2c3"_s << int(SpotifyLink::Track) << u"4uLU6hMCjMI75M1A2tKUQC"_s;
    QTest::newRow("web intl") << u"https://open.spotify.com/intl-de/album/1DFixLWuPkv3KT3TnV35m3"_s << int(SpotifyLink::Album) << u"1DFixLWuPkv3KT3TnV35m3"_s;
    QTest::newRow("surrounding spaces") << u"  spotify:track:4uLU6hMCjMI75M1A2tKUQC\n"_s << int(SpotifyLink::Track) << u"4uLU6hMCjMI75M1A2tKUQC"_s;
}

void TestSpotifyLink::parse()
{
    QFETCH(QString, text);
    QFETCH(int, kind);
    QFETCH(QString, id);
    const SpotifyLink link = SpotifyLink::parse(text);
    QCOMPARE(int(link.kind), kind);
    QCOMPARE(link.id, id);
}

void TestSpotifyLink::rejects_data()
{
    QTest::addColumn<QString>("text");
    QTest::newRow("artist") << u"spotify:artist:0OdUWJ0sBjDrqHygGUXeCF"_s;
    QTest::newRow("other host") << u"https://example.com/track/4uLU6hMCjMI75M1A2tKUQC"_s;
    QTest::newRow("missing id") << u"https://open.spotify.com/track/"_s;
    QTest::newRow("local file") << u"file:///music/a.mp3"_s;
    QTest::newRow("empty") << QString();
}

void TestSpotifyLink::rejects()
{
    QFETCH(QString, text);
    QVERIFY(!SpotifyLink::parse(text).isValid());
}

void TestSpotifyLink::formats()
{
    const SpotifyLink link = SpotifyLink::parse(u"https://open.spotify.com/track/abc?si=x"_s);
    QCOMPARE(link.uri(), u"spotify:track:abc"_s);
    QCOMPARE(link.path(), u"spotify://track/abc"_s);
    QCOMPARE(SpotifyLink::trackPath(u"abc"_s), u"spotify://track/abc"_s);
}

QTEST_APPLESS_MAIN(TestSpotifyLink)
#include "tst_spotifylink.moc"
