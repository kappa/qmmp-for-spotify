#include <QRegularExpression>
#include "spotifylink.h"

using namespace Qt::StringLiterals;

namespace {

QString kindName(SpotifyLink::Kind kind)
{
    switch(kind)
    {
    case SpotifyLink::Track: return u"track"_s;
    case SpotifyLink::Album: return u"album"_s;
    case SpotifyLink::Playlist: return u"playlist"_s;
    default: return QString();
    }
}

SpotifyLink::Kind kindFromName(const QString &name)
{
    if(name == "track"_L1)
        return SpotifyLink::Track;
    if(name == "album"_L1)
        return SpotifyLink::Album;
    if(name == "playlist"_L1)
        return SpotifyLink::Playlist;
    return SpotifyLink::Invalid;
}

}

QString SpotifyLink::uri() const
{
    return u"spotify:%1:%2"_s.arg(kindName(kind), id);
}

QString SpotifyLink::path() const
{
    return u"spotify://%1/%2"_s.arg(kindName(kind), id);
}

QString SpotifyLink::trackPath(const QString &id)
{
    return u"spotify://track/"_s + id;
}

SpotifyLink SpotifyLink::parse(const QString &text)
{
    static const QRegularExpression patterns[] = {
        QRegularExpression(u"^spotify://(track|album|playlist)/([A-Za-z0-9]+)/?$"_s),
        QRegularExpression(u"^spotify:(track|album|playlist):([A-Za-z0-9]+)$"_s),
        QRegularExpression(u"^https?://open\\.spotify\\.com/(?:intl-[A-Za-z-]+/)?(track|album|playlist)/([A-Za-z0-9]+)(?:[/?#].*)?$"_s),
    };
    const QString trimmed = text.trimmed();
    for(const QRegularExpression &pattern : patterns)
    {
        const QRegularExpressionMatch match = pattern.match(trimmed);
        if(match.hasMatch())
            return SpotifyLink { kindFromName(match.captured(1)), match.captured(2) };
    }
    return SpotifyLink();
}
