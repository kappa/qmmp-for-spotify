#include "followerlogic.h"
#include "spotifylink.h"

using namespace Qt::StringLiterals;

FollowDecision decideFollow(const PlayerState &state, const QString &selectedDevice, const QStringList &engineTrackIds,
                            const QString &suppressedTrackId, bool engineAwaiting, const QStringList &playlistPaths, int currentIndex)
{
    if(engineAwaiting || selectedDevice.isEmpty() || !state.hasPlayback || !state.isPlaying
            || state.deviceId != selectedDevice || !state.track.isValid() || engineTrackIds.contains(state.track.id)
            || state.track.id == suppressedTrackId)
        return FollowDecision();

    const QString path = SpotifyLink::trackPath(state.track.id);
    int index = playlistPaths.indexOf(path, qMax(0, currentIndex + 1));
    if(index < 0)
        index = playlistPaths.indexOf(path);
    if(index >= 0)
        return FollowDecision { FollowDecision::Select, index };
    return FollowDecision { FollowDecision::Append, -1 };
}

bool endsSuppression(const PlayerState &state, const QString &selectedDevice, const QString &suppressedTrackId)
{
    if(suppressedTrackId.isEmpty() || !state.hasPlayback)
        return false;
    // once the device is paused, a resume from elsewhere is followed again, as after Stop
    if(!state.isPlaying)
        return state.deviceId == selectedDevice;
    return state.track.isValid() && state.track.id != suppressedTrackId;
}

bool needsExpansion(const QString &path, const QString &title)
{
    const SpotifyLink link = SpotifyLink::parse(path);
    if(!link.isValid())
        return false;
    return link.kind != SpotifyLink::Track || path != link.path() || title.isEmpty();
}

QString expansionFailureMessage(const QString &path, int httpStatus, const QString &apiMessage)
{
    // Spotify's own text ("bad base62 URL") says nothing to the user
    if(httpStatus == 400)
        return u"Spotify: cannot add this link"_s;
    if(SpotifyLink::parse(path).kind == SpotifyLink::Playlist && (httpStatus == 403 || httpStatus == 404))
        return u"Spotify: this app can't read other users' playlists"_s;
    return apiMessage;
}

bool removesFailedLink(int httpStatus)
{
    return httpStatus == 400 || httpStatus == 403 || httpStatus == 404;
}

bool isFollowTrigger(const PlayerState &previous, const PlayerState &current, const QString &selectedDevice)
{
    if(!current.hasPlayback || !current.isPlaying || current.deviceId != selectedDevice || !current.track.isValid())
        return false;
    return previous.deviceId != selectedDevice || previous.track.id != current.track.id || !previous.isPlaying;
}
