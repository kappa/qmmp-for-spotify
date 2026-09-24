#ifndef SPOTIFYLINK_H
#define SPOTIFYLINK_H

#include <QString>

struct SpotifyLink
{
    enum Kind { Invalid, Track, Album, Playlist };

    Kind kind = Invalid;
    QString id;

    bool isValid() const { return kind != Invalid; }
    QString uri() const;
    QString path() const;

    static SpotifyLink parse(const QString &text);
    static QString trackPath(const QString &id);
};

#endif
