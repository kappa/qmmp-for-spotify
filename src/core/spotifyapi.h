#ifndef SPOTIFYAPI_H
#define SPOTIFYAPI_H

#include <functional>
#include <QObject>
#include <QUrl>
#include <QUrlQuery>
#include "spotifytypes.h"

class QNetworkAccessManager;
class SpotifyAuth;

struct ApiResult
{
    bool ok = false;
    int status = 0;
    QString message;
    bool deviceProblem = false;
    QByteArray body;
    QString reason;
};

class SpotifyApi : public QObject
{
    Q_OBJECT
public:
    using Callback = std::function<void(const ApiResult &)>;
    template <typename T>
    using ListCallback = std::function<void(const ApiResult &, const QList<T> &)>;

    explicit SpotifyApi(SpotifyAuth *auth, QObject *parent = nullptr);

    void devices(ListCallback<SpotifyDevice> callback);
    void playerState(std::function<void(const ApiResult &, const PlayerState &)> callback);
    void displayName(std::function<void(const ApiResult &, const QString &)> callback);
    void userId(std::function<void(const ApiResult &, const QString &)> callback);
    void play(const QString &deviceId, const QStringList &uris, qint64 positionMs, Callback callback);
    void pause(const QString &deviceId, Callback callback);
    void resume(const QString &deviceId, Callback callback);
    void seek(const QString &deviceId, qint64 positionMs, Callback callback);
    void queue(const QString &deviceId, const QString &uri, Callback callback);
    void next(const QString &deviceId, Callback callback);
    void setVolume(const QString &deviceId, int percent, Callback callback);
    void myPlaylists(ListCallback<SpotifyPlaylist> callback);
    void playlistTracks(const QString &playlistId, ListCallback<SpotifyTrack> callback);
    void likedTracks(ListCallback<SpotifyTrack> callback);
    void albumTracks(const QString &albumId, ListCallback<SpotifyTrack> callback);
    void track(const QString &trackId, std::function<void(const ApiResult &, const SpotifyTrack &)> callback);

private:
    void request(const QByteArray &verb, const QUrl &url, const QByteArray &body, Callback callback, bool retried = false);
    template <typename T>
    void collect(const QUrl &url, std::function<SpotifyPage<T>(const QByteArray &)> parse, QList<T> items,
                 ListCallback<T> callback, int pages = 0);

    SpotifyAuth *m_auth;
    QNetworkAccessManager *m_network;
};

#endif
