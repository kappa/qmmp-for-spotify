#ifndef PLAYLISTFILTER_H
#define PLAYLISTFILTER_H

#include <QList>
#include <QString>
#include "spotifytypes.h"

QList<SpotifyPlaylist> readablePlaylists(const QList<SpotifyPlaylist> &playlists, const QString &userId);

#endif
