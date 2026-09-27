#ifndef SPOTIFYLINK_H
#define SPOTIFYLINK_H

#include <QString>

struct SpotifyLink
{
    enum Kind { Invalid, Track, Album, Playlist };

    Kind kind = Invalid;
    QString id;
    // a playlist path marked "?unavailable": the track can't be played in the user's country
    bool unavailable = false;

    bool isValid() const { return kind != Invalid; }
    // Spotify IDs are 22 base62 characters; a link with another ID is broken
    bool hasValidId() const;
    QString uri() const;
    // with the unavailable marker, if set
    QString path() const;

    static SpotifyLink parse(const QString &text);
    static QString trackPath(const QString &id, bool playable = true);
};

#endif
