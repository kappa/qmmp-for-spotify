#ifndef LINKEXPANDER_H
#define LINKEXPANDER_H

#include <QObject>
#include <QPointer>
#include "spotifytypes.h"

class PlayListModel;
class PlayListTrack;
struct ApiResult;

// Replaces raw Spotify links added to any playlist with fully tagged tracks.
class LinkExpander : public QObject
{
    Q_OBJECT
public:
    explicit LinkExpander(QObject *parent = nullptr);

private:
    void watch(PlayListModel *model);
    void onTracksAdded(PlayListModel *model, const QList<PlayListTrack *> &tracks);
    // static: the expander is recreated when settings are accepted, while requests are still in flight
    static void replace(QPointer<PlayListModel> model, PlayListTrack *track, const QString &rawPath, const ApiResult &result,
                 const QList<SpotifyTrack> &tracks);
};

#endif
