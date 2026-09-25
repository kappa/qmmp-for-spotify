#include <QtTest>
#include "playlistfilter.h"

using namespace Qt::StringLiterals;

class TestPlaylistFilter : public QObject
{
    Q_OBJECT
private slots:
    void keepsOwnAndCollaborativeDropsOthers();
    void preservesOrder();
};

namespace {

SpotifyPlaylist playlist(const QString &id, const QString &ownerId, bool collaborative = false)
{
    SpotifyPlaylist p;
    p.id = id;
    p.name = id;
    p.ownerId = ownerId;
    p.collaborative = collaborative;
    return p;
}

}

void TestPlaylistFilter::keepsOwnAndCollaborativeDropsOthers()
{
    const QList<SpotifyPlaylist> playlists {
        playlist(u"p1"_s, u"me"_s),
        playlist(u"p2"_s, u"friend"_s),
        playlist(u"p3"_s, u"friend"_s, true),
    };
    const QList<SpotifyPlaylist> readable = readablePlaylists(playlists, u"me"_s);
    QCOMPARE(readable.size(), 2);
    QCOMPARE(readable.at(0).id, u"p1"_s);
    QCOMPARE(readable.at(1).id, u"p3"_s);
}

void TestPlaylistFilter::preservesOrder()
{
    const QList<SpotifyPlaylist> playlists {
        playlist(u"p1"_s, u"friend"_s, true),
        playlist(u"p2"_s, u"me"_s),
        playlist(u"p3"_s, u"friend"_s),
        playlist(u"p4"_s, u"me"_s),
    };
    const QList<SpotifyPlaylist> readable = readablePlaylists(playlists, u"me"_s);
    QCOMPARE(readable.size(), 3);
    QCOMPARE(readable.at(0).id, u"p1"_s);
    QCOMPARE(readable.at(1).id, u"p2"_s);
    QCOMPARE(readable.at(2).id, u"p4"_s);
}

QTEST_APPLESS_MAIN(TestPlaylistFilter)
#include "tst_playlistfilter.moc"
