#include <qmmpui/playlistmanager.h>
#include <qmmpui/playlistmodel.h>
#include "apipolicy.h"
#include "followerlogic.h"
#include "linkexpander.h"
#include "spotifyapi.h"
#include "spotifyclient.h"
#include "spotifylink.h"
#include "trackbuilder.h"

using namespace Qt::StringLiterals;

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
        const QString rawPath = track->path();
        auto done = [guarded, track, rawPath](const ApiResult &result, const QList<SpotifyTrack> &expanded) {
            replace(guarded, track, rawPath, result, expanded);
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

void LinkExpander::replace(QPointer<PlayListModel> model, PlayListTrack *track, const QString &rawPath,
                           const ApiResult &result, const QList<SpotifyTrack> &tracks)
{
    if(!model)
        return;
    if(!result.ok)
        SpotifyClient::instance()->notify(
            loadFailureMessage(u"links"_s, expansionFailureMessage(rawPath, result.status, result.message)));
    // track may have been deleted (or its address reused) while the request was in flight;
    // only trust it once we find it still live in the model, at the path we captured before asking.
    PlayListTrack *live = nullptr;
    for(int i = 0; i < model->trackCount(); ++i)
    {
        PlayListTrack *candidate = model->findTrack(i);
        if(candidate == track && candidate->path() == rawPath)
        {
            live = candidate;
            break;
        }
    }
    if(!live)
        return;
    if(!result.ok)
    {
        // a link Spotify rejects cannot be played either
        if(removesFailedLink(result.status))
            model->removeTrack(live);
        return;
    }
    const int index = model->indexOf(live);
    if(index < 0)
        return;
    const QList<PlayListTrack *> inserted = createPlayListTracks(tracks);
    model->insertTracks(index, inserted);
    if(!inserted.isEmpty() && model->currentTrack() == live)
        model->setCurrent(inserted.first());
    model->removeTrack(live);
}
