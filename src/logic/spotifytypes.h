#ifndef SPOTIFYTYPES_H
#define SPOTIFYTYPES_H

#include <QList>
#include <QMetaType>
#include <QString>
#include <QStringList>

struct SpotifyTrack
{
    QString id;
    QString title;
    QStringList artists;
    QString album;
    int trackNumber = 0;
    qint64 durationMs = 0;
    // false: not playable in the user's country (is_playable)
    bool playable = true;

    bool isValid() const { return !id.isEmpty(); }
    // never empty: an empty title would make the link expander expand the track again and again
    QString playlistTitle() const
    {
        const QString name = title.isEmpty() ? id : title;
        return playable ? name : QStringLiteral("(unavailable) ") + name;
    }
};

struct SpotifyDevice
{
    QString id;
    QString name;
    QString type;
    bool isActive = false;

    bool isValid() const { return !id.isEmpty(); }
};

struct SpotifyPlaylist
{
    QString id;
    QString name;
    int trackCount = 0;
    QString ownerId;
    bool collaborative = false;

    bool isValid() const { return !id.isEmpty(); }
};

struct PlayerState
{
    bool hasPlayback = false;
    QString deviceId;
    QString deviceName;
    bool isPlaying = false;
    qint64 progressMs = 0;
    SpotifyTrack track;
    int volumePercent = -1;
    bool supportsVolume = true;
};

template <typename T>
struct SpotifyPage
{
    QList<T> items;
    QString next;
};

Q_DECLARE_METATYPE(PlayerState)

#endif
