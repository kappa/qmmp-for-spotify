#ifndef FOLLOWERLOGIC_H
#define FOLLOWERLOGIC_H

#include <QStringList>
#include "spotifytypes.h"

struct FollowDecision
{
    enum Action { None, Select, Append };

    Action action = None;
    int index = -1;
};

FollowDecision decideFollow(const PlayerState &state, const QString &selectedDevice, const QStringList &engineTrackIds,
                            const QString &suppressedTrackId, bool engineAwaiting, const QStringList &playlistPaths, int currentIndex);
bool endsSuppression(const PlayerState &state, const QString &selectedDevice, const QString &suppressedTrackId);
bool needsExpansion(const QString &path, const QString &title);
// Development Mode apps cannot read playlists owned by other users; Spotify answers 403 or 404.
// A broken link (400, or an ID that is not 22 base62 characters) "cannot be added".
QString expansionFailureMessage(const QString &path, int httpStatus, const QString &apiMessage);
// only failures about the link itself; after network, login, rate-limit or server errors the
// link is kept so it can still be played or expanded later
bool removesFailedLink(int httpStatus);
bool isFollowTrigger(const PlayerState &previous, const PlayerState &current, const QString &selectedDevice);

#endif
