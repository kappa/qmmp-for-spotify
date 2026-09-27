#include <QRegularExpression>
#include "spotifylink.h"

using namespace Qt::StringLiterals;

namespace {

// marks the playlist path of a track that can't be played in the user's country
constexpr auto kUnavailableMarker = "?unavailable"_L1;

QString marker(bool unavailable)
{
    return unavailable ? QString(kUnavailableMarker) : QString();
}

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

bool SpotifyLink::hasValidId() const
{
    static const QRegularExpression base62(u"^[A-Za-z0-9]{22}$"_s);
    return isValid() && base62.match(id).hasMatch();
}

QString SpotifyLink::path() const
{
    return u"spotify://%1/%2"_s.arg(kindName(kind), id) + marker(unavailable);
}

QString SpotifyLink::trackPath(const QString &id, bool playable)
{
    return u"spotify://track/"_s + id + marker(!playable);
}

SpotifyLink SpotifyLink::parse(const QString &text)
{
    static const QRegularExpression patterns[] = {
        QRegularExpression(u"^spotify://(track|album|playlist)/([A-Za-z0-9]+)/?(\\?unavailable)?$"_s),
        QRegularExpression(u"^spotify:(track|album|playlist):([A-Za-z0-9]+)$"_s),
        QRegularExpression(u"^https?://open\\.spotify\\.com/(?:intl-[A-Za-z-]+/)?(track|album|playlist)/([A-Za-z0-9]+)(?:[/?#].*)?$"_s),
    };
    const QString trimmed = text.trimmed();
    for(const QRegularExpression &pattern : patterns)
    {
        const QRegularExpressionMatch match = pattern.match(trimmed);
        if(match.hasMatch())
            return SpotifyLink { kindFromName(match.captured(1)), match.captured(2), !match.captured(3).isEmpty() };
    }
    return SpotifyLink();
}
