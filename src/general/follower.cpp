#include <qmmpui/mediaplayer.h>
#include <qmmpui/playlistmanager.h>
#include <qmmpui/playlistmodel.h>
#include "follower.h"
#include "followerlogic.h"
#include "spotifyclient.h"
#include "spotifypoller.h"
#include "trackbuilder.h"

Follower::Follower(QObject *parent) : QObject(parent)
{
    // queued: the engine handles the same state first and publishes its tracks
    connect(SpotifyClient::instance()->poller(), &SpotifyPoller::stateChanged, this, &Follower::onState, Qt::QueuedConnection);
}

void Follower::onState(const PlayerState &state)
{
    SpotifyClient *client = SpotifyClient::instance();
    PlayListModel *model = PlayListManager::instance()->currentPlayList();
    if(!model)
        return;

    QList<PlayListTrack *> tracks;
    QStringList paths;
    for(int i = 0; i < model->trackCount(); ++i)
    {
        PlayListTrack *track = model->findTrack(i);
        tracks << track;
        paths << (track ? track->path() : QString());
    }
    const int currentIndex = tracks.indexOf(model->currentTrack());

    const FollowDecision decision = decideFollow(state, client->deviceId(), client->engineTrackIds(),
                                                 client->isEngineAwaiting(), paths, currentIndex);
    if(decision.action == FollowDecision::None)
        return;

    PlayListTrack *target = nullptr;
    if(decision.action == FollowDecision::Select)
        target = tracks.at(decision.index);
    else
    {
        target = createPlayListTrack(state.track);
        model->addTrack(target);
    }
    MediaPlayer *player = MediaPlayer::instance();
    player->stop();
    model->setCurrent(target);
    player->play();
}
