#include <qmmpui/playlistmanager.h>
#include <qmmpui/playlistmodel.h>
#include "followerlogic.h"
#include "linkexpander.h"
#include "spotifyapi.h"
#include "spotifyclient.h"
#include "spotifylink.h"
#include "trackbuilder.h"

LinkExpander::LinkExpander(QObject *parent) : QObject(parent)
{
    PlayListManager *manager = PlayListManager::instance();
    for(PlayListModel *model : manager->playLists())
        watch(model);
    connect(manager, &PlayListManager::playListAdded, this, [this, manager](int index) {
        watch(manager->playListAt(index));
    });
}

void LinkExpander::watch(PlayListModel *model)
{
    connect(model, &PlayListModel::tracksAdded, this, [this, model](const QList<PlayListTrack *> &tracks) {
        onTracksAdded(model, tracks);
    });
}

void LinkExpander::onTracksAdded(PlayListModel *model, const QList<PlayListTrack *> &tracks)
{
    SpotifyApi *api = SpotifyClient::instance()->api();
    QPointer<PlayListModel> guarded(model);
    for(PlayListTrack *track : tracks)
    {
        if(!needsExpansion(track->path(), track->value(Qmmp::TITLE)))
            continue;
        const SpotifyLink link = SpotifyLink::parse(track->path());
        auto done = [this, guarded, track](const ApiResult &result, const QList<SpotifyTrack> &expanded) {
            replace(guarded, track, result, expanded);
        };
        if(link.kind == SpotifyLink::Track)
            api->track(link.id, [done](const ApiResult &result, const SpotifyTrack &expanded) {
                done(result, expanded.isValid() ? QList<SpotifyTrack> { expanded } : QList<SpotifyTrack>());
            });
        else if(link.kind == SpotifyLink::Album)
            api->albumTracks(link.id, done);
        else
            api->playlistTracks(link.id, done);
    }
}

void LinkExpander::replace(QPointer<PlayListModel> model, PlayListTrack *track, const ApiResult &result,
                           const QList<SpotifyTrack> &tracks)
{
    if(!model)
        return;
    if(!result.ok)
    {
        SpotifyClient::instance()->notify(result.message);
        return;
    }
    const int index = model->indexOf(track);
    if(index < 0)
        return;
    model->insertTracks(index, createPlayListTracks(tracks));
    model->removeTrack(track);
}
