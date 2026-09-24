#ifndef TRACKBUILDER_H
#define TRACKBUILDER_H

#include <QList>
#include "spotifytypes.h"

class PlayListTrack;

PlayListTrack *createPlayListTrack(const SpotifyTrack &track);
QList<PlayListTrack *> createPlayListTracks(const QList<SpotifyTrack> &tracks);

#endif
