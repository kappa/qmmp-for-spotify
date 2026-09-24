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
                            bool engineAwaiting, const QStringList &playlistPaths, int currentIndex);
bool needsExpansion(const QString &path, const QString &title);
bool isFollowTrigger(const PlayerState &previous, const PlayerState &current, const QString &selectedDevice);

#endif
