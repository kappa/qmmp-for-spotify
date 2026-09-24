#include "followerlogic.h"
#include "spotifylink.h"

FollowDecision decideFollow(const PlayerState &state, const QString &selectedDevice, const QStringList &engineTrackIds,
                            bool engineAwaiting, const QStringList &playlistPaths, int currentIndex)
{
    if(engineAwaiting || selectedDevice.isEmpty() || !state.hasPlayback || !state.isPlaying
            || state.deviceId != selectedDevice || !state.track.isValid() || engineTrackIds.contains(state.track.id))
        return FollowDecision();

    const QString path = SpotifyLink::trackPath(state.track.id);
    int index = playlistPaths.indexOf(path, qMax(0, currentIndex + 1));
    if(index < 0)
        index = playlistPaths.indexOf(path);
    if(index >= 0)
        return FollowDecision { FollowDecision::Select, index };
    return FollowDecision { FollowDecision::Append, -1 };
}

bool needsExpansion(const QString &path, const QString &title)
{
    const SpotifyLink link = SpotifyLink::parse(path);
    if(!link.isValid())
        return false;
    return link.kind != SpotifyLink::Track || path != link.path() || title.isEmpty();
}

bool isFollowTrigger(const PlayerState &previous, const PlayerState &current, const QString &selectedDevice)
{
    if(!current.hasPlayback || !current.isPlaying || current.deviceId != selectedDevice || !current.track.isValid())
        return false;
    return previous.deviceId != selectedDevice || previous.track.id != current.track.id || !previous.isPlaying;
}
