#include <qmmpui/playlisttrack.h>
#include "spotifylink.h"
#include "trackbuilder.h"

using namespace Qt::StringLiterals;

PlayListTrack *createPlayListTrack(const SpotifyTrack &track)
{
    // the engine reads the unavailable marker from the path and skips the track without a request
    TrackInfo info(SpotifyLink::trackPath(track.id, track.playable));
    info.setDuration(track.durationMs);
    info.setValue(Qmmp::TITLE, track.playlistTitle());
    info.setValue(Qmmp::ARTIST, track.artists.join(u", "_s));
    info.setValue(Qmmp::ALBUM, track.album);
    if(track.trackNumber > 0)
        info.setValue(Qmmp::TRACK, track.trackNumber);
    return new PlayListTrack(info);
}

QList<PlayListTrack *> createPlayListTracks(const QList<SpotifyTrack> &tracks)
{
    QList<PlayListTrack *> result;
    for(const SpotifyTrack &track : tracks)
        result << createPlayListTrack(track);
    return result;
}
