#include "playlistfilter.h"

QList<SpotifyPlaylist> readablePlaylists(const QList<SpotifyPlaylist> &playlists, const QString &userId)
{
    QList<SpotifyPlaylist> result;
    for(const SpotifyPlaylist &playlist : playlists)
    {
        if(playlist.ownerId == userId || playlist.collaborative)
            result << playlist;
    }
    return result;
}
